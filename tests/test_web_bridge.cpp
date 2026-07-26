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
    runner.run("passive client reaches terminal result",
               test_passive_client_reaches_a_terminal_result);
    runner.run("illegal option rejected",
               test_invalid_legal_option_is_rejected);
    runner.run("debug reveal is explicit",
               test_debug_reveal_is_explicit);
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
    return runner.finish();
}
