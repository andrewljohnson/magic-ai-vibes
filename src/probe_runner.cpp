#include "alpha/probe_runner.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace alpha::probe_runner {
namespace {

constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr std::size_t kProductionActorWorlds = 2;
constexpr std::size_t kProductionActorHorizon = 0;
constexpr std::size_t kProductionValueWorlds = 2;
constexpr std::size_t kProductionValueHorizon = 4;
constexpr std::size_t kMaximumReferenceWorlds = 4096;
constexpr std::size_t kMaximumReferenceHorizon = 128;
constexpr std::size_t kMaximumReferenceRollouts = 256;

class Fnv1a {
  public:
    void byte(std::uint8_t value) {
        value_ ^= static_cast<std::uint64_t>(value);
        value_ *= kFnvPrime;
    }

    void unsigned_integer(std::uint64_t value) {
        for (std::size_t byte_index = 0;
             byte_index < sizeof(value); ++byte_index) {
            byte(static_cast<std::uint8_t>(
                value >> (byte_index * 8U)));
        }
    }

    void signed_integer(std::int64_t value) {
        unsigned_integer(std::bit_cast<std::uint64_t>(value));
    }

    void boolean(bool value) {
        byte(value ? 1U : 0U);
    }

    void text(std::string_view value) {
        unsigned_integer(
            static_cast<std::uint64_t>(value.size()));
        for (const unsigned char character : value) {
            byte(character);
        }
    }

    std::uint64_t value() const {
        return value_;
    }

  private:
    std::uint64_t value_ = kFnvOffsetBasis;
};

std::string hex_u64(std::uint64_t value) {
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16)
           << value;
    return output.str();
}

void hash_target(Fnv1a& hash, const std::optional<Target>& target) {
    hash.boolean(target.has_value());
    if (!target.has_value()) {
        return;
    }
    hash.unsigned_integer(target->player);
    hash.boolean(target->creature.has_value());
    if (target->creature.has_value()) {
        hash.unsigned_integer(*target->creature);
    }
}

void hash_priority_action(Fnv1a& hash,
                          const PriorityAction& action) {
    hash.unsigned_integer(
        static_cast<std::uint64_t>(action.kind));
    hash.unsigned_integer(
        static_cast<std::uint64_t>(action.card));
    hash_target(hash, action.target);
    hash.boolean(action.spell_target.has_value());
    if (action.spell_target.has_value()) {
        hash.unsigned_integer(*action.spell_target);
    }
    hash.boolean(action.source_permanent.has_value());
    if (action.source_permanent.has_value()) {
        hash.unsigned_integer(*action.source_permanent);
    }
}

void hash_card_vector(Fnv1a& hash,
                      const std::vector<CardId>& cards) {
    hash.unsigned_integer(cards.size());
    for (const CardId card : cards) {
        hash.unsigned_integer(
            static_cast<std::uint64_t>(card));
    }
}

void hash_public_player(Fnv1a& hash,
                        const PlayerState& player) {
    hash.signed_integer(player.life);
    hash_card_vector(hash, player.graveyard);

    hash.unsigned_integer(player.lands.size());
    for (const LandPermanent& land : player.lands) {
        hash.unsigned_integer(
            static_cast<std::uint64_t>(land.card));
        hash.boolean(land.tapped);
    }

    hash.unsigned_integer(player.creatures.size());
    for (const CreaturePermanent& creature : player.creatures) {
        hash.unsigned_integer(creature.id);
        hash.unsigned_integer(
            static_cast<std::uint64_t>(creature.card));
        hash.boolean(creature.tapped);
        hash.boolean(creature.summoning_sick);
        hash.signed_integer(creature.damage);
    }

    hash.unsigned_integer(player.artifacts.size());
    for (const ArtifactPermanent& artifact : player.artifacts) {
        hash.unsigned_integer(artifact.id);
        hash.unsigned_integer(
            static_cast<std::uint64_t>(artifact.card));
        hash.boolean(artifact.tapped);
    }
    hash_card_vector(hash, player.enchantments);
    hash.boolean(player.land_played_this_turn);
}

void hash_stats(Fnv1a& hash, const PlayerGameStats& stats) {
    hash.unsigned_integer(stats.cards_drawn);
    hash.unsigned_integer(stats.lands_played);
    hash.unsigned_integer(stats.spells_cast);
    hash.unsigned_integer(stats.spells_countered);
    hash.unsigned_integer(stats.damage_to_opponent);
    hash.unsigned_integer(stats.cards_milled);
    hash.unsigned_integer(stats.decisions);
    hash.unsigned_integer(stats.monte_carlo_rollouts);
}

void hash_probe(Fnv1a& hash,
                const probes::DecisionProbe& probe) {
    hash.text(probe.stable_id);
    hash.unsigned_integer(
        static_cast<std::uint64_t>(probe.category));
    hash.unsigned_integer(
        static_cast<std::uint64_t>(probe.decision_kind));
    hash.unsigned_integer(
        static_cast<std::uint64_t>(probe.root_deck));
    hash.unsigned_integer(
        static_cast<std::uint64_t>(probe.opponent_deck));
    hash.unsigned_integer(probe.root_player);
    hash.unsigned_integer(
        static_cast<std::uint64_t>(probe.phase));
    hash.signed_integer(probe.consecutive_passes);

    const GameState& state = probe.state;
    hash.unsigned_integer(state.active_player);
    hash.unsigned_integer(state.starting_player);
    hash.unsigned_integer(state.turn_number);
    hash.unsigned_integer(state.next_permanent_id);
    hash.unsigned_integer(state.next_stack_object_id);
    for (std::size_t player = 0; player < state.players.size();
         ++player) {
        hash_public_player(hash, state.players[player]);
        hash_stats(hash, state.stats[player]);
        // Hidden identities are included only for the observing player's
        // hand. Both library orders and the opponent hand identities are
        // intentionally excluded.
        hash.unsigned_integer(
            state.players[player].library.size());
        hash.unsigned_integer(state.players[player].hand.size());
        if (player == probe.root_player) {
            hash_card_vector(hash, state.players[player].hand);
        }
    }

    hash.unsigned_integer(state.stack.size());
    for (const StackObject& object : state.stack) {
        hash.unsigned_integer(
            static_cast<std::uint64_t>(object.kind));
        hash.unsigned_integer(object.id);
        hash.unsigned_integer(
            static_cast<std::uint64_t>(object.card));
        hash.unsigned_integer(object.controller);
        hash_target(hash, object.target);
        hash.boolean(object.spell_target.has_value());
        if (object.spell_target.has_value()) {
            hash.unsigned_integer(*object.spell_target);
        }
    }

    hash.unsigned_integer(probe.original_decks.size());
    for (const std::vector<CardId>& deck : probe.original_decks) {
        // Decklists are known rules inputs. Hash their multisets in enum
        // order, never the shuffled library order in the state.
        constexpr std::size_t card_count =
            static_cast<std::size_t>(CardId::Moat) + 1;
        std::array<std::size_t, card_count> counts{};
        for (const CardId card : deck) {
            ++counts[static_cast<std::size_t>(card)];
        }
        for (const std::size_t count : counts) {
            hash.unsigned_integer(count);
        }
    }

    hash.unsigned_integer(probe.candidates.size());
    for (const probes::Candidate& candidate : probe.candidates) {
        hash.text(candidate.descriptor);
        if (const auto* priority =
                std::get_if<PriorityAction>(&candidate.action)) {
            hash.byte(0U);
            hash_priority_action(hash, *priority);
        } else {
            const auto& attack =
                std::get<probes::BinaryAttackDecision>(
                    candidate.action);
            hash.byte(1U);
            hash.unsigned_integer(attack.attacker);
            hash.boolean(attack.include);
        }
    }
}

bool sorcery_actions_for(TurnPhase phase) {
    return phase == TurnPhase::FirstMain ||
           phase == TurnPhase::SecondMain;
}

void validate_score_config(const ProbeScoreConfig& config) {
    if (config.training_games == 0) {
        throw std::invalid_argument(
            "probe training games must be greater than zero");
    }
    if (config.reference_worlds < 2) {
        throw std::invalid_argument(
            "probe reference requires at least two worlds");
    }
    if (config.reference_worlds > kMaximumReferenceWorlds) {
        throw std::invalid_argument(
            "probe reference worlds must not exceed 4096");
    }
    if (config.reference_horizon_turns >
        kMaximumReferenceHorizon) {
        throw std::invalid_argument(
            "probe reference horizon must not exceed 128");
    }
    if (config.reference_rollouts_per_world == 0) {
        throw std::invalid_argument(
            "probe reference rollouts must be greater than zero");
    }
    if (config.reference_rollouts_per_world >
        kMaximumReferenceRollouts) {
        throw std::invalid_argument(
            "probe reference rollouts must not exceed 256");
    }
    if (config.cache_path.empty()) {
        throw std::invalid_argument(
            "probe cache path must not be empty");
    }
}

std::size_t reference_sample_count(
    const ProbeCacheMetadata& metadata) {
    if (metadata.worlds >
        std::numeric_limits<std::size_t>::max() /
            metadata.rollouts_per_world) {
        throw std::invalid_argument(
            "probe cache sample count overflows size_t");
    }
    return metadata.worlds * metadata.rollouts_per_world;
}

const ProbeReferenceSamples& find_reference_samples(
    const std::vector<ProbeReferenceSamples>& samples,
    std::string_view stable_id) {
    const auto found = std::find_if(
        samples.begin(), samples.end(),
        [stable_id](const ProbeReferenceSamples& probe) {
            return probe.stable_id == stable_id;
        });
    if (found == samples.end()) {
        throw std::invalid_argument(
            "reference samples are missing a probe");
    }
    return *found;
}

std::vector<const probes::DecisionProbe*> sorted_probes(
    const std::vector<probes::DecisionProbe>& corpus) {
    std::vector<const probes::DecisionProbe*> sorted;
    sorted.reserve(corpus.size());
    for (const probes::DecisionProbe& probe : corpus) {
        sorted.push_back(&probe);
    }
    std::sort(
        sorted.begin(), sorted.end(),
        [](const probes::DecisionProbe* left,
           const probes::DecisionProbe* right) {
            return left->stable_id < right->stable_id;
        });
    return sorted;
}

void validate_text_field(std::string_view value,
                         std::string_view field) {
    if (value.empty() ||
        value.find_first_of("\t\r\n") != std::string_view::npos) {
        throw std::invalid_argument(
            std::string(field) +
            " must be nonempty and contain no tabs/newlines");
    }
}

std::string deck_token(DeckId deck) {
    return std::string(deck_name(deck));
}

DeckId parse_deck_token(std::string_view token) {
    for (std::size_t deck = 0; deck < 4; ++deck) {
        const auto id = static_cast<DeckId>(deck);
        if (token == deck_name(id)) {
            return id;
        }
    }
    throw std::invalid_argument(
        "probe cache has an invalid deck token");
}

std::vector<std::string_view> split_tabs(
    const std::string& line) {
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    for (;;) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string::npos) {
            fields.emplace_back(line.data() + start,
                                line.size() - start);
            return fields;
        }
        fields.emplace_back(line.data() + start, tab - start);
        start = tab + 1;
    }
}

std::uint64_t parse_u64_strict(std::string_view text,
                               std::string_view field) {
    std::uint64_t value = 0;
    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || result.ec != std::errc{} ||
        result.ptr != text.data() + text.size()) {
        throw std::invalid_argument(
            "probe cache has invalid " + std::string(field));
    }
    return value;
}

std::size_t parse_size_strict(std::string_view text,
                              std::string_view field) {
    const std::uint64_t value = parse_u64_strict(text, field);
    if (value > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument(
            "probe cache " + std::string(field) +
            " exceeds size_t");
    }
    return static_cast<std::size_t>(value);
}

double parse_double_strict(std::string_view text) {
    std::istringstream input{std::string(text)};
    input.imbue(std::locale::classic());
    input >> std::noskipws;
    double value = 0.0;
    if (!(input >> value) || input.peek() != std::char_traits<char>::eof() ||
        !std::isfinite(value)) {
        throw std::invalid_argument(
            "probe cache has an invalid sample value");
    }
    return value;
}

std::string read_required_line(std::istream& input,
                               std::string_view context) {
    std::string line;
    if (!std::getline(input, line)) {
        throw std::invalid_argument(
            "probe cache ended while reading " +
            std::string(context));
    }
    if (line.find('\r') != std::string::npos) {
        throw std::invalid_argument(
            "probe cache must use LF line endings");
    }
    return line;
}

std::string read_meta_value(std::istream& input,
                            std::string_view key) {
    const std::string line = read_required_line(input, key);
    const auto fields = split_tabs(line);
    if (fields.size() != 3 || fields[0] != "meta" ||
        fields[1] != key || fields[2].empty()) {
        throw std::invalid_argument(
            "probe cache has malformed metadata field " +
            std::string(key));
    }
    return std::string(fields[2]);
}

ProbeCacheMetadata read_metadata(std::istream& input) {
    const std::string magic =
        read_required_line(input, "cache magic");
    if (magic != "# alpha-probe-label-cache-v2") {
        throw std::invalid_argument(
            "probe cache has an unknown magic header");
    }
    ProbeCacheMetadata metadata;
    metadata.schema = read_meta_value(input, "schema");
    metadata.algorithm = read_meta_value(input, "algorithm");
    metadata.semantic_revision =
        read_meta_value(input, "semantic_revision");
    metadata.corpus_id = read_meta_value(input, "corpus");
    metadata.reference_seed =
        parse_u64_strict(read_meta_value(input, "reference_seed"),
                         "reference_seed");
    metadata.production_policy_seed =
        parse_u64_strict(
            read_meta_value(input, "production_policy_seed"),
            "production_policy_seed");
    metadata.training_seed =
        parse_u64_strict(read_meta_value(input, "train_seed"),
                         "train_seed");
    metadata.training_games =
        parse_size_strict(read_meta_value(input, "train_games"),
                          "train_games");
    metadata.worlds =
        parse_size_strict(read_meta_value(input, "worlds"),
                          "worlds");
    metadata.horizon_turns =
        parse_size_strict(read_meta_value(input, "horizon"),
                          "horizon");
    metadata.rollouts_per_world =
        parse_size_strict(read_meta_value(input, "rollouts"),
                          "rollouts");
    metadata.probe_count =
        parse_size_strict(read_meta_value(input, "probe_count"),
                          "probe_count");
    metadata.reference_model_fingerprint =
        read_meta_value(input, "model_fingerprint");
    metadata.information_set_fingerprint =
        read_meta_value(input, "fingerprint");
    return metadata;
}

std::string metadata_mismatch(
    const ProbeCacheMetadata& actual,
    const ProbeCacheMetadata& expected) {
    if (actual.schema != expected.schema) {
        return "schema";
    }
    if (actual.algorithm != expected.algorithm) {
        return "algorithm";
    }
    if (actual.semantic_revision != expected.semantic_revision) {
        return "semantic_revision";
    }
    if (actual.corpus_id != expected.corpus_id) {
        return "corpus";
    }
    if (actual.reference_seed != expected.reference_seed) {
        return "reference_seed";
    }
    if (actual.production_policy_seed !=
        expected.production_policy_seed) {
        return "production_policy_seed";
    }
    if (actual.training_seed != expected.training_seed) {
        return "train_seed";
    }
    if (actual.training_games != expected.training_games) {
        return "train_games";
    }
    if (actual.worlds != expected.worlds) {
        return "worlds";
    }
    if (actual.horizon_turns != expected.horizon_turns) {
        return "horizon";
    }
    if (actual.rollouts_per_world !=
        expected.rollouts_per_world) {
        return "rollouts";
    }
    if (actual.probe_count != expected.probe_count) {
        return "probe_count";
    }
    if (actual.reference_model_fingerprint !=
        expected.reference_model_fingerprint) {
        return "model_fingerprint";
    }
    if (actual.information_set_fingerprint !=
        expected.information_set_fingerprint) {
        return "fingerprint";
    }
    return {};
}

[[noreturn]] void throw_refresh_error(
    std::string_view detail) {
    throw std::invalid_argument(
        "probe cache is stale or invalid (" +
        std::string(detail) +
        "); rerun with --refresh-probe-cache");
}

void validate_reference_samples(
    const ProbeCacheMetadata& metadata,
    const std::vector<probes::DecisionProbe>& corpus,
    const std::vector<ProbeReferenceSamples>& samples) {
    if (samples.size() != corpus.size()) {
        throw std::invalid_argument(
            "reference sample probe count does not match corpus");
    }
    const std::size_t expected_count =
        reference_sample_count(metadata);
    for (const probes::DecisionProbe& probe : corpus) {
        const ProbeReferenceSamples& reference =
            find_reference_samples(samples, probe.stable_id);
        if (reference.root_deck != probe.root_deck ||
            reference.candidates.size() !=
                probe.candidates.size()) {
            throw std::invalid_argument(
                "reference sample schema does not match probe");
        }
        for (std::size_t candidate = 0;
             candidate < probe.candidates.size(); ++candidate) {
            const auto& expected = probe.candidates[candidate];
            const auto& actual =
                reference.candidates[candidate];
            if (actual.key != expected.descriptor ||
                actual.q_samples.size() != expected_count) {
                throw std::invalid_argument(
                    "reference candidate samples do not match probe");
            }
            for (const double sample : actual.q_samples) {
                if (!std::isfinite(sample) || sample < 0.0 ||
                    sample > 1.0) {
                    throw std::invalid_argument(
                        "reference sample is not a probability");
                }
            }
        }
    }
}

void write_cache_contents(
    std::ostream& output,
    const ProbeCacheMetadata& metadata,
    const std::vector<probes::DecisionProbe>& corpus,
    const std::vector<ProbeReferenceSamples>& samples) {
    output.imbue(std::locale::classic());
    output << "# alpha-probe-label-cache-v2\n"
           << "meta\tschema\t" << metadata.schema << '\n'
           << "meta\talgorithm\t" << metadata.algorithm << '\n'
           << "meta\tsemantic_revision\t"
           << metadata.semantic_revision << '\n'
           << "meta\tcorpus\t" << metadata.corpus_id << '\n'
           << "meta\treference_seed\t" << metadata.reference_seed
           << '\n'
           << "meta\tproduction_policy_seed\t"
           << metadata.production_policy_seed << '\n'
           << "meta\ttrain_seed\t" << metadata.training_seed << '\n'
           << "meta\ttrain_games\t" << metadata.training_games
           << '\n'
           << "meta\tworlds\t" << metadata.worlds << '\n'
           << "meta\thorizon\t" << metadata.horizon_turns << '\n'
           << "meta\trollouts\t" << metadata.rollouts_per_world
           << '\n'
           << "meta\tprobe_count\t" << metadata.probe_count << '\n'
           << "meta\tmodel_fingerprint\t"
           << metadata.reference_model_fingerprint << '\n'
           << "meta\tfingerprint\t"
           << metadata.information_set_fingerprint << '\n'
           << "columns\tprobe_id\tdeck\tcandidate_index"
              "\tcandidate_key\tsample_index\tq\n"
           << std::setprecision(
                  std::numeric_limits<double>::max_digits10);

    for (const probes::DecisionProbe* probe :
         sorted_probes(corpus)) {
        const ProbeReferenceSamples& reference =
            find_reference_samples(samples, probe->stable_id);
        for (std::size_t candidate = 0;
             candidate < reference.candidates.size(); ++candidate) {
            const auto& values = reference.candidates[candidate];
            for (std::size_t sample = 0;
                 sample < values.q_samples.size(); ++sample) {
                output << "sample\t" << probe->stable_id << '\t'
                       << deck_token(probe->root_deck) << '\t'
                       << candidate << '\t' << values.key << '\t'
                       << sample << '\t'
                       << values.q_samples[sample] << '\n';
            }
        }
    }
    if (!output) {
        throw std::runtime_error(
            "failed while writing probe label cache");
    }
}

bool bit_identical(double left, double right) {
    return std::bit_cast<std::uint64_t>(left) ==
           std::bit_cast<std::uint64_t>(right);
}

void require_bit_identical(
    const LearnedActionSamples& first,
    const LearnedActionSamples& second,
    std::string_view stable_id) {
    if (first.q_samples.size() != second.q_samples.size()) {
        throw std::runtime_error(
            std::string(stable_id) +
            ": hidden repartition changed reference sample rows");
    }
    for (std::size_t row = 0; row < first.q_samples.size();
         ++row) {
        if (first.q_samples[row].size() !=
            second.q_samples[row].size()) {
            throw std::runtime_error(
                std::string(stable_id) +
                ": hidden repartition changed reference sample count");
        }
        for (std::size_t sample = 0;
             sample < first.q_samples[row].size(); ++sample) {
            if (!bit_identical(first.q_samples[row][sample],
                               second.q_samples[row][sample])) {
                throw std::runtime_error(
                    std::string(stable_id) +
                    ": hidden repartition changed raw reference Q "
                    "samples");
            }
        }
    }
}

std::vector<PriorityAction> priority_candidates(
    const probes::DecisionProbe& probe) {
    std::vector<PriorityAction> actions;
    actions.reserve(probe.candidates.size());
    for (const probes::Candidate& candidate : probe.candidates) {
        const auto* action =
            std::get_if<PriorityAction>(&candidate.action);
        if (action == nullptr) {
            throw std::invalid_argument(
                "priority probe contains a non-priority candidate");
        }
        actions.push_back(*action);
    }
    return actions;
}

PermanentId binary_attack_subject(
    const probes::DecisionProbe& probe) {
    std::optional<PermanentId> subject;
    std::array<bool, 2> choices{};
    for (const probes::Candidate& candidate : probe.candidates) {
        const auto* attack =
            std::get_if<probes::BinaryAttackDecision>(
                &candidate.action);
        if (attack == nullptr) {
            throw std::invalid_argument(
                "attack probe contains a non-attack candidate");
        }
        if (subject.has_value() && *subject != attack->attacker) {
            throw std::invalid_argument(
                "binary attack probe has multiple subjects");
        }
        subject = attack->attacker;
        choices[attack->include ? 1U : 0U] = true;
    }
    if (!subject.has_value() || !choices[0] || !choices[1] ||
        probe.candidates.size() != 2) {
        throw std::invalid_argument(
            "binary attack probe must contain Skip and Include");
    }
    return *subject;
}

LearnedActionSamples score_probe_actions(
    const probes::DecisionProbe& probe, const GameState& state,
    std::shared_ptr<const LearnedModel> model,
    LearnedSearchConfig search) {
    if (probe.decision_kind == probes::DecisionKind::Priority) {
        return learned_priority_action_samples(
            state, probe.original_decks, probe.root_player,
            sorcery_actions_for(probe.phase), probe.phase,
            probe.consecutive_passes, priority_candidates(probe),
            std::move(model), search);
    }
    const PermanentId subject = binary_attack_subject(probe);
    return learned_binary_attack_samples(
        state, probe.original_decks, probe.root_player, {}, subject,
        {}, std::move(model), search);
}

std::vector<double> actor_raw_scores(
    const probes::DecisionProbe& probe,
    std::shared_ptr<const LearnedModel> model) {
    if (probe.decision_kind == probes::DecisionKind::Priority) {
        return learned_actor_priority_logits(
            probe.state, probe.root_player,
            sorcery_actions_for(probe.phase), probe.phase,
            probe.consecutive_passes, priority_candidates(probe),
            std::move(model));
    }
    const PermanentId subject = binary_attack_subject(probe);
    const std::array<double, 2> logits =
        learned_actor_binary_attack_logits(
            probe.state, probe.root_player, {}, subject, {},
            std::move(model));
    std::vector<double> scores;
    scores.reserve(probe.candidates.size());
    for (const probes::Candidate& candidate : probe.candidates) {
        const auto& attack =
            std::get<probes::BinaryAttackDecision>(
                candidate.action);
        scores.push_back(logits[attack.include ? 1U : 0U]);
    }
    return scores;
}

std::vector<double> handcrafted_scores(
    const probes::DecisionProbe& probe) {
    if (probe.decision_kind == probes::DecisionKind::Priority) {
        return handcrafted_priority_scores(
            probe.state, probe.root_player,
            priority_candidates(probe));
    }
    const PermanentId subject = binary_attack_subject(probe);
    const std::array<double, 2> scores =
        handcrafted_binary_attack_scores(
            probe.state, probe.root_player, {}, subject, {});
    std::vector<double> mapped;
    mapped.reserve(probe.candidates.size());
    for (const probes::Candidate& candidate : probe.candidates) {
        const auto& attack =
            std::get<probes::BinaryAttackDecision>(
                candidate.action);
        mapped.push_back(scores[attack.include ? 1U : 0U]);
    }
    return mapped;
}

std::vector<double> means_of(
    const LearnedActionSamples& samples) {
    std::vector<double> means;
    means.reserve(samples.q_samples.size());
    for (const std::vector<double>& row : samples.q_samples) {
        if (row.empty()) {
            throw std::invalid_argument(
                "action scorer returned an empty sample row");
        }
        double sum = 0.0;
        for (const double value : row) {
            sum += value;
        }
        means.push_back(sum / static_cast<double>(row.size()));
    }
    return means;
}

probe_eval::ProbePrediction make_prediction(
    const probes::DecisionProbe& probe,
    const std::vector<double>& scores, double critic_value,
    std::optional<std::size_t> selected_candidate = std::nullopt) {
    if (scores.size() != probe.candidates.size()) {
        throw std::invalid_argument(
            "policy scorer returned the wrong candidate count");
    }
    probe_eval::ProbePrediction prediction;
    prediction.stable_id = probe.stable_id;
    prediction.critic_value = critic_value;
    if (selected_candidate.has_value()) {
        if (*selected_candidate >= probe.candidates.size()) {
            throw std::invalid_argument(
                "deployed selected candidate is out of range");
        }
        prediction.selected_key =
            probe.candidates[*selected_candidate].descriptor;
    }
    prediction.policy_scores.reserve(scores.size());
    for (std::size_t candidate = 0; candidate < scores.size();
         ++candidate) {
        prediction.policy_scores.push_back(
            {probe.candidates[candidate].descriptor,
             scores[candidate]});
    }
    return prediction;
}

std::vector<probe_eval::ProbePrediction> score_actor_raw(
    const std::vector<probes::DecisionProbe>& corpus,
    std::shared_ptr<const LearnedModel> actor_model) {
    std::vector<probe_eval::ProbePrediction> predictions;
    predictions.reserve(corpus.size());
    for (const probes::DecisionProbe& probe : corpus) {
        predictions.push_back(make_prediction(
            probe, actor_raw_scores(probe, actor_model),
            learned_critic_value(
                probe.state, probe.root_player, actor_model)));
    }
    return predictions;
}

std::vector<double> learned_search_scores(
    const probes::DecisionProbe& probe,
    std::shared_ptr<const LearnedModel> model,
    LearnedVariant continuation_variant, std::size_t worlds,
    std::size_t rollouts_per_world, std::size_t horizon_turns,
    bool blend_shallow_prior) {
    const LearnedSearchConfig config{
        .seed = reference_seed_for_probe(
            probes::kProbeDevV2, probe.stable_id),
        .worlds = worlds,
        .rollouts_per_world = rollouts_per_world,
        .horizon_turns = horizon_turns,
        .continuation_variant = continuation_variant,
        .blend_shallow_prior = blend_shallow_prior,
    };
    const LearnedActionSamples samples =
        score_probe_actions(probe, probe.state, model, config);
    const auto keyed = map_candidate_samples(probe, samples);
    LearnedActionSamples ordered;
    ordered.q_samples.reserve(keyed.size());
    for (const auto& candidate : keyed) {
        ordered.q_samples.push_back(candidate.q_samples);
    }
    return means_of(ordered);
}

std::vector<probe_eval::ProbePrediction> score_learned_search(
    const std::vector<probes::DecisionProbe>& corpus,
    std::shared_ptr<const LearnedModel> model,
    LearnedVariant continuation_variant, std::size_t worlds,
    std::size_t rollouts_per_world, std::size_t horizon_turns,
    bool blend_shallow_prior) {
    std::vector<probe_eval::ProbePrediction> predictions;
    predictions.reserve(corpus.size());
    for (const probes::DecisionProbe& probe : corpus) {
        predictions.push_back(make_prediction(
            probe,
            learned_search_scores(
                probe, model, continuation_variant, worlds,
                rollouts_per_world, horizon_turns,
                blend_shallow_prior),
            learned_critic_value(
                probe.state, probe.root_player, model)));
    }
    return predictions;
}

std::vector<probe_eval::ProbePrediction> score_actor_deployed(
    const std::vector<probes::DecisionProbe>& corpus,
    std::shared_ptr<const LearnedModel> actor_model) {
    std::vector<probe_eval::ProbePrediction> predictions;
    predictions.reserve(corpus.size());
    for (const probes::DecisionProbe& probe : corpus) {
        std::vector<double> scores;
        if (probe.decision_kind ==
            probes::DecisionKind::Priority) {
            scores = learned_search_scores(
                probe, actor_model, LearnedVariant::UnifiedActor,
                kProductionActorWorlds, 1,
                kProductionActorHorizon, false);
        } else {
            // Deployed Actor combat is selected directly by the masked
            // policy head. It does not run the information-set evaluator.
            scores = actor_raw_scores(probe, actor_model);
        }
        predictions.push_back(make_prediction(
            probe, scores,
            learned_critic_value(
                probe.state, probe.root_player, actor_model)));
    }
    return predictions;
}

LearnedValueAttackSetScores value_deployed_attack_scores(
    const probes::DecisionProbe& probe,
    std::shared_ptr<const LearnedModel> value_model) {
    const PermanentId subject = binary_attack_subject(probe);
    std::vector<std::vector<PermanentId>> attack_sets;
    attack_sets.reserve(probe.candidates.size());
    for (const probes::Candidate& candidate : probe.candidates) {
        const auto& decision =
            std::get<probes::BinaryAttackDecision>(
                candidate.action);
        attack_sets.push_back(
            decision.include ? std::vector<PermanentId>{subject}
                             : std::vector<PermanentId>{});
    }
    const std::uint64_t policy_seed = reference_seed_for_probe(
        probes::kProbeDevV2, probe.stable_id,
        kProbeProductionPolicySeed);
    return learned_value_attack_set_scores(
        probe.state, probe.root_player, attack_sets,
        std::move(value_model), policy_seed);
}

std::vector<probe_eval::ProbePrediction> score_value_deployed(
    const std::vector<probes::DecisionProbe>& corpus,
    std::shared_ptr<const LearnedModel> value_model) {
    std::vector<probe_eval::ProbePrediction> predictions;
    predictions.reserve(corpus.size());
    for (const probes::DecisionProbe& probe : corpus) {
        std::vector<double> scores;
        std::optional<std::size_t> selected_candidate;
        if (probe.decision_kind ==
            probes::DecisionKind::Priority) {
            scores = learned_search_scores(
                probe, value_model,
                LearnedVariant::ValueSearchChampion,
                kProductionValueWorlds, 1,
                kProductionValueHorizon, true);
        } else {
            const auto attack =
                value_deployed_attack_scores(probe, value_model);
            scores = attack.scores;
            selected_candidate = attack.selected_candidate;
        }
        predictions.push_back(make_prediction(
            probe, scores,
            learned_critic_value(
                probe.state, probe.root_player, value_model),
            selected_candidate));
    }
    return predictions;
}

std::vector<probe_eval::ProbePrediction> score_handcrafted(
    const std::vector<probes::DecisionProbe>& corpus) {
    std::vector<probe_eval::ProbePrediction> predictions;
    predictions.reserve(corpus.size());
    for (const probes::DecisionProbe& probe : corpus) {
        predictions.push_back(make_prediction(
            probe, handcrafted_scores(probe), 0.5));
    }
    return predictions;
}

std::vector<probes::DecisionProbe> hidden_clone_corpus(
    const std::vector<probes::DecisionProbe>& corpus) {
    std::vector<probes::DecisionProbe> clones = corpus;
    for (std::size_t index = 0; index < corpus.size(); ++index) {
        clones[index].state =
            hidden_repartition_clone(corpus[index]);
    }
    return clones;
}

probe_eval::DeckProbeMetrics pooled_deck_metrics(
    const probe_eval::ProbeMetricSummary& summary);

const probe_eval::ProbePrediction& prediction_for(
    const std::vector<probe_eval::ProbePrediction>& predictions,
    std::string_view stable_id) {
    const auto found = std::find_if(
        predictions.begin(), predictions.end(),
        [stable_id](const probe_eval::ProbePrediction& prediction) {
            return prediction.stable_id == stable_id;
        });
    if (found == predictions.end()) {
        throw std::runtime_error(
            "hidden-invariance prediction is missing a probe");
    }
    return *found;
}

const probe_eval::PolicyScore& policy_score_for(
    const probe_eval::ProbePrediction& prediction,
    std::string_view key) {
    const auto found = std::find_if(
        prediction.policy_scores.begin(),
        prediction.policy_scores.end(),
        [key](const probe_eval::PolicyScore& score) {
            return score.key == key;
        });
    if (found == prediction.policy_scores.end()) {
        throw std::runtime_error(
            "hidden-invariance prediction is missing a candidate");
    }
    return *found;
}

void require_predictions_bit_identical(
    const std::vector<probe_eval::ProbePrediction>& original,
    const std::vector<probe_eval::ProbePrediction>& hidden_clone,
    std::string_view policy_name) {
    if (original.size() != hidden_clone.size()) {
        throw std::runtime_error(
            std::string(policy_name) +
            ": hidden repartition changed prediction count");
    }
    for (const auto& prediction : original) {
        const auto& clone =
            prediction_for(hidden_clone, prediction.stable_id);
        if (prediction.policy_scores.size() !=
                clone.policy_scores.size() ||
            prediction.selected_key != clone.selected_key ||
            !bit_identical(prediction.critic_value,
                           clone.critic_value)) {
            throw std::runtime_error(
                std::string(policy_name) +
                ": hidden repartition changed prediction schema "
                "or critic");
        }
        for (const auto& score : prediction.policy_scores) {
            const auto& clone_score =
                policy_score_for(clone, score.key);
            if (!bit_identical(score.score, clone_score.score)) {
                throw std::runtime_error(
                    std::string(policy_name) +
                    ": hidden repartition changed policy score for " +
                    prediction.stable_id + "/" + score.key);
            }
        }
    }
}

void require_critics_bit_identical(
    const std::vector<probe_eval::ProbePrediction>& first,
    const std::vector<probe_eval::ProbePrediction>& second,
    std::string_view model_name) {
    if (first.size() != second.size()) {
        throw std::runtime_error(
            std::string(model_name) +
            ": critic comparison changed probe count");
    }
    for (const auto& prediction : first) {
        const auto& other =
            prediction_for(second, prediction.stable_id);
        if (!bit_identical(prediction.critic_value,
                           other.critic_value)) {
            throw std::runtime_error(
                std::string(model_name) +
                ": critic changed across policy views at " +
                prediction.stable_id);
        }
    }
}

void require_metric_double(double original, double hidden_clone,
                           std::string_view policy_name,
                           std::string_view field) {
    if (!bit_identical(original, hidden_clone)) {
        throw std::runtime_error(
            std::string(policy_name) +
            ": hidden repartition changed metric " +
            std::string(field));
    }
}

void require_deck_metrics_identical(
    const probe_eval::DeckProbeMetrics& original,
    const probe_eval::DeckProbeMetrics& hidden_clone,
    std::string_view policy_name) {
    if (original.root_deck != hidden_clone.root_deck ||
        original.probe_count != hidden_clone.probe_count ||
        original.stable_pair_count !=
            hidden_clone.stable_pair_count) {
        throw std::runtime_error(
            std::string(policy_name) +
            ": hidden repartition changed metric counts");
    }
    require_metric_double(
        original.top1_expected_agreement,
        hidden_clone.top1_expected_agreement, policy_name, "top1");
    require_metric_double(
        original.stable_pair_agreement,
        hidden_clone.stable_pair_agreement, policy_name,
        "stable_pair_agreement");
    require_metric_double(
        original.mean_regret, hidden_clone.mean_regret,
        policy_name, "mean_regret");
    require_metric_double(
        original.critic_brier, hidden_clone.critic_brier,
        policy_name, "critic_brier");
    require_metric_double(
        original.critic_mse, hidden_clone.critic_mse,
        policy_name, "critic_mse");
    require_metric_double(
        original.critic_log_loss, hidden_clone.critic_log_loss,
        policy_name, "critic_log_loss");
    require_metric_double(
        original.critic_bias, hidden_clone.critic_bias,
        policy_name, "critic_bias");
    require_metric_double(
        original.critic_ece, hidden_clone.critic_ece,
        policy_name, "critic_ece");
}

void require_metrics_bit_identical(
    const probe_eval::ProbeMetricSummary& original,
    const probe_eval::ProbeMetricSummary& hidden_clone,
    std::string_view policy_name) {
    const auto original_pooled = pooled_deck_metrics(original);
    const auto clone_pooled = pooled_deck_metrics(hidden_clone);
    require_deck_metrics_identical(
        original_pooled, clone_pooled, policy_name);
    for (std::size_t deck = 0; deck < original.by_deck.size();
         ++deck) {
        require_deck_metrics_identical(
            original.by_deck[deck], hidden_clone.by_deck[deck],
            policy_name);
    }
}

void require_candidate_q_fit_deck_bit_identical(
    const probe_eval::DeckCandidateQFitMetrics& original,
    const probe_eval::DeckCandidateQFitMetrics& hidden_clone,
    std::string_view policy_name) {
    if (original.root_deck != hidden_clone.root_deck ||
        original.candidate_count != hidden_clone.candidate_count) {
        throw std::runtime_error(
            std::string(policy_name) +
            ": hidden repartition changed candidate-Q fit counts");
    }
    require_metric_double(
        original.mae, hidden_clone.mae, policy_name,
        "candidate_q_mae");
    require_metric_double(
        original.rmse, hidden_clone.rmse, policy_name,
        "candidate_q_rmse");
}

void require_candidate_q_fit_bit_identical(
    const probe_eval::CandidateQFitSummary& original,
    const probe_eval::CandidateQFitSummary& hidden_clone,
    std::string_view policy_name) {
    if (original.candidate_count != hidden_clone.candidate_count) {
        throw std::runtime_error(
            std::string(policy_name) +
            ": hidden repartition changed pooled candidate-Q count");
    }
    require_metric_double(
        original.mae, hidden_clone.mae, policy_name,
        "pooled_candidate_q_mae");
    require_metric_double(
        original.rmse, hidden_clone.rmse, policy_name,
        "pooled_candidate_q_rmse");
    for (std::size_t deck = 0; deck < original.by_deck.size();
         ++deck) {
        require_candidate_q_fit_deck_bit_identical(
            original.by_deck[deck], hidden_clone.by_deck[deck],
            policy_name);
    }
}

PolicyProbeReport evaluate_hidden_invariant_policy(
    std::string name, std::string configuration,
    const std::vector<probe_eval::ProbeLabel>& labels,
    const std::vector<probe_eval::ProbePrediction>& predictions,
    const std::vector<probe_eval::ProbePrediction>& clone_predictions,
    bool has_critic_metrics, bool has_candidate_q_fit = false) {
    require_predictions_bit_identical(
        predictions, clone_predictions, name);
    const auto metrics =
        probe_eval::evaluate_probe_predictions(
            labels, predictions);
    const auto clone_metrics =
        probe_eval::evaluate_probe_predictions(
            labels, clone_predictions);
    require_metrics_bit_identical(metrics, clone_metrics, name);
    std::optional<probe_eval::CandidateQFitSummary> candidate_q_fit;
    if (has_candidate_q_fit) {
        candidate_q_fit =
            probe_eval::evaluate_candidate_q_fit(
                labels, predictions);
        const auto clone_candidate_q_fit =
            probe_eval::evaluate_candidate_q_fit(
                labels, clone_predictions);
        require_candidate_q_fit_bit_identical(
            *candidate_q_fit, clone_candidate_q_fit, name);
    }
    return {
        .name = std::move(name),
        .configuration = std::move(configuration),
        .metrics = metrics,
        .has_critic_metrics = has_critic_metrics,
        .candidate_q_fit = std::move(candidate_q_fit),
    };
}

void append_metric_line(
    std::ostringstream& output, std::string_view indent,
    std::string_view label,
    const probe_eval::DeckProbeMetrics& metrics,
    bool has_critic_metrics) {
    output << indent << label << ": probes " << metrics.probe_count
           << ", top1 " << 100.0 * metrics.top1_expected_agreement
           << "%, stable pairs " << metrics.stable_pair_count
           << ", pair agreement "
           << 100.0 * metrics.stable_pair_agreement
           << "%, regret " << metrics.mean_regret;
    if (has_critic_metrics) {
        output << ", critic Brier " << metrics.critic_brier
               << ", MSE " << metrics.critic_mse
               << ", logloss " << metrics.critic_log_loss
               << ", bias " << metrics.critic_bias
               << ", ECE " << metrics.critic_ece;
    } else {
        output << ", critic n/a";
    }
    output << '\n';
}

void append_candidate_q_fit_line(
    std::ostringstream& output, std::string_view indent,
    std::string_view label, std::size_t candidate_count,
    double mae, double rmse) {
    output << indent << label << ": candidates "
           << candidate_count << ", Q MAE " << mae
           << ", Q RMSE " << rmse << '\n';
}

probe_eval::DeckProbeMetrics pooled_deck_metrics(
    const probe_eval::ProbeMetricSummary& summary) {
    return {
        .root_deck = DeckId::Green,
        .probe_count = summary.probe_count,
        .stable_pair_count = summary.stable_pair_count,
        .top1_expected_agreement =
            summary.top1_expected_agreement,
        .stable_pair_agreement =
            summary.stable_pair_agreement,
        .mean_regret = summary.mean_regret,
        .critic_brier = summary.critic_brier,
        .critic_mse = summary.critic_mse,
        .critic_log_loss = summary.critic_log_loss,
        .critic_bias = summary.critic_bias,
        .critic_ece = summary.critic_ece,
    };
}

bool stable_pair(const probe_eval::PairLabel& pair) {
    return std::abs(pair.delta_q) >=
               probe_eval::kStablePairMinimumDelta &&
           std::abs(pair.delta_q) >
               probe_eval::kNormal95CriticalValue *
                   pair.paired_standard_error;
}

const probe_eval::ProbeLabel& label_for(
    const std::vector<probe_eval::ProbeLabel>& labels,
    std::string_view stable_id) {
    const auto found = std::find_if(
        labels.begin(), labels.end(),
        [stable_id](const probe_eval::ProbeLabel& label) {
            return label.stable_id == stable_id;
        });
    if (found == labels.end()) {
        throw std::invalid_argument(
            "continuation cross-check is missing a probe label");
    }
    return *found;
}

const probe_eval::PairLabel& pair_for(
    const probe_eval::ProbeLabel& label,
    std::string_view first, std::string_view second) {
    const auto found = std::find_if(
        label.pairs.begin(), label.pairs.end(),
        [first, second](const probe_eval::PairLabel& pair) {
            return pair.first == first && pair.second == second;
        });
    if (found == label.pairs.end()) {
        throw std::invalid_argument(
            "continuation cross-check pair schema mismatch");
    }
    return *found;
}

ReferenceSensitivitySummary compare_continuation_labels(
    const std::vector<probe_eval::ProbeLabel>& actor_labels,
    const std::vector<probe_eval::ProbeLabel>& value_labels) {
    probe_eval::validate_probe_labels(actor_labels);
    probe_eval::validate_probe_labels(value_labels);
    ReferenceSensitivitySummary summary;
    for (std::size_t deck = 0; deck < summary.by_deck.size();
         ++deck) {
        summary.by_deck[deck].root_deck =
            static_cast<DeckId>(deck);
    }

    for (const probe_eval::ProbeLabel& actor : actor_labels) {
        const auto& value =
            label_for(value_labels, actor.stable_id);
        if (value.root_deck != actor.root_deck) {
            throw std::invalid_argument(
                "continuation cross-check deck mismatch");
        }
        const std::size_t deck =
            static_cast<std::size_t>(actor.root_deck);
        for (const probe_eval::PairLabel& actor_pair :
             actor.pairs) {
            if (!stable_pair(actor_pair)) {
                continue;
            }
            ++summary.actor_stable_pair_count;
            ++summary.by_deck[deck].actor_stable_pair_count;
            const auto& value_pair = pair_for(
                value, actor_pair.first, actor_pair.second);
            if (actor_pair.delta_q * value_pair.delta_q >= 0.0) {
                continue;
            }
            ++summary.point_sign_reversal_count;
            ++summary.by_deck[deck].point_sign_reversal_count;
            const bool value_is_stable = stable_pair(value_pair);
            if (value_is_stable) {
                ++summary.dual_stable_reversal_count;
                ++summary.by_deck[deck]
                      .dual_stable_reversal_count;
            }
            summary.flags.push_back({
                .stable_id = actor.stable_id,
                .root_deck = actor.root_deck,
                .first = actor_pair.first,
                .second = actor_pair.second,
                .actor_delta_q = actor_pair.delta_q,
                .value_delta_q = value_pair.delta_q,
                .value_pair_is_stable = value_is_stable,
            });
        }
    }
    return summary;
}

ProbeReferenceSamples generate_variant_reference_samples(
    const probes::DecisionProbe& probe,
    std::shared_ptr<const LearnedModel> model,
    const ProbeScoreConfig& config, LearnedVariant variant,
    bool verify_hidden_repartition) {
    const LearnedSearchConfig search{
        .seed = reference_seed_for_probe(
            probes::kProbeDevV2, probe.stable_id),
        .worlds = config.reference_worlds,
        .rollouts_per_world =
            config.reference_rollouts_per_world,
        .horizon_turns = config.reference_horizon_turns,
        .continuation_variant = variant,
        .blend_shallow_prior = false,
    };
    const LearnedActionSamples original =
        score_probe_actions(probe, probe.state, model, search);
    if (verify_hidden_repartition) {
        const GameState clone = hidden_repartition_clone(probe);
        const LearnedActionSamples repartitioned =
            score_probe_actions(probe, clone, model, search);
        require_bit_identical(original, repartitioned,
                              probe.stable_id);
    }
    return {
        .stable_id = probe.stable_id,
        .root_deck = probe.root_deck,
        .candidates =
            map_candidate_samples(probe, original),
    };
}

} // namespace

std::uint64_t reference_seed_for_probe(
    std::string_view corpus_id, std::string_view stable_id,
    std::uint64_t reference_seed) {
    Fnv1a hash;
    hash.text(corpus_id);
    hash.text(stable_id);
    hash.unsigned_integer(reference_seed);
    return hash.value();
}

std::string corpus_information_set_fingerprint(
    const std::vector<probes::DecisionProbe>& corpus) {
    Fnv1a hash;
    hash.text(probes::kProbeDevV2);
    const auto sorted = sorted_probes(corpus);
    hash.unsigned_integer(sorted.size());
    for (const probes::DecisionProbe* probe : sorted) {
        hash_probe(hash, *probe);
    }
    return hex_u64(hash.value());
}

GameState hidden_repartition_clone(
    const probes::DecisionProbe& probe) {
    if (probe.root_player >= probe.state.players.size()) {
        throw std::invalid_argument(
            "probe root player is outside the two seats");
    }
    GameState clone = probe.state;
    std::reverse(
        clone.players[probe.root_player].library.begin(),
        clone.players[probe.root_player].library.end());

    const std::size_t opponent = 1 - probe.root_player;
    PlayerState& hidden = clone.players[opponent];
    const std::size_t hand_size = hidden.hand.size();
    std::vector<CardId> cards = hidden.hand;
    cards.insert(cards.end(), hidden.library.begin(),
                 hidden.library.end());
    if (cards.size() > 1) {
        std::rotate(cards.begin(), cards.begin() + 1, cards.end());
        std::reverse(cards.begin(), cards.end());
    }
    const auto hand_end =
        cards.begin() + static_cast<std::ptrdiff_t>(hand_size);
    hidden.hand.assign(cards.begin(), hand_end);
    hidden.library.assign(hand_end, cards.end());
    return clone;
}

std::vector<probe_eval::CandidateSamples>
map_candidate_samples(
    const probes::DecisionProbe& probe,
    const LearnedActionSamples& action_samples) {
    std::vector<probe_eval::CandidateSamples> mapped;
    mapped.reserve(probe.candidates.size());
    if (probe.decision_kind == probes::DecisionKind::Priority) {
        if (action_samples.q_samples.size() !=
            probe.candidates.size()) {
            throw std::invalid_argument(
                "priority sample rows do not match candidate count");
        }
        for (std::size_t candidate = 0;
             candidate < probe.candidates.size(); ++candidate) {
            if (!std::holds_alternative<PriorityAction>(
                    probe.candidates[candidate].action)) {
                throw std::invalid_argument(
                    "priority probe contains attack candidate");
            }
            mapped.push_back({
                probe.candidates[candidate].descriptor,
                action_samples.q_samples[candidate],
            });
        }
        return mapped;
    }

    (void)binary_attack_subject(probe);
    if (action_samples.q_samples.size() != 2) {
        throw std::invalid_argument(
            "binary attack scorer must return Skip and Include rows");
    }
    for (const probes::Candidate& candidate : probe.candidates) {
        const auto& attack =
            std::get<probes::BinaryAttackDecision>(
                candidate.action);
        mapped.push_back({
            candidate.descriptor,
            action_samples.q_samples[attack.include ? 1U : 0U],
        });
    }
    return mapped;
}

ProbeCacheMetadata make_probe_cache_metadata(
    const ProbeScoreConfig& config,
    const std::vector<probes::DecisionProbe>& corpus,
    std::string_view reference_model_fingerprint) {
    validate_score_config(config);
    validate_text_field(reference_model_fingerprint,
                        "reference model fingerprint");
    const auto validation_errors =
        probes::validate_probe_dev_v2(corpus);
    if (!validation_errors.empty()) {
        throw std::invalid_argument(
            "cannot label an invalid probe corpus: " +
            validation_errors.front());
    }
    return {
        .schema = std::string(kProbeCacheSchema),
        .algorithm = std::string(kProbeReferenceAlgorithm),
        .semantic_revision = std::string(kProbeSemanticRevision),
        .corpus_id = std::string(probes::kProbeDevV2),
        .reference_seed = kProbeReferenceSeed,
        .production_policy_seed = kProbeProductionPolicySeed,
        .training_seed = config.training_seed,
        .training_games = config.training_games,
        .worlds = config.reference_worlds,
        .horizon_turns = config.reference_horizon_turns,
        .rollouts_per_world =
            config.reference_rollouts_per_world,
        .probe_count = corpus.size(),
        .reference_model_fingerprint =
            std::string(reference_model_fingerprint),
        .information_set_fingerprint =
            corpus_information_set_fingerprint(corpus),
    };
}

void write_probe_label_cache_atomic(
    const std::filesystem::path& path,
    const ProbeCacheMetadata& metadata,
    const std::vector<probes::DecisionProbe>& corpus,
    const std::vector<ProbeReferenceSamples>& samples) {
    if (path.empty()) {
        throw std::invalid_argument(
            "probe cache path must not be empty");
    }
    if (metadata.schema != kProbeCacheSchema ||
        metadata.algorithm != kProbeReferenceAlgorithm ||
        metadata.semantic_revision != kProbeSemanticRevision ||
        metadata.corpus_id != probes::kProbeDevV2 ||
        metadata.reference_seed != kProbeReferenceSeed ||
        metadata.production_policy_seed !=
            kProbeProductionPolicySeed ||
        metadata.probe_count != corpus.size() ||
        metadata.information_set_fingerprint !=
            corpus_information_set_fingerprint(corpus)) {
        throw std::invalid_argument(
            "probe cache metadata does not match corpus");
    }
    validate_text_field(metadata.reference_model_fingerprint,
                        "reference model fingerprint");
    validate_reference_samples(metadata, corpus, samples);
    std::vector<probe_eval::ProbeLabel> validated_labels;
    validated_labels.reserve(samples.size());
    for (const ProbeReferenceSamples& probe : samples) {
        validated_labels.push_back(probe_eval::make_probe_label(
            probe.stable_id, probe.root_deck, probe.candidates));
    }
    probe_eval::validate_probe_labels(validated_labels);
    for (const probes::DecisionProbe& probe : corpus) {
        validate_text_field(probe.stable_id, "probe stable ID");
        for (const probes::Candidate& candidate : probe.candidates) {
            validate_text_field(candidate.descriptor,
                                "candidate descriptor");
        }
    }

    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(
            path.parent_path(), error);
        if (error) {
            throw std::runtime_error(
                "could not create probe cache directory: " +
                error.message());
        }
    }

    std::filesystem::path temporary = path;
    const auto clock_value = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now()
            .time_since_epoch()
            .count());
    const auto address_value = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(&metadata));
    temporary += ".tmp." + hex_u64(clock_value ^ address_value);
    try {
        {
            std::ofstream output(
                temporary,
                std::ios::out | std::ios::trunc);
            if (!output) {
                throw std::runtime_error(
                    "could not open temporary probe cache");
            }
            write_cache_contents(
                output, metadata, corpus, samples);
            output.close();
            if (!output) {
                throw std::runtime_error(
                    "could not close temporary probe cache");
            }
        }
        std::filesystem::rename(temporary, path, error);
        if (error) {
            throw std::runtime_error(
                "could not atomically publish probe cache: " +
                error.message());
        }
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

std::vector<probe_eval::ProbeLabel> load_probe_label_cache(
    const std::filesystem::path& path,
    const ProbeCacheMetadata& expected_metadata,
    const std::vector<probes::DecisionProbe>& corpus) {
    try {
        std::ifstream input(path);
        if (!input) {
            throw std::invalid_argument(
                "cache file is missing or unreadable");
        }
        input.imbue(std::locale::classic());
        const ProbeCacheMetadata actual = read_metadata(input);
        const std::string mismatch =
            metadata_mismatch(actual, expected_metadata);
        if (!mismatch.empty()) {
            throw std::invalid_argument(
                "metadata mismatch: " + mismatch);
        }

        const std::string columns =
            read_required_line(input, "sample columns");
        if (columns !=
            "columns\tprobe_id\tdeck\tcandidate_index"
            "\tcandidate_key\tsample_index\tq") {
            throw std::invalid_argument(
                "sample column schema is invalid");
        }

        const std::size_t sample_count =
            reference_sample_count(actual);
        std::vector<probe_eval::ProbeLabel> labels;
        labels.reserve(corpus.size());
        for (const probes::DecisionProbe* probe :
             sorted_probes(corpus)) {
            std::vector<probe_eval::CandidateSamples>
                candidate_samples;
            candidate_samples.reserve(probe->candidates.size());
            for (std::size_t candidate = 0;
                 candidate < probe->candidates.size(); ++candidate) {
                const std::string& expected_key =
                    probe->candidates[candidate].descriptor;
                probe_eval::CandidateSamples values{
                    expected_key, {}};
                values.q_samples.reserve(sample_count);
                for (std::size_t sample = 0;
                     sample < sample_count; ++sample) {
                    const std::string line =
                        read_required_line(input, "sample row");
                    const auto fields = split_tabs(line);
                    if (fields.size() != 7 ||
                        fields[0] != "sample" ||
                        fields[1] != probe->stable_id ||
                        parse_deck_token(fields[2]) !=
                            probe->root_deck ||
                        parse_size_strict(fields[3],
                                          "candidate_index") !=
                            candidate ||
                        fields[4] != expected_key ||
                        parse_size_strict(fields[5],
                                          "sample_index") != sample) {
                        throw std::invalid_argument(
                            "sample row is out of canonical order "
                            "or mismatches the corpus");
                    }
                    const double q =
                        parse_double_strict(fields[6]);
                    if (q < 0.0 || q > 1.0) {
                        throw std::invalid_argument(
                            "sample Q is outside [0, 1]");
                    }
                    values.q_samples.push_back(q);
                }
                candidate_samples.push_back(std::move(values));
            }
            labels.push_back(probe_eval::make_probe_label(
                probe->stable_id, probe->root_deck,
                candidate_samples));
        }
        std::string trailing;
        if (std::getline(input, trailing)) {
            throw std::invalid_argument(
                "probe cache has trailing rows");
        }
        if (!input.eof()) {
            throw std::invalid_argument(
                "probe cache read failed");
        }
        probe_eval::validate_probe_labels(labels);
        return labels;
    } catch (const std::exception& error) {
        throw_refresh_error(error.what());
    }
}

LowMarginSummary summarize_low_margin_best_pairs(
    const std::vector<probe_eval::ProbeLabel>& labels) {
    probe_eval::validate_probe_labels(labels);
    LowMarginSummary summary;
    for (std::size_t deck = 0; deck < summary.by_deck.size();
         ++deck) {
        summary.by_deck[deck].root_deck =
            static_cast<DeckId>(deck);
    }

    for (const probe_eval::ProbeLabel& label : labels) {
        const auto best = std::max_element(
            label.candidates.begin(), label.candidates.end(),
            [](const probe_eval::CandidateLabel& left,
               const probe_eval::CandidateLabel& right) {
                return left.q < right.q;
            });
        if (best == label.candidates.end()) {
            throw std::invalid_argument(
                "probe label has no reference-best candidate");
        }
        for (const probe_eval::CandidateLabel& candidate :
             label.candidates) {
            if (candidate.key == best->key) {
                continue;
            }
            const probe_eval::PairLabel* pair = nullptr;
            double delta = 0.0;
            for (const probe_eval::PairLabel& possible :
                 label.pairs) {
                if (possible.first == best->key &&
                    possible.second == candidate.key) {
                    pair = &possible;
                    delta = possible.delta_q;
                    break;
                }
                if (possible.second == best->key &&
                    possible.first == candidate.key) {
                    pair = &possible;
                    delta = -possible.delta_q;
                    break;
                }
            }
            if (pair == nullptr) {
                throw std::invalid_argument(
                    "probe label lacks a best-versus-action pair");
            }
            const bool below_effect =
                std::abs(delta) <
                probe_eval::kStablePairMinimumDelta;
            const bool crosses_zero =
                std::abs(delta) <=
                probe_eval::kNormal95CriticalValue *
                    pair->paired_standard_error;
            if (!below_effect && !crosses_zero) {
                continue;
            }
            const std::size_t deck =
                static_cast<std::size_t>(label.root_deck);
            ++summary.pair_count;
            ++summary.by_deck[deck].pair_count;
            summary.pairs.push_back({
                .stable_id = label.stable_id,
                .root_deck = label.root_deck,
                .reference_best = best->key,
                .other = candidate.key,
                .delta_q = delta,
                .paired_standard_error =
                    pair->paired_standard_error,
                .effect_below_stable_threshold = below_effect,
                .confidence_interval_crosses_zero = crosses_zero,
            });
        }
    }
    std::sort(
        summary.pairs.begin(), summary.pairs.end(),
        [](const LowMarginBestPair& left,
           const LowMarginBestPair& right) {
            if (left.stable_id != right.stable_id) {
                return left.stable_id < right.stable_id;
            }
            return left.other < right.other;
        });
    return summary;
}

ProbeReferenceSamples generate_probe_reference_samples(
    const probes::DecisionProbe& probe,
    std::shared_ptr<const LearnedModel> actor_model,
    const ProbeScoreConfig& config) {
    validate_score_config(config);
    if (!actor_model) {
        throw std::invalid_argument(
            "reference labeling requires a frozen Actor model");
    }
    const auto validation = probes::validate_probe(probe);
    if (!validation.ok()) {
        throw std::invalid_argument(
            "cannot label invalid probe " + probe.stable_id);
    }
    return generate_variant_reference_samples(
        probe, std::move(actor_model), config,
        LearnedVariant::UnifiedActor, true);
}

ProbeScoreReport score_probe_dev_v2(
    const ProbeScoreConfig& config, std::ostream& progress) {
    validate_score_config(config);
    const std::vector<probes::DecisionProbe> corpus =
        probes::make_probe_dev_v2();
    progress << "Training frozen Actor reference/scoring model (seed "
             << config.training_seed << ", "
             << config.training_games << " games)..."
             << std::flush;
    std::shared_ptr<const LearnedModel> actor_model =
        train_learned_actor_model(
            config.training_games, config.training_seed);
    progress << " done\n";
    const std::string actor_fingerprint =
        learned_model_fingerprint(actor_model);
    const ProbeCacheMetadata metadata =
        make_probe_cache_metadata(
            config, corpus, actor_fingerprint);
    std::vector<probe_eval::ProbeLabel> labels;
    ProbeCacheStatus cache_status = ProbeCacheStatus::Loaded;

    const bool cache_exists =
        std::filesystem::exists(config.cache_path);
    if (cache_exists && !config.refresh_cache) {
        progress << "Loading probe labels from "
                 << config.cache_path.string() << "...\n"
                 << std::flush;
        labels = load_probe_label_cache(
            config.cache_path, metadata, corpus);
    } else {
        cache_status = ProbeCacheStatus::Generated;
        std::vector<ProbeReferenceSamples> raw;
        raw.reserve(corpus.size());
        for (std::size_t probe = 0; probe < corpus.size();
             ++probe) {
            progress << "Labeling probe " << probe + 1 << '/'
                     << corpus.size() << " ("
                     << corpus[probe].stable_id << ")..."
                     << std::flush;
            raw.push_back(generate_probe_reference_samples(
                corpus[probe], actor_model, config));
            progress << " done\n";
        }
        labels.reserve(raw.size());
        for (const ProbeReferenceSamples& probe : raw) {
            labels.push_back(probe_eval::make_probe_label(
                probe.stable_id, probe.root_deck,
                probe.candidates));
        }
        probe_eval::validate_probe_labels(labels);
        write_probe_label_cache_atomic(
            config.cache_path, metadata, corpus, raw);
        progress << "Published deterministic probe cache to "
                 << config.cache_path.string() << '\n';
    }

    progress << "Training frozen Value scoring model (seed "
             << config.training_seed << ", "
             << config.training_games << " games)..."
             << std::flush;
    const auto value_model = train_learned_value_champion(
        config.training_games, config.training_seed);
    const std::string value_fingerprint =
        learned_model_fingerprint(value_model);
    progress << " done\nCross-checking the Actor reference with "
                "identical-config Value continuations...\n";
    std::vector<probe_eval::ProbeLabel> value_reference_labels;
    std::vector<probe_eval::ProbePrediction>
        value_reference_predictions;
    value_reference_labels.reserve(corpus.size());
    value_reference_predictions.reserve(corpus.size());
    for (std::size_t probe_index = 0;
         probe_index < corpus.size(); ++probe_index) {
        const probes::DecisionProbe& probe = corpus[probe_index];
        progress << "  Cross-check probe " << probe_index + 1 << '/'
                 << corpus.size() << " (" << probe.stable_id
                 << ")..." << std::flush;
        const ProbeReferenceSamples samples =
            generate_variant_reference_samples(
                probe, value_model, config,
                LearnedVariant::ValueSearchChampion, false);
        const probe_eval::ProbeLabel label =
            probe_eval::make_probe_label(
                samples.stable_id, samples.root_deck,
                samples.candidates);
        std::vector<double> scores;
        scores.reserve(label.candidates.size());
        for (const auto& candidate : label.candidates) {
            scores.push_back(candidate.q);
        }
        value_reference_predictions.push_back(make_prediction(
            probe, scores,
            learned_critic_value(
                probe.state, probe.root_player, value_model)));
        value_reference_labels.push_back(label);
        progress << " done\n";
    }
    const ReferenceSensitivitySummary reference_sensitivity =
        compare_continuation_labels(labels,
                                    value_reference_labels);

    progress << "Scoring five policy views and their hidden-zone "
                "repartition clones on "
             << corpus.size() << " diagnostic positions..."
             << std::flush;

    const std::vector<probes::DecisionProbe> hidden_clones =
        hidden_clone_corpus(corpus);
    if (corpus_information_set_fingerprint(hidden_clones) !=
        metadata.information_set_fingerprint) {
        throw std::runtime_error(
            "hidden clone changed corpus information-set fingerprint");
    }

    const auto actor_raw = score_actor_raw(corpus, actor_model);
    const auto actor_raw_clone =
        score_actor_raw(hidden_clones, actor_model);
    const auto actor_deployed =
        score_actor_deployed(corpus, actor_model);
    const auto actor_deployed_clone =
        score_actor_deployed(hidden_clones, actor_model);
    const auto value_deployed =
        score_value_deployed(corpus, value_model);
    const auto value_deployed_clone =
        score_value_deployed(hidden_clones, value_model);
    const auto handcrafted = score_handcrafted(corpus);
    const auto handcrafted_clone =
        score_handcrafted(hidden_clones);
    const auto value_reference_clone = score_learned_search(
        hidden_clones, value_model,
        LearnedVariant::ValueSearchChampion, config.reference_worlds,
        config.reference_rollouts_per_world,
        config.reference_horizon_turns, false);
    require_critics_bit_identical(
        actor_raw, actor_deployed, "Actor");
    require_critics_bit_identical(
        value_deployed, value_reference_predictions, "Value");
    progress << " done\n";

    ProbeScoreReport report;
    report.metadata = metadata;
    report.cache_status = cache_status;
    report.cache_path = config.cache_path;
    report.reference_samples_per_candidate =
        reference_sample_count(metadata);
    report.value_model_fingerprint = value_fingerprint;
    report.reference_sensitivity = reference_sensitivity;
    report.low_margin =
        summarize_low_margin_best_pairs(labels);
    report.hidden_repartition = {
        .passed = true,
        .policy_count = 5,
        .probe_count = corpus.size(),
    };
    report.policies = {
        evaluate_hidden_invariant_policy(
            "Actor raw head",
            "Priority and Attack: raw masked policy logits",
            labels, actor_raw, actor_raw_clone, true),
        evaluate_hidden_invariant_policy(
            "Actor deployed policy",
            "Priority: K=2/H=0 information-set search with no "
            "shallow blend; Attack: raw masked policy head",
            labels, actor_deployed, actor_deployed_clone, true),
        evaluate_hidden_invariant_policy(
            "Value deployed policy",
            "Priority: K=2/H=4 Champion mirror with deployed "
            "aggregate shallow-prior blend; Attack: deployed "
            "public-board attack-set scorer",
            labels, value_deployed, value_deployed_clone, true),
        evaluate_hidden_invariant_policy(
            "Handcrafted agreement",
            "Priority and Attack: diagnostic agreement with the "
            "Actor-derived reference only; never labels/training",
            labels, handcrafted, handcrafted_clone, false),
        evaluate_hidden_invariant_policy(
            "Value-continuation deep cross-check",
            "Priority and Attack: diagnostic K/H/rollouts match "
            "the Actor reference; no shallow-prior blend and never "
            "overwrites labels",
            labels, value_reference_predictions,
            value_reference_clone, true),
    };
    return report;
}

std::string format_probe_score_report(
    const ProbeScoreReport& report) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::fixed << std::setprecision(4)
           << "\nProbe Dev-v2 Offline Score\n"
           << "WARNING: diagnostic only, 4 positions/deck; this "
              "cannot establish playing strength or a champion.\n"
           << "Reference: Actor-mirror common worlds, K="
           << report.metadata.worlds << ", H="
           << report.metadata.horizon_turns << ", rollouts/world="
           << report.metadata.rollouts_per_world
           << ", no shallow-prior blend\n"
           << "Reference algorithm/revision: "
           << report.metadata.algorithm << " / "
           << report.metadata.semantic_revision << '\n'
           << "Fixed reference seed: 0x"
           << hex_u64(report.metadata.reference_seed)
           << "\nProduction-policy RNG domain seed: 0x"
           << hex_u64(report.metadata.production_policy_seed)
           << '\n'
           << "Reference samples/candidate: "
           << report.reference_samples_per_candidate << '\n'
           << "Frozen training model: seed "
           << report.metadata.training_seed << ", "
           << report.metadata.training_games << " games\n"
           << "Reference Actor model fingerprint: "
           << report.metadata.reference_model_fingerprint << '\n'
           << "Diagnostic Value model fingerprint: "
           << report.value_model_fingerprint << '\n'
           << "Corpus: " << report.metadata.corpus_id
           << ", fingerprint "
           << report.metadata.information_set_fingerprint << '\n'
           << "Cache: "
           << (report.cache_status == ProbeCacheStatus::Loaded
                   ? "loaded"
                   : "generated")
           << " (" << report.cache_path.string() << ")\n"
           << "Handcrafted is shown only for agreement with the "
              "Actor-derived reference; it is not an objective "
              "ceiling and never contributes labels.\n"
           << "Hidden-repartition invariance: "
           << (report.hidden_repartition.passed ? "PASS" : "FAIL")
           << " — keyed policy scores, critics, and final metrics "
              "were bit-identical for "
           << report.hidden_repartition.policy_count
           << " policy views across "
           << report.hidden_repartition.probe_count
           << " probes.\n\n"
           << "Continuation-policy cross-check\n"
           << "  Actor-stable pairs: "
           << report.reference_sensitivity.actor_stable_pair_count
           << "\n  Actor-stable point-sign reversals "
              "(reference-sensitive): "
           << report.reference_sensitivity.point_sign_reversal_count
           << "\n  Reversals stable under both references: "
           << report.reference_sensitivity.dual_stable_reversal_count
           << "\n  Interpretation: reference-sensitive, not a causal "
              "continuation-policy attribution; the variants also use "
              "separately trained critics.\n";
    for (const DeckReferenceSensitivity& deck :
         report.reference_sensitivity.by_deck) {
        output << "  " << deck_name(deck.root_deck) << ": "
               << deck.point_sign_reversal_count
               << " point-sign reversals among "
               << deck.actor_stable_pair_count
               << " Actor-stable pairs; "
               << deck.dual_stable_reversal_count
               << " reversals are stable under both\n";
    }
    for (const ReferenceSensitivityFlag& flag :
         report.reference_sensitivity.flags) {
        output << "  [REFERENCE-SENSITIVE] " << flag.stable_id
               << ": " << flag.first << " vs " << flag.second
               << ", Actor delta " << flag.actor_delta_q
               << ", Value delta " << flag.value_delta_q
               << ", Value pair "
               << (flag.value_pair_is_stable ? "stable" : "not stable")
               << '\n';
    }
    if (report.reference_sensitivity.flags.empty()) {
        output << "  No Actor-stable pair sign reversals found.\n";
    }
    output << "\nLow-margin best-action pairs\n"
           << "  Needs targeted semantic/reference follow-up before "
              "being called stable: "
           << report.low_margin.pair_count << '\n';
    for (const DeckLowMarginSummary& deck :
         report.low_margin.by_deck) {
        output << "  " << deck_name(deck.root_deck) << ": "
               << deck.pair_count << '\n';
    }
    for (const LowMarginBestPair& pair :
         report.low_margin.pairs) {
        const double radius =
            probe_eval::kNormal95CriticalValue *
            pair.paired_standard_error;
        output << "  [ESCALATE] " << pair.stable_id << ": "
               << pair.reference_best << " vs " << pair.other
               << ", delta " << pair.delta_q << ", paired SE "
               << pair.paired_standard_error << ", 95% CI ["
               << pair.delta_q - radius << ", "
               << pair.delta_q + radius << "], reason ";
        if (pair.effect_below_stable_threshold) {
            output << "|delta|<"
                   << probe_eval::kStablePairMinimumDelta;
            if (pair.confidence_interval_crosses_zero) {
                output << " + ";
            }
        }
        if (pair.confidence_interval_crosses_zero) {
            output << "CI crosses zero";
        }
        output << '\n';
    }
    if (report.low_margin.pairs.empty()) {
        output << "  No best-versus-action pair requires "
                  "escalation.\n";
    }
    output << '\n' << report.policies.size() << " policy views\n"
           << "  Critic calibration is conditioned on each policy's "
              "selected-action reference Q; the same V(s) can therefore "
              "have different calibration metrics across policy rows.\n"
           << "  Candidate-Q fit is printed only for rows whose scores "
              "are Q probabilities, never for logits or handcrafted "
              "rankings.\n";

    for (const PolicyProbeReport& policy : report.policies) {
        output << policy.name << "\n  Config: "
               << policy.configuration << '\n';
        append_metric_line(
            output, "  ", "Pooled",
            pooled_deck_metrics(policy.metrics),
            policy.has_critic_metrics);
        for (const auto& deck : policy.metrics.by_deck) {
            append_metric_line(
                output, "  ", deck_name(deck.root_deck), deck,
                policy.has_critic_metrics);
        }
        if (policy.candidate_q_fit.has_value()) {
            const auto& fit = *policy.candidate_q_fit;
            output << "  Candidate-Q fit (candidate-weighted)\n";
            append_candidate_q_fit_line(
                output, "    ", "Pooled", fit.candidate_count,
                fit.mae, fit.rmse);
            for (const auto& deck : fit.by_deck) {
                append_candidate_q_fit_line(
                    output, "    ", deck_name(deck.root_deck),
                    deck.candidate_count, deck.mae, deck.rmse);
            }
        }
        output << '\n';
    }
    return output.str();
}

} // namespace alpha::probe_runner
