#include "old_school/fq4_dev5_gameplay.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gameplay = old_school::fq4_dev5_gameplay;
namespace artifact =
    old_school::fq4_dev_candidate_artifact;
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

OpaqueModels& models() {
    static OpaqueModels value;
    return value;
}

integrity::RegularFileSnapshot make_snapshot(
    std::string path, std::uint64_t bytes,
    std::string sha256, std::uint64_t inode) {
    return {
        .path = std::move(path),
        .physical_path =
            "/physical/" + std::to_string(inode),
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

old_school::LearnedModelComponentFingerprints
candidate_components() {
    auto result =
        gameplay::anchored_contract().parent.components;
    result.priority =
        "e279267435b9644d42b66c0b2cb917b8"
        "6b1b8c3fceacae65a4f3cd565ddb6732";
    return result;
}

artifact::Report candidate_report() {
    return {
        .artifact = {
            .bytes = gameplay::kCandidateArtifactBytes,
            .sha256 = std::string(
                gameplay::kCandidateArtifactSha256),
        },
        .manifest = {
            .contract = gameplay::anchored_contract(),
            .candidate_components =
                candidate_components(),
            .tensors = {
                .hidden_count = 32,
                .feature_count = 893,
                .parameter_count = 29'534,
                .parent_sha256 =
                    "4593663a4c2512ca9996d08c64dd28de"
                    "217e430ea691174af2168d11d066e4ee",
                .candidate_sha256 =
                    "77cf99a4ff1f9d460f6b80294f91a01d"
                    "846a3c6515de40a5106e5398e688f769",
                .xor_delta_sha256 =
                    "23dd5dec0944b855fc523ef607aee6fac"
                    "3d5ae3a59b866f7aa40f446efa8b297",
            },
        },
    };
}

gameplay::FixedDeployment deployment() {
    return {
        .parent = models().parent,
        .candidate = models().candidate,
        .parent_model_fingerprint =
            std::string(gameplay::kParentModelFingerprint),
        .candidate_model_fingerprint =
            std::string(
                gameplay::kCandidateModelFingerprint),
        .parent_components =
            gameplay::anchored_contract()
                .parent.components,
        .candidate_components =
            candidate_components(),
        .parent_before = make_snapshot(
            "/artifacts/c16.bin",
            gameplay::kParentArtifactBytes,
            std::string(
                gameplay::kParentArtifactSha256),
            10),
        .candidate_artifact_before = make_snapshot(
            "/artifacts/fq4-dev5.bin",
            gameplay::kCandidateArtifactBytes,
            std::string(
                gameplay::kCandidateArtifactSha256),
            20),
        .candidate_report = candidate_report(),
        .fixed_identity_valid = true,
    };
}

void add_deck_stats(
    old_school::DeckSimulationStats& total,
    const old_school::DeckSimulationStats& value) {
    total.games += value.games;
    total.wins += value.wins;
    total.losses += value.losses;
    total.draws += value.draws;
    total.on_play_games += value.on_play_games;
    total.on_play_wins += value.on_play_wins;
    total.on_draw_games += value.on_draw_games;
    total.on_draw_wins += value.on_draw_wins;
}

void populate_quadrants(
    old_school::BotBenchmarkOutcomeQuadrants& quadrants,
    std::size_t deck,
    const old_school::DeckSimulationStats& stats) {
    constexpr std::size_t kGamesPerQuadrant = 12;
    for (std::size_t play_draw = 0;
         play_draw < 2; ++play_draw) {
        std::size_t remaining_wins =
            play_draw == 0
                ? stats.on_play_wins
                : stats.on_draw_wins;
        std::size_t remaining_draws =
            stats.draws / 2;
        for (std::size_t seat = 0; seat < 2; ++seat) {
            const std::size_t wins =
                std::min(remaining_wins,
                         kGamesPerQuadrant);
            remaining_wins -= wins;
            const std::size_t draws =
                std::min(
                    remaining_draws,
                    kGamesPerQuadrant - wins);
            remaining_draws -= draws;
            quadrants[deck][seat][play_draw] = {
                .games = kGamesPerQuadrant,
                .wins = wins,
                .losses =
                    kGamesPerQuadrant - wins - draws,
                .draws = draws,
            };
        }
        expect(
            remaining_wins == 0 &&
                remaining_draws == 0,
            "quadrant allocation did not conserve outcomes");
    }
}

old_school::BotBenchmarkSummary make_summary(
    const gameplay::BenchmarkRequest& request,
    const std::array<std::size_t,
                     old_school::kDeckCount>&
        challenger_wins_by_deck) {
    old_school::BotBenchmarkSummary result{
        .challenger = request.challenger,
        .baseline = request.baseline,
        .evaluation_seed = request.evaluation_seed,
        .learned_training_seed =
            request.game.learned_training_seed,
        .challenger_model_fingerprint =
            request.role == "identical-control"
                ? std::string(
                      gameplay::kParentModelFingerprint)
                : std::string(
                      gameplay::kCandidateModelFingerprint),
        .baseline_model_fingerprint =
            std::string(gameplay::kParentModelFingerprint),
        .repetitions_per_deck_pairing =
            request.repetitions,
        .total_games = gameplay::kSmokeGames,
        .challenger_quartet_cr1 = {
            .clusters = 15 *
                gameplay::kSmokeRepetitions,
            .records = gameplay::kSmokeGames,
        },
        .total_turns = 7 * gameplay::kSmokeGames,
        .life_total_finishes =
            gameplay::kSmokeGames,
    };

    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        std::size_t remaining_wins =
            challenger_wins_by_deck[deck];
        expect(
            remaining_wins <=
                    gameplay::kSmokeGamesPerDeck &&
                remaining_wins % 2 == 0,
            "synthetic deck wins must be even and bounded");
        for (std::size_t opponent = 0;
             opponent < old_school::kDeckCount;
             ++opponent) {
            const std::size_t games =
                (deck == opponent ? 4U : 2U) *
                gameplay::kSmokeRepetitions;
            const std::size_t wins =
                2U * std::min(
                         remaining_wins / 2U,
                         games / 2U);
            remaining_wins -= wins;
            result.challenger_deck_matchups
                [deck][opponent] = {
                .games = games,
                .wins = wins,
                .losses = games - wins,
                .on_play_games = games / 2,
                .on_play_wins = wins / 2,
                .on_draw_games = games / 2,
                .on_draw_wins = wins / 2,
            };
            add_deck_stats(
                result.challenger_decks[deck],
                result.challenger_deck_matchups
                    [deck][opponent]);
        }
        expect(remaining_wins == 0,
               "synthetic matchup row lost wins");
    }

    for (std::size_t baseline_deck = 0;
         baseline_deck < old_school::kDeckCount;
         ++baseline_deck) {
        auto& baseline =
            result.baseline_decks[baseline_deck];
        for (std::size_t challenger_deck = 0;
             challenger_deck < old_school::kDeckCount;
             ++challenger_deck) {
            const auto& cell =
                result.challenger_deck_matchups
                    [challenger_deck][baseline_deck];
            baseline.games += cell.games;
            baseline.wins += cell.losses;
            baseline.losses += cell.wins;
            baseline.draws += cell.draws;
            baseline.on_play_games +=
                cell.on_draw_games;
            baseline.on_play_wins +=
                cell.on_draw_games -
                cell.on_draw_wins;
            baseline.on_draw_games +=
                cell.on_play_games;
            baseline.on_draw_wins +=
                cell.on_play_games -
                cell.on_play_wins;
        }
    }

    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        populate_quadrants(
            result.challenger_outcome_quadrants,
            deck, result.challenger_decks[deck]);
        populate_quadrants(
            result.baseline_outcome_quadrants,
            deck, result.baseline_decks[deck]);
        result.challenger_stats.wins +=
            result.challenger_decks[deck].wins;
        result.challenger_stats.losses +=
            result.challenger_decks[deck].losses;
        result.challenger_stats.draws +=
            result.challenger_decks[deck].draws;
        result.baseline_stats.wins +=
            result.baseline_decks[deck].wins;
        result.baseline_stats.losses +=
            result.baseline_decks[deck].losses;
        result.baseline_stats.draws +=
            result.baseline_decks[deck].draws;
    }
    result.challenger_stats.games =
        gameplay::kSmokeGames;
    result.baseline_stats.games =
        gameplay::kSmokeGames;
    result.challenger_stats.total_decisions =
        3 * gameplay::kSmokeGames;
    result.challenger_stats.total_rollouts =
        8 * result.challenger_stats.total_decisions;
    result.baseline_stats.total_decisions =
        2 * gameplay::kSmokeGames;
    result.baseline_stats.total_rollouts =
        8 * result.baseline_stats.total_decisions;
    return result;
}

struct Harness {
    std::vector<gameplay::BenchmarkRequest> requests;
    std::array<std::size_t, old_school::kDeckCount>
        candidate_wins{24, 24, 24, 24, 24};
    double control_seconds = 10.0;
    double candidate_seconds = 12.0;

    gameplay::TimedBenchmark execute(
        const gameplay::BenchmarkRequest& request) {
        requests.push_back(request);
        const bool control =
            request.role == "identical-control";
        return {
            .summary = make_summary(
                request,
                control
                    ? std::array<std::size_t,
                                 old_school::kDeckCount>{
                          24, 24, 24, 24, 24}
                    : candidate_wins),
            .elapsed_seconds =
                control
                    ? control_seconds
                    : candidate_seconds,
        };
    }
};

gameplay::ModelIdentity inspect(
    std::shared_ptr<const old_school::LearnedModel> model) {
    if (model == models().parent) {
        return {
            .fingerprint = std::string(
                gameplay::kParentModelFingerprint),
            .components =
                gameplay::anchored_contract()
                    .parent.components,
        };
    }
    if (model == models().candidate) {
        return {
            .fingerprint = std::string(
                gameplay::kCandidateModelFingerprint),
            .components = candidate_components(),
        };
    }
    throw std::runtime_error(
        "unexpected synthetic model");
}

gameplay::RunReport run(
    Harness& harness,
    gameplay::FixedDeployment fixed = deployment(),
    gameplay::testing::Snapshotter snapshotter = {},
    gameplay::testing::ModelInspector inspector = {}) {
    const auto parent = fixed.parent_before;
    const auto candidate =
        fixed.candidate_artifact_before;
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
                    "unexpected snapshot path");
            };
    }
    if (!inspector) {
        inspector = inspect;
    }
    return gameplay::testing::run_with(
        std::move(fixed),
        [&](const gameplay::BenchmarkRequest& request) {
            return harness.execute(request);
        },
        std::move(snapshotter),
        std::move(inspector));
}

void test_exact_contract_and_mutations() {
    const artifact::Contract exact =
        gameplay::anchored_contract();
    gameplay::testing::validate_anchored_contract(exact);
    expect(
        exact.family ==
                "FQ4-DEV5-NEUTRAL-ANCHORED" &&
            exact.environment.size() == 238 &&
            exact.parent.artifact_bytes ==
                gameplay::kParentArtifactBytes &&
            exact.corpus.artifact_bytes == 2'250'909 &&
            exact.fit.examples == 248 &&
            exact.fit.options == 987 &&
            exact.fit.check_examples == 0 &&
            exact.fit.background_only_examples == 0 &&
            exact.candidate_model_fingerprint ==
                gameplay::kCandidateModelFingerprint &&
            exact.deployment.worlds_per_action == 8 &&
            exact.deployment.horizon_turns == 4 &&
            exact.deployment.rollouts_per_world == 1 &&
            exact.deployment.root_search_depth == 1 &&
            exact.deployment.shallow_prior &&
            exact.deployment.priority_residual_weight ==
                0.10 &&
            !exact.deployment.pass_dominance &&
            exact.deployment.continuation_controller ==
                old_school::
                    LearnedContinuationController::Legacy &&
            exact.deployment.max_turns == 500,
        "anchored contract literal drifted");

    const auto rejected =
        [](artifact::Contract changed) {
            expect_rejected(
                [&] {
                    gameplay::testing::
                        validate_anchored_contract(changed);
                },
                "mutated anchored contract was accepted");
        };
    artifact::Contract changed = exact;
    changed.family.push_back('x');
    rejected(changed);
    changed = exact;
    changed.corpus.artifact_sha256[0] = 'f';
    rejected(changed);
    changed = exact;
    ++changed.fit.options;
    rejected(changed);
    changed = exact;
    changed.fit.optimizer.seed ^= 1U;
    rejected(changed);
    changed = exact;
    changed.candidate_model_fingerprint[0] = 'f';
    rejected(changed);
    changed = exact;
    changed.deployment.root_exploration = -0.0;
    rejected(changed);
}

void test_loader_uses_exact_identities() {
    std::vector<std::string> events;
    const auto parent_snapshot = make_snapshot(
        std::string(gameplay::kParentArtifactPath),
        gameplay::kParentArtifactBytes,
        std::string(gameplay::kParentArtifactSha256),
        11);
    const auto candidate_snapshot = make_snapshot(
        std::string(gameplay::kCandidateArtifactPath),
        gameplay::kCandidateArtifactBytes,
        std::string(gameplay::kCandidateArtifactSha256),
        12);
    const gameplay::FixedDeployment loaded =
        gameplay::testing::load_fixed_deployment_with({
            .snapshot =
                [&](const std::filesystem::path& path) {
                    events.push_back(
                        "snapshot:" + path.string());
                    if (path ==
                        std::filesystem::path(
                            gameplay::
                                kParentArtifactPath)) {
                        return parent_snapshot;
                    }
                    if (path ==
                        std::filesystem::path(
                            gameplay::
                                kCandidateArtifactPath)) {
                        return candidate_snapshot;
                    }
                    throw std::runtime_error(
                        "wrong loader path");
                },
            .load_parent =
                [&](const std::filesystem::path& path) {
                    events.push_back(
                        "parent:" + path.string());
                    expect(
                        path ==
                            std::filesystem::path(
                                gameplay::
                                    kParentArtifactPath),
                        "loader used the wrong parent path");
                    return gameplay::testing::ParentArtifact{
                        .model = models().parent,
                        .training_games =
                            gameplay::kTrainingGames,
                        .training_seed =
                            gameplay::kTrainingSeed,
                        .generations =
                            gameplay::kParentGenerations,
                    };
                },
            .load_candidate =
                [&](const std::filesystem::path& path,
                    std::shared_ptr<
                        const old_school::LearnedModel>
                        parent,
                    const artifact::Contract& contract,
                    const artifact::FileIdentity& identity) {
                    events.push_back(
                        "candidate:" + path.string());
                    expect(
                        path ==
                                std::filesystem::path(
                                    gameplay::
                                        kCandidateArtifactPath) &&
                            parent == models().parent &&
                            contract ==
                                gameplay::
                                    anchored_contract() &&
                            identity ==
                                artifact::FileIdentity{
                                    .bytes =
                                        gameplay::
                                            kCandidateArtifactBytes,
                                    .sha256 =
                                        std::string(
                                            gameplay::
                                                kCandidateArtifactSha256),
                                },
                        "loader did not pass the exact candidate coordinates");
                    return gameplay::testing::LoadedCandidate{
                        .model = models().candidate,
                        .report = candidate_report(),
                    };
                },
            .inspect_model =
                [&](std::shared_ptr<
                    const old_school::LearnedModel> model) {
                    events.push_back(
                        model == models().parent
                            ? "inspect:parent"
                            : "inspect:candidate");
                    return inspect(std::move(model));
                },
        });
    expect(
        loaded.fixed_identity_valid &&
            loaded.parent == models().parent &&
            loaded.candidate == models().candidate &&
            loaded.candidate_report ==
                candidate_report(),
        "exact loader did not return the fixed deployment");
    const std::vector<std::string> expected{
        "snapshot:" +
            std::string(gameplay::kParentArtifactPath),
        "snapshot:" +
            std::string(gameplay::kCandidateArtifactPath),
        "parent:" +
            std::string(gameplay::kParentArtifactPath),
        "inspect:parent",
        "candidate:" +
            std::string(gameplay::kCandidateArtifactPath),
        "inspect:candidate",
        "snapshot:" +
            std::string(gameplay::kParentArtifactPath),
        "snapshot:" +
            std::string(gameplay::kCandidateArtifactPath),
    };
    expect(events == expected,
           "fixed loader call order drifted");
}

void test_loader_mutations_fail_closed() {
    const auto parent_snapshot = make_snapshot(
        std::string(gameplay::kParentArtifactPath),
        gameplay::kParentArtifactBytes,
        std::string(gameplay::kParentArtifactSha256),
        11);
    const auto candidate_snapshot = make_snapshot(
        std::string(gameplay::kCandidateArtifactPath),
        gameplay::kCandidateArtifactBytes,
        std::string(gameplay::kCandidateArtifactSha256),
        12);
    const auto attempt =
        [&](bool mutate_report,
            bool mutate_after_load) {
            std::size_t parent_snapshots = 0;
            std::size_t candidate_snapshots = 0;
            return gameplay::testing::
                load_fixed_deployment_with({
                    .snapshot =
                        [&](const std::filesystem::path&
                                path) {
                            if (path ==
                                std::filesystem::path(
                                    gameplay::
                                        kParentArtifactPath)) {
                                ++parent_snapshots;
                                auto value =
                                    parent_snapshot;
                                if (mutate_after_load &&
                                    parent_snapshots == 2) {
                                    ++value.inode;
                                }
                                return value;
                            }
                            ++candidate_snapshots;
                            return candidate_snapshot;
                        },
                    .load_parent =
                        [](const std::filesystem::path&) {
                            return gameplay::testing::
                                ParentArtifact{
                                    .model =
                                        models().parent,
                                    .training_games =
                                        gameplay::
                                            kTrainingGames,
                                    .training_seed =
                                        gameplay::
                                            kTrainingSeed,
                                    .generations =
                                        gameplay::
                                            kParentGenerations,
                                };
                        },
                    .load_candidate =
                        [mutate_report](
                            const std::filesystem::path&,
                            std::shared_ptr<
                                const old_school::
                                    LearnedModel>,
                            const artifact::Contract&,
                            const artifact::
                                FileIdentity&) {
                            auto report =
                                candidate_report();
                            if (mutate_report) {
                                report.manifest.tensors
                                    .xor_delta_sha256[0] =
                                    'f';
                            }
                            return gameplay::testing::
                                LoadedCandidate{
                                    .model =
                                        models().candidate,
                                    .report =
                                        std::move(report),
                                };
                        },
                    .inspect_model = inspect,
                });
        };
    expect_rejected(
        [&] {
            static_cast<void>(attempt(true, false));
        },
        "mutated candidate manifest was accepted");
    expect_rejected(
        [&] {
            static_cast<void>(attempt(false, true));
        },
        "artifact mutation during load was accepted");
}

void test_fixed_recipe_and_call_order() {
    const auto bot = gameplay::testing::make_learned_bot(
        models().candidate);
    const auto game =
        gameplay::testing::make_game_config();
    expect(
        bot.kind == old_school::BotKind::Learned &&
            bot.learned_variant ==
                old_school::LearnedVariant::
                    ValueSearchChampion &&
            bot.rollouts_per_action == 8 &&
            bot.exploration_rate == 0.0 &&
            bot.value_continuation_epsilon == 0.0 &&
            bot.value_priority_residual_weight == 0.10 &&
            !bot.value_pass_dominance &&
            !bot.value_adversarial_blocks &&
            bot.value_continuation_controller ==
                old_school::
                    LearnedContinuationController::Legacy &&
            bot.training_games == 800 &&
            bot.learned_model == models().candidate &&
            game.max_turns == 500 &&
            game.learned_training_seed == 424242 &&
            game.learned_search_depth == 1,
        "fixed Learned deployment recipe drifted");

    Harness harness;
    const gameplay::RunReport report = run(harness);
    expect(
        harness.requests.size() == 2 &&
            harness.requests[0].role ==
                "identical-control" &&
            harness.requests[1].role == "candidate",
        "smoke did not run control then candidate");
    const auto& control = harness.requests[0];
    const auto& candidate = harness.requests[1];
    expect(
        control.evaluation_seed ==
                gameplay::kSmokeSeed &&
            candidate.evaluation_seed ==
                gameplay::kSmokeSeed &&
            control.repetitions == 4 &&
            candidate.repetitions == 4 &&
            control.allow_identical_policy_control &&
            !candidate.allow_identical_policy_control &&
            control.challenger.learned_model ==
                models().parent &&
            control.baseline.learned_model ==
                models().parent &&
            candidate.challenger.learned_model ==
                models().candidate &&
            candidate.baseline.learned_model ==
                models().parent,
        "smoke request contract drifted");
    expect(
        report.identical_control.summary.total_games ==
                240 &&
            report.candidate.summary.total_games == 240 &&
            report.gate.infrastructure_valid &&
            report.gate.passed,
        "valid fixed smoke did not pass");
    for (const auto& stats :
         report.candidate.summary.challenger_decks) {
        expect(stats.games == 48,
               "challenger deck is not 48 games");
    }
    for (const auto& stats :
         report.candidate.summary.baseline_decks) {
        expect(stats.games == 48,
               "baseline deck is not 48 games");
    }
}

void test_all_five_deck_accounting_and_mutations() {
    Harness harness;
    harness.candidate_wins = {20, 22, 24, 26, 28};
    const gameplay::RunReport report = run(harness);
    const auto& summary = report.candidate.summary;
    const auto expected_challenger =
        gameplay::testing::make_learned_bot(
            models().candidate);
    const auto expected_baseline =
        gameplay::testing::make_learned_bot(
            models().parent);
    bool same_index_is_not_complementary = false;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        same_index_is_not_complementary =
            same_index_is_not_complementary ||
            summary.challenger_decks[deck].wins !=
                summary.baseline_decks[deck].losses ||
            summary.challenger_decks[deck].losses !=
                summary.baseline_decks[deck].wins;
        expect(
            summary.challenger_decks[deck].games ==
                    48 &&
                summary.baseline_decks[deck].games ==
                    48,
            "five-deck accounting lost a deck");
        for (std::size_t seat = 0; seat < 2; ++seat) {
            for (std::size_t play_draw = 0;
                 play_draw < 2; ++play_draw) {
                expect(
                    summary
                            .challenger_outcome_quadrants
                                [deck][seat][play_draw]
                            .games ==
                        12,
                    "challenger quadrant is not 12 games");
                expect(
                    summary
                            .baseline_outcome_quadrants
                                [deck][seat][play_draw]
                            .games ==
                        12,
                    "baseline quadrant is not 12 games");
            }
        }
        for (std::size_t opponent = 0;
             opponent < old_school::kDeckCount;
             ++opponent) {
            expect(
                summary.challenger_deck_matchups
                        [deck][opponent]
                        .games ==
                    (deck == opponent ? 16U : 8U),
                "matchup schedule width drifted");
        }
    }
    expect(
        same_index_is_not_complementary,
        "unequal-policy fixture did not exercise the DEV1 accounting bug");
    expect(
        gameplay::testing::benchmark_accounting_exact(
            summary, expected_challenger,
            expected_baseline,
            gameplay::kCandidateModelFingerprint,
            gameplay::kParentModelFingerprint),
        "valid unequal-policy accounting was rejected");
    auto policy_drift = summary;
    policy_drift.challenger.value_adversarial_blocks = true;
    expect(
        !gameplay::testing::benchmark_accounting_exact(
            policy_drift, expected_challenger,
            expected_baseline,
            gameplay::kCandidateModelFingerprint,
            gameplay::kParentModelFingerprint),
        "adversarial-block policy drift was accepted");
    policy_drift = summary;
    policy_drift.challenger.value_resolved_shallow_prior_weight =
        1.0;
    expect(
        !gameplay::testing::benchmark_accounting_exact(
            policy_drift, expected_challenger,
            expected_baseline,
            gameplay::kCandidateModelFingerprint,
            gameplay::kParentModelFingerprint),
        "resolved-prior policy drift was accepted");

    auto broken_row = summary;
    bool row_swapped = false;
    for (std::size_t column = 0;
         column < old_school::kDeckCount &&
         !row_swapped;
         ++column) {
        for (std::size_t first = 0;
             first < old_school::kDeckCount &&
             !row_swapped;
             ++first) {
            for (std::size_t second = first + 1;
                 second < old_school::kDeckCount;
                 ++second) {
                auto& left =
                    broken_row.challenger_deck_matchups
                        [first][column];
                auto& right =
                    broken_row.challenger_deck_matchups
                        [second][column];
                if (left.games == right.games &&
                    (left.wins != right.wins ||
                     left.losses != right.losses)) {
                    std::swap(left, right);
                    row_swapped = true;
                    break;
                }
            }
        }
    }
    expect(
        row_swapped &&
            !gameplay::testing::
                benchmark_accounting_exact(
                    broken_row, expected_challenger,
                    expected_baseline,
                    gameplay::kCandidateModelFingerprint,
                    gameplay::kParentModelFingerprint),
        "row mutation preserving a reciprocal column was accepted");

    auto broken_column = summary;
    bool column_swapped = false;
    for (std::size_t row = 0;
         row < old_school::kDeckCount &&
         !column_swapped;
         ++row) {
        for (std::size_t first = 0;
             first < old_school::kDeckCount &&
             !column_swapped;
             ++first) {
            for (std::size_t second = first + 1;
                 second < old_school::kDeckCount;
                 ++second) {
                auto& left =
                    broken_column
                        .challenger_deck_matchups
                            [row][first];
                auto& right =
                    broken_column
                        .challenger_deck_matchups
                            [row][second];
                if (left.games == right.games &&
                    (left.wins != right.wins ||
                     left.losses != right.losses)) {
                    std::swap(left, right);
                    column_swapped = true;
                    break;
                }
            }
        }
    }
    expect(
        column_swapped &&
            !gameplay::testing::
                benchmark_accounting_exact(
                    broken_column,
                    expected_challenger,
                    expected_baseline,
                    gameplay::kCandidateModelFingerprint,
                    gameplay::kParentModelFingerprint),
        "column mutation preserving a challenger row was accepted");

    auto deck_missing = summary;
    --deck_missing.challenger_decks[4].games;
    expect(
        !gameplay::testing::benchmark_accounting_exact(
            deck_missing, expected_challenger,
            expected_baseline,
            gameplay::kCandidateModelFingerprint,
            gameplay::kParentModelFingerprint),
        "missing RU Aggro game was accepted");
    auto quadrant = summary;
    --quadrant.challenger_outcome_quadrants
          [3][1][1]
          .games;
    expect(
        !gameplay::testing::benchmark_accounting_exact(
            quadrant, expected_challenger,
            expected_baseline,
            gameplay::kCandidateModelFingerprint,
            gameplay::kParentModelFingerprint),
        "wrong White quadrant width was accepted");
    auto seed = summary;
    ++seed.evaluation_seed;
    expect(
        !gameplay::testing::benchmark_accounting_exact(
            seed, expected_challenger,
            expected_baseline,
            gameplay::kCandidateModelFingerprint,
            gameplay::kParentModelFingerprint),
        "evaluation-seed drift was accepted");
    auto fingerprint = summary;
    fingerprint.challenger_model_fingerprint[0] =
        'f';
    expect(
        !gameplay::testing::benchmark_accounting_exact(
            fingerprint, expected_challenger,
            expected_baseline,
            gameplay::kCandidateModelFingerprint,
            gameplay::kParentModelFingerprint),
        "fingerprint drift was accepted");
}

void test_gate_boundaries() {
    Harness exact;
    exact.candidate_wins = {20, 20, 20, 18, 18};
    exact.control_seconds = 10.0;
    exact.candidate_seconds = 12.5;
    gameplay::RunReport boundary = run(exact);
    expect(
        boundary.candidate.summary.challenger_stats.wins ==
                96 &&
            boundary.candidate.summary.challenger_stats.losses ==
                144 &&
            boundary.candidate.summary
                    .challenger_win_rate() ==
                40.0 &&
            boundary.gate.aggregate_floor &&
            boundary.gate.runtime_ratio &&
            boundary.gate.passed,
        "exact smoke boundaries did not pass");

    Harness below;
    below.candidate_wins = {20, 20, 18, 18, 18};
    gameplay::RunReport low = run(below);
    expect(
        low.candidate.summary.challenger_stats.wins ==
                94 &&
            low.gate.infrastructure_valid &&
            !low.gate.aggregate_floor &&
            !low.gate.passed,
        "sub-40 smoke was not rejected");

    gameplay::RunReport slow = boundary;
    slow.candidate.elapsed_seconds =
        std::nextafter(
            12.5,
            std::numeric_limits<double>::infinity());
    slow.gate =
        gameplay::testing::evaluate_gate(slow);
    expect(
        slow.gate.infrastructure_valid &&
            !slow.gate.runtime_ratio &&
            !slow.gate.passed,
        "runtime epsilon above 1.25 was accepted");

    for (const double invalid :
         {0.0,
          std::numeric_limits<double>::quiet_NaN(),
          std::numeric_limits<double>::infinity()}) {
        gameplay::RunReport timing = boundary;
        timing.candidate.elapsed_seconds = invalid;
        timing.gate =
            gameplay::testing::evaluate_gate(timing);
        expect(
            timing.gate.infrastructure_valid &&
                !timing.gate.runtime_ratio &&
                !timing.gate.passed,
            "invalid candidate timing was accepted");
        timing = boundary;
        timing.identical_control.elapsed_seconds =
            invalid;
        timing.gate =
            gameplay::testing::evaluate_gate(timing);
        expect(
            timing.gate.infrastructure_valid &&
                !timing.gate.runtime_ratio &&
                !timing.gate.passed,
            "invalid control timing was accepted");
    }
}

void test_mutation_fail_closed_and_stops_candidate() {
    {
        Harness harness;
        const auto fixed = deployment();
        expect_rejected(
            [&] {
                static_cast<void>(
                    gameplay::testing::run_with(
                        fixed,
                        [&](const gameplay::BenchmarkRequest&
                                request) {
                            gameplay::TimedBenchmark timed =
                                harness.execute(request);
                            if (request.role ==
                                "identical-control") {
                                timed.summary.challenger_stats
                                    .wins = 119;
                                timed.summary.challenger_stats
                                    .losses = 121;
                                timed.summary.baseline_stats
                                    .wins = 121;
                                timed.summary.baseline_stats
                                    .losses = 119;
                            }
                            return timed;
                        },
                        [fixed](const std::string& path) {
                            return path ==
                                           fixed.parent_before
                                               .path
                                       ? fixed.parent_before
                                       : fixed
                                             .candidate_artifact_before;
                        },
                        inspect));
            },
            "119-121 control did not stop execution");
        expect(
            harness.requests.size() == 1 &&
                harness.requests.front().role ==
                    "identical-control",
            "candidate ran after invalid control score");
    }
    {
        Harness harness;
        auto malformed = deployment();
        malformed.candidate_report.manifest.tensors
            .candidate_sha256[0] = 'f';
        expect_rejected(
            [&] {
                static_cast<void>(
                    run(harness, malformed));
            },
            "malformed deployment reached gameplay");
        expect(
            harness.requests.empty(),
            "malformed deployment opened the control seed");
    }
    {
        Harness harness;
        std::size_t inspections = 0;
        expect_rejected(
            [&] {
                static_cast<void>(run(
                    harness, deployment(), {},
                    [&](std::shared_ptr<
                        const old_school::LearnedModel>
                            model) {
                        ++inspections;
                        auto identity = inspect(model);
                        if (inspections == 1) {
                            identity.fingerprint[0] =
                                'f';
                        }
                        return identity;
                    }));
            },
            "control model mutation did not stop execution");
        expect(
            harness.requests.size() == 1 &&
                harness.requests.front().role ==
                    "identical-control",
            "candidate ran after control model mutation");
    }
    {
        Harness harness;
        const auto fixed = deployment();
        std::size_t candidate_snapshots = 0;
        expect_rejected(
            [&] {
                static_cast<void>(run(
                    harness, fixed,
                    [&](const std::string& path) {
                        if (path ==
                            fixed.parent_before.path) {
                            return fixed.parent_before;
                        }
                        auto snapshot =
                            fixed.candidate_artifact_before;
                        ++candidate_snapshots;
                        if (candidate_snapshots == 1) {
                            ++snapshot.inode;
                        }
                        return snapshot;
                    }));
            },
            "control artifact mutation did not stop execution");
        expect(
            harness.requests.size() == 1,
            "candidate ran after control artifact mutation");
    }
    {
        Harness harness;
        const auto fixed = deployment();
        std::size_t candidate_snapshots = 0;
        gameplay::RunReport report = run(
            harness, fixed,
            [&](const std::string& path) {
                if (path == fixed.parent_before.path) {
                    return fixed.parent_before;
                }
                auto snapshot =
                    fixed.candidate_artifact_before;
                ++candidate_snapshots;
                if (candidate_snapshots == 2) {
                    ++snapshot.inode;
                }
                return snapshot;
            });
        expect(
            harness.requests.size() == 2 &&
                !report.gate.artifacts_unchanged &&
                !report.gate.infrastructure_valid &&
                !report.gate.passed,
            "post-candidate artifact mutation did not fail closed");
    }
    {
        Harness harness;
        gameplay::RunReport report = run(harness);
        --report.candidate.summary
              .challenger_outcome_quadrants[0][0][0]
              .games;
        report.gate =
            gameplay::testing::evaluate_gate(report);
        expect(
            !report.gate.accounting_complete &&
                !report.gate.infrastructure_valid &&
                !report.gate.passed,
            "incomplete candidate accounting did not fail closed");
    }
}

void test_cli_contract_status_and_redaction() {
    char program[] =
        "old-school-fq4-dev5-gameplay";
    char unexpected[] = "unexpected";
    char smoke[] = "--smoke";
    std::size_t calls = 0;
    {
        char* arguments[] = {program, unexpected};
        std::ostringstream output;
        std::ostringstream error;
        const int status =
            gameplay::testing::run_cli(
                2, arguments, output, error,
                [&]() -> gameplay::RunReport {
                    ++calls;
                    throw std::runtime_error(
                        "PRIVATE opponent hand");
                });
        expect(
            status == 2 && calls == 0 &&
                output.str().empty() &&
                error.str() ==
                    "Usage: "
                    "old-school-fq4-dev5-gameplay "
                    "--smoke\n",
            "arbitrary CLI argument reached execution");
    }
    {
        char* arguments[] = {program, smoke};
        std::ostringstream output;
        std::ostringstream error;
        const int status =
            gameplay::testing::run_cli(
                2, arguments, output, error,
                [&]() -> gameplay::RunReport {
                    ++calls;
                    throw std::runtime_error(
                        "PRIVATE opponent hand");
                });
        expect(
            status == 2 && calls == 1 &&
                output.str().empty() &&
                error.str() ==
                    "result=ERROR"
                    " reason="
                    "fixed_fq4_dev5_gameplay_failed\n" &&
                error.str().find("PRIVATE") ==
                    std::string::npos,
            "CLI exception was not redacted");
    }

    Harness harness;
    gameplay::RunReport passing = run(harness);
    {
        char* arguments[] = {program, smoke};
        std::ostringstream output;
        std::ostringstream error;
        expect(
            gameplay::testing::run_cli(
                2, arguments, output, error,
                [passing] { return passing; }) == 0 &&
                output.str().ends_with("result=PASS\n") &&
                error.str().empty(),
            "synthetic passing CLI did not return zero");
    }
    {
        gameplay::RunReport rejected = passing;
        rejected.gate.aggregate_floor = false;
        rejected.gate.passed = false;
        char* arguments[] = {program, smoke};
        std::ostringstream output;
        std::ostringstream error;
        expect(
            gameplay::testing::run_cli(
                2, arguments, output, error,
                [rejected] { return rejected; }) == 1 &&
                output.str().ends_with(
                    "result=REJECT\n") &&
                error.str().empty(),
            "synthetic scientific rejection did not return one");
    }
    {
        gameplay::RunReport invalid = passing;
        invalid.gate.infrastructure_valid = false;
        invalid.gate.passed = false;
        char* arguments[] = {program, smoke};
        std::ostringstream output;
        std::ostringstream error;
        expect(
            gameplay::testing::run_cli(
                2, arguments, output, error,
                [invalid] { return invalid; }) == 2 &&
                output.str().ends_with("result=ERROR\n") &&
                error.str().empty(),
            "synthetic infrastructure failure did not return two");
    }
}

void test_report_is_complete_and_aggregate_only() {
    Harness harness;
    const gameplay::RunReport report = run(harness);
    const std::string output =
        gameplay::format_report(report);
    expect(
        output.find(
            std::string("model_fingerprint=") +
            std::string(
                gameplay::kCandidateModelFingerprint)) !=
            std::string::npos &&
            output.find("worlds_per_action=8") !=
                std::string::npos &&
            output.find("horizon_turns=4") !=
                std::string::npos &&
            output.find("rollouts_per_world=1") !=
                std::string::npos &&
            output.find("root_search_depth=1") !=
                std::string::npos &&
            output.find("shallow_prior=1") !=
                std::string::npos,
        "report omitted the frozen model or recipe");
    expect(
        occurrence_count(output, "deck role=") == 10 &&
            occurrence_count(output, "quadrant role=") ==
                40 &&
            occurrence_count(output, "benchmark role=") ==
                2 &&
            output.ends_with("result=PASS\n"),
        "report omitted all-five-deck accounting");
    expect(
        output.find("PRIVATE") == std::string::npos &&
            output.find("descriptor") ==
                std::string::npos &&
            output.find("hand=") ==
                std::string::npos &&
            output.find("library=") ==
                std::string::npos,
        "report leaked nonaggregate gameplay detail");
}

} // namespace

int main() {
    TestRunner runner;
    runner.run(
        "exact anchored contract and mutations",
        test_exact_contract_and_mutations);
    runner.run(
        "exact load-only identities and order",
        test_loader_uses_exact_identities);
    runner.run(
        "loader mutations fail closed",
        test_loader_mutations_fail_closed);
    runner.run(
        "fixed recipe and smoke call order",
        test_fixed_recipe_and_call_order);
    runner.run(
        "all-five-deck unequal accounting",
        test_all_five_deck_accounting_and_mutations);
    runner.run(
        "smoke scientific and timing boundaries",
        test_gate_boundaries);
    runner.run(
        "mid-run mutations fail closed",
        test_mutation_fail_closed_and_stops_candidate);
    runner.run(
        "CLI contract, statuses, and redaction",
        test_cli_contract_status_and_redaction);
    runner.run(
        "complete aggregate-only report",
        test_report_is_complete_and_aggregate_only);
    return runner.finish();
}
