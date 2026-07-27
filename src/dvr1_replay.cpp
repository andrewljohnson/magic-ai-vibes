#include "old_school/dvr1_replay.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_set>
#include <vector>

namespace old_school::probes {
namespace {

constexpr std::size_t kMaximumRecordBytes = 1U << 20U;
constexpr std::size_t kDvr1Players = 2;
constexpr std::size_t kPlayerCount = kDvr1Players;
constexpr std::size_t kMaximumPhysicalCards = 512;
constexpr std::size_t kMaximumPublicObjects = 512;
constexpr std::size_t kMaximumTextBytes = 4096;
constexpr std::size_t kMaximumTurnNumber = 1000000;

bool dvr1_finite_probability(double value) {
    return std::isfinite(value) && value >= 0.0 &&
           value <= 1.0;
}

bool dvr1_valid_deck(DeckId deck) {
    return static_cast<std::size_t>(deck) < kDeckCount;
}

bool dvr1_valid_phase(TurnPhase phase) {
    return static_cast<std::size_t>(phase) <=
           static_cast<std::size_t>(TurnPhase::SecondMain);
}

bool dvr1_valid_card(CardId card) {
    return static_cast<std::size_t>(card) < kCardCount;
}

bool dvr1_lower_hex(std::string_view value,
                    std::size_t expected_size) {
    return value.size() == expected_size &&
           std::all_of(
               value.begin(), value.end(),
               [](char character) {
                   return (character >= '0' &&
                           character <= '9') ||
                          (character >= 'a' &&
                           character <= 'f');
               });
}

std::vector<std::string> dvr1_sorted_unique(
    std::vector<std::string> values) {
    std::sort(values.begin(), values.end());
    if (values.empty() ||
        std::adjacent_find(values.begin(), values.end()) !=
            values.end()) {
        return {};
    }
    return values;
}

std::vector<std::string> dvr1_best_actions(
    const std::vector<BsrRootScore::ActionMean>& means,
    bool scout) {
    if (means.empty()) {
        return {};
    }
    double best = -std::numeric_limits<double>::infinity();
    for (const auto& row : means) {
        const double value =
            scout ? row.scout_mean : row.confirmation_mean;
        if (!dvr1_finite_probability(value)) {
            return {};
        }
        best = std::max(best, value);
    }
    std::vector<std::string> result;
    for (const auto& row : means) {
        const double value =
            scout ? row.scout_mean : row.confirmation_mean;
        if (value == best) {
            result.push_back(row.descriptor);
        }
    }
    return dvr1_sorted_unique(std::move(result));
}

bool dvr1_reference_accounting_valid(
    std::size_t action_count,
    std::size_t scout_worlds,
    std::size_t confirmation_worlds,
    std::size_t rollouts_per_world,
    std::size_t sampled_worlds,
    std::size_t rollout_evaluations,
    std::size_t terminal_evaluations,
    std::size_t bootstrapped_evaluations) {
    if (action_count == 0 || scout_worlds == 0 ||
        confirmation_worlds == 0 ||
        rollouts_per_world == 0 ||
        scout_worlds >
            std::numeric_limits<std::size_t>::max() -
                confirmation_worlds) {
        return false;
    }
    const std::size_t reference_worlds =
        scout_worlds + confirmation_worlds;
    if (reference_worlds >
            std::numeric_limits<std::size_t>::max() / 2 ||
        action_count >
            std::numeric_limits<std::size_t>::max() /
                reference_worlds ||
        action_count * reference_worlds >
            std::numeric_limits<std::size_t>::max() /
                rollouts_per_world ||
        action_count * reference_worlds *
                rollouts_per_world >
            std::numeric_limits<std::size_t>::max() / 2) {
        return false;
    }
    const std::size_t expected_rollouts =
        action_count * reference_worlds *
        rollouts_per_world * 2;
    return
        sampled_worlds == reference_worlds * 2 &&
        rollout_evaluations == expected_rollouts &&
        terminal_evaluations <= rollout_evaluations &&
        bootstrapped_evaluations ==
            rollout_evaluations - terminal_evaluations;
}

class Dvr1Reader {
  public:
    explicit Dvr1Reader(std::string_view bytes) : bytes_(bytes) {
        if (bytes.empty() || bytes.size() > kMaximumRecordBytes) {
            fail("record size is outside the DVR1 bound");
        }
    }

    std::string text(std::string_view name,
                     std::size_t maximum = kMaximumTextBytes) {
        expect_name(name);
        const std::size_t colon = bytes_.find(':', position_);
        if (colon == std::string_view::npos) {
            fail("truncated text length");
        }
        const std::string_view length_token =
            bytes_.substr(position_, colon - position_);
        const std::uint64_t length =
            parse_unsigned(length_token, "text length");
        if (length > maximum ||
            length >
                static_cast<std::uint64_t>(
                    bytes_.size() - colon - 1)) {
            fail("text length is outside the DVR1 bound");
        }
        position_ = colon + 1;
        const std::string value(
            bytes_.substr(
                position_, static_cast<std::size_t>(length)));
        position_ += static_cast<std::size_t>(length);
        require_newline();
        return value;
    }

    std::uint64_t unsigned64(std::string_view name) {
        return parse_unsigned(token(name), name);
    }

    std::size_t size(std::string_view name,
                     std::size_t maximum) {
        const std::uint64_t value = unsigned64(name);
        if (value > maximum ||
            value >
                std::numeric_limits<std::size_t>::max()) {
            fail("bounded size is out of range");
        }
        return static_cast<std::size_t>(value);
    }

    std::int64_t signed64(std::string_view name) {
        const std::string_view value = token(name);
        std::int64_t parsed = 0;
        const auto [end, error] = std::from_chars(
            value.data(), value.data() + value.size(), parsed);
        if (error != std::errc{} ||
            end != value.data() + value.size() ||
            std::to_string(parsed) != value) {
            fail("invalid signed integer");
        }
        return parsed;
    }

    int integer(std::string_view name) {
        const std::int64_t value = signed64(name);
        if (value < std::numeric_limits<int>::min() ||
            value > std::numeric_limits<int>::max()) {
            fail("integer is out of range");
        }
        return static_cast<int>(value);
    }

    bool boolean(std::string_view name) {
        const std::string_view value = token(name);
        if (value == "0") {
            return false;
        }
        if (value == "1") {
            return true;
        }
        fail("invalid boolean");
    }

    double real(std::string_view name) {
        const std::string_view value = token(name);
        std::istringstream input{std::string(value)};
        input.imbue(std::locale::classic());
        double parsed = 0.0;
        input >> parsed;
        if (!input || input.peek() != std::char_traits<char>::eof()) {
            fail("invalid real");
        }
        return parsed;
    }

    void require_end() const {
        if (position_ != bytes_.size()) {
            fail("trailing DVR1 fields");
        }
    }

  private:
    [[noreturn]] static void fail(std::string_view message) {
        throw std::invalid_argument(
            "invalid DVR1 record: " + std::string(message));
    }

    static std::uint64_t parse_unsigned(
        std::string_view value, std::string_view context) {
        std::uint64_t parsed = 0;
        const auto [end, error] = std::from_chars(
            value.data(), value.data() + value.size(), parsed);
        if (value.empty() || error != std::errc{} ||
            end != value.data() + value.size() ||
            std::to_string(parsed) != value) {
            fail(
                "invalid unsigned integer in " +
                std::string(context));
        }
        return parsed;
    }

    void expect_name(std::string_view name) {
        if (position_ + name.size() + 1 > bytes_.size() ||
            bytes_.substr(position_, name.size()) != name ||
            bytes_[position_ + name.size()] != '\t') {
            fail(
                "missing, duplicate, reordered, or unknown field '" +
                std::string(name) + "'");
        }
        position_ += name.size() + 1;
    }

    std::string_view token(std::string_view name) {
        expect_name(name);
        const std::size_t newline =
            bytes_.find('\n', position_);
        if (newline == std::string_view::npos) {
            fail("truncated scalar field");
        }
        const std::string_view result =
            bytes_.substr(position_, newline - position_);
        position_ = newline + 1;
        return result;
    }

    void require_newline() {
        if (position_ >= bytes_.size() ||
            bytes_[position_] != '\n') {
            fail("truncated text field");
        }
        ++position_;
    }

    std::string_view bytes_;
    std::size_t position_ = 0;
};

std::string indexed(std::string_view prefix,
                    std::size_t index) {
    return std::string(prefix) + std::to_string(index);
}

std::string field(std::string_view prefix,
                  std::size_t index,
                  std::string_view suffix) {
    return indexed(prefix, index) + "." + std::string(suffix);
}

CardId read_card(Dvr1Reader& reader, std::string_view name) {
    return static_cast<CardId>(
        reader.size(name, kCardCount - 1));
}

std::vector<CardId> read_cards(
    Dvr1Reader& reader, std::string_view prefix) {
    const std::size_t count = reader.size(
        std::string(prefix) + ".count",
        kMaximumPhysicalCards);
    std::vector<CardId> cards;
    cards.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        cards.push_back(read_card(
            reader,
            std::string(prefix) + "." +
                std::to_string(index)));
    }
    return cards;
}

Target read_target(Dvr1Reader& reader,
                   std::string_view prefix) {
    Target target;
    target.player = reader.size(
        std::string(prefix) + ".player",
        kDvr1Players - 1);
    if (reader.boolean(
            std::string(prefix) +
            ".creature.present")) {
        const PermanentId id = reader.unsigned64(
            std::string(prefix) + ".creature.id");
        if (id == 0) {
            throw std::invalid_argument(
                "invalid DVR1 record: zero creature target");
        }
        target.creature = id;
    }
    return target;
}

PublicPlayerState read_public_player(
    Dvr1Reader& reader, std::size_t player) {
    const std::string prefix =
        "player." + std::to_string(player);
    PublicPlayerState result;
    result.life = reader.integer(prefix + ".life");
    result.library_size = reader.size(
        prefix + ".library_count",
        kMaximumPhysicalCards);
    result.hand_size = reader.size(
        prefix + ".hand_count",
        kMaximumPhysicalCards);
    result.graveyard =
        read_cards(reader, prefix + ".graveyard");
    result.exile = read_cards(reader, prefix + ".exile");

    const std::size_t lands = reader.size(
        prefix + ".lands.count", kMaximumPublicObjects);
    result.lands.reserve(lands);
    for (std::size_t index = 0; index < lands; ++index) {
        const std::string item =
            prefix + ".lands." + std::to_string(index);
        result.lands.push_back({
            .card = read_card(reader, item + ".card"),
            .tapped = reader.boolean(item + ".tapped"),
        });
    }

    const std::size_t creatures = reader.size(
        prefix + ".creatures.count",
        kMaximumPublicObjects);
    result.creatures.reserve(creatures);
    for (std::size_t index = 0; index < creatures; ++index) {
        const std::string item =
            prefix + ".creatures." +
            std::to_string(index);
        const PermanentId id =
            reader.unsigned64(item + ".id");
        if (id == 0) {
            throw std::invalid_argument(
                "invalid DVR1 record: zero creature ID");
        }
        result.creatures.push_back({
            .id = id,
            .card = read_card(reader, item + ".card"),
            .tapped = reader.boolean(item + ".tapped"),
            .summoning_sick =
                reader.boolean(item + ".summoning_sick"),
            .damage = reader.integer(item + ".damage"),
            .temporary_power_bonus = reader.integer(
                item + ".temporary_power_bonus"),
            .temporary_toughness_bonus = reader.integer(
                item + ".temporary_toughness_bonus"),
            .exile_on_death_this_turn = reader.boolean(
                item + ".exile_on_death_this_turn"),
        });
    }

    const std::size_t artifacts = reader.size(
        prefix + ".artifacts.count",
        kMaximumPublicObjects);
    result.artifacts.reserve(artifacts);
    for (std::size_t index = 0; index < artifacts; ++index) {
        const std::string item =
            prefix + ".artifacts." +
            std::to_string(index);
        const PermanentId id =
            reader.unsigned64(item + ".id");
        if (id == 0) {
            throw std::invalid_argument(
                "invalid DVR1 record: zero artifact ID");
        }
        result.artifacts.push_back({
            .id = id,
            .card = read_card(reader, item + ".card"),
            .tapped = reader.boolean(item + ".tapped"),
        });
    }
    result.enchantments =
        read_cards(reader, prefix + ".enchantments");
    result.mana_pool.generic =
        reader.integer(prefix + ".mana.generic");
    result.mana_pool.green =
        reader.integer(prefix + ".mana.green");
    result.mana_pool.red =
        reader.integer(prefix + ".mana.red");
    result.mana_pool.blue =
        reader.integer(prefix + ".mana.blue");
    result.mana_pool.white =
        reader.integer(prefix + ".mana.white");
    result.land_played_this_turn = reader.boolean(
        prefix + ".land_played_this_turn");
    return result;
}

PriorityAction read_priority_action(
    Dvr1Reader& reader, std::string_view prefix) {
    PriorityAction result;
    result.kind = static_cast<PriorityActionKind>(
        reader.size(
            std::string(prefix) + ".kind",
            static_cast<std::size_t>(
                PriorityActionKind::ActivateMillstone)));
    result.card =
        read_card(reader, std::string(prefix) + ".card");
    if (reader.boolean(
            std::string(prefix) + ".target.present")) {
        result.target =
            read_target(reader, std::string(prefix) + ".target");
    }
    if (reader.boolean(
            std::string(prefix) +
            ".spell_target.present")) {
        const StackObjectId id = reader.unsigned64(
            std::string(prefix) + ".spell_target.id");
        if (id == 0) {
            throw std::invalid_argument(
                "invalid DVR1 record: zero spell target");
        }
        result.spell_target = id;
    }
    if (reader.boolean(
            std::string(prefix) +
            ".source_permanent.present")) {
        const PermanentId id = reader.unsigned64(
            std::string(prefix) +
            ".source_permanent.id");
        if (id == 0) {
            throw std::invalid_argument(
                "invalid DVR1 record: zero source permanent");
        }
        result.source_permanent = id;
    }
    result.x_value =
        reader.integer(std::string(prefix) + ".x_value");
    return result;
}

StackObject read_stack_object(
    Dvr1Reader& reader, std::size_t index) {
    const std::string prefix =
        "stack." + std::to_string(index);
    StackObject result;
    result.kind = static_cast<StackObjectKind>(
        reader.size(
            prefix + ".kind",
            static_cast<std::size_t>(
                StackObjectKind::ActivatedAbility)));
    result.id = reader.unsigned64(prefix + ".id");
    if (result.id == 0) {
        throw std::invalid_argument(
            "invalid DVR1 record: zero stack-object ID");
    }
    result.card = read_card(reader, prefix + ".card");
    result.controller = reader.size(
        prefix + ".controller", kDvr1Players - 1);
    if (reader.boolean(prefix + ".target.present")) {
        result.target = read_target(
            reader, prefix + ".target");
    }
    if (reader.boolean(
            prefix + ".spell_target.present")) {
        const StackObjectId target =
            reader.unsigned64(
                prefix + ".spell_target.id");
        if (target == 0) {
            throw std::invalid_argument(
                "invalid DVR1 record: zero stack spell target");
        }
        result.spell_target = target;
    }
    result.x_value = reader.integer(prefix + ".x_value");
    return result;
}

std::vector<CardId> expand_composition(
    const std::array<std::size_t, kCardCount>& composition) {
    std::vector<CardId> result;
    const std::size_t total = std::accumulate(
        composition.begin(), composition.end(),
        std::size_t{0});
    result.reserve(total);
    for (std::size_t card = 0; card < kCardCount; ++card) {
        result.insert(
            result.end(), composition[card],
            static_cast<CardId>(card));
    }
    return result;
}

void subtract_card(
    std::array<std::size_t, kCardCount>& remaining,
    CardId card) {
    const std::size_t index =
        static_cast<std::size_t>(card);
    if (index >= kCardCount || remaining[index] == 0) {
        throw std::invalid_argument(
            "invalid DVR1 record: physical card "
            "conservation failed");
    }
    --remaining[index];
}

void subtract_public_player(
    std::array<std::size_t, kCardCount>& remaining,
    const PublicPlayerState& player) {
    for (const CardId card : player.graveyard) {
        subtract_card(remaining, card);
    }
    for (const CardId card : player.exile) {
        subtract_card(remaining, card);
    }
    for (const LandPermanent& land : player.lands) {
        subtract_card(remaining, land.card);
    }
    for (const CreaturePermanent& creature :
         player.creatures) {
        subtract_card(remaining, creature.card);
    }
    for (const ArtifactPermanent& artifact :
         player.artifacts) {
        subtract_card(remaining, artifact.card);
    }
    for (const CardId card : player.enchantments) {
        subtract_card(remaining, card);
    }
}

std::vector<std::pair<std::string, PriorityAction>>
canonical_action_rows(
    const std::vector<PriorityAction>& actions) {
    std::vector<std::pair<std::string, PriorityAction>> rows;
    rows.reserve(actions.size());
    for (const PriorityAction& action : actions) {
        rows.emplace_back(
            stable_priority_action_descriptor(action), action);
    }
    std::sort(
        rows.begin(), rows.end(),
        [](const auto& left, const auto& right) {
            return left.first < right.first;
        });
    return rows;
}

bool dvr1_record_shape_valid(
    const Dvr1OwnerVisibleRecord& record) {
    if (record.schema != kDvr1CaptureSchema ||
        record.environment_revision !=
            kBsrEnvironmentRevision ||
        record.stable_id.empty() ||
        record.stable_id.size() > 512 ||
        !dvr1_lower_hex(
            record.production_model_fingerprint, 64) ||
        !dvr1_lower_hex(
            record.information_action_fingerprint, 16) ||
        !dvr1_valid_deck(record.owner_deck) ||
        !dvr1_valid_deck(record.opponent_deck) ||
        record.decision_owner >= kPlayerCount ||
        record.active_player >= kPlayerCount ||
        record.starting_player >= kPlayerCount ||
        record.provenance.tracked_seat !=
            record.decision_owner ||
        record.owner_on_play !=
            (record.starting_player ==
             record.decision_owner) ||
        record.provenance.tracked_starts !=
            record.owner_on_play ||
        !dvr1_valid_phase(record.phase) ||
        record.consecutive_passes < 0 ||
        record.consecutive_passes > 1 ||
        record.next_permanent_id == 0 ||
        record.next_stack_object_id == 0 ||
        record.next_permanent_id ==
            std::numeric_limits<PermanentId>::max() ||
        record.next_stack_object_id ==
            std::numeric_limits<StackObjectId>::max() ||
        record.failed_draw[0] ||
        record.failed_draw[1] ||
        record.stack.empty() ||
        record.players[record.decision_owner].hand_size !=
            record.owner_hand.size() ||
        record.owner_hand.size() > 40 ||
        record.legal_action_descriptors.size() < 2 ||
        record.legal_action_descriptors.size() >
            kBsrMaximumLegalActions ||
        record.legal_actions.size() !=
            record.legal_action_descriptors.size() ||
        record.reference_action_means.size() !=
            record.legal_action_descriptors.size() ||
        record.reference_model_fingerprint !=
            record.production_model_fingerprint ||
        record.reference_seed_base == 0 ||
        record.reference_scout_seed ==
            record.reference_confirmation_seed ||
        record.reference_scout_worlds !=
            kBsrScoutWorlds ||
        record.reference_confirmation_worlds !=
            kBsrConfirmationWorlds ||
        record.reference_horizon_turns !=
            kBsrReferenceHorizon ||
        record.reference_rollouts_per_world != 1 ||
        record.reference_evaluation_threads !=
            kBsrReferenceEvaluationThreads ||
        !dvr1_reference_accounting_valid(
            record.legal_action_descriptors.size(),
            record.reference_scout_worlds,
            record.reference_confirmation_worlds,
            record.reference_rollouts_per_world,
            record.reference_sampled_worlds,
            record.reference_rollout_evaluations,
            record.reference_terminal_evaluations,
            record.reference_bootstrapped_evaluations) ||
        !std::isfinite(record.reference_regret) ||
        record.reference_regret <= 0.0 ||
        !std::isfinite(record.paired_standard_error) ||
        record.paired_standard_error < 0.0 ||
        !std::isfinite(record.paired_lower_95)) {
        return false;
    }
    if (dvr1_sorted_unique(
            record.legal_action_descriptors) !=
            record.legal_action_descriptors ||
        dvr1_sorted_unique(
            record.reference_best_actions) !=
            record.reference_best_actions ||
        record.reference_best_actions.size() >
            record.legal_action_descriptors.size() ||
        !std::binary_search(
            record.legal_action_descriptors.begin(),
            record.legal_action_descriptors.end(),
            record.production_action_descriptor) ||
        std::binary_search(
            record.reference_best_actions.begin(),
            record.reference_best_actions.end(),
            record.production_action_descriptor)) {
        return false;
    }
    std::vector<std::string> mean_descriptors;
    mean_descriptors.reserve(
        record.reference_action_means.size());
    for (const auto& mean : record.reference_action_means) {
        if (!dvr1_finite_probability(mean.scout_mean) ||
            !dvr1_finite_probability(
                mean.confirmation_mean)) {
            return false;
        }
        mean_descriptors.push_back(mean.descriptor);
    }
    if (mean_descriptors !=
            record.legal_action_descriptors ||
        dvr1_best_actions(
            record.reference_action_means, true) !=
            record.reference_best_actions ||
        dvr1_best_actions(
            record.reference_action_means, false) !=
            record.reference_best_actions) {
        return false;
    }
    for (std::size_t action = 0;
         action < record.legal_actions.size(); ++action) {
        const PriorityAction& structured =
            record.legal_actions[action];
        if (static_cast<std::size_t>(structured.kind) >
                static_cast<std::size_t>(
                    PriorityActionKind::ActivateMillstone) ||
            !dvr1_valid_card(structured.card) ||
            (structured.target.has_value() &&
             structured.target->player >= kPlayerCount) ||
            record.legal_action_descriptors[action] !=
                stable_priority_action_descriptor(
                    structured)) {
            return false;
        }
    }
    for (const CardId card : record.owner_hand) {
        if (!dvr1_valid_card(card)) {
            return false;
        }
    }
    for (const auto& player : record.players) {
        if (player.library_size > 40 ||
            player.hand_size > 40 ||
            player.graveyard.size() > 40 ||
            player.exile.size() > 40 ||
            player.lands.size() > 40 ||
            player.creatures.size() > 40 ||
            player.artifacts.size() > 40 ||
            player.enchantments.size() > 40) {
            return false;
        }
        if (!std::all_of(
                player.graveyard.begin(),
                player.graveyard.end(), dvr1_valid_card) ||
            !std::all_of(
                player.exile.begin(), player.exile.end(),
                dvr1_valid_card) ||
            !std::all_of(
                player.enchantments.begin(),
                player.enchantments.end(),
                dvr1_valid_card)) {
            return false;
        }
        for (const LandPermanent& land : player.lands) {
            if (!dvr1_valid_card(land.card)) {
                return false;
            }
        }
        for (const CreaturePermanent& creature :
             player.creatures) {
            if (!dvr1_valid_card(creature.card)) {
                return false;
            }
        }
        for (const ArtifactPermanent& artifact :
             player.artifacts) {
            if (!dvr1_valid_card(artifact.card)) {
                return false;
            }
        }
    }
    PermanentId maximum_permanent_id = 0;
    std::unordered_set<PermanentId> permanent_ids;
    for (const auto& player : record.players) {
        for (const CreaturePermanent& creature :
             player.creatures) {
            if (creature.id == 0 ||
                !permanent_ids.insert(creature.id).second) {
                return false;
            }
            maximum_permanent_id =
                std::max(maximum_permanent_id, creature.id);
        }
        for (const ArtifactPermanent& artifact :
             player.artifacts) {
            if (artifact.id == 0 ||
                !permanent_ids.insert(artifact.id).second) {
                return false;
            }
            maximum_permanent_id =
                std::max(maximum_permanent_id, artifact.id);
        }
    }
    if (record.next_permanent_id <= maximum_permanent_id) {
        return false;
    }
    StackObjectId maximum_stack_object_id = 0;
    std::unordered_set<StackObjectId> stack_object_ids;
    if (record.stack.size() > kBsrMaximumLegalActions) {
        return false;
    }
    for (const StackObject& object : record.stack) {
        if (static_cast<std::size_t>(object.kind) >
                static_cast<std::size_t>(
                    StackObjectKind::ActivatedAbility) ||
            object.id == 0 ||
            !stack_object_ids.insert(object.id).second ||
            !dvr1_valid_card(object.card) ||
            object.controller >= kPlayerCount ||
            (object.target.has_value() &&
             object.target->player >= kPlayerCount)) {
            return false;
        }
        maximum_stack_object_id =
            std::max(maximum_stack_object_id, object.id);
    }
    if (record.next_stack_object_id <=
        maximum_stack_object_id) {
        return false;
    }
    for (const auto& composition :
         record.original_deck_composition) {
        std::size_t total = 0;
        for (const std::size_t count : composition) {
            if (count > 40 - total) {
                return false;
            }
            total += count;
        }
        if (total != 40) {
            return false;
        }
    }
    return true;
}

void dvr1_append_integer(std::string& output,
                         std::string_view name,
                         std::uint64_t value) {
    output.append(name);
    output.push_back('\t');
    output += std::to_string(value);
    output.push_back('\n');
}

void dvr1_append_signed(std::string& output,
                        std::string_view name,
                        std::int64_t value) {
    output.append(name);
    output.push_back('\t');
    output += std::to_string(value);
    output.push_back('\n');
}

void dvr1_append_bool(std::string& output,
                      std::string_view name, bool value) {
    output.append(name);
    output.push_back('\t');
    output.push_back(value ? '1' : '0');
    output.push_back('\n');
}

void dvr1_append_text(std::string& output,
                      std::string_view name,
                      std::string_view value) {
    output.append(name);
    output.push_back('\t');
    output += std::to_string(value.size());
    output.push_back(':');
    output.append(value);
    output.push_back('\n');
}

void dvr1_append_real(std::string& output,
                      std::string_view name, double value) {
    std::ostringstream formatted;
    formatted.imbue(std::locale::classic());
    formatted << std::setprecision(
                     std::numeric_limits<double>::max_digits10)
              << value;
    output.append(name);
    output.push_back('\t');
    output += formatted.str();
    output.push_back('\n');
}

std::string dvr1_field(std::string_view prefix,
                       std::size_t first,
                       std::string_view suffix) {
    return std::string(prefix) + std::to_string(first) +
           "." + std::string(suffix);
}

std::string dvr1_nested_field(
    std::string_view prefix, std::size_t first,
    std::string_view middle, std::size_t second) {
    return std::string(prefix) + std::to_string(first) +
           "." + std::string(middle) +
           std::to_string(second);
}

void dvr1_serialize_cards(
    std::string& output, std::string_view prefix,
    const std::vector<CardId>& cards) {
    dvr1_append_integer(
        output, std::string(prefix) + ".count",
        cards.size());
    for (std::size_t index = 0; index < cards.size();
         ++index) {
        dvr1_append_integer(
            output,
            std::string(prefix) + "." +
                std::to_string(index),
            static_cast<std::size_t>(cards[index]));
    }
}

void dvr1_serialize_public_player(
    std::string& output, std::size_t index,
    const PublicPlayerState& player) {
    const std::string prefix =
        "player." + std::to_string(index);
    dvr1_append_signed(output, prefix + ".life", player.life);
    dvr1_append_integer(
        output, prefix + ".library_count",
        player.library_size);
    dvr1_append_integer(
        output, prefix + ".hand_count", player.hand_size);
    dvr1_serialize_cards(
        output, prefix + ".graveyard", player.graveyard);
    dvr1_serialize_cards(
        output, prefix + ".exile", player.exile);

    dvr1_append_integer(
        output, prefix + ".lands.count",
        player.lands.size());
    for (std::size_t permanent = 0;
         permanent < player.lands.size(); ++permanent) {
        const std::string land =
            prefix + ".lands." +
            std::to_string(permanent);
        dvr1_append_integer(
            output, land + ".card",
            static_cast<std::size_t>(
                player.lands[permanent].card));
        dvr1_append_bool(
            output, land + ".tapped",
            player.lands[permanent].tapped);
    }

    dvr1_append_integer(
        output, prefix + ".creatures.count",
        player.creatures.size());
    for (std::size_t permanent = 0;
         permanent < player.creatures.size();
         ++permanent) {
        const CreaturePermanent& creature =
            player.creatures[permanent];
        const std::string field =
            prefix + ".creatures." +
            std::to_string(permanent);
        dvr1_append_integer(
            output, field + ".id", creature.id);
        dvr1_append_integer(
            output, field + ".card",
            static_cast<std::size_t>(creature.card));
        dvr1_append_bool(
            output, field + ".tapped", creature.tapped);
        dvr1_append_bool(
            output, field + ".summoning_sick",
            creature.summoning_sick);
        dvr1_append_signed(
            output, field + ".damage", creature.damage);
        dvr1_append_signed(
            output, field + ".temporary_power_bonus",
            creature.temporary_power_bonus);
        dvr1_append_signed(
            output, field + ".temporary_toughness_bonus",
            creature.temporary_toughness_bonus);
        dvr1_append_bool(
            output, field + ".exile_on_death_this_turn",
            creature.exile_on_death_this_turn);
    }

    dvr1_append_integer(
        output, prefix + ".artifacts.count",
        player.artifacts.size());
    for (std::size_t permanent = 0;
         permanent < player.artifacts.size();
         ++permanent) {
        const ArtifactPermanent& artifact =
            player.artifacts[permanent];
        const std::string field =
            prefix + ".artifacts." +
            std::to_string(permanent);
        dvr1_append_integer(
            output, field + ".id", artifact.id);
        dvr1_append_integer(
            output, field + ".card",
            static_cast<std::size_t>(artifact.card));
        dvr1_append_bool(
            output, field + ".tapped", artifact.tapped);
    }
    dvr1_serialize_cards(
        output, prefix + ".enchantments",
        player.enchantments);
    dvr1_append_signed(
        output, prefix + ".mana.generic",
        player.mana_pool.generic);
    dvr1_append_signed(
        output, prefix + ".mana.green",
        player.mana_pool.green);
    dvr1_append_signed(
        output, prefix + ".mana.red",
        player.mana_pool.red);
    dvr1_append_signed(
        output, prefix + ".mana.blue",
        player.mana_pool.blue);
    dvr1_append_signed(
        output, prefix + ".mana.white",
        player.mana_pool.white);
    dvr1_append_bool(
        output, prefix + ".land_played_this_turn",
        player.land_played_this_turn);
}

void dvr1_serialize_priority_action(
    std::string& output, std::string_view prefix,
    const PriorityAction& action) {
    dvr1_append_integer(
        output, std::string(prefix) + ".kind",
        static_cast<std::size_t>(action.kind));
    dvr1_append_integer(
        output, std::string(prefix) + ".card",
        static_cast<std::size_t>(action.card));
    dvr1_append_bool(
        output, std::string(prefix) + ".target.present",
        action.target.has_value());
    if (action.target.has_value()) {
        dvr1_append_integer(
            output, std::string(prefix) + ".target.player",
            action.target->player);
        dvr1_append_bool(
            output,
            std::string(prefix) +
                ".target.creature.present",
            action.target->creature.has_value());
        if (action.target->creature.has_value()) {
            dvr1_append_integer(
                output,
                std::string(prefix) +
                    ".target.creature.id",
                *action.target->creature);
        }
    }
    dvr1_append_bool(
        output,
        std::string(prefix) + ".spell_target.present",
        action.spell_target.has_value());
    if (action.spell_target.has_value()) {
        dvr1_append_integer(
            output,
            std::string(prefix) + ".spell_target.id",
            *action.spell_target);
    }
    dvr1_append_bool(
        output,
        std::string(prefix) +
            ".source_permanent.present",
        action.source_permanent.has_value());
    if (action.source_permanent.has_value()) {
        dvr1_append_integer(
            output,
            std::string(prefix) +
                ".source_permanent.id",
            *action.source_permanent);
    }
    dvr1_append_signed(
        output, std::string(prefix) + ".x_value",
        action.x_value);
}

} // namespace

std::string serialize_dvr1_owner_visible_record(
    const Dvr1OwnerVisibleRecord& record) {
    if (!dvr1_record_shape_valid(record)) {
        throw std::invalid_argument(
            "invalid DVR1 owner-visible record");
    }

    std::string output;
    output.reserve(8192);
    dvr1_append_text(output, "schema", record.schema);
    dvr1_append_text(
        output, "environment_revision",
        record.environment_revision);
    dvr1_append_text(output, "stable_id", record.stable_id);
    dvr1_append_text(
        output, "production_model_fingerprint",
        record.production_model_fingerprint);
    dvr1_append_text(
        output, "information_action_fingerprint",
        record.information_action_fingerprint);
    dvr1_append_integer(
        output, "source.game_seed",
        record.provenance.game_seed);
    dvr1_append_integer(
        output, "source.block", record.provenance.block);
    dvr1_append_integer(
        output, "source.schedule_index",
        record.provenance.schedule_index);
    dvr1_append_integer(
        output, "source.trace_ordinal",
        record.provenance.trace_ordinal);
    dvr1_append_integer(
        output, "source.tracked_seat",
        record.provenance.tracked_seat);
    dvr1_append_bool(
        output, "source.tracked_starts",
        record.provenance.tracked_starts);
    dvr1_append_integer(
        output, "decision.owner", record.decision_owner);
    dvr1_append_integer(
        output, "decision.owner_deck",
        static_cast<std::size_t>(record.owner_deck));
    dvr1_append_integer(
        output, "decision.opponent_deck",
        static_cast<std::size_t>(record.opponent_deck));
    dvr1_append_integer(
        output, "decision.active_player",
        record.active_player);
    dvr1_append_integer(
        output, "decision.starting_player",
        record.starting_player);
    dvr1_append_bool(
        output, "decision.owner_on_play",
        record.owner_on_play);
    dvr1_append_integer(
        output, "decision.turn_number",
        record.turn_number);
    dvr1_append_integer(
        output, "decision.phase",
        static_cast<std::size_t>(record.phase));
    dvr1_append_signed(
        output, "decision.consecutive_passes",
        record.consecutive_passes);
    dvr1_append_integer(
        output, "state.next_permanent_id",
        record.next_permanent_id);
    dvr1_append_integer(
        output, "state.next_stack_object_id",
        record.next_stack_object_id);
    for (std::size_t player = 0; player < kPlayerCount;
         ++player) {
        dvr1_append_bool(
            output,
            "state.failed_draw." +
                std::to_string(player),
            record.failed_draw[player]);
    }
    for (std::size_t player = 0; player < kPlayerCount;
         ++player) {
        dvr1_serialize_public_player(
            output, player, record.players[player]);
    }
    dvr1_serialize_cards(
        output, "owner.hand", record.owner_hand);
    for (std::size_t player = 0; player < kPlayerCount;
         ++player) {
        dvr1_append_integer(
            output,
            dvr1_field(
                "player.", player,
                "extra_turns_pending"),
            record.extra_turns_pending[player]);
    }

    dvr1_append_integer(
        output, "stack.count", record.stack.size());
    for (std::size_t index = 0; index < record.stack.size();
         ++index) {
        const StackObject& object = record.stack[index];
        const std::string prefix =
            "stack." + std::to_string(index);
        dvr1_append_integer(
            output, prefix + ".kind",
            static_cast<std::size_t>(object.kind));
        dvr1_append_integer(
            output, prefix + ".id", object.id);
        dvr1_append_integer(
            output, prefix + ".card",
            static_cast<std::size_t>(object.card));
        dvr1_append_integer(
            output, prefix + ".controller",
            object.controller);
        dvr1_append_bool(
            output, prefix + ".target.present",
            object.target.has_value());
        if (object.target.has_value()) {
            dvr1_append_integer(
                output, prefix + ".target.player",
                object.target->player);
            dvr1_append_bool(
                output,
                prefix + ".target.creature.present",
                object.target->creature.has_value());
            if (object.target->creature.has_value()) {
                dvr1_append_integer(
                    output,
                    prefix + ".target.creature.id",
                    *object.target->creature);
            }
        }
        dvr1_append_bool(
            output, prefix + ".spell_target.present",
            object.spell_target.has_value());
        if (object.spell_target.has_value()) {
            dvr1_append_integer(
                output, prefix + ".spell_target.id",
                *object.spell_target);
        }
        dvr1_append_signed(
            output, prefix + ".x_value", object.x_value);
    }

    for (std::size_t player = 0; player < kPlayerCount;
         ++player) {
        for (std::size_t card = 0; card < kCardCount;
             ++card) {
            dvr1_append_integer(
                output,
                dvr1_nested_field(
                    "original_deck.", player,
                    "card.", card),
                record.original_deck_composition[player][card]);
        }
    }

    dvr1_append_integer(
        output, "legal_actions.count",
        record.legal_action_descriptors.size());
    for (std::size_t index = 0;
         index < record.legal_action_descriptors.size();
         ++index) {
        const std::string prefix =
            "legal_actions." + std::to_string(index);
        dvr1_append_text(
            output, prefix + ".descriptor",
            record.legal_action_descriptors[index]);
        dvr1_serialize_priority_action(
            output, prefix + ".action",
            record.legal_actions[index]);
    }
    dvr1_append_text(
        output, "production_action",
        record.production_action_descriptor);
    dvr1_append_integer(
        output, "reference_best.count",
        record.reference_best_actions.size());
    for (std::size_t index = 0;
         index < record.reference_best_actions.size();
         ++index) {
        dvr1_append_text(
            output,
            "reference_best." + std::to_string(index),
            record.reference_best_actions[index]);
    }
    for (std::size_t index = 0;
         index < record.reference_action_means.size();
         ++index) {
        const auto& mean =
            record.reference_action_means[index];
        const std::string prefix =
            "reference_action." + std::to_string(index);
        dvr1_append_text(
            output, prefix + ".descriptor",
            mean.descriptor);
        dvr1_append_real(
            output, prefix + ".scout_q_mean",
            mean.scout_mean);
        dvr1_append_real(
            output, prefix + ".confirmation_q_mean",
            mean.confirmation_mean);
    }
    dvr1_append_text(
        output, "reference.model_fingerprint",
        record.reference_model_fingerprint);
    dvr1_append_integer(
        output, "reference.seed_base",
        record.reference_seed_base);
    dvr1_append_integer(
        output, "reference.scout_seed",
        record.reference_scout_seed);
    dvr1_append_integer(
        output, "reference.confirmation_seed",
        record.reference_confirmation_seed);
    dvr1_append_integer(
        output, "reference.scout_worlds",
        record.reference_scout_worlds);
    dvr1_append_integer(
        output, "reference.confirmation_worlds",
        record.reference_confirmation_worlds);
    dvr1_append_integer(
        output, "reference.horizon_turns",
        record.reference_horizon_turns);
    dvr1_append_integer(
        output, "reference.rollouts_per_world",
        record.reference_rollouts_per_world);
    dvr1_append_integer(
        output, "reference.evaluation_threads",
        record.reference_evaluation_threads);
    dvr1_append_integer(
        output, "reference.sampled_worlds",
        record.reference_sampled_worlds);
    dvr1_append_integer(
        output, "reference.rollout_evaluations",
        record.reference_rollout_evaluations);
    dvr1_append_integer(
        output, "reference.terminal_evaluations",
        record.reference_terminal_evaluations);
    dvr1_append_integer(
        output, "reference.bootstrapped_evaluations",
        record.reference_bootstrapped_evaluations);
    dvr1_append_real(
        output, "reference.regret",
        record.reference_regret);
    dvr1_append_real(
        output, "reference.paired_standard_error",
        record.paired_standard_error);
    dvr1_append_real(
        output, "reference.paired_lower_95",
        record.paired_lower_95);
    return output;
}

Dvr1OwnerVisibleRecord deserialize_dvr1_owner_visible_record(
    std::string_view bytes) {
    Dvr1Reader reader(bytes);
    Dvr1OwnerVisibleRecord record;
    record.schema = reader.text("schema", 128);
    record.environment_revision =
        reader.text("environment_revision", 256);
    record.stable_id = reader.text("stable_id", 512);
    record.production_model_fingerprint =
        reader.text("production_model_fingerprint", 128);
    record.information_action_fingerprint =
        reader.text("information_action_fingerprint", 64);
    record.provenance.game_seed =
        reader.unsigned64("source.game_seed");
    record.provenance.block = reader.size(
        "source.block", kMaximumTurnNumber);
    record.provenance.schedule_index = reader.size(
        "source.schedule_index", kMaximumTurnNumber);
    record.provenance.trace_ordinal = reader.size(
        "source.trace_ordinal", kMaximumTurnNumber);
    record.provenance.tracked_seat = reader.size(
        "source.tracked_seat", kDvr1Players - 1);
    record.provenance.tracked_starts =
        reader.boolean("source.tracked_starts");
    record.decision_owner =
        reader.size("decision.owner", kDvr1Players - 1);
    record.owner_deck = static_cast<DeckId>(
        reader.size(
            "decision.owner_deck", kDeckCount - 1));
    record.opponent_deck = static_cast<DeckId>(
        reader.size(
            "decision.opponent_deck", kDeckCount - 1));
    record.active_player = reader.size(
        "decision.active_player", kDvr1Players - 1);
    record.starting_player = reader.size(
        "decision.starting_player", kDvr1Players - 1);
    record.owner_on_play =
        reader.boolean("decision.owner_on_play");
    record.turn_number = reader.size(
        "decision.turn_number", kMaximumTurnNumber);
    record.phase = static_cast<TurnPhase>(
        reader.size(
            "decision.phase",
            static_cast<std::size_t>(
                TurnPhase::SecondMain)));
    record.consecutive_passes =
        reader.integer("decision.consecutive_passes");
    record.next_permanent_id =
        reader.unsigned64("state.next_permanent_id");
    record.next_stack_object_id =
        reader.unsigned64("state.next_stack_object_id");
    for (std::size_t player = 0; player < kDvr1Players;
         ++player) {
        record.failed_draw[player] = reader.boolean(
            indexed("state.failed_draw.", player));
    }
    for (std::size_t player = 0; player < kDvr1Players;
         ++player) {
        record.players[player] =
            read_public_player(reader, player);
    }
    record.owner_hand = read_cards(reader, "owner.hand");
    for (std::size_t player = 0; player < kDvr1Players;
         ++player) {
        record.extra_turns_pending[player] = reader.size(
            field(
                "player.", player,
                "extra_turns_pending"),
            kMaximumTurnNumber);
    }

    const std::size_t stack_count = reader.size(
        "stack.count", kMaximumPublicObjects);
    record.stack.reserve(stack_count);
    for (std::size_t index = 0; index < stack_count;
         ++index) {
        record.stack.push_back(
            read_stack_object(reader, index));
    }

    for (std::size_t player = 0; player < kDvr1Players;
         ++player) {
        std::size_t total = 0;
        for (std::size_t card = 0; card < kCardCount;
             ++card) {
            const std::size_t count = reader.size(
                "original_deck." +
                    std::to_string(player) + ".card." +
                    std::to_string(card),
                kMaximumPhysicalCards);
            if (count >
                kMaximumPhysicalCards - total) {
                throw std::invalid_argument(
                    "invalid DVR1 record: deck size overflow");
            }
            record.original_deck_composition[player][card] =
                count;
            total += count;
        }
        if (total == 0) {
            throw std::invalid_argument(
                "invalid DVR1 record: empty original deck");
        }
    }

    const std::size_t action_count = reader.size(
        "legal_actions.count", kBsrMaximumLegalActions);
    if (action_count < 2) {
        throw std::invalid_argument(
            "invalid DVR1 record: incomplete action set");
    }
    record.legal_action_descriptors.reserve(action_count);
    record.legal_actions.reserve(action_count);
    for (std::size_t index = 0; index < action_count;
         ++index) {
        const std::string prefix =
            "legal_actions." + std::to_string(index);
        record.legal_action_descriptors.push_back(
            reader.text(prefix + ".descriptor"));
        record.legal_actions.push_back(
            read_priority_action(
                reader, prefix + ".action"));
    }
    record.production_action_descriptor =
        reader.text("production_action");
    const std::size_t best_count = reader.size(
        "reference_best.count", action_count);
    record.reference_best_actions.reserve(best_count);
    for (std::size_t index = 0; index < best_count;
         ++index) {
        record.reference_best_actions.push_back(
            reader.text(
                "reference_best." +
                std::to_string(index)));
    }
    record.reference_action_means.reserve(action_count);
    for (std::size_t index = 0; index < action_count;
         ++index) {
        const std::string prefix =
            "reference_action." +
            std::to_string(index);
        record.reference_action_means.push_back({
            .descriptor =
                reader.text(prefix + ".descriptor"),
            .scout_mean =
                reader.real(prefix + ".scout_q_mean"),
            .confirmation_mean = reader.real(
                prefix + ".confirmation_q_mean"),
        });
    }
    record.reference_model_fingerprint =
        reader.text("reference.model_fingerprint", 128);
    record.reference_seed_base =
        reader.unsigned64("reference.seed_base");
    record.reference_scout_seed =
        reader.unsigned64("reference.scout_seed");
    record.reference_confirmation_seed =
        reader.unsigned64("reference.confirmation_seed");
    record.reference_scout_worlds = reader.size(
        "reference.scout_worlds",
        kMaximumPhysicalCards);
    record.reference_confirmation_worlds = reader.size(
        "reference.confirmation_worlds",
        kMaximumPhysicalCards);
    record.reference_horizon_turns = reader.size(
        "reference.horizon_turns", kMaximumTurnNumber);
    record.reference_rollouts_per_world = reader.size(
        "reference.rollouts_per_world",
        kMaximumPhysicalCards);
    record.reference_evaluation_threads = reader.size(
        "reference.evaluation_threads",
        kMaximumPhysicalCards);
    record.reference_sampled_worlds = reader.size(
        "reference.sampled_worlds",
        kMaximumPhysicalCards * 4);
    record.reference_rollout_evaluations = reader.size(
        "reference.rollout_evaluations",
        kMaximumPhysicalCards *
            kMaximumPhysicalCards * 4);
    record.reference_terminal_evaluations = reader.size(
        "reference.terminal_evaluations",
        kMaximumPhysicalCards *
            kMaximumPhysicalCards * 4);
    record.reference_bootstrapped_evaluations =
        reader.size(
            "reference.bootstrapped_evaluations",
            kMaximumPhysicalCards *
                kMaximumPhysicalCards * 4);
    record.reference_regret =
        reader.real("reference.regret");
    record.paired_standard_error =
        reader.real("reference.paired_standard_error");
    record.paired_lower_95 =
        reader.real("reference.paired_lower_95");
    reader.require_end();

    if (serialize_dvr1_owner_visible_record(record) != bytes) {
        throw std::invalid_argument(
            "invalid DVR1 record: noncanonical encoding");
    }
    static_cast<void>(
        rehydrate_dvr1_decision_probe(record));
    return record;
}

DecisionProbe rehydrate_dvr1_decision_probe(
    const Dvr1OwnerVisibleRecord& record) {
    // The serializer is the single schema/shape validator for both direct
    // records and decoded records.
    static_cast<void>(
        serialize_dvr1_owner_visible_record(record));

    DecisionProbe probe;
    probe.stable_id = record.stable_id;
    probe.category = Category::BlueCounterWar;
    probe.decision_kind = DecisionKind::Priority;
    probe.root_deck = record.owner_deck;
    probe.opponent_deck = record.opponent_deck;
    probe.root_player = record.decision_owner;
    probe.phase = record.phase;
    probe.consecutive_passes =
        record.consecutive_passes;
    probe.candidates.reserve(record.legal_actions.size());
    for (std::size_t index = 0;
         index < record.legal_actions.size(); ++index) {
        probe.candidates.push_back({
            .descriptor =
                record.legal_action_descriptors[index],
            .action = record.legal_actions[index],
        });
    }

    GameState& state = probe.state;
    state.stack = record.stack;
    state.extra_turns_pending =
        record.extra_turns_pending;
    state.failed_draw = record.failed_draw;
    state.active_player = record.active_player;
    state.starting_player = record.starting_player;
    state.turn_number = record.turn_number;
    state.next_permanent_id =
        record.next_permanent_id;
    state.next_stack_object_id =
        record.next_stack_object_id;
    // Reporting counters are deliberately outside the information set.
    state.stats = {};

    for (std::size_t player = 0; player < kDvr1Players;
         ++player) {
        probe.original_decks[player] = expand_composition(
            record.original_deck_composition[player]);
        const PublicPlayerState& source =
            record.players[player];
        PlayerState& destination = state.players[player];
        destination.life = source.life;
        destination.graveyard = source.graveyard;
        destination.exile = source.exile;
        destination.lands = source.lands;
        destination.creatures = source.creatures;
        destination.artifacts = source.artifacts;
        destination.enchantments = source.enchantments;
        destination.mana_pool = source.mana_pool;
        destination.land_played_this_turn =
            source.land_played_this_turn;

        auto remaining =
            record.original_deck_composition[player];
        subtract_public_player(remaining, source);
        for (const StackObject& object : record.stack) {
            if (object.controller == player &&
                object.kind == StackObjectKind::Spell) {
                subtract_card(remaining, object.card);
            }
        }
        if (player == record.decision_owner) {
            destination.hand = record.owner_hand;
            for (const CardId card : destination.hand) {
                subtract_card(remaining, card);
            }
            destination.library =
                expand_composition(remaining);
            if (destination.library.size() !=
                source.library_size) {
                throw std::invalid_argument(
                    "invalid DVR1 record: owner hidden count "
                    "mismatch");
            }
        } else {
            std::vector<CardId> hidden =
                expand_composition(remaining);
            if (hidden.size() !=
                source.hand_size +
                    source.library_size) {
                throw std::invalid_argument(
                    "invalid DVR1 record: opponent hidden count "
                    "mismatch");
            }
            const auto hand_end =
                hidden.begin() +
                static_cast<std::ptrdiff_t>(
                    source.hand_size);
            destination.hand.assign(
                hidden.begin(), hand_end);
            destination.library.assign(
                hand_end, hidden.end());
        }
    }

    const PlayerObservation observation =
        observe_game_state(
            probe.state, probe.root_player);
    if (observation.players != record.players ||
        observation.hand != record.owner_hand ||
        observation.revealed_opponent_hand.has_value() ||
        observation.stack != record.stack ||
        observation.extra_turns_pending !=
            record.extra_turns_pending ||
        observation.active_player !=
            record.active_player ||
        observation.starting_player !=
            record.starting_player ||
        observation.turn_number != record.turn_number) {
        throw std::invalid_argument(
            "invalid DVR1 record: owner-visible replay mismatch");
    }

    const bool sorcery_actions =
        record.phase == TurnPhase::FirstMain ||
        record.phase == TurnPhase::SecondMain;
    const auto expected_rows = canonical_action_rows(
        legal_priority_actions(
            probe.state, probe.root_player,
            sorcery_actions));
    std::vector<std::pair<std::string, PriorityAction>>
        recorded_rows;
    recorded_rows.reserve(record.legal_actions.size());
    for (std::size_t index = 0;
         index < record.legal_actions.size(); ++index) {
        recorded_rows.emplace_back(
            record.legal_action_descriptors[index],
            record.legal_actions[index]);
    }
    if (expected_rows != recorded_rows ||
        static_cast<std::size_t>(std::count(
            record.legal_action_descriptors.begin(),
            record.legal_action_descriptors.end(),
            record.production_action_descriptor)) != 1 ||
        bsr_information_action_fingerprint(probe) !=
            record.information_action_fingerprint ||
        state.stats !=
            std::array<PlayerGameStats, kDvr1Players>{}) {
        throw std::invalid_argument(
            "invalid DVR1 record: action/fingerprint replay "
            "mismatch");
    }
    const Validation validation =
        validate_probe(probe, record.reference_seed_base);
    if (!validation.ok()) {
        std::string message =
            "invalid DVR1 record: rehydrated probe validation "
            "failed";
        for (const std::string& error : validation.errors) {
            message += "; " + error;
        }
        throw std::invalid_argument(message);
    }
    return probe;
}

} // namespace old_school::probes
