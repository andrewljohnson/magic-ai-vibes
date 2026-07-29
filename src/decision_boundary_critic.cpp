#include "old_school/decision_boundary_critic.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/probes.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <numeric>
#include <optional>
#include <ostream>
#include <set>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace old_school::decision_boundary_critic {
namespace {

constexpr std::string_view kSubsetSchema =
    "old-school-aq10-dbc0-owner-safe-census-v1";

std::size_t census_split_index(Split split) {
    return source::split_index(split);
}

std::size_t deck_index(DeckId deck) {
    const std::size_t index =
        static_cast<std::size_t>(deck);
    if (index >= kDeckCount) {
        throw std::out_of_range(
            "AQ10-DBC0 deck index is invalid");
    }
    return index;
}

void append_u64(
    std::string& output, std::uint64_t value) {
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        output.push_back(static_cast<char>(
            (value >> shift) & 0xffU));
    }
}

void append_size(
    std::string& output, std::size_t value) {
    append_u64(output, static_cast<std::uint64_t>(value));
}

void append_string(
    std::string& output, std::string_view value) {
    append_size(output, value.size());
    output.append(value);
}

void append_coordinate(
    std::string& output,
    const source::RootCoordinate& coordinate) {
    append_u64(
        output,
        static_cast<std::uint64_t>(coordinate.split));
    append_size(output, coordinate.schedule_index);
    append_size(output, coordinate.pairing_index);
    append_u64(output, coordinate.game_seed);
    append_size(output, coordinate.starting_player);
    for (const DeckId deck : coordinate.seat_decks) {
        append_u64(
            output, static_cast<std::uint64_t>(deck));
    }
    append_size(output, coordinate.actor);
    append_size(output, coordinate.trace_ordinal);
    append_size(output, coordinate.nontrivial_ordinal);
    append_size(
        output,
        coordinate.actor_game_nontrivial_roots);
    append_size(output, coordinate.retained_position);
    append_size(
        output, coordinate.actor_game_retained_roots);
    append_u64(output, coordinate.search_seed);
}

void append_root(
    std::string& output, const ManifestRoot& root) {
    append_coordinate(output, root.coordinate);
    append_string(output, root.stable_root_id);
    append_string(
        output, root.information_action_fingerprint);
    append_size(output, root.actions.size());
    append_size(output, root.action_descriptors.size());
    for (const std::string& descriptor :
         root.action_descriptors) {
        append_string(output, descriptor);
    }
    append_size(output, root.options.size());
    for (const auto& option : root.options) {
        append_size(output, option.size());
        for (const double feature : option) {
            append_u64(
                output, std::bit_cast<std::uint64_t>(feature));
        }
    }
}

std::vector<ManifestRoot> select_position_zero_impl(
    std::span<const ManifestRoot> roots) {
    std::vector<ManifestRoot> selected;
    selected.reserve(
        2 * kExpectedRootsPerSplit);
    std::copy_if(
        roots.begin(), roots.end(),
        std::back_inserter(selected),
        [](const ManifestRoot& root) {
            return root.coordinate.retained_position == 0;
        });
    return selected;
}

Census assemble_census(
    const source::Census& full) {
    Census census{
        .parent_fingerprint = full.parent_fingerprint,
        .source_manifest_hash = full.manifest_hash,
        .splits = {
            SplitCensus{.split = Split::Train},
            SplitCensus{.split = Split::Dev},
        },
        .roots =
            select_position_zero_impl(full.roots),
    };
    for (std::size_t index = 0;
         index < census.splits.size(); ++index) {
        census.splits[index].games =
            full.splits[index].games;
        census.splits[index].actor_games =
            full.splits[index].actor_games.size();
    }
    for (const ManifestRoot& root : census.roots) {
        const std::size_t split =
            census_split_index(root.coordinate.split);
        SplitCensus& split_row = census.splits[split];
        DeckCensus& deck_row =
            split_row.decks[
                deck_index(root.coordinate.owner_deck())];
        ++split_row.roots;
        split_row.legal_options += root.actions.size();
        ++deck_row.roots;
        deck_row.legal_options += root.actions.size();
    }
    for (SplitCensus& split : census.splits) {
        for (DeckCensus& deck : split.decks) {
            deck.actor_games =
                kExpectedRootsPerDeckAndSplit;
        }
    }
    census.subset_hash =
        canonical_subset_hash(census);
    return census;
}

} // namespace

bool parse_census_command(
    std::span<const std::string_view> arguments) {
    return arguments.size() == 1 &&
           arguments.front() == "--census";
}

void print_usage(std::ostream& output) {
    output
        << "Usage: old-school-decision-boundary-critic "
           "(--census|--run|--cache)\n";
}

std::string canonical_subset_hash(const Census& census) {
    std::string payload;
    append_string(payload, kSubsetSchema);
    append_string(payload, census.parent_fingerprint);
    append_string(payload, census.source_manifest_hash);
    append_size(payload, kPolicyFeatureCount);
    append_size(payload, 0);
    for (const SplitCensus& split : census.splits) {
        append_u64(
            payload,
            static_cast<std::uint64_t>(split.split));
        append_size(payload, split.games);
        append_size(payload, split.actor_games);
        append_size(payload, split.roots);
        append_size(payload, split.legal_options);
        for (const DeckCensus& deck : split.decks) {
            append_size(payload, deck.actor_games);
            append_size(payload, deck.roots);
            append_size(payload, deck.legal_options);
        }
    }
    append_size(payload, census.roots.size());
    for (const ManifestRoot& root : census.roots) {
        append_root(payload, root);
    }
    return artifact_integrity::sha256_string(payload);
}

void validate_census(const Census& census) {
    if (census.parent_fingerprint !=
            kRequiredParentFingerprint ||
        census.source_manifest_hash !=
            kRequiredSourceManifestHash ||
        kRequiredSourceManifestHash.empty() ||
        census.splits[0].split != Split::Train ||
        census.splits[1].split != Split::Dev ||
        census.roots.size() !=
            2 * kExpectedRootsPerSplit ||
        census.subset_hash.size() != 64 ||
        census.subset_hash !=
            canonical_subset_hash(census)) {
        throw std::invalid_argument(
            "AQ10-DBC0 census identity or subset hash is invalid");
    }

    std::set<std::string> stable_root_ids;
    std::size_t root_position = 0;
    for (const Split split :
         std::array<Split, 2>{
             Split::Train, Split::Dev}) {
        const SplitCensus& recorded =
            census.splits[census_split_index(split)];
        if (recorded.games != source::kGamesPerSplit ||
            recorded.actor_games !=
                source::kActorGamesPerSplit ||
            recorded.roots != kExpectedRootsPerSplit) {
            throw std::invalid_argument(
                "AQ10-DBC0 split census shape is invalid");
        }

        std::array<DeckCensus, kDeckCount> observed{};
        std::size_t observed_options = 0;
        for (std::size_t split_position = 0;
             split_position < kExpectedRootsPerSplit;
             ++split_position) {
            if (root_position >= census.roots.size()) {
                throw std::invalid_argument(
                    "AQ10-DBC0 census is truncated");
            }
            const ManifestRoot& root =
                census.roots[root_position++];
            const auto& coordinate = root.coordinate;
            const std::size_t expected_schedule =
                split_position / 2;
            const std::size_t expected_actor =
                split_position % 2;
            if (coordinate.split != split ||
                coordinate.schedule_index !=
                    expected_schedule ||
                coordinate.actor != expected_actor ||
                coordinate.retained_position != 0 ||
                coordinate.actor_game_retained_roots == 0 ||
                coordinate.actor_game_nontrivial_roots == 0 ||
                coordinate.nontrivial_ordinal >=
                    coordinate.actor_game_nontrivial_roots ||
                coordinate.trace_ordinal <
                    coordinate.nontrivial_ordinal ||
                coordinate.starting_player >= 2) {
                throw std::invalid_argument(
                    "AQ10-DBC0 selected root coordinate drifted");
            }
            const DeckId owner =
                coordinate.owner_deck();
            const std::size_t owner_index =
                deck_index(owner);
            if (root.stable_root_id.size() != 64 ||
                root.information_action_fingerprint.size() !=
                    64 ||
                !stable_root_ids
                     .insert(root.stable_root_id)
                     .second ||
                root.actions.size() < 2 ||
                root.action_descriptors.size() !=
                    root.actions.size() ||
                root.options.size() != root.actions.size()) {
                throw std::invalid_argument(
                    "AQ10-DBC0 owner-safe root identity or action shape drifted");
            }

            for (std::size_t action = 0;
                 action < root.actions.size(); ++action) {
                if (root.action_descriptors[action].empty() ||
                    root.action_descriptors[action] !=
                        probes::
                            stable_priority_action_descriptor(
                                root.actions[action]) ||
                    root.options[action].size() !=
                        kPolicyFeatureCount ||
                    !std::all_of(
                        root.options[action].begin(),
                        root.options[action].end(),
                        [](double feature) {
                            return std::isfinite(feature);
                        })) {
                    throw std::invalid_argument(
                        "AQ10-DBC0 action identity or policy feature width drifted");
                }
                for (std::size_t earlier = 0;
                     earlier < action; ++earlier) {
                    if (root.actions[earlier] ==
                            root.actions[action] ||
                        root.action_descriptors[earlier] ==
                            root.action_descriptors[action]) {
                        throw std::invalid_argument(
                            "AQ10-DBC0 root duplicates a legal action");
                    }
                }
            }
            DeckCensus& deck = observed[owner_index];
            ++deck.roots;
            deck.legal_options += root.actions.size();
            observed_options += root.actions.size();
        }

        for (std::size_t deck = 0;
             deck < kDeckCount; ++deck) {
            observed[deck].actor_games =
                kExpectedRootsPerDeckAndSplit;
            if (observed[deck].roots !=
                    kExpectedRootsPerDeckAndSplit ||
                recorded.decks[deck] != observed[deck]) {
                throw std::invalid_argument(
                    "AQ10-DBC0 deck balance or option cross-sum drifted");
            }
        }
        if (recorded.legal_options != observed_options) {
            throw std::invalid_argument(
                "AQ10-DBC0 split option cross-sum drifted");
        }
    }
    if (root_position != census.roots.size()) {
        throw std::invalid_argument(
            "AQ10-DBC0 census has unaccounted roots");
    }
}

void require_frozen_census(const Census& census) {
    validate_census(census);
    if (census.subset_hash != kFrozenSubsetHash) {
        throw std::invalid_argument(
            "AQ10-DBC0 census differs from its frozen subset");
    }
}

Census project_frozen_census(
    const source::Census& full) {
    source::require_frozen_census(full);
    Census census = assemble_census(full);
    require_frozen_census(census);
    return census;
}

Census collect_census(
    std::shared_ptr<const LearnedModel> parent) {
    if (!parent ||
        learned_model_fingerprint(parent) !=
            kRequiredParentFingerprint ||
        learned_critic_schema(parent) !=
            LearnedCriticSchema::LegacyStateOnly) {
        throw std::invalid_argument(
            "AQ10-DBC0 requires exact frozen C16");
    }
    const source::Census full =
        source::collect_census(std::move(parent));
    return project_frozen_census(full);
}

void print_census(
    std::ostream& output, const Census& census) {
    validate_census(census);
    output
        << "schema=old-school-aq10-dbc0-census-v1\n"
        << "mode=census"
        << " parent_fingerprint="
        << census.parent_fingerprint
        << " source_manifest_hash="
        << census.source_manifest_hash
        << " subset_hash=" << census.subset_hash
        << " retained_position=0"
        << " policy_feature_width="
        << kPolicyFeatureCount
        << " successor_eligibility_measured=0\n";
    for (const Split split :
         std::array<Split, 2>{
             Split::Train, Split::Dev}) {
        const SplitCensus& row =
            census.splits[census_split_index(split)];
        output
            << "census_split split="
            << (split == Split::Train ? "TRAIN" : "DEV")
            << " games=" << row.games
            << " actor_games=" << row.actor_games
            << " roots=" << row.roots
            << " legal_options=" << row.legal_options
            << '\n';
        for (std::size_t deck = 0;
             deck < kDeckCount; ++deck) {
            const DeckCensus& deck_row =
                row.decks[deck];
            output
                << "census_deck split="
                << (split == Split::Train
                        ? "TRAIN"
                        : "DEV")
                << " deck="
                << deck_name(static_cast<DeckId>(deck))
                << " actor_games="
                << deck_row.actor_games
                << " roots=" << deck_row.roots
                << " legal_options="
                << deck_row.legal_options << '\n';
        }
    }
    output
        << "result=PASS disposition=CENSUS_ONLY"
        << " model_created=0 labels_scored=0"
        << " teacher_coordinates_opened=0"
        << " selector_opened=0 artifact_published=0\n";
}

namespace {

constexpr std::string_view kCorpusSchema =
    "old-school-aq10-dbc1-successor-boundary-v1";
constexpr double kProbabilityClamp = 1.0e-12;
constexpr double kStablePairMinimumDelta = 0.03;
constexpr double kNormal95CriticalValue = 1.96;
constexpr double kMetricTolerance = 1.0e-12;

bool probability(double value) {
    return std::isfinite(value) &&
           value >= 0.0 && value <= 1.0;
}

double clamped_probability(double value) {
    return std::clamp(
        value, kProbabilityClamp,
        1.0 - kProbabilityClamp);
}

void append_double(
    std::string& output, double value) {
    append_u64(
        output, std::bit_cast<std::uint64_t>(value));
}

void append_bool(
    std::string& output, bool value) {
    append_u64(output, value ? 1U : 0U);
}

void append_components(
    std::string& output,
    const LearnedModelComponentFingerprints& components) {
    append_string(output, components.critic);
    append_string(output, components.priority);
    append_string(output, components.attack);
    append_string(output, components.block);
    append_string(output, components.damage_order);
}

void append_accounting(
    std::string& output,
    const RootAccounting& accounting) {
    append_size(output, accounting.sampled_worlds);
    append_size(output, accounting.rollout_evaluations);
    append_size(output, accounting.terminal_evaluations);
    append_size(output, accounting.bootstrapped_evaluations);
    append_size(output, accounting.eligible_cells);
    append_size(
        output,
        accounting.terminal_before_boundary_cells);
    append_size(
        output, accounting.inner_rollout_evaluations);
    append_size(
        output, accounting.inner_search_invocations);
    append_size(output, accounting.inner_search_max_depth);
}

void append_example(
    std::string& output,
    const RootExample& example) {
    append_string(output, example.manifest.stable_root_id);
    append_string(
        output,
        example.manifest.information_action_fingerprint);
    append_size(output, example.teacher_samples.size());
    for (const auto& row : example.teacher_samples) {
        append_size(output, row.size());
        for (const double value : row) {
            append_double(output, value);
        }
    }
    append_size(output, example.cells.size());
    for (const BoundaryCell& cell : example.cells) {
        append_size(output, cell.action_index);
        append_size(output, cell.world_index);
        append_double(output, cell.teacher_target);
        append_double(output, cell.parent_prediction);
        append_double(output, cell.weight);
        append_bool(
            output, cell.terminal_before_boundary);
        append_size(output, cell.observation.size());
        for (const double feature : cell.observation) {
            append_double(output, feature);
        }
    }
    append_accounting(output, example.accounting);
}

constexpr std::string_view kCorpusCacheMagic =
    "OSDBCC01";
constexpr std::string_view kCorpusCacheSchema =
    "old-school-aq10-dbc1-owner-safe-corpus-cache-v1";
constexpr std::uint64_t kCorpusCacheVersion = 1;
constexpr std::size_t kMaximumCorpusCacheBytes =
    256U * 1024U * 1024U;
constexpr std::size_t kMaximumCacheStringBytes = 4096;
constexpr std::size_t kMaximumCacheActionsPerRoot = 1024;

void append_priority_action(
    std::string& output,
    const PriorityAction& action) {
    append_u64(
        output, static_cast<std::uint64_t>(action.kind));
    append_u64(
        output, static_cast<std::uint64_t>(action.card));
    append_bool(output, action.target.has_value());
    if (action.target) {
        append_size(output, action.target->player);
        append_bool(
            output, action.target->creature.has_value());
        if (action.target->creature) {
            append_u64(output, *action.target->creature);
        }
    }
    append_bool(output, action.spell_target.has_value());
    if (action.spell_target) {
        append_u64(output, *action.spell_target);
    }
    append_bool(
        output, action.source_permanent.has_value());
    if (action.source_permanent) {
        append_u64(output, *action.source_permanent);
    }
    append_u64(
        output,
        std::bit_cast<std::uint64_t>(
            static_cast<std::int64_t>(action.x_value)));
}

void append_cached_manifest_root(
    std::string& output,
    const ManifestRoot& root) {
    append_coordinate(output, root.coordinate);
    append_string(output, root.stable_root_id);
    append_string(
        output, root.information_action_fingerprint);
    append_size(output, root.actions.size());
    for (const PriorityAction& action : root.actions) {
        append_priority_action(output, action);
    }
    append_size(output, root.action_descriptors.size());
    for (const std::string& descriptor :
         root.action_descriptors) {
        append_string(output, descriptor);
    }
    append_size(output, root.options.size());
    for (const auto& option : root.options) {
        append_size(output, option.size());
        for (const double feature : option) {
            append_double(output, feature);
        }
    }
}

void append_cached_census(
    std::string& output, const Census& census) {
    append_string(output, census.parent_fingerprint);
    append_string(output, census.source_manifest_hash);
    append_size(output, census.splits.size());
    for (const SplitCensus& split : census.splits) {
        append_u64(
            output,
            static_cast<std::uint64_t>(split.split));
        append_size(output, split.games);
        append_size(output, split.actor_games);
        append_size(output, split.roots);
        append_size(output, split.legal_options);
        append_size(output, split.decks.size());
        for (const DeckCensus& deck : split.decks) {
            append_size(output, deck.actor_games);
            append_size(output, deck.roots);
            append_size(output, deck.legal_options);
        }
    }
    append_size(output, census.roots.size());
    for (const ManifestRoot& root : census.roots) {
        append_cached_manifest_root(output, root);
    }
    append_string(output, census.subset_hash);
}

void append_cached_example(
    std::string& output,
    const RootExample& example) {
    // The exact census owns the manifest. These two identities bind each
    // compact example to its canonical root without duplicating the
    // owner-safe action/feature manifest.
    append_string(
        output, example.manifest.stable_root_id);
    append_string(
        output,
        example.manifest.information_action_fingerprint);
    append_size(output, example.teacher_samples.size());
    for (const auto& samples : example.teacher_samples) {
        append_size(output, samples.size());
        for (const double sample : samples) {
            append_double(output, sample);
        }
    }
    append_size(output, example.cells.size());
    for (const BoundaryCell& cell : example.cells) {
        append_size(output, cell.action_index);
        append_size(output, cell.world_index);
        append_double(output, cell.teacher_target);
        append_double(output, cell.parent_prediction);
        append_double(output, cell.weight);
        append_bool(
            output, cell.terminal_before_boundary);
        append_size(output, cell.observation.size());
        for (const double feature : cell.observation) {
            append_double(output, feature);
        }
        // Deliberately no boundary_state field.
    }
    append_accounting(output, example.accounting);
}

std::string corpus_cache_payload(
    const Corpus& corpus) {
    std::string payload;
    append_string(payload, kCorpusCacheSchema);
    append_string(payload, kCorpusSchema);
    append_size(payload, kCriticFeatureCount);
    append_size(payload, kPolicyFeatureCount);
    append_u64(payload, kTeacherSeed);
    append_size(payload, kTeacherWorlds);
    append_size(payload, kTeacherRolloutsPerWorld);
    append_size(payload, kTeacherHorizonTurns);
    append_size(payload, kInnerWorlds);
    append_size(payload, kInnerHorizonTurns);
    append_cached_census(payload, corpus.census);
    append_components(
        payload, corpus.parent_components);
    append_string(payload, corpus.digest);
    append_size(payload, corpus.train.size());
    for (const RootExample& example : corpus.train) {
        append_cached_example(payload, example);
    }
    append_size(payload, corpus.dev.size());
    for (const RootExample& example : corpus.dev) {
        append_cached_example(payload, example);
    }
    return payload;
}

class CorpusCacheReader {
  public:
    explicit CorpusCacheReader(std::string_view bytes)
        : bytes_(bytes) {}

    std::uint64_t unsigned64() {
        require_available(8);
        std::uint64_t value = 0;
        for (unsigned int byte = 0; byte < 8; ++byte) {
            value |=
                static_cast<std::uint64_t>(
                    static_cast<unsigned char>(
                        bytes_[position_ + byte]))
                << (byte * 8);
        }
        position_ += 8;
        return value;
    }

    std::size_t size(
        std::size_t maximum,
        std::string_view field) {
        const std::uint64_t value = unsigned64();
        if (value > maximum ||
            value >
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::size_t>::max())) {
            throw std::runtime_error(
                "AQ10-DBC1 cache " +
                std::string(field) +
                " exceeds its bound");
        }
        return static_cast<std::size_t>(value);
    }

    bool boolean() {
        const std::uint64_t value = unsigned64();
        if (value > 1) {
            throw std::runtime_error(
                "AQ10-DBC1 cache boolean is invalid");
        }
        return value != 0;
    }

    double real() {
        return std::bit_cast<double>(unsigned64());
    }

    std::string text(
        std::size_t maximum =
            kMaximumCacheStringBytes) {
        const std::size_t count =
            size(maximum, "text length");
        require_available(count);
        std::string result(
            bytes_.substr(position_, count));
        position_ += count;
        return result;
    }

    std::string_view bytes(std::size_t count) {
        require_available(count);
        const std::string_view result =
            bytes_.substr(position_, count);
        position_ += count;
        return result;
    }

    void require_finished() const {
        if (position_ != bytes_.size()) {
            throw std::runtime_error(
                "AQ10-DBC1 cache has trailing bytes");
        }
    }

  private:
    void require_available(std::size_t count) const {
        if (count > bytes_.size() - position_) {
            throw std::runtime_error(
                "AQ10-DBC1 cache is truncated");
        }
    }

    std::string_view bytes_;
    std::size_t position_ = 0;
};

template <typename Enum>
Enum read_enum(
    CorpusCacheReader& input,
    std::uint64_t maximum,
    std::string_view field) {
    const std::uint64_t raw = input.unsigned64();
    if (raw > maximum) {
        throw std::runtime_error(
            "AQ10-DBC1 cache " +
            std::string(field) + " is invalid");
    }
    return static_cast<Enum>(raw);
}

source::RootCoordinate read_coordinate(
    CorpusCacheReader& input) {
    source::RootCoordinate coordinate;
    coordinate.split =
        read_enum<Split>(input, 1, "split");
    coordinate.schedule_index =
        input.size(
            source::kGamesPerSplit - 1,
            "schedule index");
    coordinate.pairing_index =
        input.size(
            source::kGamesPerSplit,
            "pairing index");
    coordinate.game_seed = input.unsigned64();
    coordinate.starting_player =
        input.size(1, "starting player");
    for (DeckId& deck : coordinate.seat_decks) {
        deck = read_enum<DeckId>(
            input, kDeckCount - 1, "deck");
    }
    coordinate.actor = input.size(1, "actor");
    coordinate.trace_ordinal =
        input.size(
            std::numeric_limits<std::size_t>::max(),
            "trace ordinal");
    coordinate.nontrivial_ordinal =
        input.size(
            std::numeric_limits<std::size_t>::max(),
            "nontrivial ordinal");
    coordinate.actor_game_nontrivial_roots =
        input.size(
            std::numeric_limits<std::size_t>::max(),
            "actor-game nontrivial roots");
    coordinate.retained_position =
        input.size(
            std::numeric_limits<std::size_t>::max(),
            "retained position");
    coordinate.actor_game_retained_roots =
        input.size(
            std::numeric_limits<std::size_t>::max(),
            "actor-game retained roots");
    coordinate.search_seed = input.unsigned64();
    return coordinate;
}

PriorityAction read_priority_action(
    CorpusCacheReader& input) {
    PriorityAction action;
    action.kind =
        read_enum<PriorityActionKind>(
            input,
            static_cast<std::uint64_t>(
                PriorityActionKind::ActivateMillstone),
            "priority action kind");
    action.card =
        read_enum<CardId>(
            input, kCardCount - 1, "card");
    if (input.boolean()) {
        Target target;
        target.player = input.size(1, "target player");
        if (input.boolean()) {
            target.creature = input.unsigned64();
        }
        action.target = target;
    }
    if (input.boolean()) {
        action.spell_target = input.unsigned64();
    }
    if (input.boolean()) {
        action.source_permanent = input.unsigned64();
    }
    const std::int64_t x_value =
        std::bit_cast<std::int64_t>(
            input.unsigned64());
    if (x_value <
            std::numeric_limits<int>::min() ||
        x_value >
            std::numeric_limits<int>::max()) {
        throw std::runtime_error(
            "AQ10-DBC1 cache X value is invalid");
    }
    action.x_value = static_cast<int>(x_value);
    return action;
}

ManifestRoot read_manifest_root(
    CorpusCacheReader& input) {
    ManifestRoot root;
    root.coordinate = read_coordinate(input);
    root.stable_root_id = input.text(64);
    root.information_action_fingerprint =
        input.text(64);
    const std::size_t action_count =
        input.size(
            kMaximumCacheActionsPerRoot,
            "action count");
    root.actions.reserve(action_count);
    for (std::size_t action = 0;
         action < action_count; ++action) {
        root.actions.push_back(
            read_priority_action(input));
    }
    const std::size_t descriptor_count =
        input.size(
            kMaximumCacheActionsPerRoot,
            "descriptor count");
    root.action_descriptors.reserve(descriptor_count);
    for (std::size_t descriptor = 0;
         descriptor < descriptor_count; ++descriptor) {
        root.action_descriptors.push_back(
            input.text());
    }
    const std::size_t option_count =
        input.size(
            kMaximumCacheActionsPerRoot,
            "policy-option count");
    root.options.reserve(option_count);
    for (std::size_t option = 0;
         option < option_count; ++option) {
        const std::size_t feature_count =
            input.size(
                kPolicyFeatureCount,
                "policy feature count");
        std::vector<double> features;
        features.reserve(feature_count);
        for (std::size_t feature = 0;
             feature < feature_count; ++feature) {
            features.push_back(input.real());
        }
        root.options.push_back(
            std::move(features));
    }
    return root;
}

Census read_cached_census(
    CorpusCacheReader& input) {
    Census census;
    census.parent_fingerprint = input.text(64);
    census.source_manifest_hash = input.text(64);
    const std::size_t split_count =
        input.size(2, "split count");
    if (split_count != census.splits.size()) {
        throw std::runtime_error(
            "AQ10-DBC1 cache split count drifted");
    }
    for (SplitCensus& split : census.splits) {
        split.split =
            read_enum<Split>(input, 1, "split");
        split.games =
            input.size(
                source::kGamesPerSplit,
                "game count");
        split.actor_games =
            input.size(
                source::kActorGamesPerSplit,
                "actor-game count");
        split.roots =
            input.size(
                kExpectedRootsPerSplit,
                "root count");
        split.legal_options =
            input.size(
                kExpectedRootsPerSplit *
                    kMaximumCacheActionsPerRoot,
                "legal-option count");
        const std::size_t deck_count =
            input.size(kDeckCount, "deck count");
        if (deck_count != split.decks.size()) {
            throw std::runtime_error(
                "AQ10-DBC1 cache deck count drifted");
        }
        for (DeckCensus& deck : split.decks) {
            deck.actor_games =
                input.size(
                    kExpectedRootsPerDeckAndSplit,
                    "deck actor-game count");
            deck.roots =
                input.size(
                    kExpectedRootsPerDeckAndSplit,
                    "deck root count");
            deck.legal_options =
                input.size(
                    kExpectedRootsPerDeckAndSplit *
                        kMaximumCacheActionsPerRoot,
                    "deck legal-option count");
        }
    }
    const std::size_t root_count =
        input.size(
            2 * kExpectedRootsPerSplit,
            "census root count");
    census.roots.reserve(root_count);
    for (std::size_t root = 0;
         root < root_count; ++root) {
        census.roots.push_back(
            read_manifest_root(input));
    }
    census.subset_hash = input.text(64);
    return census;
}

LearnedModelComponentFingerprints read_components(
    CorpusCacheReader& input) {
    return {
        .critic = input.text(64),
        .priority = input.text(64),
        .attack = input.text(64),
        .block = input.text(64),
        .damage_order = input.text(64),
    };
}

RootAccounting read_accounting(
    CorpusCacheReader& input) {
    const std::size_t maximum_cells =
        kMaximumCacheActionsPerRoot *
        kTeacherWorlds;
    return {
        .sampled_worlds =
            input.size(
                kTeacherWorlds, "sampled worlds"),
        .rollout_evaluations =
            input.size(
                maximum_cells,
                "rollout evaluations"),
        .terminal_evaluations =
            input.size(
                maximum_cells,
                "terminal evaluations"),
        .bootstrapped_evaluations =
            input.size(
                maximum_cells,
                "bootstrapped evaluations"),
        .eligible_cells =
            input.size(
                maximum_cells, "eligible cells"),
        .terminal_before_boundary_cells =
            input.size(
                maximum_cells,
                "terminal-before-boundary cells"),
        .inner_rollout_evaluations =
            input.size(
                std::numeric_limits<std::size_t>::max(),
                "inner rollout evaluations"),
        .inner_search_invocations =
            input.size(
                std::numeric_limits<std::size_t>::max(),
                "inner search invocations"),
        .inner_search_max_depth =
            input.size(1, "inner search max depth"),
    };
}

RootExample read_cached_example(
    CorpusCacheReader& input,
    const ManifestRoot& manifest) {
    if (input.text(64) != manifest.stable_root_id ||
        input.text(64) !=
            manifest.information_action_fingerprint) {
        throw std::runtime_error(
            "AQ10-DBC1 cache example root identity drifted");
    }
    RootExample example;
    example.manifest = manifest;
    const std::size_t action_count =
        input.size(
            kMaximumCacheActionsPerRoot,
            "teacher action count");
    example.teacher_samples.reserve(action_count);
    for (std::size_t action = 0;
         action < action_count; ++action) {
        const std::size_t world_count =
            input.size(
                kTeacherWorlds,
                "teacher world count");
        std::vector<double> samples;
        samples.reserve(world_count);
        for (std::size_t world = 0;
             world < world_count; ++world) {
            samples.push_back(input.real());
        }
        example.teacher_samples.push_back(
            std::move(samples));
    }
    const std::size_t cell_count =
        input.size(
            kMaximumCacheActionsPerRoot *
                kTeacherWorlds,
            "cell count");
    example.cells.reserve(cell_count);
    for (std::size_t index = 0;
         index < cell_count; ++index) {
        BoundaryCell cell{
            .action_index =
                input.size(
                    kMaximumCacheActionsPerRoot - 1,
                    "cell action index"),
            .world_index =
                input.size(
                    kTeacherWorlds - 1,
                    "cell world index"),
            .teacher_target = input.real(),
            .parent_prediction = input.real(),
            .weight = input.real(),
            .terminal_before_boundary =
                input.boolean(),
        };
        const std::size_t feature_count =
            input.size(
                kCriticFeatureCount,
                "critic feature count");
        cell.observation.reserve(feature_count);
        for (std::size_t feature = 0;
             feature < feature_count; ++feature) {
            cell.observation.push_back(
                input.real());
        }
        example.cells.push_back(std::move(cell));
    }
    example.accounting = read_accounting(input);
    return example;
}

std::vector<const ManifestRoot*> split_manifests(
    const Census& census, Split split) {
    std::vector<const ManifestRoot*> result;
    result.reserve(kExpectedRootsPerSplit);
    for (const ManifestRoot& root : census.roots) {
        if (root.coordinate.split == split) {
            result.push_back(&root);
        }
    }
    return result;
}

std::vector<RootExample> read_cached_examples(
    CorpusCacheReader& input,
    const Census& census, Split split) {
    const auto manifests =
        split_manifests(census, split);
    const std::size_t count =
        input.size(
            kExpectedRootsPerSplit,
            "example count");
    if (count != manifests.size()) {
        throw std::runtime_error(
            "AQ10-DBC1 cache example census drifted");
    }
    std::vector<RootExample> examples;
    examples.reserve(count);
    for (const ManifestRoot* manifest : manifests) {
        examples.push_back(
            read_cached_example(input, *manifest));
    }
    return examples;
}

std::string make_corpus_cache_file(
    const Corpus& corpus) {
    const std::string payload =
        corpus_cache_payload(corpus);
    if (payload.size() > kMaximumCorpusCacheBytes) {
        throw std::length_error(
            "AQ10-DBC1 cache payload exceeds its bound");
    }
    std::string file;
    file.append(kCorpusCacheMagic);
    append_u64(file, kCorpusCacheVersion);
    append_size(file, payload.size());
    append_string(
        file,
        artifact_integrity::sha256_string(payload));
    file.append(payload);
    return file;
}

Corpus parse_corpus_cache_file(
    std::string_view file) {
    CorpusCacheReader envelope(file);
    if (envelope.bytes(kCorpusCacheMagic.size()) !=
            kCorpusCacheMagic ||
        envelope.unsigned64() != kCorpusCacheVersion) {
        throw std::runtime_error(
            "AQ10-DBC1 cache magic or version is invalid");
    }
    const std::size_t payload_size =
        envelope.size(
            kMaximumCorpusCacheBytes,
            "payload size");
    const std::string payload_digest =
        envelope.text(64);
    const std::string_view payload =
        envelope.bytes(payload_size);
    envelope.require_finished();
    if (payload_digest.size() != 64 ||
        payload_digest !=
            artifact_integrity::sha256_string(payload)) {
        throw std::runtime_error(
            "AQ10-DBC1 cache payload digest is invalid");
    }

    CorpusCacheReader input(payload);
    if (input.text() != kCorpusCacheSchema ||
        input.text() != kCorpusSchema ||
        input.size(
            kCriticFeatureCount,
            "critic schema width") !=
            kCriticFeatureCount ||
        input.size(
            kPolicyFeatureCount,
            "policy schema width") !=
            kPolicyFeatureCount ||
        input.unsigned64() != kTeacherSeed ||
        input.size(
            kTeacherWorlds,
            "teacher schema worlds") !=
            kTeacherWorlds ||
        input.size(
            kTeacherRolloutsPerWorld,
            "teacher schema rollouts") !=
            kTeacherRolloutsPerWorld ||
        input.size(
            kTeacherHorizonTurns,
            "teacher schema horizon") !=
            kTeacherHorizonTurns ||
        input.size(
            kInnerWorlds,
            "inner schema worlds") !=
            kInnerWorlds ||
        input.size(
            kInnerHorizonTurns,
            "inner schema horizon") !=
            kInnerHorizonTurns) {
        throw std::runtime_error(
            "AQ10-DBC1 cache recipe is invalid");
    }
    Corpus corpus;
    corpus.census = read_cached_census(input);
    corpus.parent_components = read_components(input);
    corpus.digest = input.text(64);
    corpus.train =
        read_cached_examples(
            input, corpus.census, Split::Train);
    corpus.dev =
        read_cached_examples(
            input, corpus.census, Split::Dev);
    input.require_finished();
    validate_corpus(corpus);
    return corpus;
}

std::string read_corpus_cache_file(
    const std::filesystem::path& path) {
    if (path.empty() || path.filename().empty()) {
        throw std::invalid_argument(
            "AQ10-DBC1 cache path must name a file");
    }
    std::error_code size_error;
    const std::uintmax_t byte_count =
        std::filesystem::file_size(path, size_error);
    if (size_error ||
        byte_count > kMaximumCorpusCacheBytes +
                         4096U ||
        byte_count >
            static_cast<std::uintmax_t>(
                std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error(
            "AQ10-DBC1 cache file is missing or exceeds its bound");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(
            "cannot open AQ10-DBC1 cache");
    }
    std::string bytes(
        static_cast<std::size_t>(byte_count), '\0');
    if (!bytes.empty()) {
        input.read(
            bytes.data(),
            static_cast<std::streamsize>(bytes.size()));
    }
    if (input.gcount() !=
            static_cast<std::streamsize>(bytes.size()) ||
        input.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error(
            "AQ10-DBC1 cache changed or was truncated while reading");
    }
    return bytes;
}

void write_corpus_cache_file_atomic(
    const std::filesystem::path& path,
    std::string_view bytes) {
    if (path.empty() || path.filename().empty()) {
        throw std::invalid_argument(
            "AQ10-DBC1 cache path must name a file");
    }
    const std::filesystem::path directory =
        path.has_parent_path()
            ? path.parent_path()
            : std::filesystem::path(".");
    std::error_code directory_error;
    std::filesystem::create_directories(
        directory, directory_error);
    if (directory_error) {
        throw std::runtime_error(
            "cannot create AQ10-DBC1 cache directory: " +
            directory_error.message());
    }

    static std::atomic<std::uint64_t> counter{0};
    std::filesystem::path temporary;
    int descriptor = -1;
    for (std::size_t attempt = 0;
         attempt < 128; ++attempt) {
        temporary =
            directory /
            (path.filename().string() + ".tmp." +
             std::to_string(
                 static_cast<unsigned long long>(
                     ::getpid())) +
             "." +
             std::to_string(
                 counter.fetch_add(
                     1,
                     std::memory_order_relaxed)));
        descriptor = ::open(
            temporary.c_str(),
            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
            0644);
        if (descriptor >= 0) {
            break;
        }
        if (errno != EEXIST) {
            throw std::runtime_error(
                "cannot create temporary AQ10-DBC1 cache: " +
                std::string(std::strerror(errno)));
        }
    }
    if (descriptor < 0) {
        throw std::runtime_error(
            "cannot reserve temporary AQ10-DBC1 cache");
    }
    const auto cleanup = [&] {
        if (descriptor >= 0) {
            static_cast<void>(::close(descriptor));
            descriptor = -1;
        }
        static_cast<void>(::unlink(temporary.c_str()));
    };
    std::size_t position = 0;
    while (position < bytes.size()) {
        const ssize_t written = ::write(
            descriptor,
            bytes.data() + position,
            bytes.size() - position);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            const std::string detail =
                std::strerror(errno);
            cleanup();
            throw std::runtime_error(
                "cannot write temporary AQ10-DBC1 cache: " +
                detail);
        }
        if (written == 0) {
            cleanup();
            throw std::runtime_error(
                "temporary AQ10-DBC1 cache write made no progress");
        }
        position += static_cast<std::size_t>(written);
    }
    if (::fsync(descriptor) != 0) {
        const std::string detail =
            std::strerror(errno);
        cleanup();
        throw std::runtime_error(
            "cannot sync temporary AQ10-DBC1 cache: " +
            detail);
    }
    if (::close(descriptor) != 0) {
        const std::string detail =
            std::strerror(errno);
        descriptor = -1;
        static_cast<void>(
            ::unlink(temporary.c_str()));
        throw std::runtime_error(
            "cannot close temporary AQ10-DBC1 cache: " +
            detail);
    }
    descriptor = -1;
    const int directory_descriptor =
        ::open(
            directory.c_str(),
            O_RDONLY | O_CLOEXEC);
    if (directory_descriptor < 0) {
        const std::string detail =
            std::strerror(errno);
        static_cast<void>(
            ::unlink(temporary.c_str()));
        throw std::runtime_error(
            "cannot open AQ10-DBC1 cache directory: " +
            detail);
    }
    // A frozen scientific cache is publish-once. Linking the already-synced
    // temporary inode to the final name is atomic and, unlike rename, fails
    // if that name already exists.
    if (::link(
            temporary.c_str(), path.c_str()) != 0) {
        const std::string detail =
            std::strerror(errno);
        static_cast<void>(
            ::close(directory_descriptor));
        static_cast<void>(
            ::unlink(temporary.c_str()));
        throw std::runtime_error(
            "cannot atomically publish AQ10-DBC1 cache: " +
            detail);
    }
    if (::unlink(temporary.c_str()) != 0) {
        const std::string detail =
            std::strerror(errno);
        static_cast<void>(
            ::close(directory_descriptor));
        throw std::runtime_error(
            "cannot retire published AQ10-DBC1 cache temporary: " +
            detail);
    }
    if (::fsync(directory_descriptor) != 0) {
        const std::string detail =
            std::strerror(errno);
        static_cast<void>(
            ::close(directory_descriptor));
        throw std::runtime_error(
            "cannot sync AQ10-DBC1 cache directory: " +
            detail);
    }
    if (::close(directory_descriptor) != 0) {
        throw std::runtime_error(
            "cannot close AQ10-DBC1 cache directory");
    }
}

GameState hidden_repartition_clone(
    const GameState& state, std::size_t observer) {
    if (observer >= state.players.size()) {
        throw std::out_of_range(
            "AQ10-DBC1 hidden-repartition observer is invalid");
    }
    GameState clone = state;
    std::reverse(
        clone.players[observer].library.begin(),
        clone.players[observer].library.end());

    PlayerState& hidden =
        clone.players[1U - observer];
    const std::size_t hand_size = hidden.hand.size();
    std::vector<CardId> hidden_cards = hidden.hand;
    hidden_cards.insert(
        hidden_cards.end(),
        hidden.library.begin(), hidden.library.end());
    if (hidden_cards.size() > 1) {
        std::rotate(
            hidden_cards.begin(),
            hidden_cards.begin() + 1,
            hidden_cards.end());
        std::reverse(
            hidden_cards.begin(), hidden_cards.end());
    }
    const auto hand_end =
        hidden_cards.begin() +
        static_cast<std::ptrdiff_t>(hand_size);
    hidden.hand.assign(
        hidden_cards.begin(), hand_end);
    hidden.library.assign(
        hand_end, hidden_cards.end());
    if (observe_game_state(state, observer) !=
        observe_game_state(clone, observer)) {
        throw std::logic_error(
            "AQ10-DBC1 hidden repartition changed owner information");
    }
    if (clone == state) {
        throw std::runtime_error(
            "AQ10-DBC1 hidden repartition witness is vacuous");
    }
    return clone;
}

void validate_root_example(
    const RootExample& example,
    const ManifestRoot& expected) {
    if (example.manifest != expected ||
        example.teacher_samples.size() !=
            expected.actions.size() ||
        example.cells.size() !=
            expected.actions.size() *
                kTeacherWorlds ||
        example.accounting.sampled_worlds !=
            kTeacherWorlds ||
        example.accounting.rollout_evaluations !=
            example.cells.size() ||
        example.accounting.terminal_evaluations +
                example.accounting.bootstrapped_evaluations !=
            example.accounting.rollout_evaluations ||
        example.accounting.inner_search_max_depth > 1 ||
        example.accounting.eligible_cells == 0 ||
        example.accounting.eligible_cells +
                example.accounting
                    .terminal_before_boundary_cells !=
            example.cells.size() ||
        example.accounting.inner_rollout_evaluations == 0 ||
        example.accounting.inner_search_invocations == 0 ||
        example.accounting.inner_search_max_depth != 1) {
        throw std::invalid_argument(
            "AQ10-DBC1 root shape or accounting is invalid");
    }

    std::size_t eligible = 0;
    std::size_t terminal_before = 0;
    double weight_sum = 0.0;
    for (std::size_t action = 0;
         action < expected.actions.size(); ++action) {
        const auto& samples =
            example.teacher_samples[action];
        if (samples.size() != kTeacherWorlds ||
            !std::all_of(
                samples.begin(), samples.end(),
                probability)) {
            throw std::invalid_argument(
                "AQ10-DBC1 teacher sample row is invalid");
        }
        for (std::size_t world = 0;
             world < kTeacherWorlds; ++world) {
            const BoundaryCell& cell =
                example.cells[
                    action * kTeacherWorlds + world];
            if (cell.action_index != action ||
                cell.world_index != world ||
                cell.teacher_target != samples[world] ||
                !probability(cell.teacher_target) ||
                !probability(cell.parent_prediction)) {
                throw std::invalid_argument(
                    "AQ10-DBC1 boundary cell identity is invalid");
            }
            if (cell.terminal_before_boundary) {
                ++terminal_before;
                if (!cell.observation.empty() ||
                    cell.boundary_state.has_value() ||
                    cell.weight != 0.0 ||
                    cell.parent_prediction !=
                        cell.teacher_target) {
                    throw std::invalid_argument(
                        "AQ10-DBC1 terminal cell retained a critic input");
                }
                continue;
            }
            ++eligible;
            if (cell.observation.size() !=
                    kCriticFeatureCount ||
                !std::all_of(
                    cell.observation.begin(),
                    cell.observation.end(),
                    [](double value) {
                        return std::isfinite(value);
                    }) ||
                !std::isfinite(cell.weight) ||
                cell.weight <= 0.0) {
                throw std::invalid_argument(
                    "AQ10-DBC1 successor critic example is invalid");
            }
            weight_sum += cell.weight;
        }
    }
    const double expected_root_mass =
        1.0 /
        static_cast<double>(
            kDeckCount *
            kExpectedRootsPerDeckAndSplit);
    if (eligible != example.accounting.eligible_cells ||
        terminal_before !=
            example.accounting
                .terminal_before_boundary_cells ||
        std::abs(weight_sum - expected_root_mass) >
            kMetricTolerance) {
        throw std::invalid_argument(
            "AQ10-DBC1 root eligibility weight does not cross-sum");
    }
}

RootExample label_root(
    const ManifestRoot& manifest,
    const LearnedDecisionTracePoint& point,
    const std::array<std::vector<CardId>, 2>& decks,
    const std::shared_ptr<const LearnedModel>& parent,
    bool repartition_hidden) {
    const auto& coordinate = manifest.coordinate;
    if (!point.context.valid ||
        point.context.decision_player !=
            coordinate.actor) {
        throw std::logic_error(
            "AQ10-DBC1 live replay context drifted");
    }
    GameState source_state =
        repartition_hidden
            ? hidden_repartition_clone(
                  point.state, coordinate.actor)
            : point.state;
    const auto live_actions =
        legal_priority_actions(
            source_state, coordinate.actor,
            point.context.sorcery_actions);
    if (live_actions != manifest.actions) {
        throw std::logic_error(
            "AQ10-DBC1 live replay legal actions drifted");
    }

    const LearnedActionSamples samples =
        learned_priority_action_samples(
            source_state, decks, coordinate.actor,
            point.context.sorcery_actions,
            point.context.phase,
            point.context.consecutive_passes,
            manifest.actions, parent,
            teacher_search_config(
                teacher_search_seed(coordinate)));
    const std::size_t expected_cells =
        manifest.actions.size() * kTeacherWorlds;
    if (samples.sampled_worlds != kTeacherWorlds ||
        samples.rollout_evaluations != expected_cells ||
        samples.q_samples.size() !=
            manifest.actions.size() ||
        samples.priority_shallow_prior_samples.size() !=
            manifest.actions.size() ||
        samples.priority_continuation_samples.size() !=
            manifest.actions.size() ||
        samples.priority_h0_boundaries.size() !=
            manifest.actions.size() ||
        samples.terminal_evaluation_flags.size() !=
            manifest.actions.size() ||
        samples.terminal_evaluations +
                samples.bootstrapped_evaluations !=
            samples.rollout_evaluations ||
        samples.inner_search_max_depth > 1) {
        throw std::logic_error(
            "AQ10-DBC1 teacher accounting drifted");
    }

    RootExample result;
    result.manifest = manifest;
    result.teacher_samples = samples.q_samples;
    result.accounting = {
        .sampled_worlds = samples.sampled_worlds,
        .rollout_evaluations =
            samples.rollout_evaluations,
        .terminal_evaluations =
            samples.terminal_evaluations,
        .bootstrapped_evaluations =
            samples.bootstrapped_evaluations,
        .eligible_cells = 0,
        .terminal_before_boundary_cells = 0,
        .inner_rollout_evaluations =
            samples.inner_rollout_evaluations,
        .inner_search_invocations =
            samples.inner_search_invocations,
        .inner_search_max_depth =
            samples.inner_search_max_depth,
    };
    result.cells.reserve(expected_cells);
    for (std::size_t action = 0;
         action < manifest.actions.size(); ++action) {
        if (samples.q_samples[action].size() !=
                kTeacherWorlds ||
            samples.priority_shallow_prior_samples[action].size() !=
                kTeacherWorlds ||
            samples.priority_continuation_samples[action].size() !=
                kTeacherWorlds ||
            samples.priority_h0_boundaries[action].size() !=
                kTeacherWorlds ||
            samples.terminal_evaluation_flags[action].size() !=
                kTeacherWorlds) {
            throw std::logic_error(
                "AQ10-DBC1 teacher cell row drifted");
        }
        for (std::size_t world = 0;
             world < kTeacherWorlds; ++world) {
            const double target =
                samples.q_samples[action][world];
            const auto& boundary =
                samples.priority_h0_boundaries[action][world];
            if (samples.priority_shallow_prior_samples
                    [action][world] != 0.0 ||
                samples.priority_continuation_samples
                    [action][world] != target ||
                (boundary.terminal &&
                 samples.terminal_evaluation_flags
                         [action][world] != 1U) ||
                (boundary.terminal &&
                 boundary.continuation_score != target)) {
                throw std::logic_error(
                    "AQ10-DBC1 target components or boundary "
                    "terminal identity drifted");
            }
            BoundaryCell cell{
                .action_index = action,
                .world_index = world,
                .teacher_target = target,
                .parent_prediction =
                    boundary.terminal
                        ? target
                        : boundary.continuation_score,
                .weight = 0.0,
                .terminal_before_boundary =
                    boundary.terminal,
            };
            if (boundary.terminal) {
                ++result.accounting
                      .terminal_before_boundary_cells;
            } else {
                if (!boundary.context.valid ||
                    boundary.context.phase !=
                        TurnPhase::FirstMain ||
                    boundary.context.decision_player !=
                        boundary.state.active_player) {
                    throw std::logic_error(
                        "AQ10-DBC1 captured boundary context drifted");
                }
                cell.observation =
                    learned_observation(
                        boundary.state,
                        coordinate.actor);
                cell.boundary_state = boundary.state;
                const double direct =
                    learned_critic_value(
                        boundary.state,
                        coordinate.actor, parent);
                if (direct !=
                    boundary.continuation_score) {
                    throw std::logic_error(
                        "AQ10-DBC1 captured parent prediction drifted");
                }
                ++result.accounting.eligible_cells;
            }
            result.cells.push_back(std::move(cell));
        }
    }
    if (result.accounting.eligible_cells == 0) {
        throw std::runtime_error(
            "AQ10-DBC1 root has no successor critic example");
    }
    const double cell_weight =
        1.0 /
        static_cast<double>(
            kDeckCount *
            kExpectedRootsPerDeckAndSplit *
            result.accounting.eligible_cells);
    for (BoundaryCell& cell : result.cells) {
        if (!cell.terminal_before_boundary) {
            cell.weight = cell_weight;
        }
    }
    validate_root_example(result, manifest);
    return result;
}

Corpus empty_corpus(
    const Census& subset,
    const std::shared_ptr<const LearnedModel>& parent) {
    return {
        .census = subset,
        .parent_components =
            learned_model_component_fingerprints(parent),
    };
}

void append_labeled_root(
    Corpus& corpus, RootExample example) {
    if (example.manifest.coordinate.split ==
        Split::Train) {
        corpus.train.push_back(std::move(example));
    } else {
        corpus.dev.push_back(std::move(example));
    }
}

void finalize_collected_corpus(Corpus& corpus) {
    corpus.digest =
        canonical_corpus_digest(corpus);
    validate_corpus(corpus);
}

Corpus collect_corpus_from_authenticated_source(
    const std::shared_ptr<const LearnedModel>& parent,
    const Census& subset,
    const source::Census& full,
    bool hidden_repartition_source) {
    Corpus result = empty_corpus(subset, parent);
    std::size_t subset_position = 0;
    const source::Census replayed =
        source::replay_frozen_source_roots(
            parent, full,
            [&](const ManifestRoot& root,
                const LearnedDecisionTracePoint& point,
                const std::array<
                    std::vector<CardId>, 2>& decks) {
                if (root.coordinate.retained_position != 0) {
                    return;
                }
                if (subset_position >=
                        subset.roots.size() ||
                    root !=
                        subset.roots[
                            subset_position]) {
                    throw std::runtime_error(
                        "AQ10-DBC1 live subset root drifted");
                }
                append_labeled_root(
                    result,
                    label_root(
                        root, point, decks, parent,
                        hidden_repartition_source));
                ++subset_position;
            });
    if (replayed != full ||
        subset_position != subset.roots.size()) {
        throw std::runtime_error(
            "AQ10-DBC1 authenticated replay did not cross-sum");
    }
    finalize_collected_corpus(result);
    return result;
}

std::array<Corpus, 3> collect_run_corpora(
    const std::shared_ptr<const LearnedModel>& parent,
    const Census& subset,
    const source::Census& full) {
    std::array<Corpus, 3> result{
        empty_corpus(subset, parent),
        empty_corpus(subset, parent),
        empty_corpus(subset, parent),
    };
    std::size_t subset_position = 0;
    const source::Census replayed =
        source::replay_frozen_source_roots(
            parent, full,
            [&](const ManifestRoot& root,
                const LearnedDecisionTracePoint& point,
                const std::array<
                    std::vector<CardId>, 2>& decks) {
                if (root.coordinate.retained_position != 0) {
                    return;
                }
                if (subset_position >=
                        subset.roots.size() ||
                    root !=
                        subset.roots[
                            subset_position]) {
                    throw std::runtime_error(
                        "AQ10-DBC1 repeated live subset root drifted");
                }
                append_labeled_root(
                    result[0],
                    label_root(
                        root, point, decks, parent, false));
                append_labeled_root(
                    result[1],
                    label_root(
                        root, point, decks, parent, false));
                append_labeled_root(
                    result[2],
                    label_root(
                        root, point, decks, parent, true));
                ++subset_position;
            });
    if (replayed != full ||
        subset_position != subset.roots.size()) {
        throw std::runtime_error(
            "AQ10-DBC1 repeated authenticated replay did not "
            "cross-sum");
    }
    for (Corpus& corpus : result) {
        finalize_collected_corpus(corpus);
    }
    return result;
}

std::size_t calibration_bin(double prediction) {
    if (prediction >= 1.0) {
        return kCalibrationBinCount - 1;
    }
    return std::min(
        static_cast<std::size_t>(
            prediction *
            static_cast<double>(kCalibrationBinCount)),
        kCalibrationBinCount - 1);
}

double sample_standard_error(
    const std::vector<double>& samples) {
    if (samples.size() < 2) {
        throw std::invalid_argument(
            "AQ10-DBC1 stable pair needs paired samples");
    }
    const double mean =
        std::accumulate(
            samples.begin(), samples.end(), 0.0) /
        static_cast<double>(samples.size());
    double squared = 0.0;
    for (const double value : samples) {
        const double delta = value - mean;
        squared += delta * delta;
    }
    return std::sqrt(
        squared /
        static_cast<double>(
            samples.size() *
            (samples.size() - 1)));
}

std::vector<std::size_t> exact_max_support(
    std::span<const double> values) {
    if (values.empty()) {
        throw std::invalid_argument(
            "AQ10-DBC1 exact maximum requires values");
    }
    const double maximum =
        *std::max_element(values.begin(), values.end());
    std::vector<std::size_t> support;
    for (std::size_t index = 0;
         index < values.size(); ++index) {
        if (values[index] == maximum) {
            support.push_back(index);
        }
    }
    return support;
}

struct MetricAccumulator {
    std::size_t roots = 0;
    std::size_t eligible_cells = 0;
    std::size_t stable_pairs = 0;
    double weight_mass = 0.0;
    double bce_sum = 0.0;
    double brier_sum = 0.0;
    double bias_sum = 0.0;
    double top_one_sum = 0.0;
    double stable_pair_sum = 0.0;
    double regret_sum = 0.0;
    std::array<double, kCalibrationBinCount>
        bin_weights{};
    std::array<double, kCalibrationBinCount>
        bin_prediction_sums{};
    std::array<double, kCalibrationBinCount>
        bin_target_sums{};
};

DeckMetrics finalize_metrics(
    const MetricAccumulator& source,
    DeckId deck) {
    if (source.roots == 0 ||
        source.eligible_cells == 0 ||
        source.weight_mass <= 0.0) {
        throw std::invalid_argument(
            "AQ10-DBC1 metric deck is empty");
    }
    DeckMetrics result{
        .deck = deck,
        .roots = source.roots,
        .eligible_cells = source.eligible_cells,
        .stable_pairs = source.stable_pairs,
        .weight_mass = source.weight_mass,
        .weighted_bce =
            source.bce_sum / source.weight_mass,
        .weighted_brier =
            source.brier_sum / source.weight_mass,
        .weighted_bias =
            source.bias_sum / source.weight_mass,
        .weighted_ece = 0.0,
        .top_one_expected_agreement =
            source.top_one_sum /
            static_cast<double>(source.roots),
        .stable_pair_agreement =
            source.stable_pairs == 0
                ? 0.0
                : source.stable_pair_sum /
                      static_cast<double>(
                          source.stable_pairs),
        .mean_regret =
            source.regret_sum /
            static_cast<double>(source.roots),
    };
    for (std::size_t bin = 0;
         bin < kCalibrationBinCount; ++bin) {
        if (source.bin_weights[bin] == 0.0) {
            continue;
        }
        const double prediction =
            source.bin_prediction_sums[bin] /
            source.bin_weights[bin];
        const double target =
            source.bin_target_sums[bin] /
            source.bin_weights[bin];
        result.weighted_ece +=
            source.bin_weights[bin] *
            std::abs(prediction - target);
    }
    result.weighted_ece /= source.weight_mass;
    return result;
}

void add_gate(
    bool condition, std::string_view failure,
    OfflineGate& gate) {
    if (!condition) {
        gate.failures.emplace_back(failure);
    }
}

bool output_parameters_equal(
    const LearnedOutputCalibrationParameters& left,
    const LearnedOutputCalibrationParameters& right) {
    return left == right;
}

std::size_t changed_output_parameter_count(
    const LearnedOutputCalibrationParameters& parent,
    const LearnedOutputCalibrationParameters& candidate) {
    std::size_t changed = 0;
    for (std::size_t leaf = 0;
         leaf < parent.leaves.size(); ++leaf) {
        for (std::size_t parameter = 0;
             parameter < parent.leaves[leaf].size();
             ++parameter) {
            changed +=
                std::bit_cast<std::uint64_t>(
                    parent.leaves[leaf][parameter]) !=
                std::bit_cast<std::uint64_t>(
                    candidate.leaves[leaf][parameter])
                    ? 1U
                    : 0U;
        }
    }
    return changed;
}

} // namespace

std::uint64_t teacher_search_seed(
    const source::RootCoordinate& coordinate) {
    if (coordinate.schedule_index >=
            source::kGamesPerSplit ||
        coordinate.actor >= 2 ||
        coordinate.nontrivial_ordinal >
            std::numeric_limits<std::uint32_t>::max()) {
        throw std::out_of_range(
            "AQ10-DBC1 teacher seed coordinate is invalid");
    }
    const std::uint64_t subindex =
        (static_cast<std::uint64_t>(
             coordinate.actor)
         << 32) |
        static_cast<std::uint64_t>(
            coordinate.nontrivial_ordinal);
    return learned_iteration::derive_seed(
        kTeacherSeed,
        learned_iteration::SeedDomain::PrioritySearch,
        source::split_index(coordinate.split),
        coordinate.schedule_index, subindex);
}

LearnedSearchConfig teacher_search_config(
    std::uint64_t seed) {
    LearnedSearchConfig config =
        learned_value_actor_local_search_config(seed);
    if (config.worlds != kTeacherWorlds ||
        config.rollouts_per_world !=
            kTeacherRolloutsPerWorld ||
        config.horizon_turns !=
            kTeacherHorizonTurns ||
        config.evaluation_threads !=
            kTeacherThreads ||
        config.value_continuation_search_worlds !=
            kInnerWorlds ||
        kLearnedValueSearchHorizonTurns !=
            kInnerHorizonTurns ||
        config.continuation_variant !=
            LearnedVariant::ValueSearchChampion ||
        config.value_continuation_epsilon != 0.0 ||
        config.blend_shallow_prior ||
        config.value_resolved_shallow_prior_weight != 0.0 ||
        config.value_priority_residual_weight != 0.0 ||
        config.value_pass_dominance ||
        config.value_continuation_controller !=
            LearnedContinuationController::Legacy ||
        config.value_continuation_search_scope !=
            LearnedContinuationSearchScope::PriorityOnly) {
        throw std::logic_error(
            "AQ10-DBC1 inherited AQ4-D1 recipe drifted");
    }
    config.capture_priority_h0_boundaries = true;
    config.terminal_utility_mode =
        LearnedTerminalUtilityMode::
            C16DiscountedAbsoluteTurn;
    return config;
}

std::string canonical_corpus_digest(
    const Corpus& corpus) {
    std::string payload;
    append_string(payload, kCorpusSchema);
    append_u64(payload, kTeacherSeed);
    append_size(payload, kTeacherWorlds);
    append_size(payload, kTeacherRolloutsPerWorld);
    append_size(payload, kTeacherHorizonTurns);
    append_size(payload, kTeacherThreads);
    append_size(payload, kInnerWorlds);
    append_size(payload, kInnerHorizonTurns);
    append_string(payload, corpus.census.subset_hash);
    append_components(
        payload, corpus.parent_components);
    append_size(payload, corpus.train.size());
    for (const RootExample& example : corpus.train) {
        append_example(payload, example);
    }
    append_size(payload, corpus.dev.size());
    for (const RootExample& example : corpus.dev) {
        append_example(payload, example);
    }
    return artifact_integrity::sha256_string(payload);
}

void validate_corpus(const Corpus& corpus) {
    validate_census(corpus.census);
    if (corpus.parent_components.critic.size() != 64 ||
        corpus.parent_components.priority.size() != 64 ||
        corpus.parent_components.attack.size() != 64 ||
        corpus.parent_components.block.size() != 64 ||
        corpus.parent_components.damage_order.size() != 64 ||
        corpus.digest.size() != 64 ||
        corpus.digest != canonical_corpus_digest(corpus)) {
        throw std::invalid_argument(
            "AQ10-DBC1 corpus identity is invalid");
    }

    std::array<std::size_t, 2> positions{};
    std::array<
        std::array<double, kDeckCount>, 2>
        deck_mass{};
    for (const ManifestRoot& expected :
         corpus.census.roots) {
        const std::size_t split =
            source::split_index(
                expected.coordinate.split);
        const auto& examples =
            expected.coordinate.split == Split::Train
                ? corpus.train
                : corpus.dev;
        if (positions[split] >= examples.size()) {
            throw std::invalid_argument(
                "AQ10-DBC1 corpus omitted a census root");
        }
        const RootExample& example =
            examples[positions[split]++];
        validate_root_example(example, expected);
        const std::size_t deck =
            deck_index(
                expected.coordinate.owner_deck());
        for (const BoundaryCell& cell : example.cells) {
            deck_mass[split][deck] += cell.weight;
        }
    }
    if (positions[0] != corpus.train.size() ||
        positions[1] != corpus.dev.size()) {
        throw std::invalid_argument(
            "AQ10-DBC1 corpus has unaccounted examples");
    }
    const double expected_deck_mass =
        1.0 / static_cast<double>(kDeckCount);
    for (const auto& split : deck_mass) {
        for (const double mass : split) {
            if (std::abs(
                    mass - expected_deck_mass) >
                kMetricTolerance) {
                throw std::invalid_argument(
                    "AQ10-DBC1 equal-deck weight drifted");
            }
        }
    }
}

void write_corpus_cache_atomic(
    const std::filesystem::path& path,
    const Corpus& corpus) {
    validate_corpus(corpus);
    require_frozen_census(corpus.census);
    if (corpus.digest != kFrozenCorpusDigest) {
        throw std::runtime_error(
            "AQ10-DBC1 production cache corpus digest drifted");
    }
    write_corpus_cache_file_atomic(
        path, make_corpus_cache_file(corpus));
}

Corpus roundtrip_corpus_cache(
    const Corpus& corpus,
    std::shared_ptr<const LearnedModel> parent) {
    if (!parent ||
        learned_model_fingerprint(parent) !=
            kRequiredParentFingerprint ||
        learned_critic_schema(parent) !=
            LearnedCriticSchema::LegacyStateOnly) {
        throw std::invalid_argument(
            "AQ10-DBC1 cache roundtrip requires exact frozen C16");
    }
    validate_corpus(corpus);
    require_frozen_census(corpus.census);
    if (corpus.digest != kFrozenCorpusDigest ||
        corpus.parent_components !=
            learned_model_component_fingerprints(parent)) {
        throw std::runtime_error(
            "AQ10-DBC1 cache roundtrip identity drifted");
    }
    Corpus expected = corpus;
    for (auto* examples :
         {&expected.train, &expected.dev}) {
        for (RootExample& root : *examples) {
            for (BoundaryCell& cell : root.cells) {
                cell.boundary_state.reset();
            }
        }
    }
    Corpus decoded =
        parse_corpus_cache_file(
            make_corpus_cache_file(corpus));
    if (decoded != expected) {
        throw std::runtime_error(
            "AQ10-DBC1 cache codec roundtrip drifted");
    }
    return decoded;
}

Corpus load_corpus_cache(
    const std::filesystem::path& path,
    std::shared_ptr<const LearnedModel> parent) {
    if (!parent ||
        learned_model_fingerprint(parent) !=
            kRequiredParentFingerprint ||
        learned_critic_schema(parent) !=
            LearnedCriticSchema::LegacyStateOnly) {
        throw std::invalid_argument(
            "AQ10-DBC1 cache requires exact frozen C16");
    }
    Corpus corpus =
        parse_corpus_cache_file(
            read_corpus_cache_file(path));
    require_frozen_census(corpus.census);
    if (corpus.digest != kFrozenCorpusDigest ||
        corpus.parent_components !=
            learned_model_component_fingerprints(parent)) {
        throw std::runtime_error(
            "AQ10-DBC1 cache frozen corpus identity drifted");
    }
    return corpus;
}

Corpus collect_corpus(
    std::shared_ptr<const LearnedModel> parent,
    const Census& frozen_subset,
    bool hidden_repartition_source) {
    if (!parent ||
        learned_model_fingerprint(parent) !=
            kRequiredParentFingerprint ||
        learned_critic_schema(parent) !=
            LearnedCriticSchema::LegacyStateOnly) {
        throw std::invalid_argument(
            "AQ10-DBC1 requires exact frozen C16");
    }
    require_frozen_census(frozen_subset);
    const source::Census full =
        source::collect_census(parent);
    if (project_frozen_census(full) !=
        frozen_subset) {
        throw std::runtime_error(
            "AQ10-DBC1 projected source census drifted");
    }
    return collect_corpus_from_authenticated_source(
        parent, frozen_subset, full,
        hidden_repartition_source);
}

std::vector<LearnedWeightedCriticTrainingExample>
training_examples(const Corpus& corpus) {
    validate_corpus(corpus);
    std::vector<LearnedWeightedCriticTrainingExample>
        result;
    std::size_t eligible = 0;
    for (const RootExample& root : corpus.train) {
        eligible += root.accounting.eligible_cells;
    }
    result.reserve(eligible);
    for (const RootExample& root : corpus.train) {
        for (const BoundaryCell& cell : root.cells) {
            if (cell.terminal_before_boundary) {
                continue;
            }
            result.push_back({
                .features = cell.observation,
                .target = cell.teacher_target,
                .weight = cell.weight,
            });
        }
    }
    if (result.size() != eligible) {
        throw std::logic_error(
            "AQ10-DBC1 TRAIN projection drifted");
    }
    return result;
}

std::vector<RootPrediction> score(
    std::span<const RootExample> examples,
    std::shared_ptr<const LearnedModel> model) {
    if (!model || examples.empty()) {
        throw std::invalid_argument(
            "AQ10-DBC1 scoring requires examples and a model");
    }
    std::vector<RootPrediction> result;
    result.reserve(examples.size());
    for (const RootExample& root : examples) {
        RootPrediction prediction{
            .stable_root_id =
                root.manifest.stable_root_id,
            .action_samples =
                root.teacher_samples,
        };
        for (const BoundaryCell& cell : root.cells) {
            if (cell.terminal_before_boundary) {
                continue;
            }
            prediction.action_samples
                [cell.action_index][cell.world_index] =
                    learned_critic_observation_value(
                        cell.observation, model);
        }
        result.push_back(std::move(prediction));
    }
    return result;
}

Metrics evaluate(
    std::span<const RootExample> examples,
    std::span<const RootPrediction> predictions) {
    if (examples.empty() ||
        examples.size() != predictions.size()) {
        throw std::invalid_argument(
            "AQ10-DBC1 metric inputs are not aligned");
    }
    std::array<MetricAccumulator, kDeckCount>
        accumulators{};
    for (std::size_t root_index = 0;
         root_index < examples.size(); ++root_index) {
        const RootExample& root =
            examples[root_index];
        const RootPrediction& prediction =
            predictions[root_index];
        if (prediction.stable_root_id !=
                root.manifest.stable_root_id ||
            prediction.action_samples.size() !=
                root.teacher_samples.size()) {
            throw std::invalid_argument(
                "AQ10-DBC1 prediction root identity drifted");
        }
        const std::size_t deck =
            deck_index(
                root.manifest.coordinate.owner_deck());
        MetricAccumulator& metrics =
            accumulators[deck];
        ++metrics.roots;
        std::vector<double> teacher_action_scores;
        std::vector<double> predicted_action_scores;
        teacher_action_scores.reserve(
            root.teacher_samples.size());
        predicted_action_scores.reserve(
            root.teacher_samples.size());
        for (std::size_t action = 0;
             action < root.teacher_samples.size();
             ++action) {
            const auto& teacher =
                root.teacher_samples[action];
            const auto& predicted =
                prediction.action_samples[action];
            if (predicted.size() != teacher.size() ||
                !std::all_of(
                    predicted.begin(), predicted.end(),
                    probability)) {
                throw std::invalid_argument(
                    "AQ10-DBC1 prediction cell row is invalid");
            }
            for (std::size_t world = 0;
                 world < teacher.size(); ++world) {
                const BoundaryCell& cell =
                    root.cells[
                        action * kTeacherWorlds + world];
                if (cell.terminal_before_boundary) {
                    if (predicted[world] !=
                        teacher[world]) {
                        throw std::invalid_argument(
                            "AQ10-DBC1 terminal action score was "
                            "replaced by a critic");
                    }
                    continue;
                }
                const double target = teacher[world];
                const double value = predicted[world];
                const double clamped =
                    clamped_probability(value);
                const double error = value - target;
                metrics.weight_mass += cell.weight;
                metrics.bce_sum +=
                    cell.weight *
                    (-target * std::log(clamped) -
                     (1.0 - target) *
                         std::log(1.0 - clamped));
                metrics.brier_sum +=
                    cell.weight * error * error;
                metrics.bias_sum +=
                    cell.weight * error;
                const std::size_t bin =
                    calibration_bin(value);
                metrics.bin_weights[bin] +=
                    cell.weight;
                metrics.bin_prediction_sums[bin] +=
                    cell.weight * value;
                metrics.bin_target_sums[bin] +=
                    cell.weight * target;
                ++metrics.eligible_cells;
            }
            teacher_action_scores.push_back(
                std::accumulate(
                    teacher.begin(), teacher.end(), 0.0) /
                static_cast<double>(teacher.size()));
            predicted_action_scores.push_back(
                std::accumulate(
                    predicted.begin(), predicted.end(), 0.0) /
                static_cast<double>(predicted.size()));
        }

        const auto teacher_support =
            exact_max_support(teacher_action_scores);
        const auto predicted_support =
            exact_max_support(predicted_action_scores);
        std::size_t overlap = 0;
        double selected_teacher = 0.0;
        for (const std::size_t action :
             predicted_support) {
            selected_teacher +=
                teacher_action_scores[action];
            overlap +=
                std::find(
                    teacher_support.begin(),
                    teacher_support.end(), action) !=
                        teacher_support.end()
                    ? 1U
                    : 0U;
        }
        metrics.top_one_sum +=
            static_cast<double>(overlap) /
            static_cast<double>(
                predicted_support.size());
        selected_teacher /=
            static_cast<double>(
                predicted_support.size());
        metrics.regret_sum +=
            teacher_action_scores[
                teacher_support.front()] -
            selected_teacher;

        for (std::size_t first = 0;
             first < root.teacher_samples.size();
             ++first) {
            for (std::size_t second = first + 1;
                 second < root.teacher_samples.size();
                 ++second) {
                std::vector<double> differences;
                differences.reserve(kTeacherWorlds);
                for (std::size_t world = 0;
                     world < kTeacherWorlds; ++world) {
                    differences.push_back(
                        root.teacher_samples[first][world] -
                        root.teacher_samples[second][world]);
                }
                const double delta =
                    teacher_action_scores[first] -
                    teacher_action_scores[second];
                const double uncertainty =
                    kNormal95CriticalValue *
                    sample_standard_error(differences);
                if (std::abs(delta) <
                        kStablePairMinimumDelta ||
                    std::abs(delta) <= uncertainty) {
                    continue;
                }
                ++metrics.stable_pairs;
                const double predicted_delta =
                    predicted_action_scores[first] -
                    predicted_action_scores[second];
                if (predicted_delta == 0.0) {
                    metrics.stable_pair_sum += 0.5;
                } else if (
                    (predicted_delta > 0.0) ==
                    (delta > 0.0)) {
                    metrics.stable_pair_sum += 1.0;
                }
            }
        }
    }

    Metrics result;
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        result.decks[deck] =
            finalize_metrics(
                accumulators[deck],
                static_cast<DeckId>(deck));
        const DeckMetrics& row =
            result.decks[deck];
        result.roots += row.roots;
        result.eligible_cells +=
            row.eligible_cells;
        result.stable_pairs += row.stable_pairs;
        result.weight_mass += row.weight_mass;
        result.equal_deck_weighted_bce +=
            row.weighted_bce /
            static_cast<double>(kDeckCount);
        result.equal_deck_weighted_brier +=
            row.weighted_brier /
            static_cast<double>(kDeckCount);
        result.equal_deck_weighted_bias +=
            row.weighted_bias /
            static_cast<double>(kDeckCount);
        result.equal_deck_weighted_ece +=
            row.weighted_ece /
            static_cast<double>(kDeckCount);
        result.equal_deck_top_one_expected_agreement +=
            row.top_one_expected_agreement /
            static_cast<double>(kDeckCount);
        result.equal_deck_stable_pair_agreement +=
            row.stable_pair_agreement /
            static_cast<double>(kDeckCount);
        result.equal_deck_mean_regret +=
            row.mean_regret /
            static_cast<double>(kDeckCount);
    }
    if (std::abs(result.weight_mass - 1.0) >
        kMetricTolerance) {
        throw std::invalid_argument(
            "AQ10-DBC1 metric weight does not cross-sum");
    }
    return result;
}

FitReport fit(
    const Corpus& corpus,
    std::shared_ptr<const LearnedModel> parent) {
    validate_corpus(corpus);
    require_frozen_census(corpus.census);
    if (!parent ||
        learned_model_fingerprint(parent) !=
            corpus.census.parent_fingerprint ||
        learned_model_component_fingerprints(parent) !=
            corpus.parent_components) {
        throw std::invalid_argument(
            "AQ10-DBC1 fit parent differs from corpus");
    }
    const std::string parent_before =
        learned_model_fingerprint(parent);
    const auto parent_components =
        learned_model_component_fingerprints(parent);
    const auto parent_tensors =
        learned_critic_tensor_fingerprints(parent);
    const auto parent_parameters =
        learned_output_calibration_parameters(parent);
    const auto examples = training_examples(corpus);
    const LearnedOutputCalibrationConfig optimizer{
        .max_iterations = 32,
        .l2_tether = 0.01,
        .gradient_tolerance = 1e-10,
    };
    const auto first =
        calibrate_learned_value_output_layer(
            parent, examples, optimizer);
    const auto second =
        calibrate_learned_value_output_layer(
            parent, examples, optimizer);
    if (!first.model || !second.model) {
        throw std::runtime_error(
            "AQ10-DBC1 calibration returned no candidate");
    }
    const auto candidate_parameters =
        learned_output_calibration_parameters(
            first.model);
    const auto replay_parameters =
        learned_output_calibration_parameters(
            second.model);
    const auto parameter_replay =
        with_learned_output_calibration_parameters(
            parent, candidate_parameters);

    FitReport result{
        .candidate = first.model,
        .optimizer = first.diagnostics,
        .parent_fingerprint_before = parent_before,
        .parent_fingerprint_after =
            learned_model_fingerprint(parent),
        .candidate_fingerprint =
            learned_model_fingerprint(first.model),
        .parent_components = parent_components,
        .candidate_components =
            learned_model_component_fingerprints(
                first.model),
        .parent_critic_tensors = parent_tensors,
        .candidate_critic_tensors =
            learned_critic_tensor_fingerprints(
                first.model),
        .authorized_output_parameters =
            kOutputParameterCount,
        .changed_output_parameters =
            changed_output_parameter_count(
                parent_parameters,
                candidate_parameters),
    };
    result.parent_immutable =
        result.parent_fingerprint_before ==
        result.parent_fingerprint_after;
    result.repeated_fit_bit_identical =
        result.candidate_fingerprint ==
            learned_model_fingerprint(second.model) &&
        output_parameters_equal(
            candidate_parameters, replay_parameters) &&
        first.diagnostics == second.diagnostics;
    result.parameter_replay_bit_identical =
        parameter_replay &&
        result.candidate_fingerprint ==
            learned_model_fingerprint(
                parameter_replay) &&
        output_parameters_equal(
            candidate_parameters,
            learned_output_calibration_parameters(
                parameter_replay));
    result.only_output_layer_changed =
        result.authorized_output_parameters ==
            kOutputParameterCount &&
        result.changed_output_parameters > 0 &&
        result.changed_output_parameters <=
            kOutputParameterCount &&
        result.parent_critic_tensors.input_hidden ==
            result.candidate_critic_tensors.input_hidden &&
        result.parent_critic_tensors.direct_paths ==
            result.candidate_critic_tensors.direct_paths &&
        result.parent_critic_tensors.output_layer !=
            result.candidate_critic_tensors.output_layer &&
        result.parent_components.critic !=
            result.candidate_components.critic &&
        result.parent_components.priority ==
            result.candidate_components.priority &&
        result.parent_components.attack ==
            result.candidate_components.attack &&
        result.parent_components.block ==
            result.candidate_components.block &&
        result.parent_components.damage_order ==
            result.candidate_components.damage_order;
    return result;
}

bool OfflineGate::passed() const {
    return failures.empty();
}

OfflineGate evaluate_offline_gate(
    const Corpus& corpus,
    const FitReport& fit_report,
    const Metrics& parent_train,
    const Metrics& candidate_train,
    const Metrics& parent_dev,
    const Metrics& candidate_dev,
    bool repeated_collection_bit_identical,
    bool hidden_repartition_bit_identical) {
    std::size_t expected_fit_examples = 0;
    for (const RootExample& root : corpus.train) {
        expected_fit_examples +=
            root.accounting.eligible_cells;
    }
    const bool fit_accounting_exact =
        fit_report.optimizer.example_count ==
            expected_fit_examples &&
        fit_report.optimizer.leaf_count ==
            kLearnedOutputCalibrationLeafCount &&
        fit_report.optimizer.iterations <= 32 &&
        std::abs(
            fit_report.optimizer.total_weight - 1.0) <=
            kMetricTolerance &&
        std::isfinite(
            fit_report.optimizer.before_weighted_bce) &&
        std::isfinite(
            fit_report.optimizer.after_weighted_bce) &&
        std::isfinite(
            fit_report.optimizer.max_parameter_delta) &&
        fit_report.optimizer.max_parameter_delta > 0.0;
    OfflineGate gate{
        .repeated_collection_bit_identical =
            repeated_collection_bit_identical,
        .hidden_repartition_bit_identical =
            hidden_repartition_bit_identical,
        .source_and_subset_exact =
            corpus.census.subset_hash ==
                kFrozenSubsetHash &&
            corpus.census.source_manifest_hash ==
                kRequiredSourceManifestHash &&
            corpus.digest ==
                canonical_corpus_digest(corpus),
        .accounting_exact = true,
        .parent_immutable =
            fit_report.parent_immutable,
        .repeated_fit_bit_identical =
            fit_report.repeated_fit_bit_identical,
        .exact_output_component_isolation =
            fit_report.only_output_layer_changed &&
            fit_report.parameter_replay_bit_identical &&
            fit_accounting_exact &&
            fit_report.authorized_output_parameters ==
                kOutputParameterCount,
        .train_bce_strictly_improved =
            candidate_train.equal_deck_weighted_bce <
            parent_train.equal_deck_weighted_bce,
        .train_regret_strictly_improved =
            candidate_train.equal_deck_mean_regret <
            parent_train.equal_deck_mean_regret,
        .dev_bce_strictly_improved =
            candidate_dev.equal_deck_weighted_bce <
            parent_dev.equal_deck_weighted_bce,
        .dev_regret_strictly_improved =
            candidate_dev.equal_deck_mean_regret <
            parent_dev.equal_deck_mean_regret,
        .dev_top_one_non_decreasing =
            candidate_dev
                    .equal_deck_top_one_expected_agreement >=
            parent_dev
                .equal_deck_top_one_expected_agreement,
    };
    try {
        validate_corpus(corpus);
    } catch (const std::exception&) {
        gate.accounting_exact = false;
    }
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        gate.dev_deck_regret_guard[deck] =
            candidate_dev.decks[deck].mean_regret <=
            parent_dev.decks[deck].mean_regret +
                kMaximumDevDeckRegretIncrease;
        gate.parent_train_regret_nonzero[deck] =
            parent_train.decks[deck].mean_regret > 0.0;
        gate.parent_dev_regret_nonzero[deck] =
            parent_dev.decks[deck].mean_regret > 0.0;
    }

    add_gate(
        gate.repeated_collection_bit_identical,
        "repeated collection was not bit-identical", gate);
    add_gate(
        gate.hidden_repartition_bit_identical,
        "hidden-repartition collection changed evidence", gate);
    add_gate(
        gate.source_and_subset_exact,
        "source or frozen subset identity failed", gate);
    add_gate(
        gate.accounting_exact,
        "corpus accounting failed", gate);
    add_gate(
        gate.parent_immutable,
        "frozen parent changed", gate);
    add_gate(
        gate.repeated_fit_bit_identical,
        "repeated fit changed candidate", gate);
    add_gate(
        gate.exact_output_component_isolation,
        "candidate changed outside the 34-parameter output surface",
        gate);
    add_gate(
        gate.train_bce_strictly_improved,
        "TRAIN BCE did not strictly improve", gate);
    add_gate(
        gate.train_regret_strictly_improved,
        "TRAIN regret did not strictly improve", gate);
    add_gate(
        gate.dev_bce_strictly_improved,
        "DEV BCE did not strictly improve", gate);
    add_gate(
        gate.dev_regret_strictly_improved,
        "DEV regret did not strictly improve", gate);
    add_gate(
        gate.dev_top_one_non_decreasing,
        "DEV top-one agreement decreased", gate);
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const std::string name(
            deck_name(static_cast<DeckId>(deck)));
        add_gate(
            gate.dev_deck_regret_guard[deck],
            "DEV " + name +
                " regret exceeded the 0.01 guard",
            gate);
        add_gate(
            gate.parent_train_regret_nonzero[deck],
            "TRAIN " + name +
                " parent regret was zero",
            gate);
        add_gate(
            gate.parent_dev_regret_nonzero[deck],
            "DEV " + name +
                " parent regret was zero",
            gate);
    }
    return gate;
}

RunReport run(
    std::shared_ptr<const LearnedModel> parent) {
    if (!parent ||
        learned_model_fingerprint(parent) !=
            kRequiredParentFingerprint ||
        learned_critic_schema(parent) !=
            LearnedCriticSchema::LegacyStateOnly) {
        throw std::invalid_argument(
            "AQ10-DBC1 run requires exact frozen C16");
    }
    const source::Census full =
        source::collect_census(parent);
    const Census subset =
        project_frozen_census(full);
    const auto corpora =
        collect_run_corpora(
            parent, subset, full);
    const Corpus& direct = corpora[0];
    const Corpus& repeated_direct = corpora[1];
    const Corpus& repartitioned = corpora[2];
    const bool repeated =
        direct == repeated_direct;
    const bool hidden_invariant =
        direct.digest ==
            repartitioned.digest;
    const FitReport fitted = fit(direct, parent);
    const auto parent_train_predictions =
        score(direct.train, parent);
    const auto candidate_train_predictions =
        score(direct.train, fitted.candidate);
    const auto parent_dev_predictions =
        score(direct.dev, parent);
    const auto candidate_dev_predictions =
        score(direct.dev, fitted.candidate);
    const Metrics parent_train =
        evaluate(
            direct.train, parent_train_predictions);
    const Metrics candidate_train =
        evaluate(
            direct.train, candidate_train_predictions);
    const Metrics parent_dev =
        evaluate(
            direct.dev, parent_dev_predictions);
    const Metrics candidate_dev =
        evaluate(
            direct.dev, candidate_dev_predictions);
    return {
        .corpus = direct,
        .fit = fitted,
        .parent_train = parent_train,
        .candidate_train = candidate_train,
        .parent_dev = parent_dev,
        .candidate_dev = candidate_dev,
        .gate = evaluate_offline_gate(
            direct, fitted,
            parent_train, candidate_train,
            parent_dev, candidate_dev,
            repeated, hidden_invariant),
    };
}

void print_run(
    std::ostream& output,
    const RunReport& report) {
    validate_corpus(report.corpus);
    output << std::fixed << std::setprecision(9);
    output
        << "schema=old-school-aq10-dbc1-run-v1\n"
        << "mode=run"
        << " parent_fingerprint="
        << report.fit.parent_fingerprint_before
        << " candidate_fingerprint="
        << report.fit.candidate_fingerprint
        << " subset_hash="
        << report.corpus.census.subset_hash
        << " corpus_digest="
        << report.corpus.digest
        << " teacher_seed=" << kTeacherSeed
        << " outer=K8/R1/H8"
        << " threads=" << kTeacherThreads
        << " inner=K2/R1/H4"
        << " terminal_mode=c16-discounted-absolute-turn\n";
    output
        << "fit examples="
        << report.fit.optimizer.example_count
        << " authorized_output_parameters="
        << report.fit.authorized_output_parameters
        << " changed_output_parameters="
        << report.fit.changed_output_parameters
        << " iterations="
        << report.fit.optimizer.iterations
        << " parent_immutable="
        << report.fit.parent_immutable
        << " repeated_fit_bit_identical="
        << report.fit.repeated_fit_bit_identical
        << " parameter_replay_bit_identical="
        << report.fit.parameter_replay_bit_identical
        << " only_output_layer_changed="
        << report.fit.only_output_layer_changed
        << '\n';
    const auto print_split =
        [&](std::string_view split,
            const Metrics& parent,
            const Metrics& candidate) {
            output
                << "metrics split=" << split
                << " roots=" << parent.roots
                << " eligible_cells="
                << parent.eligible_cells
                << " stable_pairs="
                << parent.stable_pairs
                << " parent_bce="
                << parent.equal_deck_weighted_bce
                << " candidate_bce="
                << candidate.equal_deck_weighted_bce
                << " parent_brier="
                << parent.equal_deck_weighted_brier
                << " candidate_brier="
                << candidate.equal_deck_weighted_brier
                << " parent_bias="
                << parent.equal_deck_weighted_bias
                << " candidate_bias="
                << candidate.equal_deck_weighted_bias
                << " parent_ece="
                << parent.equal_deck_weighted_ece
                << " candidate_ece="
                << candidate.equal_deck_weighted_ece
                << " parent_top1="
                << parent
                       .equal_deck_top_one_expected_agreement
                << " candidate_top1="
                << candidate
                       .equal_deck_top_one_expected_agreement
                << " parent_stable_pair="
                << parent
                       .equal_deck_stable_pair_agreement
                << " candidate_stable_pair="
                << candidate
                       .equal_deck_stable_pair_agreement
                << " parent_regret="
                << parent.equal_deck_mean_regret
                << " candidate_regret="
                << candidate.equal_deck_mean_regret
                << '\n';
            for (std::size_t deck = 0;
                 deck < kDeckCount; ++deck) {
                const auto& p = parent.decks[deck];
                const auto& c = candidate.decks[deck];
                output
                    << "metrics_deck split=" << split
                    << " deck="
                    << deck_name(
                           static_cast<DeckId>(deck))
                    << " roots=" << p.roots
                    << " eligible_cells="
                    << p.eligible_cells
                    << " stable_pairs="
                    << p.stable_pairs
                    << " parent_bce="
                    << p.weighted_bce
                    << " candidate_bce="
                    << c.weighted_bce
                    << " parent_brier="
                    << p.weighted_brier
                    << " candidate_brier="
                    << c.weighted_brier
                    << " parent_bias="
                    << p.weighted_bias
                    << " candidate_bias="
                    << c.weighted_bias
                    << " parent_ece="
                    << p.weighted_ece
                    << " candidate_ece="
                    << c.weighted_ece
                    << " parent_top1="
                    << p.top_one_expected_agreement
                    << " candidate_top1="
                    << c.top_one_expected_agreement
                    << " parent_stable_pair="
                    << p.stable_pair_agreement
                    << " candidate_stable_pair="
                    << c.stable_pair_agreement
                    << " parent_regret="
                    << p.mean_regret
                    << " candidate_regret="
                    << c.mean_regret << '\n';
            }
        };
    print_split(
        "TRAIN", report.parent_train,
        report.candidate_train);
    print_split(
        "DEV", report.parent_dev,
        report.candidate_dev);
    for (const std::string& failure :
         report.gate.failures) {
        output << "failure=" << failure << '\n';
    }
    output
        << "result="
        << (report.gate.passed() ? "PASS" : "REJECT")
        << " disposition="
        << (report.gate.passed()
                ? "OFFLINE_ELIGIBLE"
                : "OFFLINE_REJECT")
        << " repeated_collection_bit_identical="
        << report.gate
               .repeated_collection_bit_identical
        << " hidden_repartition_bit_identical="
        << report.gate.hidden_repartition_bit_identical
        << " strength_claim=0 champion_replaced=0"
        << " deployment_licensed=0\n";
}

namespace testing {

std::vector<ManifestRoot> select_position_zero(
    std::span<const ManifestRoot> roots) {
    return select_position_zero_impl(roots);
}

Census make_census(
    std::array<SplitCensus, 2> splits,
    std::vector<ManifestRoot> roots) {
    Census census{
        .parent_fingerprint =
            std::string(kRequiredParentFingerprint),
        .source_manifest_hash =
            std::string(kRequiredSourceManifestHash),
        .splits = std::move(splits),
        .roots = std::move(roots),
    };
    census.subset_hash =
        canonical_subset_hash(census);
    return census;
}

Corpus make_corpus(
    Census census,
    LearnedModelComponentFingerprints parent_components,
    std::vector<RootExample> train,
    std::vector<RootExample> dev) {
    Corpus corpus{
        .census = std::move(census),
        .parent_components =
            std::move(parent_components),
        .train = std::move(train),
        .dev = std::move(dev),
    };
    corpus.digest =
        canonical_corpus_digest(corpus);
    return corpus;
}

void write_unfrozen_corpus_cache_atomic(
    const std::filesystem::path& path,
    const Corpus& corpus) {
    validate_corpus(corpus);
    write_corpus_cache_file_atomic(
        path, make_corpus_cache_file(corpus));
}

Corpus load_unfrozen_corpus_cache(
    const std::filesystem::path& path,
    const Census& expected_census,
    const LearnedModelComponentFingerprints&
        expected_parent_components) {
    validate_census(expected_census);
    Corpus corpus =
        parse_corpus_cache_file(
            read_corpus_cache_file(path));
    if (corpus.census != expected_census ||
        corpus.parent_components !=
            expected_parent_components) {
        throw std::runtime_error(
            "AQ10-DBC1 cache frozen identity drifted");
    }
    return corpus;
}

GameState hidden_repartition(
    const GameState& state, std::size_t observer) {
    return hidden_repartition_clone(state, observer);
}

} // namespace testing

} // namespace old_school::decision_boundary_critic
