#include "old_school/fq0_bellman_audit.hpp"

#include "old_school/audit_common.hpp"
#include "old_school/fq0_information_set.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace old_school::fq0_bellman_audit {
namespace {

constexpr std::array<std::size_t, kDeckCount>
    kExpectedRootsByDeck = {6, 4, 8, 4, 4};
constexpr std::array<std::string_view, 8> kSectionNames = {
    "metadata",
    "manifest",
    "roots",
    "contrasts",
    "dominance",
    "collisions",
    "integrity",
    "invariance",
};

void require(bool condition, std::string message) {
    if (!condition) {
        throw std::runtime_error(std::move(message));
    }
}

bool bit_equal(double first, double second) {
    return audit_common::bit_identical(first, second);
}

bool probability(double value) {
    return std::isfinite(value) && value >= 0.0 &&
           value <= 1.0;
}

std::string bool_text(bool value) {
    return value ? "1" : "0";
}

std::string real_bits(double value) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::hex << std::setfill('0') << std::setw(16)
           << std::bit_cast<std::uint64_t>(value);
    return output.str();
}

bool exact_terminal_value_bits(std::uint64_t bits) {
    return bits == std::bit_cast<std::uint64_t>(0.0) ||
           bits == std::bit_cast<std::uint64_t>(0.5) ||
           bits == std::bit_cast<std::uint64_t>(1.0);
}

class DigestWriter {
  public:
    void text(std::string_view value) {
        integer(value.size());
        bytes_.append(value);
    }

    void integer(std::uint64_t value) {
        for (std::size_t byte = 0; byte < sizeof(value);
             ++byte) {
            bytes_.push_back(static_cast<char>(
                static_cast<unsigned char>(
                    value >> (byte * 8U))));
        }
    }

    void boolean(bool value) {
        bytes_.push_back(value ? '\1' : '\0');
    }

    void real(double value) {
        integer(std::bit_cast<std::uint64_t>(value));
    }

    std::string sha256() const {
        return artifact_integrity::sha256_string(bytes_);
    }

  private:
    std::string bytes_;
};

void digest_priority_action(
    DigestWriter& output, const PriorityAction& action) {
    output.integer(
        static_cast<std::uint64_t>(action.kind));
    output.integer(
        static_cast<std::uint64_t>(action.card));
    output.boolean(action.target.has_value());
    if (action.target.has_value()) {
        output.integer(action.target->player);
        output.boolean(
            action.target->creature.has_value());
        if (action.target->creature.has_value()) {
            output.integer(*action.target->creature);
        }
    }
    output.boolean(action.spell_target.has_value());
    if (action.spell_target.has_value()) {
        output.integer(*action.spell_target);
    }
    output.boolean(action.source_permanent.has_value());
    if (action.source_permanent.has_value()) {
        output.integer(*action.source_permanent);
    }
    output.integer(
        static_cast<std::uint64_t>(
            static_cast<std::int64_t>(action.x_value)));
}

void digest_leaf_sample(
    DigestWriter& output,
    const LeafSampleEvidence& sample,
    bool contextual_stream) {
    output.integer(sample.world_index);
    output.integer(sample.determinization_seed);
    output.integer(sample.macro_seed);
    output.integer(sample.score_bits);
    output.text(sample.redacted_leaf_hash);
    output.boolean(sample.terminal);
    output.boolean(sample.forced_action_applied);
    output.boolean(sample.critic_evaluated);
    output.boolean(
        sample.contextual_legacy_critic_bit_identical);
    if (sample.critic_evaluated) {
        output.integer(
            contextual_stream
                ? sample.contextual_score_bits
                : sample.legacy_score_bits);
    }
    output.integer(sample.actions_applied);
    output.integer(sample.priority_actions_applied);
    output.integer(sample.phase_transitions);
    output.integer(sample.turn_advances);
}

void digest_group_bank(
    DigestWriter& output, const GroupBankEvidence& bank,
    bool contextual_stream = true) {
    output.text(bank.bank);
    output.text(bank.stream_key);
    output.integer(bank.actions.size());
    for (const GroupActionEvidence& action : bank.actions) {
        output.text(action.descriptor);
        digest_priority_action(output, action.action);
        output.text(action.feature_row_id);
        output.integer(action.policy_features.size());
        for (const double feature :
             action.policy_features) {
            output.real(feature);
        }
        output.text(
            action.canonical_consequence_fingerprint);
        output.integer(action.samples.size());
        for (const LeafSampleEvidence& sample :
             action.samples) {
            digest_leaf_sample(
                output, sample, contextual_stream);
        }
    }
}

void digest_science_group_bank(
    DigestWriter& output,
    const GroupBankEvidence& bank) {
    output.text(bank.bank);
    output.text(bank.stream_key);
    output.integer(bank.actions.size());
    for (const GroupActionEvidence& action : bank.actions) {
        output.text(action.descriptor);
        output.text(
            probes::stable_priority_action_descriptor(
                action.action));
        output.text(action.feature_row_id);
        output.integer(action.policy_features.size());
        for (const double feature :
             action.policy_features) {
            output.real(feature);
        }
        output.text(
            action.canonical_consequence_fingerprint);
        output.integer(action.samples.size());
        for (const LeafSampleEvidence& sample :
             action.samples) {
            output.integer(sample.world_index);
            output.integer(sample.determinization_seed);
            output.integer(sample.macro_seed);
            output.integer(sample.score_bits);
            output.integer(sample.contextual_score_bits);
            output.integer(sample.legacy_score_bits);
            output.text(sample.redacted_leaf_hash);
            output.boolean(sample.terminal);
            output.boolean(sample.critic_evaluated);
            output.boolean(
                sample
                    .contextual_legacy_critic_bit_identical);
            output.integer(sample.actions_applied);
            output.integer(
                sample.priority_actions_applied);
            output.integer(sample.phase_transitions);
            output.integer(sample.turn_advances);
            output.boolean(sample.forced_action_applied);
        }
    }
}

void digest_operator_group_bank(
    DigestWriter& output,
    const GroupBankEvidence& bank) {
    output.text(bank.bank);
    output.text(bank.stream_key);
    output.integer(bank.actions.size());
    for (const GroupActionEvidence& action : bank.actions) {
        output.text(action.descriptor);
        output.text(
            probes::stable_priority_action_descriptor(
                action.action));
        output.text(action.feature_row_id);
        output.integer(action.policy_features.size());
        for (const double feature :
             action.policy_features) {
            output.real(feature);
        }
        output.integer(action.samples.size());
        for (const LeafSampleEvidence& sample :
             action.samples) {
            output.integer(sample.world_index);
            output.integer(sample.determinization_seed);
            output.integer(sample.macro_seed);
            output.integer(sample.score_bits);
            output.integer(sample.contextual_score_bits);
            output.integer(sample.legacy_score_bits);
            output.text(sample.redacted_leaf_hash);
            output.boolean(sample.terminal);
            output.boolean(sample.critic_evaluated);
            output.boolean(
                sample
                    .contextual_legacy_critic_bit_identical);
            output.integer(sample.actions_applied);
            output.integer(
                sample.priority_actions_applied);
            output.integer(sample.phase_transitions);
            output.integer(sample.turn_advances);
            output.boolean(sample.forced_action_applied);
        }
    }
}

void digest_cross_fit(
    DigestWriter& output,
    const fq0_bellman::CrossFitValue& value) {
    const auto append_means =
        [&](const std::vector<fq0_bellman::ActionMean>&
                means) {
            output.integer(means.size());
            for (const fq0_bellman::ActionMean& mean :
                 means) {
                output.text(mean.descriptor);
                output.real(mean.value);
            }
        };
    const auto append_support =
        [&](const std::vector<std::string>& support) {
            output.integer(support.size());
            for (const std::string& descriptor :
                 support) {
                output.text(descriptor);
            }
        };
    append_means(value.bank_a);
    append_means(value.bank_b);
    append_support(value.support_a);
    append_support(value.support_b);
    output.real(value.a_selected_b_value);
    output.real(value.b_selected_a_value);
    output.real(value.value);
}

void digest_canonical_cost(
    DigestWriter& output,
    const fq0_dominance::CanonicalPlayerResourceCost&
        cost) {
    for (const std::size_t count :
         cost.hand_cards_consumed) {
        output.integer(count);
    }
    output.integer(
        static_cast<std::uint64_t>(
            static_cast<std::int64_t>(
                cost.mana_depleted.generic)));
    output.integer(
        static_cast<std::uint64_t>(
            static_cast<std::int64_t>(
                cost.mana_depleted.green)));
    output.integer(
        static_cast<std::uint64_t>(
            static_cast<std::int64_t>(
                cost.mana_depleted.red)));
    output.integer(
        static_cast<std::uint64_t>(
            static_cast<std::int64_t>(
                cost.mana_depleted.blue)));
    output.integer(
        static_cast<std::uint64_t>(
            static_cast<std::int64_t>(
                cost.mana_depleted.white)));
    for (const std::size_t count :
         cost.lands_newly_tapped) {
        output.integer(count);
    }
    for (const std::size_t count :
         cost.artifacts_newly_tapped) {
        output.integer(count);
    }
    output.boolean(
        cost.land_play_entitlement_consumed);
}

void digest_comparison(
    DigestWriter& output,
    const fq0_dominance::Comparison& comparison) {
    output.integer(
        static_cast<std::uint64_t>(
            comparison.orientation));
    output.boolean(comparison.root_information_equal);
    output.boolean(comparison.first_normalized);
    output.boolean(comparison.second_normalized);
    output.boolean(comparison.consequences_equal);
    const auto append_settlement =
        [&](const fq0_dominance::CanonicalSettlement&
                settlement) {
            output.boolean(settlement.valid);
            output.text(
                settlement.owner_observable_consequence);
            for (const auto& cost : settlement.costs) {
                digest_canonical_cost(output, cost);
            }
        };
    append_settlement(comparison.first);
    append_settlement(comparison.second);
}

void append_row(
    std::string& output,
    std::initializer_list<std::string> fields) {
    bool first = true;
    for (const std::string& field : fields) {
        require(
            field.find_first_of("\t\n\r\0", 0, 4) ==
                std::string::npos,
            "FQ0 TSV field contains a control character");
        if (!first) {
            output.push_back('\t');
        }
        first = false;
        output += field;
    }
    output.push_back('\n');
}

std::string optional_size(
    const std::optional<std::uint64_t>& value) {
    return value.has_value() ? std::to_string(*value) : "-";
}

std::string optional_permanent(
    const std::optional<PermanentId>& value) {
    return value.has_value() ? std::to_string(*value) : "-";
}

std::string optional_stack(
    const std::optional<StackObjectId>& value) {
    return value.has_value() ? std::to_string(*value) : "-";
}

void append_action_fields(
    std::string& output, std::string_view row_kind,
    std::string_view coordinate,
    std::string_view descriptor,
    const PriorityAction& action) {
    std::optional<std::uint64_t> target_player;
    std::optional<std::uint64_t> target_creature;
    if (action.target.has_value()) {
        target_player = action.target->player;
        if (action.target->creature.has_value()) {
            target_creature = *action.target->creature;
        }
    }
    append_row(
        output,
        {std::string(row_kind), std::string(coordinate),
         std::string(descriptor),
         std::to_string(
             static_cast<std::size_t>(action.kind)),
         std::to_string(
             static_cast<std::size_t>(action.card)),
         optional_size(target_player),
         optional_size(target_creature),
         optional_stack(action.spell_target),
         optional_permanent(action.source_permanent),
         std::to_string(action.x_value)});
}

void append_identity_witness(
    std::string& output, std::string_view kind,
    std::string_view coordinate,
    const BitIdentityEvidence& witness) {
    append_row(
        output,
        {"identity_witness", std::string(kind),
         std::string(coordinate),
         witness.domain, witness.coordinate,
         witness.baseline_sha256,
         witness.comparison_sha256});
}

bool strictly_sorted_unique(
    const std::vector<std::string>& values) {
    return std::adjacent_find(
               values.begin(), values.end(),
               [](const std::string& first,
                  const std::string& second) {
                   return first >= second;
               }) == values.end();
}

bool cross_fit_equal(
    const fq0_bellman::CrossFitValue& first,
    const fq0_bellman::CrossFitValue& second) {
    if (first.bank_a.size() != second.bank_a.size() ||
        first.bank_b.size() != second.bank_b.size() ||
        first.support_a != second.support_a ||
        first.support_b != second.support_b ||
        !bit_equal(
            first.a_selected_b_value,
            second.a_selected_b_value) ||
        !bit_equal(
            first.b_selected_a_value,
            second.b_selected_a_value) ||
        !bit_equal(first.value, second.value)) {
        return false;
    }
    const auto means_equal =
        [](const std::vector<fq0_bellman::ActionMean>& left,
           const std::vector<fq0_bellman::ActionMean>& right) {
            if (left.size() != right.size()) {
                return false;
            }
            for (std::size_t index = 0;
                 index < left.size(); ++index) {
                if (left[index].descriptor !=
                        right[index].descriptor ||
                    !bit_equal(
                        left[index].value,
                        right[index].value)) {
                    return false;
                }
            }
            return true;
        };
    return means_equal(first.bank_a, second.bank_a) &&
           means_equal(first.bank_b, second.bank_b);
}

bool backed_target_equal(
    const fq0_bellman::BackedTarget& first,
    const fq0_bellman::BackedTarget& second) {
    return bit_equal(first.value, second.value) &&
           first.particles == second.particles &&
           first.terminal_particles ==
               second.terminal_particles &&
           first.same_owner_particles ==
               second.same_owner_particles &&
           first.opponent_owner_particles ==
               second.opponent_owner_particles;
}

bool contrast_equal(
    const fq0_bellman::BlockContrast& first,
    const fq0_bellman::BlockContrast& second) {
    if (!bit_equal(first.delta64, second.delta64) ||
        !bit_equal(first.block_mean, second.block_mean) ||
        !bit_equal(
            first.sample_standard_deviation,
            second.sample_standard_deviation) ||
        !bit_equal(first.lower_95, second.lower_95) ||
        first.positive_blocks != second.positive_blocks ||
        first.nonnegative_blocks !=
            second.nonnegative_blocks) {
        return false;
    }
    for (std::size_t block = 0; block < kBlocks;
         ++block) {
        if (!bit_equal(
                first.block_deltas[block],
                second.block_deltas[block])) {
            return false;
        }
    }
    return true;
}

bool target_equal(
    const fq0_bellman::TargetBlocks& first,
    const fq0_bellman::TargetBlocks& second) {
    if (!bit_equal(first.full, second.full)) {
        return false;
    }
    for (std::size_t block = 0; block < kBlocks;
         ++block) {
        if (!bit_equal(
                first.blocks[block],
                second.blocks[block])) {
            return false;
        }
    }
    return true;
}

bool collision_analysis_equal(
    const fq0_bellman::FeatureCollisionAnalysis& first,
    const fq0_bellman::FeatureCollisionAnalysis& second) {
    if (first.rows != second.rows ||
        first.colliding_feature_classes !=
            second.colliding_feature_classes ||
        first.collisions.size() !=
            second.collisions.size() ||
        first.harmful_collisions !=
            second.harmful_collisions ||
        first.passed != second.passed) {
        return false;
    }
    for (std::size_t index = 0;
         index < first.collisions.size(); ++index) {
        const auto& left = first.collisions[index];
        const auto& right = second.collisions[index];
        if (left.first_row_id != right.first_row_id ||
            left.second_row_id != right.second_row_id ||
            left.target_method != right.target_method ||
            !bit_equal(
                left.target_separation_lower_95,
                right.target_separation_lower_95) ||
            left.consequence_conflict !=
                right.consequence_conflict ||
            left.target_conflict !=
                right.target_conflict ||
            left.support_conflict !=
                right.support_conflict ||
            left.harmful != right.harmful) {
            return false;
        }
    }
    return true;
}

std::string manifest_payload_sha256_impl(
    const ac1_teacher_audit::Manifest& manifest) {
    DigestWriter output;
    output.text("fq0-manifest-payload-v1");
    output.integer(manifest.roots.size());
    for (const ac1_teacher_audit::ManifestRoot& root :
         manifest.roots) {
        output.text(root.probe.stable_id);
        output.integer(
            static_cast<std::uint64_t>(
                root.probe.category));
        output.integer(
            static_cast<std::uint64_t>(
                root.probe.decision_kind));
        output.integer(
            static_cast<std::uint64_t>(
                root.probe.root_deck));
        output.integer(
            static_cast<std::uint64_t>(
                root.probe.opponent_deck));
        output.integer(root.probe.root_player);
        output.integer(
            static_cast<std::uint64_t>(
                root.probe.phase));
        output.integer(
            static_cast<std::uint64_t>(
                static_cast<std::int64_t>(
                    root.probe.consecutive_passes)));
        output.text(root.information_action_fingerprint);
        output.text(root.factory_contract_fingerprint);
        output.boolean(root.from_dev_v3);
        output.integer(root.probe.candidates.size());
        for (const probes::Candidate& candidate :
             root.probe.candidates) {
            output.text(candidate.descriptor);
            const auto* priority =
                std::get_if<PriorityAction>(
                    &candidate.action);
            output.boolean(priority != nullptr);
            if (priority != nullptr) {
                digest_priority_action(
                    output, *priority);
            }
        }
    }
    output.text(manifest.dev_force_spike_alias);
    output.text(manifest.canonical_live_force_spike);
    output.text(manifest.attack_stable_id);
    output.text(
        manifest.attack_information_action_fingerprint);
    for (const std::size_t count :
         manifest.physical_roots_by_deck) {
        output.integer(count);
    }
    for (const std::size_t count :
         manifest.logical_dev_roots_by_deck) {
        output.integer(count);
    }
    output.integer(manifest.logical_priority_ids);
    output.boolean(manifest.exact);
    return output.sha256();
}

void digest_root_transition(
    DigestWriter& output,
    const RootTransitionParticleEvidence& transition) {
    output.integer(transition.world_index);
    output.integer(transition.determinization_seed);
    output.integer(transition.macro_seed);
    output.text(transition.redacted_result_hash);
    output.boolean(transition.terminal);
    output.integer(
        transition.terminal_root_owner_value_bits);
    output.text(
        transition
            .successor_information_set_fingerprint);
    output.integer(transition.successor_owner);
    output.boolean(transition.forced_root_action_applied);
    output.boolean(transition.successful_disposition);
    output.integer(transition.actions_applied);
    output.integer(
        transition.priority_actions_applied);
    output.integer(transition.phase_transitions);
    output.integer(transition.turn_advances);
}

void digest_scope_payload(
    DigestWriter& output, const ScopeEvidence& scope) {
    output.integer(
        static_cast<std::uint64_t>(scope.kind));
    output.integer(scope.block);
    output.integer(scope.root_world_indices.size());
    for (const std::size_t world :
         scope.root_world_indices) {
        output.integer(world);
    }
    output.integer(scope.terminals.size());
    for (const fq0_bellman::TerminalParticle& terminal :
         scope.terminals) {
        output.integer(terminal.world_index);
        output.real(terminal.root_owner_value);
    }
    output.integer(scope.groups.size());
    for (const SuccessorGroupEvidence& group :
         scope.groups) {
        output.text(group.information_set_fingerprint);
        output.integer(group.successor_owner);
        output.integer(
            static_cast<std::uint64_t>(
                group.relation));
        output.integer(group.root_world_indices.size());
        for (const std::size_t world :
             group.root_world_indices) {
            output.integer(world);
        }
        output.integer(group.representative_root_world);
        output.text(
            group
                .representative_root_action_descriptor);
        digest_group_bank(output, group.bank_a);
        digest_group_bank(output, group.bank_b);
        digest_cross_fit(output, group.cross_fit);
    }
    output.real(scope.target.value);
    output.integer(scope.target.particles);
    output.integer(scope.target.terminal_particles);
    output.integer(scope.target.same_owner_particles);
    output.integer(scope.target.opponent_owner_particles);
    output.boolean(scope.exact_particle_partition);
}

std::string root_payload_sha256_impl(
    const RootEvidence& root) {
    DigestWriter output;
    output.text("fq0-root-payload-v1");
    output.text(root.stable_id);
    output.text(
        root.manifest_information_action_fingerprint);
    output.integer(
        static_cast<std::uint64_t>(root.root_deck));
    output.integer(root.root_player);
    output.integer(root.actions.size());
    for (const RootActionEvidence& action :
         root.actions) {
        output.text(action.descriptor);
        digest_priority_action(output, action.action);
        output.text(action.feature_row_id);
        output.real(action.target.full);
        for (const double block : action.target.blocks) {
            output.real(block);
        }
        output.integer(action.policy_features.size());
        for (const double feature :
             action.policy_features) {
            output.real(feature);
        }
        output.text(
            action.canonical_consequence_fingerprint);
        output.integer(action.root_transitions.size());
        for (const auto& transition :
             action.root_transitions) {
            digest_root_transition(
                output, transition);
        }
        output.integer(action.scopes.size());
        for (const ScopeEvidence& scope :
             action.scopes) {
            digest_scope_payload(output, scope);
        }
    }
    output.integer(root.exact_support.size());
    for (const std::string& descriptor :
         root.exact_support) {
        output.text(descriptor);
    }
    return output.sha256();
}

std::string group_bank_pair_payload_sha256_impl(
    std::string_view root_stable_id,
    std::string_view root_action_descriptor,
    ScopeKind kind, std::size_t block,
    const SuccessorGroupEvidence& group) {
    DigestWriter output;
    output.text("fq0-group-bank-pair-payload-v1");
    output.text(root_stable_id);
    output.text(root_action_descriptor);
    output.integer(static_cast<std::uint64_t>(kind));
    output.integer(block);
    output.text(group.information_set_fingerprint);
    output.integer(group.successor_owner);
    output.integer(
        static_cast<std::uint64_t>(group.relation));
    output.integer(group.root_world_indices.size());
    for (const std::size_t world :
         group.root_world_indices) {
        output.integer(world);
    }
    output.integer(group.representative_root_world);
    output.text(
        group.representative_root_action_descriptor);
    output.boolean(group.hidden_repartition_eligible);
    output.boolean(group.hidden_identity_changed);
    output.boolean(
        group.every_representative_reconstructs);
    output.boolean(group.hidden_repartition_invariant);
    output.boolean(group.complete);
    digest_group_bank(output, group.bank_a);
    digest_group_bank(output, group.bank_b);
    digest_cross_fit(output, group.cross_fit);
    return output.sha256();
}

std::string successor_bank_pair_payload_sha256_impl(
    const GroupBankEvidence& bank_a,
    const GroupBankEvidence& bank_b,
    const fq0_bellman::CrossFitValue& cross_fit) {
    DigestWriter output;
    output.text(
        "old-school-fq0-successor-bank-pair-v1");
    digest_science_group_bank(output, bank_a);
    digest_science_group_bank(output, bank_b);
    digest_cross_fit(output, cross_fit);
    return output.sha256();
}

std::string successor_operator_bank_pair_payload_sha256_impl(
    const GroupBankEvidence& bank_a,
    const GroupBankEvidence& bank_b,
    const fq0_bellman::CrossFitValue& cross_fit) {
    DigestWriter output;
    output.text(
        "old-school-fq0-successor-operator-bank-pair-v2");
    digest_operator_group_bank(output, bank_a);
    digest_operator_group_bank(output, bank_b);
    digest_cross_fit(output, cross_fit);
    return output.sha256();
}

std::string group_representative_payload_sha256_impl(
    std::string_view root_stable_id,
    std::string_view root_action_descriptor,
    ScopeKind kind, std::size_t block,
    const SuccessorGroupEvidence& group,
    std::size_t member_root_world) {
    DigestWriter output;
    output.text(
        "fq0-group-representative-payload-v1");
    output.text(group_bank_pair_payload_sha256_impl(
        root_stable_id, root_action_descriptor, kind,
        block, group));
    output.integer(member_root_world);
    return output.sha256();
}

std::string
successor_feature_scope_payload_sha256_impl(
    const SuccessorFeatureEvaluationEvidence& evaluation,
    const SuccessorFeatureScopeEvidence& scope) {
    DigestWriter output;
    output.text(
        "fq0-successor-feature-scope-payload-v1");
    output.text(evaluation.root_stable_id);
    output.text(evaluation.information_set_fingerprint);
    output.integer(evaluation.successor_owner);
    output.integer(evaluation.representative_root_world);
    output.text(
        evaluation
            .representative_root_action_descriptor);
    output.integer(static_cast<std::uint64_t>(scope.kind));
    output.integer(scope.block);
    output.integer(scope.representative_catalog.size());
    for (const auto& representative :
         scope.representative_catalog) {
        output.integer(representative.root_world);
        output.text(
            representative.root_action_descriptor);
    }
    output.boolean(scope.hidden_repartition_eligible);
    output.boolean(scope.hidden_identity_changed);
    output.boolean(
        scope.every_representative_reconstructs);
    output.boolean(scope.hidden_repartition_invariant);
    output.boolean(scope.complete);
    digest_group_bank(output, scope.bank_a);
    digest_group_bank(output, scope.bank_b);
    return output.sha256();
}

std::string
successor_feature_scope_representative_payload_sha256_impl(
    const SuccessorFeatureEvaluationEvidence& evaluation,
    const SuccessorFeatureScopeEvidence& scope,
    const SuccessorRepresentativeCoordinateEvidence&
        representative) {
    DigestWriter output;
    output.text(
        "fq0-successor-feature-scope-representative-"
        "payload-v1");
    output.text(
        successor_feature_scope_payload_sha256_impl(
            evaluation, scope));
    output.integer(representative.root_world);
    output.text(
        representative.root_action_descriptor);
    return output.sha256();
}

std::string dominance_payload_sha256_impl(
    std::string_view root_stable_id,
    std::string_view first_descriptor,
    std::string_view second_descriptor,
    const DominanceWorldEvidence& world) {
    DigestWriter output;
    output.text("fq0-dominance-comparison-payload-v1");
    output.text(root_stable_id);
    output.text(first_descriptor);
    output.text(second_descriptor);
    output.integer(world.world_index);
    output.integer(world.determinization_seed);
    output.text(world.common_world_key);
    digest_comparison(output, world.comparison);
    output.integer(
        static_cast<std::uint64_t>(world.orientation));
    return output.sha256();
}

void digest_feature_target_row(
    DigestWriter& output,
    const fq0_bellman::FeatureTargetRow& row) {
    output.text(row.row_id);
    output.text(row.information_set_id);
    output.text(row.legal_set_id);
    output.text(row.common_world_key);
    output.text(row.action_descriptor);
    output.integer(row.features.size());
    for (const double feature : row.features) {
        output.real(feature);
    }
    output.text(
        row.canonical_consequence_fingerprint);
    output.real(row.target.full);
    for (const double block : row.target.blocks) {
        output.real(block);
    }
    output.boolean(row.unique_exact_max);
}

void digest_collision_analysis(
    DigestWriter& output,
    const fq0_bellman::FeatureCollisionAnalysis&
        analysis) {
    output.integer(analysis.rows);
    output.integer(analysis.colliding_feature_classes);
    output.integer(analysis.collisions.size());
    for (const fq0_bellman::FeatureCollision& collision :
         analysis.collisions) {
        output.text(collision.first_row_id);
        output.text(collision.second_row_id);
        output.integer(
            static_cast<std::uint64_t>(
                collision.target_method));
        output.real(
            collision.target_separation_lower_95);
        output.boolean(collision.consequence_conflict);
        output.boolean(collision.target_conflict);
        output.boolean(collision.support_conflict);
        output.boolean(collision.harmful);
    }
    output.integer(analysis.harmful_collisions);
    output.boolean(analysis.passed);
}

std::string scientific_core_sha256_impl(
    const ScientificEvidence& scientific) {
    DigestWriter output;
    output.text("fq0-scientific-core-payload-v1");
    output.text(
        manifest_payload_sha256_impl(
            scientific.manifest));
    output.text(scientific.model_fingerprint);
    output.integer(scientific.roots.size());
    for (const RootEvidence& root : scientific.roots) {
        output.text(root_payload_sha256_impl(root));
    }
    output.integer(
        scientific.successor_feature_evaluations.size());
    for (const SuccessorFeatureEvaluationEvidence&
             evaluation :
         scientific.successor_feature_evaluations) {
        output.text(evaluation.root_stable_id);
        output.text(
            evaluation.information_set_fingerprint);
        output.integer(evaluation.successor_owner);
        output.integer(
            evaluation.representative_root_world);
        output.text(
            evaluation
                .representative_root_action_descriptor);
        output.integer(evaluation.scopes.size());
        for (const SuccessorFeatureScopeEvidence& scope :
             evaluation.scopes) {
            output.integer(
                static_cast<std::uint64_t>(scope.kind));
            output.integer(scope.block);
            digest_group_bank(output, scope.bank_a);
            digest_group_bank(output, scope.bank_b);
        }
    }
    output.integer(scientific.feature_rows.size());
    for (const fq0_bellman::FeatureTargetRow& row :
         scientific.feature_rows) {
        digest_feature_target_row(output, row);
    }
    digest_collision_analysis(
        output, scientific.feature_collisions);
    for (const std::size_t count :
         scientific.roots_by_deck) {
        output.integer(count);
    }
    return output.sha256();
}

std::array<std::string, 2>
critic_stream_sha256_impl(
    const ScientificEvidence& scientific) {
    std::array<DigestWriter, 2> streams;
    for (DigestWriter& stream : streams) {
        stream.text("fq0-critic-paired-stream-v1");
    }
    const auto append_bank =
        [&](std::string_view coordinate,
            const GroupBankEvidence& bank) {
            for (std::size_t stream = 0;
                 stream < streams.size(); ++stream) {
                streams[stream].text(coordinate);
                digest_group_bank(
                    streams[stream], bank, stream == 0);
            }
        };
    for (const RootEvidence& root : scientific.roots) {
        for (const RootActionEvidence& action :
             root.actions) {
            for (std::size_t scope_index = 0;
                 scope_index < action.scopes.size();
                 ++scope_index) {
                const ScopeEvidence& scope =
                    action.scopes[scope_index];
                for (std::size_t group_index = 0;
                     group_index < scope.groups.size();
                     ++group_index) {
                    const SuccessorGroupEvidence& group =
                        scope.groups[group_index];
                    const std::string coordinate =
                        root.stable_id + "/" +
                        action.descriptor + "/scope/" +
                        std::to_string(scope_index) +
                        "/group/" +
                        std::to_string(group_index);
                    append_bank(
                        coordinate + "/A", group.bank_a);
                    append_bank(
                        coordinate + "/B", group.bank_b);
                }
            }
        }
    }
    for (std::size_t evaluation_index = 0;
         evaluation_index <
         scientific.successor_feature_evaluations.size();
         ++evaluation_index) {
        const SuccessorFeatureEvaluationEvidence& evaluation =
            scientific.successor_feature_evaluations
                [evaluation_index];
        for (std::size_t scope_index = 0;
             scope_index < evaluation.scopes.size();
             ++scope_index) {
            const auto& scope =
                evaluation.scopes[scope_index];
            const std::string coordinate =
                "feature/" + evaluation.root_stable_id + "/" +
                evaluation
                    .information_set_fingerprint +
                "/scope/" + std::to_string(scope_index);
            append_bank(coordinate + "/A", scope.bank_a);
            append_bank(coordinate + "/B", scope.bank_b);
        }
    }
    return {streams[0].sha256(), streams[1].sha256()};
}

bool components_valid(
    const LearnedModelComponentFingerprints& components) {
    return audit_common::is_lower_hex_digest(
               components.critic) &&
           audit_common::is_lower_hex_digest(
               components.priority) &&
           audit_common::is_lower_hex_digest(
               components.attack) &&
           audit_common::is_lower_hex_digest(
               components.block) &&
           audit_common::is_lower_hex_digest(
               components.damage_order);
}

bool identity_witness_passed(
    const BitIdentityEvidence& witness) {
    return !witness.domain.empty() &&
           !witness.coordinate.empty() &&
           audit_common::is_lower_hex_digest(
               witness.baseline_sha256) &&
           audit_common::is_lower_hex_digest(
               witness.comparison_sha256) &&
           witness.baseline_sha256 ==
               witness.comparison_sha256;
}

void validate_bound_witness(
    const BitIdentityEvidence& witness,
    std::string_view expected_domain,
    std::string_view expected_coordinate,
    std::string_view rederived_baseline,
    std::optional<std::string_view>
        rederived_comparison = std::nullopt) {
    require(
        witness.domain == expected_domain &&
            witness.coordinate == expected_coordinate &&
            witness.baseline_sha256 ==
                rederived_baseline &&
            (!rederived_comparison.has_value() ||
             witness.comparison_sha256 ==
                 *rederived_comparison) &&
            identity_witness_passed(witness),
        std::string(expected_coordinate) +
            ": identity witness is unbound or changed");
}

struct EvidenceCensus {
    struct FeatureExpectation {
        std::string information_set;
        std::string descriptor;
        std::string legal_set;
        std::string common_world_key;
        std::vector<double> features;
        std::string consequence_fingerprint;
        std::optional<double> full;
        std::array<std::optional<double>, kBlocks> blocks;
    };

    std::map<std::string, GroupBankEvidence>
        successor_streams;
    std::map<std::uint64_t, std::string> seed_owners;
    std::set<std::pair<std::string, std::string>>
        successor_information_sets;
    struct SuccessorOccurrence {
        std::optional<std::size_t> owner;
        std::optional<std::pair<std::size_t, std::string>>
            minimum_representative;
        std::set<std::pair<std::size_t, std::string>>
            representatives;
    };
    std::map<std::pair<std::string, std::string>,
             SuccessorOccurrence>
        successor_occurrences;
    std::map<std::string, FeatureExpectation>
        feature_links;
    std::map<std::pair<std::string, std::string>,
             std::string>
        feature_identities;
};

std::string scoped_successor_information_set(
    std::string_view root_stable_id,
    std::string_view successor_fingerprint) {
    return "successor/" + std::string(root_stable_id) + "/" +
           std::string(successor_fingerprint);
}

std::string scoped_root_information_set(
    std::string_view root_stable_id,
    std::string_view manifest_fingerprint) {
    return "root/" + std::string(root_stable_id) + "/" +
           std::string(manifest_fingerprint);
}

std::string expected_legal_set(
    std::string_view information_set) {
    return "legal/" + std::string(information_set);
}

std::string expected_common_world_key(
    std::string_view root_stable_id,
    std::string_view information_set, bool root) {
    return root
               ? "worlds/root/" +
                     std::string(root_stable_id)
               : "worlds/successor/" +
                     std::string(information_set);
}

std::string expected_stream_key(
    std::string_view root_stable_id,
    std::string_view successor_fingerprint,
    fq0_information_set::SeedBank bank, ScopeKind kind,
    std::size_t block) {
    const std::string bank_name =
        bank == fq0_information_set::SeedBank::A ? "A" : "B";
    const std::string scope =
        kind == ScopeKind::FullK64
            ? "full-k64"
            : "block-k8-" + std::to_string(block);
    return "fq0-stream/" + std::string(root_stable_id) + "/" +
           std::string(successor_fingerprint) + "/" +
           bank_name + "/" + scope;
}

std::uint64_t expected_seed(
    std::uint64_t base,
    fq0_information_set::SeedDomain domain,
    std::string_view scope, std::string_view group,
    fq0_information_set::SeedBank bank,
    std::size_t block, std::size_t world) {
    return fq0_information_set::derive_indexed_seed(
        base,
        {
            .domain = domain,
            .scope = std::string(scope),
            .group = std::string(group),
            .bank = bank,
            .block = block,
            .world = world,
        });
}

std::string seed_coordinate_key(
    std::uint64_t base,
    fq0_information_set::SeedDomain domain,
    std::string_view scope, std::string_view group,
    fq0_information_set::SeedBank bank,
    std::size_t block, std::size_t world) {
    return "base=" + std::to_string(base) + "/domain=" +
           std::to_string(
               static_cast<std::size_t>(domain)) +
           "/scope=" + std::string(scope) + "/group=" +
           std::string(group) + "/bank=" +
           std::to_string(static_cast<std::size_t>(bank)) +
           "/block=" + std::to_string(block) + "/world=" +
           std::to_string(world);
}

void claim_seed(
    std::map<std::uint64_t, std::string>& owners,
    std::uint64_t seed, std::string coordinate) {
    require(
        !coordinate.empty(),
        "FQ0 seed coordinate is empty");
    const auto [found, inserted] =
        owners.emplace(seed, coordinate);
    require(
        inserted || found->second == coordinate,
        "FQ0 numeric seed collision crosses semantic "
        "coordinates");
}

void claim_indexed_seed(
    EvidenceCensus& census, std::uint64_t observed_seed,
    std::uint64_t base,
    fq0_information_set::SeedDomain domain,
    std::string_view scope, std::string_view group,
    fq0_information_set::SeedBank bank,
    std::size_t block, std::size_t world) {
    const std::uint64_t derived =
        expected_seed(
            base, domain, scope, group, bank, block,
            world);
    require(
        observed_seed == derived,
        "FQ0 seed does not match its exact indexed "
        "coordinate");
    claim_seed(
        census.seed_owners, observed_seed,
        seed_coordinate_key(
            base, domain, scope, group, bank, block,
            world));
}

EvidenceCensus::FeatureExpectation& add_feature_link(
    EvidenceCensus& census, std::string_view row_id,
    std::string_view information_set,
    std::string_view descriptor, std::string_view legal_set,
    std::string_view common_world_key,
    std::string_view coordinate) {
    require(
        !row_id.empty() && !information_set.empty() &&
            !descriptor.empty() && !legal_set.empty() &&
            !common_world_key.empty(),
        std::string(coordinate) +
            ": feature-row link is incomplete");
    const auto [found, inserted] =
        census.feature_links.emplace(
            std::string(row_id),
            EvidenceCensus::FeatureExpectation{
                .information_set =
                    std::string(information_set),
                .descriptor = std::string(descriptor),
                .legal_set = std::string(legal_set),
                .common_world_key =
                    std::string(common_world_key),
            });
    require(
        inserted ||
            (found->second.information_set ==
                 information_set &&
             found->second.descriptor == descriptor &&
             found->second.legal_set == legal_set &&
             found->second.common_world_key ==
                 common_world_key),
        std::string(coordinate) +
            ": feature-row link changes identity");
    const auto [identity, identity_inserted] =
        census.feature_identities.emplace(
            std::pair{
                std::string(information_set),
                std::string(descriptor)},
            std::string(row_id));
    require(
        identity_inserted || identity->second == row_id,
        std::string(coordinate) +
            ": semantic feature row has multiple IDs");
    return found->second;
}

void bind_feature_payload(
    EvidenceCensus::FeatureExpectation& expected,
    std::span<const double> features,
    std::string_view consequence,
    std::string_view coordinate) {
    require(
        !features.empty() &&
            audit_common::is_lower_hex_digest(consequence) &&
            std::all_of(
                features.begin(), features.end(),
                [](double value) {
                    return std::isfinite(value);
                }),
        std::string(coordinate) +
            ": feature payload is invalid");
    if (expected.features.empty()) {
        expected.features.assign(
            features.begin(), features.end());
        expected.consequence_fingerprint =
            std::string(consequence);
        return;
    }
    require(
        audit_common::bit_identical(
            expected.features, features) &&
            expected.consequence_fingerprint == consequence,
        std::string(coordinate) +
            ": feature payload changes across evidence");
}

void bind_feature_target(
    EvidenceCensus::FeatureExpectation& expected,
    ScopeKind kind, std::size_t block, double value,
    std::string_view coordinate) {
    require(
        probability(value) &&
            (kind == ScopeKind::FullK64 ||
             block < kBlocks),
        std::string(coordinate) +
            ": feature target is invalid");
    std::optional<double>& destination =
        kind == ScopeKind::FullK64
            ? expected.full
            : expected.blocks.at(block);
    require(
        !destination.has_value() ||
            bit_equal(*destination, value),
        std::string(coordinate) +
            ": repeated feature target changed bits");
    if (!destination.has_value()) {
        destination = value;
    }
}

bool bank_evidence_bit_equal(
    const GroupBankEvidence& first,
    const GroupBankEvidence& second) {
    if (first.bank != second.bank ||
        first.stream_key != second.stream_key ||
        first.actions.size() != second.actions.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < first.actions.size(); ++index) {
        const GroupActionEvidence& left =
            first.actions[index];
        const GroupActionEvidence& right =
            second.actions[index];
        if (left.descriptor != right.descriptor ||
            left.action != right.action ||
            left.feature_row_id != right.feature_row_id ||
            !audit_common::bit_identical(
                left.policy_features,
                right.policy_features) ||
            left.canonical_consequence_fingerprint !=
                right.canonical_consequence_fingerprint ||
            left.samples != right.samples) {
            return false;
        }
    }
    return true;
}

std::vector<fq0_bellman::ActionSamples> bank_samples(
    const GroupBankEvidence& bank) {
    std::vector<fq0_bellman::ActionSamples> samples;
    samples.reserve(bank.actions.size());
    for (const GroupActionEvidence& action : bank.actions) {
        fq0_bellman::ActionSamples row{
            .descriptor = action.descriptor,
            .sample_stream_key = bank.stream_key,
        };
        row.world_indices.reserve(action.samples.size());
        row.samples.reserve(action.samples.size());
        for (const LeafSampleEvidence& sample :
             action.samples) {
            row.world_indices.push_back(sample.world_index);
            row.samples.push_back(
                std::bit_cast<double>(sample.score_bits));
        }
        samples.push_back(std::move(row));
    }
    return samples;
}

void validate_bank(
    const GroupBankEvidence& bank,
    std::string_view expected_name,
    std::string_view root_stable_id,
    std::string_view successor_fingerprint,
    std::string_view feature_information_set,
    ScopeKind scope_kind, std::size_t scope_block,
    EvidenceCensus& census,
    std::string_view coordinate) {
    const fq0_information_set::SeedBank seed_bank =
        expected_name == "A"
            ? fq0_information_set::SeedBank::A
            : fq0_information_set::SeedBank::B;
    const std::uint64_t seed_base =
        seed_bank == fq0_information_set::SeedBank::A
            ? kBankASeedBase
            : kBankBSeedBase;
    const fq0_information_set::SeedDomain
        determinization_domain =
            seed_bank ==
                    fq0_information_set::SeedBank::A
                ? fq0_information_set::SeedDomain::
                      SuccessorSelectionDeterminization
                : fq0_information_set::SeedDomain::
                      SuccessorEvaluationDeterminization;
    const fq0_information_set::SeedDomain macro_domain =
        seed_bank == fq0_information_set::SeedBank::A
            ? fq0_information_set::SeedDomain::
                  SuccessorSelectionMacroTransition
            : fq0_information_set::SeedDomain::
                  SuccessorEvaluationMacroTransition;
    const std::size_t seed_block =
        scope_kind == ScopeKind::FullK64 ? kBlocks
                                        : scope_block;
    const std::string required_stream_key =
        expected_stream_key(
            root_stable_id, successor_fingerprint,
            seed_bank, scope_kind, scope_block);
    require(
        bank.bank == expected_name &&
            bank.stream_key == required_stream_key &&
            !bank.actions.empty(),
        std::string(coordinate) +
            ": incomplete successor bank");
    const auto [stream, inserted_stream] =
        census.successor_streams.emplace(
            bank.stream_key, bank);
    require(
        inserted_stream ||
            bank_evidence_bit_equal(stream->second, bank),
        std::string(coordinate) +
            ": repeated successor coordinate changed bytes");
    std::string previous;
    for (std::size_t action_index = 0;
         action_index < bank.actions.size();
         ++action_index) {
        const GroupActionEvidence& action =
            bank.actions[action_index];
        require(
            !action.descriptor.empty() &&
                (action_index == 0 ||
                 previous < action.descriptor) &&
                action.samples.size() ==
                    kSuccessorWorlds,
            std::string(coordinate) +
                ": malformed successor action row");
        previous = action.descriptor;
        auto& feature = add_feature_link(
            census, action.feature_row_id,
            feature_information_set, action.descriptor,
            expected_legal_set(feature_information_set),
            expected_common_world_key(
                root_stable_id, feature_information_set,
                false),
            coordinate);
        bind_feature_payload(
            feature, action.policy_features,
            action.canonical_consequence_fingerprint,
            coordinate);
        for (std::size_t world = 0;
             world < action.samples.size(); ++world) {
            const LeafSampleEvidence& sample =
                action.samples[world];
            const LeafSampleEvidence& common_sample =
                bank.actions.front().samples[world];
            const double score =
                std::bit_cast<double>(sample.score_bits);
            const bool leaf_kind_valid =
                sample.terminal
                    ? (exact_terminal_value_bits(
                           sample.score_bits) &&
                       !sample.critic_evaluated &&
                       !sample
                            .contextual_legacy_critic_bit_identical &&
                       sample.contextual_score_bits == 0 &&
                       sample.legacy_score_bits == 0)
                    : (sample.critic_evaluated &&
                       sample
                           .contextual_legacy_critic_bit_identical &&
                       sample.score_bits ==
                           sample
                               .contextual_score_bits &&
                       sample.contextual_score_bits ==
                           sample.legacy_score_bits);
            require(
                sample.world_index == world &&
                    sample.determinization_seed ==
                        common_sample
                            .determinization_seed &&
                    sample.macro_seed ==
                        common_sample.macro_seed &&
                    leaf_kind_valid &&
                    probability(score) &&
                    audit_common::is_lower_hex_digest(
                        sample.redacted_leaf_hash) &&
                    sample.forced_action_applied &&
                    sample.actions_applied > 0 &&
                    sample.priority_actions_applied > 0 &&
                    sample.actions_applied <=
                        kMaximumActionsApplied &&
                    sample.priority_actions_applied <=
                        sample.actions_applied &&
                    sample.phase_transitions <=
                        kMaximumPhaseTransitions &&
                    sample.turn_advances <=
                        kMaximumTurnAdvances,
                std::string(coordinate) +
                    ": malformed successor leaf sample");
            claim_indexed_seed(
                census, sample.determinization_seed,
                seed_base, determinization_domain,
                root_stable_id, successor_fingerprint,
                seed_bank, seed_block, world);
            claim_indexed_seed(
                census, sample.macro_seed, seed_base,
                macro_domain, root_stable_id,
                successor_fingerprint, seed_bank,
                seed_block, world);
        }
    }
}

fq0_bellman::CrossFitValue validate_peer_banks(
    const GroupBankEvidence& bank_a,
    const GroupBankEvidence& bank_b,
    std::string_view root_stable_id,
    std::string_view feature_information_set,
    ScopeKind scope_kind, std::size_t scope_block,
    EvidenceCensus& census,
    std::string_view coordinate) {
    require(
        bank_a.stream_key != bank_b.stream_key &&
            bank_a.actions.size() == bank_b.actions.size(),
        std::string(coordinate) +
            ": successor banks are not independent peers");
    for (std::size_t action_index = 0;
         action_index < bank_a.actions.size();
         ++action_index) {
        const GroupActionEvidence& first =
            bank_a.actions[action_index];
        const GroupActionEvidence& second =
            bank_b.actions[action_index];
        require(
            first.descriptor == second.descriptor &&
                first.action == second.action &&
                first.feature_row_id ==
                    second.feature_row_id &&
                audit_common::bit_identical(
                    first.policy_features,
                    second.policy_features) &&
                first.canonical_consequence_fingerprint ==
                    second
                        .canonical_consequence_fingerprint,
            std::string(coordinate) +
                ": successor bank action identities drifted");
    }
    const fq0_bellman::CrossFitValue recomputed =
        fq0_bellman::cross_fit_v0(
            bank_samples(bank_a), bank_samples(bank_b));
    for (std::size_t action_index = 0;
         action_index < recomputed.bank_a.size();
         ++action_index) {
        require(
            recomputed.bank_a[action_index].descriptor ==
                recomputed.bank_b[action_index].descriptor,
            std::string(coordinate) +
                ": cross-fit bank action order drifted");
        const double action_target =
            0.5 *
            (recomputed.bank_a[action_index].value +
             recomputed.bank_b[action_index].value);
        const GroupActionEvidence& action =
            bank_a.actions[action_index];
        auto& feature = add_feature_link(
            census, action.feature_row_id,
            feature_information_set,
            action.descriptor,
            expected_legal_set(feature_information_set),
            expected_common_world_key(
                root_stable_id,
                feature_information_set, false),
            coordinate);
        bind_feature_target(
            feature, scope_kind, scope_block,
            action_target, coordinate);
    }
    return recomputed;
}

std::size_t local_world_index(
    const std::vector<std::size_t>& worlds,
    std::size_t global) {
    const auto found =
        std::lower_bound(worlds.begin(), worlds.end(), global);
    require(
        found != worlds.end() && *found == global,
        "scope particle is outside its root-world membership");
    return static_cast<std::size_t>(
        std::distance(worlds.begin(), found));
}

void validate_scope(
    const ScopeEvidence& scope, std::size_t scope_index,
    const fq0_bellman::TargetBlocks& target,
    std::size_t root_player,
    std::string_view root_stable_id,
    std::string_view root_action_descriptor,
    std::span<
        const RootTransitionParticleEvidence>
        root_transitions,
    EvidenceCensus& census,
    std::string_view coordinate) {
    const bool full = scope_index == 0;
    const std::size_t expected_particles =
        full ? kRootWorlds : kWorldsPerBlock;
    require(
        root_transitions.size() == kRootWorlds &&
            scope.complete &&
            scope.exact_particle_partition &&
            scope.kind ==
                (full ? ScopeKind::FullK64
                      : ScopeKind::BlockK8) &&
            scope.block == (full ? 0 : scope_index - 1) &&
            scope.root_world_indices.size() ==
                expected_particles &&
            std::is_sorted(
                scope.root_world_indices.begin(),
                scope.root_world_indices.end()) &&
            std::adjacent_find(
                scope.root_world_indices.begin(),
                scope.root_world_indices.end()) ==
                scope.root_world_indices.end(),
        std::string(coordinate) +
            ": malformed target scope");
    if (full) {
        for (std::size_t world = 0;
             world < kRootWorlds; ++world) {
            require(
                scope.root_world_indices[world] == world,
                std::string(coordinate) +
                    ": full scope worlds are not canonical");
        }
    } else {
        const std::size_t offset =
            (scope_index - 1) * kWorldsPerBlock;
        for (std::size_t world = 0;
             world < kWorldsPerBlock; ++world) {
            require(
                scope.root_world_indices[world] ==
                    offset + world,
                std::string(coordinate) +
                    ": block worlds are not contiguous");
        }
    }

    std::vector<fq0_bellman::TerminalParticle> terminals;
    terminals.reserve(scope.terminals.size());
    std::set<std::size_t> accounted_worlds;
    for (const fq0_bellman::TerminalParticle& terminal :
         scope.terminals) {
        require(
            terminal.world_index <
                root_transitions.size(),
            std::string(coordinate) +
                ": terminal world index is out of range");
        const RootTransitionParticleEvidence& transition =
            root_transitions[terminal.world_index];
        require(
            exact_terminal_value_bits(
                std::bit_cast<std::uint64_t>(
                    terminal.root_owner_value)) &&
                transition.terminal &&
                bit_equal(
                    std::bit_cast<double>(
                        transition
                            .terminal_root_owner_value_bits),
                    terminal.root_owner_value) &&
                transition
                    .successor_information_set_fingerprint
                    .empty() &&
                accounted_worlds
                    .insert(terminal.world_index)
                    .second,
            std::string(coordinate) +
                ": terminal value is invalid");
        terminals.push_back({
            .world_index = local_world_index(
                scope.root_world_indices,
                terminal.world_index),
            .root_owner_value =
                terminal.root_owner_value,
        });
    }

    std::vector<fq0_bellman::SuccessorGroup> groups;
    groups.reserve(scope.groups.size());
    std::string previous_group;
    for (std::size_t group_index = 0;
         group_index < scope.groups.size(); ++group_index) {
        const SuccessorGroupEvidence& group =
            scope.groups[group_index];
        const std::string group_coordinate =
            std::string(coordinate) + "/group/" +
            std::to_string(group_index);
        require(
            group.complete &&
                audit_common::is_lower_hex_digest(
                    group.information_set_fingerprint) &&
                (group_index == 0 ||
                 previous_group <
                     group.information_set_fingerprint) &&
                group.successor_owner < 2 &&
                group.relation ==
                    (group.successor_owner == root_player
                         ? fq0_bellman::OwnerRelation::
                               SameOwner
                         : fq0_bellman::OwnerRelation::
                               OpponentOwner) &&
                !group.root_world_indices.empty() &&
                std::is_sorted(
                    group.root_world_indices.begin(),
                    group.root_world_indices.end()) &&
                std::adjacent_find(
                    group.root_world_indices.begin(),
                    group.root_world_indices.end()) ==
                    group.root_world_indices.end() &&
                group.representative_root_world ==
                    group.root_world_indices.front() &&
                group
                        .representative_root_action_descriptor ==
                    root_action_descriptor &&
                group.every_representative_reconstructs &&
                group.hidden_repartition_eligible ==
                    group.hidden_identity_changed &&
                group.hidden_repartition_invariant,
            group_coordinate +
                ": incomplete successor group");
        require(
            group.representative_reconstruction_witnesses
                    .size() ==
                group.root_world_indices.size(),
            group_coordinate +
                ": successor reconstruction witnesses are "
                "incomplete");
        previous_group =
            group.information_set_fingerprint;
        const auto successor_key = std::pair{
            std::string(root_stable_id),
            group.information_set_fingerprint,
        };
        census.successor_information_sets.insert(
            successor_key);
        EvidenceCensus::SuccessorOccurrence& occurrence =
            census.successor_occurrences[successor_key];
        require(
            !occurrence.owner.has_value() ||
                *occurrence.owner ==
                    group.successor_owner,
            group_coordinate +
                ": successor owner changes across "
                "occurrences");
        occurrence.owner = group.successor_owner;
        for (const std::size_t world :
             group.root_world_indices) {
            occurrence.representatives.insert({
                world,
                std::string(root_action_descriptor),
            });
        }
        occurrence.minimum_representative =
            *occurrence.representatives.begin();
        const std::string feature_information_set =
            scoped_successor_information_set(
                root_stable_id,
                group.information_set_fingerprint);
        validate_bank(
            group.bank_a, "A",
            root_stable_id,
            group.information_set_fingerprint,
            feature_information_set, scope.kind,
            scope.block,
            census, group_coordinate);
        validate_bank(
            group.bank_b, "B",
            root_stable_id,
            group.information_set_fingerprint,
            feature_information_set, scope.kind,
            scope.block,
            census, group_coordinate);
        const fq0_bellman::CrossFitValue recomputed =
            validate_peer_banks(
                group.bank_a, group.bank_b,
                root_stable_id,
                feature_information_set, scope.kind,
                scope.block, census, group_coordinate);
        require(
            cross_fit_equal(recomputed, group.cross_fit),
            group_coordinate +
                ": cross-fit aggregate does not match raw bits");
        const std::string group_baseline =
            successor_operator_bank_pair_payload_sha256_impl(
                group.bank_a, group.bank_b, recomputed);
        validate_bound_witness(
            group.hidden_repartition_witness,
            "group-hidden",
            binding::hidden_repartition_coordinate(
                group_coordinate,
                group.representative_root_world,
                group
                    .representative_root_action_descriptor),
            group_baseline);
        for (std::size_t witness_index = 0;
             witness_index <
             group.root_world_indices.size();
             ++witness_index) {
            const std::size_t member_world =
                group.root_world_indices[witness_index];
            validate_bound_witness(
                group
                    .representative_reconstruction_witnesses
                        [witness_index],
                "group-representative",
                group_coordinate + "/member/" +
                    std::to_string(member_world),
                group_baseline);
        }

        fq0_bellman::SuccessorGroup backed{
            .fingerprint =
                group.information_set_fingerprint,
            .mass = group.root_world_indices.size(),
            .relation = group.relation,
            .successor_owner_value =
                group.cross_fit.value,
        };
        backed.world_indices.reserve(
            group.root_world_indices.size());
        for (const std::size_t world :
             group.root_world_indices) {
            require(
                world < root_transitions.size(),
                group_coordinate +
                    ": successor world index is out of "
                    "range");
            const RootTransitionParticleEvidence& transition =
                root_transitions[world];
            require(
                !transition.terminal &&
                    transition
                            .successor_information_set_fingerprint ==
                        group
                            .information_set_fingerprint &&
                    transition.successor_owner ==
                        group.successor_owner &&
                    accounted_worlds.insert(world).second,
                group_coordinate +
                    ": successor membership disagrees with "
                    "the root transition");
            backed.world_indices.push_back(
                local_world_index(
                    scope.root_world_indices, world));
        }
        groups.push_back(std::move(backed));
    }
    require(
        accounted_worlds ==
            std::set<std::size_t>(
                scope.root_world_indices.begin(),
                scope.root_world_indices.end()),
        std::string(coordinate) +
            ": scope membership does not account for every "
            "root transition exactly once");
    const fq0_bellman::BackedTarget recomputed =
        fq0_bellman::back_up_root_target(
            expected_particles, terminals, groups);
    require(
        backed_target_equal(recomputed, scope.target),
        std::string(coordinate) +
            ": backed target does not match raw particles");
    const double expected =
        full ? target.full : target.blocks[scope_index - 1];
    require(
        bit_equal(scope.target.value, expected),
        std::string(coordinate) +
            ": scope target disagrees with action target");
}

void validate_action(
    const RootActionEvidence& action,
    std::size_t root_player,
    std::string_view root_stable_id,
    std::string_view raw_seed_group,
    EvidenceCensus& census,
    std::string_view coordinate) {
    require(
        action.complete && !action.descriptor.empty() &&
            action.scopes.size() == kBlocks + 1 &&
            action.root_transitions.size() ==
                kRootWorlds &&
            probability(action.target.full) &&
            !action.policy_features.empty() &&
            audit_common::is_lower_hex_digest(
                action.canonical_consequence_fingerprint),
        std::string(coordinate) +
            ": incomplete root action");
    const std::string feature_information_set =
        scoped_root_information_set(
            root_stable_id, raw_seed_group);
    auto& feature = add_feature_link(
        census, action.feature_row_id,
        feature_information_set, action.descriptor,
        expected_legal_set(feature_information_set),
        expected_common_world_key(
            root_stable_id, feature_information_set, true),
        coordinate);
    bind_feature_payload(
        feature, action.policy_features,
        action.canonical_consequence_fingerprint,
        coordinate);
    bind_feature_target(
        feature, ScopeKind::FullK64, 0,
        action.target.full, coordinate);
    for (std::size_t block = 0; block < kBlocks;
         ++block) {
        bind_feature_target(
            feature, ScopeKind::BlockK8, block,
            action.target.blocks[block], coordinate);
    }
    for (std::size_t world = 0;
         world < action.root_transitions.size(); ++world) {
        const RootTransitionParticleEvidence& transition =
            action.root_transitions[world];
        require(
            transition.world_index == world &&
                (transition.terminal
                     ? (exact_terminal_value_bits(
                            transition
                                .terminal_root_owner_value_bits) &&
                        transition
                            .successor_information_set_fingerprint
                            .empty())
                     : (transition
                                .terminal_root_owner_value_bits ==
                            0 &&
                        audit_common::is_lower_hex_digest(
                            transition
                                .successor_information_set_fingerprint) &&
                        transition.redacted_result_hash ==
                            transition
                                .successor_information_set_fingerprint &&
                        transition.successor_owner < 2)) &&
                transition.forced_root_action_applied &&
                transition.successful_disposition &&
                transition.actions_applied > 0 &&
                transition.priority_actions_applied > 0 &&
                transition.actions_applied <=
                    kMaximumActionsApplied &&
                transition.priority_actions_applied <=
                    transition.actions_applied &&
                transition.phase_transitions <=
                    kMaximumPhaseTransitions &&
                transition.turn_advances <=
                    kMaximumTurnAdvances &&
                audit_common::is_lower_hex_digest(
                    transition.redacted_result_hash),
            std::string(coordinate) +
                ": malformed root transition evidence");
        claim_indexed_seed(
            census, transition.determinization_seed,
            kRootTransitionSeedBase,
            fq0_information_set::SeedDomain::
                RootDeterminization,
            root_stable_id, raw_seed_group,
            fq0_information_set::SeedBank::Root, kBlocks,
            world);
        claim_indexed_seed(
            census, transition.macro_seed,
            kRootTransitionSeedBase,
            fq0_information_set::SeedDomain::
                RootMacroTransition,
            root_stable_id, raw_seed_group,
            fq0_information_set::SeedBank::Root, kBlocks,
            world);
    }
    for (const double block : action.target.blocks) {
        require(
            probability(block),
            std::string(coordinate) +
                ": invalid root-action block target");
    }
    for (const double feature : action.policy_features) {
        require(
            std::isfinite(feature),
            std::string(coordinate) +
                ": non-finite policy feature");
    }
    for (std::size_t scope = 0;
         scope < action.scopes.size(); ++scope) {
        validate_scope(
            action.scopes[scope], scope, action.target,
            root_player, root_stable_id,
            action.descriptor,
            action.root_transitions, census,
            std::string(coordinate) + "/scope/" +
                std::to_string(scope));
    }
}

void validate_successor_feature_evaluations(
    const ScientificEvidence& evidence,
    const std::set<std::string>& root_stable_ids,
    EvidenceCensus& census) {
    std::set<std::pair<std::string, std::string>>
        evaluated;
    std::optional<std::pair<std::string, std::string>>
        previous;
    for (const SuccessorFeatureEvaluationEvidence&
             evaluation :
         evidence.successor_feature_evaluations) {
        const auto key = std::pair{
            evaluation.root_stable_id,
            evaluation.information_set_fingerprint,
        };
        require(
            evaluation.complete &&
                root_stable_ids.contains(
                    evaluation.root_stable_id) &&
                audit_common::is_lower_hex_digest(
                    evaluation
                        .information_set_fingerprint) &&
                (!previous.has_value() ||
                 *previous < key) &&
                evaluated.insert(key).second &&
                evaluation.scopes.size() == kBlocks + 1,
            "FQ0 successor feature evaluation is incomplete "
            "or noncanonical");
        previous = key;
        const auto occurrence =
            census.successor_occurrences.find(key);
        require(
            occurrence !=
                    census.successor_occurrences.end() &&
                occurrence->second.owner.has_value() &&
                occurrence->second.minimum_representative
                    .has_value() &&
                evaluation.successor_owner ==
                    *occurrence->second.owner &&
                std::pair{
                    evaluation.representative_root_world,
                    evaluation
                        .representative_root_action_descriptor} ==
                    *occurrence->second.minimum_representative,
            "FQ0 successor feature evaluation owner or "
            "canonical representative drifted");
        std::vector<
            SuccessorRepresentativeCoordinateEvidence>
            expected_representatives;
        expected_representatives.reserve(
            occurrence->second.representatives.size());
        for (const auto& [world, descriptor] :
             occurrence->second.representatives) {
            expected_representatives.push_back({
                .root_world = world,
                .root_action_descriptor = descriptor,
            });
        }
        require(
            !expected_representatives.empty() &&
                evaluation.representative_root_world ==
                    expected_representatives.front()
                        .root_world &&
                evaluation
                        .representative_root_action_descriptor ==
                    expected_representatives.front()
                        .root_action_descriptor,
            "FQ0 successor representative catalog is empty "
            "or noncanonical");
        const std::string feature_information_set =
            scoped_successor_information_set(
                evaluation.root_stable_id,
                evaluation
                    .information_set_fingerprint);
        for (std::size_t scope_index = 0;
             scope_index < evaluation.scopes.size();
             ++scope_index) {
            const SuccessorFeatureScopeEvidence& scope =
                evaluation.scopes[scope_index];
            const bool full = scope_index == 0;
            const ScopeKind expected_kind =
                full ? ScopeKind::FullK64
                     : ScopeKind::BlockK8;
            const std::size_t expected_block =
                full ? 0 : scope_index - 1;
            const std::string coordinate =
                evaluation.root_stable_id + "/" +
                evaluation
                    .information_set_fingerprint +
                "/feature-scope/" +
                std::to_string(scope_index);
            require(
                scope.complete &&
                    scope.kind == expected_kind &&
                    scope.block == expected_block &&
                    scope.representative_catalog ==
                        expected_representatives &&
                    scope
                            .representative_reconstruction_witnesses
                            .size() ==
                        expected_representatives.size() &&
                    scope
                        .every_representative_reconstructs &&
                    scope.hidden_repartition_eligible ==
                        scope.hidden_identity_changed &&
                    scope.hidden_repartition_invariant,
                coordinate +
                    ": malformed successor feature scope");
            validate_bank(
                scope.bank_a, "A",
                evaluation.root_stable_id,
                evaluation
                    .information_set_fingerprint,
                feature_information_set, scope.kind,
                scope.block, census, coordinate);
            validate_bank(
                scope.bank_b, "B",
                evaluation.root_stable_id,
                evaluation
                    .information_set_fingerprint,
                feature_information_set, scope.kind,
                scope.block, census, coordinate);
            const fq0_bellman::CrossFitValue recomputed =
                validate_peer_banks(
                    scope.bank_a, scope.bank_b,
                    evaluation.root_stable_id,
                    feature_information_set, scope.kind,
                    scope.block, census, coordinate);
            const std::string scope_baseline =
                successor_operator_bank_pair_payload_sha256_impl(
                    scope.bank_a, scope.bank_b, recomputed);
            for (std::size_t member = 0;
                 member <
                 expected_representatives.size();
                 ++member) {
                const auto& representative =
                    expected_representatives[member];
                validate_bound_witness(
                    scope
                        .representative_reconstruction_witnesses
                            [member],
                    "feature-scope-representative",
                    coordinate + "/member/" +
                        std::to_string(
                            representative.root_world) +
                        "/" +
                        representative
                            .root_action_descriptor,
                    scope_baseline);
            }
            validate_bound_witness(
                scope.hidden_repartition_witness,
                "feature-scope-hidden",
                binding::hidden_repartition_coordinate(
                    coordinate,
                    evaluation.representative_root_world,
                    evaluation
                        .representative_root_action_descriptor),
                scope_baseline);
        }
    }
    require(
        evaluated == census.successor_information_sets,
        "FQ0 successor feature evaluation census does not "
        "match empirical successor information sets");
}

const RootActionEvidence& find_action(
    const ScientificEvidence& evidence,
    std::string_view stable_id,
    std::string_view descriptor) {
    const auto root = std::find_if(
        evidence.roots.begin(), evidence.roots.end(),
        [&](const RootEvidence& value) {
            return value.stable_id == stable_id;
        });
    require(
        root != evidence.roots.end(),
        "contrast refers to a missing root");
    const auto action = std::lower_bound(
        root->actions.begin(), root->actions.end(), descriptor,
        [](const RootActionEvidence& value,
           std::string_view key) {
            return value.descriptor < key;
        });
    require(
        action != root->actions.end() &&
            action->descriptor == descriptor,
        "contrast refers to a missing action");
    return *action;
}

const RootEvidence& find_root(
    const ScientificEvidence& evidence,
    std::string_view stable_id) {
    const auto root = std::find_if(
        evidence.roots.begin(), evidence.roots.end(),
        [&](const RootEvidence& value) {
            return value.stable_id == stable_id;
        });
    require(
        root != evidence.roots.end(),
        "gate role refers to a missing root");
    return *root;
}

bool valid_gate_role(GateRole role) {
    switch (role) {
        case GateRole::Primary:
        case GateRole::LiveForceGuard:
        case GateRole::GrowthTargetGuard:
        case GateRole::ProductiveCounterGuard:
        case GateRole::RedundantCounterGuard:
        case GateRole::XZeroGuard:
        case GateRole::DominanceConsistencyGuard:
        case GateRole::IncomparableControlGuard:
        case GateRole::Descriptive:
            return true;
    }
    return false;
}

std::optional<std::string_view> productive_descriptor(
    std::string_view stable_id) {
    if (stable_id == "blue.counter-fire-elemental.v3") {
        return "counter-fire-elemental";
    }
    if (stable_id == "blue.counter-lethal-bolt.v3") {
        return "counter-lethal-lightning-bolt";
    }
    if (stable_id == "blue.counter-war.v3") {
        return "counter-opponent-counterspell";
    }
    return std::nullopt;
}

bool is_typed_x_zero(
    std::string_view stable_id,
    const PriorityAction& action) {
    if (stable_id ==
        "control.blue.braingeyser-x0.v1") {
        return action.kind ==
                   PriorityActionKind::CastBraingeyser &&
               action.card == CardId::Braingeyser &&
               action.x_value == 0;
    }
    if (stable_id == "ru.disintegrate-player-x.v3" ||
        stable_id ==
            "validation.ru.disintegrate-hold-x0.v1") {
        return action.kind ==
                   PriorityActionKind::CastDisintegrate &&
               action.card == CardId::Disintegrate &&
               action.x_value == 0;
    }
    return false;
}

bool support_condition_for(
    const ScientificEvidence& evidence,
    const ContrastEvidence& contrast) {
    const RootEvidence& root =
        find_root(evidence, contrast.stable_id);
    switch (contrast.role) {
        case GateRole::Primary:
            require(
                contrast.stable_id ==
                        "field.green.second-main-sick-bear-growth.v1" &&
                    contrast.positive_descriptor == "pass" &&
                    contrast.negative_descriptor ==
                        "growth-own-summoning-sick-grizzly-bears",
                "FQ0 primary contrast identity drifted");
            return root.exact_support ==
                   std::vector<std::string>{"pass"};
        case GateRole::LiveForceGuard:
            require(
                contrast.stable_id ==
                        "control.blue.force-spike-live-gray-ogre.v1" &&
                    contrast.positive_descriptor ==
                        "force-spike-gray-ogre" &&
                    contrast.negative_descriptor == "pass",
                "FQ0 live Force contrast identity drifted");
            return root.exact_support ==
                   std::vector<std::string>{
                       "force-spike-gray-ogre"};
        case GateRole::GrowthTargetGuard:
            require(
                contrast.stable_id ==
                        "field.green.begin-combat-growth-tapped-air.v1" &&
                    contrast.positive_descriptor ==
                        "growth-own-ironroot-treefolk" &&
                    contrast.negative_descriptor ==
                        "growth-opponent-tapped-air-elemental",
                "FQ0 Growth target contrast identity drifted");
            return std::find(
                       root.exact_support.begin(),
                       root.exact_support.end(),
                       "growth-opponent-tapped-air-elemental") ==
                   root.exact_support.end();
        case GateRole::ProductiveCounterGuard: {
            const auto expected =
                productive_descriptor(contrast.stable_id);
            require(
                expected.has_value() &&
                    contrast.positive_descriptor == *expected,
                "FQ0 productive counter identity drifted");
            return root.exact_support ==
                   std::vector<std::string>{
                       std::string(*expected)};
        }
        case GateRole::RedundantCounterGuard:
            require(
                contrast.stable_id ==
                        "control.blue.counter-redundant-same-target.v1" &&
                    contrast.positive_descriptor == "pass",
                "FQ0 redundant counter identity drifted");
            return root.exact_support ==
                   std::vector<std::string>{"pass"};
        case GateRole::XZeroGuard:
            require(
                contrast.stable_id ==
                        "control.blue.braingeyser-x0.v1" ||
                    contrast.stable_id ==
                        "ru.disintegrate-player-x.v3" ||
                    contrast.stable_id ==
                        "validation.ru.disintegrate-hold-x0.v1",
                "FQ0 X=0 guard identity drifted");
            require(
                find_action(
                    evidence, contrast.stable_id,
                    contrast.positive_descriptor)
                        .action.kind ==
                    PriorityActionKind::Pass,
                "FQ0 X=0 guard positive action is not typed "
                "Pass");
            require(
                is_typed_x_zero(
                    contrast.stable_id,
                    find_action(
                        evidence, contrast.stable_id,
                        contrast.negative_descriptor)
                        .action),
                "FQ0 X=0 guard negative action is not a "
                "typed X=0 spell");
            require(
                std::count_if(
                    root.actions.begin(), root.actions.end(),
                    [&](const RootActionEvidence& action) {
                        return is_typed_x_zero(
                            contrast.stable_id,
                            action.action);
                    }) == 2,
                "FQ0 X=0 root does not contain both typed "
                "targets");
            return std::none_of(
                root.exact_support.begin(),
                root.exact_support.end(),
                [&](const std::string& descriptor) {
                    return is_typed_x_zero(
                        contrast.stable_id,
                        find_action(
                            evidence, contrast.stable_id,
                            descriptor)
                            .action);
                });
        case GateRole::DominanceConsistencyGuard:
        case GateRole::IncomparableControlGuard:
            throw std::runtime_error(
                "dominance-only role appeared in contrasts");
        case GateRole::Descriptive:
            return contrast.support_condition;
    }
    throw std::runtime_error("invalid FQ0 contrast gate role");
}

bool invariance_fields_pass(
    const InvarianceEvidence& invariance) {
    const bool independent_manifest =
        identity_witness_passed(
            invariance.independent_manifest_witness);
    const bool repeated =
        identity_witness_passed(
            invariance.repeated_construction_witness);
    const bool descriptor_order =
        identity_witness_passed(
            invariance.descriptor_order_witness);
    const bool thread_count =
        identity_witness_passed(
            invariance.thread_count_witness);
    const bool hidden_repartition =
        identity_witness_passed(
            invariance.hidden_repartition_witness);
    const bool legacy =
        identity_witness_passed(
            invariance
                .contextual_legacy_critic_witness);
    return invariance
                   .independent_manifest_bit_identical ==
               independent_manifest &&
           invariance
                   .repeated_construction_bit_identical ==
               repeated &&
           invariance.descriptor_order_bit_identical ==
               descriptor_order &&
           invariance.thread_count_bit_identical ==
               thread_count &&
           invariance.hidden_repartition_bit_identical ==
               hidden_repartition &&
           invariance
                   .contextual_legacy_critic_bit_identical ==
               legacy &&
           independent_manifest && repeated &&
           descriptor_order && thread_count &&
           hidden_repartition && legacy;
}

bool integrity_fields_pass(
    const IntegrityEvidence& integrity) {
    return integrity.artifact_requirement_matched &&
           integrity.artifact_unchanged &&
           integrity.model_identity_matched &&
           integrity.model_before == integrity.model_after &&
           components_valid(
               integrity.model_components_before) &&
           integrity.model_components_before ==
               integrity.model_components_after;
}

int exact_order(double first, double second) {
    if (bit_equal(first, second)) {
        return 0;
    }
    require(
        first != second,
        "FQ0 ranking contains numerically equal scores "
        "with different IEEE encodings");
    return first < second ? -1 : 1;
}

bool mana_cost_leq(
    const ManaCost& first, const ManaCost& second) {
    return first.generic <= second.generic &&
           first.green <= second.green &&
           first.red <= second.red &&
           first.blue <= second.blue &&
           first.white <= second.white;
}

bool canonical_cost_valid(
    const fq0_dominance::CanonicalPlayerResourceCost&
        cost) {
    return cost.mana_depleted.generic >= 0 &&
           cost.mana_depleted.green >= 0 &&
           cost.mana_depleted.red >= 0 &&
           cost.mana_depleted.blue >= 0 &&
           cost.mana_depleted.white >= 0;
}

bool canonical_cost_leq(
    const fq0_dominance::CanonicalPlayerResourceCost& first,
    const fq0_dominance::CanonicalPlayerResourceCost&
        second) {
    const auto counts_leq =
        [](const auto& left, const auto& right) {
            for (std::size_t index = 0;
                 index < left.size(); ++index) {
                if (left[index] > right[index]) {
                    return false;
                }
            }
            return true;
        };
    return counts_leq(
               first.hand_cards_consumed,
               second.hand_cards_consumed) &&
           mana_cost_leq(
               first.mana_depleted,
               second.mana_depleted) &&
           counts_leq(
               first.lands_newly_tapped,
               second.lands_newly_tapped) &&
           counts_leq(
               first.artifacts_newly_tapped,
               second.artifacts_newly_tapped) &&
           (!first.land_play_entitlement_consumed ||
            second.land_play_entitlement_consumed);
}

fq0_dominance::Orientation
rederive_dominance_orientation(
    const fq0_dominance::Comparison& comparison,
    std::size_t actor) {
    require(
        actor < 2 &&
            comparison.root_information_equal &&
            comparison.first_normalized ==
                comparison.first.valid &&
            comparison.second_normalized ==
                comparison.second.valid &&
            comparison.consequences_equal ==
                (comparison.first.valid &&
                 comparison.second.valid &&
                 comparison.first
                         .owner_observable_consequence ==
                     comparison.second
                         .owner_observable_consequence) &&
            (comparison.first.valid
                 ? !comparison.first
                        .owner_observable_consequence.empty()
                 : comparison.first
                       .owner_observable_consequence.empty()) &&
            std::all_of(
                comparison.first.costs.begin(),
                comparison.first.costs.end(),
                canonical_cost_valid) &&
            (comparison.second.valid
                 ? !comparison.second
                        .owner_observable_consequence.empty()
                 : comparison.second
                       .owner_observable_consequence.empty()) &&
            std::all_of(
                comparison.second.costs.begin(),
                comparison.second.costs.end(),
                canonical_cost_valid),
        "FQ0 raw dominance comparison is malformed");
    if (!comparison.first.valid ||
        !comparison.second.valid ||
        !comparison.consequences_equal) {
        return fq0_dominance::Orientation::Incomparable;
    }
    const std::size_t opponent = 1 - actor;
    const bool first_dominates =
        canonical_cost_leq(
            comparison.first.costs[actor],
            comparison.second.costs[actor]) &&
        canonical_cost_leq(
            comparison.second.costs[opponent],
            comparison.first.costs[opponent]) &&
        (comparison.first.costs[actor] !=
             comparison.second.costs[actor] ||
         comparison.first.costs[opponent] !=
             comparison.second.costs[opponent]);
    const bool second_dominates =
        canonical_cost_leq(
            comparison.second.costs[actor],
            comparison.first.costs[actor]) &&
        canonical_cost_leq(
            comparison.first.costs[opponent],
            comparison.second.costs[opponent]) &&
        (comparison.first.costs[actor] !=
             comparison.second.costs[actor] ||
         comparison.first.costs[opponent] !=
             comparison.second.costs[opponent]);
    if (first_dominates == second_dominates) {
        return fq0_dominance::Orientation::Incomparable;
    }
    return first_dominates
               ? fq0_dominance::Orientation::
                     FirstDominatesSecond
               : fq0_dominance::Orientation::
                     SecondDominatesFirst;
}

double pairwise_ranking_change_fraction(
    const C16RootRankingEvidence& ranking,
    const RootEvidence& root) {
    const std::size_t pairs =
        ranking.actions.size() *
        (ranking.actions.size() - 1) / 2;
    if (pairs == 0) {
        return 0.0;
    }
    std::size_t changed = 0;
    for (std::size_t first = 0;
         first < ranking.actions.size(); ++first) {
        for (std::size_t second = first + 1;
             second < ranking.actions.size(); ++second) {
            const double c16_first = std::bit_cast<double>(
                ranking.actions[first].score_bits);
            const double c16_second = std::bit_cast<double>(
                ranking.actions[second].score_bits);
            changed +=
                exact_order(c16_first, c16_second) !=
                        exact_order(
                            root.actions[first].target.full,
                            root.actions[second].target.full)
                    ? 1U
                    : 0U;
        }
    }
    return static_cast<double>(changed) /
           static_cast<double>(pairs);
}

void validate_c16_ranking_summary(
    const ScientificEvidence& evidence) {
    const RankingSummaryEvidence& summary =
        evidence.c16_ranking_changes;
    require(
        summary.complete &&
            summary.roots.size() == evidence.roots.size(),
        "FQ0 C16 ranking comparison is incomplete");
    std::array<double, kDeckCount> pairwise_sums{};
    std::array<std::size_t, kDeckCount> changed_support{};
    std::array<std::size_t, kDeckCount> counts{};
    for (std::size_t root_index = 0;
         root_index < evidence.roots.size(); ++root_index) {
        const RootEvidence& root =
            evidence.roots[root_index];
        const C16RootRankingEvidence& ranking =
            summary.roots[root_index];
        require(
            ranking.stable_id == root.stable_id &&
                ranking.actions.size() ==
                    root.actions.size() &&
                strictly_sorted_unique(
                    ranking.exact_support),
            root.stable_id +
                ": C16 ranking row is incomplete");
        std::vector<fq0_bellman::ActionMean> c16_means;
        c16_means.reserve(ranking.actions.size());
        for (std::size_t action_index = 0;
             action_index < ranking.actions.size();
             ++action_index) {
            const C16ActionRankingEvidence& action =
                ranking.actions[action_index];
            const double score =
                std::bit_cast<double>(action.score_bits);
            require(
                action.descriptor ==
                        root.actions[action_index]
                            .descriptor &&
                    probability(score),
                root.stable_id +
                    ": C16 score rows are not complete, "
                    "canonical probabilities");
            c16_means.push_back({
                .descriptor = action.descriptor,
                .value = score,
            });
        }
        const std::vector<std::string> c16_support =
            fq0_bellman::exact_max_support(c16_means);
        const double pairwise =
            pairwise_ranking_change_fraction(
                ranking, root);
        const bool support_changed =
            c16_support != root.exact_support;
        require(
            ranking.exact_support == c16_support &&
                bit_equal(
                    std::bit_cast<double>(
                        ranking
                            .pairwise_change_fraction_bits),
                    pairwise) &&
                ranking.support_changed ==
                    support_changed,
            root.stable_id +
                ": C16 ranking aggregates disagree with "
                "raw scores");
        const std::size_t deck =
            static_cast<std::size_t>(root.root_deck);
        pairwise_sums[deck] += pairwise;
        changed_support[deck] +=
            support_changed ? 1U : 0U;
        ++counts[deck];
    }
    double equal_deck_pairwise = 0.0;
    double equal_deck_support = 0.0;
    for (std::size_t deck = 0; deck < kDeckCount;
         ++deck) {
        const RankingDeckSummaryEvidence& reported =
            summary.decks[deck];
        const double pairwise =
            pairwise_sums[deck] /
            static_cast<double>(counts[deck]);
        const double support =
            static_cast<double>(changed_support[deck]) /
            static_cast<double>(counts[deck]);
        require(
            reported.deck == static_cast<DeckId>(deck) &&
                reported.roots == counts[deck] &&
                reported.support_changed_roots ==
                    changed_support[deck] &&
                bit_equal(
                    std::bit_cast<double>(
                        reported
                            .mean_pairwise_change_fraction_bits),
                    pairwise) &&
                bit_equal(
                    std::bit_cast<double>(
                        reported
                            .support_changed_fraction_bits),
                    support),
            "FQ0 equal-root C16 deck summary drifted");
        equal_deck_pairwise += pairwise;
        equal_deck_support += support;
    }
    equal_deck_pairwise /=
        static_cast<double>(kDeckCount);
    equal_deck_support /=
        static_cast<double>(kDeckCount);
    require(
        bit_equal(
            std::bit_cast<double>(
                summary
                    .equal_deck_pairwise_change_fraction_bits),
            equal_deck_pairwise) &&
            bit_equal(
                std::bit_cast<double>(
                    summary
                        .equal_deck_support_changed_fraction_bits),
                equal_deck_support),
        "FQ0 equal-deck C16 summary drifted");
}

void validate_report(const RunReport& report) {
    const GateReport expected =
        evaluate_gate(report.scientific, report.integrity);
    require(
        report.gate == expected &&
            !expected.infrastructure_failure &&
            expected.complete_evidence &&
            expected.integrity_passed,
        "FQ0 evidence report has an incomplete or inconsistent gate");
    require(
        report.scientific.model_fingerprint ==
                kModelFingerprint &&
            report.scientific.manifest.exact &&
            report.scientific.manifest ==
                ac1_teacher_audit::build_manifest() &&
            report.scientific.manifest.roots.size() ==
                ac1_teacher_audit::kPhysicalPriorityRoots &&
            report.scientific.manifest.logical_priority_ids ==
                ac1_teacher_audit::kLogicalPriorityIds &&
            report.scientific.manifest
                    .physical_roots_by_deck ==
                kExpectedRootsByDeck &&
            report.scientific.manifest
                    .logical_dev_roots_by_deck ==
                std::array<std::size_t, kDeckCount>{
                    4, 4, 4, 4, 4} &&
            !report.scientific.manifest
                 .dev_force_spike_alias.empty() &&
            !report.scientific.manifest
                 .canonical_live_force_spike.empty() &&
            !report.scientific.manifest
                 .attack_stable_id.empty() &&
            !report.scientific.manifest
                 .attack_information_action_fingerprint
                 .empty() &&
            report.scientific.roots_by_deck ==
                kExpectedRootsByDeck &&
            report.scientific.roots.size() ==
                ac1_teacher_audit::kPhysicalPriorityRoots,
        "FQ0 manifest/root census is incomplete");
    require(
        report.integrity.model_before.byte_size ==
                kModelArtifactBytes &&
            report.integrity.model_before.sha256 ==
                kModelArtifactSha256 &&
            report.integrity.model_after.byte_size ==
                kModelArtifactBytes &&
            report.integrity.model_after.sha256 ==
                kModelArtifactSha256,
        "FQ0 immutable model provenance drifted");
    require(
        components_valid(
            report.integrity.model_components_before) &&
            report.integrity.model_components_before ==
                report.integrity.model_components_after,
        "FQ0 model component fingerprints drifted");
    validate_bound_witness(
        report.scientific.invariance
            .independent_manifest_witness,
        "manifest", "global",
        manifest_payload_sha256_impl(
            report.scientific.manifest));

    std::array<std::size_t, kDeckCount> counted{};
    std::set<std::string> stable_ids;
    EvidenceCensus census;
    for (std::size_t root_index = 0;
         root_index < report.scientific.roots.size();
         ++root_index) {
        const RootEvidence& root =
            report.scientific.roots[root_index];
        const ac1_teacher_audit::ManifestRoot& manifest_root =
            report.scientific.manifest.roots[root_index];
        const std::size_t deck =
            static_cast<std::size_t>(root.root_deck);
        require(
            root.complete && !root.stable_id.empty() &&
                stable_ids.insert(root.stable_id).second &&
                root.stable_id ==
                    manifest_root.probe.stable_id &&
                manifest_root.probe.decision_kind ==
                    probes::DecisionKind::Priority &&
                root.manifest_information_action_fingerprint ==
                    manifest_root
                        .information_action_fingerprint &&
                !root
                     .manifest_information_action_fingerprint
                     .empty() &&
                audit_common::is_lower_hex_digest(
                    manifest_root
                        .factory_contract_fingerprint) &&
                root.root_deck ==
                    manifest_root.probe.root_deck &&
                root.root_player ==
                    manifest_root.probe.root_player &&
                root.root_player < 2 &&
                deck < kDeckCount && !root.actions.empty() &&
                root.actions.size() ==
                    manifest_root.probe.candidates.size() &&
                root.hidden_repartition_bit_identical &&
                root.descriptor_order_bit_identical &&
                identity_witness_passed(
                    root.hidden_repartition_witness) &&
                identity_witness_passed(
                    root.descriptor_order_witness) &&
                strictly_sorted_unique(root.exact_support),
            "FQ0 root evidence is incomplete or misbound");
        const std::string root_payload =
            root_payload_sha256_impl(root);
        validate_bound_witness(
            root.hidden_repartition_witness,
            "root-hidden", root.stable_id, root_payload);
        validate_bound_witness(
            root.descriptor_order_witness,
            "root-order", root.stable_id, root_payload);
        ++counted[deck];

        std::vector<const probes::Candidate*>
            canonical_candidates;
        canonical_candidates.reserve(
            manifest_root.probe.candidates.size());
        for (const probes::Candidate& candidate :
             manifest_root.probe.candidates) {
            canonical_candidates.push_back(&candidate);
        }
        std::sort(
            canonical_candidates.begin(),
            canonical_candidates.end(),
            [](const probes::Candidate* first,
               const probes::Candidate* second) {
                return first->descriptor <
                       second->descriptor;
            });
        std::vector<fq0_bellman::ActionMean> means;
        means.reserve(root.actions.size());
        std::string previous;
        for (std::size_t action_index = 0;
             action_index < root.actions.size();
             ++action_index) {
            const RootActionEvidence& action =
                root.actions[action_index];
            const probes::Candidate& candidate =
                *canonical_candidates[action_index];
            const auto* manifest_action =
                std::get_if<PriorityAction>(
                    &candidate.action);
            require(
                (action_index == 0 ||
                 previous < action.descriptor) &&
                    candidate.descriptor ==
                        action.descriptor &&
                    manifest_action != nullptr &&
                    *manifest_action == action.action,
                root.stable_id +
                    ": root actions are not the canonical "
                    "manifest action set");
            previous = action.descriptor;
            validate_action(
                action, root.root_player,
                root.stable_id,
                root
                    .manifest_information_action_fingerprint,
                census,
                root.stable_id + "/action/" +
                    std::to_string(action_index));
            if (action_index != 0) {
                for (std::size_t world = 0;
                     world < kRootWorlds; ++world) {
                    require(
                        action.root_transitions[world]
                                    .determinization_seed ==
                                root.actions.front()
                                    .root_transitions[world]
                                    .determinization_seed &&
                            action.root_transitions[world]
                                    .macro_seed ==
                                root.actions.front()
                                    .root_transitions[world]
                                    .macro_seed,
                        root.stable_id +
                            ": root transition RNG depends on "
                            "the candidate");
                }
            }
            means.push_back({
                .descriptor = action.descriptor,
                .value = action.target.full,
            });
        }
        require(
            fq0_bellman::exact_max_support(means) ==
                root.exact_support,
            root.stable_id +
                ": exact support disagrees with target bits");
    }
    require(
        counted == kExpectedRootsByDeck,
        "FQ0 root deck census disagrees with evidence");
    validate_c16_ranking_summary(report.scientific);
    validate_successor_feature_evaluations(
        report.scientific, stable_ids, census);

    require(
        !report.scientific.feature_rows.empty() &&
            report.scientific.feature_rows.size() ==
                census.feature_links.size(),
        "FQ0 feature-row census is incomplete");
    std::string previous_feature_row;
    std::map<std::string,
             const fq0_bellman::FeatureTargetRow*>
        feature_rows;
    std::map<std::string,
             std::vector<fq0_bellman::ActionMean>>
        feature_means;
    for (const fq0_bellman::FeatureTargetRow& row :
         report.scientific.feature_rows) {
        require(
            previous_feature_row.empty() ||
                previous_feature_row < row.row_id,
            "FQ0 feature rows are not row-ID canonical");
        previous_feature_row = row.row_id;
        const auto link =
            census.feature_links.find(row.row_id);
        require(
            link != census.feature_links.end(),
            "FQ0 feature row is unlinked");
        const EvidenceCensus::FeatureExpectation&
            expected_feature = link->second;
        require(
            expected_feature.full.has_value() &&
                std::all_of(
                    expected_feature.blocks.begin(),
                    expected_feature.blocks.end(),
                    [](const std::optional<double>& value) {
                        return value.has_value();
                    }),
            "FQ0 raw evidence did not produce every feature "
            "target scope");
        fq0_bellman::TargetBlocks expected_target{
            .full = *expected_feature.full,
        };
        for (std::size_t block = 0; block < kBlocks;
             ++block) {
            expected_target.blocks[block] =
                *expected_feature.blocks[block];
        }
        require(
            row.information_set_id ==
                    expected_feature.information_set &&
                row.action_descriptor ==
                    expected_feature.descriptor &&
                row.legal_set_id ==
                    expected_feature.legal_set &&
                row.common_world_key ==
                    expected_feature.common_world_key &&
                audit_common::bit_identical(
                    row.features,
                    expected_feature.features) &&
                row.canonical_consequence_fingerprint ==
                    expected_feature
                        .consequence_fingerprint &&
                target_equal(row.target, expected_target) &&
                feature_rows.emplace(row.row_id, &row).second,
            "FQ0 feature row is unlinked or misbound");
        feature_means[row.information_set_id].push_back({
            .descriptor = row.action_descriptor,
            .value = row.target.full,
        });
    }
    for (const fq0_bellman::FeatureTargetRow& row :
         report.scientific.feature_rows) {
        const std::vector<std::string> support =
            fq0_bellman::exact_max_support(
                feature_means.at(row.information_set_id));
        const bool expected_unique =
            support.size() == 1 &&
            support.front() == row.action_descriptor;
        require(
            row.unique_exact_max == expected_unique,
            "FQ0 feature-row support flag disagrees with "
            "raw action targets");
    }
    const fq0_bellman::FeatureCollisionAnalysis
        recomputed_collisions =
            fq0_bellman::analyze_global_feature_collisions(
                report.scientific.feature_rows);
    require(
        collision_analysis_equal(
            recomputed_collisions,
            report.scientific.feature_collisions),
        "FQ0 collision analysis does not match feature rows");

    const std::map<std::string, std::size_t>
        expected_x_zero_actions_by_root = {
            {"control.blue.braingeyser-x0.v1", 2},
            {"ru.disintegrate-player-x.v3", 2},
            {"validation.ru.disintegrate-hold-x0.v1", 2},
        };
    std::map<std::string, std::size_t>
        x_zero_actions_by_root;
    std::set<std::pair<std::string, std::string>>
        expected_x_zero_actions;
    for (const RootEvidence& root :
         report.scientific.roots) {
        for (const RootActionEvidence& action :
             root.actions) {
            if (is_typed_x_zero(
                    root.stable_id, action.action)) {
                ++x_zero_actions_by_root[root.stable_id];
                require(
                    expected_x_zero_actions
                        .insert({
                            root.stable_id,
                            action.descriptor,
                        })
                        .second,
                    "FQ0 typed X=0 action identity is "
                    "duplicated");
            }
        }
    }
    require(
        x_zero_actions_by_root ==
                expected_x_zero_actions_by_root &&
            expected_x_zero_actions.size() == 6,
        "FQ0 raw typed X=0 action census drifted");

    std::set<std::string> contrast_names;
    std::map<GateRole, std::set<std::string>>
        contrast_role_census;
    std::map<GateRole, std::size_t> contrast_role_counts;
    std::set<std::pair<std::string, std::string>>
        x_zero_contrast_actions;
    std::optional<std::tuple<std::size_t, std::string,
                             std::string>>
        previous_contrast;
    bool primary_contrast_passed = false;
    bool contrast_guards_passed = true;
    for (const ContrastEvidence& contrast :
         report.scientific.contrasts) {
        const auto contrast_key = std::tuple{
            static_cast<std::size_t>(contrast.role),
            contrast.stable_id, contrast.name};
        require(
            valid_gate_role(contrast.role) &&
                contrast.role !=
                    GateRole::DominanceConsistencyGuard &&
                contrast.role !=
                    GateRole::IncomparableControlGuard &&
                (!previous_contrast.has_value() ||
                 *previous_contrast < contrast_key) &&
                !contrast.name.empty() &&
                contrast_names.insert(contrast.name).second,
            "FQ0 contrasts are invalid or noncanonical");
        previous_contrast = contrast_key;
        contrast_role_census[contrast.role].insert(
            contrast.stable_id);
        ++contrast_role_counts[contrast.role];
        const RootActionEvidence& positive = find_action(
            report.scientific, contrast.stable_id,
            contrast.positive_descriptor);
        const RootActionEvidence& negative = find_action(
            report.scientific, contrast.stable_id,
            contrast.negative_descriptor);
        const fq0_bellman::BlockContrast recomputed =
            fq0_bellman::summarize_block_contrast(
                positive.target, negative.target);
        const bool support_condition =
            support_condition_for(
                report.scientific, contrast);
        const bool robust_direction =
            fq0_bellman::passes_directional_gate(
                recomputed,
                kPrimaryMinimumPositiveBlocks);
        const bool directional_role =
            contrast.role == GateRole::Primary ||
            contrast.role == GateRole::LiveForceGuard ||
            contrast.role == GateRole::GrowthTargetGuard ||
            contrast.role == GateRole::Descriptive;
        const bool expected_passed =
            support_condition &&
            (directional_role ? robust_direction : true);
        if (contrast.role == GateRole::XZeroGuard) {
            require(
                x_zero_contrast_actions
                    .insert({
                        contrast.stable_id,
                        contrast.negative_descriptor,
                    })
                    .second,
                "FQ0 typed X=0 contrast action is "
                "duplicated");
        }
        require(
            contrast_equal(recomputed, contrast.contrast) &&
                contrast.support_condition ==
                    support_condition &&
                contrast.directional_passed ==
                    expected_passed,
            contrast.name +
                ": contrast does not match root targets");
        if (contrast.role == GateRole::Primary) {
            primary_contrast_passed =
                contrast.directional_passed;
        } else if (
            contrast.role != GateRole::Descriptive) {
            contrast_guards_passed =
                contrast_guards_passed &&
                contrast.directional_passed;
        }
    }
    const std::map<GateRole, std::set<std::string>>
        expected_contrast_roles = {
            {GateRole::Primary,
             {"field.green.second-main-sick-bear-growth.v1"}},
            {GateRole::LiveForceGuard,
             {"control.blue.force-spike-live-gray-ogre.v1"}},
            {GateRole::GrowthTargetGuard,
             {"field.green.begin-combat-growth-tapped-air.v1"}},
            {GateRole::ProductiveCounterGuard,
             {"blue.counter-fire-elemental.v3",
              "blue.counter-lethal-bolt.v3",
              "blue.counter-war.v3"}},
            {GateRole::RedundantCounterGuard,
             {"control.blue.counter-redundant-same-target.v1"}},
    };
    for (const auto& [role, expected_ids] :
         expected_contrast_roles) {
        require(
            contrast_role_census[role] == expected_ids &&
                contrast_role_counts[role] ==
                    expected_ids.size(),
            "FQ0 named contrast gate census drifted");
    }
    require(
        x_zero_contrast_actions ==
                expected_x_zero_actions &&
            contrast_role_counts[GateRole::XZeroGuard] ==
                expected_x_zero_actions.size() &&
            contrast_role_census[GateRole::XZeroGuard] ==
                std::set<std::string>{
                    "control.blue.braingeyser-x0.v1",
                    "ru.disintegrate-player-x.v3",
                    "validation.ru.disintegrate-hold-x0.v1",
                },
        "FQ0 typed six-action X=0 contrast census drifted");

    std::set<std::tuple<std::string, std::string,
                        std::string>>
        expected_dominance_keys;
    for (const RootEvidence& root :
         report.scientific.roots) {
        for (std::size_t first = 0;
             first < root.actions.size(); ++first) {
            for (std::size_t second = first + 1;
                 second < root.actions.size(); ++second) {
                expected_dominance_keys.insert({
                    root.stable_id,
                    root.actions[first].descriptor,
                    root.actions[second].descriptor,
                });
            }
        }
    }
    std::set<std::tuple<std::string, std::string,
                        std::string>>
        dominance_keys;
    std::optional<std::tuple<std::string, std::string,
                             std::string>>
        previous_dominance;
    bool primary_dominance_passed = false;
    bool dominance_guards_passed = true;
    std::size_t x_zero_dominance_pairs = 0;
    std::size_t incomparable_control_pairs = 0;
    for (const DominancePairEvidence& pair :
         report.scientific.dominance_pairs) {
        const auto pair_key = std::tuple{
            pair.stable_id, pair.first_descriptor,
            pair.second_descriptor};
        require(
            valid_gate_role(pair.role) &&
                (pair.role == GateRole::Primary ||
                 pair.role ==
                     GateRole::DominanceConsistencyGuard ||
                 pair.role == GateRole::XZeroGuard ||
                 pair.role ==
                     GateRole::IncomparableControlGuard ||
                 pair.role == GateRole::Descriptive) &&
                (!previous_dominance.has_value() ||
                 *previous_dominance < pair_key) &&
                pair.first_descriptor <
                    pair.second_descriptor &&
                dominance_keys
                    .insert({
                        pair.stable_id,
                        pair.first_descriptor,
                        pair.second_descriptor})
                    .second,
            "FQ0 dominance pairs are invalid or noncanonical");
        previous_dominance = pair_key;
        const RootActionEvidence& first = find_action(
            report.scientific, pair.stable_id,
            pair.first_descriptor);
        const RootActionEvidence& second = find_action(
            report.scientific, pair.stable_id,
            pair.second_descriptor);
        const RootEvidence& dominance_root =
            find_root(
                report.scientific, pair.stable_id);
        require(
            pair.required_worlds == kRootWorlds &&
                pair.worlds.size() == kRootWorlds,
            pair.stable_id +
                ": dominance world census is incomplete");
        std::size_t matching = 0;
        for (std::size_t world_index = 0;
             world_index < pair.worlds.size();
             ++world_index) {
            const DominanceWorldEvidence& world =
                pair.worlds[world_index];
            const std::string world_coordinate =
                pair.stable_id + "/" +
                pair.first_descriptor + "/" +
                pair.second_descriptor + "/" +
                std::to_string(world_index);
            claim_indexed_seed(
                census, world.determinization_seed,
                kRootTransitionSeedBase,
                fq0_information_set::SeedDomain::
                    RootDeterminization,
                pair.stable_id,
                dominance_root
                    .manifest_information_action_fingerprint,
                fq0_information_set::SeedBank::Root, kBlocks,
                world_index);
            require(
                world.world_index == world_index &&
                    world.common_world_key ==
                        binding::dominance_common_world_key(
                            pair.stable_id,
                            dominance_root
                                .manifest_information_action_fingerprint,
                            world_index) &&
                    world.orientation ==
                        rederive_dominance_orientation(
                            world.comparison,
                            dominance_root.root_player) &&
                    world.comparison.orientation ==
                        world.orientation &&
                    world.hidden_repartition_bit_identical &&
                    (world.orientation ==
                            fq0_dominance::Orientation::
                                Incomparable ||
                     world.orientation ==
                            fq0_dominance::Orientation::
                                FirstDominatesSecond ||
                     world.orientation ==
                            fq0_dominance::Orientation::
                                SecondDominatesFirst),
                pair.stable_id +
                    ": malformed dominance world");
            validate_bound_witness(
                world.hidden_repartition_witness,
                "dominance-hidden", world_coordinate,
                dominance_payload_sha256_impl(
                    pair.stable_id,
                    pair.first_descriptor,
                    pair.second_descriptor, world));
        }

        const bool first_is_pass =
            first.action.kind == PriorityActionKind::Pass;
        const bool second_is_pass =
            second.action.kind == PriorityActionKind::Pass;
        const bool first_is_x_zero =
            is_typed_x_zero(
                pair.stable_id, first.action);
        const bool second_is_x_zero =
            is_typed_x_zero(
                pair.stable_id, second.action);
        const bool x_zero_pair =
            (first_is_pass && second_is_x_zero) ||
            (second_is_pass && first_is_x_zero);
        const fq0_dominance::Orientation
            pass_dominates_orientation =
                first_is_pass
                    ? fq0_dominance::Orientation::
                          FirstDominatesSecond
                    : fq0_dominance::Orientation::
                          SecondDominatesFirst;
        const bool primary_pair =
            pair.stable_id ==
                "field.green.second-main-sick-bear-growth.v1" &&
            ((pair.first_descriptor == "pass" &&
              pair.second_descriptor ==
                  "growth-own-summoning-sick-grizzly-bears") ||
             (pair.second_descriptor == "pass" &&
              pair.first_descriptor ==
                  "growth-own-summoning-sick-grizzly-bears"));
        const bool redundant_same_target_pair =
            pair.stable_id ==
                "control.blue.counter-redundant-same-target.v1" &&
            ((pair.first_descriptor == "pass" &&
              pair.second_descriptor ==
                  "counter-same-air-elemental") ||
             (pair.second_descriptor == "pass" &&
              pair.first_descriptor ==
                  "counter-same-air-elemental"));
        const bool named_incomparable =
            (pair.stable_id ==
                     "control.blue.force-spike-live-gray-ogre.v1" &&
                 ((pair.first_descriptor == "pass" &&
                   pair.second_descriptor ==
                       "force-spike-gray-ogre") ||
                  (pair.second_descriptor == "pass" &&
                   pair.first_descriptor ==
                       "force-spike-gray-ogre"))) ||
            (pair.stable_id ==
                     "control.blue.force-spike-payable-gray-ogre.v1" &&
                 ((pair.first_descriptor == "pass" &&
                   pair.second_descriptor ==
                       "force-spike-gray-ogre") ||
                  (pair.second_descriptor == "pass" &&
                   pair.first_descriptor ==
                       "force-spike-gray-ogre"))) ||
            (pair.stable_id ==
                     "blue.counter-fire-elemental.v3" &&
                 ((pair.first_descriptor == "pass" &&
                   pair.second_descriptor ==
                       "counter-fire-elemental") ||
                  (pair.second_descriptor == "pass" &&
                   pair.first_descriptor ==
                       "counter-fire-elemental"))) ||
            (pair.stable_id ==
                     "blue.counter-lethal-bolt.v3" &&
                 ((pair.first_descriptor == "pass" &&
                   pair.second_descriptor ==
                       "counter-lethal-lightning-bolt") ||
                  (pair.second_descriptor == "pass" &&
                   pair.first_descriptor ==
                       "counter-lethal-lightning-bolt"))) ||
            (pair.stable_id == "blue.counter-war.v3" &&
                 ((pair.first_descriptor == "pass" &&
                   pair.second_descriptor ==
                       "counter-opponent-counterspell") ||
                  (pair.second_descriptor == "pass" &&
                   pair.first_descriptor ==
                       "counter-opponent-counterspell"))) ||
            (pair.stable_id ==
                     "control.blue.counter-redundant-same-target.v1" &&
                 ((pair.first_descriptor == "pass" &&
                   pair.second_descriptor ==
                       "counter-own-counterspell") ||
                  (pair.second_descriptor == "pass" &&
                   pair.first_descriptor ==
                       "counter-own-counterspell")));

        const fq0_dominance::Orientation first_world =
            pair.worlds.front().orientation;
        const bool uniform = std::all_of(
            pair.worlds.begin(), pair.worlds.end(),
            [&](const DominanceWorldEvidence& world) {
                return world.orientation == first_world;
            });
        const bool uniform_strict =
            uniform &&
            first_world !=
                fq0_dominance::Orientation::Incomparable;
        const fq0_dominance::Orientation
            expected_orientation =
                primary_pair || x_zero_pair ||
                        redundant_same_target_pair
                    ? pass_dominates_orientation
                    : named_incomparable
                          ? fq0_dominance::Orientation::
                                Incomparable
                          : uniform
                                ? first_world
                                : fq0_dominance::Orientation::
                                      Incomparable;
        const GateRole expected_role =
            primary_pair
                ? GateRole::Primary
                : x_zero_pair
                      ? GateRole::XZeroGuard
                      : named_incomparable
                            ? GateRole::
                                  IncomparableControlGuard
                            : uniform_strict ||
                                      redundant_same_target_pair
                                  ? GateRole::
                                        DominanceConsistencyGuard
                                  : GateRole::Descriptive;
        const bool all_settlements_valid = std::all_of(
            pair.worlds.begin(), pair.worlds.end(),
            [](const DominanceWorldEvidence& world) {
                return world.comparison.first.valid &&
                       world.comparison.second.valid;
            });
        require(
            pair.role == expected_role &&
                pair.required_orientation ==
                    expected_orientation,
            pair.stable_id +
                ": dominance role/orientation was not "
                "derived from exhaustive raw worlds");
        for (const DominanceWorldEvidence& world :
             pair.worlds) {
            matching +=
                world.orientation ==
                        pair.required_orientation
                    ? 1U
                    : 0U;
        }
        bool monotonic = true;
        if (expected_orientation !=
            fq0_dominance::Orientation::Incomparable) {
            const bool first_dominates =
                expected_orientation ==
                fq0_dominance::Orientation::
                    FirstDominatesSecond;
            const fq0_bellman::BlockContrast monotonicity =
                fq0_bellman::summarize_block_contrast(
                    first_dominates ? first.target
                                    : second.target,
                    first_dominates ? second.target
                                    : first.target);
            monotonic =
                monotonicity.delta64 >= 0.0 &&
                monotonicity.nonnegative_blocks >=
                    kPrimaryMinimumPositiveBlocks;
        }
        const bool expected_pair_passed =
            matching == kRootWorlds &&
            monotonic &&
            (expected_role == GateRole::Descriptive ||
             all_settlements_valid);
        require(
            !pair.stable_id.empty() &&
                pair.matching_worlds == matching &&
                pair.passed == expected_pair_passed,
            pair.stable_id +
                ": malformed dominance evidence");
        if (pair.role == GateRole::Primary) {
            primary_dominance_passed = pair.passed;
        } else if (pair.role ==
                       GateRole::
                           DominanceConsistencyGuard ||
                   pair.role == GateRole::XZeroGuard ||
                   pair.role ==
                       GateRole::
                           IncomparableControlGuard) {
            dominance_guards_passed =
                dominance_guards_passed && pair.passed;
            x_zero_dominance_pairs +=
                pair.role == GateRole::XZeroGuard ? 1U
                                                  : 0U;
            incomparable_control_pairs +=
                pair.role ==
                        GateRole::
                            IncomparableControlGuard
                    ? 1U
                    : 0U;
        }
    }
    require(
        dominance_keys == expected_dominance_keys &&
            dominance_keys.size() ==
                report.scientific.dominance_pairs.size() &&
            x_zero_dominance_pairs == 6 &&
            incomparable_control_pairs == 6,
        "FQ0 dominance gate census is incomplete");

    const std::string primary_core =
        scientific_core_sha256_impl(report.scientific);
    require(
        audit_common::is_lower_hex_digest(
            report.scientific.primary_core_sha256) &&
            report.scientific.primary_core_sha256 ==
                primary_core,
        "FQ0 primary scientific core digest drifted");
    validate_bound_witness(
        report.scientific.invariance
            .repeated_construction_witness,
        "core-repeat", "global", primary_core);
    validate_bound_witness(
        report.scientific.invariance
            .descriptor_order_witness,
        "core-order", "global", primary_core);
    validate_bound_witness(
        report.scientific.invariance
            .thread_count_witness,
        "core-thread", "global", primary_core);
    validate_bound_witness(
        report.scientific.invariance
            .hidden_repartition_witness,
        "core-hidden", "global", primary_core);
    const auto critic_streams =
        critic_stream_sha256_impl(report.scientific);
    validate_bound_witness(
        report.scientific.invariance
            .contextual_legacy_critic_witness,
        "critic-context-vs-legacy", "global",
        critic_streams[0], critic_streams[1]);
    require(
        report.scientific.invariance.passed ==
            invariance_fields_pass(
                report.scientific.invariance),
        "FQ0 invariance verdict is inconsistent");
    const bool derived_primary =
        primary_contrast_passed &&
        primary_dominance_passed;
    const bool derived_guards =
        contrast_guards_passed &&
        dominance_guards_passed;
    require(
        report.scientific.primary_passed ==
                derived_primary &&
            report.scientific.reject_only_guards_passed ==
                derived_guards &&
            report.scientific.passed ==
                (derived_primary && derived_guards &&
                 report.scientific
                     .feature_collisions.passed),
        "FQ0 scientific gate summaries do not match raw rows");
}

std::string metadata_section(const RunReport& report) {
    std::string output;
    append_row(
        output,
        {"identity", "model_fingerprint",
         report.scientific.model_fingerprint});
    append_row(
        output,
        {"artifact", "model", "path",
         std::string(kModelArtifactPath)});
    append_row(
        output,
        {"artifact", "model", "bytes",
         std::to_string(kModelArtifactBytes)});
    append_row(
        output,
        {"artifact", "model", "sha256",
         std::string(kModelArtifactSha256)});
    append_row(
        output,
        {"model", "training_games",
         std::to_string(kModelTrainingGames)});
    append_row(
        output,
        {"model", "training_seed",
         std::to_string(kModelTrainingSeed)});
    append_row(
        output,
        {"model", "generations",
         std::to_string(kModelGenerations)});
    append_row(
        output,
        {"seed", "root_transition",
         std::to_string(kRootTransitionSeedBase)});
    append_row(
        output,
        {"seed", "bank_a",
         std::to_string(kBankASeedBase)});
    append_row(
        output,
        {"seed", "bank_b",
         std::to_string(kBankBSeedBase)});
    append_row(
        output,
        {"recipe", "root_worlds",
         std::to_string(kRootWorlds)});
    append_row(
        output,
        {"recipe", "blocks", std::to_string(kBlocks)});
    append_row(
        output,
        {"recipe", "worlds_per_block",
         std::to_string(kWorldsPerBlock)});
    append_row(
        output,
        {"recipe", "threads",
         std::to_string(kEvaluationThreads)});
    append_row(
        output,
        {"identity", "primary_core_sha256",
         report.scientific.primary_core_sha256});
    append_row(
        output,
        {"recipe", "c16_ranking", "seed_tag",
         std::string(kC16RankingSeedTag)});
    append_row(
        output,
        {"recipe", "c16_ranking", "seed_base",
         std::to_string(kC16RankingSeedBase)});
    append_row(
        output,
        {"recipe", "c16_ranking", "worlds",
         std::to_string(kC16RankingWorlds)});
    append_row(
        output,
        {"recipe", "c16_ranking", "horizon_turns",
         std::to_string(kC16RankingHorizonTurns)});
    append_row(
        output,
        {"recipe", "c16_ranking", "rollouts_per_world",
         std::to_string(kC16RankingRolloutsPerWorld)});
    append_row(
        output,
        {"recipe", "c16_ranking", "blend_shallow_prior",
         bool_text(kC16RankingBlendShallowPrior)});
    append_row(
        output,
        {"recipe", "c16_ranking", "threads",
         std::to_string(kC16RankingThreads)});
    append_row(
        output,
        {"recipe", "c16_ranking",
         "continuation_epsilon_bits",
         real_bits(kC16RankingContinuationEpsilon)});
    append_row(
        output,
        {"recipe", "c16_ranking",
         "priority_residual_weight_bits",
         real_bits(kC16RankingPriorityResidualWeight)});
    append_row(
        output,
        {"recipe", "c16_ranking", "pass_dominance",
         bool_text(kC16RankingPassDominance)});
    append_row(
        output,
        {"recipe", "c16_ranking",
         "continuation_controller",
         std::to_string(static_cast<std::size_t>(
             kC16RankingContinuationController))});
    append_row(
        output,
        {"recipe", "fq0_macro", "policy",
         std::string(kMacroPolicy)});
    append_row(
        output,
        {"recipe", "fq0_macro", "rollouts_per_action",
         std::to_string(kMacroRolloutsPerAction)});
    append_row(
        output,
        {"recipe", "fq0_macro", "learned_search_depth",
         std::to_string(kMacroLearnedSearchDepth)});
    append_row(
        output,
        {"recipe", "fq0_macro", "exploration_rate_bits",
         real_bits(kMacroExplorationRate)});
    append_row(
        output,
        {"recipe", "fq0_macro",
         "continuation_epsilon_bits",
         real_bits(kMacroContinuationEpsilon)});
    append_row(
        output,
        {"recipe", "fq0_macro",
         "priority_residual_weight_bits",
         real_bits(kMacroPriorityResidualWeight)});
    append_row(
        output,
        {"recipe", "fq0_macro", "pass_dominance",
         bool_text(kMacroPassDominance)});
    append_row(
        output,
        {"recipe", "fq0_macro",
         "continuation_controller",
         std::to_string(static_cast<std::size_t>(
             kMacroContinuationController))});
    append_row(
        output,
        {"recipe", "successor_worlds",
         std::to_string(kSuccessorWorlds)});
    append_row(
        output,
        {"limit", "maximum_actions_applied",
         std::to_string(kMaximumActionsApplied)});
    append_row(
        output,
        {"limit", "maximum_phase_transitions",
         std::to_string(kMaximumPhaseTransitions)});
    append_row(
        output,
        {"limit", "maximum_turn_advances",
         std::to_string(kMaximumTurnAdvances)});
    append_row(
        output,
        {"statistics", "student_t_df7_bits",
         real_bits(fq0_bellman::kStudentT95Df7)});
    append_row(
        output,
        {"statistics", "normal_95_bits",
         real_bits(fq0_bellman::kNormal95)});
    append_row(
        output,
        {"verdict", "exit_code",
         std::to_string(exit_code(report.gate))});
    append_row(
        output,
        {"verdict", "passed",
         bool_text(report.gate.passed)});
    return output;
}

std::string manifest_section(const RunReport& report) {
    std::string output;
    const auto& manifest = report.scientific.manifest;
    append_row(
        output,
        {"manifest", "exact", bool_text(manifest.exact)});
    append_row(
        output,
        {"manifest", "logical_priority_ids",
         std::to_string(manifest.logical_priority_ids)});
    append_row(
        output,
        {"manifest", "dev_force_spike_alias",
         manifest.dev_force_spike_alias,
         manifest.canonical_live_force_spike});
    append_row(
        output,
        {"manifest", "attack_coverage",
         manifest.attack_stable_id,
         manifest.attack_information_action_fingerprint});
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        append_row(
            output,
            {"deck_census", std::to_string(deck),
             std::to_string(
                 manifest.physical_roots_by_deck[deck]),
             std::to_string(
                 manifest.logical_dev_roots_by_deck[deck]),
             std::to_string(
                 report.scientific.roots_by_deck[deck])});
    }
    for (const auto& root : manifest.roots) {
        append_row(
            output,
            {"manifest_root", root.probe.stable_id,
             std::to_string(
                 static_cast<std::size_t>(
                     root.probe.root_deck)),
             std::to_string(root.probe.root_player),
             root.information_action_fingerprint,
             root.factory_contract_fingerprint,
             bool_text(root.from_dev_v3)});
    }
    return output;
}

void append_cross_fit_rows(
    std::string& output, std::string_view coordinate,
    const fq0_bellman::CrossFitValue& cross_fit) {
    for (const auto& action : cross_fit.bank_a) {
        append_row(
            output,
            {"group_mean", std::string(coordinate), "A",
             action.descriptor, real_bits(action.value)});
    }
    for (const auto& action : cross_fit.bank_b) {
        append_row(
            output,
            {"group_mean", std::string(coordinate), "B",
             action.descriptor, real_bits(action.value)});
    }
    for (const std::string& descriptor :
         cross_fit.support_a) {
        append_row(
            output,
            {"group_support", std::string(coordinate), "A",
             descriptor});
    }
    for (const std::string& descriptor :
         cross_fit.support_b) {
        append_row(
            output,
            {"group_support", std::string(coordinate), "B",
             descriptor});
    }
    append_row(
        output,
        {"group_cross_fit", std::string(coordinate),
         real_bits(cross_fit.a_selected_b_value),
         real_bits(cross_fit.b_selected_a_value),
         real_bits(cross_fit.value)});
}

void append_bank_rows(
    std::string& output, std::string_view coordinate,
    const GroupBankEvidence& bank) {
    append_row(
        output,
        {"group_bank", std::string(coordinate), bank.bank,
         bank.stream_key, std::to_string(bank.actions.size())});
    for (const GroupActionEvidence& action : bank.actions) {
        const std::string action_coordinate =
            std::string(coordinate) + "/" + bank.bank + "/" +
            action.descriptor;
        append_action_fields(
            output, "group_action", action_coordinate,
            action.descriptor,
            action.action);
        append_row(
            output,
            {"feature_link", action_coordinate,
             action.feature_row_id,
             action.canonical_consequence_fingerprint});
        for (std::size_t feature = 0;
             feature < action.policy_features.size();
             ++feature) {
            append_row(
                output,
                {"policy_feature", action_coordinate,
                 std::to_string(feature),
                 real_bits(
                     action.policy_features[feature])});
        }
        for (const LeafSampleEvidence& sample :
             action.samples) {
            append_row(
                output,
                {"leaf", action_coordinate,
                 std::to_string(sample.world_index),
                 std::to_string(
                     sample.determinization_seed),
                 std::to_string(sample.macro_seed),
                 real_bits(std::bit_cast<double>(
                     sample.score_bits)),
                 sample.redacted_leaf_hash,
                 bool_text(sample.terminal),
                 bool_text(sample.forced_action_applied),
                 bool_text(sample.critic_evaluated),
                 bool_text(
                     sample
                         .contextual_legacy_critic_bit_identical),
                 real_bits(std::bit_cast<double>(
                     sample.contextual_score_bits)),
                 real_bits(std::bit_cast<double>(
                     sample.legacy_score_bits)),
                 std::to_string(sample.actions_applied),
                 std::to_string(
                     sample.priority_actions_applied),
                 std::to_string(
                     sample.phase_transitions),
                 std::to_string(sample.turn_advances)});
        }
    }
}

std::string roots_section(const RunReport& report) {
    std::string output;
    for (const RootEvidence& root :
         report.scientific.roots) {
        append_row(
            output,
            {"root", root.stable_id,
             root.manifest_information_action_fingerprint,
             std::to_string(
                 static_cast<std::size_t>(root.root_deck)),
             std::to_string(root.root_player),
             bool_text(
                 root.hidden_repartition_bit_identical),
             bool_text(root.descriptor_order_bit_identical),
             bool_text(root.complete)});
        append_identity_witness(
            output, "root_hidden_repartition",
            root.stable_id,
            root.hidden_repartition_witness);
        append_identity_witness(
            output, "root_descriptor_order",
            root.stable_id,
            root.descriptor_order_witness);
        for (const std::string& descriptor :
             root.exact_support) {
            append_row(
                output,
                {"root_support", root.stable_id, descriptor});
        }
        for (const RootActionEvidence& action :
             root.actions) {
            const std::string action_coordinate =
                root.stable_id + "/" + action.descriptor;
            append_action_fields(
                output, "root_action", action_coordinate,
                action.descriptor,
                action.action);
            append_row(
                output,
                {"feature_link", action_coordinate,
                 action.feature_row_id});
            append_row(
                output,
                {"root_target", action_coordinate,
                 real_bits(action.target.full),
                 action.canonical_consequence_fingerprint,
                 bool_text(action.complete)});
            for (std::size_t block = 0; block < kBlocks;
                 ++block) {
                append_row(
                    output,
                    {"root_target_block", action_coordinate,
                     std::to_string(block),
                     real_bits(action.target.blocks[block])});
            }
            for (std::size_t feature = 0;
                 feature < action.policy_features.size();
                 ++feature) {
                append_row(
                    output,
                    {"policy_feature", action_coordinate,
                     std::to_string(feature),
                     real_bits(
                         action.policy_features[feature])});
            }
            for (const RootTransitionParticleEvidence& transition :
                 action.root_transitions) {
                append_row(
                    output,
                    {"root_transition", action_coordinate,
                     std::to_string(transition.world_index),
                     std::to_string(
                         transition.determinization_seed),
                     std::to_string(transition.macro_seed),
                     transition.redacted_result_hash,
                     bool_text(transition.terminal),
                     real_bits(std::bit_cast<double>(
                         transition
                             .terminal_root_owner_value_bits)),
                     transition
                         .successor_information_set_fingerprint,
                     std::to_string(
                         transition.successor_owner),
                     bool_text(
                         transition
                             .forced_root_action_applied),
                     bool_text(
                         transition.successful_disposition),
                     std::to_string(
                         transition.actions_applied),
                     std::to_string(
                         transition
                             .priority_actions_applied),
                     std::to_string(
                         transition.phase_transitions),
                     std::to_string(
                         transition.turn_advances)});
            }
            for (std::size_t scope_index = 0;
                 scope_index < action.scopes.size();
                 ++scope_index) {
                const ScopeEvidence& scope =
                    action.scopes[scope_index];
                const std::string scope_coordinate =
                    action_coordinate + "/scope/" +
                    std::to_string(scope_index);
                append_row(
                    output,
                    {"scope", scope_coordinate,
                     std::to_string(
                         static_cast<std::size_t>(
                             scope.kind)),
                     std::to_string(scope.block),
                     real_bits(scope.target.value),
                     std::to_string(scope.target.particles),
                     std::to_string(
                         scope.target.terminal_particles),
                     std::to_string(
                         scope.target.same_owner_particles),
                     std::to_string(
                         scope.target.opponent_owner_particles),
                     bool_text(
                         scope.exact_particle_partition),
                     bool_text(scope.complete)});
                for (const std::size_t world :
                     scope.root_world_indices) {
                    append_row(
                        output,
                        {"scope_world", scope_coordinate,
                         std::to_string(world)});
                }
                for (const auto& terminal :
                     scope.terminals) {
                    append_row(
                        output,
                        {"terminal", scope_coordinate,
                         std::to_string(
                             terminal.world_index),
                         real_bits(
                             terminal.root_owner_value)});
                }
                for (std::size_t group_index = 0;
                     group_index < scope.groups.size();
                     ++group_index) {
                    const SuccessorGroupEvidence& group =
                        scope.groups[group_index];
                    const std::string group_coordinate =
                        scope_coordinate + "/group/" +
                        std::to_string(group_index);
                    append_row(
                        output,
                        {"successor_group", group_coordinate,
                         group.information_set_fingerprint,
                         std::to_string(group.successor_owner),
                         std::to_string(
                             static_cast<std::size_t>(
                                 group.relation)),
                         std::to_string(
                             group
                                 .representative_root_world),
                         group
                             .representative_root_action_descriptor,
                         bool_text(
                             group
                                 .every_representative_reconstructs),
                         bool_text(
                             group
                                 .hidden_repartition_eligible),
                         bool_text(
                             group.hidden_identity_changed),
                         bool_text(
                             group
                                 .hidden_repartition_invariant),
                         bool_text(group.complete)});
                    for (const std::size_t world :
                         group.root_world_indices) {
                        append_row(
                            output,
                            {"group_root_world",
                             group_coordinate,
                             std::to_string(world)});
                    }
                    for (std::size_t witness = 0;
                         witness <
                         group
                             .representative_reconstruction_witnesses
                             .size();
                         ++witness) {
                        append_identity_witness(
                            output,
                            "successor_reconstruction",
                            group_coordinate + "/" +
                                std::to_string(witness),
                            group
                                .representative_reconstruction_witnesses
                                    [witness]);
                    }
                    append_identity_witness(
                        output,
                        "successor_hidden_repartition",
                        group_coordinate,
                        group
                            .hidden_repartition_witness);
                    append_bank_rows(
                        output, group_coordinate,
                        group.bank_a);
                    append_bank_rows(
                        output, group_coordinate,
                        group.bank_b);
                    append_cross_fit_rows(
                        output, group_coordinate,
                        group.cross_fit);
                }
            }
        }
    }
    for (const SuccessorFeatureEvaluationEvidence&
             evaluation :
         report.scientific.successor_feature_evaluations) {
        const std::string evaluation_coordinate =
            evaluation.root_stable_id + "/" +
            evaluation.information_set_fingerprint +
            "/feature-evaluation";
        append_row(
            output,
            {"successor_feature_evaluation",
             evaluation_coordinate,
             evaluation.root_stable_id,
             evaluation.information_set_fingerprint,
             std::to_string(evaluation.successor_owner),
             std::to_string(
                 evaluation.representative_root_world),
             evaluation
                 .representative_root_action_descriptor,
             bool_text(evaluation.complete)});
        for (std::size_t scope_index = 0;
             scope_index < evaluation.scopes.size();
             ++scope_index) {
            const SuccessorFeatureScopeEvidence& scope =
                evaluation.scopes[scope_index];
            const std::string scope_coordinate =
                evaluation.root_stable_id + "/" +
                evaluation
                    .information_set_fingerprint +
                "/feature-scope/" +
                std::to_string(scope_index);
            append_row(
                output,
                {"successor_feature_scope",
                 scope_coordinate,
                 std::to_string(
                     static_cast<std::size_t>(
                         scope.kind)),
                 std::to_string(scope.block),
                 bool_text(
                     scope
                         .every_representative_reconstructs),
                 bool_text(
                     scope.hidden_repartition_eligible),
                 bool_text(scope.hidden_identity_changed),
                 bool_text(
                     scope.hidden_repartition_invariant),
                 bool_text(scope.complete)});
            for (std::size_t member = 0;
                 member <
                 scope.representative_catalog.size();
                 ++member) {
                const auto& representative =
                    scope.representative_catalog[member];
                const std::string member_coordinate =
                    scope_coordinate + "/member/" +
                    std::to_string(
                        representative.root_world) +
                    "/" +
                    representative
                        .root_action_descriptor;
                append_row(
                    output,
                    {"successor_feature_representative",
                     scope_coordinate,
                     std::to_string(
                         representative.root_world),
                     representative
                         .root_action_descriptor});
                append_identity_witness(
                    output,
                    "successor_feature_reconstruction",
                    member_coordinate,
                    scope
                        .representative_reconstruction_witnesses
                            [member]);
            }
            append_identity_witness(
                output,
                "successor_feature_hidden_repartition",
                scope_coordinate,
                scope.hidden_repartition_witness);
            append_bank_rows(
                output, scope_coordinate, scope.bank_a);
            append_bank_rows(
                output, scope_coordinate, scope.bank_b);
        }
    }
    const RankingSummaryEvidence& ranking =
        report.scientific.c16_ranking_changes;
    append_row(
        output,
        {"c16_ranking_summary",
         real_bits(std::bit_cast<double>(
             ranking
                 .equal_deck_pairwise_change_fraction_bits)),
         real_bits(std::bit_cast<double>(
             ranking
                 .equal_deck_support_changed_fraction_bits)),
         bool_text(ranking.complete)});
    for (const C16RootRankingEvidence& root :
         ranking.roots) {
        append_row(
            output,
            {"c16_root_ranking", root.stable_id,
             real_bits(std::bit_cast<double>(
                 root.pairwise_change_fraction_bits)),
             bool_text(root.support_changed)});
        for (const C16ActionRankingEvidence& action :
             root.actions) {
            append_row(
                output,
                {"c16_action_score", root.stable_id,
                 action.descriptor,
                 real_bits(std::bit_cast<double>(
                     action.score_bits))});
        }
        for (const std::string& descriptor :
             root.exact_support) {
            append_row(
                output,
                {"c16_root_support", root.stable_id,
                 descriptor});
        }
    }
    for (const RankingDeckSummaryEvidence& deck :
         ranking.decks) {
        append_row(
            output,
            {"c16_deck_summary",
             std::to_string(
                 static_cast<std::size_t>(deck.deck)),
             std::to_string(deck.roots),
             std::to_string(
                 deck.support_changed_roots),
             real_bits(std::bit_cast<double>(
                 deck
                     .mean_pairwise_change_fraction_bits)),
             real_bits(std::bit_cast<double>(
                 deck
                     .support_changed_fraction_bits))});
    }
    return output;
}

std::string contrasts_section(const RunReport& report) {
    std::string output;
    for (const ContrastEvidence& evidence :
         report.scientific.contrasts) {
        const auto& contrast = evidence.contrast;
        append_row(
            output,
            {"contrast",
             std::to_string(
                 static_cast<std::size_t>(
                     evidence.role)),
             evidence.name, evidence.stable_id,
             evidence.positive_descriptor,
             evidence.negative_descriptor,
             real_bits(contrast.delta64),
             real_bits(contrast.block_mean),
             real_bits(
                 contrast.sample_standard_deviation),
             real_bits(contrast.lower_95),
             std::to_string(contrast.positive_blocks),
             std::to_string(
                 contrast.nonnegative_blocks),
             bool_text(evidence.support_condition),
             bool_text(evidence.directional_passed)});
        for (std::size_t block = 0; block < kBlocks;
             ++block) {
            append_row(
                output,
                {"contrast_block", evidence.name,
                 std::to_string(block),
                 real_bits(contrast.block_deltas[block])});
        }
    }
    return output;
}

std::string dominance_section(const RunReport& report) {
    std::string output;
    for (const DominancePairEvidence& pair :
         report.scientific.dominance_pairs) {
        append_row(
            output,
            {"dominance_pair",
             std::to_string(
                 static_cast<std::size_t>(pair.role)),
             pair.stable_id,
             pair.first_descriptor,
             pair.second_descriptor,
             std::to_string(
                 static_cast<std::size_t>(
                     pair.required_orientation)),
             std::to_string(pair.required_worlds),
             std::to_string(pair.matching_worlds),
             bool_text(pair.passed)});
        for (const DominanceWorldEvidence& world :
             pair.worlds) {
            const std::string world_coordinate =
                pair.stable_id + "/" +
                pair.first_descriptor + "/" +
                pair.second_descriptor + "/" +
                std::to_string(world.world_index);
            append_row(
                output,
                {"dominance_world", pair.stable_id,
                 std::to_string(world.world_index),
                 std::to_string(
                     world.determinization_seed),
                 world.common_world_key,
                 std::to_string(
                     static_cast<std::size_t>(
                         world.orientation)),
                 bool_text(
                     world
                         .hidden_repartition_bit_identical)});
            const fq0_dominance::Comparison& comparison =
                world.comparison;
            append_row(
                output,
                {"dominance_comparison", world_coordinate,
                 bool_text(
                     comparison.root_information_equal),
                 bool_text(comparison.first_normalized),
                 bool_text(comparison.second_normalized),
                 bool_text(comparison.consequences_equal),
                 bool_text(comparison.first.valid),
                 bool_text(comparison.second.valid),
                 artifact_integrity::sha256_string(
                     comparison.first
                         .owner_observable_consequence),
                 artifact_integrity::sha256_string(
                     comparison.second
                         .owner_observable_consequence),
                 std::to_string(
                     static_cast<std::size_t>(
                         comparison.orientation))});
            const auto append_cost =
                [&](std::string_view branch,
                    std::size_t player,
                    const fq0_dominance::
                        CanonicalPlayerResourceCost&
                            cost) {
                    append_row(
                        output,
                        {"dominance_cost",
                         world_coordinate,
                         std::string(branch),
                         std::to_string(player),
                         std::to_string(
                             cost.mana_depleted.generic),
                         std::to_string(
                             cost.mana_depleted.green),
                         std::to_string(
                             cost.mana_depleted.red),
                         std::to_string(
                             cost.mana_depleted.blue),
                         std::to_string(
                             cost.mana_depleted.white),
                         bool_text(
                             cost
                                 .land_play_entitlement_consumed)});
                    for (std::size_t card = 0;
                         card < kCardCount; ++card) {
                        if (cost.hand_cards_consumed[card] !=
                                0 ||
                            cost.lands_newly_tapped[card] !=
                                0 ||
                            cost.artifacts_newly_tapped[card] !=
                                0) {
                            append_row(
                                output,
                                {"dominance_cost_card",
                                 world_coordinate,
                                 std::string(branch),
                                 std::to_string(player),
                                 std::to_string(card),
                                 std::to_string(
                                     cost
                                         .hand_cards_consumed
                                             [card]),
                                 std::to_string(
                                     cost
                                         .lands_newly_tapped
                                             [card]),
                                 std::to_string(
                                     cost
                                         .artifacts_newly_tapped
                                             [card])});
                        }
                    }
                };
            for (std::size_t player = 0; player < 2;
                 ++player) {
                append_cost(
                    "first", player,
                    comparison.first.costs[player]);
                append_cost(
                    "second", player,
                    comparison.second.costs[player]);
            }
            append_identity_witness(
                output, "dominance_hidden_repartition",
                world_coordinate,
                world.hidden_repartition_witness);
        }
    }
    return output;
}

std::string collisions_section(const RunReport& report) {
    std::string output;
    for (const fq0_bellman::FeatureTargetRow& row :
         report.scientific.feature_rows) {
        append_row(
            output,
            {"feature_row", row.row_id,
             row.information_set_id, row.legal_set_id,
             row.common_world_key,
             row.action_descriptor,
             row.canonical_consequence_fingerprint,
             real_bits(row.target.full),
             bool_text(row.unique_exact_max)});
        for (std::size_t feature = 0;
             feature < row.features.size(); ++feature) {
            append_row(
                output,
                {"feature_row_value", row.row_id,
                 std::to_string(feature),
                 real_bits(row.features[feature])});
        }
        for (std::size_t block = 0; block < kBlocks;
             ++block) {
            append_row(
                output,
                {"feature_row_block", row.row_id,
                 std::to_string(block),
                 real_bits(row.target.blocks[block])});
        }
    }
    const auto& analysis =
        report.scientific.feature_collisions;
    append_row(
        output,
        {"collision_census", std::to_string(analysis.rows),
         std::to_string(
             analysis.colliding_feature_classes),
         std::to_string(analysis.collisions.size()),
         std::to_string(analysis.harmful_collisions),
         bool_text(analysis.passed)});
    for (const auto& collision : analysis.collisions) {
        append_row(
            output,
            {"collision", collision.first_row_id,
             collision.second_row_id,
             std::to_string(
                 static_cast<std::size_t>(
                     collision.target_method)),
             real_bits(
                 collision.target_separation_lower_95),
             bool_text(collision.consequence_conflict),
             bool_text(collision.target_conflict),
             bool_text(collision.support_conflict),
             bool_text(collision.harmful)});
    }
    return output;
}

void append_snapshot(
    std::string& output, std::string_view name,
    const artifact_integrity::RegularFileSnapshot& snapshot) {
    append_row(
        output,
        {"snapshot", std::string(name), "bytes",
         std::to_string(snapshot.byte_size)});
    append_row(
        output,
        {"snapshot", std::string(name), "sha256",
         snapshot.sha256});
}

void append_components(
    std::string& output, std::string_view name,
    const LearnedModelComponentFingerprints& components) {
    append_row(
        output,
        {"model_component", std::string(name), "critic",
         components.critic});
    append_row(
        output,
        {"model_component", std::string(name), "priority",
         components.priority});
    append_row(
        output,
        {"model_component", std::string(name), "attack",
         components.attack});
    append_row(
        output,
        {"model_component", std::string(name), "block",
         components.block});
    append_row(
        output,
        {"model_component", std::string(name),
         "damage_order", components.damage_order});
}

std::string integrity_section(const RunReport& report) {
    std::string output;
    append_snapshot(
        output, "model_before",
        report.integrity.model_before);
    append_snapshot(
        output, "model_after",
        report.integrity.model_after);
    append_components(
        output, "model_before",
        report.integrity.model_components_before);
    append_components(
        output, "model_after",
        report.integrity.model_components_after);
    append_row(
        output,
        {"integrity", "artifact_requirement_matched",
         bool_text(
             report.integrity.artifact_requirement_matched)});
    append_row(
        output,
        {"integrity", "artifact_unchanged",
         bool_text(report.integrity.artifact_unchanged)});
    append_row(
        output,
        {"integrity", "model_identity_matched",
         bool_text(report.integrity.model_identity_matched)});
    append_row(
        output,
        {"integrity", "passed",
         bool_text(report.integrity.passed)});
    for (const std::string& failure :
         report.gate.failures) {
        append_row(output, {"gate_failure", failure});
    }
    return output;
}

std::string invariance_section(const RunReport& report) {
    const InvarianceEvidence& value =
        report.scientific.invariance;
    std::string output;
    append_identity_witness(
        output, "independent_manifest", "global",
        value.independent_manifest_witness);
    append_identity_witness(
        output, "repeated_construction", "global",
        value.repeated_construction_witness);
    append_identity_witness(
        output, "descriptor_order", "global",
        value.descriptor_order_witness);
    append_identity_witness(
        output, "thread_count", "global",
        value.thread_count_witness);
    append_identity_witness(
        output, "hidden_repartition", "global",
        value.hidden_repartition_witness);
    append_identity_witness(
        output, "contextual_legacy_critic", "global",
        value.contextual_legacy_critic_witness);
    append_row(
        output,
        {"invariance", "independent_manifest",
         bool_text(
             value.independent_manifest_bit_identical)});
    append_row(
        output,
        {"invariance", "repeated_construction",
         bool_text(
             value.repeated_construction_bit_identical)});
    append_row(
        output,
        {"invariance", "descriptor_order",
         bool_text(value.descriptor_order_bit_identical)});
    append_row(
        output,
        {"invariance", "thread_count",
         bool_text(value.thread_count_bit_identical)});
    append_row(
        output,
        {"invariance", "hidden_repartition",
         bool_text(
             value.hidden_repartition_bit_identical)});
    append_row(
        output,
        {"invariance", "contextual_legacy_critic",
         bool_text(
             value
                 .contextual_legacy_critic_bit_identical)});
    append_row(
        output,
        {"invariance", "passed", bool_text(value.passed)});
    append_row(
        output,
        {"scientific", "primary_passed",
         bool_text(report.scientific.primary_passed)});
    append_row(
        output,
        {"scientific", "reject_only_guards_passed",
         bool_text(
             report.scientific
                 .reject_only_guards_passed)});
    append_row(
        output,
        {"scientific", "complete",
         bool_text(report.scientific.complete)});
    append_row(
        output,
        {"scientific", "passed",
         bool_text(report.scientific.passed)});
    return output;
}

EvidenceBundle serialize_impl(const RunReport& report) {
    validate_report(report);
    EvidenceBundle bundle;
    append_row(
        bundle.bytes,
        {"schema", std::string(kEvidenceSchema)});
    const std::array<std::pair<std::string_view, std::string>,
                     kSectionNames.size()>
        sections = {{
            {"metadata", metadata_section(report)},
            {"manifest", manifest_section(report)},
            {"roots", roots_section(report)},
            {"contrasts", contrasts_section(report)},
            {"dominance", dominance_section(report)},
            {"collisions", collisions_section(report)},
            {"integrity", integrity_section(report)},
            {"invariance", invariance_section(report)},
        }};
    for (const auto& [name, bytes] : sections) {
        bundle.section_names.emplace_back(name);
        bundle.section_sha256.push_back(
            artifact_integrity::sha256_string(bytes));
        append_row(
            bundle.bytes,
            {"section_begin", std::string(name)});
        bundle.bytes += bytes;
        append_row(
            bundle.bytes,
            {"section_sha256", std::string(name),
             bundle.section_sha256.back()});
    }
    bundle.payload_sha256 =
        artifact_integrity::sha256_string(bundle.bytes);
    append_row(
        bundle.bytes,
        {"payload_sha256", bundle.payload_sha256});
    bundle.complete_sha256 =
        artifact_integrity::sha256_string(bundle.bytes);
    append_row(
        bundle.bytes,
        {"complete_sha256", bundle.complete_sha256});
    return bundle;
}

std::string_view read_line(
    std::string_view bytes, std::size_t& cursor,
    std::size_t* line_start = nullptr) {
    if (line_start != nullptr) {
        *line_start = cursor;
    }
    require(
        cursor < bytes.size(),
        "FQ0 evidence ended before its required footer");
    const std::size_t newline = bytes.find('\n', cursor);
    require(
        newline != std::string_view::npos,
        "FQ0 evidence has a non-terminated line");
    const std::string_view line =
        bytes.substr(cursor, newline - cursor);
    require(
        line.find('\r') == std::string_view::npos &&
            line.find('\0') == std::string_view::npos,
        "FQ0 evidence contains a forbidden byte");
    cursor = newline + 1;
    return line;
}

std::vector<std::string_view> split_tabs(
    std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t cursor = 0;
    while (true) {
        const std::size_t tab = line.find('\t', cursor);
        if (tab == std::string_view::npos) {
            fields.push_back(line.substr(cursor));
            break;
        }
        fields.push_back(line.substr(cursor, tab - cursor));
        cursor = tab + 1;
    }
    return fields;
}

void require_exact_row(
    std::string_view line,
    std::initializer_list<std::string_view> expected) {
    const std::vector<std::string_view> fields =
        split_tabs(line);
    require(
        fields.size() == expected.size() &&
            std::equal(
                fields.begin(), fields.end(),
                expected.begin(), expected.end()),
        "FQ0 evidence framing row is malformed");
}

EvidenceBundle validate_bundle_impl(std::string_view bytes) {
    require(
        !bytes.empty(),
        "FQ0 evidence bundle is empty");
    EvidenceBundle result;
    result.bytes = std::string(bytes);
    std::size_t cursor = 0;
    require_exact_row(
        read_line(bytes, cursor),
        {"schema", kEvidenceSchema});
    for (const std::string_view section : kSectionNames) {
        require_exact_row(
            read_line(bytes, cursor),
            {"section_begin", section});
        const std::size_t section_start = cursor;
        std::size_t footer_start = 0;
        std::string_view digest;
        while (true) {
            const std::string_view line =
                read_line(bytes, cursor, &footer_start);
            const std::vector<std::string_view> fields =
                split_tabs(line);
            if (fields.size() == 3 &&
                fields[0] == "section_sha256") {
                require(
                    fields[1] == section &&
                        audit_common::is_lower_hex_digest(
                            fields[2]),
                    "FQ0 evidence section footer is malformed");
                digest = fields[2];
                break;
            }
            require(
                fields.empty() ||
                    fields[0] != "section_begin",
                "FQ0 evidence section is missing its hash");
        }
        const std::string_view section_bytes =
            bytes.substr(
                section_start, footer_start - section_start);
        require(
            artifact_integrity::sha256_string(
                section_bytes) == digest,
            "FQ0 evidence section hash mismatch");
        result.section_names.emplace_back(section);
        result.section_sha256.emplace_back(digest);
    }

    std::size_t payload_start = 0;
    const std::vector<std::string_view> payload_fields =
        split_tabs(
            read_line(bytes, cursor, &payload_start));
    require(
        payload_fields.size() == 2 &&
            payload_fields[0] == "payload_sha256" &&
            audit_common::is_lower_hex_digest(
                payload_fields[1]) &&
            artifact_integrity::sha256_string(
                bytes.substr(0, payload_start)) ==
                payload_fields[1],
        "FQ0 evidence payload hash mismatch");
    result.payload_sha256 =
        std::string(payload_fields[1]);

    std::size_t complete_start = 0;
    const std::vector<std::string_view> complete_fields =
        split_tabs(
            read_line(bytes, cursor, &complete_start));
    require(
        complete_fields.size() == 2 &&
            complete_fields[0] == "complete_sha256" &&
            audit_common::is_lower_hex_digest(
                complete_fields[1]) &&
            artifact_integrity::sha256_string(
                bytes.substr(0, complete_start)) ==
                complete_fields[1],
        "FQ0 evidence complete hash mismatch");
    result.complete_sha256 =
        std::string(complete_fields[1]);
    require(
        cursor == bytes.size(),
        "FQ0 evidence has trailing data");
    return result;
}

bool contains_exact_row(
    std::string_view bytes,
    std::initializer_list<std::string_view> fields) {
    std::string expected;
    bool first = true;
    for (const std::string_view field : fields) {
        if (!first) {
            expected.push_back('\t');
        }
        first = false;
        expected.append(field);
    }
    expected.push_back('\n');
    std::size_t count = 0;
    std::size_t cursor = 0;
    while ((cursor = bytes.find(expected, cursor)) !=
           std::string_view::npos) {
        const bool at_row_start =
            cursor == 0 || bytes[cursor - 1] == '\n';
        if (at_row_start) {
            ++count;
        }
        cursor += expected.size();
    }
    return count == 1;
}

bool path_exists_at(
    int directory_descriptor, std::string_view name) {
    struct stat status {};
    if (::fstatat(
            directory_descriptor,
            std::string(name).c_str(), &status,
            AT_SYMLINK_NOFOLLOW) == 0) {
        return true;
    }
    if (errno == ENOENT) {
        return false;
    }
    throw std::runtime_error(
        "cannot inspect FQ0 evidence destination: " +
        std::string(std::strerror(errno)));
}

int open_or_create_verified_parent(
    const std::filesystem::path& parent) {
    std::error_code absolute_error;
    const std::filesystem::path absolute =
        std::filesystem::absolute(parent, absolute_error)
            .lexically_normal();
    require(
        !absolute_error,
        "cannot resolve FQ0 evidence parent path");
    int flags = O_RDONLY | O_CLOEXEC;
#ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int descriptor = ::open(
        absolute.root_path().c_str(), flags);
    if (descriptor < 0) {
        throw std::runtime_error(
            "cannot open FQ0 evidence filesystem root: " +
            std::string(std::strerror(errno)));
    }
    for (const std::filesystem::path& component :
         absolute.relative_path()) {
        const std::string name = component.string();
        if (name.empty() || name == ".") {
            continue;
        }
        if (name == "..") {
            static_cast<void>(::close(descriptor));
            throw std::runtime_error(
                "FQ0 evidence parent escaped its absolute root");
        }
        struct stat status {};
        if (::fstatat(
                descriptor, name.c_str(), &status,
                AT_SYMLINK_NOFOLLOW) != 0) {
            if (errno != ENOENT ||
                ::mkdirat(
                    descriptor, name.c_str(), 0755) != 0 ||
                ::fstatat(
                    descriptor, name.c_str(), &status,
                    AT_SYMLINK_NOFOLLOW) != 0) {
                const std::string detail =
                    std::strerror(errno);
                static_cast<void>(::close(descriptor));
                throw std::runtime_error(
                    "cannot create or inspect FQ0 evidence "
                    "parent component: " +
                    detail);
            }
        }
        if (!S_ISDIR(status.st_mode) ||
            S_ISLNK(status.st_mode)) {
            static_cast<void>(::close(descriptor));
            throw std::runtime_error(
                "FQ0 evidence parent or an ancestor is a "
                "symlink or non-directory");
        }
        const int next =
            ::openat(descriptor, name.c_str(), flags);
        if (next < 0) {
            const std::string detail = std::strerror(errno);
            static_cast<void>(::close(descriptor));
            throw std::runtime_error(
                "cannot open FQ0 evidence parent "
                "component: " +
                detail);
        }
        static_cast<void>(::close(descriptor));
        descriptor = next;
    }
    return descriptor;
}

void write_atomic_impl(
    std::string_view path_text, std::string_view bytes,
    const std::function<void()>& before_commit) {
    require(
        !path_text.empty() && !bytes.empty() &&
            path_text.find('\0') == std::string_view::npos,
        "FQ0 evidence path/bytes are invalid");
    const std::filesystem::path target{
        std::string(path_text)};
    require(
        !target.filename().empty() &&
            target.filename() != "." &&
            target.filename() != "..",
        "FQ0 evidence path must name a file");
    const std::filesystem::path parent =
        target.has_parent_path()
            ? target.parent_path()
            : std::filesystem::path(".");
    int directory_descriptor =
        open_or_create_verified_parent(parent);
    const std::string filename =
        target.filename().string();
    const std::string temporary = filename + ".tmp";
    const auto close_directory = [&] {
        if (directory_descriptor >= 0) {
            static_cast<void>(
                ::close(directory_descriptor));
            directory_descriptor = -1;
        }
    };
    try {
        require(
            !path_exists_at(
                directory_descriptor, filename) &&
                !path_exists_at(
                    directory_descriptor, temporary),
            "FQ0 evidence destination or temporary exists");
    } catch (...) {
        close_directory();
        throw;
    }

    int flags = O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int descriptor = ::openat(
        directory_descriptor, temporary.c_str(),
        flags, 0644);
    if (descriptor < 0) {
        const std::string detail = std::strerror(errno);
        close_directory();
        throw std::runtime_error(
            "cannot create FQ0 evidence temporary: " +
            detail);
    }
    const auto clean_temporary = [&] {
        if (descriptor >= 0) {
            static_cast<void>(::close(descriptor));
            descriptor = -1;
        }
        static_cast<void>(::unlinkat(
            directory_descriptor, temporary.c_str(), 0));
    };
    try {
        std::size_t cursor = 0;
        while (cursor < bytes.size()) {
            const ssize_t written = ::write(
                descriptor, bytes.data() + cursor,
                bytes.size() - cursor);
            if (written < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error(
                    "cannot write FQ0 evidence temporary: " +
                    std::string(std::strerror(errno)));
            }
            require(
                written != 0,
                "FQ0 evidence write made no progress");
            cursor += static_cast<std::size_t>(written);
        }
        require(
            ::fsync(descriptor) == 0,
            "cannot sync FQ0 evidence temporary");
        if (::close(descriptor) != 0) {
            descriptor = -1;
            throw std::runtime_error(
                "cannot close FQ0 evidence temporary");
        }
        descriptor = -1;
        before_commit();
        require(
            !path_exists_at(
                directory_descriptor, filename),
            "FQ0 evidence destination appeared before commit");
        if (::linkat(
                directory_descriptor, temporary.c_str(),
                directory_descriptor, filename.c_str(),
                0) != 0) {
            throw std::runtime_error(
                "cannot atomically publish FQ0 evidence: " +
                std::string(std::strerror(errno)));
        }
    } catch (...) {
        clean_temporary();
        close_directory();
        throw;
    }
    // The hard link is the no-replace commit point. Durability and temporary
    // cleanup are best effort after a complete target becomes visible.
    static_cast<void>(::unlinkat(
        directory_descriptor, temporary.c_str(), 0));
    static_cast<void>(::fsync(directory_descriptor));
    close_directory();
}

} // namespace

namespace binding {

std::string root_feature_information_set_id(
    std::string_view root_stable_id,
    std::string_view manifest_information_action_fingerprint) {
    return scoped_root_information_set(
        root_stable_id,
        manifest_information_action_fingerprint);
}

std::string successor_feature_information_set_id(
    std::string_view root_stable_id,
    std::string_view successor_information_set_fingerprint) {
    return scoped_successor_information_set(
        root_stable_id,
        successor_information_set_fingerprint);
}

std::string dominance_common_world_key(
    std::string_view root_stable_id,
    std::string_view manifest_information_action_fingerprint,
    std::size_t world) {
    return "fq0-dominance/" +
           std::string(root_stable_id) + "/" +
           std::string(
               manifest_information_action_fingerprint) +
           "/world-" + std::to_string(world);
}

std::string hidden_repartition_coordinate(
    std::string_view parent_coordinate,
    std::size_t representative_root_world,
    std::string_view representative_root_action_descriptor) {
    return std::string(parent_coordinate) +
           "/hidden/member/" +
           std::to_string(representative_root_world) + "/" +
           std::string(
               representative_root_action_descriptor);
}

std::string manifest_payload_sha256(
    const ac1_teacher_audit::Manifest& manifest) {
    return manifest_payload_sha256_impl(manifest);
}

std::string root_payload_sha256(
    const RootEvidence& root) {
    return root_payload_sha256_impl(root);
}

std::string successor_bank_pair_payload_sha256(
    const GroupBankEvidence& bank_a,
    const GroupBankEvidence& bank_b,
    const fq0_bellman::CrossFitValue& cross_fit) {
    return successor_bank_pair_payload_sha256_impl(
        bank_a, bank_b, cross_fit);
}

std::string successor_operator_bank_pair_payload_sha256(
    const GroupBankEvidence& bank_a,
    const GroupBankEvidence& bank_b,
    const fq0_bellman::CrossFitValue& cross_fit) {
    return
        successor_operator_bank_pair_payload_sha256_impl(
            bank_a, bank_b, cross_fit);
}

std::string group_bank_pair_payload_sha256(
    std::string_view root_stable_id,
    std::string_view root_action_descriptor,
    ScopeKind kind, std::size_t block,
    const SuccessorGroupEvidence& group) {
    return group_bank_pair_payload_sha256_impl(
        root_stable_id, root_action_descriptor, kind,
        block, group);
}

std::string group_representative_payload_sha256(
    std::string_view root_stable_id,
    std::string_view root_action_descriptor,
    ScopeKind kind, std::size_t block,
    const SuccessorGroupEvidence& group,
    std::size_t member_root_world) {
    return group_representative_payload_sha256_impl(
        root_stable_id, root_action_descriptor, kind,
        block, group, member_root_world);
}

std::string successor_feature_scope_payload_sha256(
    const SuccessorFeatureEvaluationEvidence& evaluation,
    const SuccessorFeatureScopeEvidence& scope) {
    return successor_feature_scope_payload_sha256_impl(
        evaluation, scope);
}

std::string
successor_feature_scope_representative_payload_sha256(
    const SuccessorFeatureEvaluationEvidence& evaluation,
    const SuccessorFeatureScopeEvidence& scope,
    const SuccessorRepresentativeCoordinateEvidence&
        representative) {
    return successor_feature_scope_representative_payload_sha256_impl(
        evaluation, scope, representative);
}

std::string dominance_comparison_payload_sha256(
    std::string_view root_stable_id,
    std::string_view first_descriptor,
    std::string_view second_descriptor,
    const DominanceWorldEvidence& world) {
    return dominance_payload_sha256_impl(
        root_stable_id, first_descriptor,
        second_descriptor, world);
}

std::string scientific_core_payload_sha256(
    const ScientificEvidence& scientific) {
    return scientific_core_sha256_impl(scientific);
}

std::array<std::string, 2>
critic_stream_payload_sha256(
    const ScientificEvidence& scientific) {
    return critic_stream_sha256_impl(scientific);
}

BitIdentityEvidence make_witness(
    std::string domain, std::string coordinate,
    std::string baseline_sha256,
    std::string comparison_sha256) {
    return {
        .domain = std::move(domain),
        .coordinate = std::move(coordinate),
        .baseline_sha256 = std::move(baseline_sha256),
        .comparison_sha256 =
            std::move(comparison_sha256),
    };
}

} // namespace binding

GateReport evaluate_gate(
    const ScientificEvidence& scientific,
    const IntegrityEvidence& integrity) {
    GateReport gate{
        .integrity_passed =
            integrity.passed &&
            integrity_fields_pass(integrity),
        .complete_evidence = scientific.complete,
        .scientific_passed = scientific.passed,
    };
    const bool expected_scientific_pass =
        scientific.primary_passed &&
        scientific.reject_only_guards_passed &&
        scientific.feature_collisions.passed;
    if (!gate.integrity_passed) {
        gate.infrastructure_failure = true;
        gate.failures.push_back(
            "immutable model integrity failed");
    }
    if (!scientific.complete) {
        gate.infrastructure_failure = true;
        gate.failures.push_back(
            "expected complete FQ0 evidence is missing");
    }
    if (!invariance_fields_pass(scientific.invariance) ||
        !scientific.invariance.passed) {
        gate.infrastructure_failure = true;
        gate.failures.push_back(
            "manifest, repeat, thread, hidden, order, or "
            "critic invariance failed");
    }
    if (scientific.complete &&
        scientific.passed != expected_scientific_pass) {
        gate.infrastructure_failure = true;
        gate.failures.push_back(
            "scientific verdict is internally inconsistent");
    }
    if (!gate.infrastructure_failure &&
        !scientific.passed) {
        if (!scientific.primary_passed) {
            gate.failures.push_back(
                "primary Bellman/dominance gate failed");
        }
        if (!scientific.reject_only_guards_passed) {
            gate.failures.push_back(
                "one or more reject-only guards failed");
        }
        if (!scientific.feature_collisions.passed) {
            gate.failures.push_back(
                "harmful representation collision detected");
        }
    }
    gate.passed =
        !gate.infrastructure_failure &&
        scientific.passed && gate.failures.empty();
    return gate;
}

int exit_code(const GateReport& gate) {
    if (gate.infrastructure_failure ||
        !gate.integrity_passed ||
        !gate.complete_evidence) {
        return 2;
    }
    return gate.passed ? 0 : 1;
}

EvidencePublication publish_bundle_for_parent(
    std::string_view path, const EvidenceBundle& bundle,
    const artifact_integrity::RegularFileSnapshot& expected_model,
    std::string_view expected_model_fingerprint,
    std::string_view observed_model_fingerprint) {
    const EvidenceBundle validated =
        validate_bundle_impl(bundle.bytes);
    require(
        validated == bundle,
        "FQ0 evidence bundle fields disagree with its bytes");
    require(
        audit_common::is_lower_hex_digest(
            expected_model.sha256) &&
            audit_common::is_lower_hex_digest(
                expected_model_fingerprint) &&
            observed_model_fingerprint ==
                expected_model_fingerprint &&
            contains_exact_row(
                bundle.bytes,
                {"identity", "model_fingerprint",
                 observed_model_fingerprint}) &&
            contains_exact_row(
                bundle.bytes,
                {"snapshot", "model_before", "bytes",
                 std::to_string(expected_model.byte_size)}) &&
            contains_exact_row(
                bundle.bytes,
                {"snapshot", "model_before", "sha256",
                 expected_model.sha256}) &&
            contains_exact_row(
                bundle.bytes,
                {"snapshot", "model_after", "bytes",
                 std::to_string(expected_model.byte_size)}) &&
            contains_exact_row(
                bundle.bytes,
                {"snapshot", "model_after", "sha256",
                 expected_model.sha256}) &&
            contains_exact_row(
                bundle.bytes,
                {"integrity", "artifact_unchanged", "1"}) &&
            contains_exact_row(
                bundle.bytes,
                {"integrity", "model_identity_matched", "1"}) &&
            contains_exact_row(
                bundle.bytes,
                {"integrity", "passed", "1"}) &&
            contains_exact_row(
                bundle.bytes,
                {"scientific", "complete", "1"}) &&
            (contains_exact_row(
                 bundle.bytes,
                 {"verdict", "exit_code", "0"}) ||
             contains_exact_row(
                 bundle.bytes,
                 {"verdict", "exit_code", "1"})),
        "FQ0 evidence is not bound to the expected frozen model");
    const auto verify_parent = [&] {
        const auto current =
            artifact_integrity::snapshot_regular_file(
                expected_model.path);
        require(
            current == expected_model,
            "FQ0 frozen model changed before publication");
    };
    EvidencePublication publication{
        .path = std::string(path),
        .byte_size = bundle.bytes.size(),
        .sha256 =
            artifact_integrity::sha256_string(bundle.bytes),
        .payload_sha256 = bundle.payload_sha256,
        .atomic_no_replace = false,
        .published = false,
    };
    write_atomic_impl(path, bundle.bytes, verify_parent);
    // No allocation or throwing work follows the no-replace commit point.
    publication.atomic_no_replace = true;
    publication.published = true;
    return publication;
}

EvidencePublication publish_evidence_atomic_no_replace(
    const RunReport& report) {
    const EvidenceBundle bundle = serialize_impl(report);
    std::error_code path_error;
    const std::filesystem::path expected_path =
        std::filesystem::absolute(
            std::filesystem::path(kModelArtifactPath),
            path_error)
            .lexically_normal();
    require(
        !path_error &&
            std::filesystem::path(
                report.integrity.model_after.path)
                    .lexically_normal() ==
                expected_path &&
            report.scientific.model_fingerprint ==
                kModelFingerprint &&
            report.integrity.model_after.byte_size ==
                kModelArtifactBytes &&
            report.integrity.model_after.sha256 ==
                kModelArtifactSha256,
        "FQ0 publication is not bound to the fixed model path");
    return publish_bundle_for_parent(
        kEvidencePath, bundle,
        report.integrity.model_after,
        kModelFingerprint,
        report.scientific.model_fingerprint);
}

namespace testing {

EvidenceBundle serialize_evidence_bundle(
    const RunReport& report) {
    return serialize_impl(report);
}

EvidenceBundle validate_evidence_bundle(
    std::string_view bytes) {
    return validate_bundle_impl(bytes);
}

void write_evidence_atomic_no_replace(
    std::string_view path, std::string_view bytes) {
    write_atomic_impl(path, bytes, [] {});
}

void validate_seed_coordinate_ownership(
    const std::vector<
        std::pair<std::uint64_t, std::string>>& claims) {
    std::map<std::uint64_t, std::string> owners;
    for (const auto& [seed, coordinate] : claims) {
        claim_seed(owners, seed, coordinate);
    }
}

EvidencePublication publish_evidence_for_parent(
    std::string_view path, const EvidenceBundle& bundle,
    const artifact_integrity::RegularFileSnapshot& expected_model,
    std::string_view expected_model_fingerprint,
    std::string_view observed_model_fingerprint) {
    return publish_bundle_for_parent(
        path, bundle, expected_model,
        expected_model_fingerprint,
        observed_model_fingerprint);
}

} // namespace testing

} // namespace old_school::fq0_bellman_audit
