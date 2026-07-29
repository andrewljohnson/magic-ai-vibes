#include "old_school/web_bridge.hpp"
#include "old_school/learned_priority_bilinear.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <unistd.h>

namespace {

class TestRunner {
  public:
    void run(std::string_view name,
             const std::function<void()>& test) {
        try {
            test();
            ++passed_;
        } catch (const std::exception& exception) {
            ++failed_;
            std::cerr << "FAIL " << name << ": "
                      << exception.what() << '\n';
        }
    }

    int finish() const {
        if (failed_ != 0) {
            std::cerr << failed_ << " test(s) failed; "
                      << passed_ << " passed\n";
            return 1;
        }
        std::cout << passed_ << " web bridge tests passed\n";
        return 0;
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

class CorruptAq19Artifact {
  public:
    CorruptAq19Artifact() {
        path_ =
            std::filesystem::temp_directory_path() /
            ("old-school-web-corrupt-aq19-" +
             std::to_string(
                 static_cast<unsigned long long>(
                     ::getpid())) +
             ".bin");
        if (std::filesystem::exists(path_)) {
            throw std::runtime_error(
                "corrupt AQ19 fixture already exists");
        }
        std::ofstream output(path_, std::ios::binary);
        output << "not-an-aq19-artifact";
        if (!output) {
            throw std::runtime_error(
                "could not create corrupt AQ19 fixture");
        }
    }

    CorruptAq19Artifact(
        const CorruptAq19Artifact&) = delete;
    CorruptAq19Artifact& operator=(
        const CorruptAq19Artifact&) = delete;

    ~CorruptAq19Artifact() {
        std::error_code error;
        static_cast<void>(
            std::filesystem::remove(path_, error));
    }

    const std::filesystem::path& path() const {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

std::string indexed_responses(std::size_t count,
                              std::size_t priority_index) {
    std::ostringstream responses;
    for (std::size_t decision = 1;
         decision <= count; ++decision) {
        responses
            << "{\"decisionId\":" << decision
            << ",\"index\":" << priority_index
            << ",\"ids\":[],\"pairs\":[],"
               "\"indices\":[0]}\n";
    }
    return responses.str();
}

std::string passive_responses(std::size_t count) {
    return indexed_responses(count, 0);
}

std::string responses_with_cleanup_value(
    std::size_t count, std::string_view cleanup_value) {
    std::ostringstream responses;
    for (std::size_t decision = 1;
         decision <= count; ++decision) {
        responses
            << "{\"decisionId\":" << decision
            << ",\"index\":0,\"ids\":[],\"pairs\":[],"
               "\"indices\":"
            << cleanup_value << "}\n";
    }
    return responses.str();
}

old_school::web::BridgeConfig fast_config() {
    return {
        .human_deck = old_school::DeckId::RUAggro,
        .opponent_deck = old_school::DeckId::Red,
        .opponent_bot = old_school::BotKind::Handcrafted,
        .learned_variant =
            old_school::LearnedVariant::ValueSearchChampion,
        .game_seed = 42,
        .monte_carlo_rollouts = 1,
        .deep_monte_carlo_rollouts = 1,
        .learned_rollouts = 1,
        .training_games = 1,
        .training_seed = 424242,
        .reveal_opponent_hand = false,
    };
}

std::string card_id_csv(
    const std::vector<old_school::CardId>& cards) {
    std::ostringstream output;
    for (std::size_t index = 0; index < cards.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << static_cast<unsigned int>(cards[index]);
    }
    return output.str();
}

void test_names_parse_strictly() {
    using old_school::BotKind;
    using old_school::DeckId;
    using old_school::LearnedVariant;
    using old_school::web::parse_deck_id;
    using old_school::web::parse_evolution_pilot;
    using old_school::web::parse_opponent_bot;

    expect(parse_deck_id("green") == DeckId::Green,
           "green deck did not parse");
    expect(parse_deck_id("ru-aggro") == DeckId::RUAggro,
           "RU Aggro deck did not parse");
    LearnedVariant variant =
        LearnedVariant::UnifiedActor;
    std::size_t generations = 0;
    bool adversarial_blocks = true;
    bool pass_dominance = true;
    bool actor_local_search = true;
    bool priority_bilinear = true;
    expect(parse_opponent_bot(
               "learned-value-c16", variant, generations,
               adversarial_blocks, pass_dominance,
               actor_local_search, priority_bilinear) ==
               BotKind::Learned &&
               variant ==
                   LearnedVariant::ValueSearchChampion &&
               generations == 16 &&
               !adversarial_blocks &&
               !pass_dominance &&
               !actor_local_search &&
               !priority_bilinear,
           "Learned Value C16 did not parse");
    expect(parse_opponent_bot(
               "learned-value-c16-actor-local-search",
               variant, generations, adversarial_blocks,
               pass_dominance, actor_local_search,
               priority_bilinear) ==
               BotKind::Learned &&
               variant ==
                   LearnedVariant::ValueSearchChampion &&
               generations == 16 &&
               !adversarial_blocks &&
               !pass_dominance &&
               actor_local_search &&
               !priority_bilinear,
           "AQ4 actor-local-search pilot did not parse");
    expect(parse_opponent_bot(
               "learned-value-c16-combined-search",
               variant, generations, adversarial_blocks,
               pass_dominance, actor_local_search,
               priority_bilinear) ==
               BotKind::Learned &&
               variant ==
                   LearnedVariant::ValueSearchChampion &&
               generations == 16 &&
               adversarial_blocks &&
               !pass_dominance &&
               actor_local_search &&
               !priority_bilinear,
           "AQ15 combined-search pilot did not parse");
    expect(parse_opponent_bot(
               "learned-value-c16-stack-discipline",
               variant, generations, adversarial_blocks,
               pass_dominance, actor_local_search,
               priority_bilinear) ==
               BotKind::Learned &&
               variant ==
                   LearnedVariant::ValueSearchChampion &&
               generations == 16 &&
               adversarial_blocks &&
               pass_dominance &&
               !actor_local_search &&
               !priority_bilinear,
           "stack-discipline diagnostic did not parse");
    expect(parse_opponent_bot(
               "learned-value-c16-adversarial-blocks",
               variant, generations, adversarial_blocks,
               pass_dominance, actor_local_search,
               priority_bilinear) ==
               BotKind::Learned &&
               variant ==
                   LearnedVariant::ValueSearchChampion &&
               generations == 16 &&
               adversarial_blocks &&
               !pass_dominance &&
               !actor_local_search &&
               !priority_bilinear,
           "best-response attack challenger did not parse");
    expect(parse_opponent_bot(
               "learned-value-c16-bilinear-aq19",
               variant, generations, adversarial_blocks,
               pass_dominance, actor_local_search,
               priority_bilinear) ==
               BotKind::Learned &&
               variant ==
                   LearnedVariant::ValueSearchChampion &&
               generations == 16 &&
               !adversarial_blocks &&
               !pass_dominance &&
               !actor_local_search &&
               priority_bilinear,
           "AQ19 bilinear pilot did not parse");
    expect(parse_opponent_bot(
               "learned-value-g0", variant, generations,
               adversarial_blocks, pass_dominance,
               actor_local_search, priority_bilinear) ==
               BotKind::Learned &&
               variant ==
                   LearnedVariant::ValueSearchChampion &&
               generations == 0 &&
               !adversarial_blocks &&
               !pass_dominance &&
               !actor_local_search &&
               !priority_bilinear,
           "Learned Value G0 did not parse");
    expect(parse_opponent_bot(
               "learned-actor", variant, generations,
               adversarial_blocks, pass_dominance,
               actor_local_search, priority_bilinear) ==
               BotKind::Learned &&
               variant == LearnedVariant::UnifiedActor &&
               generations == 0 &&
               !adversarial_blocks &&
               !pass_dominance &&
               !actor_local_search &&
               !priority_bilinear,
           "Learned Actor did not parse");

    bool bad_deck = false;
    try {
        static_cast<void>(parse_deck_id("vintage"));
    } catch (const std::invalid_argument&) {
        bad_deck = true;
    }
    expect(bad_deck, "unknown deck must be rejected");

    expect(
        parse_evolution_pilot("handcrafted") ==
            old_school::web::EvolutionPilot::Handcrafted,
        "Handcrafted evolution pilot did not parse");
    expect(
        parse_evolution_pilot("learned-value-c16") ==
            old_school::web::EvolutionPilot::LearnedValueC16,
        "Learned Value C16 evolution pilot did not parse");
    bool bad_pilot = false;
    try {
        static_cast<void>(
            parse_evolution_pilot("learned-value-g0"));
    } catch (const std::invalid_argument&) {
        bad_pilot = true;
    }
    expect(
        bad_pilot,
        "non-whitelisted evolution pilot must be rejected");
}

void test_exact_custom_deck_transport_is_strict() {
    const auto deck = old_school::blue_deck();
    expect(
        old_school::web::parse_exact_deck_cards(
            card_id_csv(deck)) == deck,
        "exact 40-card transport did not round-trip");

    const auto expect_rejected =
        [](std::string_view encoded) {
            bool rejected = false;
            try {
                static_cast<void>(
                    old_school::web::parse_exact_deck_cards(
                        encoded));
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            expect(rejected,
                   "malformed exact deck was accepted");
        };
    std::vector<old_school::CardId> short_deck(
        39, old_school::CardId::Forest);
    expect_rejected(card_id_csv(short_deck));
    std::vector<old_school::CardId> long_deck(
        41, old_school::CardId::Forest);
    expect_rejected(card_id_csv(long_deck));
    std::vector<old_school::CardId> valid_deck(
        40, old_school::CardId::Forest);
    std::string unknown = card_id_csv(valid_deck);
    unknown.replace(0, 1, std::to_string(old_school::kCardCount));
    expect_rejected(unknown);
    expect_rejected("0,,0");
    expect_rejected("0, 0");
}

void test_frozen_c16_load_boundary_fails_actionably() {
    bool rejected = false;
    std::string message;
    try {
        static_cast<void>(
            old_school::web::load_frozen_learned_value_c16(
                "build/model-cache/"
                "deliberately-missing-web-c16-artifact.bin"));
    } catch (const std::runtime_error& error) {
        rejected = true;
        message = error.what();
    }
    expect(rejected,
           "missing frozen C16 artifact must fail closed");
    expect(message.find("missing, stale, or invalid") !=
               std::string::npos,
           "missing C16 error omitted the failure class");
    expect(message.find(
               "--refresh-value-challenger-cache") !=
               std::string::npos,
           "missing C16 error omitted the separate CLI action");
    expect(message.find(
               "--challenger learned-value-c16 --baseline random "
               "--learned-rollouts 8 --train-games 800 "
               "--train-seed 424242 "
               "--refresh-value-challenger-cache") !=
               std::string::npos,
           "missing C16 error did not provide an executable "
           "benchmark recovery command");
    expect(message.find("--learned-generations") ==
               std::string::npos,
           "missing C16 recovery command included a benchmark-"
           "incompatible generations flag");
}

void test_c16_rejects_noncanonical_training_identity() {
    auto config = fast_config();
    config.opponent_bot = old_school::BotKind::Learned;
    config.learned_variant =
        old_school::LearnedVariant::ValueSearchChampion;
    config.learned_generations = 16;
    config.training_games = 1;

    std::istringstream input(passive_responses(1));
    std::ostringstream output;
    bool rejected = false;
    try {
        static_cast<void>(old_school::web::run_bridge_session(
            input, output, config));
    } catch (const std::invalid_argument& error) {
        rejected =
            std::string_view(error.what()).find(
                "exact --train-games 800 --train-seed 424242") !=
            std::string_view::npos;
    }
    expect(rejected,
           "C16 accepted a noncanonical training identity");
    expect(output.str().empty(),
           "C16 emitted session output before identity validation");
}

void test_adversarial_challenger_maps_only_the_attack_flag() {
    auto challenger = fast_config();
    challenger.opponent_bot = old_school::BotKind::Learned;
    challenger.learned_variant =
        old_school::LearnedVariant::ValueSearchChampion;
    challenger.learned_generations = 16;
    challenger.training_games = 800;
    challenger.learned_rollouts = 8;
    challenger.value_adversarial_blocks = true;

    const auto challenger_bot =
        old_school::web::make_opponent_bot_config(
            challenger, nullptr);
    expect(
        challenger_bot.kind ==
                old_school::BotKind::Learned &&
            challenger_bot.learned_variant ==
                old_school::LearnedVariant::
                    ValueSearchChampion &&
            challenger_bot.rollouts_per_action == 8 &&
            challenger_bot.training_games == 800 &&
            challenger_bot.value_adversarial_blocks,
        "best-response challenger did not map to exact C16 "
        "with only the attack aggregation flag enabled");

    challenger.value_adversarial_blocks = false;
    const auto canonical_bot =
        old_school::web::make_opponent_bot_config(
            challenger, nullptr);
    expect(
        challenger_bot.kind == canonical_bot.kind &&
            challenger_bot.learned_variant ==
                canonical_bot.learned_variant &&
            challenger_bot.rollouts_per_action ==
                canonical_bot.rollouts_per_action &&
            challenger_bot.exploration_rate ==
                canonical_bot.exploration_rate &&
            challenger_bot.value_continuation_epsilon ==
                canonical_bot.value_continuation_epsilon &&
            challenger_bot.value_priority_residual_weight ==
                canonical_bot.value_priority_residual_weight &&
            challenger_bot.value_pass_dominance ==
                canonical_bot.value_pass_dominance &&
            challenger_bot.value_continuation_controller ==
                canonical_bot.value_continuation_controller &&
            challenger_bot.training_games ==
                canonical_bot.training_games &&
            challenger_bot.learned_model ==
                canonical_bot.learned_model &&
            challenger_bot.value_adversarial_blocks &&
            !canonical_bot.value_adversarial_blocks,
        "best-response challenger changed more than the attack "
        "aggregation flag");

    challenger.value_adversarial_blocks = true;
    challenger.value_pass_dominance = true;
    const auto stack_discipline_bot =
        old_school::web::make_opponent_bot_config(
            challenger, nullptr);
    expect(
        stack_discipline_bot.kind == challenger_bot.kind &&
            stack_discipline_bot.learned_variant ==
                challenger_bot.learned_variant &&
            stack_discipline_bot.rollouts_per_action ==
                challenger_bot.rollouts_per_action &&
            stack_discipline_bot.exploration_rate ==
                challenger_bot.exploration_rate &&
            stack_discipline_bot.value_continuation_epsilon ==
                challenger_bot.value_continuation_epsilon &&
            stack_discipline_bot.value_priority_residual_weight ==
                challenger_bot.value_priority_residual_weight &&
            stack_discipline_bot.value_adversarial_blocks ==
                challenger_bot.value_adversarial_blocks &&
            stack_discipline_bot.value_continuation_controller ==
                challenger_bot.value_continuation_controller &&
            stack_discipline_bot.training_games ==
                challenger_bot.training_games &&
            stack_discipline_bot.learned_model ==
                challenger_bot.learned_model &&
            stack_discipline_bot.value_pass_dominance &&
            !challenger_bot.value_pass_dominance,
        "stack-discipline diagnostic changed more than Pass "
        "dominance relative to the attack-only pilot");

    auto invalid = fast_config();
    invalid.value_adversarial_blocks = true;
    std::istringstream input(passive_responses(1));
    std::ostringstream output;
    bool rejected = false;
    try {
        static_cast<void>(
            old_school::web::run_bridge_session(
                input, output, invalid));
    } catch (const std::invalid_argument& error) {
        rejected =
            std::string_view(error.what()).find(
                "require frozen Learned Value C16") !=
            std::string_view::npos;
    }
    expect(
        rejected && output.str().empty(),
        "non-C16 web policy accepted the challenger attack flag");

    invalid.value_adversarial_blocks = false;
    invalid.value_pass_dominance = true;
    std::istringstream pass_input(passive_responses(1));
    std::ostringstream pass_output;
    rejected = false;
    try {
        static_cast<void>(
            old_school::web::run_bridge_session(
                pass_input, pass_output, invalid));
    } catch (const std::invalid_argument& error) {
        rejected =
            std::string_view(error.what()).find(
                "stack discipline requires frozen "
                "Learned Value C16") !=
            std::string_view::npos;
    }
    expect(
        rejected && pass_output.str().empty(),
        "non-C16 web policy accepted Pass dominance");

    invalid.opponent_bot = old_school::BotKind::Learned;
    invalid.learned_variant =
        old_school::LearnedVariant::ValueSearchChampion;
    invalid.learned_generations = 16;
    invalid.training_games = 800;
    invalid.training_seed = 424242;
    invalid.learned_rollouts = 8;
    std::istringstream composition_input(passive_responses(1));
    std::ostringstream composition_output;
    rejected = false;
    try {
        static_cast<void>(
            old_school::web::run_bridge_session(
                composition_input, composition_output, invalid));
    } catch (const std::invalid_argument& error) {
        rejected =
            std::string_view(error.what()).find(
                "stack discipline requires "
                "defender-best-response attacks") !=
            std::string_view::npos;
    }
    expect(
        rejected && composition_output.str().empty(),
        "Pass-dominance-only C16 web policy was accepted");

    invalid.value_adversarial_blocks = true;
    invalid.learned_rollouts = 7;
    std::istringstream search_input(passive_responses(1));
    std::ostringstream search_output;
    rejected = false;
    try {
        static_cast<void>(
            old_school::web::run_bridge_session(
                search_input, search_output, invalid));
    } catch (const std::invalid_argument& error) {
        rejected =
            std::string_view(error.what()).find(
                "requires frozen C16 K8/H4 search") !=
            std::string_view::npos;
    }
    expect(
        rejected && search_output.str().empty(),
        "stack-discipline web policy accepted non-K8 search");
}

void test_actor_local_search_maps_only_the_exact_pilot_flag() {
    auto pilot = fast_config();
    pilot.opponent_bot = old_school::BotKind::Learned;
    pilot.learned_variant =
        old_school::LearnedVariant::ValueSearchChampion;
    pilot.learned_generations = 16;
    pilot.training_games = 800;
    pilot.training_seed = 424242;
    pilot.learned_rollouts = 8;

    const auto canonical_bot =
        old_school::web::make_opponent_bot_config(
            pilot, nullptr);
    pilot.value_actor_local_search = true;
    const auto actor_local_bot =
        old_school::web::make_opponent_bot_config(
            pilot, nullptr);
    expect(
        actor_local_bot.kind == canonical_bot.kind &&
            actor_local_bot.learned_variant ==
                canonical_bot.learned_variant &&
            actor_local_bot.rollouts_per_action ==
                canonical_bot.rollouts_per_action &&
            actor_local_bot.exploration_rate ==
                canonical_bot.exploration_rate &&
            actor_local_bot.value_continuation_epsilon ==
                canonical_bot.value_continuation_epsilon &&
            actor_local_bot.value_priority_residual_weight ==
                canonical_bot.value_priority_residual_weight &&
            actor_local_bot.value_pass_dominance ==
                canonical_bot.value_pass_dominance &&
            actor_local_bot
                    .value_resolved_shallow_prior_weight ==
                canonical_bot
                    .value_resolved_shallow_prior_weight &&
            actor_local_bot.value_adversarial_blocks ==
                canonical_bot.value_adversarial_blocks &&
            actor_local_bot.value_continuation_controller ==
                canonical_bot.value_continuation_controller &&
            actor_local_bot.training_games ==
                canonical_bot.training_games &&
            actor_local_bot.learned_model ==
                canonical_bot.learned_model &&
            actor_local_bot.value_actor_local_search &&
            !canonical_bot.value_actor_local_search,
        "AQ4 pilot changed more than the actor-local-search flag");

    pilot.value_adversarial_blocks = true;
    const auto combined_bot =
        old_school::web::make_opponent_bot_config(
            pilot, nullptr);
    expect(
        combined_bot.kind == actor_local_bot.kind &&
            combined_bot.learned_variant ==
                actor_local_bot.learned_variant &&
            combined_bot.rollouts_per_action ==
                actor_local_bot.rollouts_per_action &&
            combined_bot.exploration_rate ==
                actor_local_bot.exploration_rate &&
            combined_bot.value_continuation_epsilon ==
                actor_local_bot.value_continuation_epsilon &&
            combined_bot.value_priority_residual_weight ==
                actor_local_bot.value_priority_residual_weight &&
            combined_bot.value_pass_dominance ==
                actor_local_bot.value_pass_dominance &&
            combined_bot
                    .value_resolved_shallow_prior_weight ==
                actor_local_bot
                    .value_resolved_shallow_prior_weight &&
            combined_bot.value_continuation_controller ==
                actor_local_bot.value_continuation_controller &&
            combined_bot.training_games ==
                actor_local_bot.training_games &&
            combined_bot.learned_model ==
                actor_local_bot.learned_model &&
            combined_bot.value_actor_local_search ==
                actor_local_bot.value_actor_local_search &&
            combined_bot.value_recursive_policy_improvement ==
                actor_local_bot.value_recursive_policy_improvement &&
            combined_bot.value_adversarial_blocks &&
            !actor_local_bot.value_adversarial_blocks,
        "AQ15 combined pilot changed more than attack "
        "aggregation relative to AQ4");

    pilot.value_actor_local_search = false;
    const auto adversarial_bot =
        old_school::web::make_opponent_bot_config(
            pilot, nullptr);
    expect(
        combined_bot.kind == adversarial_bot.kind &&
            combined_bot.learned_variant ==
                adversarial_bot.learned_variant &&
            combined_bot.rollouts_per_action ==
                adversarial_bot.rollouts_per_action &&
            combined_bot.exploration_rate ==
                adversarial_bot.exploration_rate &&
            combined_bot.value_continuation_epsilon ==
                adversarial_bot.value_continuation_epsilon &&
            combined_bot.value_priority_residual_weight ==
                adversarial_bot.value_priority_residual_weight &&
            combined_bot.value_pass_dominance ==
                adversarial_bot.value_pass_dominance &&
            combined_bot
                    .value_resolved_shallow_prior_weight ==
                adversarial_bot
                    .value_resolved_shallow_prior_weight &&
            combined_bot.value_adversarial_blocks ==
                adversarial_bot.value_adversarial_blocks &&
            combined_bot.value_recursive_policy_improvement ==
                adversarial_bot.value_recursive_policy_improvement &&
            combined_bot.value_continuation_controller ==
                adversarial_bot.value_continuation_controller &&
            combined_bot.training_games ==
                adversarial_bot.training_games &&
            combined_bot.learned_model ==
                adversarial_bot.learned_model &&
            combined_bot.value_actor_local_search &&
            !adversarial_bot.value_actor_local_search,
        "AQ15 combined pilot changed more than actor-local "
        "search relative to Best-Response Attacks");

    const auto rejected_with =
        [](old_school::web::BridgeConfig config,
           std::string_view required_text) {
            std::istringstream input(passive_responses(1));
            std::ostringstream output;
            bool rejected = false;
            try {
                static_cast<void>(
                    old_school::web::run_bridge_session(
                        input, output, config));
            } catch (const std::invalid_argument& error) {
                rejected =
                    std::string_view(error.what()).find(
                        required_text) != std::string_view::npos;
            }
            return rejected && output.str().empty();
        };

    auto invalid = fast_config();
    invalid.value_actor_local_search = true;
    expect(
        rejected_with(
            invalid,
            "actor-local search requires frozen Learned Value C16"),
        "non-C16 web policy accepted actor-local search");

    invalid = pilot;
    invalid.value_actor_local_search = true;
    invalid.learned_rollouts = 7;
    expect(
        rejected_with(
            invalid,
            "requires frozen C16 K8/H8 plus inner K2/H4 search"),
        "AQ4 web policy accepted non-K8 root search");

    invalid = pilot;
    invalid.value_actor_local_search = true;
    invalid.value_adversarial_blocks = true;
    invalid.value_pass_dominance = true;
    expect(
        rejected_with(
            invalid,
            "cannot be combined with Pass dominance"),
        "AQ15 web policy accepted Pass dominance");
}

void test_aq19_maps_only_the_bilinear_residual() {
    auto pilot = fast_config();
    pilot.opponent_bot = old_school::BotKind::Learned;
    pilot.learned_variant =
        old_school::LearnedVariant::ValueSearchChampion;
    pilot.learned_generations = 16;
    pilot.training_games = 800;
    pilot.training_seed = 424242;
    pilot.learned_rollouts = 8;

    const auto canonical_bot =
        old_school::web::make_opponent_bot_config(
            pilot, nullptr);
    const auto residual =
        std::make_shared<
            const old_school::
                LearnedPriorityBilinear>(
            old_school::
                LearnedPriorityBilinearParameters{});
    bool rejected_unflagged_residual = false;
    try {
        static_cast<void>(
            old_school::web::make_opponent_bot_config(
                pilot, nullptr, residual));
    } catch (const std::invalid_argument&) {
        rejected_unflagged_residual = true;
    }
    expect(
        rejected_unflagged_residual,
        "programmatic caller injected AQ19 residual while "
        "its treatment flag was false");
    pilot.value_priority_bilinear = true;
    bool rejected_missing_residual = false;
    try {
        static_cast<void>(
            old_school::web::make_opponent_bot_config(
                pilot, nullptr));
    } catch (const std::invalid_argument&) {
        rejected_missing_residual = true;
    }
    expect(
        rejected_missing_residual,
        "programmatic caller enabled AQ19 without its "
        "authenticated residual");
    const auto aq19_bot =
        old_school::web::make_opponent_bot_config(
            pilot, nullptr, residual);
    expect(
        aq19_bot.kind == canonical_bot.kind &&
            aq19_bot.learned_variant ==
                canonical_bot.learned_variant &&
            aq19_bot.rollouts_per_action ==
                canonical_bot.rollouts_per_action &&
            aq19_bot.exploration_rate ==
                canonical_bot.exploration_rate &&
            aq19_bot.value_continuation_epsilon ==
                canonical_bot.value_continuation_epsilon &&
            aq19_bot.value_priority_residual_weight ==
                canonical_bot.value_priority_residual_weight &&
            aq19_bot.value_pass_dominance ==
                canonical_bot.value_pass_dominance &&
            aq19_bot
                    .value_resolved_shallow_prior_weight ==
                canonical_bot
                    .value_resolved_shallow_prior_weight &&
            aq19_bot.value_adversarial_blocks ==
                canonical_bot.value_adversarial_blocks &&
            aq19_bot.value_actor_local_search ==
                canonical_bot.value_actor_local_search &&
            aq19_bot
                    .value_recursive_policy_improvement ==
                canonical_bot
                    .value_recursive_policy_improvement &&
            aq19_bot.value_continuation_controller ==
                canonical_bot.value_continuation_controller &&
            aq19_bot.training_games ==
                canonical_bot.training_games &&
            aq19_bot.learned_model ==
                canonical_bot.learned_model &&
            aq19_bot.value_priority_bilinear ==
                residual &&
            !canonical_bot.value_priority_bilinear,
        "AQ19 pilot changed more than the immutable "
        "bilinear residual object");

    const auto rejected_with =
        [](old_school::web::BridgeConfig config,
           std::string_view required_text) {
            std::istringstream input(
                passive_responses(1));
            std::ostringstream output;
            bool rejected = false;
            try {
                static_cast<void>(
                    old_school::web::run_bridge_session(
                        input, output, config));
            } catch (const std::invalid_argument& error) {
                rejected =
                    std::string_view(error.what()).find(
                        required_text) !=
                    std::string_view::npos;
            }
            return rejected && output.str().empty();
        };

    auto invalid = fast_config();
    invalid.value_priority_bilinear = true;
    expect(
        rejected_with(
            invalid,
            "AQ19 bilinear requires frozen Learned Value C16"),
        "non-C16 web policy accepted AQ19");

    invalid = pilot;
    invalid.learned_rollouts = 7;
    expect(
        rejected_with(
            invalid,
            "AQ19 bilinear requires frozen C16 K8/H4 search"),
        "AQ19 web policy accepted non-K8 search");

    invalid = pilot;
    invalid.value_adversarial_blocks = true;
    expect(
        rejected_with(
            invalid,
            "AQ19 bilinear cannot be combined with another "
            "web research treatment"),
        "AQ19 web policy accepted a companion treatment");

    pilot.aq19_bilinear_artifact_path =
        "build/model-cache/"
        "deliberately-missing-aq19-bilinear.bin";
    std::istringstream missing_input(
        passive_responses(1));
    std::ostringstream missing_output;
    bool missing_rejected = false;
    std::string missing_message;
    try {
        static_cast<void>(
            old_school::web::run_bridge_session(
                missing_input, missing_output, pilot));
    } catch (const std::runtime_error& error) {
        missing_rejected = true;
        missing_message = error.what();
    }
    expect(
        missing_rejected &&
            missing_message.find(
                "old-school-decision-density-bilinear-artifact "
                "--publish") != std::string::npos,
        "missing AQ19 artifact did not fail with its offline "
        "publication command");
    expect(
        missing_output.str().empty(),
        "missing AQ19 artifact emitted partial session JSON");

    const CorruptAq19Artifact corrupt;
    pilot.aq19_bilinear_artifact_path =
        corrupt.path().string();
    std::istringstream corrupt_input(
        passive_responses(1));
    std::ostringstream corrupt_output;
    bool corrupt_rejected = false;
    try {
        static_cast<void>(
            old_school::web::run_bridge_session(
                corrupt_input, corrupt_output, pilot));
    } catch (const std::runtime_error&) {
        corrupt_rejected = true;
    }
    expect(
        corrupt_rejected,
        "corrupt AQ19 artifact was accepted");
    expect(
        corrupt_output.str().empty(),
        "corrupt AQ19 artifact emitted partial session JSON");
}

void test_g0_status_exposes_actual_model_identity() {
    auto config = fast_config();
    config.opponent_bot = old_school::BotKind::Learned;
    config.learned_variant =
        old_school::LearnedVariant::ValueSearchChampion;
    config.learned_generations = 0;
    config.training_games = 1;
    config.learned_rollouts = 1;

    std::istringstream input(passive_responses(5000));
    std::ostringstream output;
    expect(old_school::web::run_bridge_session(
               input, output, config) == 0,
           "G0 identity session did not complete");
    const std::string transcript = output.str();
    expect(transcript.find(
               "\"family\":\"learned-value\","
               "\"generation\":0,\"searchWorlds\":1,"
               "\"horizonTurns\":4,"
               "\"source\":\"trained-for-match\","
               "\"fingerprint\":\"") !=
               std::string::npos,
           "G0 status omitted structured model identity");
}

void test_passive_client_reaches_a_terminal_result() {
    std::istringstream input(passive_responses(5000));
    std::ostringstream output;
    const int result = old_school::web::run_bridge_session(
        input, output, fast_config());
    const std::string transcript = output.str();
    expect(result == 0, "bridge session did not return success");
    expect(transcript.find("\"type\":\"decision\"") !=
               std::string::npos,
           "bridge emitted no authoritative decision");
    expect(transcript.find("\"options\":[") !=
               std::string::npos,
           "priority decision omitted its legal options");
    expect(transcript.find("\"type\":\"game_over\"") !=
               std::string::npos,
           "passive session never emitted game_over");
    expect(transcript.find("\"revealedHand\"") ==
               std::string::npos,
           "normal session leaked the opponent hand");
}

void test_invalid_legal_option_is_rejected() {
    std::istringstream input(
        "{\"decisionId\":1,\"index\":999,"
        "\"ids\":[],\"pairs\":[]}\n");
    std::ostringstream output;
    bool rejected = false;
    try {
        static_cast<void>(old_school::web::run_bridge_session(
            input, output, fast_config()));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected,
           "out-of-range priority option must be rejected");
}

void test_debug_reveal_is_explicit() {
    auto config = fast_config();
    config.reveal_opponent_hand = true;
    std::istringstream input(passive_responses(5000));
    std::ostringstream output;
    static_cast<void>(old_school::web::run_bridge_session(
        input, output, config));
    expect(output.str().find("\"revealedHand\"") !=
               std::string::npos,
           "debug session did not expose its labeled reveal");
}

old_school::web::BridgeConfig cleanup_config() {
    auto config = fast_config();
    config.human_deck = old_school::DeckId::Blue;
    config.opponent_deck = old_school::DeckId::Green;
    config.opponent_bot = old_school::BotKind::Random;
    return config;
}

std::string complete_transcript(
    const old_school::web::BridgeConfig& config,
    std::size_t priority_index = 0) {
    std::istringstream input(
        indexed_responses(5000, priority_index));
    std::ostringstream output;
    expect(old_school::web::run_bridge_session(
               input, output, config) == 0,
           "bridge session did not complete");
    return output.str();
}

void test_custom_decks_are_exact_session_inputs() {
    auto named = fast_config();
    named.human_deck = old_school::DeckId::Green;
    named.opponent_deck = old_school::DeckId::Red;

    auto custom = named;
    custom.human_deck = old_school::DeckId::RUAggro;
    custom.opponent_deck = old_school::DeckId::White;
    custom.human_deck_cards = old_school::green_deck();
    custom.opponent_deck_cards = old_school::red_deck();

    expect(
        complete_transcript(custom) ==
            complete_transcript(named),
        "custom vectors were not the exact session decks");
}

void test_invalid_programmatic_custom_deck_fails_closed() {
    for (const std::vector<old_school::CardId>& invalid : {
             std::vector<old_school::CardId>(
                 39, old_school::CardId::Forest),
             std::vector<old_school::CardId>(
                 40, static_cast<old_school::CardId>(
                         old_school::kCardCount)),
         }) {
        auto config = fast_config();
        config.human_deck_cards = invalid;
        std::istringstream input(passive_responses(1));
        std::ostringstream output;
        bool rejected = false;
        try {
            static_cast<void>(
                old_school::web::run_bridge_session(
                    input, output, config));
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        expect(rejected,
               "invalid programmatic custom deck was accepted");
        expect(output.str().empty(),
               "invalid custom deck emitted partial session JSON");
    }
}

old_school::DeckEvolutionSummary sample_evolution_summary() {
    old_school::DeckEvolutionSummary summary;
    summary.best.cards.insert(
        summary.best.cards.end(), 20,
        old_school::CardId::Forest);
    summary.best.cards.insert(
        summary.best.cards.end(), 20,
        old_school::CardId::GrizzlyBears);
    summary.best.total = {
        .games = 20,
        .wins = 12,
        .losses = 6,
        .draws = 2,
    };
    summary.best.by_opponent = {{
        {.games = 4, .wins = 1, .losses = 2, .draws = 1},
        {.games = 4, .wins = 2, .losses = 2, .draws = 0},
        {.games = 4, .wins = 2, .losses = 1, .draws = 1},
        {.games = 4, .wins = 3, .losses = 1, .draws = 0},
        {.games = 4, .wins = 4, .losses = 0, .draws = 0},
    }};
    summary.generation_best_win_rates = {55.0, 60.0};
    return summary;
}

void test_evolution_json_is_complete_and_deterministic() {
    const old_school::web::EvolutionJsonConfig config = {
        .generations = 2,
        .population = 5,
        .games_per_opponent = 1,
        .seed = 987654321,
        .pilot =
            old_school::web::EvolutionPilot::LearnedValueC16,
        .learned_rollouts = 8,
    };
    const auto summary = sample_evolution_summary();
    std::ostringstream first;
    std::ostringstream second;
    old_school::web::write_evolution_json(
        first, summary, config);
    old_school::web::write_evolution_json(
        second, summary, config);
    const std::string json = first.str();
    expect(json == second.str(),
           "evolution JSON serialization is not deterministic");
    expect(
        json.find(
            "{\"type\":\"evolution_result\","
            "\"schemaVersion\":1,\"seed\":\"987654321\","
            "\"pilot\":\"learned-value-c16\"") == 0,
        "evolution JSON omitted seed, schema, or pilot");
    expect(
        json.find(
            "\"parameters\":{\"generations\":2,"
            "\"population\":5,\"gamesPerOpponent\":1,"
            "\"learnedRollouts\":8}") != std::string::npos,
        "evolution JSON omitted its exact parameters");
    expect(
        json.find(
            "\"cards\":[{\"id\":0,\"name\":\"Forest\","
            "\"count\":20},{\"id\":2,"
            "\"name\":\"Grizzly Bears\",\"count\":20}]") !=
            std::string::npos,
        "evolution JSON manifest omitted numeric IDs, names, or "
        "counts");
    expect(
        json.find(
            "\"fitness\":{\"games\":20,\"wins\":12,"
            "\"losses\":6,\"draws\":2,\"winRate\":60}") !=
            std::string::npos,
        "evolution JSON omitted aggregate fitness");
    expect(
        json.find("\"deck\":\"ru-aggro\","
                  "\"name\":\"RU Aggro\"") !=
            std::string::npos,
        "evolution JSON omitted a metagame opponent");
    expect(
        json.find(
            "\"generationBestWinRates\":[55,60]}\\n") ==
            std::string::npos,
        "evolution JSON encoded its trailing newline as text");
    expect(
        !json.empty() && json.back() == '\n' &&
            json.find('\n') == json.size() - 1,
        "evolution result must be exactly one JSON line");

    auto maximum_seed_config = config;
    maximum_seed_config.seed =
        std::numeric_limits<std::uint64_t>::max();
    std::ostringstream maximum_seed_output;
    old_school::web::write_evolution_json(
        maximum_seed_output, summary, maximum_seed_config);
    expect(
        maximum_seed_output.str().find(
            "\"seed\":\"18446744073709551615\"") !=
            std::string::npos,
        "evolution JSON did not preserve a full uint64 seed");

    auto inconsistent = summary;
    inconsistent.best.total.wins = 11;
    inconsistent.best.total.losses = 7;
    std::ostringstream inconsistent_output;
    bool rejected_inconsistent = false;
    try {
        old_school::web::write_evolution_json(
            inconsistent_output, inconsistent, config);
    } catch (const std::invalid_argument&) {
        rejected_inconsistent = true;
    }
    expect(
        rejected_inconsistent && inconsistent_output.str().empty(),
        "evolution JSON accepted contradictory aggregate/opponent stats");
}

void test_learned_evolution_is_frozen_load_only() {
    old_school::web::EvolutionJsonConfig config = {
        .generations = 1,
        .population = 5,
        .games_per_opponent = 1,
        .seed = 42,
        .pilot =
            old_school::web::EvolutionPilot::LearnedValueC16,
        .learned_rollouts = 1,
        .frozen_c16_artifact_path =
            "build/model-cache/"
            "deliberately-missing-evolution-c16.bin",
    };
    std::ostringstream output;
    bool rejected = false;
    std::string message;
    try {
        static_cast<void>(
            old_school::web::run_evolution_json(
                output, config));
    } catch (const std::runtime_error& error) {
        rejected = true;
        message = error.what();
    }
    expect(rejected,
           "Learned evolution substituted or trained a missing C16");
    expect(
        message.find("missing, stale, or invalid") !=
            std::string::npos,
        "Learned evolution did not use the frozen load boundary");
    expect(output.str().empty(),
           "failed Learned evolution emitted partial JSON");
}

std::string sole_pass_decision(std::string_view phase) {
    return "\"kind\":\"priority\",\"phase\":\"" +
           std::string(phase) +
           "\",\"options\":[{\"index\":0,"
           "\"label\":\"Pass priority\",\"kind\":\"pass\"}]}}";
}

void test_bluff_mode_emits_only_otherwise_forced_passes() {
    constexpr std::string_view kOnePassOption =
        "\"options\":[{\"index\":0,\"label\":\"Pass priority\","
        "\"kind\":\"pass\"}]}}";
    auto normal_config = cleanup_config();
    expect(!normal_config.bluff_mode,
           "bridge bluff mode must default off");
    const std::string normal =
        complete_transcript(normal_config);
    expect(normal.find(kOnePassOption) ==
               std::string::npos,
           "default bridge emitted a forced Pass decision");
    for (const std::string_view phase :
         {"first_main", "second_main"}) {
        expect(normal.find(sole_pass_decision(phase)) ==
                   std::string::npos,
               "default bridge exposed a forced main-phase Pass");
    }

    auto bluff_config = normal_config;
    bluff_config.bluff_mode = true;
    const std::string bluff =
        complete_transcript(bluff_config);
    expect(bluff.find(kOnePassOption) !=
               std::string::npos,
           "bluff bridge omitted its one-option Pass decision");
    for (const std::string_view phase :
         {"first_main", "second_main"}) {
        expect(bluff.find(sole_pass_decision(phase)) !=
                   std::string::npos,
               "bluff bridge omitted a forced main-phase Pass");
    }
}

void test_legal_millstone_activation_prevents_auto_pass() {
    auto config = fast_config();
    config.human_deck = old_school::DeckId::White;
    config.opponent_deck = old_school::DeckId::Green;
    config.opponent_bot = old_school::BotKind::Random;
    expect(!config.bluff_mode,
           "Millstone auto-pass fixture unexpectedly enabled Bluff");

    const std::string transcript =
        complete_transcript(config, 1);
    const std::size_t source =
        transcript.find("\"sourcePermanent\":");
    expect(source != std::string::npos,
           "legal Millstone activation was not offered");
    const std::size_t prior_newline =
        transcript.rfind('\n', source);
    const std::size_t line_start =
        prior_newline == std::string::npos
            ? 0
            : prior_newline + 1;
    const std::size_t next_newline =
        transcript.find('\n', source);
    const std::string_view decision_line(
        transcript.data() + line_start,
        (next_newline == std::string::npos
             ? transcript.size()
             : next_newline) -
            line_start);
    expect(decision_line.find("\"type\":\"decision\"") !=
               std::string_view::npos,
           "sourcePermanent was not exposed in a decision");
    expect(decision_line.find("\"kind\":\"priority\"") !=
               std::string_view::npos,
           "Millstone source was not a priority decision");
    expect(decision_line.find(
               "\"label\":\"Pass priority\",\"kind\":\"pass\"") !=
               std::string_view::npos,
           "Millstone decision omitted Pass");
    expect(decision_line.find(
               "\"kind\":\"activate_millstone\"") !=
               std::string_view::npos,
           "Millstone decision omitted its activation");
}

void test_same_seed_bridge_transcripts_are_byte_identical() {
    const auto normal_config = cleanup_config();
    expect(complete_transcript(normal_config) ==
               complete_transcript(normal_config),
           "same-seed normal transcripts differ");

    auto bluff_config = normal_config;
    bluff_config.bluff_mode = true;
    expect(complete_transcript(bluff_config) ==
               complete_transcript(bluff_config),
           "same-seed Bluff transcripts differ");
}

void test_priority_events_expose_structured_action_kinds() {
    const std::string transcript =
        complete_transcript(cleanup_config());
    std::size_t cursor = 0;
    std::size_t priority_events = 0;
    while ((cursor = transcript.find(
                "\"kind\":\"priority_action\"",
                cursor)) != std::string::npos) {
        const std::size_t line_end =
            transcript.find('\n', cursor);
        const std::string_view line(
            transcript.data() + cursor,
            (line_end == std::string::npos
                 ? transcript.size()
                 : line_end) -
                cursor);
        expect(line.find("\"actionKind\":") !=
                   std::string_view::npos,
               "priority event omitted its actionKind");
        ++priority_events;
        cursor = line_end == std::string::npos
                     ? transcript.size()
                     : line_end + 1;
    }
    expect(priority_events != 0,
           "structured action-kind fixture emitted no priority events");
    expect(transcript.find("\"actionKind\":\"pass\"") !=
               std::string::npos,
           "public priority events omitted the pass action kind");
}

void test_cleanup_decision_and_public_event_are_emitted() {
    std::istringstream input(passive_responses(5000));
    std::ostringstream output;
    const int result = old_school::web::run_bridge_session(
        input, output, cleanup_config());
    const std::string transcript = output.str();
    expect(result == 0, "cleanup bridge session failed");
    expect(transcript.find(
               "\"kind\":\"cleanup_discard\",\"count\":1,"
               "\"options\":[{\"index\":0,\"card\":") !=
               std::string::npos,
           "cleanup decision omitted indexed hand options");
    expect(transcript.find(
               "\"kind\":\"cards_discarded\"") !=
               std::string::npos,
           "cleanup omitted its public discard event");
    expect(transcript.find(
               "\"kind\":\"cards_discarded\"") <
               transcript.find("\"type\":\"game_over\""),
           "cleanup event was not emitted before game over");
}

void test_cleanup_array_syntax_is_strict() {
    for (const std::string_view malformed : {
             "[-1]",
             "[\"0\"]",
             "[0.5]",
             "[0]junk",
         }) {
        std::istringstream input(
            responses_with_cleanup_value(5000, malformed));
        std::ostringstream output;
        bool rejected = false;
        try {
            static_cast<void>(
                old_school::web::run_bridge_session(
                    input, output, cleanup_config()));
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        expect(
            output.str().find(
                "\"kind\":\"cleanup_discard\"") !=
                std::string::npos,
            "malformed cleanup fixture never reached cleanup");
        expect(rejected,
               "malformed cleanup integer array was accepted");
        expect(output.str().find(
                   "\"kind\":\"cards_discarded\"") ==
                   std::string::npos,
               "malformed cleanup mutated public game state");
    }
}

} // namespace

int main() {
    TestRunner runner;
    runner.run("strict deck and policy names",
               test_names_parse_strictly);
    runner.run("exact custom deck transport",
               test_exact_custom_deck_transport_is_strict);
    runner.run("frozen C16 failure is actionable",
               test_frozen_c16_load_boundary_fails_actionably);
    runner.run("C16 training identity is canonical",
               test_c16_rejects_noncanonical_training_identity);
    runner.run("adversarial challenger changes only attacks",
               test_adversarial_challenger_maps_only_the_attack_flag);
    runner.run("AQ4 actor-local pilot maps exactly",
               test_actor_local_search_maps_only_the_exact_pilot_flag);
    runner.run("AQ19 bilinear pilot maps exactly",
               test_aq19_maps_only_the_bilinear_residual);
    runner.run("G0 status exposes model identity",
               test_g0_status_exposes_actual_model_identity);
    runner.run("passive client reaches terminal result",
               test_passive_client_reaches_a_terminal_result);
    runner.run("illegal option rejected",
               test_invalid_legal_option_is_rejected);
    runner.run("debug reveal is explicit",
               test_debug_reveal_is_explicit);
    runner.run("custom decks are exact session inputs",
               test_custom_decks_are_exact_session_inputs);
    runner.run("invalid custom decks fail closed",
               test_invalid_programmatic_custom_deck_fails_closed);
    runner.run("evolution JSON is complete",
               test_evolution_json_is_complete_and_deterministic);
    runner.run("Learned evolution is frozen load-only",
               test_learned_evolution_is_frozen_load_only);
    runner.run("cleanup decision and event",
               test_cleanup_decision_and_public_event_are_emitted);
    runner.run("cleanup array syntax is strict",
               test_cleanup_array_syntax_is_strict);
    runner.run("bluff mode emits forced Pass",
               test_bluff_mode_emits_only_otherwise_forced_passes);
    runner.run("Millstone prevents forced auto-pass",
               test_legal_millstone_activation_prevents_auto_pass);
    runner.run("same-seed bridge transcripts are exact",
               test_same_seed_bridge_transcripts_are_byte_identical);
    runner.run("priority events expose action kinds",
               test_priority_events_expose_structured_action_kinds);
    return runner.finish();
}
