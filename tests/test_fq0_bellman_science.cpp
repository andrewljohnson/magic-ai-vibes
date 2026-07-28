#include "old_school/fq0_bellman_science.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/fq0_bellman.hpp"
#include "old_school/fq0_information_set.hpp"
#include "old_school/probes.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace science = old_school::fq0_bellman_science;
namespace bellman = old_school::fq0_bellman;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Function>
void expect_invalid(
    Function&& function, std::string_view message) {
    try {
        std::forward<Function>(function)();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

bool same_bits(double first, double second) {
    return std::bit_cast<std::uint64_t>(first) ==
           std::bit_cast<std::uint64_t>(second);
}

class LegacyDigestWriter {
  public:
    void text(std::string_view value) {
        integer(value.size());
        bytes_.append(value);
    }

    template <typename Integer>
    void integer(Integer value) {
        static_assert(std::is_integral_v<Integer>);
        using Unsigned = std::make_unsigned_t<Integer>;
        std::uint64_t bits = static_cast<std::uint64_t>(
            static_cast<Unsigned>(value));
        for (std::size_t index = 0;
             index < sizeof(Unsigned); ++index) {
            bytes_.push_back(static_cast<char>(
                bits & std::uint64_t{0xff}));
            bits >>= 8U;
        }
    }

    void boolean(bool value) {
        integer<std::uint8_t>(value ? 1 : 0);
    }

    void real(double value) {
        integer(std::bit_cast<std::uint64_t>(value));
    }

    std::string sha256() const {
        return old_school::artifact_integrity::sha256_string(
            bytes_);
    }

  private:
    std::string bytes_;
};

std::string legacy_digest_writer_framing_fixture_sha256() {
    LegacyDigestWriter writer;
    writer.text(
        "old-school-fq0-digest-writer-framing-fixture-v1");
    writer.integer<std::uint8_t>(0xa5U);
    writer.integer<std::uint16_t>(0xb60cU);
    writer.integer<std::uint32_t>(0xd70e1f20U);
    writer.integer<std::uint64_t>(0xe80123456789abcdULL);
    writer.integer<std::size_t>(0x01020304U);
    writer.boolean(false);
    writer.boolean(true);
    writer.real(1.25);
    writer.real(-0.0);
    writer.text(std::string_view("left\0right", 10));
    std::string boundary_text(64 * 1024 + 257, '\0');
    for (std::size_t index = 0;
         index < boundary_text.size(); ++index) {
        boundary_text[index] = static_cast<char>(
            (index * 37U + 11U) & 0xffU);
    }
    writer.text(boundary_text);
    return writer.sha256();
}

void test_streaming_digest_preserves_legacy_framing() {
    expect(
        science::testing::
                digest_writer_framing_fixture_sha256() ==
            legacy_digest_writer_framing_fixture_sha256(),
        "streaming digest changed legacy primitive framing");
}

old_school::probes::DecisionProbe test_probe() {
    std::vector<old_school::probes::DecisionProbe> probes =
        old_school::probes::make_probe_dev_v3();
    const auto found = std::find_if(
        probes.begin(), probes.end(),
        [](const old_school::probes::DecisionProbe& probe) {
            return probe.stable_id ==
                   "white.establish-millstone.v3";
        });
    if (found == probes.end()) {
        throw std::runtime_error(
            "test Priority root is missing");
    }
    return *found;
}

old_school::ac1_teacher_audit::Manifest
test_manifest(bool permute_observation_and_candidates) {
    old_school::probes::DecisionProbe probe = test_probe();
    if (permute_observation_and_candidates) {
        std::reverse(
            probe.state.players[probe.root_player].hand.begin(),
            probe.state.players[probe.root_player].hand.end());
        std::reverse(
            probe.candidates.begin(),
            probe.candidates.end());
    }
    old_school::ac1_teacher_audit::Manifest manifest;
    manifest.roots.push_back({
        .probe = probe,
        .information_action_fingerprint =
            old_school::probes::
                bsr_information_action_fingerprint(probe),
        .factory_contract_fingerprint =
            "test-only-contract",
        .from_dev_v3 = true,
    });
    manifest.physical_roots_by_deck[
        static_cast<std::size_t>(
            old_school::DeckId::White)] = 1;
    return manifest;
}

old_school::ac1_teacher_audit::Manifest
braingeyser_hidden_draw_manifest() {
    std::vector<old_school::probes::DecisionProbe> probes =
        old_school::probes::
            make_braingeyser_x_zero_control_v1();
    expect(
        probes.size() == 1,
        "Braingeyser control fixture is missing");
    const std::string information_action_fingerprint =
        old_school::probes::
            bsr_information_action_fingerprint(
                probes.front());
    old_school::ac1_teacher_audit::Manifest manifest;
    manifest.roots.push_back({
        .probe = std::move(probes.front()),
        .information_action_fingerprint =
            information_action_fingerprint,
        .factory_contract_fingerprint =
            "test-braingeyser-hidden-draw-contract",
    });
    manifest.physical_roots_by_deck[
        static_cast<std::size_t>(
            old_school::DeckId::Blue)] = 1;
    return manifest;
}

old_school::ac1_teacher_audit::Manifest
homogeneous_hidden_manifest() {
    using old_school::CardId;
    old_school::probes::DecisionProbe probe;
    probe.stable_id =
        "test.homogeneous-hidden-priority.v1";
    probe.category =
        old_school::probes::Category::GreenDevelop;
    probe.decision_kind =
        old_school::probes::DecisionKind::Priority;
    probe.root_deck = old_school::DeckId::Green;
    probe.opponent_deck = old_school::DeckId::Red;
    probe.root_player = 0;
    probe.phase = old_school::TurnPhase::FirstMain;
    probe.state.active_player = 0;
    probe.state.starting_player = 0;
    probe.state.turn_number = 1;
    probe.state.players[0].hand = {CardId::Forest};
    probe.state.players[0].library.assign(
        39, CardId::Forest);
    probe.state.players[1].hand = {CardId::Mountain};
    probe.state.players[1].library.assign(
        39, CardId::Mountain);
    probe.original_decks[0].assign(40, CardId::Forest);
    probe.original_decks[1].assign(40, CardId::Mountain);
    probe.candidates = {
        {
            .descriptor = "pass",
            .action =
                old_school::PriorityAction::pass(),
        },
        {
            .descriptor = "play-forest",
            .action =
                old_school::PriorityAction::play_land(
                    CardId::Forest),
        },
    };

    old_school::ac1_teacher_audit::Manifest manifest;
    manifest.roots.push_back({
        .probe = probe,
        .information_action_fingerprint =
            old_school::probes::
                bsr_information_action_fingerprint(probe),
        .factory_contract_fingerprint =
            "test-homogeneous-contract",
    });
    manifest.physical_roots_by_deck[
        static_cast<std::size_t>(
            old_school::DeckId::Green)] = 1;
    return manifest;
}

struct Fixture {
    std::shared_ptr<const old_school::LearnedModel> model;
    old_school::ac1_teacher_audit::Manifest manifest;
    science::Construction single_worker;
    science::Construction four_workers;

    Fixture()
        : model(old_school::train_learned_value_champion(
              1, 0xF00D00000000C160ULL)),
          manifest(test_manifest(false)),
          single_worker(
              science::testing::construct_reduced(
                  manifest, model,
                  {
                      .root_seed_base =
                          0xF00D000000001001ULL,
                      .bank_a_seed_base =
                          0xF00D000000001002ULL,
                      .bank_b_seed_base =
                          0xF00D000000001003ULL,
                      .root_worlds =
                          bellman::kBlockCount,
                      .successor_worlds = 1,
                      .workers = 1,
                  })),
          four_workers(
              science::testing::construct_reduced(
                  test_manifest(true), model,
                  {
                      .root_seed_base =
                          0xF00D000000001001ULL,
                      .bank_a_seed_base =
                          0xF00D000000001002ULL,
                      .bank_b_seed_base =
                          0xF00D000000001003ULL,
                      .root_worlds =
                          bellman::kBlockCount,
                      .successor_worlds = 1,
                      .workers = 4,
                  })) {}
};

Fixture& fixture() {
    static Fixture value;
    return value;
}

science::testing::ReducedRecipe fixture_recipe(
    std::size_t workers = 1) {
    return {
        .root_seed_base =
            0xF00D000000001001ULL,
        .bank_a_seed_base =
            0xF00D000000001002ULL,
        .bank_b_seed_base =
            0xF00D000000001003ULL,
        .root_worlds = bellman::kBlockCount,
        .successor_worlds = 1,
        .workers = workers,
    };
}

std::vector<bellman::ActionSamples> bank_samples(
    const science::GroupBank& bank) {
    std::vector<bellman::ActionSamples> result;
    for (const science::GroupAction& action : bank.actions) {
        bellman::ActionSamples row{
            .descriptor = action.descriptor,
            .sample_stream_key = bank.stream_key,
        };
        for (const science::LeafSample& sample :
             action.samples) {
            row.world_indices.push_back(
                sample.world_index);
            row.samples.push_back(sample.score);
        }
        result.push_back(std::move(row));
    }
    return result;
}

const bellman::FeatureTargetRow& feature_row(
    const science::Construction& construction,
    std::string_view row_id) {
    const auto found = std::lower_bound(
        construction.feature_rows.begin(),
        construction.feature_rows.end(), row_id,
        [](const bellman::FeatureTargetRow& row,
           std::string_view id) {
            return row.row_id < id;
        });
    if (found == construction.feature_rows.end() ||
        found->row_id != row_id) {
        throw std::runtime_error(
            "feature evidence row is missing");
    }
    return *found;
}

void test_authoritative_actions_and_common_root_worlds() {
    const Fixture& data = fixture();
    expect(
        data.single_worker.roots.size() == 1,
        "reduced manifest did not produce one root");
    const science::Root& root =
        data.single_worker.roots.front();
    const auto& probe =
        data.manifest.roots.front().probe;
    expect(
        root.actions.size() ==
            probe.candidates.size() &&
            root.sampled_worlds.size() ==
                bellman::kBlockCount,
        "root action/world census is wrong");

    std::map<std::string, old_school::PriorityAction>
        expected;
    for (const old_school::probes::Candidate& candidate :
         probe.candidates) {
        const auto* action =
            std::get_if<old_school::PriorityAction>(
                &candidate.action);
        expect(
            action != nullptr,
            "test manifest contains a non-Priority action");
        expected.emplace(candidate.descriptor, *action);
    }
    std::string previous;
    for (const science::RootAction& action :
         root.actions) {
        expect(
            previous.empty() ||
                previous < action.descriptor,
            "root actions are not descriptor-canonical");
        previous = action.descriptor;
        const auto found = expected.find(
            action.descriptor);
        expect(
            found != expected.end() &&
                found->second == action.action,
            "construction changed a typed root action");
        const bellman::FeatureTargetRow& row =
            feature_row(
                data.single_worker,
                action.feature_row_id);
        expect(
            row.information_set_id ==
                "root/" + root.stable_id + "/" +
                    root
                        .manifest_information_action_fingerprint,
            "root feature information ID is not scoped "
            "independently from its seed group");
        expect(
            !action.canonical_consequence_fingerprint.empty(),
            "root action lacks a consequence hash");
    }

    for (std::size_t world = 0;
         world < root.sampled_worlds.size(); ++world) {
        const std::uint64_t determinization_seed =
            root.sampled_worlds[world]
                .determinization_seed;
        std::uint64_t macro_seed = 0;
        for (std::size_t action_index = 0;
             action_index < root.actions.size();
             ++action_index) {
            const science::RootTransition& transition =
                root.actions[action_index]
                    .root_transitions.at(world);
            expect(
                transition.determinization_seed ==
                    determinization_seed,
                "root determinization depends on candidate");
            expect(
                transition.forced_action_applied &&
                    transition.actions_applied >= 1 &&
                    transition
                            .priority_actions_applied >=
                        1 &&
                    transition
                            .priority_actions_applied <=
                        transition.actions_applied,
                "root transition did not account for "
                "its forced action");
            if (action_index == 0) {
                macro_seed = transition.macro_seed;
            } else {
                expect(
                    transition.macro_seed == macro_seed,
                    "root macro seed depends on candidate");
            }
        }
    }
}

void test_scopes_group_before_max_and_perspective() {
    const science::Construction& construction =
        fixture().single_worker;
    bool observed_successor = false;
    bool observed_opponent_owner = false;
    for (const science::RootAction& action :
         construction.roots.front().actions) {
        expect(
            action.scopes.size() ==
                bellman::kBlockCount + 1,
            "root action lacks Full+8 scopes");
        for (std::size_t scope_index = 0;
             scope_index < action.scopes.size();
             ++scope_index) {
            const science::Scope& scope =
                action.scopes[scope_index];
            expect(
                scope.kind ==
                    (scope_index == 0
                         ? science::ScopeKind::Full
                         : science::ScopeKind::Block) &&
                    scope.block ==
                        (scope_index == 0
                             ? 0
                             : scope_index - 1) &&
                    scope.exact_particle_partition,
                "scope identity or partition is wrong");

            std::vector<bellman::TerminalParticle>
                terminals;
            for (const auto& terminal :
                 scope.terminals) {
                const auto position = std::lower_bound(
                    scope.root_world_indices.begin(),
                    scope.root_world_indices.end(),
                    terminal.world_index);
                terminals.push_back({
                    .world_index =
                        static_cast<std::size_t>(
                            position -
                            scope.root_world_indices.begin()),
                    .root_owner_value =
                        terminal.root_owner_value,
                });
            }
            std::vector<bellman::SuccessorGroup> groups;
            for (const science::SuccessorGroup& group :
                 scope.groups) {
                observed_successor = true;
                observed_opponent_owner |=
                    group.relation ==
                    bellman::OwnerRelation::OpponentOwner;
                expect(
                    group.cross_fit ==
                        bellman::cross_fit_v0(
                            bank_samples(group.bank_a),
                            bank_samples(group.bank_b)),
                    "group V0 is not recomputed from raw "
                    "common-world banks");
                bellman::SuccessorGroup backed{
                    .fingerprint =
                        group
                            .information_set_fingerprint,
                    .mass =
                        group.root_world_indices.size(),
                    .relation = group.relation,
                    .successor_owner_value =
                        group.cross_fit.value,
                };
                for (const std::size_t world :
                     group.root_world_indices) {
                    const auto position =
                        std::lower_bound(
                            scope.root_world_indices.begin(),
                            scope.root_world_indices.end(),
                            world);
                    backed.world_indices.push_back(
                        static_cast<std::size_t>(
                            position -
                            scope
                                .root_world_indices.begin()));
                }
                groups.push_back(std::move(backed));
            }
            const bellman::BackedTarget recomputed =
                bellman::back_up_root_target(
                    scope.root_world_indices.size(),
                    terminals, groups);
            expect(
                recomputed == scope.target,
                "root target was not backed up after "
                "information-set grouping");
        }
    }
    expect(
        observed_successor,
        "reduced construction did not exercise a "
        "successor information set");
    expect(
        observed_opponent_owner,
        "reduced construction did not exercise the "
        "opponent-owner complement");
}

void test_feature_only_banks_and_symmetric_targets() {
    const science::Construction& construction =
        fixture().single_worker;
    expect(
        !construction.successor_feature_evaluations.empty(),
        "successor feature-only census is empty");
    for (const science::SuccessorFeatureEvaluation&
             evaluation :
         construction.successor_feature_evaluations) {
        expect(
            evaluation.scopes.size() ==
                bellman::kBlockCount + 1,
            "successor feature row lacks Full+8 raw scopes");
        for (std::size_t scope_index = 0;
             scope_index < evaluation.scopes.size();
             ++scope_index) {
            const science::SuccessorFeatureScope& scope =
                evaluation.scopes[scope_index];
            expect(
                !scope.bank_a.actions.empty() &&
                    scope.bank_a.actions.size() ==
                        scope.bank_b.actions.size(),
                "feature-only bank is missing a legal action");
            const auto validate_leaf_bits =
                [](const science::GroupBank& bank) {
                    for (const science::GroupAction& action :
                         bank.actions) {
                        for (const science::LeafSample& sample :
                             action.samples) {
                            expect(
                                sample.forced_action_applied &&
                                    sample.actions_applied >=
                                        1 &&
                                    sample
                                            .priority_actions_applied >=
                                        1 &&
                                    sample
                                            .priority_actions_applied <=
                                        sample.actions_applied,
                                "successor leaf did not account "
                                "for its forced action");
                            if (sample.terminal) {
                                expect(
                                    !sample.critic_evaluated &&
                                        sample
                                                .contextual_score_bits ==
                                            0 &&
                                        sample
                                                .legacy_score_bits ==
                                            0,
                                    "terminal leaf retained "
                                    "critic bits");
                            } else {
                                const std::uint64_t score_bits =
                                    std::bit_cast<
                                        std::uint64_t>(
                                        sample.score);
                                expect(
                                    sample.critic_evaluated &&
                                        sample
                                                .contextual_score_bits ==
                                            score_bits &&
                                        sample
                                                .legacy_score_bits ==
                                            score_bits &&
                                        sample
                                            .contextual_legacy_critic_bit_identical,
                                    "critic leaf discarded or "
                                    "changed raw IEEE bits");
                            }
                        }
                    }
                };
            validate_leaf_bits(scope.bank_a);
            validate_leaf_bits(scope.bank_b);
            const auto means_a =
                bellman::canonical_action_means(
                    bank_samples(scope.bank_a));
            const auto means_b =
                bellman::canonical_action_means(
                    bank_samples(scope.bank_b));
            for (std::size_t action_index = 0;
                 action_index < means_a.size();
                 ++action_index) {
                expect(
                    means_a[action_index].descriptor ==
                        means_b[action_index].descriptor,
                    "feature-only peer legal sets drifted");
                const double expected =
                    0.5 *
                    (means_a[action_index].value +
                     means_b[action_index].value);
                const science::GroupAction& action =
                    scope.bank_a.actions[action_index];
                const bellman::FeatureTargetRow& row =
                    feature_row(
                        construction,
                        action.feature_row_id);
                const double actual =
                    scope.kind ==
                            science::ScopeKind::Full
                        ? row.target.full
                        : row.target.blocks.at(
                              scope.block);
                expect(
                    same_bits(expected, actual),
                    "successor action target is not symmetric "
                    "0.5*(qA+qB)");
            }
        }
    }
}

void test_physical_and_hidden_reconstruction_witnesses() {
    Fixture& data = fixture();
    const science::Root& root =
        data.single_worker.roots.front();
    const science::RootAction* selected_action = nullptr;
    const science::Scope* selected_scope = nullptr;
    const science::SuccessorGroup* selected_group = nullptr;
    for (const science::RootAction& action : root.actions) {
        for (const science::Scope& scope : action.scopes) {
            if (!scope.groups.empty()) {
                selected_action = &action;
                selected_scope = &scope;
                selected_group = &scope.groups.front();
                break;
            }
        }
        if (selected_group != nullptr) {
            break;
        }
    }
    expect(
        selected_group != nullptr,
        "reconstruction test lacks a successor group");
    const science::GroupReconstructionWitnesses witnesses =
        science::testing::reconstruct_group(
            data.manifest.roots.front(),
            *selected_action, *selected_scope,
            *selected_group, data.model,
            {
                .root_seed_base =
                    0xF00D000000001001ULL,
                .bank_a_seed_base =
                    0xF00D000000001002ULL,
                .bank_b_seed_base =
                    0xF00D000000001003ULL,
                .root_worlds =
                    bellman::kBlockCount,
                .successor_worlds = 1,
                .workers = 2,
            });
    expect(
        witnesses.representatives.size() ==
                selected_group
                    ->root_world_indices.size() &&
            witnesses
                .every_representative_bit_identical &&
            witnesses
                .hidden_repartition_bit_identical &&
            witnesses.hidden_repartition
                .identity.bit_identical(),
        "physical or hidden successor representative "
        "did not reconstruct the cached raw bank");
    for (std::size_t index = 0;
         index < witnesses.representatives.size(); ++index) {
        expect(
            witnesses.representatives[index]
                    .root_action_descriptor ==
                selected_action->descriptor &&
                witnesses.representatives[index]
                    .root_world ==
                selected_group
                    ->root_world_indices[index] &&
                witnesses.representatives[index]
                    .identity.bit_identical(),
            "group reconstruction witness lost canonical "
            "root-action/world provenance");
    }
    expect(
        !witnesses.hidden_repartition_eligible ||
            (witnesses.hidden_repartition_changed &&
             witnesses
                 .hidden_repartition_bit_identical),
        "eligible group hidden repartition was vacuous");
    expect(
        witnesses.execution.workers_requested == 2 &&
            witnesses.execution
                    .maximum_workers_started ==
                2 &&
            witnesses.execution.parallel_batches > 0,
        "reconstruction witness did not use its actual "
        "indexed worker schedule");
}

void test_resolving_hidden_draw_uses_operator_identity() {
    Fixture& data = fixture();
    const auto manifest =
        braingeyser_hidden_draw_manifest();
    const science::testing::ReducedRecipe recipe{
        .root_seed_base =
            0xF00D00000000D101ULL,
        .bank_a_seed_base =
            0xF00D00000000D102ULL,
        .bank_b_seed_base =
            0xF00D00000000D103ULL,
        .root_worlds = 64,
        .successor_worlds = 1,
        .workers = 2,
    };
    const science::Construction construction =
        science::testing::construct_reduced(
            manifest, data.model, recipe);
    expect(
        construction.roots.size() == 1,
        "Braingeyser fixture did not produce one root");
    const science::Root& root =
        construction.roots.front();
    const auto root_action = std::find_if(
        root.actions.begin(), root.actions.end(),
        [](const science::RootAction& action) {
            return action.descriptor ==
                   "braingeyser-x1-opponent";
        });
    expect(
        root_action != root.actions.end(),
        "Braingeyser fixture lacks the productive root action");

    const science::Scope* selected_scope = nullptr;
    const science::SuccessorGroup* selected_group = nullptr;
    const science::RootTransition*
        selected_physical_transition = nullptr;
    const science::RootTransition*
        selected_canonical_transition = nullptr;
    old_school::PriorityAction selected_pass =
        old_school::PriorityAction::pass();
    std::string physical_consequence;
    std::string canonical_consequence;
    for (const science::Scope& scope :
         root_action->scopes) {
        for (const science::SuccessorGroup& group :
             scope.groups) {
            const auto evaluation = std::find_if(
                construction
                    .successor_feature_evaluations.begin(),
                construction
                    .successor_feature_evaluations.end(),
                [&](const auto& candidate) {
                    return candidate.root_stable_id ==
                               root.stable_id &&
                           candidate
                                   .information_set_fingerprint ==
                               group
                                   .information_set_fingerprint;
                });
            expect(
                evaluation !=
                    construction
                        .successor_feature_evaluations.end(),
                "Braingeyser group lacks a canonical feature "
                "evaluation");
            if (group.representative_root_world ==
                    evaluation
                        ->representative_root_world &&
                group
                        .representative_root_action_descriptor ==
                    evaluation
                        ->representative_root_action_descriptor) {
                continue;
            }
            const auto canonical_action = std::find_if(
                root.actions.begin(), root.actions.end(),
                [&](const science::RootAction& action) {
                    return action.descriptor ==
                           evaluation
                               ->representative_root_action_descriptor;
                });
            expect(
                canonical_action != root.actions.end(),
                "Braingeyser canonical representative action "
                "is missing");
            const science::RootTransition& physical =
                root_action->root_transitions.at(
                    group.representative_root_world);
            const science::RootTransition& canonical =
                canonical_action->root_transitions.at(
                    evaluation
                        ->representative_root_world);
            const auto pass = std::find(
                physical.successor_legal_actions.begin(),
                physical.successor_legal_actions.end(),
                old_school::PriorityAction::pass());
            if (pass ==
                    physical.successor_legal_actions.end() ||
                std::find(
                    canonical.successor_legal_actions.begin(),
                    canonical.successor_legal_actions.end(),
                    *pass) ==
                    canonical.successor_legal_actions.end()) {
                continue;
            }
            const std::string physical_hash =
                old_school::fq0_information_set::
                    canonical_priority_consequence_sha256(
                        physical.successor_state,
                        physical.successor_owner,
                        physical.successor_context, *pass);
            const std::string canonical_hash =
                old_school::fq0_information_set::
                    canonical_priority_consequence_sha256(
                        canonical.successor_state,
                        canonical.successor_owner,
                        canonical.successor_context, *pass);
            if (physical_hash == canonical_hash) {
                continue;
            }
            selected_scope = &scope;
            selected_group = &group;
            selected_physical_transition = &physical;
            selected_canonical_transition = &canonical;
            selected_pass = *pass;
            physical_consequence = physical_hash;
            canonical_consequence = canonical_hash;
            break;
        }
        if (selected_group != nullptr) {
            break;
        }
    }
    expect(
        selected_group != nullptr &&
            selected_physical_transition != nullptr &&
            selected_canonical_transition != nullptr,
        "natural Braingeyser fixture did not expose the "
        "representative-local resolving-draw divergence");
    expect(
        selected_physical_transition
                    ->successor_context.consecutive_passes ==
                1 &&
            selected_canonical_transition
                    ->successor_context.consecutive_passes ==
                1 &&
            !selected_physical_transition
                 ->successor_state.stack.empty() &&
            !selected_canonical_transition
                 ->successor_state.stack.empty(),
        "Braingeyser divergence is not at the resolving-Pass "
        "priority boundary");
    const old_school::StackObject& physical_spell =
        selected_physical_transition
            ->successor_state.stack.back();
    const old_school::StackObject& canonical_spell =
        selected_canonical_transition
            ->successor_state.stack.back();
    expect(
        physical_spell.card ==
                old_school::CardId::Braingeyser &&
            physical_spell.x_value == 1 &&
            physical_spell.target.has_value() &&
            !physical_spell.target->creature.has_value() &&
            canonical_spell == physical_spell &&
            !selected_physical_transition
                 ->successor_state
                 .players[physical_spell.target->player]
                 .library.empty() &&
            !selected_canonical_transition
                 ->successor_state
                 .players[physical_spell.target->player]
                 .library.empty() &&
            selected_physical_transition
                    ->successor_state
                    .players[physical_spell.target->player]
                    .library.back() !=
                selected_canonical_transition
                    ->successor_state
                    .players[physical_spell.target->player]
                    .library.back(),
        "Braingeyser divergence does not come from the hidden "
        "card drawn on resolution");

    const auto physical_key =
        old_school::fq0_information_set::
            make_information_set_key(
                selected_physical_transition
                    ->successor_state,
                selected_physical_transition
                    ->successor_context,
                selected_physical_transition
                    ->successor_legal_actions);
    const auto canonical_key =
        old_school::fq0_information_set::
            make_information_set_key(
                selected_canonical_transition
                    ->successor_state,
                selected_canonical_transition
                    ->successor_context,
                selected_canonical_transition
                    ->successor_legal_actions);
    const auto physical_features =
        old_school::learned_priority_policy_features(
            selected_physical_transition
                ->successor_state,
            selected_physical_transition
                ->successor_owner,
            selected_pass,
            selected_physical_transition
                ->successor_context.sorcery_actions,
            selected_physical_transition
                ->successor_context.phase,
            selected_physical_transition
                ->successor_context.consecutive_passes);
    const auto canonical_features =
        old_school::learned_priority_policy_features(
            selected_canonical_transition
                ->successor_state,
            selected_canonical_transition
                ->successor_owner,
            selected_pass,
            selected_canonical_transition
                ->successor_context.sorcery_actions,
            selected_canonical_transition
                ->successor_context.phase,
            selected_canonical_transition
                ->successor_context.consecutive_passes);
    expect(
        old_school::fq0_information_set::
                information_set_sha256(physical_key) ==
                selected_group
                    ->information_set_fingerprint &&
            physical_key == canonical_key &&
            physical_features == canonical_features &&
            physical_consequence !=
                canonical_consequence,
        "Braingeyser resolving draw did not isolate raw "
        "consequence from current information/features");

    science::testing::validate_complete_preflight(
        construction, manifest, data.model, recipe);
    const science::GroupReconstructionWitnesses witnesses =
        science::testing::reconstruct_group(
            manifest.roots.front(), *root_action,
            *selected_scope, *selected_group,
            data.model, recipe);
    const auto physical_witness = std::find_if(
        witnesses.representatives.begin(),
        witnesses.representatives.end(),
        [&](const auto& witness) {
            return witness.root_action_descriptor ==
                       root_action->descriptor &&
                   witness.root_world ==
                       selected_group
                           ->representative_root_world;
        });
    expect(
        physical_witness !=
                witnesses.representatives.end() &&
            physical_witness->identity.bit_identical() &&
            witnesses
                .every_representative_bit_identical,
        "operator v2 did not reconstruct the natural "
        "Braingeyser physical representative");
}

void test_all_feature_scopes_reconstruct_full_catalog() {
    Fixture& data = fixture();
    const science::Root& root =
        data.single_worker.roots.front();
    expect(
        !data.single_worker
             .successor_feature_evaluations.empty(),
        "feature reconstruction test lacks an evaluation");
    bool observed_eligible_hidden_repartition = false;
    bool observed_zero_mass_block = false;
    for (const science::SuccessorFeatureEvaluation& evaluation :
         data.single_worker
             .successor_feature_evaluations) {
        expect(
            !evaluation.members.empty() &&
                evaluation.scopes.size() ==
                    bellman::kBlockCount + 1,
            "feature evaluation lacks its full member catalog "
            "or nine scopes");
        for (const science::SuccessorFeatureScope& scope :
             evaluation.scopes) {
            if (scope.kind == science::ScopeKind::Block) {
                const std::size_t first = scope.block;
                const std::size_t last = first + 1;
                const bool has_mass = std::any_of(
                    evaluation.members.begin(),
                    evaluation.members.end(),
                    [&](const auto& member) {
                        return member.root_world >= first &&
                               member.root_world < last;
                    });
                observed_zero_mass_block |= !has_mass;
            }
            const science::GroupReconstructionWitnesses
                witnesses =
                    science::testing::
                        reconstruct_feature_scope(
                            data.manifest.roots.front(),
                            root, evaluation, scope,
                            data.model,
                            {
                                .root_seed_base =
                                    0xF00D000000001001ULL,
                                .bank_a_seed_base =
                                    0xF00D000000001002ULL,
                                .bank_b_seed_base =
                                    0xF00D000000001003ULL,
                                .root_worlds =
                                    bellman::kBlockCount,
                                .successor_worlds = 1,
                                .workers = 2,
                            });
            expect(
                witnesses.representatives.size() ==
                        evaluation.members.size() &&
                    witnesses
                        .every_representative_bit_identical &&
                    witnesses
                        .hidden_repartition_bit_identical,
                "feature scope did not reconstruct every "
                "full-catalog member");
            for (std::size_t index = 0;
                 index < evaluation.members.size(); ++index) {
                expect(
                    witnesses.representatives[index]
                            .root_action_descriptor ==
                        evaluation.members[index]
                            .root_action_descriptor &&
                        witnesses.representatives[index]
                                .root_world ==
                            evaluation.members[index]
                                .root_world &&
                        witnesses.representatives[index]
                            .identity.bit_identical(),
                    "feature-scope witness lost canonical "
                    "member provenance");
            }
            if (witnesses.hidden_repartition_eligible) {
                observed_eligible_hidden_repartition = true;
                expect(
                    witnesses.hidden_repartition_changed,
                    "eligible feature hidden repartition was "
                    "vacuous");
            }

            std::set<std::pair<std::size_t, std::string>>
                expected_empirical;
            for (const science::RootAction& action :
                 root.actions) {
                const auto root_scope = std::find_if(
                    action.scopes.begin(),
                    action.scopes.end(),
                    [&](const science::Scope& candidate) {
                        return candidate.kind ==
                                   scope.kind &&
                               candidate.block ==
                                   scope.block;
                    });
                expect(
                    root_scope != action.scopes.end(),
                    "root action lacks a feature scope");
                for (const science::SuccessorGroup& group :
                     root_scope->groups) {
                    if (group
                            .information_set_fingerprint ==
                        evaluation
                            .information_set_fingerprint) {
                        expected_empirical.emplace(
                            group
                                .representative_root_world,
                            group
                                .representative_root_action_descriptor);
                    }
                }
            }
            expect(
                witnesses
                        .empirical_group_hidden_repartitions
                        .size() ==
                    expected_empirical.size(),
                "feature scope did not hidden-reconstruct "
                "every unique empirical group representative");
            std::size_t empirical_index = 0;
            for (const auto& [world, descriptor] :
                 expected_empirical) {
                const auto& empirical =
                    witnesses
                        .empirical_group_hidden_repartitions[
                            empirical_index++];
                expect(
                    empirical.representative
                                .root_action_descriptor ==
                            descriptor &&
                        empirical.representative.root_world ==
                            world &&
                        empirical.bit_identical &&
                        empirical.representative
                            .identity.bit_identical() &&
                        (!empirical.eligible ||
                         empirical.changed) &&
                        (empirical.eligible ||
                         !empirical.changed),
                    "empirical-group hidden witness lost its "
                    "coordinate or was vacuous");
            }
        }
    }
    expect(
        observed_zero_mass_block,
        "reduced fixture did not exercise a zero-mass "
        "feature-only block");
    expect(
        observed_eligible_hidden_repartition,
        "diverse fixture did not exercise an eligible "
        "feature hidden repartition");
}

void test_hidden_repartition_eligibility_is_nonvacuous() {
    Fixture& data = fixture();
    const auto homogeneous = homogeneous_hidden_manifest();

    old_school::GameState observer_only_diverse =
        homogeneous.roots.front().probe.state;
    observer_only_diverse.players[0].library.back() =
        old_school::CardId::GrizzlyBears;
    const auto observer_only =
        science::testing::hidden_repartition(
            observer_only_diverse, 0);
    expect(
        !observer_only.eligible &&
            !observer_only.changed &&
            observer_only.state ==
                observer_only_diverse,
        "observer-library order falsely satisfied the "
        "opponent repartition witness");

    old_school::GameState diverse =
        observer_only_diverse;
    diverse.players[1].library.back() =
        old_school::CardId::LightningBolt;
    const old_school::PlayerObservation before_observation =
        old_school::observe_game_state(diverse, 0);
    const old_school::LearnedDecisionContext context{
        .valid = true,
        .phase = old_school::TurnPhase::FirstMain,
        .decision_player = 0,
        .consecutive_passes = 0,
        .sorcery_actions = true,
    };
    const auto before_actions =
        old_school::legal_priority_actions(
            diverse, 0, true);
    const std::string before_information_set =
        old_school::fq0_information_set::
            information_set_sha256(
                old_school::fq0_information_set::
                    make_information_set_key(
                        diverse, context,
                        before_actions));
    old_school::probes::DecisionProbe before_probe =
        homogeneous.roots.front().probe;
    before_probe.state = diverse;
    const std::string before_information_action =
        old_school::probes::
            bsr_information_action_fingerprint(
                before_probe);
    std::vector<old_school::CardId> before_hidden =
        diverse.players[1].hand;
    before_hidden.insert(
        before_hidden.end(),
        diverse.players[1].library.begin(),
        diverse.players[1].library.end());
    std::sort(before_hidden.begin(), before_hidden.end());
    const auto eligible =
        science::hidden_repartition(diverse, 0);
    std::vector<old_school::CardId> after_hidden =
        eligible.state.players[1].hand;
    after_hidden.insert(
        after_hidden.end(),
        eligible.state.players[1].library.begin(),
        eligible.state.players[1].library.end());
    std::sort(after_hidden.begin(), after_hidden.end());
    const auto after_actions =
        old_school::legal_priority_actions(
            eligible.state, 0, true);
    const std::string after_information_set =
        old_school::fq0_information_set::
            information_set_sha256(
                old_school::fq0_information_set::
                    make_information_set_key(
                        eligible.state, context,
                        after_actions));
    old_school::probes::DecisionProbe after_probe =
        before_probe;
    after_probe.state = eligible.state;
    const std::string after_information_action =
        old_school::probes::
            bsr_information_action_fingerprint(
                after_probe);
    expect(
        eligible.eligible && eligible.changed &&
            eligible.state.players[0] ==
                diverse.players[0] &&
            eligible.state.players[1].hand !=
                diverse.players[1].hand &&
            eligible.state.players[1].library !=
                diverse.players[1].library &&
            eligible.state.players[1].hand.size() ==
                diverse.players[1].hand.size() &&
            eligible.state.players[1].library.size() ==
                diverse.players[1].library.size() &&
            after_hidden == before_hidden &&
            old_school::observe_game_state(
                eligible.state, 0) ==
                before_observation &&
            after_actions == before_actions &&
            after_information_set ==
                before_information_set &&
            after_information_action ==
                before_information_action,
        "opponent hidden hand/library cards were not "
        "truthfully repartitioned");

    const auto ineligible =
        science::testing::hidden_repartition(
            homogeneous.roots.front().probe.state, 0);
    expect(
        !ineligible.eligible && !ineligible.changed &&
            ineligible.state ==
                homogeneous.roots.front().probe.state,
        "homogeneous hidden zones were falsely eligible");

    const science::testing::ReducedRecipe recipe{
        .root_seed_base =
            0xF00D000000002001ULL,
        .bank_a_seed_base =
            0xF00D000000002002ULL,
        .bank_b_seed_base =
            0xF00D000000002003ULL,
        .root_worlds = bellman::kBlockCount,
        .successor_worlds = 1,
        .workers = 2,
    };
    const science::Construction construction =
        science::testing::construct_reduced(
            homogeneous, data.model, recipe);
    expect(
        !construction.successor_feature_evaluations.empty(),
        "homogeneous fixture has no feature scope");
    const science::Root& root =
        construction.roots.front();
    const auto& evaluation =
        construction.successor_feature_evaluations.front();
    const science::GroupReconstructionWitnesses feature =
        science::testing::reconstruct_feature_scope(
            homogeneous.roots.front(), root, evaluation,
            evaluation.scopes.front(), data.model, recipe);
    expect(
        !feature.hidden_repartition_eligible &&
            !feature.hidden_repartition_changed &&
            feature.hidden_repartition_bit_identical,
        "homogeneous feature scope did not report a "
        "truthful ineligible hidden witness");

    const science::SuccessorFeatureEvaluation*
        distinct_evaluation = nullptr;
    const science::SuccessorFeatureScope*
        distinct_scope = nullptr;
    std::pair<std::size_t, std::string>
        distinct_coordinate;
    for (const auto& candidate_evaluation :
         construction.successor_feature_evaluations) {
        for (const auto& candidate_scope :
             candidate_evaluation.scopes) {
            if (candidate_scope.kind !=
                science::ScopeKind::Block) {
                continue;
            }
            for (const auto& action : root.actions) {
                const auto action_scope = std::find_if(
                    action.scopes.begin(),
                    action.scopes.end(),
                    [&](const science::Scope& candidate) {
                        return candidate.kind ==
                                   candidate_scope.kind &&
                               candidate.block ==
                                   candidate_scope.block;
                    });
                expect(
                    action_scope != action.scopes.end(),
                    "homogeneous root lacks a block scope");
                const auto group = std::find_if(
                    action_scope->groups.begin(),
                    action_scope->groups.end(),
                    [&](const science::SuccessorGroup&
                            candidate) {
                        return candidate
                                   .information_set_fingerprint ==
                               candidate_evaluation
                                   .information_set_fingerprint;
                    });
                if (group ==
                    action_scope->groups.end()) {
                    continue;
                }
                if (group->representative_root_world ==
                        candidate_evaluation
                            .representative_root_world &&
                    group
                            ->representative_root_action_descriptor ==
                        candidate_evaluation
                            .representative_root_action_descriptor) {
                    continue;
                }
                distinct_evaluation =
                    &candidate_evaluation;
                distinct_scope = &candidate_scope;
                distinct_coordinate = {
                    group->representative_root_world,
                    group
                        ->representative_root_action_descriptor,
                };
                break;
            }
            if (distinct_scope != nullptr) {
                break;
            }
        }
        if (distinct_scope != nullptr) {
            break;
        }
    }
    expect(
        distinct_evaluation != nullptr &&
            distinct_scope != nullptr,
        "homogeneous fixture lacks an empirical group "
        "representative distinct from the global one");
    const science::GroupReconstructionWitnesses distinct =
        science::testing::reconstruct_feature_scope(
            homogeneous.roots.front(), root,
            *distinct_evaluation, *distinct_scope,
            data.model, recipe);
    const auto distinct_witness = std::find_if(
        distinct
            .empirical_group_hidden_repartitions.begin(),
        distinct
            .empirical_group_hidden_repartitions.end(),
        [&](const auto& witness) {
            return witness.representative.root_world ==
                       distinct_coordinate.first &&
                   witness.representative
                           .root_action_descriptor ==
                       distinct_coordinate.second;
        });
    expect(
        distinct_witness !=
                distinct
                    .empirical_group_hidden_repartitions.end() &&
            distinct_witness->bit_identical &&
            distinct_witness->representative
                .identity.bit_identical(),
        "distinct empirical-group representative was not "
        "hidden-reconstructed");

    const science::RootAction* group_action = nullptr;
    const science::Scope* group_scope = nullptr;
    const science::SuccessorGroup* group = nullptr;
    for (const auto& action : root.actions) {
        for (const auto& scope : action.scopes) {
            if (!scope.groups.empty()) {
                group_action = &action;
                group_scope = &scope;
                group = &scope.groups.front();
                break;
            }
        }
        if (group != nullptr) {
            break;
        }
    }
    expect(
        group != nullptr,
        "homogeneous fixture has no empirical group");
    const science::GroupReconstructionWitnesses empirical =
        science::testing::reconstruct_group(
            homogeneous.roots.front(), *group_action,
            *group_scope, *group, data.model, recipe);
    expect(
        !empirical.hidden_repartition_eligible &&
            !empirical.hidden_repartition_changed &&
            empirical.hidden_repartition_bit_identical,
        "homogeneous empirical group did not report a "
        "truthful ineligible hidden witness");
}

void test_one_vs_many_workers_and_input_order() {
    const Fixture& data = fixture();
    expect(
        data.single_worker.execution.workers_requested == 1 &&
            data.single_worker.execution
                    .maximum_workers_started ==
                1 &&
            data.four_workers.execution.workers_requested == 4 &&
            data.four_workers.execution
                    .maximum_workers_started ==
                4,
        "construction did not start the requested worker counts");
    expect(
        data.single_worker.execution.parallel_batches > 0 &&
            data.four_workers.execution.parallel_batches > 0 &&
            data.single_worker.execution.indexed_tasks > 0 &&
            data.four_workers.execution.indexed_tasks > 0,
        "construction did not execute indexed parallel work");
    expect(
        data.single_worker.semantic_sha256 ==
                data.four_workers.semantic_sha256 &&
            science::testing::semantic_sha256(
                data.single_worker) ==
                data.single_worker.semantic_sha256 &&
            science::testing::semantic_sha256(
                data.four_workers) ==
                data.four_workers.semantic_sha256,
        "worker count, own-hand order, or candidate order "
        "changed canonical scientific evidence");
    expect(
        data.single_worker.roots.front().sampled_worlds ==
            data.four_workers.roots.front().sampled_worlds,
        "worker count changed retained physical root worlds");
}

void test_complete_preflight_rejects_coherent_tampering() {
    Fixture& data = fixture();
    const auto recipe = fixture_recipe();
    science::testing::validate_complete_preflight(
        data.single_worker, data.manifest,
        data.model, recipe);

    const auto refresh_semantic =
        [](science::Construction& construction) {
            construction.semantic_sha256 =
                science::testing::semantic_sha256(
                    construction);
        };
    const auto expect_preflight_rejects =
        [&](science::Construction construction,
            std::string_view message) {
            refresh_semantic(construction);
            expect_invalid(
                [&] {
                    science::testing::
                        validate_complete_preflight(
                            construction, data.manifest,
                            data.model, recipe);
                },
                message);
        };

    const auto first_group =
        [](science::Construction& construction)
            -> science::SuccessorGroup* {
        for (science::Root& root : construction.roots) {
            for (science::RootAction& action :
                 root.actions) {
                for (science::Scope& scope :
                     action.scopes) {
                    if (!scope.groups.empty()) {
                        return &scope.groups.front();
                    }
                }
            }
        }
        return nullptr;
    };
    science::Construction canonical =
        data.single_worker;
    science::SuccessorGroup* canonical_group =
        first_group(canonical);
    expect(
        canonical_group != nullptr &&
            !canonical_group->bank_a.actions.empty() &&
            !canonical_group->bank_a.actions.front()
                 .samples.empty(),
        "preflight fixture lacks a complete group bank");
    const auto canonical_digests =
        science::testing::successor_bank_pair_digests(
            canonical_group->bank_a,
            canonical_group->bank_b,
            canonical_group->cross_fit);
    const auto expect_bank_mutation =
        [&](auto mutate, bool changes_operator_v2,
            std::string_view message) {
        science::Construction changed =
            data.single_worker;
        science::SuccessorGroup* group =
            first_group(changed);
        expect(
            group != nullptr,
            "bank mutation fixture lost its group");
        mutate(*group);
        const auto changed_digests =
            science::testing::
                successor_bank_pair_digests(
                    group->bank_a, group->bank_b,
                    group->cross_fit);
        expect(
            changed_digests.full_v1 !=
                    canonical_digests.full_v1 &&
                ((changed_digests.operator_v2 !=
                  canonical_digests.operator_v2) ==
                 changes_operator_v2),
            message);
        expect_preflight_rejects(
            std::move(changed), message);
    };

    expect_bank_mutation(
        [](science::SuccessorGroup& group) {
            group.bank_a.actions.front()
                .canonical_consequence_fingerprint +=
                "-tampered";
        },
        false,
        "consequence-only mutation did not separate full "
        "v1 from operator v2 or escaped copy validation");
    expect_bank_mutation(
        [](science::SuccessorGroup& group) {
            group.bank_a.actions.front().descriptor +=
                "-tampered";
        },
        true,
        "descriptor mutation escaped operator v2 or "
        "preflight");
    expect_bank_mutation(
        [](science::SuccessorGroup& group) {
            group.bank_a.actions.front()
                .samples.front().redacted_leaf_hash +=
                "-tampered";
        },
        true,
        "sampled leaf mutation escaped operator v2 or "
        "preflight");
    expect_bank_mutation(
        [](science::SuccessorGroup& group) {
            science::LeafSample& sample =
                group.bank_a.actions.front()
                    .samples.front();
            sample.score =
                sample.score == 0.0
                    ? 0.25
                    : sample.score / 2.0;
        },
        true,
        "sample score mutation escaped operator v2 or "
        "preflight");
    expect_bank_mutation(
        [](science::SuccessorGroup& group) {
            ++group.bank_a.actions.front()
                  .samples.front()
                  .determinization_seed;
        },
        true,
        "sample seed mutation escaped operator v2 or "
        "preflight");
    expect_bank_mutation(
        [](science::SuccessorGroup& group) {
            ++group.bank_a.actions.front()
                  .samples.front()
                  .actions_applied;
        },
        true,
        "sample counter mutation escaped operator v2 or "
        "preflight");
    expect_bank_mutation(
        [](science::SuccessorGroup& group) {
            group.cross_fit.value =
                group.cross_fit.value == 0.0
                    ? 0.25
                    : group.cross_fit.value / 2.0;
        },
        true,
        "cross-fit mutation escaped operator v2 or "
        "preflight");

    science::Construction stale_support =
        data.single_worker;
    stale_support.roots.front().exact_support = {
        "not-a-legal-action",
    };
    expect_preflight_rejects(
        std::move(stale_support),
        "coherently rehashed stale root support was accepted");

    science::Construction stale_feature =
        data.single_worker;
    expect(
        !stale_feature.feature_rows.empty(),
        "preflight fixture lacks a feature row");
    stale_feature.feature_rows.front().common_world_key +=
        "-tampered";
    expect_preflight_rejects(
        std::move(stale_feature),
        "coherently rehashed feature-row drift was accepted");

    science::Construction unsorted_features =
        data.single_worker;
    expect(
        unsorted_features.feature_rows.size() >= 2,
        "preflight fixture lacks two feature rows");
    std::swap(
        unsorted_features.feature_rows[0],
        unsorted_features.feature_rows[1]);
    expect_preflight_rejects(
        std::move(unsorted_features),
        "coherently rehashed unsorted feature rows were "
        "accepted");

    science::Construction stale_collision =
        data.single_worker;
    ++stale_collision.feature_collisions.rows;
    expect_preflight_rejects(
        std::move(stale_collision),
        "coherently rehashed collision drift was accepted");

    science::Construction stale_census =
        data.single_worker;
    ++stale_census.roots_by_deck[
        static_cast<std::size_t>(
            old_school::DeckId::Green)];
    expect_preflight_rejects(
        std::move(stale_census),
        "coherently rehashed deck-census drift was accepted");
}

void test_reduced_recipe_and_production_fail_closed() {
    Fixture& data = fixture();
    science::testing::ReducedRecipe invalid;
    invalid.root_worlds =
        bellman::kBlockCount - 1;
    expect_invalid(
        [&] {
            static_cast<void>(
                science::testing::construct_reduced(
                    data.manifest, data.model, invalid));
        },
        "non-eight-block reduced recipe was accepted");

    science::testing::ReducedRecipe reserved_root;
    reserved_root.root_seed_base =
        science::kProductionRootSeedBase;
    expect_invalid(
        [&] {
            static_cast<void>(
                science::testing::construct_reduced(
                    data.manifest, data.model,
                    reserved_root));
        },
        "reserved production root seed entered the "
        "testing API");
    science::testing::ReducedRecipe reserved_bank_a;
    reserved_bank_a.bank_a_seed_base =
        science::kProductionBankASeedBase;
    expect_invalid(
        [&] {
            static_cast<void>(
                science::testing::construct_reduced(
                    data.manifest, data.model,
                    reserved_bank_a));
        },
        "reserved production bank-A seed entered the "
        "testing API");
    science::testing::ReducedRecipe reserved_bank_b;
    reserved_bank_b.bank_b_seed_base =
        science::kProductionBankBSeedBase;
    expect_invalid(
        [&] {
            static_cast<void>(
                science::testing::construct_reduced(
                    data.manifest, data.model,
                    reserved_bank_b));
        },
        "reserved production bank-B seed entered the "
        "testing API");

    science::testing::ReducedRecipe production_scale;
    production_scale.root_seed_base =
        0xF00D000000003001ULL;
    production_scale.bank_a_seed_base =
        0xF00D000000003002ULL;
    production_scale.bank_b_seed_base =
        0xF00D000000003003ULL;
    production_scale.root_worlds = 64;
    production_scale.successor_worlds = 64;
    production_scale.workers = 4;
    expect_invalid(
        [&] {
            static_cast<void>(
                science::testing::construct_reduced(
                    data.manifest, data.model,
                    production_scale));
        },
        "exact production scale entered the testing API");

    auto mismatched_manifest = data.manifest;
    mismatched_manifest.roots.front()
        .information_action_fingerprint += "0";
    expect_invalid(
        [&] {
            static_cast<void>(
                science::testing::construct_reduced(
                    mismatched_manifest, data.model));
        },
        "manifest information/action drift was accepted");
}

} // namespace

int main() {
    const std::vector<
        std::pair<std::string_view, std::function<void()>>>
        tests = {
            {"streaming digest preserves legacy framing",
             test_streaming_digest_preserves_legacy_framing},
            {"authoritative root actions and common worlds",
             test_authoritative_actions_and_common_root_worlds},
            {"scope regrouping and perspective complement",
             test_scopes_group_before_max_and_perspective},
            {"feature-only banks and symmetric targets",
             test_feature_only_banks_and_symmetric_targets},
            {"physical and hidden bank reconstruction",
             test_physical_and_hidden_reconstruction_witnesses},
            {"resolving hidden draw operator identity",
             test_resolving_hidden_draw_uses_operator_identity},
            {"all feature scopes reconstruct full catalog",
             test_all_feature_scopes_reconstruct_full_catalog},
            {"nonvacuous hidden repartition eligibility",
             test_hidden_repartition_eligibility_is_nonvacuous},
            {"one-vs-many worker determinism",
             test_one_vs_many_workers_and_input_order},
            {"complete preflight rejects coherent tampering",
             test_complete_preflight_rejects_coherent_tampering},
            {"recipe and manifest fail closed",
             test_reduced_recipe_and_production_fail_closed},
        };

    std::size_t passed = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& failure) {
            std::cerr << "[FAIL] " << name << ": "
                      << failure.what() << '\n';
        }
    }
    std::cout << passed << "/" << tests.size()
              << " tests passed\n";
    return passed == tests.size() ? 0 : 1;
}
