#include "old_school/action_q_recursive_policy_improvement.hpp"

#include "old_school/learned_iteration.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace aq5 =
    old_school::action_q_recursive_policy_improvement;
namespace iteration = old_school::learned_iteration;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Function>
void expect_rejected(
    Function&& function, std::string_view message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

void expect_search_config(
    const old_school::LearnedSearchConfig& config,
    std::uint64_t seed, const aq5::SearchBudget& budget,
    std::string_view message) {
    expect(
        config.seed == seed &&
            config.worlds == budget.worlds &&
            config.rollouts_per_world ==
                budget.rollouts_per_world &&
            config.horizon_turns ==
                budget.horizon_turns &&
            config.continuation_variant ==
                old_school::LearnedVariant::
                    ValueSearchChampion &&
            config.value_continuation_epsilon == 0.0 &&
            config.blend_shallow_prior ==
                budget.blend_shallow_prior &&
            config.value_resolved_shallow_prior_weight ==
                0.0 &&
            config.value_priority_residual_weight == 0.0 &&
            !config.value_pass_dominance &&
            config.value_continuation_controller ==
                old_school::LearnedContinuationController::
                    Legacy &&
            config.evaluation_threads ==
                budget.evaluation_threads &&
            !config.capture_priority_h0_boundaries &&
            config.value_continuation_search_worlds ==
                budget.continuation_worlds &&
            config.value_continuation_search_scope ==
                old_school::
                    LearnedContinuationSearchScope::
                        AllDecisions,
        message);
}

void test_sealed_recipe_and_defaults() {
    const aq5::SealedRecipe expected{
        .root_seed = 202607290401ULL,
        .priority_outer = {
            .worlds = 8,
            .rollouts_per_world = 1,
            .horizon_turns = 8,
            .evaluation_threads = 4,
            .continuation_worlds = 2,
            .blend_shallow_prior = false,
        },
        .combat_outer = {
            .worlds = 2,
            .rollouts_per_world = 1,
            .horizon_turns = 4,
            .evaluation_threads = 1,
            .continuation_worlds = 1,
            .blend_shallow_prior = false,
        },
        .priority_inner = {
            .worlds = 2,
            .rollouts_per_world = 1,
            .horizon_turns = 4,
            .evaluation_threads = 1,
            .continuation_worlds = 0,
            .blend_shallow_prior = true,
        },
        .combat_inner = {
            .worlds = 1,
            .rollouts_per_world = 1,
            .horizon_turns = 4,
            .evaluation_threads = 1,
            .continuation_worlds = 0,
            .blend_shallow_prior = false,
        },
        .maximum_active_nesting = 1,
        .treatment_default_off = true,
        .actor_local_redeterminization = true,
        .candidate_identity_absent_from_seeds = true,
        .inner_priority_blends_shallow_prior = true,
        .inner_combat_unblended = true,
        .damage_order_uses_c16 = true,
        .cleanup_uses_c16 = true,
    };
    const aq5::SealedRecipe actual = aq5::sealed_recipe();
    expect(
        actual == expected &&
            aq5::kPreflightSeed == expected.root_seed &&
            aq5::kMaximumActiveNesting == 1 &&
            aq5::kBridgeTimeoutSeconds == 120,
        "AQ5 sealed recipe or bound drifted");
    expect(
        aq5::kRequiredParentFingerprint ==
                "68126afc5a3e3757eb1d510a056585aa"
                "974c4f54ce1b4a789ff430f1c7413e2f" &&
            aq5::kRequiredParentPath ==
                "build/model-cache/"
                "old-school-value-challenger-v3-c16-t800-"
                "s424242.bin" &&
            aq5::kRequiredParentBytes == 3'111'437 &&
            aq5::kRequiredParentSha256 ==
                "53aeb904bd87311b37201859317f05ab066bdfe13"
                "4c72460cf94bff6d1f944ca" &&
            aq5::kPilotLabel ==
                "Learned C16 · Recursive Foresight (AQ5)" &&
            aq5::kPilotQualification ==
                "manual diagnostic / not promoted",
        "AQ5 frozen parent or public diagnostic identity drifted");

    const old_school::BotConfig default_bot;
    const old_school::LearnedSearchConfig default_search;
    expect(
        !default_bot.value_recursive_policy_improvement &&
            default_search.value_continuation_search_scope ==
                old_school::
                    LearnedContinuationSearchScope::
                        PriorityOnly,
        "AQ5 treatment is no longer default-off");

    constexpr std::uint64_t kPrioritySeed =
        0xA051000000000001ULL;
    constexpr std::uint64_t kAttackSeed =
        0xA051000000000002ULL;
    constexpr std::uint64_t kBlockSeed =
        0xA051000000000003ULL;
    expect_search_config(
        aq5::outer_search_config(
            aq5::DecisionFamily::Priority,
            kPrioritySeed),
        kPrioritySeed, expected.priority_outer,
        "AQ5 Priority outer configuration drifted");
    expect_search_config(
        aq5::outer_search_config(
            aq5::DecisionFamily::Attack,
            kAttackSeed),
        kAttackSeed, expected.combat_outer,
        "AQ5 Attack outer configuration drifted");
    expect_search_config(
        aq5::outer_search_config(
            aq5::DecisionFamily::Block,
            kBlockSeed),
        kBlockSeed, expected.combat_outer,
        "AQ5 Block outer configuration drifted");
    expect_search_config(
        aq5::outer_search_config(
            aq5::DecisionFamily::Block,
            kAttackSeed),
        kAttackSeed, expected.combat_outer,
        "AQ5 Attack and Block no longer share the sealed combat recipe");
    expect_rejected(
        [] {
            static_cast<void>(
                aq5::outer_search_config(
                    static_cast<aq5::DecisionFamily>(255),
                    1));
        },
        "AQ5 accepted an undeclared decision family");
    expect_rejected(
        [] {
            static_cast<void>(
                aq5::treatment_bot_config(nullptr));
        },
        "AQ5 treatment accepted no frozen C16 parent");
}

void test_manifest_shape_and_seed_coordinates() {
    constexpr std::array<aq5::DecisionFamily, aq5::kFixtureCount>
        kFamilies{{
            aq5::DecisionFamily::Priority,
            aq5::DecisionFamily::Priority,
            aq5::DecisionFamily::Priority,
            aq5::DecisionFamily::Priority,
            aq5::DecisionFamily::Priority,
            aq5::DecisionFamily::Block,
            aq5::DecisionFamily::Attack,
            aq5::DecisionFamily::Block,
        }};
    constexpr std::array<aq5::DirectionKind, aq5::kFixtureCount>
        kDirections{{
            aq5::DirectionKind::CounterComposition,
            aq5::DirectionKind::BraingeyserTargetAndX,
            aq5::DirectionKind::AncestralTarget,
            aq5::DirectionKind::GiantGrowthTimingAndTarget,
            aq5::DirectionKind::ForceSpikeTax,
            aq5::DirectionKind::LifeSensitiveBlock,
            aq5::DirectionKind::AvoidBadAttack,
            aq5::DirectionKind::MultiChoiceBlock,
        }};
    constexpr std::array<std::size_t, aq5::kFixtureCount>
        kRootCounts{{2, 1, 1, 2, 2, 2, 1, 1}};
    constexpr std::array<std::array<std::string_view, 2>,
                         aq5::kFixtureCount>
        kStableIds{{
            {{
                "control.blue.counter-redundant-same-target.v1",
                "control.blue.counter-same-target-after-"
                "intervening-counter.v1",
            }},
            {{
                "control.blue.braingeyser-x0.v1",
                "",
            }},
            {{
                "field.blue.ancestral-opponent-seed24.aq0.v1",
                "",
            }},
            {{
                "field.green.second-main-sick-bear-growth.v1",
                "field.green.begin-combat-growth-tapped-air.v1",
            }},
            {{
                "control.blue.force-spike-live-gray-ogre.v1",
                "control.blue.force-spike-payable-five-open-"
                "gray-ogre.aq0.v1",
            }},
            {{
                "field.ru.life20-flying-men-chump-air.v1",
                "field.ru.life4-flying-men-chump-air.v1",
            }},
            {{
                "diagnostic.ru.life20-flying-men-attack-air.v1",
                "",
            }},
            {{
                aq5::kNewBlueBlockFixtureId,
                "",
            }},
        }};

    const auto manifest = aq5::fixture_manifest();
    std::size_t root_count = 0;
    std::set<std::string_view> nonempty_ids;
    for (std::size_t ordinal = 0;
         ordinal < manifest.size(); ++ordinal) {
        const aq5::FixtureSpec& spec = manifest[ordinal];
        root_count += spec.root_count;
        for (const std::string_view id : spec.stable_ids) {
            if (!id.empty()) {
                expect(
                    nonempty_ids.insert(id).second,
                    "AQ5 manifest repeated a stable fixture id");
            }
        }
        expect(
            spec.ordinal == ordinal &&
                spec.family == kFamilies[ordinal] &&
                spec.direction == kDirections[ordinal] &&
                spec.root_count == kRootCounts[ordinal] &&
                spec.stable_ids == kStableIds[ordinal] &&
                spec.expected_search_seed ==
                    aq5::search_seed(
                        ordinal, spec.family) &&
                spec.expected_tie_seed ==
                    aq5::tie_seed(
                        ordinal, spec.family),
            "AQ5 fixture manifest drifted");
    }
    expect(
        manifest.size() == aq5::kFixtureCount &&
            root_count == aq5::kFixtureRootCount &&
            root_count == 12 &&
            nonempty_ids.size() == root_count,
        "AQ5 fixture/root census drifted");

    constexpr std::array<aq5::DecisionFamily, 3> kAllFamilies{{
        aq5::DecisionFamily::Priority,
        aq5::DecisionFamily::Attack,
        aq5::DecisionFamily::Block,
    }};
    std::set<std::uint64_t> all_seeds;
    for (std::size_t ordinal = 0;
         ordinal < aq5::kFixtureCount; ++ordinal) {
        for (std::size_t family_index = 0;
             family_index < kAllFamilies.size();
             ++family_index) {
            const aq5::DecisionFamily family =
                kAllFamilies[family_index];
            const auto search_domain =
                family == aq5::DecisionFamily::Priority
                    ? iteration::SeedDomain::PrioritySearch
                    : iteration::SeedDomain::AttackSearch;
            const auto choice_domain =
                family == aq5::DecisionFamily::Priority
                    ? iteration::SeedDomain::PriorityChoice
                    : iteration::SeedDomain::AttackChoice;
            const std::uint64_t expected_search =
                iteration::derive_seed(
                    aq5::kPreflightSeed, search_domain,
                    0, ordinal, family_index);
            const std::uint64_t expected_tie =
                iteration::derive_seed(
                    aq5::kPreflightSeed, choice_domain,
                    0, ordinal, family_index);
            const std::uint64_t actual_search =
                aq5::search_seed(ordinal, family);
            const std::uint64_t actual_tie =
                aq5::tie_seed(ordinal, family);
            expect(
                actual_search == expected_search &&
                    actual_tie == expected_tie &&
                    all_seeds.insert(actual_search).second &&
                    all_seeds.insert(actual_tie).second,
                "AQ5 search/tie seed coordinate drifted");
        }
    }
    expect(
        all_seeds.size() ==
            aq5::kFixtureCount * kAllFamilies.size() * 2,
        "AQ5 seed domains or coordinates collided");
    expect_rejected(
        [] {
            static_cast<void>(
                aq5::search_seed(
                    aq5::kFixtureCount,
                    aq5::DecisionFamily::Priority));
        },
        "AQ5 search seed accepted an undeclared fixture ordinal");
    expect_rejected(
        [] {
            static_cast<void>(
                aq5::tie_seed(
                    aq5::kFixtureCount,
                    aq5::DecisionFamily::Priority));
        },
        "AQ5 tie seed accepted an undeclared fixture ordinal");
    expect_rejected(
        [] {
            static_cast<void>(
                aq5::search_seed(
                    0,
                    static_cast<aq5::DecisionFamily>(255)));
        },
        "AQ5 search seed accepted an undeclared decision family");
    expect_rejected(
        [] {
            static_cast<void>(
                aq5::tie_seed(
                    0,
                    static_cast<aq5::DecisionFamily>(255)));
        },
        "AQ5 tie seed accepted an undeclared decision family");
}

aq5::RootReport passing_root(
    std::string stable_id,
    aq5::DecisionFamily family) {
    return {
        .stable_id = std::move(stable_id),
        .family = family,
        .candidates = {
            {
                .key = "preferred",
                .samples = {0.75, 0.75},
                .mean = 0.75,
                .exact_max = true,
            },
            {
                .key = "other",
                .samples = {0.25, 0.25},
                .mean = 0.25,
                .exact_max = false,
            },
        },
        .selected_key = "preferred",
        .accounting = {
            .sampled_worlds = 2,
            .rollout_evaluations = 4,
            .terminal_evaluations = 0,
            .bootstrapped_evaluations = 4,
            .inner_rollout_evaluations = 8,
            .inner_search_invocations = 4,
            .inner_search_max_depth = 1,
        },
        .complete_legal_choice_coverage = true,
        .rules_settled = true,
        .finite_scores = true,
        .accounting_consistent = true,
        .reversed_input_action_keyed_bit_identical = true,
        .hidden_repartition_nonvacuous = true,
        .hidden_observation_bit_identical = true,
        .hidden_scores_bit_identical = true,
        .hidden_choice_bit_identical = true,
        .hidden_accounting_bit_identical = true,
    };
}

aq5::CandidateScore score(
    std::string key, double mean, bool exact_max) {
    return {
        .key = std::move(key),
        .samples = {mean, mean},
        .mean = mean,
        .exact_max = exact_max,
    };
}

aq5::RootReport direction_root(
    std::string stable_id,
    aq5::DecisionFamily family,
    std::string selected_key,
    std::vector<aq5::CandidateScore> candidates) {
    aq5::RootReport root =
        passing_root(std::move(stable_id), family);
    root.selected_key = std::move(selected_key);
    root.candidates = std::move(candidates);
    return root;
}

std::array<aq5::FixtureReport, aq5::kFixtureCount>
passing_fixture_reports() {
    const auto manifest = aq5::fixture_manifest();
    std::array<aq5::FixtureReport, aq5::kFixtureCount>
        reports;
    reports[0] = {
        .spec = manifest[0],
        .roots = {
            direction_root(
                std::string(manifest[0].stable_ids[0]),
                manifest[0].family, "pass",
                {
                    score("pass", 0.8, true),
                    score(
                        "counter-same-air-elemental",
                        0.6, false),
                    score(
                        "counter-own-counterspell",
                        0.7, false),
                }),
            direction_root(
                std::string(manifest[0].stable_ids[1]),
                manifest[0].family,
                "counter-opponent-counterspell",
                {
                    score("pass", 0.4, false),
                    score(
                        "counter-same-air-elemental",
                        0.3, false),
                    score(
                        "counter-own-counterspell",
                        0.2, false),
                    score(
                        "counter-opponent-counterspell",
                        0.8, true),
                }),
        },
        .direction_passed = true,
    };
    reports[1] = {
        .spec = manifest[1],
        .roots = {
            direction_root(
                std::string(manifest[1].stable_ids[0]),
                manifest[1].family,
                "braingeyser-x1-self",
                {
                    score(
                        "braingeyser-x1-self",
                        0.8, true),
                    score(
                        "braingeyser-x0-self",
                        0.4, false),
                    score(
                        "braingeyser-x0-opponent",
                        0.3, false),
                }),
        },
        .direction_passed = true,
    };
    reports[2] = {
        .spec = manifest[2],
        .roots = {
            direction_root(
                std::string(manifest[2].stable_ids[0]),
                manifest[2].family, "ancestral-self",
                {
                    score("ancestral-self", 0.8, true),
                    score(
                        "ancestral-opponent",
                        0.3, false),
                }),
        },
        .direction_passed = true,
    };
    reports[3] = {
        .spec = manifest[3],
        .roots = {
            direction_root(
                std::string(manifest[3].stable_ids[0]),
                manifest[3].family, "pass",
                {
                    score("pass", 0.8, true),
                    score(
                        "growth-own-summoning-sick-"
                        "grizzly-bears",
                        0.3, false),
                }),
            direction_root(
                std::string(manifest[3].stable_ids[1]),
                manifest[3].family, "pass",
                {
                    score("pass", 0.8, true),
                    score(
                        "growth-opponent-tapped-"
                        "air-elemental",
                        0.3, false),
                }),
        },
        .direction_passed = true,
    };
    reports[4] = {
        .spec = manifest[4],
        .roots = {
            direction_root(
                std::string(manifest[4].stable_ids[0]),
                manifest[4].family,
                "force-spike-gray-ogre",
                {
                    score("pass", 0.3, false),
                    score(
                        "force-spike-gray-ogre",
                        0.8, true),
                }),
            direction_root(
                std::string(manifest[4].stable_ids[1]),
                manifest[4].family, "pass",
                {
                    score("pass", 0.8, true),
                    score(
                        "force-spike-gray-ogre",
                        0.3, false),
                }),
        },
        .direction_passed = true,
    };
    reports[5] = {
        .spec = manifest[5],
        .roots = {
            direction_root(
                std::string(manifest[5].stable_ids[0]),
                manifest[5].family, "no-blocks",
                {
                    score("no-blocks", 0.8, true),
                    score(
                        "block-air-elemental-with-"
                        "flying-men",
                        0.3, false),
                }),
            direction_root(
                std::string(manifest[5].stable_ids[1]),
                manifest[5].family,
                "block-air-elemental-with-flying-men",
                {
                    score("no-blocks", 0.3, false),
                    score(
                        "block-air-elemental-with-"
                        "flying-men",
                        0.8, true),
                }),
        },
        .direction_passed = true,
    };
    reports[6] = {
        .spec = manifest[6],
        .roots = {
            direction_root(
                std::string(manifest[6].stable_ids[0]),
                manifest[6].family, "no-attack",
                {
                    score("no-attack", 0.8, true),
                    score(
                        "attack-with-only-legal-attacker",
                        0.3, false),
                }),
        },
        .direction_passed = true,
    };
    reports[7] = {
        .spec = manifest[7],
        .roots = {
            direction_root(
                std::string(manifest[7].stable_ids[0]),
                manifest[7].family, "no-block",
                {
                    score("no-block", 0.8, true),
                    score(
                        "block-air-elemental-with-"
                        "flying-men",
                        0.3, false),
                }),
        },
        .direction_passed = true,
    };
    return reports;
}

void test_direction_and_fixture_gates_are_action_keyed() {
    const auto reports = passing_fixture_reports();
    for (std::size_t index = 0;
         index < reports.size(); ++index) {
        const aq5::FixtureReport& report =
            reports[index];
        expect(
            aq5::fixture_direction_passed(
                report.spec, report.roots) &&
                report.gate_passed(),
            "AQ5 passing direction or fixture gate failed");

        auto reordered = report;
        std::reverse(
            reordered.roots.begin(),
            reordered.roots.end());
        for (aq5::RootReport& root : reordered.roots) {
            std::reverse(
                root.candidates.begin(),
                root.candidates.end());
        }
        expect(
            aq5::fixture_direction_passed(
                reordered.spec, reordered.roots) &&
                reordered.gate_passed(),
            "AQ5 direction gate depends on input order");

        auto missing_root = report;
        missing_root.roots.pop_back();
        expect(
            !aq5::fixture_direction_passed(
                missing_root.spec,
                missing_root.roots) &&
                !missing_root.gate_passed(),
            "AQ5 fixture gate accepted an incomplete root set");

        auto externally_failed = report;
        externally_failed.direction_passed = false;
        expect(
            !externally_failed.gate_passed(),
            "AQ5 fixture gate omitted its recorded direction bit");

        auto unsafe_root = report;
        unsafe_root.roots.front()
            .hidden_scores_bit_identical = false;
        expect(
            !unsafe_root.gate_passed(),
            "AQ5 fixture gate omitted a root safety failure");
    }

    {
        auto changed = reports[0];
        changed.roots[0].selected_key =
            "counter-same-air-elemental";
        expect(
            !aq5::fixture_direction_passed(
                changed.spec, changed.roots),
            "AQ5 accepted redundant same-target Counterspell");
    }
    {
        auto changed = reports[0];
        changed.roots[1].selected_key = "pass";
        expect(
            !aq5::fixture_direction_passed(
                changed.spec, changed.roots),
            "AQ5 omitted the productive intervening counter");
    }
    {
        auto changed = reports[1];
        changed.roots[0].candidates[1].exact_max = true;
        expect(
            !aq5::fixture_direction_passed(
                changed.spec, changed.roots),
            "AQ5 allowed Braingeyser X=0 self in support");
        changed = reports[1];
        changed.roots[0].candidates[2].exact_max = true;
        expect(
            !aq5::fixture_direction_passed(
                changed.spec, changed.roots),
            "AQ5 allowed Braingeyser X=0 opponent in support");
    }
    {
        auto changed = reports[2];
        changed.roots[0].candidates[1].exact_max = true;
        expect(
            !aq5::fixture_direction_passed(
                changed.spec, changed.roots),
            "AQ5 allowed opponent-target Ancestral in support");
    }
    {
        auto changed = reports[3];
        changed.roots[0].selected_key =
            "growth-own-summoning-sick-grizzly-bears";
        expect(
            !aq5::fixture_direction_passed(
                changed.spec, changed.roots),
            "AQ5 accepted Growth on a summoning-sick Bear");
        changed = reports[3];
        changed.roots[1].candidates[1].exact_max = true;
        expect(
            !aq5::fixture_direction_passed(
                changed.spec, changed.roots),
            "AQ5 allowed opponent-target Growth in support");
    }
    {
        auto changed = reports[4];
        changed.roots[0].selected_key = "pass";
        expect(
            !aq5::fixture_direction_passed(
                changed.spec, changed.roots),
            "AQ5 omitted a live Force Spike");
        changed = reports[4];
        changed.roots[1].selected_key =
            "force-spike-gray-ogre";
        expect(
            !aq5::fixture_direction_passed(
                changed.spec, changed.roots),
            "AQ5 accepted a payable five-open Force Spike");
    }
    {
        auto changed = reports[5];
        changed.roots[0].selected_key =
            "block-air-elemental-with-flying-men";
        expect(
            !aq5::fixture_direction_passed(
                changed.spec, changed.roots),
            "AQ5 accepted the life-20 Flying Men chump");
        changed = reports[5];
        changed.roots[1].selected_key = "no-blocks";
        expect(
            !aq5::fixture_direction_passed(
                changed.spec, changed.roots),
            "AQ5 omitted the life-4 Flying Men block");
    }
    {
        auto changed = reports[6];
        changed.roots[0].selected_key =
            "attack-with-only-legal-attacker";
        expect(
            !aq5::fixture_direction_passed(
                changed.spec, changed.roots),
            "AQ5 accepted Flying Men attacking into Air Elemental");
    }
    {
        auto changed = reports[7];
        changed.roots[0].candidates[0].mean = 0.3;
        changed.roots[0].candidates[1].mean = 0.3;
        expect(
            !aq5::fixture_direction_passed(
                changed.spec, changed.roots),
            "AQ5 accepted a tied multi-choice Flying Men chump");
        changed = reports[7];
        changed.roots[0].selected_key =
            "block-air-elemental-with-flying-men";
        expect(
            !aq5::fixture_direction_passed(
                changed.spec, changed.roots),
            "AQ5 selected the multi-choice Flying Men chump");
    }
}

std::vector<old_school::CardId> physical_cards(
    const aq5::PreparedRoot& root, std::size_t player) {
    const old_school::PlayerState& state =
        root.state.players[player];
    std::vector<old_school::CardId> cards = state.hand;
    cards.insert(
        cards.end(), state.library.begin(),
        state.library.end());
    cards.insert(
        cards.end(), state.graveyard.begin(),
        state.graveyard.end());
    cards.insert(
        cards.end(), state.exile.begin(),
        state.exile.end());
    for (const old_school::LandPermanent& land :
         state.lands) {
        cards.push_back(land.card);
    }
    for (const old_school::CreaturePermanent& creature :
         state.creatures) {
        cards.push_back(creature.card);
    }
    for (const old_school::ArtifactPermanent& artifact :
         state.artifacts) {
        cards.push_back(artifact.card);
    }
    cards.insert(
        cards.end(), state.enchantments.begin(),
        state.enchantments.end());
    for (const old_school::StackObject& object :
         root.state.stack) {
        if (object.controller == player &&
            object.kind ==
                old_school::StackObjectKind::Spell) {
            cards.push_back(object.card);
        }
    }
    std::sort(cards.begin(), cards.end());
    return cards;
}

void test_fixture_builder_and_blue_block_shape() {
    const auto manifest = aq5::fixture_manifest();
    const std::vector<aq5::PreparedRoot> roots =
        aq5::build_fixture_roots();
    expect(
        roots.size() == aq5::kFixtureRootCount,
        "AQ5 fixture builder root count drifted");
    std::set<std::string> seen;
    for (std::size_t fixture = 0;
         fixture < manifest.size(); ++fixture) {
        const aq5::FixtureSpec& spec =
            manifest[fixture];
        std::size_t count = 0;
        for (const aq5::PreparedRoot& root : roots) {
            if (root.fixture_ordinal != fixture) {
                continue;
            }
            ++count;
            expect(
                root.family == spec.family &&
                    seen.insert(root.stable_id).second &&
                    std::find(
                        spec.stable_ids.begin(),
                        spec.stable_ids.begin() +
                            static_cast<std::ptrdiff_t>(
                                spec.root_count),
                        root.stable_id) !=
                        spec.stable_ids.begin() +
                            static_cast<std::ptrdiff_t>(
                                spec.root_count),
                "AQ5 fixture builder emitted an unsealed root");
        }
        expect(
            count == spec.root_count,
            "AQ5 fixture builder group count drifted");
    }
    expect(
        seen.size() == aq5::kFixtureRootCount,
        "AQ5 fixture builder repeated a stable root id");

    const aq5::PreparedRoot blue =
        aq5::make_blue_multi_choice_block_fixture();
    const auto found = std::find_if(
        roots.begin(), roots.end(),
        [](const aq5::PreparedRoot& root) {
            return root.stable_id ==
                   aq5::kNewBlueBlockFixtureId;
        });
    expect(
        found != roots.end() && *found == blue,
        "AQ5 fixture corpus did not embed the exact Blue block root");
    expect(
        blue.fixture_ordinal == 7 &&
            blue.family == aq5::DecisionFamily::Block &&
            blue.actor == 0 &&
            blue.phase ==
                old_school::TurnPhase::DeclareBlockers &&
            blue.state.turn_number == 10 &&
            blue.state.active_player == 1 &&
            blue.state.players[0].life == 20 &&
            blue.state.players[1].life == 20 &&
            blue.subject_blocker == 1 &&
            blue.attackers ==
                std::vector<old_school::PermanentId>{2} &&
            blue.selected_blocks.empty() &&
            blue.remaining_blockers ==
                std::vector<old_school::PermanentId>{3} &&
            blue.candidate_keys ==
                std::vector<std::string>{
                    "no-block",
                    "block-air-elemental-with-flying-men"} &&
            blue.state.stack.empty(),
        "AQ5 exact Blue block context drifted");

    const old_school::PlayerState& defender =
        blue.state.players[0];
    const old_school::PlayerState& attacker =
        blue.state.players[1];
    expect(
        defender.lands.size() == 5 &&
            std::all_of(
                defender.lands.begin(),
                defender.lands.end(),
                [](const old_school::LandPermanent& land) {
                    return land.card ==
                               old_school::CardId::Island &&
                           !land.tapped;
                }) &&
            attacker.lands.size() == 5 &&
            std::all_of(
                attacker.lands.begin(),
                attacker.lands.end(),
                [](const old_school::LandPermanent& land) {
                    return land.card ==
                               old_school::CardId::Island &&
                           land.tapped;
                }),
        "AQ5 exact Blue block land state drifted");
    expect(
        defender.creatures.size() == 2 &&
            defender.creatures[0].id == 1 &&
            defender.creatures[0].card ==
                old_school::CardId::FlyingMen &&
            !defender.creatures[0].tapped &&
            !defender.creatures[0].summoning_sick &&
            defender.creatures[1].id == 3 &&
            defender.creatures[1].card ==
                old_school::CardId::AirElemental &&
            !defender.creatures[1].tapped &&
            !defender.creatures[1].summoning_sick &&
            attacker.creatures.size() == 1 &&
            attacker.creatures[0].id == 2 &&
            attacker.creatures[0].card ==
                old_school::CardId::AirElemental &&
            attacker.creatures[0].tapped &&
            !attacker.creatures[0].summoning_sick,
        "AQ5 exact Blue block creature state drifted");
    expect(
        defender.graveyard.empty() &&
            attacker.graveyard.empty() &&
            defender.exile.empty() &&
            attacker.exile.empty() &&
            defender.artifacts.empty() &&
            attacker.artifacts.empty() &&
            defender.enchantments.empty() &&
            attacker.enchantments.empty(),
        "AQ5 exact Blue block public zones drifted");

    std::vector<old_school::CardId> expected =
        old_school::blue_deck();
    std::sort(expected.begin(), expected.end());
    expect(
        blue.original_decks[0] ==
                old_school::blue_deck() &&
            blue.original_decks[1] ==
                old_school::blue_deck() &&
            physical_cards(blue, 0) == expected &&
            physical_cards(blue, 1) == expected,
        "AQ5 exact Blue block deck conservation failed");

    const aq5::PreparedRoot hidden =
        aq5::make_hidden_repartition_clone(blue);
    expect(
        hidden.state != blue.state &&
            old_school::observe_game_state(
                hidden.state, blue.actor) ==
                old_school::observe_game_state(
                    blue.state, blue.actor) &&
            hidden.original_decks ==
                blue.original_decks &&
            hidden.candidate_keys ==
                blue.candidate_keys &&
            hidden.candidates == blue.candidates &&
            physical_cards(hidden, 0) == expected &&
            physical_cards(hidden, 1) == expected,
        "AQ5 Blue hidden clone is vacuous or observation-changing");
    const aq5::PreparedRoot reversed =
        aq5::reverse_candidate_order(blue);
    expect(
        reversed.candidate_keys ==
                std::vector<std::string>{
                    "block-air-elemental-with-flying-men",
                    "no-block"} &&
            aq5::reverse_candidate_order(reversed) == blue,
        "AQ5 candidate-order control is not an exact involution");
}

void test_root_family_and_isolation_gates_fail_closed() {
    const aq5::RootReport root =
        passing_root(
            "synthetic.root",
            aq5::DecisionFamily::Priority);
    expect(root.gate_passed(), "AQ5 complete root gate failed");

    {
        auto changed = root;
        changed.complete_legal_choice_coverage = false;
        expect(
            !changed.gate_passed(),
            "AQ5 root gate omitted legal-choice coverage");
    }
    {
        auto changed = root;
        changed.rules_settled = false;
        expect(
            !changed.gate_passed(),
            "AQ5 root gate omitted rules settlement");
    }
    {
        auto changed = root;
        changed.finite_scores = false;
        expect(
            !changed.gate_passed(),
            "AQ5 root gate omitted finite-score validation");
    }
    {
        auto changed = root;
        changed.accounting_consistent = false;
        expect(
            !changed.gate_passed(),
            "AQ5 root gate omitted accounting consistency");
    }
    {
        auto changed = root;
        changed.reversed_input_action_keyed_bit_identical = false;
        expect(
            !changed.gate_passed(),
            "AQ5 root gate omitted reversed-input identity");
    }
    {
        auto changed = root;
        changed.hidden_repartition_nonvacuous = false;
        expect(
            !changed.gate_passed(),
            "AQ5 root gate accepted a vacuous hidden clone");
    }
    {
        auto changed = root;
        changed.hidden_observation_bit_identical = false;
        expect(
            !changed.gate_passed(),
            "AQ5 root gate omitted observation identity");
    }
    {
        auto changed = root;
        changed.hidden_scores_bit_identical = false;
        expect(
            !changed.gate_passed(),
            "AQ5 root gate omitted hidden score identity");
    }
    {
        auto changed = root;
        changed.hidden_choice_bit_identical = false;
        expect(
            !changed.gate_passed(),
            "AQ5 root gate omitted hidden choice identity");
    }
    {
        auto changed = root;
        changed.hidden_accounting_bit_identical = false;
        expect(
            !changed.gate_passed(),
            "AQ5 root gate omitted hidden accounting identity");
    }
    {
        auto changed = root;
        changed.accounting.inner_search_max_depth = 2;
        expect(
            !changed.gate_passed(),
            "AQ5 root gate accepted recursive depth two");
    }

    const auto passing_family =
        [](aq5::DecisionFamily family) {
            return aq5::FamilyInvariantReport{
                .family = family,
                .roots = 1,
                .complete_legal_choice_coverage = true,
                .rules_settled = true,
                .finite_scores = true,
                .accounting_consistent = true,
                .reversed_input_action_keyed_bit_identical =
                    true,
                .hidden_repartition_nonvacuous = true,
                .hidden_observation_bit_identical = true,
                .hidden_scores_bit_identical = true,
                .hidden_choice_bit_identical = true,
                .hidden_accounting_bit_identical = true,
                .one_level_nesting_bounded = true,
                .maximum_active_nesting = 1,
            };
        };
    const aq5::FamilyInvariantReport family =
        passing_family(aq5::DecisionFamily::Priority);
    expect(
        family.gate_passed(),
        "AQ5 complete family gate failed");
    {
        auto changed = family;
        changed.roots = 0;
        expect(
            !changed.gate_passed(),
            "AQ5 family gate accepted zero roots");
    }
    {
        auto changed = family;
        changed.complete_legal_choice_coverage = false;
        expect(
            !changed.gate_passed(),
            "AQ5 family gate omitted legal-choice coverage");
    }
    {
        auto changed = family;
        changed.rules_settled = false;
        expect(
            !changed.gate_passed(),
            "AQ5 family gate omitted rules settlement");
    }
    {
        auto changed = family;
        changed.finite_scores = false;
        expect(
            !changed.gate_passed(),
            "AQ5 family gate omitted finite scores");
    }
    {
        auto changed = family;
        changed.accounting_consistent = false;
        expect(
            !changed.gate_passed(),
            "AQ5 family gate omitted accounting consistency");
    }
    {
        auto changed = family;
        changed.reversed_input_action_keyed_bit_identical = false;
        expect(
            !changed.gate_passed(),
            "AQ5 family gate omitted reversed-input identity");
    }
    {
        auto changed = family;
        changed.hidden_repartition_nonvacuous = false;
        expect(
            !changed.gate_passed(),
            "AQ5 family gate accepted vacuous hidden controls");
    }
    {
        auto changed = family;
        changed.hidden_observation_bit_identical = false;
        expect(
            !changed.gate_passed(),
            "AQ5 family gate omitted observation identity");
    }
    {
        auto changed = family;
        changed.hidden_scores_bit_identical = false;
        expect(
            !changed.gate_passed(),
            "AQ5 family gate omitted hidden score identity");
    }
    {
        auto changed = family;
        changed.hidden_choice_bit_identical = false;
        expect(
            !changed.gate_passed(),
            "AQ5 family gate omitted hidden choice identity");
    }
    {
        auto changed = family;
        changed.hidden_accounting_bit_identical = false;
        expect(
            !changed.gate_passed(),
            "AQ5 family gate omitted hidden accounting identity");
    }
    {
        auto changed = family;
        changed.one_level_nesting_bounded = false;
        expect(
            !changed.gate_passed(),
            "AQ5 family gate omitted recursion sentinel");
    }
    {
        auto changed = family;
        changed.maximum_active_nesting = 2;
        expect(
            !changed.gate_passed(),
            "AQ5 family gate accepted recursive depth two");
    }

    const aq5::IsolationReport isolation{
        .exact_parent = true,
        .treatment_default_off = true,
        .exact_configuration = true,
        .all_other_treatments_off = true,
        .treatment_off_fixed_seed_game_bit_identical = true,
    };
    expect(
        isolation.gate_passed(),
        "AQ5 complete isolation gate failed");
    {
        auto changed = isolation;
        changed.exact_parent = false;
        expect(
            !changed.gate_passed(),
            "AQ5 isolation gate omitted exact parent");
    }
    {
        auto changed = isolation;
        changed.treatment_default_off = false;
        expect(
            !changed.gate_passed(),
            "AQ5 isolation gate omitted default-off");
    }
    {
        auto changed = isolation;
        changed.exact_configuration = false;
        expect(
            !changed.gate_passed(),
            "AQ5 isolation gate omitted exact configuration");
    }
    {
        auto changed = isolation;
        changed.all_other_treatments_off = false;
        expect(
            !changed.gate_passed(),
            "AQ5 isolation gate accepted a combined treatment");
    }
    {
        auto changed = isolation;
        changed.treatment_off_fixed_seed_game_bit_identical =
            false;
        expect(
            !changed.gate_passed(),
            "AQ5 isolation gate omitted treatment-off identity");
    }
}

aq5::FamilyInvariantReport passing_family(
    aq5::DecisionFamily family, std::size_t roots) {
    return {
        .family = family,
        .roots = roots,
        .complete_legal_choice_coverage = true,
        .rules_settled = true,
        .finite_scores = true,
        .accounting_consistent = true,
        .reversed_input_action_keyed_bit_identical = true,
        .hidden_repartition_nonvacuous = true,
        .hidden_observation_bit_identical = true,
        .hidden_scores_bit_identical = true,
        .hidden_choice_bit_identical = true,
        .hidden_accounting_bit_identical = true,
        .one_level_nesting_bounded = true,
        .maximum_active_nesting = 1,
    };
}

aq5::IsolationReport passing_isolation() {
    return {
        .exact_parent = true,
        .treatment_default_off = true,
        .exact_configuration = true,
        .all_other_treatments_off = true,
        .treatment_off_fixed_seed_game_bit_identical = true,
    };
}

aq5::UntreatedC16Report passing_untreated_report(
    const std::array<aq5::FixtureReport,
                     aq5::kFixtureCount>& fixtures) {
    aq5::UntreatedC16Report report{
        .parent_fingerprint =
            std::string(aq5::kRequiredParentFingerprint),
        .captured_before_rpi = true,
    };
    for (const aq5::FixtureReport& fixture :
         fixtures) {
        for (const aq5::RootReport& root :
             fixture.roots) {
            const auto selected = std::find_if(
                root.candidates.begin(),
                root.candidates.end(),
                [&root](
                    const aq5::CandidateScore& candidate) {
                    return candidate.key ==
                           root.selected_key;
                });
            expect(
                selected != root.candidates.end(),
                "synthetic untreated root has no selected action");
            double runner_up =
                -std::numeric_limits<double>::infinity();
            for (const aq5::CandidateScore& candidate :
                 root.candidates) {
                if (candidate.key != root.selected_key) {
                    runner_up =
                        std::max(
                            runner_up, candidate.mean);
                }
            }
            expect(
                std::isfinite(runner_up),
                "synthetic untreated root has no runner-up");
            report.roots.push_back({
                .stable_id = root.stable_id,
                .family = root.family,
                .candidates = root.candidates,
                .selected_key = root.selected_key,
                .selected_margin =
                    selected->mean - runner_up,
                .complete_legal_choice_coverage = true,
                .finite_scores = true,
            });
        }
    }
    return report;
}

aq5::PreflightReport passing_preflight_report() {
    aq5::PreflightReport report{
        .recipe = aq5::sealed_recipe(),
        .parent_fingerprint =
            std::string(aq5::kRequiredParentFingerprint),
        .fixtures = passing_fixture_reports(),
        .families = {
            passing_family(
                aq5::DecisionFamily::Priority, 8),
            passing_family(
                aq5::DecisionFamily::Attack, 1),
            passing_family(
                aq5::DecisionFamily::Block, 3),
        },
        .isolation = passing_isolation(),
        .hypothesis_passed = true,
    };
    report.untreated =
        passing_untreated_report(report.fixtures);
    return report;
}

aq5::BridgeReport passing_bridge_report() {
    const auto smoke =
        [](aq5::DecisionFamily family,
           std::string fixture_id) {
            return aq5::BridgeSmokeReport{
                .family = family,
                .fixture_id = std::move(fixture_id),
                .elapsed_seconds =
                    aq5::kBridgeTimeoutSeconds - 1,
                .authoritative_decision_completed = true,
            };
        };
    return {
        .smokes = {
            smoke(
                aq5::DecisionFamily::Priority,
                "bridge.ru-mirror.seed42.first-island-pass"),
            smoke(
                aq5::DecisionFamily::Attack,
                "diagnostic.ru.life20-flying-men-attack-air.v1"),
            smoke(
                aq5::DecisionFamily::Block,
                std::string(aq5::kNewBlueBlockFixtureId)),
        },
        .web_bridge_tests_passed = true,
        .web_ui_tests_passed = true,
    };
}

void test_preflight_and_public_license_are_exact_conjunctions() {
    const aq5::PreflightReport passing =
        passing_preflight_report();
    const aq5::BridgeReport bridge =
        passing_bridge_report();
    expect(
        passing.untreated.gate_passed() &&
            passing.gate_passed() &&
            aq5::public_option_licensed(
                passing, bridge),
        "AQ5 complete preflight/public license failed");

    {
        auto changed = passing.untreated;
        changed.parent_fingerprint = "wrong";
        expect(
            !changed.gate_passed(),
            "AQ5 untreated gate omitted exact C16");
    }
    {
        auto changed = passing.untreated;
        changed.captured_before_rpi = false;
        expect(
            !changed.gate_passed(),
            "AQ5 untreated gate omitted capture ordering");
    }
    {
        auto changed = passing.untreated;
        changed.roots.pop_back();
        expect(
            !changed.gate_passed(),
            "AQ5 untreated gate accepted eleven roots");
    }
    {
        auto changed = passing.untreated;
        changed.roots[0].stable_id =
            changed.roots[1].stable_id;
        expect(
            !changed.gate_passed(),
            "AQ5 untreated gate accepted a duplicate fixture");
    }
    {
        auto changed = passing.untreated;
        changed.roots[0].family =
            aq5::DecisionFamily::Attack;
        expect(
            !changed.gate_passed(),
            "AQ5 untreated gate omitted fixture family");
    }
    {
        auto changed = passing.untreated;
        changed.roots[0].selected_margin =
            std::nextafter(
                changed.roots[0].selected_margin,
                std::numeric_limits<double>::infinity());
        expect(
            !changed.gate_passed(),
            "AQ5 untreated gate accepted a false margin");
    }
    {
        auto changed = passing.untreated;
        changed.roots[0].candidates[0].samples.clear();
        expect(
            !changed.gate_passed(),
            "AQ5 untreated gate accepted empty score samples");
    }
    {
        auto changed = passing;
        changed.hypothesis_passed = false;
        expect(
            !changed.gate_passed() &&
                !aq5::public_option_licensed(
                    changed, bridge),
            "AQ5 preflight omitted its hypothesis conjunction");
    }
    {
        auto changed = passing;
        changed.recipe.actor_local_redeterminization =
            false;
        expect(
            !changed.gate_passed(),
            "AQ5 preflight accepted recipe drift");
    }
    {
        auto changed = passing;
        changed.parent_fingerprint = "wrong";
        expect(
            !changed.gate_passed(),
            "AQ5 preflight omitted exact parent");
    }
    {
        auto changed = passing;
        changed.untreated.captured_before_rpi = false;
        expect(
            !changed.gate_passed(),
            "AQ5 preflight omitted untreated evidence");
    }
    {
        auto changed = passing;
        changed.fixtures[0].direction_passed = false;
        expect(
            !changed.gate_passed(),
            "AQ5 preflight omitted a directional fixture");
    }
    {
        auto changed = passing;
        changed.families[0]
            .hidden_scores_bit_identical = false;
        expect(
            !changed.gate_passed(),
            "AQ5 preflight omitted a family invariant");
    }
    {
        auto changed = passing;
        changed.isolation.all_other_treatments_off =
            false;
        expect(
            !changed.gate_passed(),
            "AQ5 preflight omitted configuration isolation");
    }
    {
        auto changed = passing;
        changed.fixtures[3] = changed.fixtures[0];
        expect(
            !changed.gate_passed(),
            "AQ5 preflight accepted a duplicate manifest group");
    }
    {
        auto changed = passing;
        changed.families[1] = changed.families[0];
        expect(
            !changed.gate_passed(),
            "AQ5 preflight accepted duplicate family evidence");
    }
    {
        auto changed = passing;
        ++changed.families[2].roots;
        expect(
            !changed.gate_passed(),
            "AQ5 preflight accepted a false family root census");
    }
    {
        auto changed = bridge;
        changed.web_bridge_tests_passed = false;
        expect(
            passing.gate_passed() &&
                !aq5::public_option_licensed(
                    passing, changed),
            "AQ5 public option ignored bridge failure");
    }
}

void test_bridge_gates_are_strict_and_conjunctive() {
    const auto passing_smoke =
        [](aq5::DecisionFamily family,
           std::string fixture_id) {
            return aq5::BridgeSmokeReport{
                .family = family,
                .fixture_id = std::move(fixture_id),
                .elapsed_seconds =
                    aq5::kBridgeTimeoutSeconds - 1,
                .authoritative_decision_completed = true,
            };
        };
    const aq5::BridgeSmokeReport smoke =
        passing_smoke(
            aq5::DecisionFamily::Priority,
            "bridge.ru-mirror.seed42.first-island-pass");
    expect(
        smoke.gate_passed(),
        "AQ5 complete bridge smoke failed");
    {
        auto changed = smoke;
        changed.fixture_id.clear();
        expect(
            !changed.gate_passed(),
            "AQ5 bridge smoke accepted an unnamed fixture");
    }
    {
        auto changed = smoke;
        changed.elapsed_seconds =
            aq5::kBridgeTimeoutSeconds;
        expect(
            !changed.gate_passed(),
            "AQ5 bridge smoke accepted the timeout boundary");
    }
    {
        auto changed = smoke;
        changed.authoritative_decision_completed = false;
        expect(
            !changed.gate_passed(),
            "AQ5 bridge smoke omitted authoritative completion");
    }

    aq5::BridgeReport bridge{
        .smokes = {
            passing_smoke(
                aq5::DecisionFamily::Priority,
                "bridge.ru-mirror.seed42.first-island-pass"),
            passing_smoke(
                aq5::DecisionFamily::Attack,
                "diagnostic.ru.life20-flying-men-attack-air.v1"),
            passing_smoke(
                aq5::DecisionFamily::Block,
                std::string(aq5::kNewBlueBlockFixtureId)),
        },
        .web_bridge_tests_passed = true,
        .web_ui_tests_passed = true,
    };
    expect(
        bridge.gate_passed(),
        "AQ5 complete three-family bridge gate failed");
    for (std::size_t index = 0;
         index < bridge.smokes.size(); ++index) {
        auto changed = bridge;
        changed.smokes[index]
            .authoritative_decision_completed = false;
        expect(
            !changed.gate_passed(),
            "AQ5 bridge gate omitted a declared smoke");
    }
    {
        auto changed = bridge;
        changed.smokes[1].family =
            aq5::DecisionFamily::Priority;
        expect(
            !changed.gate_passed(),
            "AQ5 bridge gate accepted a missing decision family");
    }
    {
        auto changed = bridge;
        changed.web_bridge_tests_passed = false;
        expect(
            !changed.gate_passed(),
            "AQ5 bridge gate omitted web bridge tests");
    }
    {
        auto changed = bridge;
        changed.web_ui_tests_passed = false;
        expect(
            !changed.gate_passed(),
            "AQ5 bridge gate omitted web UI tests");
    }

    const aq5::PreflightReport failed_preflight;
    expect(
        !aq5::public_option_licensed(
            failed_preflight, bridge),
        "AQ5 public option ignored a failed core preflight");
    auto failed_bridge = bridge;
    failed_bridge.web_ui_tests_passed = false;
    expect(
        !aq5::public_option_licensed(
            failed_preflight, failed_bridge),
        "AQ5 public option licensed two failed gates");
}

} // namespace

int main() {
    try {
        test_sealed_recipe_and_defaults();
        test_manifest_shape_and_seed_coordinates();
        test_direction_and_fixture_gates_are_action_keyed();
        test_fixture_builder_and_blue_block_shape();
        test_root_family_and_isolation_gates_fail_closed();
        test_preflight_and_public_license_are_exact_conjunctions();
        test_bridge_gates_are_strict_and_conjunctive();
        std::cout
            << "7 action-Q recursive policy-improvement "
               "protocol tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "action-Q recursive policy-improvement test failure: "
            << error.what() << '\n';
        return 1;
    }
}
