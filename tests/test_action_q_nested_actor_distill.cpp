#include "old_school/action_q_nested_actor_distill.hpp"

#include "old_school/learned_iteration.hpp"
#include "old_school/probes.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

namespace aq =
    old_school::action_q_nested_actor_distill;
namespace iteration = old_school::learned_iteration;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Function>
void expect_rejected(
    Function function, std::string_view message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

aq::ManifestRoot make_manifest_root(
    old_school::DeckId deck, std::size_t retained_position) {
    const auto schedule = iteration::balanced_schedule(
        aq::kCollectionRootSeed, aq::kScheduleGeneration,
        aq::kScheduleBlock);
    const auto match = std::find_if(
        schedule.begin(), schedule.end(),
        [deck](const iteration::ScheduledGame& game) {
            return game.seat_decks[0] == deck ||
                   game.seat_decks[1] == deck;
        });
    expect(
        match != schedule.end(),
        "AQ4-G1 synthetic census could not find owner deck");
    const std::size_t actor =
        match->seat_decks[0] == deck ? 0 : 1;
    const std::size_t ordinal = retained_position;

    std::vector<old_school::PriorityAction> actions{
        old_school::PriorityAction::pass(),
        old_school::PriorityAction::play_land(
            old_school::CardId::Forest),
    };
    std::vector<std::string> descriptors;
    std::vector<std::vector<double>> options;
    descriptors.reserve(actions.size());
    options.reserve(actions.size());
    for (std::size_t index = 0; index < actions.size(); ++index) {
        descriptors.push_back(
            old_school::probes::
                stable_priority_action_descriptor(actions[index]));
        std::vector<double> features(
            aq::kPolicyFeatureCount, 0.0);
        features[0] =
            static_cast<double>(
                static_cast<std::size_t>(deck) + 1);
        features[1] =
            static_cast<double>(retained_position);
        features[2] = static_cast<double>(index);
        options.push_back(std::move(features));
    }
    return {
        .coordinate = {
            .schedule_index = match->schedule_index,
            .pairing_index = match->pairing_index,
            .game_seed = match->seed,
            .starting_player = match->starting_player,
            .seat_decks = match->seat_decks,
            .actor = actor,
            .trace_ordinal = ordinal,
            .nontrivial_ordinal = ordinal,
            .actor_game_nontrivial_roots = 2,
            .retained_position = retained_position,
            .split =
                aq::split_for_retained_position(
                    retained_position),
            .search_seed = aq::root_search_seed(
                match->schedule_index, actor, ordinal),
        },
        .actions = std::move(actions),
        .action_descriptors = std::move(descriptors),
        .options = std::move(options),
    };
}

aq::Census make_valid_census() {
    std::array<aq::DeckCensus, old_school::kDeckCount> decks{};
    std::vector<aq::ManifestRoot> roots;
    for (std::size_t index = 0;
         index < old_school::kDeckCount; ++index) {
        decks[index] = {
            .actor_games = 16,
            .nontrivial_roots = 2,
            .retained_roots = {1, 1},
            .retained_options = {2, 2},
        };
        const auto deck =
            static_cast<old_school::DeckId>(index);
        roots.push_back(make_manifest_root(deck, 0));
        roots.push_back(make_manifest_root(deck, 1));
    }
    std::sort(
        roots.begin(), roots.end(),
        [](const aq::ManifestRoot& first,
           const aq::ManifestRoot& second) {
            return std::tie(
                       first.coordinate.schedule_index,
                       first.coordinate.actor,
                       first.coordinate.retained_position) <
                   std::tie(
                       second.coordinate.schedule_index,
                       second.coordinate.actor,
                       second.coordinate.retained_position);
        });
    return aq::testing::make_census(
        std::string(aq::kRequiredParentFingerprint),
        std::move(roots), decks,
        iteration::kBalancedScheduleGames);
}

old_school::BotBenchmarkSummary selector_summary(
    const std::array<std::size_t, old_school::kDeckCount>&
        wins) {
    old_school::BotBenchmarkSummary result;
    result.evaluation_seed = aq::kSelectorSeed;
    result.learned_training_seed = 424242;
    result.repetitions_per_deck_pairing =
        aq::kSelectorRepetitions;
    result.total_games = aq::kExpectedSelectorGames;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        auto& challenger = result.challenger_decks[deck];
        auto& baseline = result.baseline_decks[deck];
        challenger.games =
            aq::kExpectedSelectorGamesPerDeck;
        challenger.wins = wins[deck];
        challenger.losses =
            challenger.games - challenger.wins;
        baseline.games = challenger.games;
        baseline.wins = challenger.losses;
        baseline.losses = challenger.wins;
        result.challenger_stats.wins += challenger.wins;
        result.challenger_stats.losses += challenger.losses;
    }
    result.challenger_stats.games =
        aq::kExpectedSelectorGames;
    result.baseline_stats.games = aq::kExpectedSelectorGames;
    result.baseline_stats.wins =
        result.challenger_stats.losses;
    result.baseline_stats.losses =
        result.challenger_stats.wins;
    return result;
}

void test_cli_recipe_and_optimizer_are_frozen() {
    const std::vector<std::string_view> census{"--census"};
    const std::vector<std::string_view> run{"--run"};
    const std::vector<std::string_view> empty;
    const std::vector<std::string_view> extra{
        "--census",
        "--run",
    };
    expect(
        aq::parse_command(census) == aq::Command::Census &&
            aq::parse_command(run) == aq::Command::Run &&
            !aq::parse_command(empty).has_value() &&
            !aq::parse_command(extra).has_value(),
        "AQ4-G1 CLI accepted an undeclared shape");
    std::ostringstream usage;
    aq::print_usage(usage);
    expect(
        usage.str() ==
            "Usage: old-school-action-q-nested-actor-distill "
            "(--census|--run)\n",
        "AQ4-G1 usage drifted");

    const auto base = aq::base_search_config(123);
    const auto teacher = aq::teacher_search_config(456);
    const auto optimizer = aq::optimizer_config();
    expect(
        aq::kCollectionRootSeed == 202607282031ULL &&
            aq::kFitSeed == 12262988820247274425ULL &&
            aq::kSelectorSeed == 202607282041ULL &&
            aq::kMaximumRootsPerActorGame == 8 &&
            aq::kPolicyFeatureCount == 893 &&
            aq::kCandidateResidualWeight == 0.10 &&
            aq::kFastGoWins == 37 &&
            aq::kManualOnlyWins == 31 &&
            aq::kMinimumDeckWins == 3 &&
            aq::kFrozenCensusManifestHash ==
                "c67d345dba6f2ea1c59014aefd56aadfbf6560daa610445f"
                "f686a9fe0999d80b" &&
            base.seed == 123 &&
            base.worlds == 8 &&
            base.rollouts_per_world == 1 &&
            base.horizon_turns == 4 &&
            base.value_continuation_search_worlds == 0 &&
            base.blend_shallow_prior &&
            teacher.seed == 456 &&
            teacher.worlds == 8 &&
            teacher.rollouts_per_world == 1 &&
            teacher.horizon_turns == 8 &&
            teacher.value_continuation_search_worlds == 2 &&
            !teacher.blend_shallow_prior &&
            teacher.evaluation_threads == 4 &&
            optimizer.batch_size == 64 &&
            optimizer.epochs == 64 &&
            optimizer.learning_rate == 0.003 &&
            optimizer.beta1 == 0.9 &&
            optimizer.beta2 == 0.999 &&
            optimizer.epsilon == 1.0e-8 &&
            optimizer.global_gradient_norm_clip == 5.0 &&
            optimizer.seed == aq::kFitSeed &&
            optimizer.residual_weight == 0.10 &&
            optimizer.policy_temperature == 0.10,
        "AQ4-G1 recipe or optimizer drifted");
}

void test_split_and_seed_domains_are_exact() {
    expect(
        aq::split_for_retained_position(0) ==
                aq::Split::Fit &&
            aq::split_for_retained_position(1) ==
                aq::Split::Check &&
            aq::split_for_retained_position(8) ==
                aq::Split::Fit &&
            aq::split_for_retained_position(9) ==
                aq::Split::Check,
        "AQ4-G1 even/odd split drifted");
    expect(
        aq::root_search_seed(17, 1, 23) ==
            2925409084975404554ULL,
        "AQ4-G1 retained-root search seed drifted");
    expect(
        aq::kFitSeed ==
            iteration::derive_seed(
                aq::kCollectionRootSeed,
                iteration::SeedDomain::PolicyFit, 0, 0, 0),
        "AQ4-G1 fit seed is outside the PolicyFit domain");
}

void test_census_hash_validation_and_freeze_are_fail_closed() {
    const aq::Census census = make_valid_census();
    aq::validate_census(census);
    expect(
        census.manifest_hash ==
                aq::canonical_manifest_hash(census) &&
            census == make_valid_census(),
        "AQ4-G1 census or owner-safe hash is nondeterministic");

    aq::Census changed = census;
    changed.roots.front().options.front().front() += 1.0;
    expect(
        aq::canonical_manifest_hash(changed) !=
            census.manifest_hash,
        "AQ4-G1 manifest hash omitted an owner-safe feature");

    aq::Census bad_hash = census;
    bad_hash.manifest_hash = "not-the-manifest";
    expect_rejected(
        [&] {
            aq::validate_census(bad_hash);
        },
        "AQ4-G1 census accepted a mismatched manifest hash");

    aq::Census bad_feature_width = census;
    bad_feature_width.roots.front().options.front().pop_back();
    expect_rejected(
        [&] {
            aq::validate_census(bad_feature_width);
        },
        "AQ4-G1 census accepted a non-893 feature row");

    expect_rejected(
        [&] {
            aq::require_frozen_census(census);
        },
        "AQ4-G1 run opened while the census hash is unfrozen");
}

void test_selector_classification_is_conjunctive() {
    expect(
        aq::classify_selector(selector_summary(
            {8, 8, 7, 7, 7})) ==
            aq::SelectorDisposition::FastGo,
        "AQ4-G1 rejected the exact FAST_GO boundary");
    expect(
        aq::classify_selector(selector_summary(
            {7, 6, 6, 6, 6})) ==
            aq::SelectorDisposition::ManualOnly,
        "AQ4-G1 rejected the exact MANUAL_ONLY boundary");
    expect(
        aq::classify_selector(selector_summary(
            {6, 6, 6, 6, 6})) ==
            aq::SelectorDisposition::Reject,
        "AQ4-G1 accepted the aggregate rejection boundary");
    expect(
        aq::classify_selector(selector_summary(
            {2, 9, 9, 9, 8})) ==
            aq::SelectorDisposition::Reject,
        "AQ4-G1 ignored the per-deck floor");
}

void test_offline_gate_membership_is_exact() {
    aq::OfflineReport passing;
    passing.parent_immutable = true;
    passing.repeated_fit_bit_identical = true;
    passing.only_priority_component_changed = true;
    passing.fit_regret_strictly_improved = true;
    passing.check_regret_strictly_improved = true;
    passing.check_deck_regret_guard.fill(true);
    passing.descriptor_order_identity = true;
    passing.redundant_counter_pass = true;
    passing.braingeyser_x_zero_excluded = true;
    passing.sick_bear_growth_pass = true;
    passing.live_force_spike = true;
    expect(
        !passing.frozen_dev.gate_passed() &&
            !passing.ancestral.gate_passed() &&
            passing.gate_passed(),
        "AQ4-G1 made a descriptive legacy diagnostic "
        "conjunctive");

    const auto rejects =
        [&](auto mutate, std::string_view message) {
            aq::OfflineReport report = passing;
            mutate(report);
            expect(!report.gate_passed(), message);
        };
    rejects(
        [](aq::OfflineReport& report) {
            report.parent_immutable = false;
        },
        "AQ4-G1 omitted parent immutability");
    rejects(
        [](aq::OfflineReport& report) {
            report.repeated_fit_bit_identical = false;
        },
        "AQ4-G1 omitted repeated-fit identity");
    rejects(
        [](aq::OfflineReport& report) {
            report.only_priority_component_changed = false;
        },
        "AQ4-G1 omitted component isolation");
    rejects(
        [](aq::OfflineReport& report) {
            report.fit_regret_strictly_improved = false;
        },
        "AQ4-G1 omitted FIT regret improvement");
    rejects(
        [](aq::OfflineReport& report) {
            report.check_regret_strictly_improved = false;
        },
        "AQ4-G1 omitted CHECK regret improvement");
    rejects(
        [](aq::OfflineReport& report) {
            report.check_deck_regret_guard[3] = false;
        },
        "AQ4-G1 omitted a CHECK deck guard");
    rejects(
        [](aq::OfflineReport& report) {
            report.descriptor_order_identity = false;
        },
        "AQ4-G1 omitted descriptor/order identity");
    rejects(
        [](aq::OfflineReport& report) {
            report.redundant_counter_pass = false;
        },
        "AQ4-G1 omitted the redundant-counter repair");
    rejects(
        [](aq::OfflineReport& report) {
            report.braingeyser_x_zero_excluded = false;
        },
        "AQ4-G1 omitted the Braingeyser X=0 repair");
    rejects(
        [](aq::OfflineReport& report) {
            report.sick_bear_growth_pass = false;
        },
        "AQ4-G1 omitted the sick-Bear Growth repair");
    rejects(
        [](aq::OfflineReport& report) {
            report.live_force_spike = false;
        },
        "AQ4-G1 omitted the live Force Spike repair");
}

void test_selector_refuses_unauthorized_reports() {
    const aq::FitReport fit;
    const aq::OfflineReport offline;
    expect_rejected(
        [&] {
            static_cast<void>(
                aq::run_selector(
                    std::shared_ptr<
                        const old_school::LearnedModel>{},
                    std::shared_ptr<
                        const old_school::LearnedModel>{},
                    fit, offline));
        },
        "AQ4-G1 selector opened without a bound passing fit");
}

void test_parameterized_preflight_seam_preserves_aq4() {
    const auto recipe = aq::preflight_recipe();
    constexpr std::array<std::uint64_t, 4> kFixtureSeeds{
        17325739327377847697ULL,
        7290531001140838622ULL,
        14289136244687689594ULL,
        18147400410803436980ULL,
    };
    bool fixture_seeds_match = true;
    for (std::size_t index = 0;
         index < kFixtureSeeds.size(); ++index) {
        fixture_seeds_match =
            fixture_seeds_match &&
            old_school::action_q_nested_actor_diagnostic::
                    preflight_fixture_seed(recipe, index) ==
                kFixtureSeeds[index];
    }
    const auto outer =
        old_school::action_q_nested_actor_diagnostic::
            preflight_outer_search_config(recipe, 991);
    expect(
        fixture_seeds_match &&
            recipe.root_seed == aq::kCollectionRootSeed &&
            recipe.worlds == aq::kTeacherWorlds &&
            recipe.rollouts_per_world == 1 &&
            recipe.horizon_turns ==
                aq::kTeacherHorizonTurns &&
            recipe.evaluation_threads ==
                aq::kTeacherEvaluationThreads &&
            recipe.inner_worlds == aq::kInnerWorlds &&
            old_school::action_q_nested_actor_diagnostic::
                    preflight_actor_local_seed(recipe) ==
                aq::kActorLocalSeed &&
            outer.seed == 991 &&
            outer.worlds == 8 &&
            outer.rollouts_per_world == 1 &&
            outer.horizon_turns == 8 &&
            outer.evaluation_threads == 4 &&
            outer.value_continuation_search_worlds == 2 &&
            !outer.blend_shallow_prior,
        "AQ4-G1 preflight seam changed its frozen AQ4 recipe");
    expect_rejected(
        [&] {
            static_cast<void>(
                old_school::
                    action_q_nested_actor_diagnostic::
                        preflight_fixture_seed(recipe, 4));
        },
        "AQ4-G1 accepted an undeclared preflight fixture");
    old_school::action_q_nested_actor_diagnostic::
        validate_fixture_witnesses();
}

} // namespace

int main() {
    std::size_t passed = 0;
    const auto run =
        [&passed](std::string_view name, auto test) {
            try {
                test();
                ++passed;
            } catch (const std::exception& error) {
                std::cerr
                    << "FAIL " << name << ": "
                    << error.what() << '\n';
                throw;
            }
        };
    run(
        "cli_recipe_and_optimizer_are_frozen",
        test_cli_recipe_and_optimizer_are_frozen);
    run(
        "split_and_seed_domains_are_exact",
        test_split_and_seed_domains_are_exact);
    run(
        "census_hash_validation_and_freeze_are_fail_closed",
        test_census_hash_validation_and_freeze_are_fail_closed);
    run(
        "selector_classification_is_conjunctive",
        test_selector_classification_is_conjunctive);
    run(
        "offline_gate_membership_is_exact",
        test_offline_gate_membership_is_exact);
    run(
        "selector_refuses_unauthorized_reports",
        test_selector_refuses_unauthorized_reports);
    run(
        "parameterized_preflight_seam_preserves_aq4",
        test_parameterized_preflight_seam_preserves_aq4);
    std::cout
        << "action-Q nested-actor distill tests: "
        << passed << "/7 passed\n";
    return 0;
}
