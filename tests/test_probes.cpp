#include "old_school/probes.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using old_school::CardId;
using old_school::DeckId;
using old_school::PriorityAction;
using old_school::probes::Category;
using old_school::probes::DecisionProbe;
using old_school::probes::Validation;

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
            std::cerr << failed_ << " test(s) failed; " << passed_
                      << " passed\n";
            return 1;
        }
        std::cout << passed_ << " probe tests passed across 16 fixtures\n";
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

const DecisionProbe& find_probe(
    const std::vector<DecisionProbe>& probes, Category category) {
    const auto found = std::find_if(
        probes.begin(), probes.end(),
        [category](const DecisionProbe& probe) {
            return probe.category == category;
        });
    if (found == probes.end()) {
        throw std::runtime_error("required probe category is missing");
    }
    return *found;
}

std::string validation_errors(const Validation& validation) {
    std::string joined;
    for (const std::string& error : validation.errors) {
        if (!joined.empty()) {
            joined += "; ";
        }
        joined += error;
    }
    return joined;
}

void test_corpus_shape_and_candidate_schema() {
    const std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v1();
    expect(probes.size() == 16,
           "probe-dev-v1 must contain exactly 16 fixtures");

    constexpr std::array<std::size_t, 16> kCandidateCounts = {
        2, 3, 2, 2, 4, 5, 4, 3,
        2, 2, 2, 3, 2, 2, 3, 3,
    };
    std::array<std::size_t, 4> deck_counts{};
    for (const DecisionProbe& probe : probes) {
        const std::size_t category =
            static_cast<std::size_t>(probe.category);
        expect(category < kCandidateCounts.size(),
               "category is outside probe-dev-v1");
        expect(probe.candidates.size() ==
                   kCandidateCounts[category],
               "fixture candidate count changed");
        ++deck_counts[static_cast<std::size_t>(probe.root_deck)];
        for (const auto& candidate : probe.candidates) {
            expect(!candidate.descriptor.empty(),
                   "candidate descriptor must be present");
        }
    }
    for (const std::size_t count : deck_counts) {
        expect(count == 4, "each root deck needs four probes");
    }
}

void test_every_probe_passes_each_validation_dimension() {
    const std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v1();
    for (const DecisionProbe& probe : probes) {
        const Validation validation =
            old_school::probes::validate_probe(probe);
        if (!validation.ok()) {
            throw std::runtime_error(
                probe.stable_id + ": " +
                validation_errors(validation));
        }
        expect(validation.exact_card_conservation,
               "exact card conservation was not checked");
        expect(validation.candidates_legal_and_complete,
               "candidate completeness was not checked");
        expect(validation.reachable_state,
               "reachability was not checked");
        expect(validation.hidden_clone_invariant,
               "hidden clone invariance was not checked");
    }
    expect(old_school::probes::validate_probe_dev_v1(probes).empty(),
           "valid corpus failed aggregate validation");
}

void test_card_conservation_rejects_missing_physical_card() {
    std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v1();
    DecisionProbe probe =
        find_probe(probes, Category::GreenDevelop);
    expect(!probe.state.players[0].library.empty(),
           "test fixture unexpectedly has an empty library");
    probe.state.players[0].library.pop_back();
    const Validation validation =
        old_school::probes::validate_probe(probe);
    expect(!validation.exact_card_conservation,
           "missing physical card was accepted");
}

void test_exile_is_a_conserved_public_zone() {
    std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v1();
    DecisionProbe probe =
        find_probe(probes, Category::GreenDevelop);
    expect(!probe.state.players[0].library.empty(),
           "test fixture unexpectedly has an empty library");
    const CardId exiled = probe.state.players[0].library.back();
    probe.state.players[0].library.pop_back();
    probe.state.players[0].exile.push_back(exiled);

    const Validation validation =
        old_school::probes::validate_probe(probe);
    expect(validation.ok(),
           "moving a physical card to public exile broke validation");
}

void test_ru_probe_is_rejected_until_ru_corpus_exists() {
    std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v1();
    DecisionProbe probe =
        find_probe(probes, Category::GreenDevelop);
    probe.root_deck = DeckId::RUAggro;

    const Validation validation =
        old_school::probes::validate_probe(probe);
    expect(!validation.exact_card_conservation,
           "RU probe was accepted by the four-deck corpus");
    expect(
        std::any_of(
            validation.errors.begin(), validation.errors.end(),
            [](const std::string& error) {
                return error.find(
                           "RU Aggro decision probes have not been authored") !=
                       std::string::npos;
            }),
        "RU rejection did not explain that its probes are not authored");
}

void test_priority_validation_rejects_illegal_or_incomplete_set() {
    const std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v1();
    DecisionProbe probe =
        find_probe(probes, Category::GreenDevelop);
    probe.candidates[1].action =
        PriorityAction::cast_creature(CardId::IronrootTreefolk);
    const Validation validation =
        old_school::probes::validate_probe(probe);
    expect(!validation.candidates_legal_and_complete,
           "unpayable Treefolk replaced a legal action");
}

void test_attack_reachability_rejects_sickness_and_moat() {
    const std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v1();
    DecisionProbe sick =
        find_probe(probes, Category::GreenFavorableAttack);
    sick.state.players[0].creatures[0].summoning_sick = true;
    expect(!old_school::probes::validate_probe(sick).reachable_state,
           "summoning-sick binary attacker was accepted");

    DecisionProbe moated =
        find_probe(probes, Category::GreenFavorableAttack);
    moated.state.players[1].enchantments.push_back(CardId::Moat);
    expect(!old_school::probes::validate_probe(moated).reachable_state,
           "nonflying binary attacker was accepted through Moat");
}

void test_hidden_zone_clones_are_observation_invariant() {
    const std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v1();
    for (std::size_t index = 0; index < probes.size(); ++index) {
        expect(
            old_school::probes::hidden_clone_is_determinization_invariant(
                probes[index],
                old_school::probes::kProbeValidationSeed + index),
            "hidden repartition changed sampled information set");
    }
}

void test_red_damaged_threat_fixture_is_reachable() {
    const std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v1();
    const DecisionProbe& probe =
        find_probe(probes, Category::RedFinishDamagedThreat);
    const auto& root = probe.state.players[probe.root_player];
    const auto& opponent = probe.state.players[1 - probe.root_player];
    expect(root.hand == std::vector<CardId>{CardId::LightningBolt},
           "R3 must retain the second Bolt");
    expect(std::count(root.graveyard.begin(), root.graveyard.end(),
                      CardId::LightningBolt) == 1,
           "R3 must put the resolved first Bolt in the graveyard");
    expect(root.lands.size() == 2,
           "R3 must expose two Mountains");
    expect(std::count_if(
               root.lands.begin(), root.lands.end(),
               [](const old_school::LandPermanent& land) {
                   return land.tapped;
               }) == 1,
           "R3 must leave one Mountain after the first Bolt");
    expect(opponent.creatures.size() == 1 &&
               opponent.creatures[0].card ==
                   CardId::WaterElemental &&
               opponent.creatures[0].damage == 3,
           "R3 must retain a live Water Elemental with 3 damage");
    expect(old_school::probes::validate_probe(probe).reachable_state,
           "corrected R3 fixture failed reachability validation");
}

void test_counter_war_lists_every_legal_spell_target() {
    const std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v1();
    const DecisionProbe& probe =
        find_probe(probes, Category::BlueCounterWar);
    expect(probe.state.stack.size() == 2,
           "counter-war stack must contain two spells");
    expect(probe.consecutive_passes == 1,
           "counter-war responder must act after the caster passes");
    const Validation validation =
        old_school::probes::validate_probe(probe);
    expect(validation.candidates_legal_and_complete,
           "counter-war omitted a targetable stack spell");
    expect(probe.candidates.size() == 3,
           "counter-war requires pass plus two Counterspell targets");
}

void test_response_windows_record_the_casters_pass() {
    const std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v1();
    constexpr std::array<Category, 5> kResponseCategories = {
        Category::RedStackRace,
        Category::BlueCounterExpensiveSpell,
        Category::BlueConserveCounter,
        Category::BlueCounterLethal,
        Category::BlueCounterWar,
    };
    for (const Category category : kResponseCategories) {
        const DecisionProbe& probe = find_probe(probes, category);
        expect(!probe.state.stack.empty(),
               "response fixture must have a stack object");
        expect(probe.state.stack.back().controller !=
                   probe.root_player,
               "response fixture root cannot control the top spell");
        expect(probe.consecutive_passes == 1,
               "responder must receive priority after one pass");
    }
}

void test_corpus_rejects_duplicate_stable_id_and_category() {
    std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v1();
    probes[1].stable_id = probes[0].stable_id;
    probes[1].category = probes[0].category;
    expect(!old_school::probes::validate_probe_dev_v1(probes).empty(),
           "duplicate stable ID/category was accepted");
}

void test_v2_plan_probes_are_root_irreversible() {
    const std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v2();
    expect(old_school::probes::validate_probe_dev_v2(probes).empty(),
           "probe-dev-v2 failed aggregate validation");
    for (const DecisionProbe& probe : probes) {
        expect(probe.stable_id.ends_with(".v2"),
               "probe-dev-v2 retained a v1 stable ID");
    }

    const DecisionProbe& green =
        find_probe(probes, Category::GreenDevelop);
    expect(green.phase == old_school::TurnPhase::SecondMain,
           "Green develop Pass can still heal in a later main phase");
    expect(std::all_of(
               green.state.players[1].lands.begin(),
               green.state.players[1].lands.end(),
               [](const old_school::LandPermanent& land) {
                   return land.tapped;
               }),
           "Green opponent can reopen the final priority window");

    const DecisionProbe& red =
        find_probe(probes, Category::RedFaceLethal);
    expect(red.phase == old_school::TurnPhase::SecondMain,
           "Red lethal Pass can still heal in a later main phase");
    old_school::GameState pass_state = red.state;
    old_school::PriorityState pass_priority_state{
        .player = red.root_player,
        .consecutive_passes = red.consecutive_passes,
    };
    expect(old_school::pass_priority(pass_state, pass_priority_state) ==
               old_school::PriorityPassResult::Passed,
           "first Red Pass did not yield priority");
    const auto opponent_actions = old_school::legal_priority_actions(
        pass_state, pass_priority_state.player, true);
    expect(opponent_actions ==
               std::vector<PriorityAction>{PriorityAction::pass()},
           "tapped-out opponent can reopen the Red branch");
    expect(old_school::pass_priority(pass_state, pass_priority_state) ==
               old_school::PriorityPassResult::WindowEnded,
           "Red Pass did not end the final main-phase window");
    expect(pass_state.players[0].hand ==
               std::vector<CardId>{CardId::LightningBolt},
           "Red Pass branch unexpectedly spent the Bolt");

    const DecisionProbe& emergency =
        find_probe(probes, Category::WhiteEmergencyMoat);
    expect(emergency.state.players[1].creatures.size() == 1 &&
               std::all_of(
                   emergency.state.players[1].creatures.begin(),
                   emergency.state.players[1].creatures.end(),
                   [](const old_school::CreaturePermanent& creature) {
                       return creature.card ==
                                  CardId::FireElemental &&
                              !creature.summoning_sick;
                   }),
           "emergency-Moat attacker is not an immediate threat");
}

void test_v2_lethal_priority_branches_apply_exactly() {
    const std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v2();

    const DecisionProbe& red =
        find_probe(probes, Category::RedFaceLethal);
    old_school::GameState bolt_state = red.state;
    const auto& bolt_action =
        std::get<PriorityAction>(red.candidates[2].action);
    expect(old_school::apply_priority_action(
               bolt_state, red.root_player, bolt_action, true),
           "Red lethal Bolt candidate failed to apply");
    old_school::PriorityState bolt_priority{
        .player = red.root_player,
        .consecutive_passes = 0,
    };
    expect(old_school::pass_priority(bolt_state, bolt_priority) ==
               old_school::PriorityPassResult::Passed &&
               old_school::pass_priority(bolt_state, bolt_priority) ==
                   old_school::PriorityPassResult::StackObjectResolved,
           "Red lethal Bolt did not resolve after two passes");
    expect(bolt_state.players[1].life == 0,
           "Red lethal Bolt did not produce its terminal branch");

    const DecisionProbe& blue =
        find_probe(probes, Category::BlueCounterLethal);
    old_school::GameState blue_pass = blue.state;
    old_school::PriorityState blue_pass_priority{
        .player = blue.root_player,
        .consecutive_passes = blue.consecutive_passes,
    };
    expect(old_school::pass_priority(
               blue_pass, blue_pass_priority) ==
               old_school::PriorityPassResult::StackObjectResolved,
           "Blue Pass did not resolve the pending lethal Bolt");
    expect(blue_pass.players[0].life == 0,
           "Blue Pass was not an immediate terminal loss");

    old_school::GameState blue_counter = blue.state;
    const auto& counter_action =
        std::get<PriorityAction>(blue.candidates[1].action);
    expect(old_school::apply_priority_action(
               blue_counter, blue.root_player, counter_action, false),
           "Blue Counterspell candidate failed to apply");
    expect(blue_counter.stack.size() == 2 &&
               blue_counter.stack.back().card ==
                   CardId::Counterspell,
           "Counterspell was not placed above the lethal Bolt");
    old_school::PriorityState counter_priority{
        .player = blue.root_player,
        .consecutive_passes = 0,
    };
    expect(old_school::pass_priority(
               blue_counter, counter_priority) ==
               old_school::PriorityPassResult::Passed &&
               old_school::pass_priority(
                   blue_counter, counter_priority) ==
                   old_school::PriorityPassResult::StackObjectResolved,
           "Counterspell did not resolve after two passes");
    expect(blue_counter.stack.empty() &&
               blue_counter.players[0].life == 3,
           "Counterspell failed to remove the lethal Bolt");
    expect(std::count(
               blue_counter.players[0].graveyard.begin(),
               blue_counter.players[0].graveyard.end(),
               CardId::Counterspell) == 1 &&
               std::count(
                   blue_counter.players[1].graveyard.begin(),
                   blue_counter.players[1].graveyard.end(),
                   CardId::LightningBolt) == 1 &&
               blue_counter.stats[0].spells_countered == 1,
           "Counterspell resolution did not preserve stack accounting");
}

} // namespace

int main() {
    TestRunner runner;
    runner.run("corpus shape and candidate schema",
               test_corpus_shape_and_candidate_schema);
    runner.run("all fixtures validate",
               test_every_probe_passes_each_validation_dimension);
    runner.run("exact physical conservation",
               test_card_conservation_rejects_missing_physical_card);
    runner.run("public exile conservation",
               test_exile_is_a_conserved_public_zone);
    runner.run("RU corpus is explicitly absent",
               test_ru_probe_is_rejected_until_ru_corpus_exists);
    runner.run("priority legality and completeness",
               test_priority_validation_rejects_illegal_or_incomplete_set);
    runner.run("attack legality and Moat",
               test_attack_reachability_rejects_sickness_and_moat);
    runner.run("hidden-zone clone invariance",
               test_hidden_zone_clones_are_observation_invariant);
    runner.run("corrected reachable red R3",
               test_red_damaged_threat_fixture_is_reachable);
    runner.run("Counterspell stack targets",
               test_counter_war_lists_every_legal_spell_target);
    runner.run("response priority context",
               test_response_windows_record_the_casters_pass);
    runner.run("unique stable IDs and categories",
               test_corpus_rejects_duplicate_stable_id_and_category);
    runner.run("v2 root-irreversible plans",
               test_v2_plan_probes_are_root_irreversible);
    runner.run("v2 lethal branch traces",
               test_v2_lethal_priority_branches_apply_exactly);
    return runner.finish();
}
