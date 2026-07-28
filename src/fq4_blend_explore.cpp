#include "old_school/fq4_blend_explore.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/fq4_dev5_gameplay.hpp"
#include "old_school/fq4_dev_candidate_artifact.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace old_school::fq4_blend_explore {
namespace {

namespace artifact = fq4_dev_candidate_artifact;
namespace gameplay = fq4_dev5_gameplay;
namespace integrity = artifact_integrity;

struct Variant {
    double alpha = 0.0;
    std::shared_ptr<const LearnedModel> model;
    std::string fingerprint;
};

struct TimedResult {
    BotBenchmarkSummary summary;
    double elapsed_seconds = 0.0;
};

bool score_precedes(
    const CandidateScore& left,
    const CandidateScore& right) {
    if (left.wins != right.wins) {
        return left.wins > right.wins;
    }
    return left.alpha < right.alpha;
}

bool same_non_priority_components(
    const LearnedModelComponentFingerprints& left,
    const LearnedModelComponentFingerprints& right) {
    return left.critic == right.critic &&
           left.attack == right.attack &&
           left.block == right.block &&
           left.damage_order == right.damage_order;
}

void require_compatible_endpoints(
    const std::shared_ptr<const LearnedModel>& parent,
    const std::shared_ptr<const LearnedModel>& candidate) {
    if (!parent || !candidate) {
        throw std::invalid_argument(
            "Priority blend requires two models");
    }
    const auto parent_components =
        learned_model_component_fingerprints(parent);
    const auto candidate_components =
        learned_model_component_fingerprints(candidate);
    if (!same_non_priority_components(
            parent_components, candidate_components)) {
        throw std::invalid_argument(
            "Priority blend endpoints differ outside Priority");
    }
}

void require_same_shape(
    const LearnedPriorityHeadParameters& parent,
    const LearnedPriorityHeadParameters& candidate) {
    if (parent.input_hidden.size() !=
            candidate.input_hidden.size() ||
        parent.hidden_bias.size() !=
            candidate.hidden_bias.size() ||
        parent.hidden_output.size() !=
            candidate.hidden_output.size() ||
        parent.direct.size() != candidate.direct.size()) {
        throw std::invalid_argument(
            "Priority blend endpoints have different shapes");
    }
    for (std::size_t row = 0;
         row < parent.input_hidden.size(); ++row) {
        if (parent.input_hidden[row].size() !=
            candidate.input_hidden[row].size()) {
            throw std::invalid_argument(
                "Priority blend endpoints have different shapes");
        }
    }
}

double blend_value(
    double parent, double candidate, double alpha) {
    const double value = std::lerp(parent, candidate, alpha);
    if (!std::isfinite(value)) {
        throw std::invalid_argument(
            "Priority blend produced a non-finite parameter");
    }
    return value;
}

void blend_vector(
    std::vector<double>& output,
    const std::vector<double>& candidate,
    double alpha) {
    for (std::size_t index = 0; index < output.size();
         ++index) {
        output[index] =
            blend_value(output[index], candidate[index], alpha);
    }
}

gameplay::FixedDeployment load_exact_deployment() {
    return gameplay::testing::load_fixed_deployment_with({
        .snapshot =
            [](const std::filesystem::path& path) {
                return integrity::snapshot_regular_file(path);
            },
        .load_parent =
            [](const std::filesystem::path& path) {
                const LearnedValueChallengerArtifact loaded =
                    load_learned_value_challenger_artifact(
                        path.string(), gameplay::kTrainingGames,
                        gameplay::kTrainingSeed,
                        gameplay::kParentGenerations);
                return gameplay::testing::ParentArtifact{
                    .model = loaded.model(),
                    .training_games = loaded.training_games(),
                    .training_seed = loaded.seed(),
                    .generations =
                        loaded.self_play_generations(),
                };
            },
        .load_candidate =
            [](const std::filesystem::path& path,
               std::shared_ptr<const LearnedModel> parent,
               const artifact::Contract& contract,
               const artifact::FileIdentity& identity) {
                const artifact::LoadedCandidate loaded =
                    artifact::load(
                        path, std::move(parent),
                        contract, identity);
                return gameplay::testing::LoadedCandidate{
                    .model = loaded.model(),
                    .report = loaded.report(),
                };
            },
        .inspect_model =
            [](std::shared_ptr<const LearnedModel> model) {
                return gameplay::ModelIdentity{
                    .fingerprint =
                        learned_model_fingerprint(model),
                    .components =
                        learned_model_component_fingerprints(
                            model),
                };
            },
    });
}

std::vector<Variant> make_variants(
    const gameplay::FixedDeployment& deployment) {
    std::vector<Variant> variants;
    variants.reserve(kCandidateAlphas.size());
    for (const double alpha : kCandidateAlphas) {
        auto model = blend_priority_heads(
            deployment.parent, deployment.candidate, alpha);
        variants.push_back({
            .alpha = alpha,
            .model = std::move(model),
            .fingerprint = {},
        });
        variants.back().fingerprint =
            learned_model_fingerprint(variants.back().model);
    }
    if (variants.back().model != deployment.candidate ||
        variants.back().fingerprint !=
            deployment.candidate_model_fingerprint) {
        throw std::logic_error(
            "alpha one did not preserve exact DEV5");
    }
    return variants;
}

TimedResult run_one(
    std::shared_ptr<const LearnedModel> challenger_model,
    std::shared_ptr<const LearnedModel> baseline_model,
    std::size_t repetitions, std::uint64_t seed,
    bool identical_control) {
    const BotConfig challenger =
        gameplay::testing::make_learned_bot(
            std::move(challenger_model));
    const BotConfig baseline =
        gameplay::testing::make_learned_bot(
            std::move(baseline_model));
    const auto started = std::chrono::steady_clock::now();
    BotBenchmarkSummary summary = run_bot_benchmark(
        repetitions, seed, challenger, baseline,
        gameplay::testing::make_game_config(),
        identical_control);
    const std::chrono::duration<double> elapsed =
        std::chrono::steady_clock::now() - started;
    const std::size_t expected_games = 60 * repetitions;
    if (summary.total_games != expected_games ||
        summary.repetitions_per_deck_pairing != repetitions ||
        summary.evaluation_seed != seed) {
        throw std::logic_error(
            "balanced benchmark accounting is incomplete");
    }
    const std::size_t expected_deck_games =
        12 * repetitions;
    if (!std::all_of(
            summary.challenger_decks.begin(),
            summary.challenger_decks.end(),
            [expected_deck_games](
                const DeckSimulationStats& row) {
                return row.games == expected_deck_games;
            })) {
        throw std::logic_error(
            "balanced benchmark deck accounting is incomplete");
    }
    return {
        .summary = std::move(summary),
        .elapsed_seconds = elapsed.count(),
    };
}

void print_result(
    std::ostream& output, std::string_view stage,
    std::string_view variant, double alpha,
    const TimedResult& result) {
    const auto& summary = result.summary;
    const std::size_t decisions =
        summary.challenger_stats.total_decisions +
        summary.baseline_stats.total_decisions;
    const std::size_t rollouts =
        summary.challenger_stats.total_rollouts +
        summary.baseline_stats.total_rollouts;
    output << std::setprecision(9)
           << "result stage=" << stage
           << " variant=" << variant
           << " alpha=" << alpha
           << " seed=" << summary.evaluation_seed
           << " repetitions="
           << summary.repetitions_per_deck_pairing
           << " games=" << summary.total_games
           << " wins=" << summary.challenger_stats.wins
           << " losses=" << summary.challenger_stats.losses
           << " draws=" << summary.challenger_stats.draws
           << " win_rate=" << summary.challenger_win_rate()
           << " cr1_low_95="
           << 100.0 *
                  summary.challenger_quartet_cr1
                      .confidence_low_95
           << " cr1_high_95="
           << 100.0 *
                  summary.challenger_quartet_cr1
                      .confidence_high_95
           << " elapsed_seconds=" << result.elapsed_seconds
           << " decisions=" << decisions
           << " rollouts=" << rollouts << '\n';
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        const auto& row = summary.challenger_decks[deck];
        output << "deck stage=" << stage
               << " alpha=" << alpha
               << " deck="
               << deck_name(static_cast<DeckId>(deck))
               << " wins=" << row.wins
               << " losses=" << row.losses
               << " draws=" << row.draws
               << " games=" << row.games << '\n';
    }
    output.flush();
}

const Variant& find_variant(
    const std::vector<Variant>& variants, double alpha) {
    const auto found = std::find_if(
        variants.begin(), variants.end(),
        [alpha](const Variant& variant) {
            return variant.alpha == alpha;
        });
    if (found == variants.end()) {
        throw std::logic_error(
            "selected alpha has no model");
    }
    return *found;
}

} // namespace

std::shared_ptr<const LearnedModel> blend_priority_heads(
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate,
    double alpha) {
    if (!std::isfinite(alpha) ||
        alpha < 0.0 || alpha > 1.0) {
        throw std::invalid_argument(
            "Priority blend alpha must be in [0, 1]");
    }
    require_compatible_endpoints(parent, candidate);
    if (alpha == 0.0) {
        return parent;
    }
    if (alpha == 1.0) {
        return candidate;
    }

    LearnedPriorityHeadParameters blended =
        learned_priority_head_parameters(parent);
    const LearnedPriorityHeadParameters candidate_parameters =
        learned_priority_head_parameters(candidate);
    require_same_shape(blended, candidate_parameters);
    for (std::size_t row = 0;
         row < blended.input_hidden.size(); ++row) {
        blend_vector(
            blended.input_hidden[row],
            candidate_parameters.input_hidden[row],
            alpha);
    }
    blend_vector(
        blended.hidden_bias,
        candidate_parameters.hidden_bias, alpha);
    blend_vector(
        blended.hidden_output,
        candidate_parameters.hidden_output, alpha);
    blend_vector(
        blended.direct, candidate_parameters.direct, alpha);
    blended.output_bias = blend_value(
        blended.output_bias,
        candidate_parameters.output_bias, alpha);

    auto model = with_learned_priority_head_parameters(
        parent, blended);
    const auto parent_components =
        learned_model_component_fingerprints(parent);
    const auto blended_components =
        learned_model_component_fingerprints(model);
    if (!same_non_priority_components(
            parent_components, blended_components)) {
        throw std::logic_error(
            "Priority blend changed a non-Priority component");
    }
    return model;
}

std::array<double, 2> select_top_two_alphas(
    const std::vector<CandidateScore>& scores) {
    if (scores.size() != kCandidateAlphas.size()) {
        throw std::invalid_argument(
            "E0 ranking requires all four candidate alphas");
    }
    std::vector<CandidateScore> ranked = scores;
    const std::size_t expected_games =
        ranked.front().wins + ranked.front().losses +
        ranked.front().draws;
    if (expected_games == 0) {
        throw std::invalid_argument(
            "E0 ranking requires nonempty results");
    }
    for (const double alpha : kCandidateAlphas) {
        if (std::count_if(
                ranked.begin(), ranked.end(),
                [alpha](const CandidateScore& score) {
                    return score.alpha == alpha;
                }) != 1) {
            throw std::invalid_argument(
                "E0 ranking has missing or duplicate alphas");
        }
    }
    for (const auto& score : ranked) {
        if (score.wins + score.losses + score.draws !=
            expected_games) {
            throw std::invalid_argument(
                "E0 ranking has unequal game counts");
        }
    }
    std::sort(
        ranked.begin(), ranked.end(),
        score_precedes);
    return {ranked[0].alpha, ranked[1].alpha};
}

int run_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error) {
    constexpr std::string_view kUsage =
        "Usage: old-school-fq4-blend-explore\n";
    if (argc != 1 || argv == nullptr || argv[0] == nullptr) {
        error << kUsage;
        return 2;
    }
    try {
        const gameplay::FixedDeployment deployment =
            load_exact_deployment();
        const std::vector<Variant> variants =
            make_variants(deployment);
        output
            << "FQ4 Priority blend exploration"
            << " parent="
            << deployment.parent_model_fingerprint
            << " dev5="
            << deployment.candidate_model_fingerprint
            << '\n'
            << "plan E0_seed=" << kStageE0Seed
            << " E0_repetitions=" << kStageE0Repetitions
            << " E1_seed=" << kStageE1Seed
            << " E1_repetitions=" << kStageE1Repetitions
            << " runtime=descriptive\n";
        output.flush();

        output << "running stage=E0 variant=C16 alpha=0\n";
        output.flush();
        const TimedResult control = run_one(
            deployment.parent, deployment.parent,
            kStageE0Repetitions, kStageE0Seed, true);
        print_result(output, "E0", "C16", 0.0, control);

        std::vector<CandidateScore> scores;
        scores.reserve(variants.size());
        for (const Variant& variant : variants) {
            output << "running stage=E0 variant=blend alpha="
                   << variant.alpha << " model="
                   << variant.fingerprint << '\n';
            output.flush();
            const TimedResult result = run_one(
                variant.model, deployment.parent,
                kStageE0Repetitions, kStageE0Seed, false);
            print_result(
                output, "E0", "blend", variant.alpha, result);
            scores.push_back({
                .alpha = variant.alpha,
                .wins = result.summary.challenger_stats.wins,
                .losses = result.summary.challenger_stats.losses,
                .draws = result.summary.challenger_stats.draws,
            });
        }

        const std::array<double, 2> selected =
            select_top_two_alphas(scores);
        output << "selection stage=E1 alpha_first="
               << selected[0] << " alpha_second="
               << selected[1]
               << " tie_break=smaller_alpha\n";
        output.flush();
        std::vector<CandidateScore> stage_e1_scores;
        stage_e1_scores.reserve(selected.size());
        for (const double alpha : selected) {
            const Variant& variant =
                find_variant(variants, alpha);
            output << "running stage=E1 variant=blend alpha="
                   << alpha << " model="
                   << variant.fingerprint << '\n';
            output.flush();
            const TimedResult result = run_one(
                variant.model, deployment.parent,
                kStageE1Repetitions, kStageE1Seed, false);
            print_result(
                output, "E1", "blend", alpha, result);
            stage_e1_scores.push_back({
                .alpha = alpha,
                .wins = result.summary.challenger_stats.wins,
                .losses = result.summary.challenger_stats.losses,
                .draws = result.summary.challenger_stats.draws,
            });
        }
        const CandidateScore& winner =
            score_precedes(
                stage_e1_scores[1], stage_e1_scores[0])
                ? stage_e1_scores[1]
                : stage_e1_scores[0];
        output << "winner stage=E1 alpha=" << winner.alpha
               << " wins=" << winner.wins
               << " losses=" << winner.losses
               << " draws=" << winner.draws << '\n';
        output.flush();
        return 0;
    } catch (const std::exception&) {
        error << "result=ERROR reason=fq4_blend_explore_failed\n";
        return 2;
    }
}

} // namespace old_school::fq4_blend_explore
