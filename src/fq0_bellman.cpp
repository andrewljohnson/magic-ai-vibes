#include "old_school/fq0_bellman.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace old_school::fq0_bellman {
namespace {

void require_probability(double value, const char* field) {
    if (!std::isfinite(value) || value < 0.0 || value > 1.0) {
        throw std::invalid_argument(
            std::string(field) + " must be a finite probability");
    }
}

double sequential_mean(std::span<const double> values) {
    if (values.empty()) {
        throw std::invalid_argument(
            "cannot average an empty value sequence");
    }
    double total = 0.0;
    for (const double value : values) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument(
                "cannot average a non-finite value");
        }
        total += value;
        if (!std::isfinite(total)) {
            throw std::invalid_argument(
                "value sum became non-finite");
        }
    }
    const double mean =
        total / static_cast<double>(values.size());
    if (!std::isfinite(mean)) {
        throw std::invalid_argument(
            "value mean became non-finite");
    }
    return mean;
}

double sample_standard_deviation(
    std::span<const double> values, double mean) {
    if (values.size() < 2 || !std::isfinite(mean)) {
        throw std::invalid_argument(
            "sample deviation requires two finite values");
    }
    double squared_deviations = 0.0;
    for (const double value : values) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument(
                "sample deviation received a non-finite value");
        }
        const double deviation = value - mean;
        squared_deviations += deviation * deviation;
        if (!std::isfinite(squared_deviations)) {
            throw std::invalid_argument(
                "sample deviation sum became non-finite");
        }
    }
    const double variance =
        squared_deviations /
        static_cast<double>(values.size() - 1);
    const double deviation = std::sqrt(variance);
    if (!std::isfinite(deviation)) {
        throw std::invalid_argument(
            "sample deviation became non-finite");
    }
    return deviation;
}

bool same_bits(double first, double second) {
    return std::bit_cast<std::uint64_t>(first) ==
           std::bit_cast<std::uint64_t>(second);
}

void validate_target(const TargetBlocks& target) {
    require_probability(target.full, "full target");
    for (const double block : target.blocks) {
        require_probability(block, "block target");
    }
}

double block_mean(
    const std::array<double, kBlockCount>& blocks) {
    return sequential_mean(blocks);
}

double block_deviation(
    const std::array<double, kBlockCount>& blocks,
    double mean) {
    return sample_standard_deviation(blocks, mean);
}

double selected_value(
    std::span<const ActionMean> values,
    std::span<const std::string> support) {
    if (support.empty()) {
        throw std::invalid_argument(
            "cross-fit support cannot be empty");
    }
    std::vector<double> selected;
    selected.reserve(support.size());
    for (const std::string& descriptor : support) {
        const auto found = std::lower_bound(
            values.begin(), values.end(), descriptor,
            [](const ActionMean& action,
               const std::string& key) {
                return action.descriptor < key;
            });
        if (found == values.end() ||
            found->descriptor != descriptor) {
            throw std::invalid_argument(
                "cross-fit banks have different actions");
        }
        selected.push_back(found->value);
    }
    return sequential_mean(selected);
}

double paired_separation_lower(
    const TargetBlocks& first,
    const TargetBlocks& second) {
    std::array<double, kBlockCount> differences{};
    for (std::size_t index = 0; index < differences.size();
         ++index) {
        differences[index] =
            first.blocks[index] - second.blocks[index];
    }
    const double mean = block_mean(differences);
    const double deviation =
        block_deviation(differences, mean);
    return std::abs(mean) -
           kStudentT95Df7 * deviation /
               std::sqrt(static_cast<double>(kBlockCount));
}

double independent_separation_lower(
    const TargetBlocks& first,
    const TargetBlocks& second) {
    const double first_mean = block_mean(first.blocks);
    const double second_mean = block_mean(second.blocks);
    const double first_deviation =
        block_deviation(first.blocks, first_mean);
    const double second_deviation =
        block_deviation(second.blocks, second_mean);
    const double standard_error = std::sqrt(
        first_deviation * first_deviation /
            static_cast<double>(kBlockCount) +
        second_deviation * second_deviation /
            static_cast<double>(kBlockCount));
    return std::abs(first_mean - second_mean) -
           kNormal95 * standard_error;
}

} // namespace

std::vector<ActionMean> canonical_action_means(
    std::span<const ActionSamples> actions) {
    if (actions.empty()) {
        throw std::invalid_argument(
            "action sample bank cannot be empty");
    }
    const std::size_t width = actions.front().samples.size();
    if (width == 0) {
        throw std::invalid_argument(
            "action sample rows cannot be empty");
    }
    if (actions.front().sample_stream_key.empty() ||
        actions.front().world_indices.size() != width) {
        throw std::invalid_argument(
            "action sample stream identity is incomplete");
    }
    for (std::size_t index = 0; index < width; ++index) {
        if (actions.front().world_indices[index] != index) {
            throw std::invalid_argument(
                "action sample worlds are not in canonical order");
        }
    }

    std::vector<ActionMean> means;
    means.reserve(actions.size());
    for (const ActionSamples& action : actions) {
        if (action.descriptor.empty()) {
            throw std::invalid_argument(
                "action descriptor cannot be empty");
        }
        if (action.samples.size() != width ||
            action.sample_stream_key !=
                actions.front().sample_stream_key ||
            action.world_indices !=
                actions.front().world_indices) {
            throw std::invalid_argument(
                "action sample rows are not common-world aligned");
        }
        double total = 0.0;
        for (const double sample : action.samples) {
            require_probability(sample, "action sample");
            total += sample;
            if (!std::isfinite(total)) {
                throw std::invalid_argument(
                    "action sample sum became non-finite");
            }
        }
        const double mean =
            total / static_cast<double>(width);
        require_probability(mean, "action mean");
        means.push_back({
            .descriptor = action.descriptor,
            .value = mean,
        });
    }
    std::sort(
        means.begin(), means.end(),
        [](const ActionMean& first,
           const ActionMean& second) {
            return first.descriptor < second.descriptor;
        });
    for (std::size_t index = 1; index < means.size(); ++index) {
        if (means[index - 1].descriptor ==
            means[index].descriptor) {
            throw std::invalid_argument(
                "action descriptors must be unique");
        }
    }
    return means;
}

std::vector<std::string> exact_max_support(
    std::span<const ActionMean> actions) {
    if (actions.empty()) {
        throw std::invalid_argument(
            "maximum support requires an action");
    }
    std::vector<ActionMean> canonical(
        actions.begin(), actions.end());
    std::sort(
        canonical.begin(), canonical.end(),
        [](const ActionMean& first,
           const ActionMean& second) {
            return first.descriptor < second.descriptor;
        });
    for (std::size_t index = 0; index < canonical.size();
         ++index) {
        if (canonical[index].descriptor.empty()) {
            throw std::invalid_argument(
                "action descriptor cannot be empty");
        }
        require_probability(
            canonical[index].value, "action mean");
        if (index > 0 &&
            canonical[index - 1].descriptor ==
                canonical[index].descriptor) {
            throw std::invalid_argument(
                "action descriptors must be unique");
        }
    }

    double maximum = canonical.front().value;
    for (const ActionMean& action : canonical) {
        if (action.value > maximum) {
            maximum = action.value;
        }
    }
    std::vector<std::string> support;
    for (const ActionMean& action : canonical) {
        if (same_bits(action.value, maximum)) {
            support.push_back(action.descriptor);
        }
    }
    if (support.empty()) {
        throw std::logic_error(
            "finite action set produced empty support");
    }
    return support;
}

CrossFitValue cross_fit_v0(
    std::span<const ActionSamples> bank_a,
    std::span<const ActionSamples> bank_b) {
    if (bank_a.empty() || bank_b.empty() ||
        bank_a.front().samples.size() !=
            bank_b.front().samples.size()) {
        throw std::invalid_argument(
            "cross-fit banks have unequal sample widths");
    }
    if (bank_a.front().sample_stream_key.empty() ||
        bank_b.front().sample_stream_key.empty() ||
        bank_a.front().sample_stream_key ==
            bank_b.front().sample_stream_key ||
        bank_a.front().world_indices !=
            bank_b.front().world_indices) {
        throw std::invalid_argument(
            "cross-fit banks are not independent common-world streams");
    }
    CrossFitValue result;
    result.bank_a = canonical_action_means(bank_a);
    result.bank_b = canonical_action_means(bank_b);
    if (result.bank_a.size() != result.bank_b.size()) {
        throw std::invalid_argument(
            "cross-fit banks have different action counts");
    }
    for (std::size_t index = 0; index < result.bank_a.size();
         ++index) {
        if (result.bank_a[index].descriptor !=
            result.bank_b[index].descriptor) {
            throw std::invalid_argument(
                "cross-fit banks have different actions");
        }
    }
    result.support_a = exact_max_support(result.bank_a);
    result.support_b = exact_max_support(result.bank_b);
    result.a_selected_b_value =
        selected_value(result.bank_b, result.support_a);
    result.b_selected_a_value =
        selected_value(result.bank_a, result.support_b);
    result.value =
        0.5 * (result.a_selected_b_value +
               result.b_selected_a_value);
    require_probability(result.value, "cross-fit value");
    return result;
}

double root_owner_continuation_value(
    double successor_owner_value, OwnerRelation relation) {
    require_probability(
        successor_owner_value, "successor-owner value");
    double value = 0.0;
    switch (relation) {
    case OwnerRelation::SameOwner:
        value = successor_owner_value;
        break;
    case OwnerRelation::OpponentOwner:
        value = 1.0 - successor_owner_value;
        break;
    default:
        throw std::invalid_argument(
            "successor owner relation is invalid");
    }
    require_probability(value, "root-owner value");
    return value;
}

BackedTarget back_up_root_target(
    std::size_t particle_count,
    std::span<const TerminalParticle> terminals,
    std::span<const SuccessorGroup> groups) {
    if (particle_count == 0) {
        throw std::invalid_argument(
            "Bellman backup requires a particle");
    }

    std::vector<TerminalParticle> canonical_terminals(
        terminals.begin(), terminals.end());
    std::sort(
        canonical_terminals.begin(),
        canonical_terminals.end(),
        [](const TerminalParticle& first,
           const TerminalParticle& second) {
            return first.world_index < second.world_index;
        });
    double total = 0.0;
    std::vector<bool> accounted_worlds(
        particle_count, false);
    for (std::size_t index = 0;
         index < canonical_terminals.size(); ++index) {
        const TerminalParticle& terminal =
            canonical_terminals[index];
        if (terminal.world_index >= particle_count ||
            (index > 0 &&
             canonical_terminals[index - 1].world_index ==
                 terminal.world_index)) {
            throw std::invalid_argument(
                "terminal worlds must be unique and in range");
        }
        accounted_worlds[terminal.world_index] = true;
        if (terminal.root_owner_value != 0.0 &&
            terminal.root_owner_value != 0.5 &&
            terminal.root_owner_value != 1.0) {
            throw std::invalid_argument(
                "terminal value must be exactly 0, 0.5, or 1");
        }
        total += terminal.root_owner_value;
    }

    std::vector<SuccessorGroup> canonical_groups(
        groups.begin(), groups.end());
    std::sort(
        canonical_groups.begin(), canonical_groups.end(),
        [](const SuccessorGroup& first,
           const SuccessorGroup& second) {
            if (first.relation != second.relation) {
                return first.relation ==
                       OwnerRelation::SameOwner;
            }
            return first.fingerprint < second.fingerprint;
        });
    std::vector<std::string> fingerprints;
    fingerprints.reserve(canonical_groups.size());
    for (const SuccessorGroup& group : canonical_groups) {
        if (group.fingerprint.empty() || group.mass == 0 ||
            group.world_indices.size() != group.mass) {
            throw std::invalid_argument(
                "successor group identity, mass, and membership are required");
        }
        require_probability(
            group.successor_owner_value,
            "successor-group value");
        fingerprints.push_back(group.fingerprint);
        std::vector<std::size_t> members =
            group.world_indices;
        std::sort(members.begin(), members.end());
        if (std::adjacent_find(
                members.begin(), members.end()) !=
            members.end()) {
            throw std::invalid_argument(
                "successor group repeats a particle");
        }
        for (const std::size_t world : members) {
            if (world >= particle_count ||
                accounted_worlds[world]) {
                throw std::invalid_argument(
                    "Bellman particle partition overlaps or is out of range");
            }
            accounted_worlds[world] = true;
        }
    }
    std::sort(fingerprints.begin(), fingerprints.end());
    if (std::adjacent_find(
            fingerprints.begin(), fingerprints.end()) !=
        fingerprints.end()) {
        throw std::invalid_argument(
            "successor group fingerprints must be unique");
    }

    BackedTarget result;
    result.particles = particle_count;
    result.terminal_particles = canonical_terminals.size();
    std::size_t accounted = result.terminal_particles;
    for (const SuccessorGroup& group : canonical_groups) {
        if (accounted > particle_count ||
            group.mass > particle_count - accounted) {
            throw std::invalid_argument(
                "Bellman group masses exceed particle count");
        }
        accounted += group.mass;
        const double contribution =
            static_cast<double>(group.mass) *
            root_owner_continuation_value(
                group.successor_owner_value,
                group.relation);
        total += contribution;
        if (!std::isfinite(total)) {
            throw std::invalid_argument(
                "Bellman target sum became non-finite");
        }
        if (group.relation == OwnerRelation::SameOwner) {
            result.same_owner_particles += group.mass;
        } else {
            result.opponent_owner_particles += group.mass;
        }
    }
    if (accounted != particle_count) {
        throw std::invalid_argument(
            "Bellman inputs do not partition particles");
    }
    if (std::find(
            accounted_worlds.begin(), accounted_worlds.end(),
            false) != accounted_worlds.end()) {
        throw std::invalid_argument(
            "Bellman particle partition omits a world");
    }
    result.value =
        total / static_cast<double>(particle_count);
    require_probability(result.value, "backed target");
    return result;
}

BlockContrast summarize_block_contrast(
    const TargetBlocks& positive,
    const TargetBlocks& negative) {
    validate_target(positive);
    validate_target(negative);
    BlockContrast result;
    result.delta64 = positive.full - negative.full;
    for (std::size_t index = 0; index < kBlockCount;
         ++index) {
        result.block_deltas[index] =
            positive.blocks[index] -
            negative.blocks[index];
        if (result.block_deltas[index] > 0.0) {
            ++result.positive_blocks;
        }
        if (result.block_deltas[index] >= 0.0) {
            ++result.nonnegative_blocks;
        }
    }
    result.block_mean =
        block_mean(result.block_deltas);
    result.sample_standard_deviation =
        block_deviation(
            result.block_deltas, result.block_mean);
    result.lower_95 =
        result.block_mean -
        kStudentT95Df7 *
            result.sample_standard_deviation /
            std::sqrt(static_cast<double>(kBlockCount));
    if (!std::isfinite(result.delta64) ||
        !std::isfinite(result.lower_95)) {
        throw std::invalid_argument(
            "block contrast became non-finite");
    }
    return result;
}

bool passes_directional_gate(
    const BlockContrast& contrast,
    std::size_t minimum_positive_blocks) {
    if (minimum_positive_blocks > kBlockCount ||
        !std::isfinite(contrast.delta64) ||
        !std::isfinite(contrast.block_mean) ||
        !std::isfinite(
            contrast.sample_standard_deviation) ||
        !std::isfinite(contrast.lower_95) ||
        contrast.positive_blocks > kBlockCount ||
        contrast.nonnegative_blocks > kBlockCount) {
        throw std::invalid_argument(
            "directional gate received an invalid contrast");
    }
    return contrast.delta64 > 0.0 &&
           contrast.lower_95 > 0.0 &&
           contrast.positive_blocks >=
               minimum_positive_blocks;
}

FeatureCollisionAnalysis analyze_global_feature_collisions(
    std::span<const FeatureTargetRow> rows) {
    if (rows.empty()) {
        throw std::invalid_argument(
            "feature collision census cannot be empty");
    }
    std::vector<FeatureTargetRow> canonical(
        rows.begin(), rows.end());
    std::sort(
        canonical.begin(), canonical.end(),
        [](const FeatureTargetRow& first,
           const FeatureTargetRow& second) {
            return first.row_id < second.row_id;
        });
    const std::size_t feature_width =
        canonical.front().features.size();
    if (feature_width == 0) {
        throw std::invalid_argument(
            "feature collision rows cannot be empty");
    }
    std::map<std::string, std::pair<std::string, std::string>>
        information_set_streams;
    std::map<std::pair<std::string, std::string>,
             std::vector<std::size_t>>
        legal_sets;
    for (std::size_t index = 0; index < canonical.size();
         ++index) {
        const FeatureTargetRow& row = canonical[index];
        if (row.row_id.empty() ||
            row.information_set_id.empty() ||
            row.legal_set_id.empty() ||
            row.common_world_key.empty() ||
            row.action_descriptor.empty() ||
            row.canonical_consequence_fingerprint.empty()) {
            throw std::invalid_argument(
                "feature collision row identity is incomplete");
        }
        if (row.features.size() != feature_width) {
            throw std::invalid_argument(
                "feature collision rows have unequal widths");
        }
        for (const double feature : row.features) {
            if (!std::isfinite(feature)) {
                throw std::invalid_argument(
                    "feature collision row is non-finite");
            }
        }
        validate_target(row.target);
        if (index > 0 &&
            canonical[index - 1].row_id == row.row_id) {
            throw std::invalid_argument(
                "feature collision row IDs must be unique");
        }
        legal_sets[
            {row.information_set_id, row.legal_set_id}]
            .push_back(index);
        const auto [stream, inserted] =
            information_set_streams.emplace(
                row.information_set_id,
                std::pair{
                    row.legal_set_id,
                    row.common_world_key});
        if (!inserted &&
            stream->second !=
                std::pair{
                    row.legal_set_id,
                    row.common_world_key}) {
            throw std::invalid_argument(
                "one information set has inconsistent legal/common-world "
                "identity");
        }
    }
    for (const auto& [identity, members] : legal_sets) {
        static_cast<void>(identity);
        std::vector<std::string> descriptors;
        descriptors.reserve(members.size());
        double maximum =
            canonical[members.front()].target.full;
        for (const std::size_t member : members) {
            descriptors.push_back(
                canonical[member].action_descriptor);
            if (canonical[member].target.full > maximum) {
                maximum = canonical[member].target.full;
            }
        }
        std::sort(descriptors.begin(), descriptors.end());
        if (std::adjacent_find(
                descriptors.begin(), descriptors.end()) !=
            descriptors.end()) {
            throw std::invalid_argument(
                "a legal set repeats an action descriptor");
        }
        std::size_t support_size = 0;
        std::size_t support_member = 0;
        for (const std::size_t member : members) {
            if (same_bits(
                    canonical[member].target.full, maximum)) {
                ++support_size;
                support_member = member;
            }
        }
        for (const std::size_t member : members) {
            const bool expected_unique =
                support_size == 1 &&
                member == support_member;
            if (canonical[member].unique_exact_max !=
                expected_unique) {
                throw std::invalid_argument(
                    "unique exact-max flag disagrees with full target "
                    "support");
            }
        }
    }

    FeatureCollisionAnalysis result;
    result.rows = canonical.size();
    std::map<std::vector<std::uint64_t>,
             std::vector<std::size_t>>
        feature_classes;
    for (std::size_t row = 0; row < canonical.size(); ++row) {
        std::vector<std::uint64_t> bits;
        bits.reserve(canonical[row].features.size());
        for (const double feature : canonical[row].features) {
            bits.push_back(
                std::bit_cast<std::uint64_t>(feature));
        }
        feature_classes[std::move(bits)].push_back(row);
    }
    for (const auto& [bits, members] : feature_classes) {
        static_cast<void>(bits);
        if (members.size() < 2) {
            continue;
        }
        ++result.colliding_feature_classes;
        for (std::size_t first_position = 0;
             first_position < members.size();
             ++first_position) {
            for (std::size_t second_position =
                     first_position + 1;
                 second_position < members.size();
                 ++second_position) {
            const std::size_t first = members[first_position];
            const std::size_t second = members[second_position];
            const bool paired =
                canonical[first].information_set_id ==
                    canonical[second].information_set_id &&
                canonical[first].common_world_key ==
                    canonical[second].common_world_key;
            FeatureCollision collision{
                .first_row_id = canonical[first].row_id,
                .second_row_id = canonical[second].row_id,
                .target_method =
                    paired
                        ? CollisionTargetMethod::PairedStudentT
                        : CollisionTargetMethod::
                              IndependentNormal,
                .target_separation_lower_95 =
                    paired
                        ? paired_separation_lower(
                              canonical[first].target,
                              canonical[second].target)
                        : independent_separation_lower(
                              canonical[first].target,
                              canonical[second].target),
                .consequence_conflict =
                    canonical[first]
                        .canonical_consequence_fingerprint !=
                    canonical[second]
                        .canonical_consequence_fingerprint,
                .support_conflict =
                    canonical[first].information_set_id ==
                        canonical[second].information_set_id &&
                    canonical[first].legal_set_id ==
                        canonical[second].legal_set_id &&
                    canonical[first].action_descriptor !=
                        canonical[second].action_descriptor &&
                    canonical[first].unique_exact_max !=
                        canonical[second].unique_exact_max,
            };
            collision.target_conflict =
                collision.target_separation_lower_95 > 0.0 ||
                collision.support_conflict;
            collision.harmful =
                collision.consequence_conflict ||
                collision.target_conflict ||
                collision.support_conflict;
            if (collision.harmful) {
                ++result.harmful_collisions;
            }
            result.collisions.push_back(
                std::move(collision));
            }
        }
    }
    result.passed = result.harmful_collisions == 0;
    return result;
}

} // namespace old_school::fq0_bellman
