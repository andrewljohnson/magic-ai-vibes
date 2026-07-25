#include "alpha/probes.hpp"

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

using alpha::CardId;
using alpha::DeckId;
using alpha::PriorityAction;
using alpha::probes::Category;
using alpha::probes::DecisionProbe;
using alpha::probes::Validation;

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
        alpha::probes::make_probe_dev_v1();
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
        alpha::probes::make_probe_dev_v1();
    for (const DecisionProbe& probe : probes) {
        const Validation validation =
            alpha::probes::validate_probe(probe);
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
    expect(alpha::probes::validate_probe_dev_v1(probes).empty(),
           "valid corpus failed aggregate validation");
}

void test_card_conservation_rejects_missing_physical_card() {
    std::vector<DecisionProbe> probes =
        alpha::probes::make_probe_dev_v1();
    DecisionProbe probe =
        find_probe(probes, Category::GreenDevelop);
    expect(!probe.state.players[0].library.empty(),
           "test fixture unexpectedly has an empty library");
    probe.state.players[0].library.pop_back();
    const Validation validation =
        alpha::probes::validate_probe(probe);
    expect(!validation.exact_card_conservation,
           "missing physical card was accepted");
}

void test_priority_validation_rejects_illegal_or_incomplete_set() {
    const std::vector<DecisionProbe> probes =
        alpha::probes::make_probe_dev_v1();
    DecisionProbe probe =
        find_probe(probes, Category::GreenDevelop);
    probe.candidates[1].action =
        PriorityAction::cast_creature(CardId::IronrootTreefolk);
    const Validation validation =
        alpha::probes::validate_probe(probe);
    expect(!validation.candidates_legal_and_complete,
           "unpayable Treefolk replaced a legal action");
}

void test_attack_reachability_rejects_sickness_and_moat() {
    const std::vector<DecisionProbe> probes =
        alpha::probes::make_probe_dev_v1();
    DecisionProbe sick =
        find_probe(probes, Category::GreenFavorableAttack);
    sick.state.players[0].creatures[0].summoning_sick = true;
    expect(!alpha::probes::validate_probe(sick).reachable_state,
           "summoning-sick binary attacker was accepted");

    DecisionProbe moated =
        find_probe(probes, Category::GreenFavorableAttack);
    moated.state.players[1].enchantments.push_back(CardId::Moat);
    expect(!alpha::probes::validate_probe(moated).reachable_state,
           "nonflying binary attacker was accepted through Moat");
}

void test_hidden_zone_clones_are_observation_invariant() {
    const std::vector<DecisionProbe> probes =
        alpha::probes::make_probe_dev_v1();
    for (std::size_t index = 0; index < probes.size(); ++index) {
        expect(
            alpha::probes::hidden_clone_is_determinization_invariant(
                probes[index],
                alpha::probes::kProbeValidationSeed + index),
            "hidden repartition changed sampled information set");
    }
}

void test_red_damaged_threat_fixture_is_reachable() {
    const std::vector<DecisionProbe> probes =
        alpha::probes::make_probe_dev_v1();
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
               [](const alpha::LandPermanent& land) {
                   return land.tapped;
               }) == 1,
           "R3 must leave one Mountain after the first Bolt");
    expect(opponent.creatures.size() == 1 &&
               opponent.creatures[0].card ==
                   CardId::WaterElemental &&
               opponent.creatures[0].damage == 3,
           "R3 must retain a live Water Elemental with 3 damage");
    expect(alpha::probes::validate_probe(probe).reachable_state,
           "corrected R3 fixture failed reachability validation");
}

void test_counter_war_lists_every_legal_spell_target() {
    const std::vector<DecisionProbe> probes =
        alpha::probes::make_probe_dev_v1();
    const DecisionProbe& probe =
        find_probe(probes, Category::BlueCounterWar);
    expect(probe.state.stack.size() == 2,
           "counter-war stack must contain two spells");
    expect(probe.consecutive_passes == 1,
           "counter-war responder must act after the caster passes");
    const Validation validation =
        alpha::probes::validate_probe(probe);
    expect(validation.candidates_legal_and_complete,
           "counter-war omitted a targetable stack spell");
    expect(probe.candidates.size() == 3,
           "counter-war requires pass plus two Counterspell targets");
}

void test_response_windows_record_the_casters_pass() {
    const std::vector<DecisionProbe> probes =
        alpha::probes::make_probe_dev_v1();
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
        alpha::probes::make_probe_dev_v1();
    probes[1].stable_id = probes[0].stable_id;
    probes[1].category = probes[0].category;
    expect(!alpha::probes::validate_probe_dev_v1(probes).empty(),
           "duplicate stable ID/category was accepted");
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
    return runner.finish();
}
