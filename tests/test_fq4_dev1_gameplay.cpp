#include "old_school/fq4_dev1_gameplay.hpp"

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
#include <utility>
#include <vector>

namespace gameplay = old_school::fq4_dev1_gameplay;
namespace integrity = old_school::artifact_integrity;

namespace {

class TestRunner {
  public:
    template <typename Function>
    void run(std::string_view name, Function function) {
        try {
            function();
            ++passed_;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failed_;
            std::cerr << "[FAIL] " << name << ": "
                      << error.what() << '\n';
        }
    }

    int finish() const {
        std::cout << passed_ << " passed, " << failed_
                  << " failed\n";
        return failed_ == 0 ? 0 : 1;
    }

  private:
    std::size_t passed_ = 0;
    std::size_t failed_ = 0;
};

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

std::size_t occurrence_count(
    std::string_view value, std::string_view token) {
    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = value.find(token, offset)) !=
           std::string_view::npos) {
        ++count;
        offset += token.size();
    }
    return count;
}

struct OpaqueModels {
    std::shared_ptr<int> parent_owner =
        std::make_shared<int>(1);
    std::shared_ptr<int> candidate_owner =
        std::make_shared<int>(2);
    std::shared_ptr<const old_school::LearnedModel> parent{
        parent_owner,
        reinterpret_cast<const old_school::LearnedModel*>(
            parent_owner.get())};
    std::shared_ptr<const old_school::LearnedModel> candidate{
        candidate_owner,
        reinterpret_cast<const old_school::LearnedModel*>(
            candidate_owner.get())};
};

integrity::RegularFileSnapshot snapshot(
    std::string path, std::size_t bytes,
    std::string sha256, std::uint64_t inode) {
    return {
        .path = std::move(path),
        .physical_path = "/physical/" +
            std::to_string(inode),
        .byte_size = bytes,
        .sha256 = std::move(sha256),
        .device = 7,
        .inode = inode,
        .link_count = 1,
        .modification_seconds = 100,
        .modification_nanoseconds = 200,
        .change_seconds = 300,
        .change_nanoseconds = 400,
    };
}

gameplay::FixedDeployment deployment() {
    static OpaqueModels models;
    gameplay::FixedDeployment result{
        .parent = models.parent,
        .candidate = models.candidate,
        .parent_model_fingerprint =
            std::string(gameplay::kParentModelFingerprint),
        .candidate_model_fingerprint =
            std::string(gameplay::kCandidateModelFingerprint),
        .parent_components = {
            .critic = std::string(64, '1'),
            .priority = std::string(64, '2'),
            .attack = std::string(64, '3'),
            .block = std::string(64, '4'),
            .damage_order = std::string(64, '5'),
        },
        .candidate_components = {
            .critic = std::string(64, '1'),
            .priority = std::string(64, '6'),
            .attack = std::string(64, '3'),
            .block = std::string(64, '4'),
            .damage_order = std::string(64, '5'),
        },
        .parent_before = snapshot(
            "/artifacts/c16.bin",
            gameplay::kParentArtifactBytes,
            std::string(gameplay::kParentArtifactSha256),
            10),
        .candidate_artifact_before = snapshot(
            "/artifacts/fq4-dev1.bin", 123456,
            std::string(64, 'a'), 20),
        .candidate_artifact_family =
            "old-school.fq4-dev-priority-candidate.v1",
        .candidate_artifact_recipe =
            "fq4-dev1-fixed-priority-delta",
        .fixed_identity_valid = true,
    };
    return result;
}

old_school::DeckSimulationStats deck_stats(
    std::size_t wins_per_quadrant,
    std::size_t losses_per_quadrant,
    std::size_t draws_per_quadrant) {
    const std::size_t games_per_quadrant =
        wins_per_quadrant + losses_per_quadrant +
        draws_per_quadrant;
    return {
        .games = 4 * games_per_quadrant,
        .wins = 4 * wins_per_quadrant,
        .losses = 4 * losses_per_quadrant,
        .draws = 4 * draws_per_quadrant,
        .on_play_games = 2 * games_per_quadrant,
        .on_play_wins = 2 * wins_per_quadrant,
        .on_draw_games = 2 * games_per_quadrant,
        .on_draw_wins = 2 * wins_per_quadrant,
    };
}

void populate_matchup_row(
    old_school::BotBenchmarkSummary& summary,
    std::size_t deck, std::size_t repetitions,
    std::size_t wins, std::size_t losses,
    std::size_t draws) {
    std::size_t remaining_wins = wins;
    std::size_t remaining_losses = losses;
    std::size_t remaining_draws = draws;
    for (std::size_t opponent = 0;
         opponent < old_school::kDeckCount; ++opponent) {
        const std::size_t games =
            (deck == opponent ? 4U : 2U) *
            repetitions;
        const std::size_t cell_wins =
            2U * std::min(
                     remaining_wins / 2U,
                     games / 2U);
        remaining_wins -= cell_wins;
        const std::size_t open = games - cell_wins;
        const std::size_t cell_losses =
            std::min(remaining_losses, open);
        remaining_losses -= cell_losses;
        const std::size_t cell_draws =
            open - cell_losses;
        expect(
            cell_draws <= remaining_draws,
            "synthetic matchup outcome allocation failed");
        remaining_draws -= cell_draws;
        summary.challenger_deck_matchups
            [deck][opponent] = {
            .games = games,
            .wins = cell_wins,
            .losses = cell_losses,
            .draws = cell_draws,
            .on_play_games = games / 2U,
            .on_play_wins = cell_wins / 2U,
            .on_draw_games = games / 2U,
            .on_draw_wins = cell_wins / 2U,
        };
    }
    expect(
        remaining_wins == 0 &&
            remaining_losses == 0 &&
            remaining_draws == 0,
        "synthetic matchup row did not conserve outcomes");
}

old_school::BotBenchmarkSummary summary(
    const gameplay::BenchmarkRequest& request,
    std::size_t challenger_wins_per_quadrant,
    std::size_t challenger_losses_per_quadrant,
    std::size_t draws_per_quadrant = 0) {
    const std::size_t games_per_quadrant =
        challenger_wins_per_quadrant +
        challenger_losses_per_quadrant +
        draws_per_quadrant;
    const std::size_t expected_quadrant =
        3 * request.repetitions;
    expect(
        games_per_quadrant == expected_quadrant,
        "synthetic quadrant width differs from the request");
    const std::size_t games = 20 * games_per_quadrant;
    const std::size_t challenger_wins =
        20 * challenger_wins_per_quadrant;
    const std::size_t challenger_losses =
        20 * challenger_losses_per_quadrant;
    const std::size_t draws = 20 * draws_per_quadrant;

    old_school::BotBenchmarkSummary result{
        .challenger = request.challenger,
        .baseline = request.baseline,
        .evaluation_seed = request.evaluation_seed,
        .learned_training_seed =
            request.game.learned_training_seed,
        .challenger_model_fingerprint =
            request.challenger.kind ==
                    old_school::BotKind::Learned
                ? (request.role == "identical-control"
                       ? std::string(
                             gameplay::
                                 kParentModelFingerprint)
                       : std::string(
                             gameplay::
                                 kCandidateModelFingerprint))
                : std::string{},
        .baseline_model_fingerprint =
            request.baseline.kind ==
                    old_school::BotKind::Learned
                ? std::string(
                      gameplay::kParentModelFingerprint)
                : std::string{},
        .repetitions_per_deck_pairing =
            request.repetitions,
        .total_games = games,
        .challenger_stats = {
            .games = games,
            .wins = challenger_wins,
            .losses = challenger_losses,
            .draws = draws,
        },
        .baseline_stats = {
            .games = games,
            .wins = challenger_losses,
            .losses = challenger_wins,
            .draws = draws,
        },
        .challenger_quartet_cr1 = {
            .clusters = 15 * request.repetitions,
            .records = games,
        },
    };
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        result.challenger_decks[deck] =
            deck_stats(
                challenger_wins_per_quadrant,
                challenger_losses_per_quadrant,
                draws_per_quadrant);
        result.baseline_decks[deck] =
            deck_stats(
                challenger_losses_per_quadrant,
                challenger_wins_per_quadrant,
                draws_per_quadrant);
        for (std::size_t policy_seat = 0;
             policy_seat < 2; ++policy_seat) {
            for (std::size_t play_draw = 0;
                 play_draw < 2; ++play_draw) {
                result.challenger_outcome_quadrants
                    [deck][policy_seat][play_draw] = {
                    .games = games_per_quadrant,
                    .wins =
                        challenger_wins_per_quadrant,
                    .losses =
                        challenger_losses_per_quadrant,
                    .draws = draws_per_quadrant,
                };
                result.baseline_outcome_quadrants
                    [deck][policy_seat][play_draw] = {
                    .games = games_per_quadrant,
                    .wins =
                        challenger_losses_per_quadrant,
                    .losses =
                        challenger_wins_per_quadrant,
                    .draws = draws_per_quadrant,
                };
            }
        }
        populate_matchup_row(
            result, deck, request.repetitions,
            4U * challenger_wins_per_quadrant,
            4U * challenger_losses_per_quadrant,
            4U * draws_per_quadrant);
    }
    return result;
}

struct Harness {
    std::vector<gameplay::BenchmarkRequest> requests;
    double control_seconds = 10.0;
    double candidate_seconds = 12.0;
    std::size_t candidate_wins_per_quadrant = 6;
    std::size_t candidate_losses_per_quadrant = 6;

    gameplay::TimedBenchmark execute(
        const gameplay::BenchmarkRequest& request) {
        requests.push_back(request);
        const bool control =
            request.role == "identical-control";
        return {
            .summary = summary(
                request,
                control
                    ? 6
                    : candidate_wins_per_quadrant,
                control
                    ? 6
                    : candidate_losses_per_quadrant),
            .elapsed_seconds =
                control
                    ? control_seconds
                    : candidate_seconds,
        };
    }
};

gameplay::RunReport run(
    gameplay::Mode mode, Harness& harness,
    gameplay::FixedDeployment fixed = deployment(),
    gameplay::testing::ModelInspector inspect_model = {},
    gameplay::testing::Snapshotter snapshotter = {}) {
    const auto parent = fixed.parent_before;
    const auto candidate = fixed.candidate_artifact_before;
    const auto parent_model = fixed.parent;
    const auto candidate_model = fixed.candidate;
    const gameplay::ModelIdentity parent_identity{
        .fingerprint = fixed.parent_model_fingerprint,
        .components = fixed.parent_components,
    };
    const gameplay::ModelIdentity candidate_identity{
        .fingerprint =
            fixed.candidate_model_fingerprint,
        .components = fixed.candidate_components,
    };
    if (!inspect_model) {
        inspect_model =
            [parent_model, candidate_model,
             parent_identity, candidate_identity](
                std::shared_ptr<
                    const old_school::LearnedModel> model) {
                if (model == parent_model) {
                    return parent_identity;
                }
                if (model == candidate_model) {
                    return candidate_identity;
                }
                throw std::runtime_error(
                    "unexpected synthetic model");
            };
    }
    if (!snapshotter) {
        snapshotter =
            [parent, candidate](const std::string& path) {
                if (path == parent.path) {
                    return parent;
                }
                if (path == candidate.path) {
                    return candidate;
                }
                throw std::runtime_error(
                    "unexpected synthetic snapshot path");
            };
    }
    return gameplay::testing::run_with(
        mode, std::move(fixed),
        [&](const gameplay::BenchmarkRequest& request) {
            return harness.execute(request);
        },
        snapshotter,
        inspect_model);
}

void test_fixed_modes_and_parser() {
    const gameplay::ModeSpec smoke =
        gameplay::mode_spec(gameplay::Mode::Smoke);
    expect(smoke.token == "smoke", "smoke token drifted");
    expect(smoke.evaluation_seed == gameplay::kSmokeSeed,
           "smoke seed drifted");
    expect(smoke.repetitions == 4,
           "smoke repetitions drifted");
    expect(smoke.run_identical_control,
           "smoke lost identical control");
    expect(!smoke.handcrafted_baseline,
           "smoke unexpectedly uses Handcrafted");
    expect(
        gameplay::mode_is_licensed(
            gameplay::Mode::Smoke) &&
            !gameplay::mode_is_licensed(
                gameplay::Mode::MilestoneC16) &&
            !gameplay::mode_is_licensed(
                gameplay::Mode::
                    MilestoneHandcrafted),
        "initial promotion licenses drifted");

    const gameplay::ModeSpec c16 =
        gameplay::mode_spec(
            gameplay::Mode::MilestoneC16);
    expect(c16.evaluation_seed ==
               gameplay::kMilestoneC16Seed &&
               c16.repetitions == 34 &&
               !c16.run_identical_control &&
               !c16.handcrafted_baseline,
           "C16 milestone recipe drifted");
    const gameplay::ModeSpec handcrafted =
        gameplay::mode_spec(
            gameplay::Mode::MilestoneHandcrafted);
    expect(handcrafted.evaluation_seed ==
               gameplay::kMilestoneHandcraftedSeed &&
               handcrafted.repetitions == 34 &&
               handcrafted.handcrafted_baseline,
           "Handcrafted milestone recipe drifted");

    const std::array<std::string_view, 1> smoke_argument{
        "--smoke"};
    expect(
        gameplay::parse_mode(smoke_argument) ==
            gameplay::Mode::Smoke,
        "smoke parser drifted");
    const std::array<std::string_view, 1> c16_argument{
        "--milestone-c16"};
    expect_rejected(
        [&] {
            static_cast<void>(
                gameplay::parse_mode(c16_argument));
        },
        "CLI exposed the unlicensed C16 milestone");
    const std::array<std::string_view, 1> handcrafted_argument{
        "--milestone-handcrafted"};
    expect_rejected(
        [&] {
            static_cast<void>(
                gameplay::parse_mode(
                    handcrafted_argument));
        },
        "CLI exposed the unlicensed Handcrafted milestone");
    const std::array<std::string_view, 0> empty{};
    expect_rejected(
        [&] { static_cast<void>(
            gameplay::parse_mode(empty)); },
        "empty gameplay CLI was accepted");
    const std::array<std::string_view, 1> unknown{
        "--seed"};
    expect_rejected(
        [&] { static_cast<void>(
            gameplay::parse_mode(unknown)); },
        "gameplay accepted a policy knob");
    const std::array<std::string_view, 2> extra{
        "--smoke", "--seed"};
    expect_rejected(
        [&] { static_cast<void>(
            gameplay::parse_mode(extra)); },
        "gameplay accepted an extra argument");
}

void test_cli_rejects_nonmodes_with_usage() {
    char program[] = "old-school-fq4-dev1-gameplay";
    char unexpected[] = "unexpected";
    char smoke[] = "--smoke";
    char seed[] = "--seed";
    char milestone_c16[] = "--milestone-c16";
    char milestone_handcrafted[] =
        "--milestone-handcrafted";

    {
        char* arguments[] = {program, unexpected};
        std::ostringstream output;
        std::ostringstream error;
        expect(
            gameplay::run_cli(
                2, arguments, output, error) == 2 &&
                output.str().empty() &&
                error.str().starts_with("Usage: ") &&
                error.str().find("result=ERROR") ==
                    std::string::npos,
            "unknown mode did not return CLI usage");
    }
    {
        char* arguments[] = {program, smoke, seed};
        std::ostringstream output;
        std::ostringstream error;
        expect(
            gameplay::run_cli(
                3, arguments, output, error) == 2 &&
                output.str().empty() &&
                error.str().starts_with("Usage: "),
            "extra CLI knob did not return usage");
    }
    for (char* locked :
         {milestone_c16, milestone_handcrafted}) {
        char* arguments[] = {program, locked};
        std::ostringstream output;
        std::ostringstream error;
        expect(
            gameplay::run_cli(
                2, arguments, output, error) == 2 &&
                output.str().empty() &&
                error.str() ==
                    "Usage: old-school-fq4-dev1-gameplay "
                    "--smoke\n",
            "CLI exposed an unlicensed milestone");
    }
}

void test_locked_modes_do_not_reach_execution() {
    for (const gameplay::Mode mode :
         {gameplay::Mode::MilestoneC16,
          gameplay::Mode::MilestoneHandcrafted}) {
        std::size_t loads = 0;
        std::size_t benchmarks = 0;
        std::size_t snapshots = 0;
        std::size_t inspections = 0;
        expect_rejected(
            [&] {
                static_cast<void>(
                    gameplay::testing::run_licensed_with(
                        mode,
                        [&]() -> gameplay::FixedDeployment {
                            ++loads;
                            return deployment();
                        },
                        [&](const gameplay::BenchmarkRequest&)
                            -> gameplay::TimedBenchmark {
                            ++benchmarks;
                            return {};
                        },
                        [&](const std::string&)
                            -> integrity::RegularFileSnapshot {
                            ++snapshots;
                            return {};
                        },
                        [&](std::shared_ptr<
                            const old_school::LearnedModel>)
                            -> gameplay::ModelIdentity {
                            ++inspections;
                            return {};
                        }));
            },
            "unlicensed mode reached its execution seam");
        expect(
            loads == 0 && benchmarks == 0 &&
                snapshots == 0 && inspections == 0,
            "unlicensed mode touched a deployment or seed seam");
    }
}

void test_exact_recipe_builders() {
    const gameplay::FixedDeployment fixed = deployment();
    const auto learned =
        gameplay::testing::make_learned_bot(
            fixed.candidate);
    expect(
        learned.kind == old_school::BotKind::Learned &&
            learned.learned_variant ==
                old_school::LearnedVariant::
                    ValueSearchChampion &&
            learned.rollouts_per_action == 8 &&
            learned.exploration_rate == 0.0 &&
            learned.value_continuation_epsilon == 0.0 &&
            learned.value_priority_residual_weight == 0.10 &&
            !learned.value_pass_dominance &&
            learned.value_continuation_controller ==
                old_school::LearnedContinuationController::
                    Legacy &&
            learned.training_games == 800 &&
            learned.learned_model == fixed.candidate,
        "fixed Learned deployment recipe drifted");

    const auto handcrafted =
        gameplay::testing::make_handcrafted_bot();
    expect(
        handcrafted.kind ==
                old_school::BotKind::Handcrafted &&
            handcrafted.rollouts_per_action == 1 &&
            !handcrafted.learned_model,
        "fixed Handcrafted recipe drifted");
    const auto game =
        gameplay::testing::make_game_config();
    expect(
        game.max_turns == 500 &&
            game.learned_training_seed == 424242 &&
            game.learned_search_depth == 1,
        "fixed game recipe drifted");
    expect(
        gameplay::kHorizonTurns ==
            old_school::kLearnedValueSearchHorizonTurns,
        "reported horizon differs from engine deployment");
}

void test_smoke_call_order_and_gate() {
    Harness harness;
    const gameplay::RunReport report =
        run(gameplay::Mode::Smoke, harness);
    expect(harness.requests.size() == 2,
           "smoke did not make exactly two calls");
    expect(
        harness.requests[0].role ==
                "identical-control" &&
            harness.requests[0]
                .allow_identical_policy_control,
        "identical control was not first or permitted");
    expect(
        harness.requests[1].role == "candidate" &&
            !harness.requests[1]
                 .allow_identical_policy_control,
        "candidate call used the control bypass");
    for (const auto& request : harness.requests) {
        expect(
            request.repetitions == 4 &&
                request.evaluation_seed ==
                    gameplay::kSmokeSeed,
            "smoke schedule request drifted");
    }
    expect(
        harness.requests[0].challenger.learned_model ==
                deployment().parent &&
            harness.requests[0].baseline.learned_model ==
                deployment().parent &&
            harness.requests[1].challenger.learned_model ==
                deployment().candidate &&
            harness.requests[1].baseline.learned_model ==
                deployment().parent,
        "smoke model assignment drifted");
    expect(report.gate.infrastructure_valid,
           "valid smoke was not complete");
    expect(report.gate.passed,
           "valid smoke did not pass");
    expect(report.candidate.summary.total_games == 240,
           "smoke total is not 240 games");
    for (const auto& deck :
         report.candidate.summary.challenger_decks) {
        expect(deck.games == 48,
               "smoke deck slice is not 48 games");
    }
}

void test_smoke_rejections_and_infrastructure_failures() {
    Harness passing_harness;
    gameplay::RunReport base =
        run(gameplay::Mode::Smoke, passing_harness);

    gameplay::RunReport low = base;
    low.candidate.summary.challenger_stats = {
        .games = 240,
        .wins = 80,
        .losses = 160,
    };
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        low.candidate.summary.challenger_decks[deck] =
            deck_stats(4, 8, 0);
        low.candidate.summary.baseline_decks[deck] =
            deck_stats(8, 4, 0);
        for (std::size_t seat = 0; seat < 2; ++seat) {
            for (std::size_t draw = 0; draw < 2; ++draw) {
                low.candidate.summary
                    .challenger_outcome_quadrants
                        [deck][seat][draw] = {
                    .games = 12,
                    .wins = 4,
                    .losses = 8,
                };
                low.candidate.summary
                    .baseline_outcome_quadrants
                        [deck][seat][draw] = {
                    .games = 12,
                    .wins = 8,
                    .losses = 4,
                };
            }
        }
        populate_matchup_row(
            low.candidate.summary, deck, 4, 16, 32, 0);
    }
    low.candidate.summary.baseline_stats = {
        .games = 240,
        .wins = 160,
        .losses = 80,
    };
    low.gate = gameplay::testing::evaluate_gate(low);
    expect(
        low.gate.infrastructure_valid &&
            !low.gate.aggregate_floor &&
            !low.gate.passed,
        "sub-40 smoke was not a scientific rejection");

    gameplay::RunReport slow = base;
    slow.candidate.elapsed_seconds = 12.5000001;
    slow.gate = gameplay::testing::evaluate_gate(slow);
    expect(
        slow.gate.infrastructure_valid &&
            !slow.gate.runtime_ratio &&
            !slow.gate.passed,
        "overhead above 25 percent was not rejected");

    gameplay::RunReport control = base;
    control.identical_control->summary
        .challenger_stats.wins = 119;
    control.identical_control->summary
        .challenger_stats.losses = 121;
    control.gate =
        gameplay::testing::evaluate_gate(control);
    expect(
        !control.gate.infrastructure_valid &&
            !control.gate.control_exact,
        "asymmetric identical control was not invalid");

    gameplay::RunReport quadrant = base;
    --quadrant.candidate.summary
          .challenger_outcome_quadrants[0][0][0]
          .games;
    quadrant.gate =
        gameplay::testing::evaluate_gate(quadrant);
    expect(
        !quadrant.gate.infrastructure_valid &&
            !quadrant.gate.accounting_complete,
        "incomplete quadrant was not invalid");

    gameplay::RunReport artifact = base;
    ++artifact.candidate_artifact_after.inode;
    artifact.gate =
        gameplay::testing::evaluate_gate(artifact);
    expect(
        !artifact.gate.infrastructure_valid &&
            !artifact.gate.artifacts_unchanged,
        "artifact mutation was not invalid");

    gameplay::RunReport model = base;
    model.candidate_model_after.components.priority =
        std::string(64, 'f');
    model.gate =
        gameplay::testing::evaluate_gate(model);
    expect(
        !model.gate.infrastructure_valid &&
            !model.gate.models_unchanged,
        "in-memory model mutation was not invalid");

    gameplay::RunReport identity = base;
    identity.deployment.fixed_identity_valid = false;
    identity.gate =
        gameplay::testing::evaluate_gate(identity);
    expect(
        !identity.gate.infrastructure_valid &&
            !identity.gate.fixed_identity,
        "identity failure was not invalid");
}

void test_control_model_mutation_stops_candidate() {
    Harness harness;
    const gameplay::FixedDeployment fixed = deployment();
    const auto parent = fixed.parent;
    const auto candidate = fixed.candidate;
    const gameplay::ModelIdentity parent_identity{
        .fingerprint = fixed.parent_model_fingerprint,
        .components = fixed.parent_components,
    };
    const gameplay::ModelIdentity candidate_identity{
        .fingerprint = fixed.candidate_model_fingerprint,
        .components = fixed.candidate_components,
    };
    expect_rejected(
        [&] {
            static_cast<void>(run(
                gameplay::Mode::Smoke, harness, fixed,
                [parent, candidate, parent_identity,
                 candidate_identity](
                    std::shared_ptr<
                        const old_school::LearnedModel> model) {
                    if (model == parent) {
                        gameplay::ModelIdentity changed =
                            parent_identity;
                        changed.fingerprint =
                            std::string(64, 'e');
                        return changed;
                    }
                    if (model == candidate) {
                        return candidate_identity;
                    }
                    throw std::runtime_error(
                        "unexpected synthetic model");
                }));
        },
        "control model mutation did not stop execution");
    expect(
        harness.requests.size() == 1 &&
            harness.requests.front().role ==
                "identical-control",
        "candidate seed ran after control model mutation");
}

void test_control_artifact_mutation_stops_candidate() {
    Harness harness;
    const gameplay::FixedDeployment fixed = deployment();
    const auto parent = fixed.parent_before;
    const auto candidate = fixed.candidate_artifact_before;
    std::size_t snapshots = 0;
    expect_rejected(
        [&] {
            static_cast<void>(run(
                gameplay::Mode::Smoke, harness, fixed, {},
                [parent, candidate, &snapshots](
                    const std::string& path) {
                    ++snapshots;
                    if (path == parent.path) {
                        return parent;
                    }
                    if (path == candidate.path) {
                        auto changed = candidate;
                        ++changed.inode;
                        return changed;
                    }
                    throw std::runtime_error(
                        "unexpected synthetic snapshot path");
                }));
        },
        "control artifact mutation did not stop execution");
    expect(
        snapshots == 2 &&
            harness.requests.size() == 1 &&
            harness.requests.front().role ==
                "identical-control",
        "candidate seed ran after control artifact mutation");
}

void test_milestone_gates_and_baselines() {
    Harness c16;
    c16.candidate_wins_per_quadrant = 55;
    c16.candidate_losses_per_quadrant = 47;
    gameplay::RunReport passing =
        run(gameplay::Mode::MilestoneC16, c16);
    expect(c16.requests.size() == 1,
           "C16 milestone made an extra call");
    expect(
        c16.requests.front().repetitions == 34 &&
            c16.requests.front().evaluation_seed ==
                gameplay::kMilestoneC16Seed &&
            c16.requests.front().baseline.kind ==
                old_school::BotKind::Learned,
        "C16 milestone request drifted");
    expect(
        passing.candidate.summary.total_games == 2040 &&
            passing.gate.aggregate_majority &&
            passing.gate.aggregate_wilson &&
            passing.gate.every_deck_majority &&
            passing.gate.passed,
        "valid C16 milestone did not pass");

    Harness handcrafted;
    handcrafted.candidate_wins_per_quadrant = 55;
    handcrafted.candidate_losses_per_quadrant = 47;
    gameplay::RunReport hc =
        run(
            gameplay::Mode::MilestoneHandcrafted,
            handcrafted);
    expect(
        handcrafted.requests.size() == 1 &&
            handcrafted.requests.front()
                    .evaluation_seed ==
                gameplay::kMilestoneHandcraftedSeed &&
            handcrafted.requests.front().baseline.kind ==
                old_school::BotKind::Handcrafted &&
            !handcrafted.requests.front()
                 .allow_identical_policy_control &&
            hc.gate.passed,
        "Handcrafted milestone request or gate drifted");

    Harness underpowered;
    underpowered.candidate_wins_per_quadrant = 52;
    underpowered.candidate_losses_per_quadrant = 50;
    gameplay::RunReport wilson =
        run(
            gameplay::Mode::MilestoneC16,
            underpowered);
    expect(
        wilson.gate.infrastructure_valid &&
            wilson.gate.aggregate_majority &&
            !wilson.gate.aggregate_wilson &&
            !wilson.gate.passed,
        "milestone ignored its Wilson gate");

    gameplay::RunReport deck = passing;
    deck.candidate.summary.challenger_decks[0].wins =
        204;
    deck.candidate.summary.challenger_decks[0].losses =
        204;
    deck.candidate.summary.challenger_decks[0]
        .on_play_wins = 102;
    deck.candidate.summary.challenger_decks[0]
        .on_draw_wins = 102;
    deck.candidate.summary.baseline_decks[0].wins =
        204;
    deck.candidate.summary.baseline_decks[0].losses =
        204;
    deck.candidate.summary.baseline_decks[0]
        .on_play_wins = 102;
    deck.candidate.summary.baseline_decks[0]
        .on_draw_wins = 102;
    for (std::size_t seat = 0; seat < 2; ++seat) {
        for (std::size_t draw = 0; draw < 2; ++draw) {
            deck.candidate.summary
                .challenger_outcome_quadrants[0][seat][draw]
                .wins = 51;
            deck.candidate.summary
                .challenger_outcome_quadrants[0][seat][draw]
                .losses = 51;
            deck.candidate.summary
                .baseline_outcome_quadrants[0][seat][draw]
                .wins = 51;
            deck.candidate.summary
                .baseline_outcome_quadrants[0][seat][draw]
                .losses = 51;
        }
    }
    // Keep pooled accounting unchanged by moving the displaced outcomes to
    // another deck.
    deck.candidate.summary.challenger_decks[1].wins =
        236;
    deck.candidate.summary.challenger_decks[1].losses =
        172;
    deck.candidate.summary.challenger_decks[1]
        .on_play_wins = 118;
    deck.candidate.summary.challenger_decks[1]
        .on_draw_wins = 118;
    deck.candidate.summary.baseline_decks[1].wins =
        172;
    deck.candidate.summary.baseline_decks[1].losses =
        236;
    deck.candidate.summary.baseline_decks[1]
        .on_play_wins = 86;
    deck.candidate.summary.baseline_decks[1]
        .on_draw_wins = 86;
    for (std::size_t seat = 0; seat < 2; ++seat) {
        for (std::size_t draw = 0; draw < 2; ++draw) {
            deck.candidate.summary
                .challenger_outcome_quadrants[1][seat][draw]
                .wins = 59;
            deck.candidate.summary
                .challenger_outcome_quadrants[1][seat][draw]
                .losses = 43;
            deck.candidate.summary
                .baseline_outcome_quadrants[1][seat][draw]
                .wins = 43;
            deck.candidate.summary
                .baseline_outcome_quadrants[1][seat][draw]
                .losses = 59;
        }
    }
    populate_matchup_row(
        deck.candidate.summary, 0, 34, 204, 204, 0);
    populate_matchup_row(
        deck.candidate.summary, 1, 34, 236, 172, 0);
    deck.gate = gameplay::testing::evaluate_gate(deck);
    expect(
        deck.gate.infrastructure_valid &&
            !deck.gate.every_deck_majority &&
            !deck.gate.passed,
        "milestone ignored a tied challenger deck");
}

void test_report_is_complete_and_aggregate_only() {
    Harness harness;
    const gameplay::RunReport report =
        run(gameplay::Mode::Smoke, harness);
    const std::string output =
        gameplay::format_report(report);
    expect(
        output.find(
            std::string(
                "model_fingerprint=") +
            std::string(
                gameplay::kCandidateModelFingerprint)) !=
            std::string::npos,
        "report omitted candidate fingerprint");
    expect(
        output.find(
            "priority_residual_weight=0.10000000000000001") !=
            std::string::npos &&
            output.find("worlds_per_action=8") !=
                std::string::npos &&
            output.find("horizon_turns=4") !=
                std::string::npos &&
            output.find(
                "continuation_controller=Legacy") !=
                std::string::npos &&
            output.find("root_search_depth=1") !=
                std::string::npos &&
            output.find("pass_dominance=0") !=
                std::string::npos,
        "report omitted the fixed deployment recipe");
    expect(
        occurrence_count(output, "deck role=") == 10,
        "smoke report omitted a deck row");
    expect(
        occurrence_count(output, "quadrant role=") == 40,
        "smoke report omitted full quadrant accounting");
    expect(
        output.find("result=PASS\n") !=
            std::string::npos,
        "report omitted its verdict");
    expect(
        output.find("PRIVATE") == std::string::npos &&
            output.find("descriptor") ==
                std::string::npos &&
            output.find("hand=") == std::string::npos,
        "report leaked nonaggregate gameplay detail");
}

} // namespace

int main() {
    TestRunner runner;
    runner.run(
        "fixed modes and parser",
        test_fixed_modes_and_parser);
    runner.run(
        "CLI rejects nonmodes with usage",
        test_cli_rejects_nonmodes_with_usage);
    runner.run(
        "locked modes cannot reach execution",
        test_locked_modes_do_not_reach_execution);
    runner.run(
        "exact recipe builders",
        test_exact_recipe_builders);
    runner.run(
        "smoke call order and gate",
        test_smoke_call_order_and_gate);
    runner.run(
        "smoke rejection and infrastructure classes",
        test_smoke_rejections_and_infrastructure_failures);
    runner.run(
        "control model mutation stops candidate",
        test_control_model_mutation_stops_candidate);
    runner.run(
        "control artifact mutation stops candidate",
        test_control_artifact_mutation_stops_candidate);
    runner.run(
        "milestone gates and baselines",
        test_milestone_gates_and_baselines);
    runner.run(
        "complete aggregate report",
        test_report_is_complete_and_aggregate_only);
    return runner.finish();
}
