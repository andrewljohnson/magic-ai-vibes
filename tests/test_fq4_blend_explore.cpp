#include "old_school/fq4_blend_explore.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace explore = old_school::fq4_blend_explore;

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

std::shared_ptr<const old_school::LearnedModel> parent_model() {
    static const auto model =
        old_school::train_learned_value_champion(
            1, 0xB1E0D123ULL);
    return model;
}

std::shared_ptr<const old_school::LearnedModel>
priority_only_candidate() {
    static const auto model = [] {
        const auto parent = parent_model();
        auto parameters =
            old_school::learned_priority_head_parameters(parent);
        parameters.input_hidden[0][0] += 0.75;
        parameters.hidden_bias[0] -= 0.50;
        parameters.hidden_output[0] += 0.25;
        parameters.direct[0] -= 1.25;
        parameters.output_bias += 2.0;
        return old_school::with_learned_priority_head_parameters(
            parent, parameters);
    }();
    return model;
}

bool same_non_priority(
    const old_school::LearnedModelComponentFingerprints& left,
    const old_school::LearnedModelComponentFingerprints& right) {
    return left.critic == right.critic &&
           left.attack == right.attack &&
           left.block == right.block &&
           left.damage_order == right.damage_order;
}

void test_frozen_schedule() {
    expect(
        explore::kCandidateAlphas ==
            std::array<double, 4>{
                0.25, 0.50, 0.75, 1.00},
        "candidate alpha set drifted");
    expect(
        explore::kStageE0Seed == 202607280801ULL &&
            explore::kStageE0Repetitions == 1 &&
            explore::kStageE1Seed == 202607280802ULL &&
            explore::kStageE1Repetitions == 4,
        "exploration schedule drifted");
    expect(
        explore::kPd0StageE0Seed == 202607280803ULL &&
            explore::kPd0StageE0Repetitions == 1 &&
            explore::kPd0StageE1Seed == 202607280804ULL &&
            explore::kPd0StageE1Repetitions == 4 &&
            explore::kPd0BlendAlpha == 0.50,
        "PD0 exploration schedule drifted");
    expect(
        explore::kAdversarialBlocksStageE0Seed ==
                202607280805ULL &&
            explore::kAdversarialBlocksStageE0Repetitions ==
                1 &&
            explore::kAdversarialBlocksStageE1Seed ==
                202607280806ULL &&
            explore::kAdversarialBlocksStageE1Repetitions ==
                4 &&
            explore::kAdversarialBlocksBlendAlpha == 0.50,
        "AdversarialBlocks exploration schedule drifted");
}

void test_blend_endpoints_and_isolation() {
    const auto parent = parent_model();
    const auto candidate = priority_only_candidate();
    expect(
        explore::blend_priority_heads(
            parent, candidate, 0.0) == parent,
        "alpha zero did not preserve parent pointer");
    expect(
        explore::blend_priority_heads(
            parent, candidate, 1.0) == candidate,
        "alpha one did not preserve candidate pointer");

    const auto blended = explore::blend_priority_heads(
        parent, candidate, 0.5);
    const auto parent_parameters =
        old_school::learned_priority_head_parameters(parent);
    const auto candidate_parameters =
        old_school::learned_priority_head_parameters(candidate);
    const auto blended_parameters =
        old_school::learned_priority_head_parameters(blended);
    expect(
        blended_parameters.input_hidden[0][0] ==
            std::lerp(
                parent_parameters.input_hidden[0][0],
                candidate_parameters.input_hidden[0][0],
                0.5) &&
            blended_parameters.hidden_bias[0] ==
                std::lerp(
                    parent_parameters.hidden_bias[0],
                    candidate_parameters.hidden_bias[0],
                    0.5) &&
            blended_parameters.hidden_output[0] ==
                std::lerp(
                    parent_parameters.hidden_output[0],
                    candidate_parameters.hidden_output[0],
                    0.5) &&
            blended_parameters.direct[0] ==
                std::lerp(
                    parent_parameters.direct[0],
                    candidate_parameters.direct[0],
                    0.5) &&
            blended_parameters.output_bias ==
                std::lerp(
                    parent_parameters.output_bias,
                    candidate_parameters.output_bias, 0.5),
        "alpha half did not interpolate Priority tensors");
    const auto parent_components =
        old_school::learned_model_component_fingerprints(parent);
    const auto blended_components =
        old_school::learned_model_component_fingerprints(blended);
    expect(
        same_non_priority(
            parent_components, blended_components),
        "blend changed a non-Priority component");
    expect(
        parent_components.priority !=
            blended_components.priority,
        "blend did not change Priority");
}

void test_blend_rejects_invalid_inputs() {
    const auto parent = parent_model();
    const auto candidate = priority_only_candidate();
    expect_rejected(
        [&] {
            static_cast<void>(
                explore::blend_priority_heads(
                    parent, candidate, -0.01));
        },
        "negative alpha was accepted");
    expect_rejected(
        [&] {
            static_cast<void>(
                explore::blend_priority_heads(
                    parent, candidate, 1.01));
        },
        "alpha above one was accepted");
    expect_rejected(
        [&] {
            static_cast<void>(
                explore::blend_priority_heads(
                    parent, candidate,
                    std::numeric_limits<double>::quiet_NaN()));
        },
        "NaN alpha was accepted");
    expect_rejected(
        [&] {
            static_cast<void>(
                explore::blend_priority_heads(
                    nullptr, candidate, 0.5));
        },
        "null parent was accepted");
}

void test_ranking_and_tie_break() {
    const std::vector<explore::CandidateScore> scores{
        {.alpha = 1.00, .wins = 31, .losses = 29},
        {.alpha = 0.75, .wins = 31, .losses = 29},
        {.alpha = 0.50, .wins = 30, .losses = 0,
         .draws = 30},
        {.alpha = 0.25, .wins = 31, .losses = 29},
    };
    const auto selected =
        explore::select_top_two_alphas(scores);
    expect(
        selected == std::array<double, 2>{0.25, 0.75},
        "ranking did not prefer smaller tied alphas");

    auto malformed = scores;
    malformed.pop_back();
    expect_rejected(
        [&] {
            static_cast<void>(
                explore::select_top_two_alphas(malformed));
        },
        "ranking accepted a missing alpha");
    malformed = scores;
    malformed[0].alpha = 0.75;
    expect_rejected(
        [&] {
            static_cast<void>(
                explore::select_top_two_alphas(malformed));
        },
        "ranking accepted a duplicate alpha");
}

void test_pd0_ranking_and_tie_break() {
    std::array<explore::CandidateScore, 2> scores{{
        {.alpha = 0.50, .wins = 31, .losses = 29},
        {.alpha = 0.00, .wins = 31, .losses = 29},
    }};
    expect(
        explore::select_pd0_winner_alpha(scores) == 0.0,
        "PD0 tie did not prefer exact C16");
    scores[0] = {
        .alpha = 0.50, .wins = 32, .losses = 28,
    };
    expect(
        explore::select_pd0_winner_alpha(scores) == 0.50,
        "PD0 ranking did not prefer more wins");

    auto malformed = scores;
    malformed[1].alpha = 0.50;
    expect_rejected(
        [&] {
            static_cast<void>(
                explore::select_pd0_winner_alpha(malformed));
        },
        "PD0 ranking accepted duplicate candidates");
    malformed = scores;
    malformed[1] = {
        .alpha = 0.0, .wins = 30, .losses = 29,
    };
    expect_rejected(
        [&] {
            static_cast<void>(
                explore::select_pd0_winner_alpha(malformed));
        },
        "PD0 ranking accepted unequal game counts");
}

void test_pd0_config_changes_only_pass_dominance() {
    const auto model = parent_model();
    const auto ordinary =
        explore::make_exploratory_bot(model, false);
    const auto pd0 =
        explore::make_exploratory_bot(model, true);
    expect(
        ordinary.kind == old_school::BotKind::Learned &&
            ordinary.learned_variant ==
                old_school::LearnedVariant::
                    ValueSearchChampion &&
            ordinary.rollouts_per_action == 8 &&
            ordinary.exploration_rate == 0.0 &&
            ordinary.value_continuation_epsilon == 0.0 &&
            ordinary.value_priority_residual_weight == 0.10 &&
            !ordinary.value_pass_dominance &&
            !ordinary.value_adversarial_blocks &&
            ordinary.value_continuation_controller ==
                old_school::LearnedContinuationController::
                    Legacy &&
            ordinary.training_games == 800 &&
            ordinary.learned_model == model,
        "ordinary exploratory bot recipe drifted");
    expect(
        pd0.kind == ordinary.kind &&
            pd0.learned_variant ==
                ordinary.learned_variant &&
            pd0.rollouts_per_action ==
                ordinary.rollouts_per_action &&
            pd0.exploration_rate ==
                ordinary.exploration_rate &&
            pd0.value_continuation_epsilon ==
                ordinary.value_continuation_epsilon &&
            pd0.value_priority_residual_weight ==
                ordinary.value_priority_residual_weight &&
            pd0.value_pass_dominance &&
            !pd0.value_adversarial_blocks &&
            pd0.value_continuation_controller ==
                ordinary.value_continuation_controller &&
            pd0.training_games == ordinary.training_games &&
            pd0.learned_model == ordinary.learned_model,
        "PD0 changed more than Pass dominance");
    expect_rejected(
        [&] {
            static_cast<void>(
                explore::make_exploratory_bot(
                    nullptr, true));
        },
        "PD0 accepted a missing frozen model");
}

void test_adversarial_blocks_ranking_and_tie_break() {
    std::array<explore::CandidateScore, 2> scores{{
        {.alpha = 0.50, .wins = 31, .losses = 29},
        {.alpha = 0.00, .wins = 31, .losses = 29},
    }};
    expect(
        explore::select_adversarial_blocks_winner_alpha(
            scores) == 0.0,
        "AdversarialBlocks tie did not prefer exact C16");
    scores[0] = {
        .alpha = 0.50, .wins = 32, .losses = 28,
    };
    expect(
        explore::select_adversarial_blocks_winner_alpha(
            scores) == 0.50,
        "AdversarialBlocks ranking did not prefer more wins");

    auto malformed = scores;
    malformed[1].alpha = 0.50;
    expect_rejected(
        [&] {
            static_cast<void>(
                explore::
                    select_adversarial_blocks_winner_alpha(
                        malformed));
        },
        "AdversarialBlocks ranking accepted duplicate "
        "candidates");
    malformed = scores;
    malformed[1] = {
        .alpha = 0.0, .wins = 30, .losses = 29,
    };
    expect_rejected(
        [&] {
            static_cast<void>(
                explore::
                    select_adversarial_blocks_winner_alpha(
                        malformed));
        },
        "AdversarialBlocks ranking accepted unequal game "
        "counts");
}

void test_adversarial_blocks_config_changes_only_aggregation() {
    const auto model = parent_model();
    const auto ordinary =
        explore::make_exploratory_bot(model, false, false);
    const auto treatment =
        explore::make_exploratory_bot(model, false, true);
    expect(
        treatment.kind == ordinary.kind &&
            treatment.learned_variant ==
                ordinary.learned_variant &&
            treatment.rollouts_per_action ==
                ordinary.rollouts_per_action &&
            treatment.exploration_rate ==
                ordinary.exploration_rate &&
            treatment.value_continuation_epsilon ==
                ordinary.value_continuation_epsilon &&
            treatment.value_priority_residual_weight ==
                ordinary.value_priority_residual_weight &&
            treatment.value_pass_dominance ==
                ordinary.value_pass_dominance &&
            treatment.value_adversarial_blocks &&
            !ordinary.value_adversarial_blocks &&
            treatment.value_continuation_controller ==
                ordinary.value_continuation_controller &&
            treatment.training_games ==
                ordinary.training_games &&
            treatment.learned_model ==
                ordinary.learned_model,
        "AdversarialBlocks changed more than attack "
        "aggregation");
}

void test_adversarial_block_aggregation_changes_ranking() {
    const std::vector<std::vector<double>> block_scores{
        {1.0, 1.0, 1.0, 0.0},
        {0.5, 0.5, 0.5, 0.5},
    };
    const auto historical =
        old_school::
            aggregate_learned_value_attack_block_scores(
                block_scores, false);
    const auto adversarial =
        old_school::
            aggregate_learned_value_attack_block_scores(
                block_scores, true);
    expect(
        historical.scores ==
                std::vector<double>{0.75, 0.5} &&
            historical.selected_candidate == 0,
        "historical mean aggregation recipe drifted");
    expect(
        adversarial.scores ==
                std::vector<double>{0.0, 0.5} &&
            adversarial.selected_candidate == 1,
        "defender-best-response minimum did not change "
        "the synthetic ranking");
    const auto tied =
        old_school::
            aggregate_learned_value_attack_block_scores(
                {{0.25}, {0.25}}, true);
    expect(
        tied.selected_candidate == 0,
        "adversarial aggregation changed first-candidate "
        "tie behavior");
}

void test_cli_rejects_arguments_without_loading_models() {
    char program[] = "old-school-fq4-blend-explore";
    char unexpected[] = "unexpected";
    char* arguments[]{program, unexpected};
    std::ostringstream output;
    std::ostringstream error;
    expect(
        explore::run_cli(
            2, arguments, output, error) == 2,
        "CLI accepted an argument");
    expect(
        output.str().empty() &&
            error.str() ==
                "Usage: old-school-fq4-blend-explore"
                " [--pd0|--adversarial-blocks]\n",
        "CLI argument rejection was not concise");
}

} // namespace

int main() {
    TestRunner runner;
    runner.run("frozen schedule", test_frozen_schedule);
    runner.run(
        "blend endpoints and isolation",
        test_blend_endpoints_and_isolation);
    runner.run(
        "blend invalid inputs",
        test_blend_rejects_invalid_inputs);
    runner.run(
        "ranking and tie break",
        test_ranking_and_tie_break);
    runner.run(
        "PD0 ranking and tie break",
        test_pd0_ranking_and_tie_break);
    runner.run(
        "PD0 config isolation",
        test_pd0_config_changes_only_pass_dominance);
    runner.run(
        "AdversarialBlocks ranking and tie break",
        test_adversarial_blocks_ranking_and_tie_break);
    runner.run(
        "AdversarialBlocks config isolation",
        test_adversarial_blocks_config_changes_only_aggregation);
    runner.run(
        "AdversarialBlocks synthetic aggregation",
        test_adversarial_block_aggregation_changes_ranking);
    runner.run(
        "CLI argument rejection",
        test_cli_rejects_arguments_without_loading_models);
    return runner.finish();
}
