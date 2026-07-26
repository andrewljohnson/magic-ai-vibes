#include "old_school/probe_runner.hpp"

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
#include <functional>
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

namespace old_school::probe_runner {
namespace {

constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr std::size_t kProductionActorWorlds = 2;
constexpr std::size_t kProductionActorHorizon = 0;
constexpr std::size_t kProductionValueHorizon = 4;
constexpr std::size_t kMaximumReferenceWorlds = 4096;
constexpr std::size_t kMaximumReferenceHorizon = 128;
constexpr std::size_t kMaximumReferenceRollouts = 256;
constexpr std::string_view kProbeCacheMagic =
    "# old-school-probe-label-cache-v3";
constexpr std::string_view kProbeValidationCacheMagic =
    "# old-school-probe-validation-label-cache-v2";
constexpr std::string_view kProbeDevDefaultCachePath =
    "data/old-school-probe-dev-v3-env-v3.labels.tsv";
constexpr std::string_view kProbeValidationDefaultCachePath =
    "data/old-school-probe-validation-v1-env-v3.labels.tsv";

struct ProbeCorpusDefinition {
    std::string_view corpus_id;
    std::string_view cache_schema;
    std::string_view cache_magic;
    std::string_view semantic_revision;
    std::string_view default_cache_path;
};

ProbeCorpusDefinition corpus_definition(
    ProbeCorpusKind corpus_kind) {
    switch (corpus_kind) {
    case ProbeCorpusKind::DevV3:
        return {
            .corpus_id = probes::kProbeDevV3,
            .cache_schema = kProbeCacheSchema,
            .cache_magic = kProbeCacheMagic,
            .semantic_revision = kProbeSemanticRevision,
            .default_cache_path = kProbeDevDefaultCachePath,
        };
    case ProbeCorpusKind::ValidationV1:
        return {
            .corpus_id = probes::kProbeValidationV1,
            .cache_schema = kProbeValidationCacheSchema,
            .cache_magic = kProbeValidationCacheMagic,
            .semantic_revision = kProbeValidationSemanticRevision,
            .default_cache_path =
                kProbeValidationDefaultCachePath,
        };
    }
    throw std::invalid_argument("unknown probe corpus kind");
}

std::vector<probes::DecisionProbe> make_corpus(
    ProbeCorpusKind corpus_kind) {
    switch (corpus_kind) {
    case ProbeCorpusKind::DevV3:
        return probes::make_probe_dev_v3();
    case ProbeCorpusKind::ValidationV1:
        return probes::make_probe_validation_v1();
    }
    throw std::invalid_argument("unknown probe corpus kind");
}

std::vector<std::string> validate_corpus(
    ProbeCorpusKind corpus_kind,
    const std::vector<probes::DecisionProbe>& corpus) {
    switch (corpus_kind) {
    case ProbeCorpusKind::DevV3:
        return probes::validate_probe_dev_v3(corpus);
    case ProbeCorpusKind::ValidationV1:
        return probes::validate_probe_validation_v1(corpus);
    }
    throw std::invalid_argument("unknown probe corpus kind");
}

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
    hash.signed_integer(action.x_value);
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
    hash_card_vector(hash, player.exile);

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
        hash.signed_integer(creature.temporary_power_bonus);
        hash.signed_integer(creature.temporary_toughness_bonus);
        hash.boolean(creature.exile_on_death_this_turn);
    }

    hash.unsigned_integer(player.artifacts.size());
    for (const ArtifactPermanent& artifact : player.artifacts) {
        hash.unsigned_integer(artifact.id);
        hash.unsigned_integer(
            static_cast<std::uint64_t>(artifact.card));
        hash.boolean(artifact.tapped);
    }
    hash_card_vector(hash, player.enchantments);
    hash.signed_integer(player.mana_pool.generic);
    hash.signed_integer(player.mana_pool.green);
    hash.signed_integer(player.mana_pool.red);
    hash.signed_integer(player.mana_pool.blue);
    hash.signed_integer(player.mana_pool.white);
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
    for (const std::size_t extra_turns :
         state.extra_turns_pending) {
        hash.unsigned_integer(extra_turns);
    }
    for (const bool failed_draw : state.failed_draw) {
        hash.boolean(failed_draw);
    }
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
        hash.signed_integer(object.x_value);
    }

    hash.unsigned_integer(probe.original_decks.size());
    for (const std::vector<CardId>& deck : probe.original_decks) {
        // Decklists are known rules inputs. Hash their multisets in enum
        // order, never the shuffled library order in the state.
        std::array<std::size_t, kCardCount> counts{};
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

LearnedDecisionContext critic_context_for_probe(
    const probes::DecisionProbe& probe) {
    if (probe.decision_kind !=
        probes::DecisionKind::Priority) {
        // Attack probes are declaration choices, not live priority roots.
        // A contextual critic has no trained rules context for that boundary.
        return {};
    }
    return {
        .valid = true,
        .phase = probe.phase,
        .decision_player = probe.root_player,
        .consecutive_passes = probe.consecutive_passes,
        .sorcery_actions = sorcery_actions_for(probe.phase),
    };
}

double probe_critic_value(
    const probes::DecisionProbe& probe,
    const std::shared_ptr<const LearnedModel>& model) {
    return learned_contextual_critic_value(
        probe.state, probe.root_player,
        critic_context_for_probe(probe), model);
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
    if (config.scoring_value_worlds < 2) {
        throw std::invalid_argument(
            "probe scoring Value requires at least two worlds");
    }
    if (config.scoring_value_worlds > kMaximumReferenceWorlds) {
        throw std::invalid_argument(
            "probe scoring Value worlds must not exceed 4096");
    }
    if (!std::isfinite(
            config.scoring_value_continuation_epsilon) ||
        config.scoring_value_continuation_epsilon < 0.0 ||
        config.scoring_value_continuation_epsilon > 1.0) {
        throw std::invalid_argument(
            "probe scoring Value continuation epsilon must be "
            "finite and in [0, 1]");
    }
    if (config.cache_path.empty()) {
        throw std::invalid_argument(
            "probe cache path must not be empty");
    }
}

void validate_corpus_score_config(
    ProbeCorpusKind corpus_kind,
    const ProbeScoreConfig& config) {
    validate_score_config(config);
    if (corpus_kind == ProbeCorpusKind::ValidationV1 &&
        config.reference_rollouts_per_world != 1) {
        throw std::invalid_argument(
            "probe-validation-v1 requires exactly one rollout per "
            "world so its paired confidence interval does not treat "
            "within-world rollouts as independent");
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
    constexpr std::array<DeckId, kDeckCount> kProbeDecks = {
        DeckId::Green, DeckId::Red, DeckId::Blue, DeckId::White,
        DeckId::RUAggro};
    for (const DeckId id : kProbeDecks) {
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

ProbeCacheMetadata read_metadata(
    std::istream& input, std::string_view expected_magic) {
    const std::string magic =
        read_required_line(input, "cache magic");
    if (magic != expected_magic) {
        throw std::invalid_argument(
            "probe cache has an unknown magic header");
    }
    ProbeCacheMetadata metadata;
    metadata.schema = read_meta_value(input, "schema");
    metadata.algorithm = read_meta_value(input, "algorithm");
    metadata.semantic_revision =
        read_meta_value(input, "semantic_revision");
    metadata.environment_revision =
        read_meta_value(input, "environment_revision");
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
    if (actual.environment_revision !=
        expected.environment_revision) {
        return "environment_revision";
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
    std::string_view cache_magic,
    const ProbeCacheMetadata& metadata,
    const std::vector<probes::DecisionProbe>& corpus,
    const std::vector<ProbeReferenceSamples>& samples) {
    output.imbue(std::locale::classic());
    output << cache_magic << '\n'
           << "meta\tschema\t" << metadata.schema << '\n'
           << "meta\talgorithm\t" << metadata.algorithm << '\n'
           << "meta\tsemantic_revision\t"
           << metadata.semantic_revision << '\n'
           << "meta\tenvironment_revision\t"
           << metadata.environment_revision << '\n'
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
            priority_candidates(probe), probe.phase);
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
            probe_critic_value(probe, actor_model)));
    }
    return predictions;
}

std::vector<double> learned_search_scores(
    const probes::DecisionProbe& probe,
    std::shared_ptr<const LearnedModel> model,
    std::string_view corpus_id,
    LearnedVariant continuation_variant, std::size_t worlds,
    std::size_t rollouts_per_world, std::size_t horizon_turns,
    bool blend_shallow_prior,
    double value_continuation_epsilon = 0.0,
    double value_priority_residual_weight = 0.0) {
    const LearnedSearchConfig config{
        .seed = reference_seed_for_probe(
            corpus_id, probe.stable_id),
        .worlds = worlds,
        .rollouts_per_world = rollouts_per_world,
        .horizon_turns = horizon_turns,
        .continuation_variant = continuation_variant,
        .value_continuation_epsilon =
            value_continuation_epsilon,
        .blend_shallow_prior = blend_shallow_prior,
        .value_priority_residual_weight =
            value_priority_residual_weight,
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
    std::string_view corpus_id,
    LearnedVariant continuation_variant, std::size_t worlds,
    std::size_t rollouts_per_world, std::size_t horizon_turns,
    bool blend_shallow_prior) {
    std::vector<probe_eval::ProbePrediction> predictions;
    predictions.reserve(corpus.size());
    for (const probes::DecisionProbe& probe : corpus) {
        predictions.push_back(make_prediction(
            probe,
            learned_search_scores(
                probe, model, corpus_id, continuation_variant, worlds,
                rollouts_per_world, horizon_turns,
                blend_shallow_prior),
            probe_critic_value(probe, model)));
    }
    return predictions;
}

std::vector<probe_eval::ProbePrediction> score_actor_deployed(
    const std::vector<probes::DecisionProbe>& corpus,
    std::shared_ptr<const LearnedModel> actor_model,
    std::string_view corpus_id) {
    std::vector<probe_eval::ProbePrediction> predictions;
    predictions.reserve(corpus.size());
    for (const probes::DecisionProbe& probe : corpus) {
        std::vector<double> scores;
        if (probe.decision_kind ==
            probes::DecisionKind::Priority) {
            scores = learned_search_scores(
                probe, actor_model, corpus_id,
                LearnedVariant::UnifiedActor,
                kProductionActorWorlds, 1,
                kProductionActorHorizon, false);
        } else {
            // Deployed Actor combat is selected directly by the masked
            // policy head. It does not run the information-set evaluator.
            scores = actor_raw_scores(probe, actor_model);
        }
        predictions.push_back(make_prediction(
            probe, scores,
            probe_critic_value(probe, actor_model)));
    }
    return predictions;
}

LearnedValueAttackSetScores value_deployed_attack_scores(
    const probes::DecisionProbe& probe,
    std::shared_ptr<const LearnedModel> value_model,
    std::string_view corpus_id) {
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
        corpus_id, probe.stable_id,
        kProbeProductionPolicySeed);
    return learned_value_attack_set_scores(
        probe.state, probe.root_player, attack_sets,
        std::move(value_model), policy_seed);
}

std::vector<probe_eval::ProbePrediction> score_value_deployed(
    const std::vector<probes::DecisionProbe>& corpus,
    std::shared_ptr<const LearnedModel> value_model,
    std::string_view corpus_id, std::size_t worlds,
    double value_continuation_epsilon,
    double value_priority_residual_weight) {
    std::vector<probe_eval::ProbePrediction> predictions;
    predictions.reserve(corpus.size());
    for (const probes::DecisionProbe& probe : corpus) {
        std::vector<double> scores;
        std::optional<std::size_t> selected_candidate;
        if (probe.decision_kind ==
            probes::DecisionKind::Priority) {
            scores = learned_search_scores(
                probe, value_model, corpus_id,
                LearnedVariant::ValueSearchChampion,
                worlds, 1,
                kProductionValueHorizon, true,
                value_continuation_epsilon,
                value_priority_residual_weight);
        } else {
            const auto attack =
                value_deployed_attack_scores(
                    probe, value_model, corpus_id);
            scores = attack.scores;
            selected_candidate = attack.selected_candidate;
        }
        predictions.push_back(make_prediction(
            probe, scores,
            probe_critic_value(probe, value_model),
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

std::vector<std::string> deployed_selected_keys(
    const probe_eval::ProbePrediction& prediction) {
    if (prediction.policy_scores.empty()) {
        throw std::runtime_error(
            "deployed prediction has no policy scores");
    }
    if (prediction.selected_key.has_value()) {
        return {*prediction.selected_key};
    }

    const double highest_score = std::max_element(
        prediction.policy_scores.begin(),
        prediction.policy_scores.end(),
        [](const probe_eval::PolicyScore& left,
           const probe_eval::PolicyScore& right) {
            return left.score < right.score;
        })->score;
    std::vector<std::string> selected;
    for (const probe_eval::PolicyScore& score :
         prediction.policy_scores) {
        if (score.score == highest_score) {
            selected.push_back(score.key);
        }
    }
    std::sort(selected.begin(), selected.end());
    return selected;
}

ForceSpikeControlDecision make_force_spike_control_decision(
    const probe_eval::ProbePrediction& prediction) {
    return {
        .stable_id = prediction.stable_id,
        .pass_score =
            policy_score_for(prediction, "pass").score,
        .force_spike_score =
            policy_score_for(
                prediction, "force-spike-gray-ogre")
                .score,
        .selected_keys = deployed_selected_keys(prediction),
    };
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

ValueProbeDecisionDetail build_value_probe_decision_detail(
    const probe_eval::ProbeLabel& label,
    const probe_eval::ProbePrediction& prediction,
    const ValueProbeDecisionDetail* reference,
    const ValueProbeDecisionDetail* previous) {
    probe_eval::validate_probe_predictions({label}, {prediction});

    std::vector<std::string> selected_keys;
    bool deterministic_selection = false;
    if (prediction.selected_key.has_value()) {
        selected_keys.push_back(*prediction.selected_key);
        deterministic_selection = true;
    } else {
        const double highest_score = std::max_element(
            prediction.policy_scores.begin(),
            prediction.policy_scores.end(),
            [](const probe_eval::PolicyScore& left,
               const probe_eval::PolicyScore& right) {
                return left.score < right.score;
            })->score;
        for (const probe_eval::PolicyScore& score :
             prediction.policy_scores) {
            if (score.score == highest_score) {
                selected_keys.push_back(score.key);
            }
        }
    }
    std::sort(selected_keys.begin(), selected_keys.end());

    double selected_reference_q = 0.0;
    for (const std::string& key : selected_keys) {
        const auto candidate = std::find_if(
            label.candidates.begin(), label.candidates.end(),
            [&key](const probe_eval::CandidateLabel& item) {
                return item.key == key;
            });
        if (candidate == label.candidates.end()) {
            throw std::invalid_argument(
                "Value detail selection is missing from reference label");
        }
        selected_reference_q += candidate->q;
    }
    selected_reference_q /=
        static_cast<double>(selected_keys.size());

    std::vector<std::string> reference_best_set =
        label.reference_best_set;
    std::sort(reference_best_set.begin(),
              reference_best_set.end());
    const auto selection_differs =
        [&selected_keys, deterministic_selection](
            const ValueProbeDecisionDetail* other) {
            return other != nullptr &&
                   (other->selected_keys != selected_keys ||
                    other->deterministic_selection !=
                        deterministic_selection);
        };
    const bool selection_changed_from_reference =
        selection_differs(reference);
    const bool selection_changed_from_previous =
        selection_differs(previous);
    return {
        .stable_id = label.stable_id,
        .root_deck = label.root_deck,
        .selected_keys = std::move(selected_keys),
        .deterministic_selection = deterministic_selection,
        .reference_best_set = std::move(reference_best_set),
        .regret =
            label.reference_value - selected_reference_q,
        .critic_prediction = prediction.critic_value,
        .selected_action_reference_q = selected_reference_q,
        .critic_error =
            prediction.critic_value - selected_reference_q,
        .selection_changed_from_reference =
            selection_changed_from_reference,
        .selection_changed_from_previous =
            selection_changed_from_previous,
    };
}

ValueCheckpointProbeReport make_value_checkpoint_report(
    std::string name, std::string fingerprint,
    double value_priority_residual_weight,
    const std::vector<probe_eval::ProbeLabel>& labels,
    const std::vector<probe_eval::ProbePrediction>& predictions,
    const std::vector<probe_eval::ProbePrediction>& clone_predictions,
    const ValueCheckpointProbeReport* reference,
    const ValueCheckpointProbeReport* previous) {
    const PolicyProbeReport evaluated =
        evaluate_hidden_invariant_policy(
            name, "deployed Value checkpoint", labels,
            predictions, clone_predictions, true);
    ValueCheckpointProbeReport checkpoint{
        .name = std::move(name),
        .fingerprint = std::move(fingerprint),
        .transition_parent_name =
            previous == nullptr ? std::string{} : previous->name,
        .value_priority_residual_weight =
            value_priority_residual_weight,
        .metrics = evaluated.metrics,
    };
    checkpoint.decisions.reserve(labels.size());
    for (const probe_eval::ProbeLabel& label : labels) {
        const auto& prediction =
            prediction_for(predictions, label.stable_id);
        const ValueProbeDecisionDetail* reference_detail = nullptr;
        if (reference != nullptr) {
            const auto found = std::find_if(
                reference->decisions.begin(),
                reference->decisions.end(),
                [&label](const ValueProbeDecisionDetail& detail) {
                    return detail.stable_id == label.stable_id;
                });
            if (found == reference->decisions.end()) {
                throw std::invalid_argument(
                    "reference Value checkpoint is missing a probe");
            }
            reference_detail = &*found;
        }
        const ValueProbeDecisionDetail* previous_detail = nullptr;
        if (previous != nullptr) {
            const auto found = std::find_if(
                previous->decisions.begin(),
                previous->decisions.end(),
                [&label](const ValueProbeDecisionDetail& detail) {
                    return detail.stable_id == label.stable_id;
                });
            if (found == previous->decisions.end()) {
                throw std::invalid_argument(
                    "previous Value checkpoint is missing a probe");
            }
            previous_detail = &*found;
        }
        checkpoint.decisions.push_back(
            build_value_probe_decision_detail(
                label, prediction, reference_detail,
                previous_detail));
    }
    std::sort(
        checkpoint.decisions.begin(), checkpoint.decisions.end(),
        [](const ValueProbeDecisionDetail& left,
           const ValueProbeDecisionDetail& right) {
            return left.stable_id < right.stable_id;
        });
    return checkpoint;
}

const std::string& unique_candidate_key(
    const probes::DecisionProbe& probe,
    const std::function<bool(const probes::Candidate&)>& matches,
    std::string_view description) {
    const std::string* key = nullptr;
    for (const probes::Candidate& candidate : probe.candidates) {
        if (!matches(candidate)) {
            continue;
        }
        if (key != nullptr) {
            throw std::invalid_argument(
                "focused pair has multiple " +
                std::string(description) + " candidates");
        }
        key = &candidate.descriptor;
    }
    if (key == nullptr) {
        throw std::invalid_argument(
            "focused pair is missing its " +
            std::string(description) + " candidate");
    }
    return *key;
}

CandidatePairEstimate oriented_pair_estimate(
    const probe_eval::ProbeLabel& label,
    std::string name, std::string_view first_key,
    std::string_view second_key,
    std::size_t samples_per_candidate = 0) {
    const probe_eval::PairLabel* matched = nullptr;
    double direction = 1.0;
    for (const probe_eval::PairLabel& pair : label.pairs) {
        if (pair.first == first_key &&
            pair.second == second_key) {
            matched = &pair;
            break;
        }
        if (pair.first == second_key &&
            pair.second == first_key) {
            matched = &pair;
            direction = -1.0;
            break;
        }
    }
    if (matched == nullptr) {
        throw std::invalid_argument(
            "focused pair is absent from the reference labels");
    }
    const double delta = direction * matched->delta_q;
    const double radius =
        probe_eval::kNormal95CriticalValue *
        matched->paired_standard_error;
    return {
        .name = std::move(name),
        .stable_id = label.stable_id,
        .root_deck = label.root_deck,
        .first_key = std::string(first_key),
        .second_key = std::string(second_key),
        .samples_per_candidate = samples_per_candidate,
        .delta_q = delta,
        .paired_standard_error =
            matched->paired_standard_error,
        .confidence_lower_95 = delta - radius,
        .confidence_upper_95 = delta + radius,
    };
}

struct FocusedCandidatePair {
    const probes::DecisionProbe* probe = nullptr;
    const std::string* first_key = nullptr;
    const std::string* second_key = nullptr;
};

std::optional<FocusedCandidatePair> focused_candidate_pair(
    ProbeCorpusKind corpus_kind,
    const std::vector<probes::DecisionProbe>& corpus) {
    if (corpus_kind == ProbeCorpusKind::DevV3) {
        return std::nullopt;
    }
    if (corpus.size() != 1) {
        throw std::invalid_argument(
            "validation-v1 focused pair requires its one "
            "canonical probe");
    }
    const probes::DecisionProbe& probe = corpus.front();
    const std::string& pass_key = unique_candidate_key(
        probe,
        [](const probes::Candidate& candidate) {
            const auto* action =
                std::get_if<PriorityAction>(&candidate.action);
            return action != nullptr &&
                   action->kind == PriorityActionKind::Pass;
        },
        "Pass");
    const std::string& x_zero_key = unique_candidate_key(
        probe,
        [&probe](const probes::Candidate& candidate) {
            const auto* action =
                std::get_if<PriorityAction>(&candidate.action);
            return action != nullptr &&
                   action->kind ==
                       PriorityActionKind::CastDisintegrate &&
                   action->x_value == 0 &&
                   action->target.has_value() &&
                   !action->target->creature.has_value() &&
                   action->target->player ==
                       1 - probe.root_player;
        },
        "opponent-targeted X=0 Disintegrate");
    return FocusedCandidatePair{
        .probe = &probe,
        .first_key = &pass_key,
        .second_key = &x_zero_key,
    };
}

std::vector<CandidatePairEstimate> focused_candidate_pairs(
    ProbeCorpusKind corpus_kind,
    const std::vector<probes::DecisionProbe>& corpus,
    const std::vector<probe_eval::ProbeLabel>& labels,
    std::size_t samples_per_candidate) {
    const auto focused =
        focused_candidate_pair(corpus_kind, corpus);
    if (!focused.has_value()) {
        return {};
    }
    if (labels.size() != 1 ||
        corpus.front().stable_id != labels.front().stable_id) {
        throw std::invalid_argument(
            "validation-v1 focused pair requires its one "
            "canonical labeled probe");
    }
    return {oriented_pair_estimate(
        labels.front(), "Actor reference Q(Pass) - Q(X=0)",
        *focused->first_key, *focused->second_key,
        samples_per_candidate)};
}

ProbeReferenceSamples generate_variant_reference_samples(
    const probes::DecisionProbe& probe,
    std::shared_ptr<const LearnedModel> model,
    const ProbeScoreConfig& config, std::string_view corpus_id,
    LearnedVariant variant,
    bool verify_hidden_repartition) {
    const LearnedSearchConfig search{
        .seed = reference_seed_for_probe(
            corpus_id, probe.stable_id),
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

std::vector<CandidatePairEstimate> score_value_candidate_pair(
    ProbeCorpusKind corpus_kind,
    const std::vector<probes::DecisionProbe>& corpus,
    std::shared_ptr<const LearnedModel> model, std::string name,
    std::string_view corpus_id, const ProbeScoreConfig& config,
    double value_priority_residual_weight) {
    const auto focused =
        focused_candidate_pair(corpus_kind, corpus);
    if (!focused.has_value()) {
        return {};
    }
    const LearnedSearchConfig search{
        .seed = reference_seed_for_probe(
            corpus_id, focused->probe->stable_id),
        .worlds = config.scoring_value_worlds,
        .rollouts_per_world = 1,
        .horizon_turns = kProductionValueHorizon,
        .continuation_variant =
            LearnedVariant::ValueSearchChampion,
        .value_continuation_epsilon =
            config.scoring_value_continuation_epsilon,
        .blend_shallow_prior = true,
        .value_priority_residual_weight =
            value_priority_residual_weight,
    };
    const LearnedActionSamples original =
        score_probe_actions(
            *focused->probe, focused->probe->state, model, search);
    const GameState clone =
        hidden_repartition_clone(*focused->probe);
    const LearnedActionSamples repartitioned =
        score_probe_actions(
            *focused->probe, clone, model, search);
    require_bit_identical(
        original, repartitioned,
        focused->probe->stable_id + " " + name);

    const auto candidate_samples =
        map_candidate_samples(*focused->probe, original);
    const probe_eval::ProbeLabel label =
        probe_eval::make_probe_label(
            focused->probe->stable_id,
            focused->probe->root_deck, candidate_samples);
    return {oriented_pair_estimate(
        label, std::move(name), *focused->first_key,
        *focused->second_key,
        config.scoring_value_worlds)};
}

const probes::DecisionProbe& probe_with_stable_id(
    const std::vector<probes::DecisionProbe>& corpus,
    std::string_view stable_id) {
    const auto found = std::find_if(
        corpus.begin(), corpus.end(),
        [stable_id](const probes::DecisionProbe& probe) {
            return probe.stable_id == stable_id;
        });
    if (found == corpus.end()) {
        throw std::invalid_argument(
            "teacher audit fixture is missing " +
            std::string(stable_id));
    }
    return *found;
}

bool action_samples_bit_identical(
    const LearnedActionSamples& first,
    const LearnedActionSamples& second) {
    if (first.sampled_worlds != second.sampled_worlds ||
        first.rollout_evaluations != second.rollout_evaluations ||
        first.terminal_evaluations != second.terminal_evaluations ||
        first.bootstrapped_evaluations !=
            second.bootstrapped_evaluations ||
        first.q_samples.size() != second.q_samples.size()) {
        return false;
    }
    for (std::size_t candidate = 0;
         candidate < first.q_samples.size(); ++candidate) {
        if (first.q_samples[candidate].size() !=
            second.q_samples[candidate].size()) {
            return false;
        }
        for (std::size_t sample = 0;
             sample < first.q_samples[candidate].size(); ++sample) {
            if (!bit_identical(
                    first.q_samples[candidate][sample],
                    second.q_samples[candidate][sample])) {
                return false;
            }
        }
    }
    return true;
}

const probe_eval::CandidateSamples& candidate_samples_for(
    const std::vector<probe_eval::CandidateSamples>& samples,
    std::string_view key) {
    const auto found = std::find_if(
        samples.begin(), samples.end(),
        [key](const probe_eval::CandidateSamples& candidate) {
            return candidate.key == key;
        });
    if (found == samples.end()) {
        throw std::invalid_argument(
            "teacher audit samples are missing candidate " +
            std::string(key));
    }
    return *found;
}

std::size_t conservative_terminal_bound_turns(
    const probes::DecisionProbe& probe) {
    const std::size_t first =
        probe.state.players[0].library.size();
    const std::size_t second =
        probe.state.players[1].library.size();
    if (second ==
            std::numeric_limits<std::size_t>::max() ||
        first >
            std::numeric_limits<std::size_t>::max() -
                second - 1) {
        throw std::overflow_error(
            "teacher audit conservative terminal bound overflows "
            "size_t");
    }
    return first + second + 1;
}

std::size_t total_candidate_samples(
    const LearnedActionSamples& samples) {
    std::size_t total = 0;
    for (const auto& candidate : samples.q_samples) {
        if (candidate.size() >
            std::numeric_limits<std::size_t>::max() - total) {
            throw std::overflow_error(
                "teacher audit candidate sample count overflows "
                "size_t");
        }
        total += candidate.size();
    }
    return total;
}

std::size_t expected_teacher_evaluations(
    std::size_t candidate_count,
    const TeacherSufficiencyAuditConfig& config) {
    if (candidate_count != 0 &&
        config.worlds >
            std::numeric_limits<std::size_t>::max() /
                candidate_count) {
        throw std::overflow_error(
            "teacher audit expected evaluation count overflows "
            "size_t");
    }
    return candidate_count * config.worlds;
}

TeacherOptionComparison score_teacher_option_comparison(
    const probes::DecisionProbe& probe, std::string_view corpus_id,
    std::shared_ptr<const LearnedModel> model,
    const TeacherSufficiencyAuditConfig& config,
    std::string description, std::string_view first_key,
    std::string_view second_key) {
    const LearnedSearchConfig search{
        .seed = reference_seed_for_probe(
            corpus_id, probe.stable_id),
        .worlds = config.worlds,
        .rollouts_per_world = 1,
        .horizon_turns = config.horizon_turns,
        .continuation_variant = config.continuation_variant,
        .blend_shallow_prior = config.blend_shallow_prior,
        .evaluation_threads = config.evaluation_threads,
    };
    const LearnedActionSamples original =
        score_probe_actions(probe, probe.state, model, search);
    const LearnedActionSamples hidden =
        score_probe_actions(
            probe, hidden_repartition_clone(probe), model, search);
    if (original.sampled_worlds != config.worlds) {
        throw std::runtime_error(
            "teacher audit scorer returned the wrong world count");
    }

    const auto candidates =
        map_candidate_samples(probe, original);
    const probe_eval::ProbeLabel label =
        probe_eval::make_probe_label(
            probe.stable_id, probe.root_deck, candidates);
    const auto& first =
        candidate_samples_for(candidates, first_key);
    const auto& second =
        candidate_samples_for(candidates, second_key);
    const auto prediction = make_prediction(
        probe, means_of(original), probe_critic_value(probe, model));
    const std::size_t terminal_bound =
        conservative_terminal_bound_turns(probe);
    return {
        .description = std::move(description),
        .estimate = oriented_pair_estimate(
            label, "Q(" + std::string(first_key) + ") - Q(" +
                       std::string(second_key) + ")",
            first_key, second_key, config.worlds),
        .selected_keys = deployed_selected_keys(prediction),
        .ordered_blocks = summarize_ordered_pair_blocks(
            first.q_samples, second.q_samples),
        .hidden_repartition_bit_identical =
            action_samples_bit_identical(original, hidden),
        .candidate_count = original.q_samples.size(),
        .recorded_candidate_samples =
            total_candidate_samples(original),
        .expected_evaluations = expected_teacher_evaluations(
            probe.candidates.size(), config),
        .rollout_evaluations =
            original.rollout_evaluations,
        .terminal_evaluations =
            original.terminal_evaluations,
        .bootstrapped_evaluations =
            original.bootstrapped_evaluations,
        .conservative_terminal_bound_turns = terminal_bound,
        .conservative_terminal_bound_satisfied =
            config.horizon_turns >= terminal_bound,
    };
}

} // namespace

std::size_t
OrderedPairBlockSummary::required_correct_block_count() const {
    return block_count - block_count / 4;
}

bool OrderedPairBlockSummary::gate_passed() const {
    return block_count != 0 &&
           correct_block_count >= required_correct_block_count();
}

OrderedPairBlockSummary summarize_ordered_pair_blocks(
    const std::vector<double>& first,
    const std::vector<double>& second,
    std::size_t worlds_per_block) {
    if (first.empty() || first.size() != second.size()) {
        throw std::invalid_argument(
            "ordered pair blocks require equally sized, nonempty "
            "sample rows");
    }
    if (worlds_per_block == 0 ||
        first.size() % worlds_per_block != 0) {
        throw std::invalid_argument(
            "ordered pair sample count must be divisible by the "
            "nonzero block size");
    }
    OrderedPairBlockSummary summary{
        .worlds_per_block = worlds_per_block,
        .block_count = first.size() / worlds_per_block,
    };
    for (std::size_t block = 0; block < summary.block_count;
         ++block) {
        double first_sum = 0.0;
        double second_sum = 0.0;
        const std::size_t begin = block * worlds_per_block;
        const std::size_t end = begin + worlds_per_block;
        for (std::size_t sample = begin; sample < end; ++sample) {
            if (!std::isfinite(first[sample]) ||
                !std::isfinite(second[sample])) {
                throw std::invalid_argument(
                    "ordered pair blocks require finite samples");
            }
            first_sum += first[sample];
            second_sum += second[sample];
        }
        if (first_sum > second_sum) {
            ++summary.correct_block_count;
        }
    }
    return summary;
}

bool TeacherOptionComparison::confidence_gate_passed() const {
    return estimate.confidence_lower_95 > 0.0;
}

bool TeacherOptionComparison::block_gate_passed() const {
    return ordered_blocks.gate_passed();
}

bool TeacherOptionComparison::gate_passed() const {
    return hidden_repartition_bit_identical &&
           confidence_gate_passed() && block_gate_passed();
}

bool TeacherOptionComparison::evaluation_accounting_is_exact()
    const {
    return candidate_count != 0 &&
           expected_evaluations != 0 &&
           recorded_candidate_samples == expected_evaluations &&
           rollout_evaluations == expected_evaluations &&
           terminal_evaluations <= rollout_evaluations &&
           bootstrapped_evaluations ==
               rollout_evaluations - terminal_evaluations;
}

bool TeacherOptionComparison::terminal_results_gate_passed()
    const {
    return conservative_terminal_bound_satisfied &&
           evaluation_accounting_is_exact() &&
           terminal_evaluations == expected_evaluations &&
           bootstrapped_evaluations == 0;
}

bool TeacherOptionComparison::
    second_key_excluded_from_selected_set() const {
    return !selected_keys.empty() &&
           std::find(selected_keys.begin(), selected_keys.end(),
                     estimate.second_key) == selected_keys.end();
}

bool TeacherSufficiencyAuditReport::gate_passed() const {
    return hidden_repartition_bit_identical &&
           force_spike_live.gate_passed() &&
           force_spike_payable.gate_passed() &&
           disintegrate_x_zero.gate_passed();
}

bool terminal_credit_primary_gate_passed(
    const TeacherSufficiencyAuditReport& report) {
    const auto comparison_passes =
        [](const TeacherOptionComparison& comparison) {
            return comparison.hidden_repartition_bit_identical &&
                   comparison.confidence_gate_passed() &&
                   comparison.block_gate_passed() &&
                   comparison.terminal_results_gate_passed();
        };
    return report.config.require_terminal_results &&
           !report.config.blend_shallow_prior &&
           comparison_passes(report.force_spike_live) &&
           comparison_passes(report.force_spike_payable);
}

bool ForceSpikePolicyControlReport::live_selects_force_spike()
    const {
    return live.selected_keys ==
           std::vector<std::string>{"force-spike-gray-ogre"};
}

bool ForceSpikePolicyControlReport::payable_selects_pass() const {
    return payable.selected_keys ==
           std::vector<std::string>{"pass"};
}

bool ForceSpikePolicyControlReport::gate_passed() const {
    return hidden_repartition_passed &&
           live_selects_force_spike() && payable_selects_pass();
}

ForceSpikePolicyControlReport
score_value_force_spike_policy_controls(
    std::shared_ptr<const LearnedModel> model,
    std::string policy_name, std::size_t worlds,
    double value_continuation_epsilon,
    double value_priority_residual_weight) {
    if (!model) {
        throw std::invalid_argument(
            "Force Spike control scoring requires a frozen Value "
            "model");
    }
    validate_text_field(policy_name,
                        "Force Spike control policy name");
    if (worlds < 2 || worlds > kMaximumReferenceWorlds) {
        throw std::invalid_argument(
            "Force Spike control scoring worlds must be in "
            "[2, 4096]");
    }
    if (!std::isfinite(value_continuation_epsilon) ||
        value_continuation_epsilon < 0.0 ||
        value_continuation_epsilon > 1.0) {
        throw std::invalid_argument(
            "Force Spike control continuation epsilon must be "
            "finite and in [0, 1]");
    }
    if (!std::isfinite(value_priority_residual_weight) ||
        value_priority_residual_weight < 0.0 ||
        value_priority_residual_weight > 1.0) {
        throw std::invalid_argument(
            "Force Spike control Value Priority residual weight "
            "must be finite and in [0, 1]");
    }

    const std::vector<probes::DecisionProbe> controls =
        probes::make_force_spike_policy_controls_v1();
    const std::vector<std::string> validation_errors =
        probes::validate_force_spike_policy_controls_v1(controls);
    if (!validation_errors.empty()) {
        std::ostringstream message;
        message << "invalid Force Spike policy controls";
        for (const std::string& error : validation_errors) {
            message << "; " << error;
        }
        throw std::runtime_error(message.str());
    }

    const auto predictions = score_value_deployed(
        controls, model, probes::kForceSpikePolicyControlsV1,
        worlds, value_continuation_epsilon,
        value_priority_residual_weight);
    const std::vector<probes::DecisionProbe> hidden_clones =
        hidden_clone_corpus(controls);
    const auto clone_predictions = score_value_deployed(
        hidden_clones, model,
        probes::kForceSpikePolicyControlsV1, worlds,
        value_continuation_epsilon,
        value_priority_residual_weight);
    require_predictions_bit_identical(
        predictions, clone_predictions, policy_name);

    constexpr std::string_view kLiveId =
        "control.blue.force-spike-live-gray-ogre.v1";
    constexpr std::string_view kPayableId =
        "control.blue.force-spike-payable-gray-ogre.v1";
    return {
        .policy_name = std::move(policy_name),
        .model_fingerprint = learned_model_fingerprint(model),
        .worlds = worlds,
        .horizon_turns = kProductionValueHorizon,
        .live = make_force_spike_control_decision(
            prediction_for(predictions, kLiveId)),
        .payable = make_force_spike_control_decision(
            prediction_for(predictions, kPayableId)),
        .hidden_repartition_passed = true,
        .value_priority_residual_weight =
            value_priority_residual_weight,
    };
}

TeacherSufficiencyAuditReport score_teacher_sufficiency_audit(
    std::shared_ptr<const LearnedModel> model,
    std::string policy_name,
    TeacherSufficiencyAuditConfig config) {
    if (!model) {
        throw std::invalid_argument(
            "teacher-sufficiency audit requires a frozen model");
    }
    validate_text_field(
        policy_name, "teacher-sufficiency policy name");
    if (config.worlds < kTeacherAuditBlockWorlds ||
        config.worlds > kMaximumReferenceWorlds ||
        config.worlds % kTeacherAuditBlockWorlds != 0) {
        throw std::invalid_argument(
            "teacher-sufficiency worlds must be a multiple of 8 "
            "in [8, 4096]");
    }
    if (config.horizon_turns > kMaximumReferenceHorizon) {
        throw std::invalid_argument(
            "teacher-sufficiency horizon must be at most 128 turns");
    }
    if (config.require_terminal_results &&
        config.blend_shallow_prior) {
        throw std::invalid_argument(
            "full-terminal teacher audit cannot blend a shallow "
            "critic prior");
    }
    switch (config.continuation_variant) {
    case LearnedVariant::ValueSearchChampion:
    case LearnedVariant::UnifiedActor:
        break;
    default:
        throw std::invalid_argument(
            "teacher-sufficiency continuation variant is invalid");
    }

    const std::vector<probes::DecisionProbe> controls =
        probes::make_force_spike_policy_controls_v1();
    const std::vector<std::string> control_errors =
        probes::validate_force_spike_policy_controls_v1(controls);
    if (!control_errors.empty()) {
        throw std::runtime_error(
            "invalid Force Spike teacher-audit controls: " +
            control_errors.front());
    }
    const std::vector<probes::DecisionProbe> validation =
        probes::make_probe_validation_v1();
    const std::vector<std::string> validation_errors =
        probes::validate_probe_validation_v1(validation);
    if (!validation_errors.empty()) {
        throw std::runtime_error(
            "invalid validation-v1 teacher-audit corpus: " +
            validation_errors.front());
    }

    constexpr std::string_view kLiveId =
        "control.blue.force-spike-live-gray-ogre.v1";
    constexpr std::string_view kPayableId =
        "control.blue.force-spike-payable-gray-ogre.v1";
    const probes::DecisionProbe& live =
        probe_with_stable_id(controls, kLiveId);
    const probes::DecisionProbe& payable =
        probe_with_stable_id(controls, kPayableId);
    const auto pass_key_for =
        [](const probes::DecisionProbe& probe)
        -> const std::string& {
        return unique_candidate_key(
            probe,
            [](const probes::Candidate& candidate) {
                const auto* action =
                    std::get_if<PriorityAction>(&candidate.action);
                return action != nullptr &&
                       action->kind == PriorityActionKind::Pass;
            },
            "Pass");
    };
    const auto force_spike_key_for =
        [](const probes::DecisionProbe& probe)
        -> const std::string& {
        return unique_candidate_key(
            probe,
            [](const probes::Candidate& candidate) {
                const auto* action =
                    std::get_if<PriorityAction>(&candidate.action);
                return action != nullptr &&
                       action->kind ==
                           PriorityActionKind::CastForceSpike;
            },
            "Force Spike");
    };

    const auto validation_pair = focused_candidate_pair(
        ProbeCorpusKind::ValidationV1, validation);
    if (!validation_pair.has_value()) {
        throw std::logic_error(
            "validation-v1 teacher audit has no focused pair");
    }
    if (config.require_terminal_results) {
        for (const probes::DecisionProbe* probe :
             std::array{
                 &live, &payable, validation_pair->probe}) {
            const std::size_t required =
                conservative_terminal_bound_turns(*probe);
            if (config.horizon_turns < required) {
                throw std::invalid_argument(
                    "full-terminal teacher audit horizon H=" +
                    std::to_string(config.horizon_turns) +
                    " is below conservative bound " +
                    std::to_string(required) + " for " +
                    probe->stable_id +
                    " (sum of both libraries plus one turn)");
            }
        }
    }

    TeacherSufficiencyAuditReport report;
    report.policy_name = std::move(policy_name);
    report.model_fingerprint = learned_model_fingerprint(model);
    report.config = config;
    report.force_spike_live = score_teacher_option_comparison(
        live, probes::kForceSpikePolicyControlsV1, model, config,
        "Force Spike live: counter versus Pass",
        force_spike_key_for(live), pass_key_for(live));
    report.force_spike_payable =
        score_teacher_option_comparison(
            payable, probes::kForceSpikePolicyControlsV1, model,
            config,
            "Force Spike payable: Pass versus counter",
            pass_key_for(payable),
            force_spike_key_for(payable));
    report.disintegrate_x_zero =
        score_teacher_option_comparison(
            *validation_pair->probe, probes::kProbeValidationV1,
            model, config,
            "RU hold: Pass versus opponent-targeted "
            "Disintegrate X=0",
            *validation_pair->first_key,
            *validation_pair->second_key);
    report.hidden_repartition_bit_identical =
        report.force_spike_live
                .hidden_repartition_bit_identical &&
        report.force_spike_payable
                .hidden_repartition_bit_identical &&
        report.disintegrate_x_zero
                .hidden_repartition_bit_identical;
    return report;
}

std::string format_teacher_sufficiency_audit_report(
    const std::vector<TeacherSufficiencyAuditReport>& reports) {
    if (reports.empty()) {
        throw std::invalid_argument(
            "teacher-sufficiency report requires at least one row");
    }
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::fixed << std::setprecision(6)
           << "P16 Search-Teacher Sufficiency Audit\n"
           << "Evaluation-only; fixed physical fixtures, paired "
              "common worlds, and no probe-label cache access or "
              "mutation.\n"
           << "The first row is primary. Diagnostic rows cannot "
              "substitute for its conjunctive gate.\n";

    const auto append_keys =
        [&output](const std::vector<std::string>& keys) {
        output << '{';
        for (std::size_t index = 0; index < keys.size(); ++index) {
            if (index != 0) {
                output << ", ";
            }
            output << keys[index];
        }
        output << '}';
    };
    const auto append_comparison =
        [&output, &append_keys](
            const TeacherOptionComparison& comparison) {
        const CandidatePairEstimate& estimate =
            comparison.estimate;
        output
            << "    " << comparison.description << '\n'
            << "      oriented delta: Q(" << estimate.first_key
            << ") - Q(" << estimate.second_key << ") = "
            << estimate.delta_q << ", paired SE "
            << estimate.paired_standard_error << ", 95% CI ["
            << estimate.confidence_lower_95 << ", "
            << estimate.confidence_upper_95 << "] ["
            << (comparison.confidence_gate_passed()
                    ? "PASS"
                    : "FAIL")
            << "]\n"
            << "      exact selected keys: ";
        append_keys(comparison.selected_keys);
        output
            << "\n      ordered K="
            << comparison.ordered_blocks.worlds_per_block
            << " blocks: "
            << comparison.ordered_blocks.correct_block_count << '/'
            << comparison.ordered_blocks.block_count
            << " correct; require "
            << comparison.ordered_blocks
                   .required_correct_block_count()
            << " ["
            << (comparison.block_gate_passed() ? "PASS" : "FAIL")
            << "]\n"
            << "      hidden repartition: "
            << (comparison.hidden_repartition_bit_identical
                    ? "bit-identical"
                    : "CHANGED")
            << "; comparison gate "
            << (comparison.gate_passed() ? "PASS" : "FAIL")
            << '\n';
    };

    for (std::size_t index = 0; index < reports.size(); ++index) {
        const TeacherSufficiencyAuditReport& report =
            reports[index];
        output
            << "\n[" << (index == 0 ? "PRIMARY" : "DIAGNOSTIC")
            << "] " << report.policy_name << '\n'
            << "  fingerprint: " << report.model_fingerprint << '\n'
            << "  search: K=" << report.config.worlds << "/H="
            << report.config.horizon_turns << ", "
            << learned_variant_name(
                   report.config.continuation_variant)
            << " mirror, shallow-prior blend "
            << (report.config.blend_shallow_prior ? "on" : "off")
            << ", one rollout/world";
        if (report.config.evaluation_threads != 1) {
            output << ", evaluation threads "
                   << report.config.evaluation_threads;
        }
        output
            << "\n  all hidden repartitions: "
            << (report.hidden_repartition_bit_identical
                    ? "bit-identical"
                    : "CHANGED")
            << '\n';
        append_comparison(report.force_spike_live);
        append_comparison(report.force_spike_payable);
        append_comparison(report.disintegrate_x_zero);
        output << "  row gate: "
               << (report.gate_passed() ? "PASS" : "FAIL")
               << '\n';
    }
    output
        << "\nPrimary teacher gate: "
        << (reports.front().gate_passed() ? "PASS" : "FAIL")
        << '\n'
        << (reports.front().gate_passed()
                ? "Decision: the measured search teacher is "
                  "sufficient for the preregistered pure-distillation "
                  "experiment.\n"
                : "Decision: do not pure-distill this teacher; add an "
                  "orthogonal self-generated improvement signal.\n");
    return output.str();
}

std::string format_terminal_credit_audit_report(
    const TeacherSufficiencyAuditReport& report) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::fixed << std::setprecision(6)
           << "Full-Terminal Credit Audit\n"
           << "Evaluation-only; every candidate uses paired common "
              "worlds and a conservatively terminal horizon.\n"
           << "The primary verdict uses only the Force Spike live "
              "and payable ordering controls. The X=0 row is "
              "diagnostic only.\n\n"
           << report.policy_name << '\n'
           << "  fingerprint: " << report.model_fingerprint << '\n'
           << "  search: K=" << report.config.worlds << "/H="
           << report.config.horizon_turns << ", "
           << learned_variant_name(
                  report.config.continuation_variant)
           << " mirror, shallow-prior blend "
           << (report.config.blend_shallow_prior ? "on" : "off")
           << ", one rollout/world, terminal results "
           << (report.config.require_terminal_results
                   ? "required"
                   : "NOT REQUIRED");
    if (report.config.evaluation_threads != 1) {
        output << ", evaluation threads "
               << report.config.evaluation_threads;
    }
    output << '\n';

    const auto append_keys =
        [&output](const std::vector<std::string>& keys) {
            output << '{';
            for (std::size_t index = 0; index < keys.size();
                 ++index) {
                if (index != 0) {
                    output << ", ";
                }
                output << keys[index];
            }
            output << '}';
        };
    const auto append_comparison =
        [&output, &append_keys, &report](
            std::string_view role,
            const TeacherOptionComparison& comparison,
            bool primary) {
            const CandidatePairEstimate& estimate =
                comparison.estimate;
            output
                << "\n  [" << role << "] "
                << comparison.description << '\n'
                << "    oriented delta: Q(" << estimate.first_key
                << ") - Q(" << estimate.second_key << ") = "
                << estimate.delta_q << ", paired SE "
                << estimate.paired_standard_error << ", 95% CI ["
                << estimate.confidence_lower_95 << ", "
                << estimate.confidence_upper_95 << "] ["
                << (comparison.confidence_gate_passed()
                        ? "PASS"
                        : "FAIL")
                << "]\n"
                << "    exact selected-best set: ";
            append_keys(comparison.selected_keys);
            output
                << "\n    ordered K="
                << comparison.ordered_blocks.worlds_per_block
                << " blocks: "
                << comparison.ordered_blocks.correct_block_count
                << '/' << comparison.ordered_blocks.block_count
                << " correct; require "
                << comparison.ordered_blocks
                       .required_correct_block_count()
                << " ["
                << (comparison.block_gate_passed()
                        ? "PASS"
                        : "FAIL")
                << "]\n"
                << "    hidden repartition: "
                << (comparison.hidden_repartition_bit_identical
                        ? "bit-identical"
                        : "CHANGED")
                << '\n'
                << "    conservative terminal bound: H="
                << report.config.horizon_turns << ", require >= "
                << comparison.conservative_terminal_bound_turns
                << " (both libraries + 1) ["
                << (comparison
                            .conservative_terminal_bound_satisfied
                        ? "PASS"
                        : "FAIL")
                << "]\n"
                << "    evaluation accounting: candidates "
                << comparison.candidate_count << ", expected "
                << comparison.expected_evaluations
                << ", recorded candidate samples "
                << comparison.recorded_candidate_samples
                << ", rollouts "
                << comparison.rollout_evaluations
                << ", terminal "
                << comparison.terminal_evaluations
                << ", bootstrapped "
                << comparison.bootstrapped_evaluations << " ["
                << (comparison.evaluation_accounting_is_exact()
                        ? "EXACT"
                        : "MISMATCH")
                << "]\n"
                << "    all evaluations terminal: "
                << (comparison.terminal_results_gate_passed()
                        ? "PASS"
                        : "FAIL")
                << '\n';
            if (primary) {
                output
                    << "    primary row gate: "
                    << (comparison
                                    .hidden_repartition_bit_identical &&
                                comparison.confidence_gate_passed() &&
                                comparison.block_gate_passed() &&
                                comparison
                                    .terminal_results_gate_passed()
                            ? "PASS"
                            : "FAIL")
                    << '\n';
            }
        };

    append_comparison(
        "PRIMARY 1/2", report.force_spike_live, true);
    append_comparison(
        "PRIMARY 2/2", report.force_spike_payable, true);
    append_comparison(
        "DIAGNOSTIC ONLY", report.disintegrate_x_zero, false);
    output
        << "    X=0 excluded from exact selected-best set: "
        << (report.disintegrate_x_zero
                    .second_key_excluded_from_selected_set()
                ? "PASS"
                : "FAIL")
        << '\n'
        << "    This X=0 selection and its sign/integrity do not "
           "enter the primary Force Spike gate.\n"
        << "\nPrimary terminal-credit gate: "
        << (terminal_credit_primary_gate_passed(report)
                ? "PASS"
                : "FAIL")
        << '\n'
        << (terminal_credit_primary_gate_passed(report)
                ? "Decision: full-terminal mirror outcomes resolve "
                  "both preregistered Force Spike orderings.\n"
                : "Decision: full-terminal mirror outcomes do not "
                  "yet resolve both preregistered Force Spike "
                  "orderings.\n");
    return output.str();
}

ValueProbeDecisionDetail make_value_probe_decision_detail(
    const probe_eval::ProbeLabel& label,
    const probe_eval::ProbePrediction& prediction,
    const ValueProbeDecisionDetail* reference,
    const ValueProbeDecisionDetail* previous) {
    return build_value_probe_decision_detail(
        label, prediction, reference, previous);
}

bool value_decision_uniquely_selects(
    const ValueProbeDecisionDetail& decision,
    std::string_view candidate_key) {
    return decision.selected_keys.size() == 1 &&
           decision.selected_keys.front() == candidate_key;
}

CandidatePairEstimate make_candidate_pair_estimate(
    const probe_eval::ProbeLabel& label, std::string name,
    std::string_view first_key, std::string_view second_key) {
    probe_eval::validate_probe_label(label);
    return oriented_pair_estimate(
        label, std::move(name), first_key, second_key);
}

std::filesystem::path default_probe_cache_path(
    ProbeCorpusKind corpus_kind) {
    return std::filesystem::path(
        corpus_definition(corpus_kind).default_cache_path);
}

std::uint64_t reference_seed_for_probe(
    std::string_view corpus_id, std::string_view stable_id,
    std::uint64_t reference_seed) {
    Fnv1a hash;
    hash.text(kProbeEnvironmentRevision);
    hash.text(corpus_id);
    hash.text(stable_id);
    hash.unsigned_integer(reference_seed);
    return hash.value();
}

std::string corpus_information_set_fingerprint(
    const std::vector<probes::DecisionProbe>& corpus) {
    return corpus_information_set_fingerprint(
        ProbeCorpusKind::DevV3, corpus);
}

std::string corpus_information_set_fingerprint(
    ProbeCorpusKind corpus_kind,
    const std::vector<probes::DecisionProbe>& corpus) {
    Fnv1a hash;
    hash.text(kProbeEnvironmentRevision);
    hash.text(corpus_definition(corpus_kind).corpus_id);
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
    return make_probe_cache_metadata(
        ProbeCorpusKind::DevV3, config, corpus,
        reference_model_fingerprint);
}

ProbeCacheMetadata make_probe_cache_metadata(
    ProbeCorpusKind corpus_kind, const ProbeScoreConfig& config,
    const std::vector<probes::DecisionProbe>& corpus,
    std::string_view reference_model_fingerprint) {
    validate_corpus_score_config(corpus_kind, config);
    validate_text_field(reference_model_fingerprint,
                        "reference model fingerprint");
    const auto validation_errors =
        validate_corpus(corpus_kind, corpus);
    if (!validation_errors.empty()) {
        throw std::invalid_argument(
            "cannot label an invalid probe corpus: " +
            validation_errors.front());
    }
    const ProbeCorpusDefinition definition =
        corpus_definition(corpus_kind);
    return {
        .schema = std::string(definition.cache_schema),
        .algorithm = std::string(kProbeReferenceAlgorithm),
        .semantic_revision =
            std::string(definition.semantic_revision),
        .environment_revision =
            std::string(kProbeEnvironmentRevision),
        .corpus_id = std::string(definition.corpus_id),
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
            corpus_information_set_fingerprint(
                corpus_kind, corpus),
    };
}

void write_probe_label_cache_atomic(
    const std::filesystem::path& path,
    const ProbeCacheMetadata& metadata,
    const std::vector<probes::DecisionProbe>& corpus,
    const std::vector<ProbeReferenceSamples>& samples) {
    write_probe_label_cache_atomic(
        ProbeCorpusKind::DevV3, path, metadata, corpus, samples);
}

void write_probe_label_cache_atomic(
    ProbeCorpusKind corpus_kind, const std::filesystem::path& path,
    const ProbeCacheMetadata& metadata,
    const std::vector<probes::DecisionProbe>& corpus,
    const std::vector<ProbeReferenceSamples>& samples) {
    if (path.empty()) {
        throw std::invalid_argument(
            "probe cache path must not be empty");
    }
    const ProbeCorpusDefinition definition =
        corpus_definition(corpus_kind);
    const auto validation_errors =
        validate_corpus(corpus_kind, corpus);
    if (!validation_errors.empty()) {
        throw std::invalid_argument(
            "cannot write an invalid probe corpus: " +
            validation_errors.front());
    }
    if (metadata.schema != definition.cache_schema ||
        metadata.algorithm != kProbeReferenceAlgorithm ||
        metadata.semantic_revision !=
            definition.semantic_revision ||
        metadata.environment_revision !=
            kProbeEnvironmentRevision ||
        metadata.corpus_id != definition.corpus_id ||
        metadata.reference_seed != kProbeReferenceSeed ||
        metadata.production_policy_seed !=
            kProbeProductionPolicySeed ||
        metadata.probe_count != corpus.size() ||
        metadata.information_set_fingerprint !=
            corpus_information_set_fingerprint(
                corpus_kind, corpus) ||
        (corpus_kind == ProbeCorpusKind::ValidationV1 &&
         metadata.rollouts_per_world != 1)) {
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
                output, definition.cache_magic, metadata, corpus,
                samples);
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
    return load_probe_label_cache(
        ProbeCorpusKind::DevV3, path, expected_metadata, corpus);
}

std::vector<probe_eval::ProbeLabel> load_probe_label_cache(
    ProbeCorpusKind corpus_kind, const std::filesystem::path& path,
    const ProbeCacheMetadata& expected_metadata,
    const std::vector<probes::DecisionProbe>& corpus) {
    try {
        const ProbeCorpusDefinition definition =
            corpus_definition(corpus_kind);
        const auto validation_errors =
            validate_corpus(corpus_kind, corpus);
        if (!validation_errors.empty()) {
            throw std::invalid_argument(
                "expected corpus is invalid: " +
                validation_errors.front());
        }
        std::string expected_mismatch;
        if (expected_metadata.schema !=
            definition.cache_schema) {
            expected_mismatch = "schema";
        } else if (expected_metadata.algorithm !=
                   kProbeReferenceAlgorithm) {
            expected_mismatch = "algorithm";
        } else if (expected_metadata.semantic_revision !=
                   definition.semantic_revision) {
            expected_mismatch = "semantic_revision";
        } else if (expected_metadata.environment_revision !=
                   kProbeEnvironmentRevision) {
            expected_mismatch = "environment_revision";
        } else if (expected_metadata.corpus_id !=
                   definition.corpus_id) {
            expected_mismatch = "corpus";
        } else if (expected_metadata.reference_seed !=
                   kProbeReferenceSeed) {
            expected_mismatch = "reference_seed";
        } else if (expected_metadata.production_policy_seed !=
                   kProbeProductionPolicySeed) {
            expected_mismatch = "production_policy_seed";
        } else if (
            corpus_kind == ProbeCorpusKind::ValidationV1 &&
            expected_metadata.rollouts_per_world != 1) {
            expected_mismatch = "rollouts";
        } else if (expected_metadata.probe_count !=
                   corpus.size()) {
            expected_mismatch = "probe_count";
        } else if (
            expected_metadata.information_set_fingerprint !=
            corpus_information_set_fingerprint(
                corpus_kind, corpus)) {
            expected_mismatch = "fingerprint";
        }
        if (!expected_mismatch.empty()) {
            throw std::invalid_argument(
                "expected metadata does not match the selected "
                "probe corpus: " + expected_mismatch);
        }
        std::ifstream input(path);
        if (!input) {
            throw std::invalid_argument(
                "cache file is missing or unreadable");
        }
        input.imbue(std::locale::classic());
        const ProbeCacheMetadata actual =
            read_metadata(input, definition.cache_magic);
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
    return generate_probe_reference_samples(
        ProbeCorpusKind::DevV3, probe, std::move(actor_model),
        config);
}

ProbeReferenceSamples generate_probe_reference_samples(
    ProbeCorpusKind corpus_kind,
    const probes::DecisionProbe& probe,
    std::shared_ptr<const LearnedModel> actor_model,
    const ProbeScoreConfig& config) {
    validate_corpus_score_config(corpus_kind, config);
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
        corpus_definition(corpus_kind).corpus_id,
        LearnedVariant::UnifiedActor, true);
}

HiddenRepartitionSummary verify_value_hidden_repartition(
    ProbeCorpusKind corpus_kind,
    const std::vector<NamedValueScoringModel>& models,
    std::size_t scoring_value_worlds,
    double value_continuation_epsilon) {
    if (models.empty() || scoring_value_worlds == 0 ||
        scoring_value_worlds > kMaximumReferenceWorlds ||
        !std::isfinite(value_continuation_epsilon) ||
        value_continuation_epsilon < 0.0 ||
        value_continuation_epsilon > 1.0) {
        throw std::invalid_argument(
            "hidden-repartition audit configuration is invalid");
    }
    for (std::size_t index = 0; index < models.size(); ++index) {
        const auto& model = models[index];
        if (!model.model || model.name.empty() ||
            !std::isfinite(
                model.value_priority_residual_weight) ||
            model.value_priority_residual_weight < 0.0 ||
            model.value_priority_residual_weight > 1.0) {
            throw std::invalid_argument(
                "hidden-repartition audit model is invalid");
        }
        validate_text_field(
            model.name, "hidden-repartition model name");
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (models[prior].name == model.name) {
                throw std::invalid_argument(
                    "hidden-repartition model names must be unique");
            }
        }
    }

    const auto definition = corpus_definition(corpus_kind);
    const auto corpus = make_corpus(corpus_kind);
    const auto hidden_clones = hidden_clone_corpus(corpus);
    if (corpus_information_set_fingerprint(
            corpus_kind, corpus) !=
        corpus_information_set_fingerprint(
            corpus_kind, hidden_clones)) {
        throw std::runtime_error(
            "hidden clone changed corpus information-set fingerprint");
    }
    for (const auto& candidate : models) {
        const auto original = score_value_deployed(
            corpus, candidate.model, definition.corpus_id,
            scoring_value_worlds, value_continuation_epsilon,
            candidate.value_priority_residual_weight);
        const auto clone = score_value_deployed(
            hidden_clones, candidate.model,
            definition.corpus_id, scoring_value_worlds,
            value_continuation_epsilon,
            candidate.value_priority_residual_weight);
        require_predictions_bit_identical(
            original, clone, candidate.name);
    }
    return {
        .passed = true,
        .policy_count = models.size(),
        .probe_count = corpus.size(),
    };
}

ProbeScoreReport score_probe_corpus_with_candidates(
    ProbeCorpusKind corpus_kind,
    const ProbeScoreConfig& requested_config,
    std::ostream& progress, ProbeScoringModels models) {
    ProbeScoreConfig config = requested_config;
    if (corpus_kind == ProbeCorpusKind::ValidationV1 &&
        config.cache_path ==
            default_probe_cache_path(ProbeCorpusKind::DevV3)) {
        throw std::invalid_argument(
            "probe-validation-v1 cannot use the dev-v3 default "
            "cache path; use " +
            default_probe_cache_path(corpus_kind).string());
    }
    validate_corpus_score_config(corpus_kind, config);
    if (!models.reference_actor_model ||
        !models.scoring_actor_model ||
        !models.reference_value_model) {
        throw std::invalid_argument(
            "probe scoring requires frozen reference and scoring "
            "Actor models and a reference Value model");
    }
    if (models.scoring_actor_name.empty() ||
        models.reference_value_name.empty()) {
        throw std::invalid_argument(
            "probe scoring model names must not be empty");
    }
    validate_text_field(models.scoring_actor_name,
                        "probe scoring Actor name");
    validate_text_field(models.reference_value_name,
                        "probe reference Value name");
    for (std::size_t candidate = 0;
         candidate < models.scoring_value_models.size();
         ++candidate) {
        const NamedValueScoringModel& scoring =
            models.scoring_value_models[candidate];
        if (!scoring.model) {
            throw std::invalid_argument(
                "probe scoring requires every Value checkpoint "
                "model to be frozen and non-null");
        }
        validate_text_field(scoring.name,
                            "probe scoring Value name");
        if (!scoring.transition_family.empty()) {
            validate_text_field(
                scoring.transition_family,
                "probe scoring Value transition family");
        }
        if (!std::isfinite(
                scoring.value_priority_residual_weight) ||
            scoring.value_priority_residual_weight < 0.0 ||
            scoring.value_priority_residual_weight > 1.0) {
            throw std::invalid_argument(
                "probe scoring Value Priority residual weight must "
                "be finite and in [0, 1]");
        }
        for (std::size_t prior = 0; prior < candidate; ++prior) {
            if (models.scoring_value_models[prior].name ==
                scoring.name) {
                throw std::invalid_argument(
                    "probe scoring Value names must be unique");
            }
        }
        if (models.scoring_value_models.size() > 1 &&
            scoring.name == models.reference_value_name) {
            throw std::invalid_argument(
                "multi-checkpoint Value names must differ from "
                "the reference Value name");
        }
    }
    const ProbeCorpusDefinition definition =
        corpus_definition(corpus_kind);
    const std::vector<probes::DecisionProbe> corpus =
        make_corpus(corpus_kind);
    const std::string reference_actor_fingerprint =
        learned_model_fingerprint(models.reference_actor_model);
    const std::string scoring_actor_fingerprint =
        learned_model_fingerprint(models.scoring_actor_model);
    const std::string reference_value_fingerprint =
        learned_model_fingerprint(models.reference_value_model);
    std::vector<std::string> scoring_value_fingerprints;
    scoring_value_fingerprints.reserve(
        models.scoring_value_models.size());
    for (const NamedValueScoringModel& candidate :
         models.scoring_value_models) {
        scoring_value_fingerprints.push_back(
            learned_model_fingerprint(candidate.model));
    }
    const ProbeCacheMetadata metadata =
        make_probe_cache_metadata(
            corpus_kind, config, corpus,
            reference_actor_fingerprint);
    std::vector<probe_eval::ProbeLabel> labels;
    ProbeCacheStatus cache_status = ProbeCacheStatus::Loaded;

    const bool cache_exists =
        std::filesystem::exists(config.cache_path);
    if (cache_exists && !config.refresh_cache) {
        progress << "Loading probe labels from "
                 << config.cache_path.string() << "...\n"
                 << std::flush;
        labels = load_probe_label_cache(
            corpus_kind, config.cache_path, metadata, corpus);
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
                corpus_kind, corpus[probe],
                models.reference_actor_model, config));
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
            corpus_kind, config.cache_path, metadata, corpus, raw);
        progress << "Published deterministic probe cache to "
                 << config.cache_path.string() << '\n';
    }

    progress << "Cross-checking the Actor reference with "
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
                probe, models.reference_value_model, config,
                definition.corpus_id,
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
            probe_critic_value(
                probe, models.reference_value_model)));
        value_reference_labels.push_back(label);
        progress << " done\n";
    }
    const ReferenceSensitivitySummary reference_sensitivity =
        compare_continuation_labels(labels,
                                    value_reference_labels);

    const bool multi_checkpoint_attribution =
        models.scoring_value_models.size() > 1;
    const bool has_distinct_single_value_candidate =
        models.scoring_value_models.size() == 1 &&
        (scoring_value_fingerprints.front() !=
             reference_value_fingerprint ||
         models.scoring_value_models.front()
                 .value_priority_residual_weight != 0.0);
    const std::size_t policy_count =
        5 + (multi_checkpoint_attribution
                 ? models.scoring_value_models.size()
                 : (has_distinct_single_value_candidate ? 1U : 0U));
    progress << "Scoring " << policy_count
             << " policy views and their hidden-zone "
                "repartition clones on "
             << corpus.size() << " diagnostic positions..."
             << std::flush;

    const std::vector<probes::DecisionProbe> hidden_clones =
        hidden_clone_corpus(corpus);
    if (corpus_information_set_fingerprint(
            corpus_kind, hidden_clones) !=
        metadata.information_set_fingerprint) {
        throw std::runtime_error(
            "hidden clone changed corpus information-set fingerprint");
    }

    const auto actor_raw =
        score_actor_raw(corpus, models.scoring_actor_model);
    const auto actor_raw_clone =
        score_actor_raw(hidden_clones, models.scoring_actor_model);
    const auto actor_deployed =
        score_actor_deployed(
            corpus, models.scoring_actor_model,
            definition.corpus_id);
    const auto actor_deployed_clone =
        score_actor_deployed(
            hidden_clones, models.scoring_actor_model,
            definition.corpus_id);
    const auto reference_value_deployed =
        score_value_deployed(
            corpus, models.reference_value_model,
            definition.corpus_id, config.scoring_value_worlds,
            config.scoring_value_continuation_epsilon, 0.0);
    const auto reference_value_deployed_clone =
        score_value_deployed(
            hidden_clones, models.reference_value_model,
            definition.corpus_id, config.scoring_value_worlds,
            config.scoring_value_continuation_epsilon, 0.0);
    std::vector<std::vector<probe_eval::ProbePrediction>>
        scoring_value_deployed;
    std::vector<std::vector<probe_eval::ProbePrediction>>
        scoring_value_deployed_clones;
    scoring_value_deployed.reserve(
        models.scoring_value_models.size());
    scoring_value_deployed_clones.reserve(
        models.scoring_value_models.size());
    for (const NamedValueScoringModel& candidate :
         models.scoring_value_models) {
        scoring_value_deployed.push_back(
            score_value_deployed(
                corpus, candidate.model, definition.corpus_id,
                config.scoring_value_worlds,
                config.scoring_value_continuation_epsilon,
                candidate.value_priority_residual_weight));
        scoring_value_deployed_clones.push_back(
            score_value_deployed(
                hidden_clones, candidate.model,
                definition.corpus_id,
                config.scoring_value_worlds,
                config.scoring_value_continuation_epsilon,
                candidate.value_priority_residual_weight));
    }
    const auto handcrafted = score_handcrafted(corpus);
    const auto handcrafted_clone =
        score_handcrafted(hidden_clones);
    const auto value_reference_clone = score_learned_search(
        hidden_clones, models.reference_value_model,
        definition.corpus_id,
        LearnedVariant::ValueSearchChampion, config.reference_worlds,
        config.reference_rollouts_per_world,
        config.reference_horizon_turns, false);
    require_critics_bit_identical(
        actor_raw, actor_deployed, "Actor");
    require_critics_bit_identical(
        reference_value_deployed, value_reference_predictions,
        "Value");
    progress << " done\n";

    ProbeScoreReport report;
    report.corpus_kind = corpus_kind;
    report.promotion_eligible = false;
    report.metadata = metadata;
    report.cache_status = cache_status;
    report.cache_path = config.cache_path;
    report.reference_samples_per_candidate =
        reference_sample_count(metadata);
    report.scoring_actor_model_fingerprint =
        scoring_actor_fingerprint;
    report.value_model_fingerprint =
        reference_value_fingerprint;
    if (models.scoring_value_models.size() == 1) {
        report.scoring_value_model_fingerprint =
            scoring_value_fingerprints.front();
    }
    report.reference_sensitivity = reference_sensitivity;
    report.low_margin =
        summarize_low_margin_best_pairs(labels);
    report.candidate_pairs =
        focused_candidate_pairs(
            corpus_kind, corpus, labels,
            report.reference_samples_per_candidate);
    {
        auto value_pairs = score_value_candidate_pair(
            corpus_kind, corpus, models.reference_value_model,
            models.reference_value_name +
                " Q(Pass) - Q(X=0)",
            definition.corpus_id, config, 0.0);
        for (CandidatePairEstimate& pair : value_pairs) {
            report.value_candidate_pairs.push_back(
                std::move(pair));
        }
    }
    for (const NamedValueScoringModel& candidate :
         models.scoring_value_models) {
        auto value_pairs = score_value_candidate_pair(
            corpus_kind, corpus, candidate.model,
            candidate.name + " Q(Pass) - Q(X=0)",
            definition.corpus_id, config,
            candidate.value_priority_residual_weight);
        for (CandidatePairEstimate& pair : value_pairs) {
            report.value_candidate_pairs.push_back(
                std::move(pair));
        }
    }
    if (corpus_kind == ProbeCorpusKind::DevV3) {
        progress
            << "Scoring supplemental deployed Force Spike "
               "live/payable controls..."
            << std::flush;
        report.force_spike_controls.push_back(
            score_value_force_spike_policy_controls(
                models.reference_value_model,
                models.reference_value_name,
                config.scoring_value_worlds,
                config.scoring_value_continuation_epsilon, 0.0));
        for (const NamedValueScoringModel& candidate :
             models.scoring_value_models) {
            report.force_spike_controls.push_back(
                score_value_force_spike_policy_controls(
                    candidate.model, candidate.name,
                    config.scoring_value_worlds,
                    config.scoring_value_continuation_epsilon,
                    candidate.value_priority_residual_weight));
        }
        progress << " done\n";
    }
    report.hidden_repartition = {
        .passed = true,
        .policy_count = policy_count,
        .probe_count = corpus.size(),
    };
    const std::string raw_name =
        models.scoring_actor_name + " raw head";
    const std::string deployed_name =
        models.scoring_actor_name + " deployed policy";
    const std::string reference_value_deployed_name =
        models.reference_value_name + " deployed policy";
    const auto value_deployed_configuration =
        [&config](double value_priority_residual_weight) {
            std::ostringstream value_configuration;
            value_configuration.imbue(std::locale::classic());
            value_configuration
                << "Priority: K="
                << config.scoring_value_worlds
                << "/H=4 Value mirror with deployed aggregate "
                   "shallow-prior blend";
            if (config.scoring_value_continuation_epsilon != 0.0) {
                value_configuration
                    << ", continuation epsilon="
                    << config.scoring_value_continuation_epsilon;
            }
            if (value_priority_residual_weight != 0.0) {
                value_configuration
                    << ", Value Priority residual weight="
                    << value_priority_residual_weight;
            }
            value_configuration
                << "; Attack: deployed public-board attack-set "
                   "scorer";
            return value_configuration.str();
        };
    report.policies.reserve(policy_count);
    report.policies.push_back(evaluate_hidden_invariant_policy(
        raw_name,
        "Priority and Attack: raw masked policy logits",
        labels, actor_raw, actor_raw_clone, true));
    report.policies.push_back(evaluate_hidden_invariant_policy(
        deployed_name,
        "Priority: K=2/H=0 information-set search with no "
        "shallow blend; Attack: raw masked policy head",
        labels, actor_deployed, actor_deployed_clone, true));
    report.policies.push_back(evaluate_hidden_invariant_policy(
        reference_value_deployed_name,
        value_deployed_configuration(0.0),
        labels, reference_value_deployed,
        reference_value_deployed_clone, true));
    if (has_distinct_single_value_candidate) {
        report.policies.push_back(evaluate_hidden_invariant_policy(
            models.scoring_value_models.front().name +
                " deployed policy",
            value_deployed_configuration(
                models.scoring_value_models.front()
                    .value_priority_residual_weight),
            labels, scoring_value_deployed.front(),
            scoring_value_deployed_clones.front(), true));
    }
    report.policies.push_back(evaluate_hidden_invariant_policy(
        "Handcrafted agreement",
        "Priority and Attack: diagnostic agreement with the "
        "Actor-derived reference only; never labels/training",
        labels, handcrafted, handcrafted_clone, false));
    report.policies.push_back(evaluate_hidden_invariant_policy(
        models.reference_value_name +
            "-continuation deep cross-check",
        "Priority and Attack: diagnostic K/H/rollouts match "
        "the Actor reference; no shallow-prior blend and never "
        "overwrites labels",
        labels, value_reference_predictions,
        value_reference_clone, true));

    if (multi_checkpoint_attribution) {
        report.value_checkpoints.reserve(
            models.scoring_value_models.size() + 1);
        report.value_checkpoints.push_back(
            make_value_checkpoint_report(
                models.reference_value_name,
                reference_value_fingerprint, 0.0, labels,
                reference_value_deployed,
                reference_value_deployed_clone, nullptr, nullptr));
        std::unordered_map<std::string, std::size_t>
            last_checkpoint_by_family;
        for (std::size_t candidate = 0;
             candidate < models.scoring_value_models.size();
             ++candidate) {
            const ValueCheckpointProbeReport* reference =
                &report.value_checkpoints.front();
            const NamedValueScoringModel& scoring =
                models.scoring_value_models[candidate];
            const ValueCheckpointProbeReport* previous = reference;
            if (!scoring.transition_family.empty()) {
                const auto found = last_checkpoint_by_family.find(
                    scoring.transition_family);
                if (found !=
                    last_checkpoint_by_family.end()) {
                    previous =
                        &report.value_checkpoints[found->second];
                }
            }
            report.value_checkpoints.push_back(
                make_value_checkpoint_report(
                    scoring.name,
                    scoring_value_fingerprints[candidate],
                    scoring.value_priority_residual_weight, labels,
                    scoring_value_deployed[candidate],
                    scoring_value_deployed_clones[candidate],
                    reference, previous));
            if (!scoring.transition_family.empty()) {
                last_checkpoint_by_family[
                    scoring.transition_family] =
                    report.value_checkpoints.size() - 1;
            }
        }
    }
    return report;
}

ProbeScoreReport score_probe_dev_with_candidates(
    const ProbeScoreConfig& config, std::ostream& progress,
    ProbeScoringModels models) {
    return score_probe_corpus_with_candidates(
        ProbeCorpusKind::DevV3, config, progress,
        std::move(models));
}

ProbeScoreReport score_probe_dev_with_models(
    const ProbeScoreConfig& config, std::ostream& progress,
    std::shared_ptr<const LearnedModel> reference_actor_model,
    std::shared_ptr<const LearnedModel> scoring_actor_model,
    std::string scoring_actor_name) {
    validate_score_config(config);
    progress << "Training frozen Value scoring model (seed "
             << config.training_seed << ", "
             << config.training_games << " games)..."
             << std::flush;
    auto value_model = train_learned_value_champion(
        config.training_games, config.training_seed);
    progress << " done\n";
    return score_probe_dev_with_candidates(
        config, progress,
        {
            .reference_actor_model =
                std::move(reference_actor_model),
            .scoring_actor_model =
                std::move(scoring_actor_model),
            .scoring_actor_name =
                std::move(scoring_actor_name),
            .reference_value_model = value_model,
            .reference_value_name = "Value",
            .scoring_value_models = {},
        });
}

ProbeScoreReport score_probe_dev(
    const ProbeScoreConfig& config, std::ostream& progress) {
    validate_score_config(config);
    progress << "Training frozen Actor reference/scoring model (seed "
             << config.training_seed << ", "
             << config.training_games << " games)..."
             << std::flush;
    auto actor_model = train_learned_actor_model(
        config.training_games, config.training_seed);
    progress << " done\n";
    return score_probe_dev_with_models(
        config, progress, actor_model, actor_model,
        "Actor");
}

std::string format_probe_score_report(
    const ProbeScoreReport& report) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::fixed << std::setprecision(4);
    if (report.corpus_kind == ProbeCorpusKind::ValidationV1) {
        output
            << "\nProbe Validation-v1 Offline Score\n"
            << "WARNING: focused harvested behavioral regression "
               "only; not deck-balanced and cannot be used for "
               "policy promotion or a Learned-is-king claim.\n";
    } else {
        output
            << "\nProbe Dev-v3 Offline Score\n"
            << "WARNING: diagnostic only, 4 positions each for "
               "Green/Red/Blue/White/RU Aggro. "
               "This cannot establish playing strength or a champion.\n";
    }
    output << "Reference: Actor-mirror common worlds, K="
           << report.metadata.worlds << ", H="
           << report.metadata.horizon_turns << ", rollouts/world="
           << report.metadata.rollouts_per_world
           << ", no shallow-prior blend\n"
           << "Reference algorithm/revision: "
           << report.metadata.algorithm << " / "
           << report.metadata.semantic_revision << '\n'
           << "Rules environment: "
           << report.metadata.environment_revision << '\n'
           << "Fixed reference seed: 0x"
           << hex_u64(report.metadata.reference_seed)
           << "\nProduction-policy RNG domain seed: 0x"
           << hex_u64(report.metadata.production_policy_seed)
           << '\n'
           << "Reference samples/candidate: "
           << report.reference_samples_per_candidate << '\n'
           << "Reference Actor training recipe: seed "
           << report.metadata.training_seed << ", "
           << report.metadata.training_games << " games\n"
           << "Reference Actor model fingerprint: "
           << report.metadata.reference_model_fingerprint << '\n'
           << "Scoring Actor model fingerprint: "
           << report.scoring_actor_model_fingerprint << '\n';
    if (report.scoring_value_model_fingerprint.empty() ||
        report.scoring_value_model_fingerprint ==
            report.value_model_fingerprint) {
        output << "Diagnostic Value model fingerprint: "
               << report.value_model_fingerprint << '\n';
    } else {
        output << "Reference Value model fingerprint: "
               << report.value_model_fingerprint << '\n'
               << "Scoring Value model fingerprint: "
               << report.scoring_value_model_fingerprint << '\n';
    }
    output
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
    if (!report.candidate_pairs.empty()) {
        output
            << "\nFocused cached Actor-reference candidate pairs\n"
            << "  These are immutable teacher-label diagnostics, "
               "not measurements of a Value scoring policy.\n";
        for (const CandidatePairEstimate& pair :
             report.candidate_pairs) {
            output << "  [BEHAVIORAL-REGRESSION-ONLY] "
                   << pair.stable_id << " ("
                   << deck_name(pair.root_deck) << "): "
                   << pair.name << " = " << pair.delta_q
                   << ", paired SE "
                   << pair.paired_standard_error << ", 95% CI ["
                   << pair.confidence_lower_95 << ", "
                   << pair.confidence_upper_95 << "], "
                   << pair.samples_per_candidate
                   << " common-world samples/candidate\n"
                   << "    keys: " << pair.first_key << " minus "
                   << pair.second_key << '\n';
        }
    }
    if (!report.value_candidate_pairs.empty()) {
        output
            << "\nFocused Value-policy candidate pairs\n"
            << "  Each row is independently estimated from that "
               "policy's own Value-mirror K/H=4 search; changing K "
               "does not change the cached Actor reference.\n";
        for (const CandidatePairEstimate& pair :
             report.value_candidate_pairs) {
            output << "  [BEHAVIORAL-REGRESSION-ONLY] "
                   << pair.stable_id << " ("
                   << deck_name(pair.root_deck) << "): "
                   << pair.name << " = " << pair.delta_q
                   << ", paired SE "
                   << pair.paired_standard_error << ", 95% CI ["
                   << pair.confidence_lower_95 << ", "
                   << pair.confidence_upper_95 << "], K="
                   << pair.samples_per_candidate << '\n'
                   << "    keys: " << pair.first_key << " minus "
                   << pair.second_key << '\n';
        }
    }
    if (!report.force_spike_controls.empty()) {
        const auto append_control_key_set =
            [&output](const std::vector<std::string>& keys) {
                output << '{';
                for (std::size_t index = 0;
                     index < keys.size(); ++index) {
                    if (index != 0) {
                        output << ", ";
                    }
                    output << keys[index];
                }
                output << '}';
            };
        output
            << "\nSupplemental Force Spike deployed controls\n"
            << "  Reject-only diagnostic; excluded from balanced "
               "metrics, cache identity, and promotion claims.\n"
            << "  Gate requires the unique deployed exact maximum "
               "to be Force Spike when the tax is unpayable and "
               "Pass when it is payable.\n";
        for (const ForceSpikePolicyControlReport& control :
             report.force_spike_controls) {
            output << "  " << control.policy_name
                   << ": fingerprint "
                   << control.model_fingerprint << ", K="
                   << control.worlds << "/H="
                   << control.horizon_turns;
            if (control.value_priority_residual_weight != 0.0) {
                output << ", Value Priority residual weight="
                       << control.value_priority_residual_weight;
            }
            output << ", hidden repartition "
                   << (control.hidden_repartition_passed
                           ? "PASS"
                           : "FAIL")
                   << '\n'
                   << "    live: Pass="
                   << control.live.pass_score
                   << ", Force Spike="
                   << control.live.force_spike_score
                   << ", selected ";
            append_control_key_set(
                control.live.selected_keys);
            output
                << " ["
                << (control.live_selects_force_spike()
                        ? "PASS"
                        : "FAIL")
                << "]\n"
                << "    payable: Pass="
                << control.payable.pass_score
                << ", Force Spike="
                << control.payable.force_spike_score
                << ", selected ";
            append_control_key_set(
                control.payable.selected_keys);
            output
                << " ["
                << (control.payable_selects_pass()
                        ? "PASS"
                        : "FAIL")
                << "]\n"
                << "    behavioral gate: "
                << (control.gate_passed() ? "PASS" : "FAIL")
                << '\n';
        }
    }
    if (!report.value_checkpoints.empty()) {
        const auto append_key_set =
            [&output](const std::vector<std::string>& keys) {
                output << '{';
                for (std::size_t index = 0;
                     index < keys.size(); ++index) {
                    if (index != 0) {
                        output << ", ";
                    }
                    output << keys[index];
                }
                output << '}';
            };
        output << "\nValue checkpoint transitions (compact)\n"
               << "  Full deployed-policy metrics remain below for "
                  "legacy G0 only; this section attributes immutable "
                  "checkpoint changes.\n";
        for (const ValueCheckpointProbeReport& checkpoint :
             report.value_checkpoints) {
            output << "  " << checkpoint.name << ": fingerprint "
                   << checkpoint.fingerprint;
            if (checkpoint.value_priority_residual_weight != 0.0) {
                output << ", Value Priority residual weight="
                       << checkpoint
                              .value_priority_residual_weight;
            }
            output << ", pooled top1 "
                   << 100.0 *
                          checkpoint.metrics.top1_expected_agreement
                   << "%, pair "
                   << 100.0 *
                          checkpoint.metrics.stable_pair_agreement
                   << "%, regret "
                   << checkpoint.metrics.mean_regret
                   << ", critic Brier "
                   << checkpoint.metrics.critic_brier
                   << ", transition parent "
                   << (checkpoint.transition_parent_name.empty()
                           ? "(baseline)"
                           : checkpoint.transition_parent_name)
                   << ", deck regrets [";
            for (std::size_t deck = 0;
                 deck < checkpoint.metrics.by_deck.size(); ++deck) {
                if (deck != 0) {
                    output << ", ";
                }
                const auto& deck_metrics =
                    checkpoint.metrics.by_deck[deck];
                output << deck_name(deck_metrics.root_deck)
                       << ' ' << deck_metrics.mean_regret;
            }
            output << "]\n";
        }
        output << "  Actionable decision rows (all nonzero regret "
                  "or legacy-G0 selection disagreements)\n";
        for (std::size_t checkpoint_index = 0;
             checkpoint_index < report.value_checkpoints.size();
             ++checkpoint_index) {
            const ValueCheckpointProbeReport& checkpoint =
                report.value_checkpoints[checkpoint_index];
            for (const ValueProbeDecisionDetail& detail :
                 checkpoint.decisions) {
                if (detail.regret == 0.0 &&
                    !detail.selection_changed_from_reference) {
                    continue;
                }
                output << "  [";
                if (detail.regret != 0.0) {
                    output << "NONZERO-REGRET";
                    if (detail.selection_changed_from_reference) {
                        output << "+G0-DISAGREEMENT";
                    }
                } else {
                    output << "G0-DISAGREEMENT";
                }
                output << "] ";
                if (checkpoint_index == 0) {
                    output << checkpoint.name;
                } else {
                    output << checkpoint.transition_parent_name
                           << " -> " << checkpoint.name;
                }
                output << ' ' << detail.stable_id << " ("
                       << deck_name(detail.root_deck)
                       << "): G0 selection "
                       << (detail.selection_changed_from_reference
                               ? "different"
                               : "same")
                       << ", adjacent selection "
                       << (detail.selection_changed_from_previous
                               ? "changed"
                               : "same")
                       << ", selected "
                       << (detail.deterministic_selection
                               ? "key "
                               : "uniform exact-max set ");
                append_key_set(detail.selected_keys);
                output << ", Actor-reference best ";
                append_key_set(detail.reference_best_set);
                output << ", regret " << detail.regret
                       << ", critic " << detail.critic_prediction
                       << ", selected-action reference Q "
                       << detail.selected_action_reference_q
                       << ", critic error "
                       << detail.critic_error << '\n';
            }
        }
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

} // namespace old_school::probe_runner
