#include "old_school/decision_boundary_critic.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/probes.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <ostream>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
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
           "--census\n";
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

} // namespace testing

} // namespace old_school::decision_boundary_critic
