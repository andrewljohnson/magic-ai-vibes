#include "old_school/fq0_bellman_audit.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/fq0_information_set.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <unistd.h>

namespace {

namespace audit = old_school::fq0_bellman_audit;
namespace integrity = old_school::artifact_integrity;
namespace information_set =
    old_school::fq0_information_set;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Exception = std::exception,
          typename Function>
void expect_throws(
    Function&& function, std::string_view message) {
    try {
        std::forward<Function>(function)();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        path_ =
            std::filesystem::canonical(
                std::filesystem::temp_directory_path()) /
            ("old-school-fq0-audit-tests-" +
             std::to_string(
                 static_cast<unsigned long long>(::getpid())) +
             "-" + std::to_string(next_++));
        std::filesystem::create_directory(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

  private:
    static inline std::size_t next_ = 0;
    std::filesystem::path path_;
};

void write_file(
    const std::filesystem::path& path,
    std::string_view bytes) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("cannot create test file");
    }
    output.write(
        bytes.data(),
        static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw std::runtime_error("cannot write test file");
    }
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot read test file");
    }
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

audit::BitIdentityEvidence identity_witness(
    std::string_view) {
    return {};
}

audit::ScopeEvidence terminal_scope(
    bool full, std::size_t block, double value) {
    const std::size_t count =
        full ? audit::kRootWorlds
             : audit::kWorldsPerBlock;
    const std::size_t offset =
        full ? 0 : block * audit::kWorldsPerBlock;
    audit::ScopeEvidence scope{
        .kind = full ? audit::ScopeKind::FullK64
                     : audit::ScopeKind::BlockK8,
        .block = block,
        .target = {
            .value = value,
            .particles = count,
            .terminal_particles = count,
            .same_owner_particles = 0,
            .opponent_owner_particles = 0,
        },
        .exact_particle_partition = true,
        .complete = true,
    };
    for (std::size_t world = 0; world < count; ++world) {
        scope.root_world_indices.push_back(offset + world);
        scope.terminals.push_back({
            .world_index = offset + world,
            .root_owner_value = value,
        });
    }
    return scope;
}

double action_value(
    std::string_view stable_id,
    const old_school::probes::Candidate& candidate,
    std::size_t canonical_index) {
    const std::string_view descriptor =
        candidate.descriptor;
    const auto* priority =
        std::get_if<old_school::PriorityAction>(
            &candidate.action);
    if (priority == nullptr) {
        throw std::runtime_error(
            "test action value received a non-Priority "
            "candidate");
    }
    if (stable_id ==
        "field.green.second-main-sick-bear-growth.v1") {
        return descriptor == "pass" ? 1.0 : 0.0;
    }
    if (stable_id ==
        "control.blue.force-spike-live-gray-ogre.v1") {
        return descriptor == "force-spike-gray-ogre"
                   ? 1.0
                   : 0.0;
    }
    if (stable_id ==
        "field.green.begin-combat-growth-tapped-air.v1") {
        return descriptor ==
                       "growth-own-ironroot-treefolk"
                   ? 1.0
                   : 0.0;
    }
    if (stable_id == "blue.counter-fire-elemental.v3") {
        return descriptor == "counter-fire-elemental"
                   ? 1.0
                   : 0.0;
    }
    if (stable_id == "blue.counter-lethal-bolt.v3") {
        return descriptor ==
                       "counter-lethal-lightning-bolt"
                   ? 1.0
                   : 0.0;
    }
    if (stable_id == "blue.counter-war.v3") {
        return descriptor ==
                       "counter-opponent-counterspell"
                   ? 1.0
                   : 0.0;
    }
    if (stable_id ==
        "control.blue.counter-redundant-same-target.v1") {
        return descriptor == "pass" ? 1.0 : 0.0;
    }
    if (stable_id ==
            "control.blue.braingeyser-x0.v1" ||
        stable_id == "ru.disintegrate-player-x.v3" ||
        stable_id ==
            "validation.ru.disintegrate-hold-x0.v1") {
        const bool x_zero =
            (priority->kind ==
                 old_school::PriorityActionKind::
                     CastBraingeyser ||
             priority->kind ==
                 old_school::PriorityActionKind::
                     CastDisintegrate) &&
            priority->x_value == 0;
        return x_zero ? 0.0 : 1.0;
    }
    return canonical_index == 0 ? 1.0 : 0.0;
}

audit::RootActionEvidence root_action(
    std::string_view stable_id,
    std::string_view information_set_id,
    const old_school::probes::Candidate& candidate,
    double value, std::size_t root_index,
    std::size_t action_index) {
    const auto* priority =
        std::get_if<old_school::PriorityAction>(
            &candidate.action);
    if (priority == nullptr) {
        throw std::runtime_error(
            "test manifest contains a non-Priority action");
    }
    const std::string row_id =
        "root/" + std::string(stable_id) + "/" +
        candidate.descriptor;
    audit::RootActionEvidence action{
        .descriptor = candidate.descriptor,
        .action = *priority,
        .feature_row_id = row_id,
        .target = {
            .full = value,
            .blocks = {
                value, value, value, value,
                value, value, value, value,
            },
        },
        .policy_features = {
            static_cast<double>(root_index + 1),
            static_cast<double>(action_index + 1),
            -0.0,
        },
        .canonical_consequence_fingerprint =
            integrity::sha256_string(
                std::string(stable_id) + "/" +
                candidate.descriptor + "/consequence"),
        .complete = true,
    };
    for (std::size_t world = 0;
         world < audit::kRootWorlds; ++world) {
        action.root_transitions.push_back({
            .world_index = world,
            .determinization_seed =
                information_set::derive_indexed_seed(
                    audit::kRootTransitionSeedBase,
                    {
                        .domain =
                            information_set::SeedDomain::
                                RootDeterminization,
                        .scope = std::string(stable_id),
                        .group =
                            std::string(information_set_id),
                        .bank =
                            information_set::SeedBank::Root,
                        .block = audit::kBlocks,
                        .world = world,
                    }),
            .macro_seed =
                information_set::derive_indexed_seed(
                    audit::kRootTransitionSeedBase,
                    {
                        .domain =
                            information_set::SeedDomain::
                                RootMacroTransition,
                        .scope = std::string(stable_id),
                        .group =
                            std::string(information_set_id),
                        .bank =
                            information_set::SeedBank::Root,
                        .block = audit::kBlocks,
                        .world = world,
                    }),
            .redacted_result_hash =
                integrity::sha256_string(
                    std::string(stable_id) + "/" +
                    candidate.descriptor + "/" +
                    std::to_string(world)),
            .terminal = true,
            .terminal_root_owner_value_bits =
                std::bit_cast<std::uint64_t>(value),
            .successor_information_set_fingerprint = {},
            .successor_owner = 0,
            .forced_root_action_applied = true,
            .successful_disposition = true,
            .actions_applied = 1,
            .priority_actions_applied = 1,
            .phase_transitions = 0,
            .turn_advances = 0,
        });
    }
    action.scopes.push_back(terminal_scope(true, 0, value));
    for (std::size_t block = 0; block < audit::kBlocks;
         ++block) {
        action.scopes.push_back(
            terminal_scope(false, block, value));
    }
    return action;
}

audit::GroupBankEvidence group_bank(
    std::string_view root_stable_id,
    std::string_view successor_fingerprint,
    audit::ScopeKind kind, std::size_t block,
    std::string name,
    double first_value, double second_value) {
    const information_set::SeedBank seed_bank =
        name == "A" ? information_set::SeedBank::A
                    : information_set::SeedBank::B;
    const std::uint64_t seed_base =
        seed_bank == information_set::SeedBank::A
            ? audit::kBankASeedBase
            : audit::kBankBSeedBase;
    const information_set::SeedDomain det_domain =
        seed_bank == information_set::SeedBank::A
            ? information_set::SeedDomain::
                  SuccessorSelectionDeterminization
            : information_set::SeedDomain::
                  SuccessorEvaluationDeterminization;
    const information_set::SeedDomain macro_domain =
        seed_bank == information_set::SeedBank::A
            ? information_set::SeedDomain::
                  SuccessorSelectionMacroTransition
            : information_set::SeedDomain::
                  SuccessorEvaluationMacroTransition;
    const std::size_t seed_block =
        kind == audit::ScopeKind::FullK64
            ? audit::kBlocks
            : block;
    const std::string scoped_information =
        "successor/" + std::string(root_stable_id) + "/" +
        std::string(successor_fingerprint);
    const std::string scope_name =
        kind == audit::ScopeKind::FullK64
            ? "full-k64"
            : "block-k8-" + std::to_string(block);
    audit::GroupBankEvidence bank{
        .bank = name,
        .stream_key =
            "fq0-stream/" + std::string(root_stable_id) +
            "/" + std::string(successor_fingerprint) +
            "/" + name + "/" + scope_name,
    };
    const std::array<std::pair<std::string_view, double>, 2>
        actions = {{
            {"a", first_value},
            {"b", second_value},
        }};
    for (const auto& [descriptor, value] : actions) {
        audit::GroupActionEvidence action{
            .descriptor = std::string(descriptor),
            .action =
                descriptor == "a"
                    ? old_school::PriorityAction::pass()
                    : old_school::PriorityAction::
                          cast_creature(
                              old_school::CardId::
                                  GrizzlyBears),
            .feature_row_id =
                scoped_information + "/" +
                std::string(descriptor),
            .policy_features = {
                descriptor == "a" ? 100.0 : 101.0,
                1.0, 0.0,
            },
            .canonical_consequence_fingerprint =
                integrity::sha256_string(
                    scoped_information + "/" +
                    std::string(descriptor) +
                    "/consequence"),
        };
        for (std::size_t world = 0;
             world < audit::kRootWorlds; ++world) {
            action.samples.push_back({
                .world_index = world,
                .determinization_seed =
                    information_set::derive_indexed_seed(
                        seed_base,
                        {
                            .domain = det_domain,
                            .scope =
                                std::string(
                                    root_stable_id),
                            .group = std::string(
                                successor_fingerprint),
                            .bank = seed_bank,
                            .block = seed_block,
                            .world = world,
                        }),
                .macro_seed =
                    information_set::derive_indexed_seed(
                        seed_base,
                        {
                            .domain = macro_domain,
                            .scope =
                                std::string(
                                    root_stable_id),
                            .group = std::string(
                                successor_fingerprint),
                            .bank = seed_bank,
                            .block = seed_block,
                            .world = world,
                        }),
                .score_bits =
                    std::bit_cast<std::uint64_t>(value),
                .redacted_leaf_hash =
                    integrity::sha256_string(
                        std::string(root_stable_id) + "/" +
                        std::string(successor_fingerprint) +
                        "/" + scope_name + "/" + name +
                        "/" + std::string(descriptor) + "/" +
                        std::to_string(world)),
                .terminal = false,
                .forced_action_applied = true,
                .critic_evaluated = true,
                .contextual_score_bits =
                    std::bit_cast<std::uint64_t>(value),
                .legacy_score_bits =
                    std::bit_cast<std::uint64_t>(value),
                .actions_applied = 2,
                .priority_actions_applied = 2,
                .phase_transitions = 0,
                .turn_advances = 0,
            });
        }
        bank.actions.push_back(std::move(action));
    }
    return bank;
}

std::vector<old_school::fq0_bellman::ActionSamples>
bank_samples(const audit::GroupBankEvidence& bank) {
    std::vector<old_school::fq0_bellman::ActionSamples> rows;
    rows.reserve(bank.actions.size());
    for (const audit::GroupActionEvidence& action :
         bank.actions) {
        old_school::fq0_bellman::ActionSamples row{
            .descriptor = action.descriptor,
            .sample_stream_key = bank.stream_key,
        };
        row.world_indices.reserve(action.samples.size());
        row.samples.reserve(action.samples.size());
        for (const audit::LeafSampleEvidence& sample :
             action.samples) {
            row.world_indices.push_back(sample.world_index);
            row.samples.push_back(
                std::bit_cast<double>(sample.score_bits));
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

old_school::fq0_bellman::CrossFitValue
recomputed_cross_fit(
    const audit::GroupBankEvidence& bank_a,
    const audit::GroupBankEvidence& bank_b) {
    return old_school::fq0_bellman::cross_fit_v0(
        bank_samples(bank_a), bank_samples(bank_b));
}

void rebuild_c16_ranking_summary(
    audit::RunReport& report);
void rebind_report_witnesses(
    audit::RunReport& report);

void add_successor_group_to_first_root(
    audit::RunReport& report) {
    auto root = std::find_if(
        report.scientific.roots.begin(),
        report.scientific.roots.end(),
        [](const audit::RootEvidence& value) {
            return value.stable_id ==
                   "green.develop-bears.v3";
        });
    if (root == report.scientific.roots.end()) {
        throw std::runtime_error(
            "descriptive group test root is missing");
    }
    auto& action = root->actions.front();
    const std::string successor_fingerprint =
        integrity::sha256_string(
            "successor-information");
    const std::string scoped_information =
        "successor/" + root->stable_id + "/" +
        successor_fingerprint;
    const auto convert =
        [](const audit::GroupBankEvidence& bank) {
            std::vector<
                old_school::fq0_bellman::ActionSamples>
                rows;
            for (const auto& action : bank.actions) {
                old_school::fq0_bellman::ActionSamples row{
                    .descriptor = action.descriptor,
                    .sample_stream_key = bank.stream_key,
                };
                for (const auto& sample : action.samples) {
                    row.world_indices.push_back(
                        sample.world_index);
                    row.samples.push_back(
                        std::bit_cast<double>(
                            sample.score_bits));
                }
                rows.push_back(std::move(row));
            }
            return rows;
        };
    for (auto& transition : action.root_transitions) {
        transition.redacted_result_hash =
            successor_fingerprint;
        transition.terminal = false;
        transition.terminal_root_owner_value_bits = 0;
        transition.successor_information_set_fingerprint =
            successor_fingerprint;
        transition.successor_owner = root->root_player;
    }

    audit::SuccessorFeatureEvaluationEvidence evaluation{
        .root_stable_id = root->stable_id,
        .information_set_fingerprint =
            successor_fingerprint,
        .successor_owner = root->root_player,
        .representative_root_world = 0,
        .representative_root_action_descriptor =
            action.descriptor,
        .complete = true,
    };
    for (std::size_t scope_index = 0;
         scope_index <= audit::kBlocks; ++scope_index) {
        const bool full = scope_index == 0;
        const audit::ScopeKind kind =
            full ? audit::ScopeKind::FullK64
                 : audit::ScopeKind::BlockK8;
        const std::size_t block =
            full ? 0 : scope_index - 1;
        const std::size_t count =
            full ? audit::kRootWorlds
                 : audit::kWorldsPerBlock;
        const std::size_t offset =
            full ? 0 : block * audit::kWorldsPerBlock;
        audit::ScopeEvidence scope{
            .kind = kind,
            .block = block,
            .exact_particle_partition = true,
            .complete = true,
        };
        for (std::size_t world = 0; world < count;
             ++world) {
            scope.root_world_indices.push_back(
                offset + world);
        }
        audit::SuccessorGroupEvidence group{
            .information_set_fingerprint =
                successor_fingerprint,
            .successor_owner = root->root_player,
            .relation =
                old_school::fq0_bellman::OwnerRelation::
                    SameOwner,
            .root_world_indices =
                scope.root_world_indices,
            .representative_root_world =
                scope.root_world_indices.front(),
            .representative_root_action_descriptor =
                action.descriptor,
            .bank_a =
                group_bank(
                    root->stable_id,
                    successor_fingerprint, kind, block,
                    "A", 0.25, 0.75),
            .bank_b =
                group_bank(
                    root->stable_id,
                    successor_fingerprint, kind, block,
                    "B", 0.5, 1.0),
            .hidden_repartition_witness =
                identity_witness(
                    root->stable_id + "/" +
                    std::to_string(scope_index) +
                    "/hidden"),
            .every_representative_reconstructs = true,
            .hidden_repartition_eligible = true,
            .hidden_identity_changed = true,
            .hidden_repartition_invariant = true,
            .complete = true,
        };
        for (const std::size_t world :
             group.root_world_indices) {
            group.representative_reconstruction_witnesses
                .push_back(identity_witness(
                    root->stable_id + "/" +
                    std::to_string(scope_index) + "/" +
                    std::to_string(world) +
                    "/reconstruct"));
        }
        group.cross_fit =
            old_school::fq0_bellman::cross_fit_v0(
                convert(group.bank_a),
                convert(group.bank_b));
        scope.groups.push_back(group);
        scope.target = {
            .value = group.cross_fit.value,
            .particles = count,
            .terminal_particles = 0,
            .same_owner_particles = count,
            .opponent_owner_particles = 0,
        };
        if (full) {
            action.target.full = scope.target.value;
        } else {
            action.target.blocks[block] =
                scope.target.value;
        }
        action.scopes[scope_index] = std::move(scope);
        audit::SuccessorFeatureScopeEvidence feature_scope{
            .kind = kind,
            .block = block,
            .bank_a = group.bank_a,
            .bank_b = group.bank_b,
            .hidden_repartition_witness = {},
            .every_representative_reconstructs = true,
            .hidden_repartition_eligible = true,
            .hidden_identity_changed = true,
            .hidden_repartition_invariant = true,
            .complete = true,
        };
        for (std::size_t representative_world = 0;
             representative_world < audit::kRootWorlds;
             ++representative_world) {
            feature_scope.representative_catalog.push_back({
                .root_world = representative_world,
                .root_action_descriptor =
                    action.descriptor,
            });
            feature_scope
                .representative_reconstruction_witnesses
                .push_back({});
        }
        evaluation.scopes.push_back(
            std::move(feature_scope));
    }
    report.scientific.successor_feature_evaluations
        .push_back(evaluation);

    for (const auto& group_action :
         evaluation.scopes.front().bank_a.actions) {
        const double target =
            group_action.descriptor == "a" ? 0.375
                                            : 0.875;
        report.scientific.feature_rows.push_back({
            .row_id = group_action.feature_row_id,
            .information_set_id =
                scoped_information,
            .legal_set_id =
                "legal/" + scoped_information,
            .common_world_key =
                "worlds/successor/" +
                scoped_information,
            .action_descriptor =
                group_action.descriptor,
            .features = group_action.policy_features,
            .canonical_consequence_fingerprint =
                group_action
                    .canonical_consequence_fingerprint,
            .target = {
                .full = target,
                .blocks = {
                    target, target, target, target,
                    target, target, target, target,
                },
            },
            .unique_exact_max =
                group_action.descriptor == "b",
        });
    }
    const auto root_feature = std::find_if(
        report.scientific.feature_rows.begin(),
        report.scientific.feature_rows.end(),
        [&](const old_school::fq0_bellman::
                FeatureTargetRow& row) {
            return row.row_id == action.feature_row_id;
        });
    if (root_feature ==
        report.scientific.feature_rows.end()) {
        throw std::runtime_error(
            "root feature row is missing");
    }
    root_feature->target = action.target;
    std::vector<old_school::fq0_bellman::ActionMean>
        root_means;
    for (const auto& root_action : root->actions) {
        root_means.push_back({
            .descriptor = root_action.descriptor,
            .value = root_action.target.full,
        });
    }
    root->exact_support =
        old_school::fq0_bellman::exact_max_support(
            root_means);
    for (auto& row : report.scientific.feature_rows) {
        if (row.information_set_id ==
            audit::binding::root_feature_information_set_id(
                root->stable_id,
                root
                    ->manifest_information_action_fingerprint)) {
            row.unique_exact_max =
                root->exact_support.size() == 1 &&
                root->exact_support.front() ==
                    row.action_descriptor;
        }
    }
    std::sort(
        report.scientific.feature_rows.begin(),
        report.scientific.feature_rows.end(),
        [](const auto& first, const auto& second) {
            return first.row_id < second.row_id;
        });
    report.scientific.feature_collisions =
        old_school::fq0_bellman::
            analyze_global_feature_collisions(
                report.scientific.feature_rows);
    rebuild_c16_ranking_summary(report);
    rebind_report_witnesses(report);
    report.gate = audit::evaluate_gate(
        report.scientific, report.integrity);
}

const audit::RootEvidence& report_root(
    const audit::RunReport& report,
    std::string_view stable_id) {
    const auto found = std::find_if(
        report.scientific.roots.begin(),
        report.scientific.roots.end(),
        [&](const audit::RootEvidence& root) {
            return root.stable_id == stable_id;
        });
    if (found == report.scientific.roots.end()) {
        throw std::runtime_error(
            "test report root is missing");
    }
    return *found;
}

const audit::RootActionEvidence& report_action(
    const audit::RunReport& report,
    std::string_view stable_id,
    std::string_view descriptor) {
    const auto& root = report_root(report, stable_id);
    const auto found = std::lower_bound(
        root.actions.begin(), root.actions.end(), descriptor,
        [](const audit::RootActionEvidence& action,
           std::string_view key) {
            return action.descriptor < key;
        });
    if (found == root.actions.end() ||
        found->descriptor != descriptor) {
        throw std::runtime_error(
            "test report action is missing: " +
            std::string(stable_id) + "/" +
            std::string(descriptor));
    }
    return *found;
}

void add_contrast(
    audit::RunReport& report, audit::GateRole role,
    std::string name, std::string stable_id,
    std::string positive, std::string negative) {
    const auto& positive_action =
        report_action(report, stable_id, positive);
    const auto& negative_action =
        report_action(report, stable_id, negative);
    const auto contrast =
        old_school::fq0_bellman::summarize_block_contrast(
            positive_action.target,
            negative_action.target);
    const bool directional =
        role == audit::GateRole::Primary ||
        role == audit::GateRole::LiveForceGuard ||
        role == audit::GateRole::GrowthTargetGuard ||
        role == audit::GateRole::Descriptive;
    report.scientific.contrasts.push_back({
        .role = role,
        .name = std::move(name),
        .stable_id = std::move(stable_id),
        .positive_descriptor = std::move(positive),
        .negative_descriptor = std::move(negative),
        .contrast = contrast,
        .support_condition = true,
        .directional_passed =
            directional
                ? old_school::fq0_bellman::
                      passes_directional_gate(
                          contrast,
                          audit::
                              kPrimaryMinimumPositiveBlocks)
                : true,
    });
}

template <typename Predicate>
std::string first_typed_descriptor(
    const audit::RunReport& report,
    std::string_view stable_id, Predicate predicate) {
    for (const auto& action :
         report_root(report, stable_id).actions) {
        if (predicate(action.action)) {
            return action.descriptor;
        }
    }
    throw std::runtime_error(
        "test root has no requested typed action");
}

std::string pass_descriptor(
    const audit::RunReport& report,
    std::string_view stable_id) {
    return first_typed_descriptor(
        report, stable_id,
        [](const old_school::PriorityAction& action) {
            return action.kind ==
                   old_school::PriorityActionKind::Pass;
        });
}

std::vector<std::string> x_zero_descriptors(
    const audit::RunReport& report,
    std::string_view stable_id) {
    std::vector<std::string> result;
    for (const auto& action :
         report_root(report, stable_id).actions) {
        const bool cast_x_spell =
            action.action.kind ==
                old_school::PriorityActionKind::
                    CastBraingeyser ||
            action.action.kind ==
                old_school::PriorityActionKind::
                    CastDisintegrate;
        if (cast_x_spell && action.action.x_value == 0) {
            result.push_back(action.descriptor);
        }
    }
    if (result.size() != 2) {
        throw std::runtime_error(
            "test X=0 root does not have two targets");
    }
    return result;
}

bool descriptor_pair(
    const audit::RootActionEvidence& first,
    const audit::RootActionEvidence& second,
    std::string_view one, std::string_view two) {
    return (first.descriptor == one &&
            second.descriptor == two) ||
           (first.descriptor == two &&
            second.descriptor == one);
}

void add_exhaustive_dominance(
    audit::RunReport& report) {
    using Orientation =
        old_school::fq0_dominance::Orientation;
    for (const audit::RootEvidence& root :
         report.scientific.roots) {
        for (std::size_t first_index = 0;
             first_index < root.actions.size();
             ++first_index) {
            for (std::size_t second_index = first_index + 1;
                 second_index < root.actions.size();
                 ++second_index) {
                const audit::RootActionEvidence& first =
                    root.actions[first_index];
                const audit::RootActionEvidence& second =
                    root.actions[second_index];
                const bool first_pass =
                    first.action.kind ==
                    old_school::PriorityActionKind::Pass;
                const bool second_pass =
                    second.action.kind ==
                    old_school::PriorityActionKind::Pass;
                const auto x_zero =
                    [](const old_school::PriorityAction&
                           action) {
                        return (action.kind ==
                                    old_school::
                                        PriorityActionKind::
                                            CastBraingeyser ||
                                action.kind ==
                                    old_school::
                                        PriorityActionKind::
                                            CastDisintegrate) &&
                               action.x_value == 0;
                    };
                const bool x_zero_pair =
                    (first_pass && x_zero(second.action)) ||
                    (second_pass && x_zero(first.action));
                const bool primary =
                    root.stable_id ==
                        "field.green.second-main-sick-bear-growth.v1" &&
                    descriptor_pair(
                        first, second, "pass",
                        "growth-own-summoning-sick-grizzly-bears");
                const bool redundant =
                    root.stable_id ==
                        "control.blue.counter-redundant-same-target.v1" &&
                    descriptor_pair(
                        first, second, "pass",
                        "counter-same-air-elemental");
                const bool named_incomparable =
                    (root.stable_id ==
                             "control.blue.force-spike-live-gray-ogre.v1" &&
                         descriptor_pair(
                             first, second, "pass",
                             "force-spike-gray-ogre")) ||
                    (root.stable_id ==
                             "control.blue.force-spike-payable-gray-ogre.v1" &&
                         descriptor_pair(
                             first, second, "pass",
                             "force-spike-gray-ogre")) ||
                    (root.stable_id ==
                             "blue.counter-fire-elemental.v3" &&
                         descriptor_pair(
                             first, second, "pass",
                             "counter-fire-elemental")) ||
                    (root.stable_id ==
                             "blue.counter-lethal-bolt.v3" &&
                         descriptor_pair(
                             first, second, "pass",
                             "counter-lethal-lightning-bolt")) ||
                    (root.stable_id ==
                             "blue.counter-war.v3" &&
                         descriptor_pair(
                             first, second, "pass",
                             "counter-opponent-counterspell")) ||
                    (root.stable_id ==
                             "control.blue.counter-redundant-same-target.v1" &&
                         descriptor_pair(
                             first, second, "pass",
                             "counter-own-counterspell"));
                const bool pass_dominates =
                    primary || x_zero_pair || redundant;
                const Orientation orientation =
                    pass_dominates
                        ? (first_pass
                               ? Orientation::
                                     FirstDominatesSecond
                               : Orientation::
                                     SecondDominatesFirst)
                        : Orientation::Incomparable;
                const audit::GateRole role =
                    primary
                        ? audit::GateRole::Primary
                        : x_zero_pair
                              ? audit::GateRole::XZeroGuard
                              : named_incomparable
                                    ? audit::GateRole::
                                          IncomparableControlGuard
                                    : redundant
                                          ? audit::GateRole::
                                                DominanceConsistencyGuard
                                          : audit::GateRole::
                                                Descriptive;
                audit::DominancePairEvidence pair{
                    .role = role,
                    .stable_id = root.stable_id,
                    .first_descriptor =
                        first.descriptor,
                    .second_descriptor =
                        second.descriptor,
                    .required_orientation = orientation,
                    .required_worlds =
                        audit::kRootWorlds,
                    .matching_worlds =
                        audit::kRootWorlds,
                    .passed = true,
                };
                for (std::size_t world = 0;
                     world < audit::kRootWorlds; ++world) {
                    old_school::fq0_dominance::
                        CanonicalSettlement first_settlement{
                            .valid = true,
                            .owner_observable_consequence =
                                pass_dominates
                                    ? "same-consequence"
                                    : "first-consequence",
                        };
                    old_school::fq0_dominance::
                        CanonicalSettlement second_settlement{
                            .valid = true,
                            .owner_observable_consequence =
                                pass_dominates
                                    ? "same-consequence"
                                    : "second-consequence",
                        };
                    if (pass_dominates) {
                        auto& dominated_cost =
                            first_pass
                                ? second_settlement
                                      .costs[root
                                                 .root_player]
                                : first_settlement
                                      .costs[root
                                                 .root_player];
                        dominated_cost
                            .hand_cards_consumed[0] = 1;
                    }
                    old_school::fq0_dominance::Comparison
                        comparison{
                            .orientation = orientation,
                            .root_information_equal = true,
                            .first_normalized = true,
                            .second_normalized = true,
                            .consequences_equal =
                                pass_dominates,
                            .first =
                                std::move(first_settlement),
                            .second =
                                std::move(second_settlement),
                        };
                    pair.worlds.push_back({
                        .world_index = world,
                        .determinization_seed =
                            information_set::
                                derive_indexed_seed(
                                    audit::
                                        kRootTransitionSeedBase,
                                    {
                                        .domain =
                                            information_set::
                                                SeedDomain::
                                                    RootDeterminization,
                                        .scope =
                                            root.stable_id,
                                        .group =
                                            root
                                                .manifest_information_action_fingerprint,
                                        .bank =
                                            information_set::
                                                SeedBank::Root,
                                        .block =
                                            audit::kBlocks,
                                        .world = world,
                                    }),
                        .common_world_key =
                            audit::binding::
                                dominance_common_world_key(
                                    root.stable_id,
                                    root
                                        .manifest_information_action_fingerprint,
                                    world),
                        .comparison =
                            std::move(comparison),
                        .orientation = orientation,
                        .hidden_repartition_witness =
                            identity_witness(
                                root.stable_id + "/" +
                                first.descriptor + "/" +
                                second.descriptor + "/" +
                                std::to_string(world) +
                                "/dominance-hidden"),
                        .hidden_repartition_bit_identical =
                            true,
                    });
                }
                report.scientific.dominance_pairs
                    .push_back(std::move(pair));
            }
        }
    }
    std::sort(
        report.scientific.dominance_pairs.begin(),
        report.scientific.dominance_pairs.end(),
        [](const auto& first, const auto& second) {
            return std::tuple{
                       first.stable_id,
                       first.first_descriptor,
                       first.second_descriptor} <
                   std::tuple{
                       second.stable_id,
                       second.first_descriptor,
                       second.second_descriptor};
        });
}

void rebuild_c16_ranking_summary(
    audit::RunReport& report) {
    audit::RankingSummaryEvidence summary{
        .equal_deck_pairwise_change_fraction_bits =
            std::bit_cast<std::uint64_t>(0.0),
        .equal_deck_support_changed_fraction_bits =
            std::bit_cast<std::uint64_t>(0.0),
        .complete = true,
    };
    std::array<std::size_t, old_school::kDeckCount>
        counts{};
    for (const audit::RootEvidence& root :
         report.scientific.roots) {
        audit::C16RootRankingEvidence ranking{
            .stable_id = root.stable_id,
            .exact_support = root.exact_support,
            .pairwise_change_fraction_bits =
                std::bit_cast<std::uint64_t>(0.0),
            .support_changed = false,
        };
        for (const audit::RootActionEvidence& action :
             root.actions) {
            ranking.actions.push_back({
                .descriptor = action.descriptor,
                .score_bits =
                    std::bit_cast<std::uint64_t>(
                        action.target.full),
            });
        }
        ++counts[static_cast<std::size_t>(
            root.root_deck)];
        summary.roots.push_back(std::move(ranking));
    }
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        summary.decks[deck] = {
            .deck = static_cast<old_school::DeckId>(deck),
            .roots = counts[deck],
            .support_changed_roots = 0,
            .mean_pairwise_change_fraction_bits =
                std::bit_cast<std::uint64_t>(0.0),
            .support_changed_fraction_bits =
                std::bit_cast<std::uint64_t>(0.0),
        };
    }
    report.scientific.c16_ranking_changes =
        std::move(summary);
}

audit::BitIdentityEvidence bound_identity(
    std::string domain, std::string coordinate,
    const std::string& digest) {
    return audit::binding::make_witness(
        std::move(domain), std::move(coordinate),
        digest, digest);
}

void rebind_report_witnesses(
    audit::RunReport& report) {
    const std::string manifest_digest =
        audit::binding::manifest_payload_sha256(
            report.scientific.manifest);
    report.scientific.invariance
        .independent_manifest_witness =
        bound_identity(
            "manifest", "global", manifest_digest);
    for (std::size_t root_index = 0;
         root_index < report.scientific.roots.size();
         ++root_index) {
        audit::RootEvidence& root =
            report.scientific.roots[root_index];
        for (std::size_t action_index = 0;
             action_index < root.actions.size();
             ++action_index) {
            audit::RootActionEvidence& action =
                root.actions[action_index];
            for (std::size_t scope_index = 0;
                 scope_index < action.scopes.size();
                 ++scope_index) {
                audit::ScopeEvidence& scope =
                    action.scopes[scope_index];
                for (std::size_t group_index = 0;
                     group_index < scope.groups.size();
                     ++group_index) {
                    audit::SuccessorGroupEvidence& group =
                        scope.groups[group_index];
                    const std::string coordinate =
                        root.stable_id + "/action/" +
                        std::to_string(action_index) +
                        "/scope/" +
                        std::to_string(scope_index) +
                        "/group/" +
                        std::to_string(group_index);
                    const std::string group_digest =
                        audit::binding::
                            successor_bank_pair_payload_sha256(
                                group.bank_a, group.bank_b,
                                group.cross_fit);
                    group.hidden_repartition_witness =
                        bound_identity(
                            "group-hidden",
                            audit::binding::
                                hidden_repartition_coordinate(
                                    coordinate,
                                    group
                                        .representative_root_world,
                                    group
                                        .representative_root_action_descriptor),
                            group_digest);
                    group
                        .representative_reconstruction_witnesses
                        .clear();
                    for (const std::size_t world :
                         group.root_world_indices) {
                        group
                            .representative_reconstruction_witnesses
                            .push_back(bound_identity(
                                "group-representative",
                                coordinate + "/member/" +
                                    std::to_string(world),
                                group_digest));
                    }
                }
            }
        }
        const std::string root_digest =
            audit::binding::root_payload_sha256(root);
        root.hidden_repartition_witness =
            bound_identity(
                "root-hidden", root.stable_id,
                root_digest);
        root.descriptor_order_witness =
            bound_identity(
                "root-order", root.stable_id,
                root_digest);
    }
    for (audit::SuccessorFeatureEvaluationEvidence&
             evaluation :
         report.scientific
             .successor_feature_evaluations) {
        for (std::size_t scope_index = 0;
             scope_index < evaluation.scopes.size();
             ++scope_index) {
            audit::SuccessorFeatureScopeEvidence& scope =
                evaluation.scopes[scope_index];
            const std::string coordinate =
                evaluation.root_stable_id + "/" +
                evaluation
                    .information_set_fingerprint +
                "/feature-scope/" +
                std::to_string(scope_index);
            const std::string digest =
                audit::binding::
                    successor_bank_pair_payload_sha256(
                        scope.bank_a, scope.bank_b,
                        recomputed_cross_fit(
                            scope.bank_a, scope.bank_b));
            scope.representative_reconstruction_witnesses
                .clear();
            for (const auto& representative :
                 scope.representative_catalog) {
                scope
                    .representative_reconstruction_witnesses
                    .push_back(bound_identity(
                        "feature-scope-representative",
                        coordinate + "/member/" +
                        std::to_string(
                            representative.root_world) +
                            "/" +
                            representative
                                .root_action_descriptor,
                        digest));
            }
            scope.hidden_repartition_witness =
                bound_identity(
                    "feature-scope-hidden",
                    audit::binding::
                        hidden_repartition_coordinate(
                            coordinate,
                            evaluation
                                .representative_root_world,
                            evaluation
                                .representative_root_action_descriptor),
                    digest);
        }
    }
    for (audit::DominancePairEvidence& pair :
         report.scientific.dominance_pairs) {
        for (audit::DominanceWorldEvidence& world :
             pair.worlds) {
            const std::string coordinate =
                pair.stable_id + "/" +
                pair.first_descriptor + "/" +
                pair.second_descriptor + "/" +
                std::to_string(world.world_index);
            const std::string digest =
                audit::binding::
                    dominance_comparison_payload_sha256(
                        pair.stable_id,
                        pair.first_descriptor,
                        pair.second_descriptor, world);
            world.hidden_repartition_witness =
                bound_identity(
                    "dominance-hidden", coordinate,
                    digest);
        }
    }
    report.scientific.primary_core_sha256 =
        audit::binding::scientific_core_payload_sha256(
            report.scientific);
    const std::string& core =
        report.scientific.primary_core_sha256;
    report.scientific.invariance
        .repeated_construction_witness =
        bound_identity("core-repeat", "global", core);
    report.scientific.invariance
        .descriptor_order_witness =
        bound_identity("core-order", "global", core);
    report.scientific.invariance.thread_count_witness =
        bound_identity("core-thread", "global", core);
    report.scientific.invariance
        .hidden_repartition_witness =
        bound_identity("core-hidden", "global", core);
    const auto critic =
        audit::binding::critic_stream_payload_sha256(
            report.scientific);
    report.scientific.invariance
        .contextual_legacy_critic_witness =
        audit::binding::make_witness(
            "critic-context-vs-legacy", "global",
            critic[0], critic[1]);
}

audit::RunReport complete_report() {
    audit::RunReport report;
    report.scientific.model_fingerprint =
        std::string(audit::kModelFingerprint);
    report.scientific.manifest =
        old_school::ac1_teacher_audit::build_manifest();
    report.scientific.roots_by_deck =
        report.scientific.manifest
            .physical_roots_by_deck;
    for (std::size_t root_index = 0;
         root_index <
         report.scientific.manifest.roots.size();
         ++root_index) {
        const auto& manifest =
            report.scientific.manifest.roots[root_index];
        std::vector<const old_school::probes::Candidate*>
            candidates;
        for (const auto& candidate :
             manifest.probe.candidates) {
            candidates.push_back(&candidate);
        }
        std::sort(
            candidates.begin(), candidates.end(),
            [](const auto* first, const auto* second) {
                return first->descriptor <
                       second->descriptor;
            });
        audit::RootEvidence root{
            .stable_id = manifest.probe.stable_id,
            .manifest_information_action_fingerprint =
                manifest.information_action_fingerprint,
            .root_deck = manifest.probe.root_deck,
            .root_player = manifest.probe.root_player,
            .hidden_repartition_witness =
                identity_witness(
                    manifest.probe.stable_id +
                    "/root-hidden"),
            .descriptor_order_witness =
                identity_witness(
                    manifest.probe.stable_id +
                    "/root-order"),
            .hidden_repartition_bit_identical = true,
            .descriptor_order_bit_identical = true,
            .complete = true,
        };
        std::vector<old_school::fq0_bellman::ActionMean>
            means;
        for (std::size_t action_index = 0;
             action_index < candidates.size();
             ++action_index) {
            const double value = action_value(
                root.stable_id,
                *candidates[action_index],
                action_index);
            root.actions.push_back(root_action(
                root.stable_id,
                root
                    .manifest_information_action_fingerprint,
                *candidates[action_index], value,
                root_index, action_index));
            means.push_back({
                .descriptor =
                    candidates[action_index]->descriptor,
                .value = value,
            });
        }
        root.exact_support =
            old_school::fq0_bellman::exact_max_support(
                means);
        const std::string root_information_set =
            audit::binding::
                root_feature_information_set_id(
                    root.stable_id,
                    root
                        .manifest_information_action_fingerprint);
        for (const auto& action : root.actions) {
            report.scientific.feature_rows.push_back({
                .row_id = action.feature_row_id,
                .information_set_id =
                    root_information_set,
                .legal_set_id =
                    "legal/" + root_information_set,
                .common_world_key =
                    "worlds/root/" + root.stable_id,
                .action_descriptor = action.descriptor,
                .features = action.policy_features,
                .canonical_consequence_fingerprint =
                    action
                        .canonical_consequence_fingerprint,
                .target = action.target,
                .unique_exact_max =
                    root.exact_support.size() == 1 &&
                    root.exact_support.front() ==
                        action.descriptor,
            });
        }
        report.scientific.roots.push_back(
            std::move(root));
    }
    std::sort(
        report.scientific.feature_rows.begin(),
        report.scientific.feature_rows.end(),
        [](const auto& first, const auto& second) {
            return first.row_id < second.row_id;
        });
    report.scientific.feature_collisions =
        old_school::fq0_bellman::
            analyze_global_feature_collisions(
                report.scientific.feature_rows);

    add_contrast(
        report, audit::GateRole::Primary, "primary",
        "field.green.second-main-sick-bear-growth.v1",
        "pass",
        "growth-own-summoning-sick-grizzly-bears");
    add_contrast(
        report, audit::GateRole::LiveForceGuard,
        "live-force",
        "control.blue.force-spike-live-gray-ogre.v1",
        "force-spike-gray-ogre", "pass");
    add_contrast(
        report, audit::GateRole::GrowthTargetGuard,
        "growth-target",
        "field.green.begin-combat-growth-tapped-air.v1",
        "growth-own-ironroot-treefolk",
        "growth-opponent-tapped-air-elemental");
    add_contrast(
        report, audit::GateRole::ProductiveCounterGuard,
        "productive-fire",
        "blue.counter-fire-elemental.v3",
        "counter-fire-elemental", "pass");
    add_contrast(
        report, audit::GateRole::ProductiveCounterGuard,
        "productive-lethal",
        "blue.counter-lethal-bolt.v3",
        "counter-lethal-lightning-bolt", "pass");
    add_contrast(
        report, audit::GateRole::ProductiveCounterGuard,
        "productive-war", "blue.counter-war.v3",
        "counter-opponent-counterspell", "pass");
    add_contrast(
        report, audit::GateRole::RedundantCounterGuard,
        "redundant",
        "control.blue.counter-redundant-same-target.v1",
        "pass", "counter-same-air-elemental");
    const auto add_x_zero_contrasts =
        [&](std::string_view stable_id,
            std::string_view name_prefix) {
            const std::string pass =
                pass_descriptor(report, stable_id);
            const std::vector<std::string> x_zero =
                x_zero_descriptors(report, stable_id);
            for (std::size_t index = 0;
                 index < x_zero.size(); ++index) {
                add_contrast(
                    report, audit::GateRole::XZeroGuard,
                    std::string(name_prefix) + "-" +
                        std::to_string(index),
                    std::string(stable_id), pass,
                    x_zero[index]);
            }
        };
    add_x_zero_contrasts(
        "control.blue.braingeyser-x0.v1",
        "xzero-blue");
    add_x_zero_contrasts(
        "ru.disintegrate-player-x.v3",
        "xzero-ru-player");
    add_x_zero_contrasts(
        "validation.ru.disintegrate-hold-x0.v1",
        "xzero-ru-validation");
    add_exhaustive_dominance(report);
    rebuild_c16_ranking_summary(report);

    report.scientific.invariance = {
        .independent_manifest_witness =
            identity_witness("independent-manifest"),
        .repeated_construction_witness =
            identity_witness("repeated-construction"),
        .descriptor_order_witness =
            identity_witness("descriptor-order"),
        .thread_count_witness =
            identity_witness("thread-count"),
        .hidden_repartition_witness =
            identity_witness("hidden-repartition"),
        .contextual_legacy_critic_witness =
            identity_witness("contextual-legacy-critic"),
        .independent_manifest_bit_identical = true,
        .repeated_construction_bit_identical = true,
        .descriptor_order_bit_identical = true,
        .thread_count_bit_identical = true,
        .hidden_repartition_bit_identical = true,
        .contextual_legacy_critic_bit_identical = true,
        .passed = true,
    };
    report.scientific.primary_passed = true;
    report.scientific.reject_only_guards_passed = true;
    report.scientific.complete = true;
    report.scientific.passed = true;

    report.integrity.model_before = {
        .path = std::filesystem::absolute(
                    std::filesystem::path(
                        audit::kModelArtifactPath))
                    .lexically_normal()
                    .string(),
        .physical_path =
            std::filesystem::absolute(
                std::filesystem::path(
                    audit::kModelArtifactPath))
                .lexically_normal()
                .string(),
        .byte_size = audit::kModelArtifactBytes,
        .sha256 = std::string(audit::kModelArtifactSha256),
    };
    report.integrity.model_after =
        report.integrity.model_before;
    report.integrity.model_components_before = {
        .critic = integrity::sha256_string("critic"),
        .priority = integrity::sha256_string("priority"),
        .attack = integrity::sha256_string("attack"),
        .block = integrity::sha256_string("block"),
        .damage_order =
            integrity::sha256_string("damage-order"),
    };
    report.integrity.model_components_after =
        report.integrity.model_components_before;
    report.integrity.artifact_requirement_matched = true;
    report.integrity.artifact_unchanged = true;
    report.integrity.model_identity_matched = true;
    report.integrity.passed = true;
    rebind_report_witnesses(report);
    report.gate =
        audit::evaluate_gate(
            report.scientific, report.integrity);
    return report;
}

void expect_report_rejected(
    audit::RunReport report, std::string_view message) {
    expect_throws<std::runtime_error>(
        [&] {
            static_cast<void>(
                audit::testing::serialize_evidence_bundle(
                    report));
        },
        message);
}

void append_row(
    std::string& bytes,
    std::initializer_list<std::string_view> fields) {
    bool first = true;
    for (const std::string_view field : fields) {
        if (!first) {
            bytes.push_back('\t');
        }
        first = false;
        bytes.append(field);
    }
    bytes.push_back('\n');
}

audit::EvidenceBundle minimal_bound_bundle(
    const integrity::RegularFileSnapshot& model) {
    static constexpr std::array<std::string_view, 8>
        sections = {
            "metadata",
            "manifest",
            "roots",
            "contrasts",
            "dominance",
            "collisions",
            "integrity",
            "invariance",
        };
    audit::EvidenceBundle bundle;
    append_row(
        bundle.bytes,
        {"schema", audit::kEvidenceSchema});
    for (const std::string_view name : sections) {
        bundle.section_names.emplace_back(name);
        std::string content;
        if (name == "metadata") {
            append_row(
                content,
                {"identity", "model_fingerprint",
                 audit::kModelFingerprint});
            append_row(
                content,
                {"verdict", "exit_code", "0"});
        }
        if (name == "integrity") {
            const std::string size =
                std::to_string(model.byte_size);
            append_row(
                content,
                {"snapshot", "model_before", "bytes",
                 size});
            append_row(
                content,
                {"snapshot", "model_before", "sha256",
                 model.sha256});
            append_row(
                content,
                {"snapshot", "model_after", "bytes", size});
            append_row(
                content,
                {"snapshot", "model_after", "sha256",
                 model.sha256});
            append_row(
                content,
                {"integrity", "artifact_unchanged", "1"});
            append_row(
                content,
                {"integrity", "model_identity_matched", "1"});
            append_row(
                content,
                {"integrity", "passed", "1"});
        }
        if (name == "invariance") {
            append_row(
                content,
                {"scientific", "complete", "1"});
        }
        append_row(
            bundle.bytes, {"section_begin", name});
        bundle.bytes += content;
        const std::string digest =
            integrity::sha256_string(content);
        bundle.section_sha256.push_back(digest);
        append_row(
            bundle.bytes,
            {"section_sha256", name, digest});
    }
    bundle.payload_sha256 =
        integrity::sha256_string(bundle.bytes);
    append_row(
        bundle.bytes,
        {"payload_sha256", bundle.payload_sha256});
    bundle.complete_sha256 =
        integrity::sha256_string(bundle.bytes);
    append_row(
        bundle.bytes,
        {"complete_sha256", bundle.complete_sha256});
    return bundle;
}

void test_gate_exit_codes_and_internal_consistency() {
    audit::RunReport report = complete_report();
    expect(
        report.gate.passed &&
            audit::exit_code(report.gate) == 0,
        "passing report did not produce exit zero");

    report.scientific.primary_passed = false;
    report.scientific.passed = false;
    report.gate = audit::evaluate_gate(
        report.scientific, report.integrity);
    expect(
        !report.gate.passed &&
            !report.gate.infrastructure_failure &&
            audit::exit_code(report.gate) == 1,
        "complete scientific rejection did not produce exit one");

    report.scientific.passed = true;
    report.gate = audit::evaluate_gate(
        report.scientific, report.integrity);
    expect(
        report.gate.infrastructure_failure &&
            audit::exit_code(report.gate) == 2,
        "inconsistent verdict did not fail as infrastructure");

    report = complete_report();
    report.integrity.model_after.sha256[0] = '0';
    report.gate = audit::evaluate_gate(
        report.scientific, report.integrity);
    expect(
        report.gate.infrastructure_failure &&
            audit::exit_code(report.gate) == 2,
        "changed parent snapshot did not fail immutability");
}

void test_bundle_is_canonical_exact_and_tree_independent() {
    audit::RunReport report = complete_report();
    const audit::EvidenceBundle first =
        audit::testing::serialize_evidence_bundle(report);
    const audit::EvidenceBundle repeated =
        audit::testing::serialize_evidence_bundle(report);
    expect(
        first == repeated,
        "FQ0 evidence serialization is nondeterministic");
    expect(
        first.section_names ==
            std::vector<std::string>{
                "metadata", "manifest", "roots",
                "contrasts", "dominance", "collisions",
                "integrity", "invariance"} &&
            first.section_names.size() ==
                first.section_sha256.size(),
        "FQ0 evidence section census drifted");
    expect(
        first.bytes.find("8000000000000000") !=
                std::string::npos &&
            first.bytes.find(
                "payload_sha256\t" +
                first.payload_sha256 + "\n") !=
                std::string::npos &&
            first.bytes.find(
                "complete_sha256\t" +
                first.complete_sha256 + "\n") !=
                std::string::npos,
        "FQ0 exact bits or hash footers are absent");
    expect(
        audit::testing::validate_evidence_bundle(
            first.bytes) == first,
        "FQ0 bundle did not validate exactly");

    audit::RunReport other_tree = report;
    other_tree.integrity.model_before.path =
        "/tree-b/model.bin";
    other_tree.integrity.model_before.physical_path =
        "/tree-b/model.bin";
    other_tree.integrity.model_after =
        other_tree.integrity.model_before;
    other_tree.gate = audit::evaluate_gate(
        other_tree.scientific,
        other_tree.integrity);
    expect(
        audit::testing::serialize_evidence_bundle(
            other_tree) == first,
        "host worktree paths contaminated FQ0 evidence");

    report.scientific.primary_passed = false;
    report.scientific.passed = false;
    report.gate = audit::evaluate_gate(
        report.scientific, report.integrity);
    expect_throws<std::runtime_error>(
        [&] {
            static_cast<void>(
                audit::testing::serialize_evidence_bundle(
                    report));
        },
        "forged scientific summary was serializable");
}

void test_bundle_rejects_corruption_and_trailing_data() {
    const audit::EvidenceBundle bundle =
        audit::testing::serialize_evidence_bundle(
            complete_report());
    std::string corrupt_section = bundle.bytes;
    const std::size_t feature =
        corrupt_section.find("8000000000000000");
    expect(
        feature != std::string::npos,
        "corruption fixture field is missing");
    corrupt_section[feature] = '0';
    expect_throws<std::runtime_error>(
        [&] {
            static_cast<void>(
                audit::testing::validate_evidence_bundle(
                    corrupt_section));
        },
        "section corruption was accepted");

    std::string corrupt_payload = bundle.bytes;
    const std::size_t payload =
        corrupt_payload.find("payload_sha256\t");
    expect(
        payload != std::string::npos,
        "payload footer is missing");
    corrupt_payload[payload + 15] =
        corrupt_payload[payload + 15] == '0' ? '1' : '0';
    expect_throws<std::runtime_error>(
        [&] {
            static_cast<void>(
                audit::testing::validate_evidence_bundle(
                    corrupt_payload));
        },
        "payload hash corruption was accepted");

    std::string corrupt_complete = bundle.bytes;
    const std::size_t complete =
        corrupt_complete.find("complete_sha256\t");
    expect(
        complete != std::string::npos,
        "complete footer is missing");
    corrupt_complete[complete + 16] =
        corrupt_complete[complete + 16] == '0' ? '1' : '0';
    expect_throws<std::runtime_error>(
        [&] {
            static_cast<void>(
                audit::testing::validate_evidence_bundle(
                    corrupt_complete));
        },
        "complete hash corruption was accepted");

    expect_throws<std::runtime_error>(
        [&] {
            static_cast<void>(
                audit::testing::validate_evidence_bundle(
                    bundle.bytes + "trailing\n"));
        },
        "trailing evidence bytes were accepted");
}

void test_group_rows_are_raw_and_recomputed() {
    audit::RunReport report = complete_report();
    add_successor_group_to_first_root(report);
    const audit::EvidenceBundle bundle =
        audit::testing::serialize_evidence_bundle(report);
    std::size_t leaves = 0;
    std::size_t cursor = 0;
    while ((cursor = bundle.bytes.find(
                "\nleaf\t", cursor)) !=
           std::string::npos) {
        ++leaves;
        cursor += 6;
    }
    expect(
        leaves ==
            (audit::kBlocks + 1) * 8 *
                audit::kRootWorlds,
        "FQ0 bundle omitted successor raw score rows");

    audit::RunReport bad_mean = report;
    auto bad_root = std::find_if(
        bad_mean.scientific.roots.begin(),
        bad_mean.scientific.roots.end(),
        [](const audit::RootEvidence& root) {
            return root.stable_id ==
                   "green.develop-bears.v3";
        });
    expect(
        bad_root != bad_mean.scientific.roots.end(),
        "successor fixture root is missing");
    bad_root->actions.front()
        .scopes.front()
        .groups.front()
        .cross_fit.value = 0.0;
    expect_throws<std::runtime_error>(
        [&] {
            static_cast<void>(
                audit::testing::serialize_evidence_bundle(
                    bad_mean));
        },
        "raw successor rows did not bind their aggregate");

    audit::RunReport candidate_keyed_rng = report;
    auto rng_root = std::find_if(
        candidate_keyed_rng.scientific.roots.begin(),
        candidate_keyed_rng.scientific.roots.end(),
        [](const audit::RootEvidence& root) {
            return root.stable_id ==
                   "green.develop-bears.v3";
        });
    expect(
        rng_root !=
            candidate_keyed_rng.scientific.roots.end(),
        "successor RNG fixture root is missing");
    ++rng_root->actions.front()
          .scopes.front()
          .groups.front()
          .bank_a.actions.back()
          .samples.front()
          .macro_seed;
    expect_throws<std::runtime_error>(
        [&] {
            static_cast<void>(
                audit::testing::serialize_evidence_bundle(
                    candidate_keyed_rng));
        },
        "candidate-keyed common-world RNG was accepted");
}

void test_seed_coordinate_ownership_is_global() {
    audit::testing::validate_seed_coordinate_ownership({
        {17, "root/a/world/0"},
        {17, "root/a/world/0"},
        {29, "successor/a/world/0"},
    });
    expect_throws<std::runtime_error>(
        [] {
            audit::testing::
                validate_seed_coordinate_ownership({
                    {17, "root/a/world/0"},
                    {17, "successor/a/world/0"},
                });
        },
        "numeric seed collision across semantic coordinates "
        "was accepted");
}

void test_witnesses_are_payload_and_coordinate_bound() {
    audit::RunReport swapped = complete_report();
    std::swap(
        swapped.scientific.roots[0]
            .hidden_repartition_witness,
        swapped.scientific.roots[1]
            .hidden_repartition_witness);
    expect_report_rejected(
        std::move(swapped),
        "substituted root witnesses were accepted");

    audit::RunReport forged = complete_report();
    const std::string forged_digest =
        integrity::sha256_string("forged-equal-digest");
    auto& forged_witness =
        forged.scientific.roots.front()
            .descriptor_order_witness;
    forged_witness.baseline_sha256 = forged_digest;
    forged_witness.comparison_sha256 = forged_digest;
    expect_report_rejected(
        std::move(forged),
        "equal but non-rederived witness was accepted");

    audit::RunReport successor = complete_report();
    add_successor_group_to_first_root(successor);
    auto& scopes = successor.scientific
                       .successor_feature_evaluations
                       .front()
                       .scopes;
    std::swap(
        scopes[0]
            .representative_reconstruction_witnesses[0],
        scopes[1]
            .representative_reconstruction_witnesses[0]);
    expect_report_rejected(
        std::move(successor),
        "substituted feature-scope member witnesses were "
        "accepted");

    const auto first_group =
        [](audit::RunReport& report)
            -> audit::SuccessorGroupEvidence& {
            const auto root = std::find_if(
                report.scientific.roots.begin(),
                report.scientific.roots.end(),
                [](const audit::RootEvidence& value) {
                    return value.stable_id ==
                           "green.develop-bears.v3";
                });
            if (root == report.scientific.roots.end() ||
                root->actions.empty() ||
                root->actions.front().scopes.empty() ||
                root->actions.front()
                    .scopes.front()
                    .groups.empty()) {
                throw std::runtime_error(
                    "successor group witness fixture is "
                    "missing");
            }
            return root->actions.front()
                .scopes.front()
                .groups.front();
        };
    const auto forge_self_equal =
        [&](audit::BitIdentityEvidence& witness) {
            witness.baseline_sha256 = forged_digest;
            witness.comparison_sha256 = forged_digest;
        };

    audit::RunReport forged_group_member =
        complete_report();
    add_successor_group_to_first_root(
        forged_group_member);
    forge_self_equal(
        first_group(forged_group_member)
            .representative_reconstruction_witnesses
            .front());
    expect_report_rejected(
        std::move(forged_group_member),
        "self-equal forged group-member reconstruction "
        "witness was accepted");

    audit::RunReport forged_group_hidden =
        complete_report();
    add_successor_group_to_first_root(
        forged_group_hidden);
    forge_self_equal(
        first_group(forged_group_hidden)
            .hidden_repartition_witness);
    expect_report_rejected(
        std::move(forged_group_hidden),
        "self-equal forged group-hidden reconstruction "
        "witness was accepted");

    audit::RunReport relabeled_group_hidden =
        complete_report();
    add_successor_group_to_first_root(
        relabeled_group_hidden);
    first_group(relabeled_group_hidden)
        .hidden_repartition_witness.coordinate =
        "wrong-group/hidden/member/999/wrong-action";
    expect_report_rejected(
        std::move(relabeled_group_hidden),
        "group-hidden reconstruction witness was reusable "
        "under a different representative coordinate");

    audit::RunReport forged_feature_member =
        complete_report();
    add_successor_group_to_first_root(
        forged_feature_member);
    forge_self_equal(
        forged_feature_member.scientific
            .successor_feature_evaluations.front()
            .scopes.front()
            .representative_reconstruction_witnesses
            .front());
    expect_report_rejected(
        std::move(forged_feature_member),
        "self-equal forged feature-member reconstruction "
        "witness was accepted");

    audit::RunReport forged_feature_hidden =
        complete_report();
    add_successor_group_to_first_root(
        forged_feature_hidden);
    forge_self_equal(
        forged_feature_hidden.scientific
            .successor_feature_evaluations.front()
            .scopes.front()
            .hidden_repartition_witness);
    expect_report_rejected(
        std::move(forged_feature_hidden),
        "self-equal forged feature-hidden reconstruction "
        "witness was accepted");

    audit::RunReport relabeled_feature_hidden =
        complete_report();
    add_successor_group_to_first_root(
        relabeled_feature_hidden);
    relabeled_feature_hidden.scientific
        .successor_feature_evaluations.front()
        .scopes.front()
        .hidden_repartition_witness.coordinate =
        "wrong-feature/hidden/member/999/wrong-action";
    expect_report_rejected(
        std::move(relabeled_feature_hidden),
        "feature-hidden reconstruction witness was reusable "
        "under a different representative coordinate");
}

void test_hidden_repartition_eligibility_is_truthful() {
    audit::RunReport homogeneous = complete_report();
    add_successor_group_to_first_root(homogeneous);
    for (auto& root : homogeneous.scientific.roots) {
        for (auto& action : root.actions) {
            for (auto& scope : action.scopes) {
                for (auto& group : scope.groups) {
                    group.hidden_repartition_eligible = false;
                    group.hidden_identity_changed = false;
                }
            }
        }
    }
    for (auto& evaluation :
         homogeneous.scientific
             .successor_feature_evaluations) {
        for (auto& scope : evaluation.scopes) {
            scope.hidden_repartition_eligible = false;
            scope.hidden_identity_changed = false;
        }
    }
    rebind_report_witnesses(homogeneous);
    homogeneous.gate = audit::evaluate_gate(
        homogeneous.scientific, homogeneous.integrity);
    static_cast<void>(
        audit::testing::serialize_evidence_bundle(
            homogeneous));

    audit::RunReport unchanged_eligible = homogeneous;
    auto group_root = std::find_if(
        unchanged_eligible.scientific.roots.begin(),
        unchanged_eligible.scientific.roots.end(),
        [](const audit::RootEvidence& root) {
            return root.stable_id ==
                   "green.develop-bears.v3";
        });
    expect(
        group_root !=
            unchanged_eligible.scientific.roots.end(),
        "homogeneous group fixture root is missing");
    auto& group = group_root->actions.front()
                      .scopes.front()
                      .groups.front();
    group.hidden_repartition_eligible = true;
    group.hidden_identity_changed = false;
    rebind_report_witnesses(unchanged_eligible);
    unchanged_eligible.gate = audit::evaluate_gate(
        unchanged_eligible.scientific,
        unchanged_eligible.integrity);
    expect_report_rejected(
        std::move(unchanged_eligible),
        "eligible but unchanged group hidden identity was "
        "accepted");

    audit::RunReport feature_unchanged = homogeneous;
    auto& feature_scope =
        feature_unchanged.scientific
            .successor_feature_evaluations.front()
            .scopes.front();
    feature_scope.hidden_repartition_eligible = true;
    feature_scope.hidden_identity_changed = false;
    rebind_report_witnesses(feature_unchanged);
    feature_unchanged.gate = audit::evaluate_gate(
        feature_unchanged.scientific,
        feature_unchanged.integrity);
    expect_report_rejected(
        std::move(feature_unchanged),
        "eligible but unchanged feature hidden identity was "
        "accepted");
}

void test_sizes_bounds_and_terminal_bits_fail_closed() {
    audit::RunReport truncated = complete_report();
    truncated.scientific.roots.front()
        .actions.front()
        .root_transitions.pop_back();
    expect_report_rejected(
        std::move(truncated),
        "truncated root transition bank was accepted");

    audit::RunReport terminal_oob = complete_report();
    terminal_oob.scientific.roots.front()
        .actions.front()
        .scopes.front()
        .terminals.front()
        .world_index = audit::kRootWorlds;
    expect_report_rejected(
        std::move(terminal_oob),
        "out-of-range terminal membership was accepted");

    audit::RunReport nonterminal_value = complete_report();
    nonterminal_value.scientific.roots.front()
        .actions.front()
        .root_transitions.front()
        .terminal_root_owner_value_bits =
        std::bit_cast<std::uint64_t>(0.25);
    expect_report_rejected(
        std::move(nonterminal_value),
        "noncanonical root terminal value was accepted");

    audit::RunReport missing_forced = complete_report();
    missing_forced.scientific.roots.front()
        .actions.front()
        .root_transitions.front()
        .forced_root_action_applied = false;
    expect_report_rejected(
        std::move(missing_forced),
        "root transition without a forced action was accepted");

    audit::RunReport zero_counter = complete_report();
    zero_counter.scientific.roots.front()
        .actions.front()
        .root_transitions.front()
        .actions_applied = 0;
    expect_report_rejected(
        std::move(zero_counter),
        "zero successful root action counter was accepted");

    audit::RunReport excessive_counter = complete_report();
    excessive_counter.scientific.roots.front()
        .actions.front()
        .root_transitions.front()
        .phase_transitions =
        audit::kMaximumPhaseTransitions + 1;
    expect_report_rejected(
        std::move(excessive_counter),
        "out-of-budget root transition was accepted");

    audit::RunReport empty_dominance = complete_report();
    empty_dominance.scientific.dominance_pairs.front()
        .worlds.clear();
    expect_report_rejected(
        std::move(empty_dominance),
        "empty dominance world bank was accepted");

    audit::RunReport successor = complete_report();
    add_successor_group_to_first_root(successor);
    auto group_root = std::find_if(
        successor.scientific.roots.begin(),
        successor.scientific.roots.end(),
        [](const audit::RootEvidence& root) {
            return root.stable_id ==
                   "green.develop-bears.v3";
        });
    expect(
        group_root != successor.scientific.roots.end(),
        "successor shape fixture root is missing");
    group_root->actions.front()
        .scopes.front()
        .groups.front()
        .bank_a.actions.front()
        .samples.pop_back();
    rebind_report_witnesses(successor);
    successor.gate = audit::evaluate_gate(
        successor.scientific, successor.integrity);
    expect_report_rejected(
        std::move(successor),
        "truncated successor sample bank was accepted");

    audit::RunReport bad_leaf = complete_report();
    add_successor_group_to_first_root(bad_leaf);
    auto leaf_root = std::find_if(
        bad_leaf.scientific.roots.begin(),
        bad_leaf.scientific.roots.end(),
        [](const audit::RootEvidence& root) {
            return root.stable_id ==
                   "green.develop-bears.v3";
        });
    auto& leaf = leaf_root->actions.front()
                     .scopes.front()
                     .groups.front()
                     .bank_a.actions.front()
                     .samples.front();
    leaf.terminal = true;
    leaf.critic_evaluated = false;
    leaf.score_bits =
        std::bit_cast<std::uint64_t>(0.25);
    leaf.contextual_score_bits = 0;
    leaf.legacy_score_bits = 0;
    rebind_report_witnesses(bad_leaf);
    bad_leaf.gate = audit::evaluate_gate(
        bad_leaf.scientific, bad_leaf.integrity);
    expect_report_rejected(
        std::move(bad_leaf),
        "noncanonical successor terminal value was accepted");

    audit::RunReport missing_catalog = complete_report();
    add_successor_group_to_first_root(missing_catalog);
    missing_catalog.scientific
        .successor_feature_evaluations.front()
        .scopes.back()
        .representative_catalog.pop_back();
    rebind_report_witnesses(missing_catalog);
    missing_catalog.gate = audit::evaluate_gate(
        missing_catalog.scientific,
        missing_catalog.integrity);
    expect_report_rejected(
        std::move(missing_catalog),
        "incomplete zero-mass representative catalog was "
        "accepted");
}

void test_dominance_failure_is_scientific_not_infrastructure() {
    audit::RunReport rejected = complete_report();
    auto primary = std::find_if(
        rejected.scientific.dominance_pairs.begin(),
        rejected.scientific.dominance_pairs.end(),
        [](const audit::DominancePairEvidence& pair) {
            return pair.role == audit::GateRole::Primary;
        });
    expect(
        primary !=
            rejected.scientific.dominance_pairs.end(),
        "primary dominance fixture is missing");
    for (auto& world : primary->worlds) {
        world.comparison.second
            .owner_observable_consequence =
            "different-consequence";
        world.comparison.consequences_equal = false;
        world.comparison.orientation =
            old_school::fq0_dominance::Orientation::
                Incomparable;
        world.orientation =
            old_school::fq0_dominance::Orientation::
                Incomparable;
    }
    primary->matching_worlds = 0;
    primary->passed = false;
    rejected.scientific.primary_passed = false;
    rejected.scientific.passed = false;
    rebind_report_witnesses(rejected);
    rejected.gate = audit::evaluate_gate(
        rejected.scientific, rejected.integrity);
    expect(
        !rejected.gate.infrastructure_failure &&
            audit::exit_code(rejected.gate) == 1,
        "legitimate dominance miss was not scientific exit one");
    const audit::EvidenceBundle bundle =
        audit::testing::serialize_evidence_bundle(rejected);
    expect(
        bundle.bytes.find("verdict\texit_code\t1\n") !=
            std::string::npos,
        "scientific dominance rejection was not serialized");

    audit::RunReport descriptive = complete_report();
    auto descriptive_pair = std::find_if(
        descriptive.scientific.dominance_pairs.begin(),
        descriptive.scientific.dominance_pairs.end(),
        [](const audit::DominancePairEvidence& pair) {
            return pair.role == audit::GateRole::Descriptive;
        });
    expect(
        descriptive_pair !=
            descriptive.scientific.dominance_pairs.end(),
        "descriptive dominance fixture is missing");
    for (auto& world : descriptive_pair->worlds) {
        world.comparison.first.valid = false;
        world.comparison.first_normalized = false;
        world.comparison.first
            .owner_observable_consequence.clear();
        world.comparison.consequences_equal = false;
        world.comparison.orientation =
            old_school::fq0_dominance::Orientation::
                Incomparable;
        world.orientation =
            old_school::fq0_dominance::Orientation::
                Incomparable;
    }
    rebind_report_witnesses(descriptive);
    descriptive.gate = audit::evaluate_gate(
        descriptive.scientific, descriptive.integrity);
    expect(
        descriptive.gate.passed &&
            audit::exit_code(descriptive.gate) == 0,
        "invalid descriptive settlement became gating");
    const audit::EvidenceBundle descriptive_bundle =
        audit::testing::serialize_evidence_bundle(
            descriptive);
    expect(
        descriptive_bundle.bytes.find(
            "verdict\texit_code\t0\n") !=
            std::string::npos,
        "invalid descriptive settlement did not serialize");

    audit::RunReport gated_invalid = complete_report();
    auto gated = std::find_if(
        gated_invalid.scientific.dominance_pairs.begin(),
        gated_invalid.scientific.dominance_pairs.end(),
        [](const audit::DominancePairEvidence& pair) {
            return pair.role == audit::GateRole::Primary;
        });
    for (auto& world : gated->worlds) {
        world.comparison.first.valid = false;
        world.comparison.first_normalized = false;
        world.comparison.first
            .owner_observable_consequence.clear();
        world.comparison.consequences_equal = false;
        world.comparison.orientation =
            old_school::fq0_dominance::Orientation::
                Incomparable;
        world.orientation =
            old_school::fq0_dominance::Orientation::
                Incomparable;
    }
    gated->matching_worlds = 0;
    gated->passed = false;
    gated_invalid.scientific.primary_passed = false;
    gated_invalid.scientific.passed = false;
    rebind_report_witnesses(gated_invalid);
    gated_invalid.gate = audit::evaluate_gate(
        gated_invalid.scientific,
        gated_invalid.integrity);
    expect(
        !gated_invalid.gate.infrastructure_failure &&
            audit::exit_code(gated_invalid.gate) == 1,
        "invalid primary settlement was not scientific exit one");
    const audit::EvidenceBundle gated_bundle =
        audit::testing::serialize_evidence_bundle(
            gated_invalid);
    expect(
        gated_bundle.bytes.find(
            "verdict\texit_code\t1\n") !=
            std::string::npos,
        "invalid primary settlement did not serialize");

    audit::RunReport incomparable_invalid =
        complete_report();
    auto incomparable = std::find_if(
        incomparable_invalid.scientific.dominance_pairs.begin(),
        incomparable_invalid.scientific.dominance_pairs.end(),
        [](const audit::DominancePairEvidence& pair) {
            return pair.role ==
                       audit::GateRole::
                           IncomparableControlGuard &&
                   pair.stable_id ==
                       "control.blue.force-spike-live-gray-ogre.v1";
        });
    expect(
        incomparable !=
            incomparable_invalid.scientific
                .dominance_pairs.end(),
        "named incomparable dominance fixture is missing");
    for (auto& world : incomparable->worlds) {
        world.comparison.first.valid = false;
        world.comparison.first_normalized = false;
        world.comparison.first
            .owner_observable_consequence.clear();
        world.comparison.consequences_equal = false;
        world.comparison.orientation =
            old_school::fq0_dominance::Orientation::
                Incomparable;
        world.orientation =
            old_school::fq0_dominance::Orientation::
                Incomparable;
    }
    incomparable->passed = false;
    incomparable_invalid.scientific
        .reject_only_guards_passed = false;
    incomparable_invalid.scientific.passed = false;
    rebind_report_witnesses(incomparable_invalid);
    incomparable_invalid.gate = audit::evaluate_gate(
        incomparable_invalid.scientific,
        incomparable_invalid.integrity);
    expect(
        incomparable->matching_worlds == audit::kRootWorlds &&
            !incomparable_invalid.gate
                 .infrastructure_failure &&
            audit::exit_code(incomparable_invalid.gate) == 1,
        "invalid named-incomparable settlements passed "
        "vacuously or became infrastructure failure");
    const audit::EvidenceBundle incomparable_bundle =
        audit::testing::serialize_evidence_bundle(
            incomparable_invalid);
    expect(
        incomparable_bundle.bytes.find(
            "verdict\texit_code\t1\n") !=
            std::string::npos,
        "invalid named-incomparable guard did not serialize");
}

void test_semantic_forgeries_fail_closed() {
    const audit::RunReport complete = complete_report();
    expect(
        std::count_if(
            complete.scientific.contrasts.begin(),
            complete.scientific.contrasts.end(),
            [](const audit::ContrastEvidence& contrast) {
                return contrast.role ==
                       audit::GateRole::XZeroGuard;
            }) == 6,
        "valid fixture does not cover all six typed X=0 "
        "actions");
    const auto rejected =
        [&](audit::RunReport report,
            std::string_view message) {
            expect_throws<std::runtime_error>(
                [&] {
                    static_cast<void>(
                        audit::testing::
                            serialize_evidence_bundle(
                                report));
                },
                message);
        };

    audit::RunReport missing_contrasts = complete;
    missing_contrasts.scientific.contrasts.clear();
    rejected(
        std::move(missing_contrasts),
        "zero-contrast report was accepted");

    audit::RunReport missing_x_zero = complete;
    const auto x_zero = std::find_if(
        missing_x_zero.scientific.contrasts.begin(),
        missing_x_zero.scientific.contrasts.end(),
        [](const audit::ContrastEvidence& contrast) {
            return contrast.role ==
                       audit::GateRole::XZeroGuard &&
                   contrast.stable_id ==
                       "ru.disintegrate-player-x.v3";
        });
    expect(
        x_zero !=
            missing_x_zero.scientific.contrasts.end(),
        "typed X=0 contrast fixture is missing");
    missing_x_zero.scientific.contrasts.erase(x_zero);
    rejected(
        std::move(missing_x_zero),
        "incomplete typed six-action X=0 census was accepted");

    audit::RunReport duplicate_x_zero = complete;
    const auto first_x_zero = std::find_if(
        duplicate_x_zero.scientific.contrasts.begin(),
        duplicate_x_zero.scientific.contrasts.end(),
        [](const audit::ContrastEvidence& contrast) {
            return contrast.role ==
                   audit::GateRole::XZeroGuard;
        });
    expect(
        first_x_zero !=
            duplicate_x_zero.scientific.contrasts.end(),
        "typed X=0 duplicate fixture is missing");
    audit::ContrastEvidence duplicate = *first_x_zero;
    duplicate.name += "-duplicate";
    duplicate_x_zero.scientific.contrasts.push_back(
        std::move(duplicate));
    std::sort(
        duplicate_x_zero.scientific.contrasts.begin(),
        duplicate_x_zero.scientific.contrasts.end(),
        [](const audit::ContrastEvidence& first,
           const audit::ContrastEvidence& second) {
            return std::tuple{
                       static_cast<std::size_t>(first.role),
                       first.stable_id, first.name} <
                   std::tuple{
                       static_cast<std::size_t>(second.role),
                       second.stable_id, second.name};
        });
    rejected(
        std::move(duplicate_x_zero),
        "duplicate typed X=0 contrast row was accepted");

    audit::RunReport substituted_x_zero = complete;
    std::vector<audit::ContrastEvidence*> blue_x_zero;
    for (audit::ContrastEvidence& contrast :
         substituted_x_zero.scientific.contrasts) {
        if (contrast.role == audit::GateRole::XZeroGuard &&
            contrast.stable_id ==
                "control.blue.braingeyser-x0.v1") {
            blue_x_zero.push_back(&contrast);
        }
    }
    expect(
        blue_x_zero.size() == 2,
        "typed X=0 substitution fixture is incomplete");
    blue_x_zero[1]->negative_descriptor =
        blue_x_zero[0]->negative_descriptor;
    blue_x_zero[1]->contrast =
        old_school::fq0_bellman::summarize_block_contrast(
            report_action(
                substituted_x_zero,
                blue_x_zero[1]->stable_id,
                blue_x_zero[1]->positive_descriptor)
                .target,
            report_action(
                substituted_x_zero,
                blue_x_zero[1]->stable_id,
                blue_x_zero[1]->negative_descriptor)
                .target);
    blue_x_zero[1]->support_condition = true;
    blue_x_zero[1]->directional_passed = true;
    rejected(
        std::move(substituted_x_zero),
        "substituted typed X=0 contrast row was accepted");

    audit::RunReport missing_dominance = complete;
    missing_dominance.scientific.dominance_pairs.pop_back();
    rejected(
        std::move(missing_dominance),
        "non-exhaustive dominance census was accepted");

    audit::RunReport forged_dominance = complete;
    const auto strict = std::find_if(
        forged_dominance.scientific.dominance_pairs.begin(),
        forged_dominance.scientific.dominance_pairs.end(),
        [](const audit::DominancePairEvidence& pair) {
            return pair.role == audit::GateRole::Primary;
        });
    expect(
        strict !=
            forged_dominance.scientific.dominance_pairs.end(),
        "strict dominance fixture is missing");
    strict->worlds.front()
        .comparison.second
        .owner_observable_consequence += "-forged";
    strict->worlds.front()
        .comparison.consequences_equal = false;
    rejected(
        std::move(forged_dominance),
        "dominance orientation detached from raw costs was "
        "accepted");

    audit::RunReport missing_feature = complete;
    missing_feature.scientific.feature_rows.pop_back();
    rejected(
        std::move(missing_feature),
        "incomplete feature corpus was accepted");

    audit::RunReport forged_ranking = complete;
    auto& ranking =
        forged_ranking.scientific.c16_ranking_changes
            .roots.front();
    const auto non_support = std::find_if(
        ranking.actions.begin(), ranking.actions.end(),
        [&](const audit::C16ActionRankingEvidence&
                action) {
            return std::find(
                       ranking.exact_support.begin(),
                       ranking.exact_support.end(),
                       action.descriptor) ==
                   ranking.exact_support.end();
        });
    expect(
        non_support != ranking.actions.end(),
        "ranking forgery fixture has no non-support action");
    for (auto& action : ranking.actions) {
        action.score_bits =
            std::bit_cast<std::uint64_t>(
                &action == &*non_support ? 1.0 : 0.0);
    }
    rejected(
        std::move(forged_ranking),
        "C16 ranking summary detached from raw scores was "
        "accepted");

    audit::RunReport wrong_manifest = complete;
    wrong_manifest.scientific.manifest.roots.front()
        .probe.root_player =
        1 - wrong_manifest.scientific.manifest.roots.front()
                .probe.root_player;
    rejected(
        std::move(wrong_manifest),
        "non-frozen manifest was accepted");

    audit::RunReport candidate_seeded = complete;
    ++candidate_seeded.scientific.roots.front()
          .actions.front()
          .root_transitions.front()
          .macro_seed;
    rejected(
        std::move(candidate_seeded),
        "non-derived root transition seed was accepted");

    audit::RunReport unbound_transition = complete;
    unbound_transition.scientific.roots.front()
        .actions.front()
        .root_transitions.front()
        .terminal = false;
    unbound_transition.scientific.roots.front()
        .actions.front()
        .root_transitions.front()
        .terminal_root_owner_value_bits = 0;
    unbound_transition.scientific.roots.front()
        .actions.front()
        .root_transitions.front()
        .successor_information_set_fingerprint =
        integrity::sha256_string("forged-successor");
    unbound_transition.scientific.roots.front()
        .actions.front()
        .root_transitions.front()
        .redacted_result_hash =
        unbound_transition.scientific.roots.front()
            .actions.front()
            .root_transitions.front()
            .successor_information_set_fingerprint;
    rejected(
        std::move(unbound_transition),
        "transition classification detached from scope "
        "accounting was accepted");

    audit::RunReport unwitnessed = complete;
    unwitnessed.scientific.invariance
        .thread_count_witness.comparison_sha256[0] =
        unwitnessed.scientific.invariance
                    .thread_count_witness
                    .comparison_sha256[0] == '0'
            ? '1'
            : '0';
    unwitnessed.gate = audit::evaluate_gate(
        unwitnessed.scientific, unwitnessed.integrity);
    expect(
        audit::exit_code(unwitnessed.gate) == 2,
        "failed invariance witness was not infrastructure "
        "exit two");
    rejected(
        std::move(unwitnessed),
        "failed invariance witness was publishable");

    audit::RunReport missing_zero_mass = complete;
    add_successor_group_to_first_root(
        missing_zero_mass);
    missing_zero_mass.scientific
        .successor_feature_evaluations.front()
        .scopes.pop_back();
    rejected(
        std::move(missing_zero_mass),
        "missing zero-mass feature-only K8 stream was "
        "accepted");
}

void test_atomic_writer_refuses_replace_and_symlinks() {
    TemporaryDirectory temporary;
    const auto target = temporary.path() / "evidence.tsv";
    audit::testing::write_evidence_atomic_no_replace(
        target.string(), "sealed\n");
    expect(
        read_file(target) == "sealed\n" &&
            !std::filesystem::exists(
                target.string() + ".tmp"),
        "atomic writer did not publish exact bytes");
    expect_throws<std::runtime_error>(
        [&] {
            audit::testing::write_evidence_atomic_no_replace(
                target.string(), "replacement\n");
        },
        "atomic writer replaced its target");
    expect(
        read_file(target) == "sealed\n",
        "no-replace failure mutated its target");

    const auto occupied =
        temporary.path() / "occupied.tsv";
    write_file(occupied.string() + ".tmp", "sentinel");
    expect_throws<std::runtime_error>(
        [&] {
            audit::testing::write_evidence_atomic_no_replace(
                occupied.string(), "new\n");
        },
        "atomic writer accepted an occupied temporary");
    expect(
        !std::filesystem::exists(occupied) &&
            read_file(occupied.string() + ".tmp") ==
                "sentinel",
        "temporary collision mutated filesystem state");

    const auto referent =
        temporary.path() / "private";
    write_file(referent, "private");
    const auto link =
        temporary.path() / "evidence-link.tsv";
    std::filesystem::create_symlink(referent, link);
    expect_throws<std::runtime_error>(
        [&] {
            audit::testing::write_evidence_atomic_no_replace(
                link.string(), "replacement\n");
        },
        "atomic writer followed a final symlink");
    expect(
        read_file(referent) == "private",
        "final symlink rejection mutated its referent");

    const auto real_parent =
        temporary.path() / "real-parent";
    std::filesystem::create_directory(real_parent);
    const auto parent_link =
        temporary.path() / "parent-link";
    std::filesystem::create_directory_symlink(
        real_parent, parent_link);
    expect_throws<std::runtime_error>(
        [&] {
            audit::testing::write_evidence_atomic_no_replace(
                (parent_link / "evidence.tsv").string(),
                "replacement\n");
        },
        "atomic writer followed a parent symlink");
    expect(
        !std::filesystem::exists(
            real_parent / "evidence.tsv"),
        "parent symlink rejection published a target");

    const auto missing_parent =
        temporary.path() / "missing" / "nested";
    const auto nested_target =
        missing_parent / "evidence.tsv";
    audit::testing::write_evidence_atomic_no_replace(
        nested_target.string(), "nested\n");
    expect(
        read_file(nested_target) == "nested\n",
        "verified parent creation did not publish");
}

void test_publication_binds_fresh_parent_and_fingerprint() {
    TemporaryDirectory temporary;
    const auto model = temporary.path() / "model.bin";
    write_file(model, "frozen model bytes");
    const auto expected =
        integrity::snapshot_regular_file(model);
    const audit::EvidenceBundle bundle =
        minimal_bound_bundle(expected);
    expect(
        audit::testing::validate_evidence_bundle(
            bundle.bytes) == bundle,
        "synthetic publication bundle is malformed");

    const auto wrong_fingerprint_target =
        temporary.path() / "wrong-fingerprint.tsv";
    expect_throws<std::runtime_error>(
        [&] {
            static_cast<void>(
                audit::testing::publish_evidence_for_parent(
                    wrong_fingerprint_target.string(),
                    bundle, expected,
                    audit::kModelFingerprint,
                    std::string(64, '0')));
        },
        "publication accepted a different model fingerprint");
    expect(
        !std::filesystem::exists(
            wrong_fingerprint_target),
        "fingerprint rejection published evidence");

    write_file(model, "mutated model bytes");
    const auto stale_target =
        temporary.path() / "stale.tsv";
    expect_throws<std::runtime_error>(
        [&] {
            static_cast<void>(
                audit::testing::publish_evidence_for_parent(
                    stale_target.string(), bundle, expected,
                    audit::kModelFingerprint,
                    audit::kModelFingerprint));
        },
        "publication accepted a stale parent snapshot");
    expect(
        !std::filesystem::exists(stale_target) &&
            !std::filesystem::exists(
                stale_target.string() + ".tmp"),
        "stale-parent rejection left publication state");

    write_file(model, "frozen model bytes");
    const auto fresh =
        integrity::snapshot_regular_file(model);
    const audit::EvidenceBundle fresh_bundle =
        minimal_bound_bundle(fresh);
    const auto published_target =
        temporary.path() / "published.tsv";
    const audit::EvidencePublication publication =
        audit::testing::publish_evidence_for_parent(
            published_target.string(), fresh_bundle, fresh,
            audit::kModelFingerprint,
            audit::kModelFingerprint);
    expect(
        publication.published &&
            publication.atomic_no_replace &&
            publication.byte_size ==
                fresh_bundle.bytes.size() &&
            publication.sha256 ==
                integrity::sha256_string(
                    fresh_bundle.bytes) &&
            read_file(published_target) ==
                fresh_bundle.bytes,
        "fresh parent did not produce a bound publication");
}

} // namespace

int main() {
    const std::vector<
        std::pair<std::string, std::function<void()>>>
        tests = {
            {"gate and exit codes",
             test_gate_exit_codes_and_internal_consistency},
            {"canonical exact bundle",
             test_bundle_is_canonical_exact_and_tree_independent},
            {"corruption and trailing rejection",
             test_bundle_rejects_corruption_and_trailing_data},
            {"raw successor group rows",
             test_group_rows_are_raw_and_recomputed},
            {"global seed coordinate ownership",
             test_seed_coordinate_ownership_is_global},
            {"payload-bound witnesses",
             test_witnesses_are_payload_and_coordinate_bound},
            {"truthful hidden eligibility",
             test_hidden_repartition_eligibility_is_truthful},
            {"shape bounds and terminal bits",
             test_sizes_bounds_and_terminal_bits_fail_closed},
            {"dominance failure disposition",
             test_dominance_failure_is_scientific_not_infrastructure},
            {"semantic forgeries fail closed",
             test_semantic_forgeries_fail_closed},
            {"atomic no-replace and symlinks",
             test_atomic_writer_refuses_replace_and_symlinks},
            {"fresh parent publication",
             test_publication_binds_fresh_parent_and_fingerprint},
        };
    std::size_t passed = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "PASS: " << name << '\n';
        } catch (const std::exception& failure) {
            std::cerr
                << "FAIL: " << name << ": "
                << failure.what() << '\n';
        }
    }
    std::cout << passed << '/' << tests.size()
              << " tests passed\n";
    return passed == tests.size() ? 0 : 1;
}
