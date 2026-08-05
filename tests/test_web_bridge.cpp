#include "old_school/web_bridge.hpp"

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

// Fixture decks: 40-card inline lists (formerly the retired synthetic
// metagame decks), used as custom-deck session inputs. Test scaffolding
// only; they are not part of the six-deck metagame.
std::vector<old_school::CardId> fixture_green_deck() {
    using old_school::CardId;
    std::vector<CardId> deck(16, CardId::Forest);
    deck.insert(deck.end(), 4, CardId::LlanowarElves);
    deck.insert(deck.end(), 6, CardId::GrizzlyBears);
    deck.insert(deck.end(), 2, CardId::IronrootTreefolk);
    deck.insert(deck.end(), 4, CardId::MossBeast);
    deck.insert(deck.end(), 4, CardId::ForestColossus);
    deck.insert(deck.end(), 4, CardId::GiantGrowth);
    return deck;
}

std::vector<old_school::CardId> fixture_red_deck() {
    using old_school::CardId;
    std::vector<CardId> deck(15, CardId::Mountain);
    deck.insert(deck.end(), 9, CardId::LightningBolt);
    deck.insert(deck.end(), 7, CardId::IronclawOrcs);
    deck.insert(deck.end(), 4, CardId::GrayOgre);
    deck.insert(deck.end(), 3, CardId::HillGiant);
    deck.insert(deck.end(), 2, CardId::FireElemental);
    return deck;
}

std::vector<old_school::CardId> fixture_blue_deck() {
    using old_school::CardId;
    std::vector<CardId> deck(15, CardId::Island);
    deck.push_back(CardId::MoxSapphire);
    deck.push_back(CardId::SolRing);
    deck.push_back(CardId::AncestralRecall);
    deck.push_back(CardId::TimeWalk);
    deck.push_back(CardId::Braingeyser);
    deck.insert(deck.end(), 4, CardId::FlyingMen);
    deck.insert(deck.end(), 4, CardId::ForceSpike);
    deck.insert(deck.end(), 8, CardId::Counterspell);
    deck.insert(deck.end(), 4, CardId::AirElemental);
    return deck;
}

std::vector<old_school::CardId> fixture_white_control_deck() {
    using old_school::CardId;
    std::vector<CardId> deck(22, CardId::Plains);
    deck.insert(deck.end(), 3, CardId::Millstone);
    deck.insert(deck.end(), 15, CardId::Moat);
    return deck;
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
        .human_deck = old_school::DeckId::RGBerserk,
        .opponent_deck = old_school::DeckId::BRMidrange,
        .opponent_bot = old_school::BotKind::Handcrafted,
        .game_seed = 42,
        .monte_carlo_rollouts = 1,
        .deep_monte_carlo_rollouts = 1,
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
    using old_school::web::parse_deck_id;
    using old_school::web::parse_evolution_pilot;
    using old_school::web::parse_opponent_bot;

    expect(parse_deck_id("rg-berserk") == DeckId::RGBerserk,
           "rg-berserk deck did not parse");
    expect(parse_deck_id("white-weenie") == DeckId::WhiteWeenie,
           "white-weenie deck did not parse");
    expect(parse_opponent_bot("random") == BotKind::Random,
           "random policy did not parse");
    expect(parse_opponent_bot("monte-carlo") ==
               BotKind::MonteCarlo,
           "monte-carlo policy did not parse");
    expect(parse_opponent_bot("deep-monte-carlo") ==
               BotKind::DeepMonteCarlo,
           "deep-monte-carlo policy did not parse");
    expect(parse_opponent_bot("handcrafted") ==
               BotKind::Handcrafted,
           "handcrafted policy did not parse");
    bool bad_policy = false;
    try {
        static_cast<void>(
            parse_opponent_bot("learned-value-c16"));
    } catch (const std::invalid_argument&) {
        bad_policy = true;
    }
    expect(bad_policy,
           "removed learned policy must be rejected");

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
    bool bad_pilot = false;
    try {
        static_cast<void>(
            parse_evolution_pilot("learned-value-c16"));
    } catch (const std::invalid_argument&) {
        bad_pilot = true;
    }
    expect(
        bad_pilot,
        "removed learned evolution pilot must be rejected");
    bad_pilot = false;
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
    const auto deck = fixture_blue_deck();
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
    // The draw-heavy blue fixture list overfills the human hand.
    config.human_deck_cards = fixture_blue_deck();
    config.opponent_deck_cards = fixture_green_deck();
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
    // Two sessions with identical custom vectors but different named
    // fallbacks: equal transcripts prove the custom vectors, not the
    // named decks, are the exact session decks.
    auto custom = fast_config();
    custom.human_deck = old_school::DeckId::RGBerserk;
    custom.opponent_deck = old_school::DeckId::UWR;
    custom.human_deck_cards = fixture_green_deck();
    custom.opponent_deck_cards = fixture_red_deck();

    auto refallbacked = custom;
    refallbacked.human_deck = old_school::DeckId::WhiteWeenie;
    refallbacked.opponent_deck = old_school::DeckId::Robots;

    expect(
        complete_transcript(custom) ==
            complete_transcript(refallbacked),
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
            old_school::web::EvolutionPilot::Handcrafted,
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
            "\"pilot\":\"handcrafted\"") == 0,
        "evolution JSON omitted seed, schema, or pilot");
    expect(
        json.find(
            "\"parameters\":{\"generations\":2,"
            "\"population\":5,\"gamesPerOpponent\":1}") !=
            std::string::npos,
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
        json.find("\"deck\":\"white-weenie\","
                  "\"name\":\"White Weenie\"") !=
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
    // Millstone lives in the pool but no metagame deck; the fixture
    // list keeps the activation coverage.
    config.human_deck_cards = fixture_white_control_deck();
    config.opponent_deck_cards = fixture_green_deck();
    config.opponent_bot = old_school::BotKind::Random;
    expect(!config.bluff_mode,
           "Millstone auto-pass fixture unexpectedly enabled Bluff");

    const std::string transcript =
        complete_transcript(config, 1);
    // Anchor on the activation itself: mana taps also carry
    // sourcePermanent, so that field no longer identifies Millstone.
    const std::size_t source =
        transcript.find("\"kind\":\"activate_millstone\"");
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
    expect(decision_line.find("\"sourcePermanent\":") !=
               std::string_view::npos,
           "Millstone activation omitted its source permanent");
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
