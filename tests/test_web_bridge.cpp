#include "old_school/web_bridge.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

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

std::string passive_responses(std::size_t count) {
    std::ostringstream responses;
    for (std::size_t decision = 1;
         decision <= count; ++decision) {
        responses
            << "{\"decisionId\":" << decision
            << ",\"index\":0,\"ids\":[],\"pairs\":[]}\n";
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

void test_names_parse_strictly() {
    using old_school::BotKind;
    using old_school::DeckId;
    using old_school::LearnedVariant;
    using old_school::web::parse_deck_id;
    using old_school::web::parse_opponent_bot;

    expect(parse_deck_id("green") == DeckId::Green,
           "green deck did not parse");
    expect(parse_deck_id("ru-aggro") == DeckId::RUAggro,
           "RU Aggro deck did not parse");
    LearnedVariant variant =
        LearnedVariant::UnifiedActor;
    expect(parse_opponent_bot("learned-value", variant) ==
               BotKind::Learned &&
               variant ==
                   LearnedVariant::ValueSearchChampion,
           "Learned Value did not parse");
    expect(parse_opponent_bot("learned-actor", variant) ==
               BotKind::Learned &&
               variant == LearnedVariant::UnifiedActor,
           "Learned Actor did not parse");

    bool bad_deck = false;
    try {
        static_cast<void>(parse_deck_id("vintage"));
    } catch (const std::invalid_argument&) {
        bad_deck = true;
    }
    expect(bad_deck, "unknown deck must be rejected");
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

} // namespace

int main() {
    TestRunner runner;
    runner.run("strict deck and policy names",
               test_names_parse_strictly);
    runner.run("passive client reaches terminal result",
               test_passive_client_reaches_a_terminal_result);
    runner.run("illegal option rejected",
               test_invalid_legal_option_is_rejected);
    runner.run("debug reveal is explicit",
               test_debug_reveal_is_explicit);
    return runner.finish();
}
