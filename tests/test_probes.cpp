#include "old_school/probes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using old_school::CardId;
using old_school::DeckId;
using old_school::GameState;
using old_school::PermanentId;
using old_school::PriorityAction;
using old_school::PriorityActionKind;
using old_school::PriorityPassResult;
using old_school::PriorityState;
using old_school::TurnPhase;
using old_school::probes::Category;
using old_school::probes::Dc1Dominance;
using old_school::probes::DecisionProbe;
using old_school::probes::BinaryBlockDecision;
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
        std::cout
            << passed_
            << " probe tests passed (20 dev fixtures + 1 harvested "
               "validation fixture + 2 supplemental Force Spike "
               "controls + 6 reject-only field regressions)\n";
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

const PriorityAction& priority_candidate(
    const DecisionProbe& probe, std::string_view descriptor) {
    const auto found = std::find_if(
        probe.candidates.begin(), probe.candidates.end(),
        [descriptor](const old_school::probes::Candidate& candidate) {
            return candidate.descriptor == descriptor;
        });
    if (found == probe.candidates.end()) {
        throw std::runtime_error(
            "required priority candidate is missing");
    }
    const auto* action = std::get_if<PriorityAction>(&found->action);
    if (action == nullptr) {
        throw std::runtime_error(
            "named candidate is not a priority action");
    }
    return *action;
}

const BinaryBlockDecision& block_candidate(
    const DecisionProbe& probe, std::string_view descriptor) {
    const auto found = std::find_if(
        probe.candidates.begin(), probe.candidates.end(),
        [descriptor](const old_school::probes::Candidate& candidate) {
            return candidate.descriptor == descriptor;
        });
    if (found == probe.candidates.end()) {
        throw std::runtime_error(
            "required block candidate is missing");
    }
    const auto* action =
        std::get_if<BinaryBlockDecision>(&found->action);
    if (action == nullptr) {
        throw std::runtime_error(
            "named candidate is not a block decision");
    }
    return *action;
}

std::size_t candidate_index(
    const DecisionProbe& probe, std::string_view descriptor) {
    const auto found = std::find_if(
        probe.candidates.begin(), probe.candidates.end(),
        [descriptor](const old_school::probes::Candidate& candidate) {
            return candidate.descriptor == descriptor;
        });
    if (found == probe.candidates.end()) {
        throw std::runtime_error(
            "required candidate index is missing");
    }
    return static_cast<std::size_t>(
        std::distance(probe.candidates.begin(), found));
}

std::size_t candidate_index(
    const DecisionProbe& probe, const PriorityAction& wanted) {
    const auto found = std::find_if(
        probe.candidates.begin(), probe.candidates.end(),
        [&wanted](const old_school::probes::Candidate& candidate) {
            const auto* action =
                std::get_if<PriorityAction>(&candidate.action);
            return action != nullptr && *action == wanted;
        });
    if (found == probe.candidates.end()) {
        throw std::runtime_error(
            "required Priority candidate index is missing");
    }
    return static_cast<std::size_t>(
        std::distance(probe.candidates.begin(), found));
}

PermanentId creature_id(const GameState& state, std::size_t player,
                        CardId card) {
    const auto& creatures = state.players.at(player).creatures;
    const auto found = std::find_if(
        creatures.begin(), creatures.end(),
        [card](const old_school::CreaturePermanent& creature) {
            return creature.card == card;
        });
    if (found == creatures.end()) {
        throw std::runtime_error("required creature is missing");
    }
    return found->id;
}

bool contains_action(const std::vector<PriorityAction>& actions,
                     const PriorityAction& wanted) {
    return std::find(actions.begin(), actions.end(), wanted) !=
           actions.end();
}

const PriorityAction& find_priority_action(
    const DecisionProbe& probe, const PriorityAction& wanted) {
    const auto found = std::find_if(
        probe.candidates.begin(), probe.candidates.end(),
        [&wanted](const old_school::probes::Candidate& candidate) {
            const auto* action =
                std::get_if<PriorityAction>(&candidate.action);
            return action != nullptr && *action == wanted;
        });
    if (found == probe.candidates.end()) {
        throw std::runtime_error(
            "required priority action is missing");
    }
    return std::get<PriorityAction>(found->action);
}

bool player_states_equal(const old_school::PlayerState& left,
                         const old_school::PlayerState& right) {
    return left.life == right.life &&
           left.library == right.library &&
           left.hand == right.hand &&
           left.graveyard == right.graveyard &&
           left.exile == right.exile &&
           left.lands == right.lands &&
           left.creatures == right.creatures &&
           left.artifacts == right.artifacts &&
           left.enchantments == right.enchantments &&
           left.mana_pool == right.mana_pool &&
           left.land_played_this_turn ==
               right.land_played_this_turn;
}

bool player_stats_equal(const old_school::PlayerGameStats& left,
                        const old_school::PlayerGameStats& right) {
    return left.cards_drawn == right.cards_drawn &&
           left.lands_played == right.lands_played &&
           left.spells_cast == right.spells_cast &&
           left.spells_countered == right.spells_countered &&
           left.damage_to_opponent == right.damage_to_opponent &&
           left.cards_milled == right.cards_milled &&
           left.decisions == right.decisions &&
           left.monte_carlo_rollouts ==
               right.monte_carlo_rollouts;
}

bool game_states_equal(const GameState& left,
                       const GameState& right) {
    for (std::size_t player = 0; player < 2; ++player) {
        if (!player_states_equal(left.players[player],
                                 right.players[player]) ||
            !player_stats_equal(left.stats[player],
                                right.stats[player])) {
            return false;
        }
    }
    return left.stack == right.stack &&
           left.extra_turns_pending ==
               right.extra_turns_pending &&
           left.failed_draw == right.failed_draw &&
           left.active_player == right.active_player &&
           left.starting_player == right.starting_player &&
           left.turn_number == right.turn_number &&
           left.next_permanent_id == right.next_permanent_id &&
           left.next_stack_object_id ==
               right.next_stack_object_id;
}

std::array<std::size_t, old_school::kCardCount>
physical_card_counts(const GameState& state, std::size_t player) {
    std::array<std::size_t, old_school::kCardCount> counts{};
    const auto add = [&](CardId card) {
        ++counts.at(static_cast<std::size_t>(card));
    };
    const auto& seat = state.players.at(player);
    for (const CardId card : seat.library) {
        add(card);
    }
    for (const CardId card : seat.hand) {
        add(card);
    }
    for (const CardId card : seat.graveyard) {
        add(card);
    }
    for (const CardId card : seat.exile) {
        add(card);
    }
    for (const auto& land : seat.lands) {
        add(land.card);
    }
    for (const auto& creature : seat.creatures) {
        add(creature.card);
    }
    for (const auto& artifact : seat.artifacts) {
        add(artifact.card);
    }
    for (const CardId card : seat.enchantments) {
        add(card);
    }
    for (const auto& object : state.stack) {
        if (object.controller == player &&
            object.kind ==
                old_school::StackObjectKind::Spell) {
            add(object.card);
        }
    }
    return counts;
}

std::array<std::size_t, old_school::kCardCount>
deck_card_counts(const std::vector<CardId>& deck) {
    std::array<std::size_t, old_school::kCardCount> counts{};
    for (const CardId card : deck) {
        ++counts.at(static_cast<std::size_t>(card));
    }
    return counts;
}

void expect_dc1_raw_card_conservation(
    const DecisionProbe& probe,
    const std::vector<std::size_t>& candidate_indices,
    std::uint64_t seed) {
    for (std::size_t world_index = 0; world_index < 8;
         ++world_index) {
        const GameState world = old_school::sample_determinization(
            probe.state, probe.original_decks, probe.root_player,
            seed + world_index);
        for (const std::size_t candidate : candidate_indices) {
            const auto settlement =
                old_school::probes::
                    settle_dc1_priority_candidate(
                        probe, world, candidate);
            for (std::size_t player = 0; player < 2; ++player) {
                expect(
                    physical_card_counts(
                        settlement.settled_state, player) ==
                        deck_card_counts(
                            probe.original_decks[player]),
                    "raw DC1 settlement violated physical card "
                    "conservation");
            }
        }
    }
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

std::size_t expected_candidate_count(Category category) {
    switch (category) {
    case Category::GreenDevelop:
    case Category::GreenGrowthSaveBolt:
    case Category::GreenGrowthHold:
    case Category::BlueCounterExpensiveSpell:
    case Category::BlueForceSpike:
    case Category::BlueCounterLethal:
    case Category::WhiteEmergencyMoat:
    case Category::WhiteEstablishMillstone:
    case Category::RUFlyingMoatAttack:
        return 2;
    case Category::GreenGrowthPushCombat:
    case Category::RedStackRace:
    case Category::BlueCounterWar:
    case Category::WhiteMillBeforeDraw:
    case Category::WhiteAvoidRedundantMoat:
    case Category::RULandColor:
    case Category::RUBlockerDevelopment:
        return 3;
    case Category::RedFaceLethal:
    case Category::RedFinishDamagedThreat:
        return 4;
    case Category::RedClearBlocker:
        return 5;
    case Category::RUDisintegrateLethal:
        return 9;
    case Category::GreenTsunamiTiming:
    case Category::GreenFavorableAttack:
    case Category::GreenUnfavorableAttack:
    case Category::RUDisintegrateHoldValidation:
    case Category::FieldRULife20FlyingMenChumpAir:
    case Category::FieldRULife4FlyingMenChumpAir:
    case Category::FieldGreenSecondMainSickBearGrowth:
    case Category::FieldGreenBeginCombatGrowthTappedAir:
    case Category::FieldGreenAttackAfterGrowthTappedAir:
    case Category::FieldGreenAttackAfterGrowthUntappedAirControl:
        break;
    }
    throw std::runtime_error(
        "probe-dev-v3 contains a retired category");
}

void resolve_cast_spell(GameState& state) {
    expect(old_school::resolve_top_of_stack(state),
           "spell failed to resolve");
}

void test_corpus_shape_and_candidate_schema() {
    const std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v3();
    expect(probes.size() == 20,
           "probe-dev-v3 must contain exactly 20 fixtures");

    std::array<std::size_t, old_school::kDeckCount> deck_counts{};
    for (const DecisionProbe& probe : probes) {
        expect(probe.stable_id.ends_with(".v3"),
               "probe-dev-v3 retained an old stable ID");
        expect(probe.candidates.size() ==
                   expected_candidate_count(probe.category),
               "fixture candidate count changed");
        expect(!probe.harvest.has_value(),
               "probe-dev-v3 absorbed validation harvest metadata");
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
        old_school::probes::make_probe_dev_v3();
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
    expect(old_school::probes::validate_probe_dev_v3(probes).empty(),
           "valid corpus failed aggregate validation");
}

void test_card_conservation_rejects_missing_physical_card() {
    std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v3();
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
        old_school::probes::make_probe_dev_v3();
    DecisionProbe probe =
        find_probe(probes, Category::RULandColor);
    expect(!probe.state.players[0].library.empty(),
           "test fixture unexpectedly has an empty library");
    const CardId exiled = probe.state.players[0].library.back();
    probe.state.players[0].library.pop_back();
    probe.state.players[0].exile.push_back(exiled);

    const Validation validation =
        old_school::probes::validate_probe(probe);
    expect(validation.ok(),
           "moving an RU card to public exile broke validation");
}

void test_priority_validation_rejects_illegal_or_incomplete_set() {
    const std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v3();
    DecisionProbe probe =
        find_probe(probes, Category::GreenDevelop);
    probe.candidates[1].action =
        PriorityAction::cast_creature(CardId::IronrootTreefolk);
    const Validation validation =
        old_school::probes::validate_probe(probe);
    expect(!validation.candidates_legal_and_complete,
           "unpayable Treefolk replaced a legal action");
}

void test_attack_reachability_checks_flying_through_moat() {
    const std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v3();
    DecisionProbe sick =
        find_probe(probes, Category::RUFlyingMoatAttack);
    sick.state.players[0].creatures[0].summoning_sick = true;
    expect(!old_school::probes::validate_probe(sick).reachable_state,
           "summoning-sick Flying Men was accepted as an attacker");

    DecisionProbe ground =
        find_probe(probes, Category::RUFlyingMoatAttack);
    ground.state.players[0].creatures[0].card =
        CardId::IronclawOrcs;
    expect(!old_school::probes::validate_probe(ground).reachable_state,
           "nonflying attacker was accepted through Moat");
}

void test_hidden_zone_clones_are_observation_invariant() {
    const std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v3();
    for (std::size_t index = 0; index < probes.size(); ++index) {
        expect(
            old_school::probes::hidden_clone_is_determinization_invariant(
                probes[index],
                old_school::probes::kProbeValidationSeed + index),
            "hidden repartition changed sampled information set");
    }
}

void test_red_last_opportunity_timing() {
    const std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v3();
    const DecisionProbe& blocker =
        find_probe(probes, Category::RedClearBlocker);
    expect(blocker.phase == TurnPhase::BeginCombat,
           "blocker-clearing Bolt can heal in a later precombat window");
    GameState blocker_pass = blocker.state;
    PriorityState blocker_priority{
        .player = blocker.root_player,
        .consecutive_passes = blocker.consecutive_passes,
    };
    expect(old_school::pass_priority(
               blocker_pass, blocker_priority) ==
               PriorityPassResult::Passed,
           "blocker-clearing root Pass did not yield priority");
    const auto blocker_responses =
        old_school::legal_priority_actions(
            blocker_pass, blocker_priority.player, false);
    expect(blocker_responses ==
               std::vector<PriorityAction>{PriorityAction::pass()} &&
               old_school::pass_priority(
                   blocker_pass, blocker_priority) ==
                   PriorityPassResult::WindowEnded,
           "opponent can reopen the blocker-clearing priority window");

    const DecisionProbe& damaged =
        find_probe(probes, Category::RedFinishDamagedThreat);
    expect(damaged.phase == TurnPhase::SecondMain &&
               damaged.state.active_player == 1 &&
               damaged.state.turn_number == 10 &&
               damaged.consecutive_passes == 1,
           "damaged-Air fixture is not the final cleanup opportunity");
    GameState pass_state = damaged.state;
    PriorityState priority{
        .player = damaged.root_player,
        .consecutive_passes = damaged.consecutive_passes,
    };
    expect(old_school::pass_priority(pass_state, priority) ==
               PriorityPassResult::WindowEnded,
           "passing damaged-Air fixture did not end the turn window");
}

void test_white_plan_passes_cannot_heal() {
    const std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v3();
    for (const Category category :
         {Category::WhiteEmergencyMoat,
          Category::WhiteAvoidRedundantMoat}) {
        const DecisionProbe& probe = find_probe(probes, category);
        expect(probe.phase == TurnPhase::SecondMain,
               "White plan probe can heal in a later main phase");
        GameState state = probe.state;
        PriorityState priority{
            .player = probe.root_player,
            .consecutive_passes = probe.consecutive_passes,
        };
        expect(old_school::pass_priority(state, priority) ==
                   PriorityPassResult::Passed,
               "White root Pass did not yield priority");
        const auto responses =
            old_school::legal_priority_actions(
                state, priority.player, true);
        expect(responses ==
                   std::vector<PriorityAction>{
                       PriorityAction::pass()} &&
                   old_school::pass_priority(state, priority) ==
                       PriorityPassResult::WindowEnded,
               "opponent can reopen a White final-main plan probe");
    }
}

void test_public_mana_supports_deployed_creatures() {
    const std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v3();
    const DecisionProbe& develop =
        find_probe(probes, Category::GreenDevelop);
    expect(develop.state.players[1].lands.size() >= 5,
           "visible Fire Elemental has no plausible five-mana history");

    const DecisionProbe& save =
        find_probe(probes, Category::GreenGrowthSaveBolt);
    expect(save.state.players[0].lands.size() >= 2,
           "visible Grizzly Bears has no plausible two-mana history");

    const DecisionProbe& push =
        find_probe(probes, Category::GreenGrowthPushCombat);
    expect(push.state.players[0].lands.size() >= 5,
           "visible Ironroot Treefolk has no plausible five-mana history");

    const DecisionProbe& counter_lethal =
        find_probe(probes, Category::BlueCounterLethal);
    expect(counter_lethal.state.players[0].lands.size() >= 5,
           "visible Air Elemental has no plausible "
           "five-mana history");
}

void test_counter_war_lists_every_legal_spell_target() {
    const std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v3();
    const DecisionProbe& probe =
        find_probe(probes, Category::BlueCounterWar);
    expect(probe.state.stack.size() == 2,
           "counter-war stack must contain two spells");
    expect(probe.consecutive_passes == 1,
           "counter-war responder must act after the caster passes");
    expect(probe.candidates.size() == 3 &&
               old_school::probes::validate_probe(probe)
                   .candidates_legal_and_complete,
           "counter-war omitted a targetable stack spell");
}

void test_force_spike_probe_is_a_live_mana_advantage_counter() {
    const std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v3();
    const DecisionProbe& probe =
        find_probe(probes, Category::BlueForceSpike);
    expect(
        probe.stable_id ==
                "blue.force-spike-tapped-out-gray-ogre.v3" &&
            probe.state.stack.size() == 1 &&
            probe.state.stack.back().card == CardId::GrayOgre &&
            probe.state.stack.back().controller == 1 &&
            probe.consecutive_passes == 1,
        "Force Spike probe lost its live opposing spell");
    expect(
        probe.state.players[0].hand ==
                std::vector<CardId>{CardId::ForceSpike} &&
            probe.state.players[0].lands.size() == 1 &&
            !probe.state.players[0].lands.front().tapped,
        "Force Spike probe does not expose exactly one blue mana");
    expect(
        probe.state.players[1].lands.size() == 3 &&
            std::all_of(
                probe.state.players[1].lands.begin(),
                probe.state.players[1].lands.end(),
                [](const old_school::LandPermanent& land) {
                    return land.tapped;
                }),
        "Force Spike target controller can still pay one");

    const old_school::StackObjectId gray_ogre =
        probe.state.stack.back().id;
    const PriorityAction force_spike =
        PriorityAction::cast_force_spike(gray_ogre);
    static_cast<void>(
        find_priority_action(probe, PriorityAction::pass()));
    static_cast<void>(
        find_priority_action(probe, force_spike));

    GameState pass_state = probe.state;
    PriorityState pass_priority{
        .player = probe.root_player,
        .consecutive_passes = probe.consecutive_passes,
    };
    expect(
        old_school::pass_priority(pass_state, pass_priority) ==
                PriorityPassResult::StackObjectResolved &&
            pass_state.stack.empty() &&
            creature_id(pass_state, 1, CardId::GrayOgre) != 0,
        "passing did not let the opposing Gray Ogre resolve");

    GameState counter_state = probe.state;
    expect(
        old_school::apply_priority_action(
            counter_state, probe.root_player, force_spike, false),
        "live Force Spike candidate failed to cast");
    expect(
        counter_state.stack.size() == 2 &&
            counter_state.players[0].lands.front().tapped,
        "casting Force Spike did not use its blue mana");
    resolve_cast_spell(counter_state);
    expect(
        counter_state.stack.empty() &&
            counter_state.players[1].creatures.empty() &&
            std::count(
                counter_state.players[1].graveyard.begin(),
                counter_state.players[1].graveyard.end(),
                CardId::GrayOgre) == 1 &&
            std::count(
                counter_state.players[0].graveyard.begin(),
                counter_state.players[0].graveyard.end(),
                CardId::ForceSpike) == 1 &&
            counter_state.stats[0].spells_countered == 1,
        "Force Spike did not counter the tapped-out Gray Ogre");
}

void test_force_spike_policy_controls_isolate_payable_tax() {
    const std::vector<DecisionProbe> controls =
        old_school::probes::make_force_spike_policy_controls_v1();
    expect(
        old_school::probes::
            validate_force_spike_policy_controls_v1(controls)
                .empty(),
        "Force Spike policy controls failed specialized validation");
    expect(controls.size() == 2,
           "Force Spike policy controls lost a paired state");

    const DecisionProbe& live = controls[0];
    const DecisionProbe& payable = controls[1];
    expect(
        live.stable_id ==
                "control.blue.force-spike-live-gray-ogre.v1" &&
            payable.stable_id ==
                "control.blue.force-spike-payable-gray-ogre.v1",
        "Force Spike policy control identity changed");
    expect(
        live.candidates.size() == 2 &&
            live.candidates[0].descriptor == "pass" &&
            live.candidates[1].descriptor ==
                "force-spike-gray-ogre" &&
            live.candidates[0].action ==
                payable.candidates[0].action &&
            live.candidates[1].action ==
                payable.candidates[1].action,
        "Force Spike controls do not share Pass/Spike candidates");

    GameState normalized_live = live.state;
    GameState normalized_payable = payable.state;
    auto& payable_lands =
        normalized_payable.players[1].lands;
    const auto extra_mountain = std::find_if(
        payable_lands.begin(), payable_lands.end(),
        [](const old_school::LandPermanent& permanent) {
            return permanent.card == CardId::Mountain &&
                   !permanent.tapped;
        });
    expect(extra_mountain != payable_lands.end(),
           "payable control lacks its fourth untapped Mountain");
    payable_lands.erase(extra_mountain);
    normalized_payable.players[1].library.push_back(
        CardId::Mountain);
    for (std::size_t player = 0; player < 2; ++player) {
        std::sort(
            normalized_live.players[player].library.begin(),
            normalized_live.players[player].library.end());
        std::sort(
            normalized_payable.players[player].library.begin(),
            normalized_payable.players[player].library.end());
    }
    expect(
        normalized_live == normalized_payable &&
            live.state.players[1].mana_pool ==
                old_school::ManaCost{} &&
            payable.state.players[1].mana_pool ==
                old_school::ManaCost{} &&
            live.state.players[1].lands.size() == 3 &&
            payable.state.players[1].lands.size() == 4,
        "Force Spike controls differ by more than one public "
        "Mountain");
    expect(
        old_school::probes::
                hidden_clone_is_determinization_invariant(
                    live, old_school::probes::kProbeValidationSeed) &&
            old_school::probes::
                hidden_clone_is_determinization_invariant(
                    payable,
                    old_school::probes::kProbeValidationSeed + 1),
        "Force Spike controls are not hidden-repartition invariant");

    const PriorityAction force_spike =
        std::get<PriorityAction>(live.candidates[1].action);

    GameState live_state = live.state;
    expect(
        old_school::apply_priority_action(
            live_state, live.root_player, force_spike, false),
        "live control could not cast Force Spike");
    resolve_cast_spell(live_state);
    expect(
        live_state.stack.empty() &&
            live_state.players[1].creatures.empty() &&
            live_state.stats[0].spells_countered == 1,
        "live control did not counter the unpayable Gray Ogre");

    GameState payable_state = payable.state;
    expect(
        old_school::apply_priority_action(
            payable_state, payable.root_player, force_spike, false),
        "payable control could not cast Force Spike");
    resolve_cast_spell(payable_state);
    expect(
        payable_state.stack.size() == 1 &&
            payable_state.stack.back().card == CardId::GrayOgre &&
            payable_state.players[1].mana_pool ==
                old_school::ManaCost{} &&
            std::all_of(
                payable_state.players[1].lands.begin(),
                payable_state.players[1].lands.end(),
                [](const old_school::LandPermanent& permanent) {
                    return permanent.tapped;
                }) &&
            payable_state.stats[0].spells_countered == 0,
        "payable control did not spend the tax and retain Gray Ogre");
    resolve_cast_spell(payable_state);
    expect(
        payable_state.stack.empty() &&
            creature_id(payable_state, 1, CardId::GrayOgre) != 0 &&
            payable_state.stats[0].spells_countered == 0,
        "Gray Ogre failed to resolve after paying Force Spike");

    std::vector<DecisionProbe> malformed = controls;
    malformed[1].state.players[1].lands.back().tapped = true;
    expect(
        !old_school::probes::
             validate_force_spike_policy_controls_v1(malformed)
                 .empty(),
        "specialized validator accepted two unpayable controls");
}

void test_response_windows_record_the_casters_pass() {
    const std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v3();
    constexpr std::array<Category, 6> kResponseCategories = {
        Category::GreenGrowthSaveBolt,
        Category::RedStackRace,
        Category::BlueCounterExpensiveSpell,
        Category::BlueForceSpike,
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
        old_school::probes::make_probe_dev_v3();
    probes[1].stable_id = probes[0].stable_id;
    probes[1].category = probes[0].category;
    expect(!old_school::probes::validate_probe_dev_v3(probes).empty(),
           "duplicate stable ID/category was accepted");
}

void test_existing_lethal_stack_branches_apply_exactly() {
    const std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v3();

    const DecisionProbe& red =
        find_probe(probes, Category::RedFaceLethal);
    GameState bolt_state = red.state;
    const auto& bolt_action =
        priority_candidate(red, "bolt-opponent-player");
    expect(old_school::apply_priority_action(
               bolt_state, red.root_player, bolt_action, true),
           "Red lethal Bolt candidate failed to apply");
    resolve_cast_spell(bolt_state);
    expect(bolt_state.players[1].life == 0,
           "Red lethal Bolt did not produce its terminal branch");

    const DecisionProbe& blue =
        find_probe(probes, Category::BlueCounterLethal);
    GameState blue_pass = blue.state;
    PriorityState blue_pass_priority{
        .player = blue.root_player,
        .consecutive_passes = blue.consecutive_passes,
    };
    expect(old_school::pass_priority(
               blue_pass, blue_pass_priority) ==
               PriorityPassResult::StackObjectResolved &&
               blue_pass.players[0].life == 0,
           "Blue Pass did not resolve the pending lethal Bolt");

    GameState blue_counter = blue.state;
    const auto& counter_action =
        priority_candidate(blue, "counter-lethal-lightning-bolt");
    expect(old_school::apply_priority_action(
               blue_counter, blue.root_player, counter_action, true),
           "Blue Counterspell candidate failed to apply");
    resolve_cast_spell(blue_counter);
    expect(blue_counter.stack.empty() &&
               blue_counter.players[0].life == 3,
           "Counterspell failed to remove the lethal Bolt");
}

void test_giant_growth_saves_bolt_target() {
    const std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v3();
    const DecisionProbe& probe =
        find_probe(probes, Category::GreenGrowthSaveBolt);

    GameState pass_state = probe.state;
    PriorityState pass_priority{
        .player = probe.root_player,
        .consecutive_passes = probe.consecutive_passes,
    };
    expect(old_school::pass_priority(pass_state, pass_priority) ==
               PriorityPassResult::StackObjectResolved,
           "passing did not resolve the pending Bolt");
    expect(pass_state.players[0].creatures.empty(),
           "unprotected Bear survived Lightning Bolt");

    GameState growth_state = probe.state;
    const PriorityAction& growth =
        priority_candidate(probe, "growth-own-grizzly-bears");
    expect(old_school::apply_priority_action(
               growth_state, probe.root_player, growth, true),
           "Giant Growth response failed to apply");
    resolve_cast_spell(growth_state);
    expect(growth_state.players[0].creatures.size() == 1 &&
               growth_state.players[0]
                       .creatures.front()
                       .temporary_toughness_bonus == 3,
           "Giant Growth did not resolve above Bolt");
    resolve_cast_spell(growth_state);
    expect(growth_state.players[0].creatures.size() == 1 &&
               growth_state.players[0].creatures.front().damage == 3,
           "grown Bear did not survive the resolved Bolt");
}

void test_giant_growth_push_and_hold_traces() {
    const std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v3();
    const DecisionProbe& push =
        find_probe(probes, Category::GreenGrowthPushCombat);
    const PermanentId treefolk =
        creature_id(push.state, 0, CardId::IronrootTreefolk);

    GameState eligible = push.state;
    expect(old_school::apply_priority_action(
               eligible, push.root_player,
               priority_candidate(
                   push, "growth-own-ironroot-treefolk"),
               false),
           "eligible Growth target failed to apply");
    resolve_cast_spell(eligible);
    expect(old_school::resolve_combat(
               eligible, 0, {treefolk}, {}) &&
               eligible.players[1].life == 0,
           "Growth on the eligible attacker did not push lethal");

    GameState sick = push.state;
    expect(old_school::apply_priority_action(
               sick, push.root_player,
               priority_candidate(
                   push,
                   "growth-own-summoning-sick-grizzly-bears"),
               false),
           "summoning-sick Growth target failed to apply");
    resolve_cast_spell(sick);
    expect(old_school::resolve_combat(sick, 0, {treefolk}, {}) &&
               sick.players[1].life == 3,
           "Growth on the sick Bear changed Treefolk combat damage");

    const DecisionProbe& hold =
        find_probe(probes, Category::GreenGrowthHold);
    GameState held = hold.state;
    PriorityState held_priority{
        .player = hold.root_player,
        .consecutive_passes = 0,
    };
    expect(old_school::pass_priority(held, held_priority) ==
               PriorityPassResult::Passed &&
               old_school::pass_priority(held, held_priority) ==
                   PriorityPassResult::WindowEnded &&
               held.players[0].hand ==
                   std::vector<CardId>{CardId::GiantGrowth},
           "final-main Pass did not retain Giant Growth");

    GameState wasted = hold.state;
    expect(old_school::apply_priority_action(
               wasted, hold.root_player,
               priority_candidate(
                   hold, "growth-own-grizzly-bears"),
               true),
           "final-main Growth failed to apply");
    resolve_cast_spell(wasted);
    old_school::cleanup_turn(
        wasted, wasted.active_player, {});
    expect(wasted.players[0].hand.empty() &&
               std::count(wasted.players[0].graveyard.begin(),
                          wasted.players[0].graveyard.end(),
                          CardId::GiantGrowth) == 1 &&
               wasted.players[0]
                       .creatures.front()
                       .temporary_power_bonus == 0,
           "wasted Growth survived cleanup or remained in hand");
}

void test_ru_land_color_and_blocker_traces() {
    const std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v3();
    const DecisionProbe& land =
        find_probe(probes, Category::RULandColor);

    GameState island = land.state;
    expect(old_school::apply_priority_action(
               island, land.root_player,
               priority_candidate(land, "play-island"), true),
           "Island land choice failed to apply");
    const auto island_actions =
        old_school::legal_priority_actions(island, land.root_player, true);
    expect(contains_action(
               island_actions,
               PriorityAction::cast_creature(CardId::FlyingMen)),
           "Island did not unlock Flying Men");

    GameState mountain = land.state;
    expect(old_school::apply_priority_action(
               mountain, land.root_player,
               priority_candidate(land, "play-mountain"), true),
           "Mountain land choice failed to apply");
    const auto mountain_actions =
        old_school::legal_priority_actions(
            mountain, land.root_player, true);
    expect(!contains_action(
               mountain_actions,
               PriorityAction::cast_creature(CardId::FlyingMen)),
           "Mountain incorrectly unlocked Flying Men");

    const DecisionProbe& blocker =
        find_probe(probes, Category::RUBlockerDevelopment);
    const PermanentId bear =
        creature_id(blocker.state, 1, CardId::GrizzlyBears);

    GameState no_blocker = blocker.state;
    expect(old_school::resolve_combat(
               no_blocker, 1, {bear}, {}) &&
               no_blocker.players[0].life == 0,
           "visible Bear was not lethal without a blocker");

    GameState ironclaw = blocker.state;
    expect(old_school::apply_priority_action(
               ironclaw, blocker.root_player,
               priority_candidate(
                   blocker, "cast-ironclaw-orcs"),
               true),
           "Ironclaw Orcs failed to cast");
    resolve_cast_spell(ironclaw);
    const PermanentId orcs =
        creature_id(ironclaw, 0, CardId::IronclawOrcs);
    expect(!old_school::resolve_combat(
               ironclaw, 1, {bear}, {{bear, orcs}}),
           "Ironclaw Orcs illegally blocked a power-2 Bear");

    GameState gray = blocker.state;
    expect(old_school::apply_priority_action(
               gray, blocker.root_player,
               priority_candidate(blocker, "cast-gray-ogre"), true),
           "Gray Ogre failed to cast");
    resolve_cast_spell(gray);
    const PermanentId ogre =
        creature_id(gray, 0, CardId::GrayOgre);
    expect(old_school::resolve_combat(
               gray, 1, {bear}, {{bear, ogre}}) &&
               gray.players[0].life == 2,
           "Gray Ogre did not legally block the Bear");
}

void test_ru_flying_and_disintegrate_traces() {
    const std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v3();
    const DecisionProbe& flying =
        find_probe(probes, Category::RUFlyingMoatAttack);
    const PermanentId flying_men =
        creature_id(flying.state, 0, CardId::FlyingMen);
    GameState attack = flying.state;
    expect(old_school::resolve_combat(
               attack, 0, {flying_men}, {}) &&
               attack.players[1].life == 0,
           "Flying Men did not attack through Moat for lethal");

    const DecisionProbe& disintegrate =
        find_probe(probes, Category::RUDisintegrateLethal);
    const auto legal = old_school::legal_priority_actions(
        disintegrate.state, disintegrate.root_player, true);
    expect(legal.size() == 9,
           "Disintegrate fixture does not have exactly nine actions");
    for (int x_value = 0; x_value <= 3; ++x_value) {
        for (std::size_t target = 0; target < 2; ++target) {
            expect(contains_action(
                       legal,
                       PriorityAction::cast_disintegrate(
                           x_value,
                           old_school::Target::player_target(target))),
                   "affordable player-targeted Disintegrate is missing");
        }
    }
    expect(!contains_action(
               legal,
               PriorityAction::cast_disintegrate(
                   4, old_school::Target::player_target(1))),
           "unaffordable X=4 Disintegrate was legal");

    GameState lethal = disintegrate.state;
    expect(old_school::apply_priority_action(
               lethal, disintegrate.root_player,
               priority_candidate(
                   disintegrate,
                   "disintegrate-x3-opponent-player"),
               true),
           "lethal Disintegrate failed to apply");
    resolve_cast_spell(lethal);
    expect(lethal.players[1].life == 0,
           "X=3 Disintegrate was not lethal");

    GameState short_burn = disintegrate.state;
    expect(old_school::apply_priority_action(
               short_burn, disintegrate.root_player,
               priority_candidate(
                   disintegrate,
                   "disintegrate-x2-opponent-player"),
               true),
           "X=2 Disintegrate failed to apply");
    resolve_cast_spell(short_burn);
    expect(short_burn.players[1].life == 1,
           "X=2 Disintegrate dealt the wrong damage");
}

void test_field_regressions_are_separate_and_rules_valid() {
    const std::vector<DecisionProbe> fields =
        old_school::probes::make_field_regressions_v1();
    expect(fields.size() == 6,
           "field-regressions-v1 must contain six fixtures");
    const std::vector<std::string> corpus_errors =
        old_school::probes::validate_field_regressions_v1(fields);
    if (!corpus_errors.empty()) {
        std::string joined;
        for (const std::string& error : corpus_errors) {
            if (!joined.empty()) {
                joined += "; ";
            }
            joined += error;
        }
        throw std::runtime_error(
            "field-regressions-v1 failed corpus validation: " +
            joined);
    }
    for (const DecisionProbe& probe : fields) {
        const Validation validation =
            old_school::probes::validate_probe(probe);
        if (!validation.ok()) {
            throw std::runtime_error(
                probe.stable_id + ": " +
                validation_errors(validation));
        }
        expect(validation.exact_card_conservation &&
                   validation.candidates_legal_and_complete &&
                   validation.reachable_state &&
                   validation.hidden_clone_invariant,
               "field fixture skipped a validation dimension");
        expect(probe.stable_id.starts_with("field.") &&
                   probe.stable_id.ends_with(".v1") &&
                   !probe.harvest.has_value(),
               "field fixture lost its separate authored identity");
    }

    expect(old_school::probes::make_probe_dev_v3().size() == 20,
           "field fixtures leaked into probe-dev-v3");

    std::vector<DecisionProbe> malformed = fields;
    BinaryBlockDecision& illegal =
        std::get<BinaryBlockDecision>(
            malformed.front().candidates.back().action);
    illegal.blocker = 999'999;
    expect(
        !old_school::probes::validate_probe(malformed.front())
             .candidates_legal_and_complete &&
            !old_school::probes::
                 validate_field_regressions_v1(malformed)
                 .empty(),
        "field block validation accepted a missing blocker");
}

void test_field_ru_chump_block_rules_consequences() {
    const std::vector<DecisionProbe> fields =
        old_school::probes::make_field_regressions_v1();
    for (const Category category :
         {Category::FieldRULife20FlyingMenChumpAir,
          Category::FieldRULife4FlyingMenChumpAir}) {
        const DecisionProbe& probe = find_probe(fields, category);
        const BinaryBlockDecision& no_blocks =
            block_candidate(probe, "no-blocks");
        const BinaryBlockDecision& block = block_candidate(
            probe, "block-air-elemental-with-flying-men");
        expect(!no_blocks.include && block.include &&
                   no_blocks.attacker == block.attacker &&
                   no_blocks.blocker == block.blocker,
               "field chump fixture lost its binary block branches");

        const int starting_life =
            probe.state.players[probe.root_player].life;
        GameState unblocked = probe.state;
        expect(
            old_school::probes::settle_binary_block_decision(
                unblocked, probe.state.active_player, no_blocks) &&
                unblocked.players[probe.root_player].life ==
                    starting_life - 4 &&
                unblocked.players[0].creatures.size() == 1 &&
                unblocked.players[1].creatures.size() == 1,
            "unblocked Air Elemental branch has wrong factual "
            "consequences");

        GameState blocked = probe.state;
        expect(
            old_school::probes::settle_binary_block_decision(
                blocked, probe.state.active_player, block) &&
                blocked.players[probe.root_player].life ==
                    starting_life &&
                blocked.players[0].creatures.empty() &&
                std::count(
                    blocked.players[0].graveyard.begin(),
                    blocked.players[0].graveyard.end(),
                    CardId::FlyingMen) == 1 &&
                blocked.players[1].creatures.size() == 1 &&
                blocked.players[1].creatures.front().tapped &&
                blocked.players[1].creatures.front().damage == 1,
            "Flying Men block branch has wrong factual "
            "consequences");
    }
}

void test_field_second_main_sick_bear_growth_consequences() {
    const std::vector<DecisionProbe> fields =
        old_school::probes::make_field_regressions_v1();
    const DecisionProbe& probe = find_probe(
        fields, Category::FieldGreenSecondMainSickBearGrowth);
    const PriorityAction& growth = priority_candidate(
        probe, "growth-own-summoning-sick-grizzly-bears");

    GameState state = probe.state;
    expect(old_school::apply_priority_action(
               state, probe.root_player, growth, true),
           "field sick-Bear Giant Growth failed to cast");
    resolve_cast_spell(state);
    expect(
        state.players[0].hand.empty() &&
            state.players[0].creatures.size() == 1 &&
            state.players[0].creatures.front().summoning_sick &&
            state.players[0]
                    .creatures.front()
                    .temporary_power_bonus == 3 &&
            state.players[0]
                    .creatures.front()
                    .temporary_toughness_bonus == 3 &&
            old_school::card_definition(
                state.players[0].creatures.front().card)
                        .power +
                    state.players[0]
                        .creatures.front()
                        .temporary_power_bonus ==
                5 &&
            old_school::card_definition(
                state.players[0].creatures.front().card)
                        .toughness +
                    state.players[0]
                        .creatures.front()
                        .temporary_toughness_bonus ==
                5,
        "resolved field Growth did not produce a sick 5/5 Bear");

    GameState illegal_attack = state;
    const PermanentId bear =
        creature_id(illegal_attack, 0, CardId::GrizzlyBears);
    expect(!old_school::resolve_combat(
               illegal_attack, 0, {bear}, {}),
           "summoning-sick field Bear was allowed to attack");

    old_school::cleanup_turn(state, probe.root_player, {});
    const auto& cleaned_bear = state.players[0].creatures.front();
    const auto& bear_definition =
        old_school::card_definition(cleaned_bear.card);
    expect(
        bear_definition.power +
                    cleaned_bear.temporary_power_bonus ==
                2 &&
            bear_definition.toughness +
                    cleaned_bear.temporary_toughness_bonus ==
                2 &&
            std::count(
                state.players[0].graveyard.begin(),
                state.players[0].graveyard.end(),
                CardId::GiantGrowth) == 1,
        "cleanup did not restore the Bear to 2/2 and retain the "
        "spent Growth");
}

void test_field_growth_on_air_has_linked_attack_consequences() {
    const std::vector<DecisionProbe> fields =
        old_school::probes::make_field_regressions_v1();
    const DecisionProbe& growth_probe = find_probe(
        fields, Category::FieldGreenBeginCombatGrowthTappedAir);
    const DecisionProbe& linked_attack = find_probe(
        fields, Category::FieldGreenAttackAfterGrowthTappedAir);
    const DecisionProbe& untapped_control = find_probe(
        fields,
        Category::FieldGreenAttackAfterGrowthUntappedAirControl);

    GameState resolved = growth_probe.state;
    expect(old_school::apply_priority_action(
               resolved, growth_probe.root_player,
               priority_candidate(
                   growth_probe,
                   "growth-opponent-tapped-air-elemental"),
               false),
           "field opponent-targeted Growth failed to cast");
    resolve_cast_spell(resolved);
    expect(
        game_states_equal(resolved, linked_attack.state) &&
            resolved.players[1].creatures.size() == 1 &&
            resolved.players[1].creatures.front().tapped &&
            resolved.players[1]
                    .creatures.front()
                    .temporary_power_bonus == 3 &&
            resolved.players[1]
                    .creatures.front()
                    .temporary_toughness_bonus == 3,
        "field attack fixture is not the exact resolved Growth "
        "successor");

    const PermanentId treefolk =
        creature_id(linked_attack.state, 0, CardId::IronrootTreefolk);
    GameState skipped = linked_attack.state;
    expect(old_school::resolve_combat(skipped, 0, {}, {}) &&
               skipped.players[1].life == 6,
           "linked skip branch changed the opponent's life");
    GameState attacked = linked_attack.state;
    expect(old_school::resolve_combat(
               attacked, 0, {treefolk}, {}) &&
               attacked.players[1].life == 3,
           "linked unblocked Treefolk did not deal three damage");

    const PermanentId control_treefolk =
        creature_id(untapped_control.state, 0,
                    CardId::IronrootTreefolk);
    const PermanentId control_air =
        creature_id(untapped_control.state, 1,
                    CardId::AirElemental);
    GameState control_skip = untapped_control.state;
    expect(old_school::resolve_combat(control_skip, 0, {}, {}) &&
               control_skip.players[1].life == 6 &&
               control_skip.players[0].creatures.size() == 1 &&
               control_skip.players[1].creatures.size() == 1,
           "untapped-Air skip control changed public resources");
    GameState control_block = untapped_control.state;
    expect(
        old_school::resolve_combat(
            control_block, 0, {control_treefolk},
            {{control_treefolk, control_air}}) &&
            control_block.players[1].life == 6 &&
            control_block.players[0].creatures.empty() &&
            control_block.players[1].creatures.size() == 1 &&
            control_block.players[1].creatures.front().damage == 3,
        "untapped grown Air Elemental did not legally eat the "
        "Treefolk");
}

void test_harvested_validation_probe_is_reproducible_and_valid() {
    const std::vector<DecisionProbe> first =
        old_school::probes::make_probe_validation_v1();
    const std::vector<DecisionProbe> repeated =
        old_school::probes::make_probe_validation_v1();
    expect(first.size() == 1 && repeated.size() == 1,
           "validation-v1 must contain one harvested decision");
    expect(
        old_school::probes::validate_probe_validation_v1(first)
            .empty(),
        "harvested validation-v1 corpus failed validation");

    const DecisionProbe& probe = first.front();
    const DecisionProbe& second = repeated.front();
    expect(probe.stable_id ==
                   "validation.ru.disintegrate-hold-x0.v1" &&
               probe.category ==
                   Category::RUDisintegrateHoldValidation &&
               probe.root_deck == DeckId::RUAggro &&
               probe.opponent_deck == DeckId::Green &&
               probe.phase == TurnPhase::SecondMain &&
               probe.state.active_player == probe.root_player &&
               probe.state.players[probe.root_player]
                   .land_played_this_turn,
           "harvested probe lost its RU decision identity");
    expect(probe.harvest.has_value() &&
               second.harvest.has_value(),
           "harvested probe omitted provenance");
    expect(
        probe.harvest->collector ==
                old_school::probes::
                    kProbePriorityCallbackCollector &&
            probe.harvest->trajectory_script ==
                old_school::probes::kProbeLandThenPassScript &&
            probe.harvest->game_seed ==
                old_school::probes::
                    kProbeValidationV1GameSeed &&
            probe.harvest->turn_number ==
                probe.state.turn_number &&
            probe.harvest->phase == probe.phase,
        "harvest provenance does not identify the seeded callback");
    expect(probe.harvest == second.harvest &&
               probe.original_decks == second.original_decks &&
               game_states_equal(probe.state, second.state) &&
               probe.candidates.size() ==
                   second.candidates.size(),
           "fixed harvest seed did not reproduce the same state");
    for (std::size_t index = 0;
         index < probe.candidates.size(); ++index) {
        expect(
            probe.candidates[index].descriptor ==
                    second.candidates[index].descriptor &&
                probe.candidates[index].action ==
                    second.candidates[index].action,
            "fixed harvest seed changed its legal action list");
    }

    const Validation validation =
        old_school::probes::validate_probe(probe);
    expect(validation.exact_card_conservation &&
               validation.candidates_legal_and_complete &&
               validation.reachable_state &&
               validation.hidden_clone_invariant &&
               validation.ok(),
           "harvested state failed a probe validation dimension");

    int maximum_opponent_x = -1;
    bool has_land_play = false;
    for (const auto& candidate : probe.candidates) {
        const auto* action =
            std::get_if<PriorityAction>(&candidate.action);
        has_land_play =
            has_land_play ||
            (action != nullptr &&
             action->kind == PriorityActionKind::PlayLand);
        if (action != nullptr &&
            action->kind ==
                PriorityActionKind::CastDisintegrate &&
            action->target.has_value() &&
            !action->target->creature.has_value() &&
            action->target->player == 1) {
            maximum_opponent_x =
                std::max(maximum_opponent_x, action->x_value);
        }
    }
    expect(!has_land_play && maximum_opponent_x >= 0 &&
               maximum_opponent_x <
                   probe.state.players[1].life,
           "harvested decision includes a land play or lethal X");
}

void test_harvested_x_zero_has_no_effect_and_pass_holds() {
    const std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_validation_v1();
    const DecisionProbe& probe = probes.front();
    const PriorityAction x_zero =
        PriorityAction::cast_disintegrate(
            0, old_school::Target::player_target(1));
    static_cast<void>(find_priority_action(probe, x_zero));
    static_cast<void>(
        find_priority_action(probe, PriorityAction::pass()));

    const auto count_card =
        [](const std::vector<CardId>& cards, CardId card) {
            return static_cast<std::size_t>(
                std::count(cards.begin(), cards.end(), card));
        };
    const auto untapped_mountains =
        [](const old_school::PlayerState& player) {
            return static_cast<std::size_t>(std::count_if(
                player.lands.begin(), player.lands.end(),
                [](const old_school::LandPermanent& land) {
                    return land.card == CardId::Mountain &&
                           !land.tapped;
                }));
        };

    const std::size_t disintegrates_in_hand =
        count_card(probe.state.players[0].hand,
                   CardId::Disintegrate);
    const std::size_t disintegrates_in_graveyard =
        count_card(probe.state.players[0].graveyard,
                   CardId::Disintegrate);
    const std::size_t ready_red =
        untapped_mountains(probe.state.players[0]);
    expect(disintegrates_in_hand > 0 && ready_red > 0,
           "harvested state cannot cast its X=0 spell");

    GameState wasted = probe.state;
    const std::array<int, 2> original_life = {
        wasted.players[0].life,
        wasted.players[1].life,
    };
    expect(old_school::apply_priority_action(
               wasted, probe.root_player, x_zero, true),
           "harvested X=0 action failed to apply");
    resolve_cast_spell(wasted);
    expect(wasted.players[0].life == original_life[0] &&
               wasted.players[1].life == original_life[1],
           "X=0 Disintegrate changed a life total");
    expect(
        count_card(wasted.players[0].hand,
                   CardId::Disintegrate) +
                    1 ==
                disintegrates_in_hand &&
            count_card(wasted.players[0].graveyard,
                       CardId::Disintegrate) ==
                disintegrates_in_graveyard + 1 &&
            untapped_mountains(wasted.players[0]) + 1 ==
                ready_red,
        "X=0 did not spend exactly the spell and one red mana");

    GameState held = probe.state;
    PriorityState priority{
        .player = probe.root_player,
        .consecutive_passes = probe.consecutive_passes,
    };
    expect(old_school::pass_priority(held, priority) ==
                   PriorityPassResult::Passed &&
               old_school::pass_priority(held, priority) ==
                   PriorityPassResult::WindowEnded,
           "two passes did not close the harvested final main");
    expect(
        held.players[0].hand == probe.state.players[0].hand &&
            held.players[0].graveyard ==
                probe.state.players[0].graveyard &&
            held.players[0].lands ==
                probe.state.players[0].lands,
        "Pass failed to retain Disintegrate and its mana");
    old_school::begin_turn(held, probe.root_player);
    expect(
        contains_action(
            old_school::legal_priority_actions(
                held, probe.root_player, true),
            x_zero),
        "held Disintegrate was not available on a later main phase");
}

void test_dc1_x_zero_is_strictly_dominated_and_hidden_exact() {
    const DecisionProbe probe =
        old_school::probes::make_probe_validation_v1().front();
    const std::size_t pass =
        candidate_index(probe, PriorityAction::pass());
    const auto x_zero_found = std::find_if(
        probe.candidates.begin(), probe.candidates.end(),
        [](const old_school::probes::Candidate& candidate) {
            const auto* action =
                std::get_if<PriorityAction>(&candidate.action);
            return action != nullptr &&
                   action->kind ==
                       PriorityActionKind::CastDisintegrate &&
                   action->x_value == 0;
        });
    expect(x_zero_found != probe.candidates.end(),
           "validation probe lost its X=0 candidate");
    const std::size_t x_zero = static_cast<std::size_t>(
        std::distance(probe.candidates.begin(), x_zero_found));

    const auto comparison =
        old_school::probes::compare_dc1_priority_pair(
            probe, pass, x_zero, 8, 577215);
    expect(comparison.unanimous_orientation ==
               Dc1Dominance::FirstDominatesSecond,
           "DC1 did not classify Pass as strictly dominating X=0");
    expect(comparison.hidden_repartition_bit_identical,
           "DC1 X=0 comparison changed under hidden repartition");
    expect(comparison.world_orientations.size() == 8 &&
               std::all_of(
                   comparison.world_orientations.begin(),
                   comparison.world_orientations.end(),
                   [](Dc1Dominance orientation) {
                       return orientation ==
                              Dc1Dominance::FirstDominatesSecond;
                   }),
           "DC1 X=0 orientation was not unanimous over K=8");

    const GameState world = old_school::sample_determinization(
        probe.state, probe.original_decks, probe.root_player, 577215);
    const auto settled =
        old_school::probes::settle_dc1_priority_candidate(
            probe, world, x_zero);
    expect(settled.window_ended && !settled.terminal &&
               settled.settled_state.stack.empty(),
           "DC1 X=0 branch did not stop at the settled window");
    const auto mountain = std::find_if(
        probe.state.players[probe.root_player].lands.begin(),
        probe.state.players[probe.root_player].lands.end(),
        [](const old_school::LandPermanent& land) {
            return land.card == CardId::Mountain && !land.tapped;
        });
    expect(
        mountain !=
            probe.state.players[probe.root_player].lands.end(),
        "DC1 X=0 fixture lost its payable Mountain");
    old_school::probes::Dc1PlayerResourceCost expected_cost;
    expected_cost.hand_cards_consumed[
        static_cast<std::size_t>(CardId::Disintegrate)] = 1;
    expected_cost.mana_depleted.red = 1;
    expected_cost.preexisting_sources_newly_tapped.push_back({
        .kind =
            old_school::probes::Dc1ManaSourceKind::Land,
        .key = static_cast<std::uint64_t>(
            std::distance(
                probe.state.players[probe.root_player].lands.begin(),
                mountain)),
        .card = CardId::Mountain,
    });
    expect(
        settled.resources[probe.root_player] == expected_cost &&
            settled.resources[1 - probe.root_player] ==
                old_school::probes::Dc1PlayerResourceCost{},
        "DC1 did not record X=0's exact one-card, one-red-source "
        "ledger");
}

void test_dc1_payable_force_spike_is_a_tradeoff() {
    const auto controls =
        old_school::probes::make_force_spike_policy_controls_v1();
    const DecisionProbe& live = controls.at(0);
    const DecisionProbe& payable = controls.at(1);
    const auto compare = [](const DecisionProbe& probe) {
        return old_school::probes::compare_dc1_priority_pair(
            probe, candidate_index(probe, "pass"),
            candidate_index(probe, "force-spike-gray-ogre"),
            8, 577215);
    };

    const auto payable_result = compare(payable);
    expect(payable_result.unanimous_orientation ==
               Dc1Dominance::Incomparable,
           "payable Force Spike was mislabeled as dominance");
    expect(payable_result.hidden_repartition_bit_identical &&
               std::all_of(
                   payable_result.world_orientations.begin(),
                   payable_result.world_orientations.end(),
                   [](Dc1Dominance orientation) {
                       return orientation ==
                              Dc1Dominance::Incomparable;
                   }),
           "payable Force Spike tradeoff was not hidden-exact");

    const std::size_t payable_pass =
        candidate_index(payable, "pass");
    const std::size_t payable_spike =
        candidate_index(
            payable, "force-spike-gray-ogre");
    const GameState payable_world =
        old_school::sample_determinization(
            payable.state, payable.original_decks,
            payable.root_player, 577215);
    const auto pass_settlement =
        old_school::probes::settle_dc1_priority_candidate(
            payable, payable_world, payable_pass);
    const auto spike_settlement =
        old_school::probes::settle_dc1_priority_candidate(
            payable, payable_world, payable_spike);
    const auto& actor_spend =
        spike_settlement.resources[payable.root_player];
    const auto& opponent_spend =
        spike_settlement.resources[1 - payable.root_player];
    expect(
        old_school::probes::
            dc1_settlements_have_equal_normalized_effect(
                payable, pass_settlement, spike_settlement),
        "payable Force Spike branches did not reduce to their "
        "two-sided resource tradeoff");
    expect(
        actor_spend.hand_cards_consumed[
            static_cast<std::size_t>(CardId::ForceSpike)] == 1 &&
            actor_spend.mana_depleted.blue == 1 &&
            actor_spend.preexisting_sources_newly_tapped.size() ==
                1 &&
            actor_spend.preexisting_sources_newly_tapped.front()
                    .card == CardId::Island,
        "payable Force Spike did not record the actor's exact "
        "card/blue-source cost");
    expect(
        opponent_spend.mana_depleted.red == 1 &&
            opponent_spend
                    .preexisting_sources_newly_tapped.size() == 1 &&
            opponent_spend
                    .preexisting_sources_newly_tapped.front()
                    .card == CardId::Mountain,
        "payable Force Spike did not record the opponent's exact "
        "tax payment");
    expect(
        pass_settlement.resources[payable.root_player] ==
                old_school::probes::Dc1PlayerResourceCost{} &&
            pass_settlement.resources[1 - payable.root_player] ==
                old_school::probes::Dc1PlayerResourceCost{},
        "payable Pass branch unexpectedly consumed a resource");
    const auto has_gray_ogre = [](const GameState& state) {
        return std::any_of(
            state.players[1].creatures.begin(),
            state.players[1].creatures.end(),
            [](const old_school::CreaturePermanent& creature) {
                return creature.card == CardId::GrayOgre;
            });
    };
    expect(
        has_gray_ogre(pass_settlement.settled_state) &&
            has_gray_ogre(spike_settlement.settled_state),
        "payable Force Spike changed the target spell's resolved "
        "effect");

    const auto live_result = compare(live);
    expect(live_result.unanimous_orientation ==
               Dc1Dominance::Incomparable &&
               live_result.hidden_repartition_bit_identical,
           "live Force Spike's different spell outcome triggered "
           "resource dominance");
    const GameState live_world =
        old_school::sample_determinization(
            live.state, live.original_decks, live.root_player,
            577215);
    const auto live_pass =
        old_school::probes::settle_dc1_priority_candidate(
            live, live_world, candidate_index(live, "pass"));
    const auto live_spike =
        old_school::probes::settle_dc1_priority_candidate(
            live, live_world,
            candidate_index(
                live, "force-spike-gray-ogre"));
    expect(
        !old_school::probes::
             dc1_settlements_have_equal_normalized_effect(
                 live, live_pass, live_spike) &&
            has_gray_ogre(live_pass.settled_state) &&
            !has_gray_ogre(live_spike.settled_state),
        "live Force Spike negative did not preserve its unequal "
        "spell outcome");
}

void test_dc1_productive_actions_do_not_trigger() {
    const auto dev = old_school::probes::make_probe_dev_v3();
    const auto expect_incomparable =
        [](const DecisionProbe& probe, std::string_view first,
           std::string_view second) {
            const auto comparison =
                old_school::probes::compare_dc1_priority_pair(
                    probe, candidate_index(probe, first),
                    candidate_index(probe, second), 8, 577215);
            expect(
                comparison.unanimous_orientation ==
                        Dc1Dominance::Incomparable &&
                    comparison.hidden_repartition_bit_identical,
                "productive action triggered DC1 dominance");
        };

    expect_incomparable(
        find_probe(dev, Category::GreenGrowthSaveBolt),
        "pass", "growth-own-grizzly-bears");
    expect_incomparable(
        find_probe(dev, Category::GreenDevelop),
        "pass", "cast-grizzly-bears");
    expect_incomparable(
        find_probe(dev, Category::RULandColor),
        "pass", "play-mountain");
    expect_incomparable(
        find_probe(dev, Category::WhiteEstablishMillstone),
        "pass", "cast-millstone");
    expect_incomparable(
        find_probe(dev, Category::RUDisintegrateLethal),
        "pass", "disintegrate-x3-opponent-player");
}

void test_dc1_order_invariance_and_malformed_fail_closed() {
    const DecisionProbe original =
        old_school::probes::make_probe_validation_v1().front();
    const std::size_t original_pass =
        candidate_index(original, PriorityAction::pass());
    const PriorityAction x_zero_action =
        PriorityAction::cast_disintegrate(
            0, old_school::Target::player_target(1));
    const std::size_t original_x_zero =
        candidate_index(original, x_zero_action);
    const auto first =
        old_school::probes::compare_dc1_priority_pair(
            original, original_pass, original_x_zero, 8, 271828);
    const auto reversed =
        old_school::probes::compare_dc1_priority_pair(
            original, original_x_zero, original_pass, 8, 271828);
    expect(
        reversed.unanimous_orientation ==
                Dc1Dominance::SecondDominatesFirst &&
            reversed.hidden_repartition_bit_identical &&
            std::all_of(
                reversed.world_orientations.begin(),
                reversed.world_orientations.end(),
                [](Dc1Dominance orientation) {
                    return orientation ==
                           Dc1Dominance::SecondDominatesFirst;
                }),
        "reversing the DC1 candidate pair did not reverse its "
        "strict orientation");

    DecisionProbe reordered = original;
    std::reverse(reordered.candidates.begin(),
                 reordered.candidates.end());
    const auto second =
        old_school::probes::compare_dc1_priority_pair(
            reordered,
            candidate_index(reordered, PriorityAction::pass()),
            candidate_index(reordered, x_zero_action), 8, 271828);
    expect(first.first_descriptor == second.first_descriptor &&
               first.second_descriptor == second.second_descriptor &&
               first.world_seeds == second.world_seeds &&
               first.world_orientations ==
                   second.world_orientations &&
               first.unanimous_orientation ==
                   second.unanimous_orientation &&
               first.hidden_repartition_bit_identical &&
               second.hidden_repartition_bit_identical,
           "candidate order changed descriptor-keyed DC1 result");

    DecisionProbe renamed = original;
    renamed.stable_id = "unrelated.trajectory.provenance";
    const auto renamed_result =
        old_school::probes::compare_dc1_priority_pair(
            renamed,
            candidate_index(renamed, PriorityAction::pass()),
            candidate_index(renamed, x_zero_action), 8, 271828);
    expect(
        renamed_result.world_seeds == first.world_seeds &&
            renamed_result.world_orientations ==
                first.world_orientations &&
            renamed_result.unanimous_orientation ==
                first.unanimous_orientation,
        "trajectory stable ID changed exact-pair world seeds");

    DecisionProbe relabeled = original;
    relabeled.candidates[original_pass].descriptor +=
        ".different-exact-pair";
    const auto relabeled_result =
        old_school::probes::compare_dc1_priority_pair(
            relabeled, original_pass, original_x_zero, 8, 271828);
    expect(
        relabeled_result.world_seeds != first.world_seeds,
        "changing an exact action descriptor did not change pair-world "
        "seeds");

    DecisionProbe malformed = original;
    const GameState before = malformed.state;
    std::get<PriorityAction>(
        malformed.candidates[original_x_zero].action) =
        PriorityAction::cast_disintegrate(
            99, old_school::Target::player_target(1));
    bool rejected = false;
    try {
        static_cast<void>(
            old_school::probes::settle_dc1_priority_candidate(
                malformed, malformed.state, original_x_zero));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected && game_states_equal(malformed.state, before),
           "malformed DC1 root mutated before failing closed");
}

void test_dc1_pair_label_dedupe_drops_conflicts() {
    const auto result =
        old_school::probes::dedupe_dc1_pair_labels({
            {
                .exact_pair_key = "consistent-positive",
                .seat_game_key = "seat-b",
                .positive = true,
            },
            {
                .exact_pair_key = "consistent-positive",
                .seat_game_key = "seat-a",
                .positive = true,
            },
            {
                .exact_pair_key = "consistent-control",
                .seat_game_key = "seat-d",
                .positive = false,
            },
            {
                .exact_pair_key = "consistent-control",
                .seat_game_key = "seat-c",
                .positive = false,
            },
            {
                .exact_pair_key = "conflict",
                .seat_game_key = "seat-e",
                .positive = false,
            },
            {
                .exact_pair_key = "conflict",
                .seat_game_key = "seat-f",
                .positive = true,
            },
        });
    expect(
        result.conflicting_pair_keys == 1 &&
            result.retained.size() == 2,
        "DC1 exact-pair dedupe did not drop one conflicting key");
    const auto positive = std::find_if(
        result.retained.begin(), result.retained.end(),
        [](const auto& observation) {
            return observation.exact_pair_key ==
                   "consistent-positive";
        });
    const auto control = std::find_if(
        result.retained.begin(), result.retained.end(),
        [](const auto& observation) {
            return observation.exact_pair_key ==
                   "consistent-control";
        });
    expect(
        positive != result.retained.end() &&
            positive->positive &&
            positive->seat_game_key == "seat-a" &&
            control != result.retained.end() &&
            !control->positive &&
            control->seat_game_key == "seat-c" &&
            std::none_of(
                result.retained.begin(), result.retained.end(),
                [](const auto& observation) {
                    return observation.exact_pair_key ==
                           "conflict";
                }),
        "DC1 exact-pair dedupe retained the wrong deterministic "
        "representatives");
}

void test_dc1_action_summary_enforces_exact_90_action_bound() {
    GameState state;
    state.active_player = 0;
    state.players[0].hand = {CardId::Disintegrate};
    state.players[0].land_played_this_turn = true;
    state.players[0].lands.assign(
        32, old_school::LandPermanent{
                .card = CardId::Mountain,
            });

    const auto summary =
        old_school::probes::summarize_dc1_legal_actions(
            state, 0, true,
            old_school::probes::Dc1MiningConfig{}
                .max_legal_actions);
    expect(
        summary.legal_actions == 65 &&
            summary.action_kinds[
                static_cast<std::size_t>(
                    PriorityActionKind::Pass)] == 1 &&
            summary.action_kinds[
                static_cast<std::size_t>(
                    PriorityActionKind::CastDisintegrate)] == 64 &&
            summary.descriptors_distinct &&
            summary.sorted_descriptor_fnv1a64 != 0,
        "DC1 census did not preserve the complete synthetic "
        "65-action set");

    expect(
        old_school::probes::Dc1MiningConfig{}
                .max_legal_actions == 90,
        "DC1 mining did not use the evidence-bound 90-action "
        "ceiling");

    state.players[0].lands.assign(
        45, old_school::LandPermanent{
                .card = CardId::Mountain,
            });
    const auto over_bound =
        old_school::probes::summarize_dc1_legal_actions(
            state, 0, true, 512);
    expect(
        over_bound.legal_actions == 91 &&
            over_bound.action_kinds[
                static_cast<std::size_t>(
                    PriorityActionKind::Pass)] == 1 &&
            over_bound.action_kinds[
                static_cast<std::size_t>(
                    PriorityActionKind::CastDisintegrate)] == 90,
        "DC1 census did not preserve the complete synthetic "
        "91-action set");

    bool rejected = false;
    try {
        static_cast<void>(
            old_school::probes::summarize_dc1_legal_actions(
                state, 0, true,
                old_school::probes::Dc1MiningConfig{}
                    .max_legal_actions));
    } catch (const std::logic_error&) {
        rejected = true;
    }
    expect(
        rejected,
        "DC1 density bound did not fail closed on a 91-action root");
}

void test_dc1_raw_settlements_conserve_cards_and_context() {
    const DecisionProbe validation =
        old_school::probes::make_probe_validation_v1().front();
    const PriorityAction validation_x_zero =
        PriorityAction::cast_disintegrate(
            0, old_school::Target::player_target(1));
    expect_dc1_raw_card_conservation(
        validation,
        {
            candidate_index(validation, PriorityAction::pass()),
            candidate_index(validation, validation_x_zero),
        },
        577215);

    const auto controls =
        old_school::probes::make_force_spike_policy_controls_v1();
    for (const DecisionProbe& control : controls) {
        expect_dc1_raw_card_conservation(
            control,
            {
                candidate_index(control, "pass"),
                candidate_index(
                    control, "force-spike-gray-ogre"),
            },
            271828);
    }

    const auto dev = old_school::probes::make_probe_dev_v3();
    const DecisionProbe& creature =
        find_probe(dev, Category::GreenDevelop);
    expect_dc1_raw_card_conservation(
        creature,
        {candidate_index(creature, "cast-grizzly-bears")},
        314159);
    const DecisionProbe& growth =
        find_probe(dev, Category::GreenGrowthSaveBolt);
    expect_dc1_raw_card_conservation(
        growth,
        {candidate_index(
            growth, "growth-own-grizzly-bears")},
        314160);
    const DecisionProbe& artifact =
        find_probe(dev, Category::WhiteEstablishMillstone);
    expect_dc1_raw_card_conservation(
        artifact,
        {candidate_index(artifact, "cast-millstone")},
        314161);

    const DecisionProbe& nonzero_pass = controls.front();
    expect(nonzero_pass.consecutive_passes == 1,
           "DC1 context control lost its prior pass");
    const auto world = old_school::sample_determinization(
        nonzero_pass.state, nonzero_pass.original_decks,
        nonzero_pass.root_player, 424242);
    const auto settled =
        old_school::probes::settle_dc1_priority_candidate(
            nonzero_pass, world,
            candidate_index(nonzero_pass, "pass"));
    expect(
        settled.phase == nonzero_pass.phase &&
            settled.settled_state.active_player ==
                nonzero_pass.state.active_player &&
            settled.settled_state.turn_number ==
                nonzero_pass.state.turn_number &&
            settled.settled_state.starting_player ==
                nonzero_pass.state.starting_player &&
            settled.settled_state.extra_turns_pending ==
                nonzero_pass.state.extra_turns_pending &&
            settled.final_priority_player ==
                1 - nonzero_pass.state.active_player &&
            settled.final_consecutive_passes == 2 &&
            settled.window_ended && !settled.terminal &&
            settled.settled_state.stack.empty(),
        "DC1 nonzero-pass root lost exact phase/active/pass/window "
        "settlement context");

    const auto zero_pass_settled =
        old_school::probes::settle_dc1_priority_candidate(
            validation,
            old_school::sample_determinization(
                validation.state, validation.original_decks,
                validation.root_player, 424242),
            candidate_index(validation, PriorityAction::pass()));
    expect(
        validation.consecutive_passes == 0 &&
            zero_pass_settled.phase == validation.phase &&
            zero_pass_settled.settled_state.active_player ==
                validation.state.active_player &&
            zero_pass_settled.settled_state.turn_number ==
                validation.state.turn_number &&
            zero_pass_settled.settled_state.starting_player ==
                validation.state.starting_player &&
            zero_pass_settled.settled_state.extra_turns_pending ==
                validation.state.extra_turns_pending &&
            zero_pass_settled.final_priority_player ==
                1 - validation.root_player &&
            zero_pass_settled.final_consecutive_passes == 2 &&
            zero_pass_settled.window_ended,
        "DC1 zero-pass empty-stack root did not end after exactly "
        "two forced passes");
}

void test_dc1_mox_and_sol_ring_ledgers_are_exact() {
    DecisionProbe mox;
    mox.stable_id = "synthetic.dc1.mox-ancestral";
    mox.root_player = 0;
    mox.phase = TurnPhase::FirstMain;
    mox.state.active_player = 0;
    mox.state.turn_number = 2;
    mox.state.players[0].hand = {CardId::AncestralRecall};
    mox.state.players[0].library = {
        CardId::Island, CardId::Island, CardId::Island,
    };
    constexpr PermanentId kMoxId = 41;
    mox.state.players[0].artifacts = {{
        .id = kMoxId,
        .card = CardId::MoxSapphire,
    }};
    mox.candidates = {{
        .descriptor = "ancestral-self",
        .action = PriorityAction::cast_ancestral_recall(
            old_school::Target::player_target(0)),
    }};

    const auto mox_settlement =
        old_school::probes::settle_dc1_priority_candidate(
            mox, mox.state, 0);
    old_school::probes::Dc1PlayerResourceCost expected_mox;
    expected_mox.hand_cards_consumed[
        static_cast<std::size_t>(CardId::AncestralRecall)] = 1;
    expected_mox.mana_depleted.blue = 1;
    expected_mox.preexisting_sources_newly_tapped.push_back({
        .kind =
            old_school::probes::Dc1ManaSourceKind::Artifact,
        .key = kMoxId,
        .card = CardId::MoxSapphire,
    });
    expect(
        mox_settlement.resources[0] == expected_mox &&
            mox_settlement.resources[1] ==
                old_school::probes::Dc1PlayerResourceCost{} &&
            mox_settlement.settled_state.players[0].hand.size() == 3 &&
            mox_settlement.settled_state.players[0].library.empty(),
        "DC1 Mox Sapphire ledger or resolved Ancestral effect "
        "was not exact");

    DecisionProbe sol;
    sol.stable_id = "synthetic.dc1.sol-disesintegrate";
    sol.root_player = 0;
    sol.phase = TurnPhase::FirstMain;
    sol.state.active_player = 0;
    sol.state.turn_number = 2;
    sol.state.players[0].hand = {CardId::Disintegrate};
    sol.state.players[0].lands = {{
        .card = CardId::Mountain,
    }};
    constexpr PermanentId kSolRingId = 73;
    sol.state.players[0].artifacts = {{
        .id = kSolRingId,
        .card = CardId::SolRing,
    }};
    sol.candidates = {{
        .descriptor = "disintegrate-x1-opponent",
        .action = PriorityAction::cast_disintegrate(
            1, old_school::Target::player_target(1)),
    }};

    const auto sol_settlement =
        old_school::probes::settle_dc1_priority_candidate(
            sol, sol.state, 0);
    old_school::probes::Dc1PlayerResourceCost expected_sol;
    expected_sol.hand_cards_consumed[
        static_cast<std::size_t>(CardId::Disintegrate)] = 1;
    expected_sol.mana_depleted.generic = 1;
    expected_sol.mana_depleted.red = 1;
    expected_sol.preexisting_sources_newly_tapped = {
        {
            .kind =
                old_school::probes::Dc1ManaSourceKind::Land,
            .key = 0,
            .card = CardId::Mountain,
        },
        {
            .kind =
                old_school::probes::Dc1ManaSourceKind::Artifact,
            .key = kSolRingId,
            .card = CardId::SolRing,
        },
    };
    expect(
        sol_settlement.resources[0] == expected_sol &&
            sol_settlement.resources[1] ==
                old_school::probes::Dc1PlayerResourceCost{} &&
            sol_settlement.settled_state.players[1].life == 19 &&
            sol_settlement.settled_state.players[0].mana_pool ==
                old_school::ManaCost{},
        "DC1 Sol Ring ledger did not distinguish one generic paid "
        "from the phase-local excess cleared at window end");
}

void test_dc1_bounded_mining_smoke_is_deterministic() {
    const auto model =
        old_school::train_learned_value_champion(1, 8675309);
    old_school::probes::Dc1MiningConfig config;
    config.training_seed = 101;
    config.heldout_seed = 202;
    config.blocks_per_split = 1;
    config.worlds = 1;
    config.max_roots_per_seat_game = 1;
    config.max_pairs_per_root = 1;
    config.max_game_turns = 2;
    config.training_exploration_rate = 0.10;
    config.training_minimum_examples_per_deck = 0;
    config.training_minimum_seat_games_per_deck = 0;
    config.heldout_minimum_examples_per_deck = 0;
    config.heldout_minimum_seat_games_per_deck = 0;
    config.required_model_fingerprint =
        old_school::learned_model_fingerprint(model);

    const auto first =
        old_school::probes::audit_dc1_dominance_mining(
            model, config);
    const auto second =
        old_school::probes::audit_dc1_dominance_mining(
            model, config);
    expect(first == second,
           "bounded DC1 mining smoke was not deterministic");
    expect(
        first.fixture_gate_passed &&
            first.accounting_passed && first.gate_passed &&
            first.training.games == 40 &&
            first.heldout.games == 40 &&
            first.training.seat_games == 80 &&
            first.heldout.seat_games == 80 &&
            first.training.paired_world_cells ==
                first.training.pair_groups &&
            first.heldout.paired_world_cells ==
                first.heldout.pair_groups &&
            first.training.settlement_operations ==
                first.training.paired_world_cells * 4 &&
            first.heldout.settlement_operations ==
                first.heldout.paired_world_cells * 4,
        "bounded DC1 mining smoke failed exact accounting");
    expect(
        old_school::learned_model_fingerprint(model) ==
            config.required_model_fingerprint,
        "DC1 mining mutated its frozen parent");

    config.required_model_fingerprint.assign(64, '0');
    bool rejected = false;
    try {
        static_cast<void>(
            old_school::probes::audit_dc1_dominance_mining(
                model, config));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected,
           "DC1 mining accepted the wrong parent fingerprint");

    old_school::probes::Dc1MiningConfig equal_seeds = config;
    equal_seeds.heldout_seed = equal_seeds.training_seed;
    rejected = false;
    try {
        static_cast<void>(
            old_school::probes::audit_dc1_dominance_mining(
                model, equal_seeds));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(
        rejected,
        "DC1 mining accepted identical train/held-out seeds");
}

void test_dc1_bounded_action_census_is_deterministic() {
    const auto model =
        old_school::train_learned_value_champion(1, 112358);
    old_school::probes::Dc1ActionCensusConfig config;
    config.training_seed = 101;
    config.heldout_seed = 202;
    config.blocks_per_split = 1;
    config.worlds = 1;
    config.max_game_turns = 2;
    config.training_exploration_rate = 0.10;
    config.threshold = 1;
    config.diagnostic_ceiling = 512;
    config.required_model_fingerprint =
        old_school::learned_model_fingerprint(model);

    const auto first =
        old_school::probes::audit_dc1_action_census(
            model, config);
    const auto second =
        old_school::probes::audit_dc1_action_census(
            model, config);
    expect(
        first == second &&
            first.accounting_passed &&
            first.training.accounting_passed &&
            first.heldout.accounting_passed &&
            first.pair_comparisons == 0 &&
            first.density_examples == 0 &&
            first.training.games == 40 &&
            first.heldout.games == 40 &&
            first.training.seat_games == 80 &&
            first.heldout.seat_games == 80,
        "bounded DC1 action census was nondeterministic or "
        "misaccounted");
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        expect(
            first.training.decks[deck].seat_games == 16 &&
                first.heldout.decks[deck].seat_games == 16,
            "bounded DC1 census lost exact per-deck seat balance");
    }

    old_school::probes::Dc1ActionCensusConfig equal_seeds =
        config;
    equal_seeds.heldout_seed = equal_seeds.training_seed;
    bool rejected = false;
    try {
        static_cast<void>(
            old_school::probes::audit_dc1_action_census(
                model, equal_seeds));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(
        rejected,
        "DC1 action census accepted identical split seeds");
}

std::shared_ptr<const old_school::LearnedModel>
tiny_bsr_model() {
    static const auto model =
        old_school::train_learned_value_champion(
            1, 0xB5A0C16ULL);
    return model;
}

void test_bsr_source_schedule_is_exactly_balanced() {
    const auto first =
        old_school::probes::bsr_source_schedule();
    const auto second =
        old_school::probes::bsr_source_schedule();
    expect(
        first == second &&
            first.size() ==
                old_school::probes::kBsrSourceGames,
        "BSR source schedule is not fixed and reproducible");

    std::array<std::array<std::array<std::size_t, 2>, 2>,
               old_school::kDeckCount>
        counts{};
    for (const auto& game : first) {
        const std::size_t opponent =
            static_cast<std::size_t>(
                game.opponent_deck);
        expect(
            opponent < old_school::kDeckCount &&
                game.block <
                    old_school::probes::kBsrSourceBlocks &&
                game.schedule_index <
                    old_school::probes::
                        kBsrSourceGamesPerBlock &&
                game.tracked_seat < 2 &&
                game.starting_player ==
                    (game.tracked_starts
                         ? game.tracked_seat
                         : 1 - game.tracked_seat),
            "BSR source schedule contains invalid metadata");
        ++counts[opponent][game.tracked_seat]
                [game.tracked_starts ? 1 : 0];
    }
    for (const auto& opponent : counts) {
        for (const auto& seat : opponent) {
            expect(
                seat[0] ==
                        old_school::probes::
                            kBsrSourceBlocks &&
                    seat[1] ==
                        old_school::probes::
                            kBsrSourceBlocks,
                "BSR source schedule lost opponent/seat/play-draw "
                "balance");
        }
    }
}

void test_bsr_owner_filter_retains_binary_and_maps_action() {
    const DecisionProbe probe =
        old_school::probes::
            make_force_spike_policy_controls_v1()
                .front();
    const PriorityAction pass = PriorityAction::pass();
    old_school::LearnedDecisionTracePoint point{
        .state = probe.state,
        .context = {
            .valid = true,
            .phase = probe.phase,
            .decision_player = probe.root_player,
            .consecutive_passes =
                probe.consecutive_passes,
            .sorcery_actions = false,
        },
        .selected_priority_action = pass,
    };
    const auto retained =
        old_school::probes::classify_bsr_trace_root(
            point, probe.root_player);
    expect(
        retained.eligible() &&
            retained.legal_actions.size() == 2 &&
            retained.descriptors.size() == 2 &&
            retained.selected_action_matches == 1 &&
            retained.selected_action_index.has_value() &&
            retained.legal_actions[
                *retained.selected_action_index] == pass,
        "BSR owner filter failed to retain and map a legal "
        "two-action Blue-held stack root");

    point.context.decision_player =
        1 - probe.root_player;
    const auto opponent_held =
        old_school::probes::classify_bsr_trace_root(
            point, probe.root_player);
    expect(
        opponent_held.eligibility ==
            old_school::probes::BsrRootEligibility::
                WrongDecisionOwner &&
            !opponent_held.eligible(),
        "BSR owner filter accepted an opponent-held root");

    point.context.decision_player = probe.root_player;
    point.selected_priority_action =
        PriorityAction::cast_lightning_bolt(
            old_school::Target::player_target(0));
    const auto illegal_mapping =
        old_school::probes::classify_bsr_trace_root(
            point, probe.root_player);
    expect(
        illegal_mapping.eligibility ==
            old_school::probes::BsrRootEligibility::
                SelectedActionNotLegal,
        "BSR trace mapping accepted an action outside the legal set");
}

void test_bsr_reference_is_order_hidden_and_seed_invariant() {
    DecisionProbe probe =
        old_school::probes::
            make_force_spike_policy_controls_v1()
                .front();
    old_school::probes::BsrReferenceConfig config;
    config.seed = 0x1414213562ULL;
    config.scout_worlds = 2;
    config.confirmation_worlds = 2;
    config.horizon_turns = 0;
    config.rollouts_per_world = 1;
    config.evaluation_threads = 1;
    const auto first =
        old_school::probes::score_bsr_priority_probe(
            probe, "pass", tiny_bsr_model(), config);
    std::reverse(
        probe.candidates.begin(), probe.candidates.end());
    const auto reversed =
        old_school::probes::score_bsr_priority_probe(
            probe, "pass", tiny_bsr_model(), config);
    expect(
        first == reversed &&
            first.action_count == 2 &&
            first.scout_seed !=
                first.confirmation_seed &&
            first.descriptor_order_invariant &&
            first.hidden_repartition_eligible &&
            first.hidden_repartition_bit_identical &&
            first.accounting_passed &&
            first.sampled_worlds == 8 &&
            first.rollout_evaluations == 16 &&
            first.terminal_evaluations +
                    first.bootstrapped_evaluations ==
                first.rollout_evaluations,
        "BSR reference changed under candidate order or hidden "
        "repartition, reused seeds, or misaccounted samples");
}

void test_bsr_stable_root_key_binds_actual_and_model_not_hidden() {
    DecisionProbe probe =
        old_school::probes::
            make_force_spike_policy_controls_v1()
                .front();
    const old_school::probes::BsrRootKeyContext provenance{
        .game_seed = 123456789,
        .block = 3,
        .schedule_index = 9,
        .tracked_seat = probe.root_player,
        .tracked_starts = false,
        .trace_ordinal = 17,
    };
    const std::string model_fingerprint =
        old_school::learned_model_fingerprint(
            tiny_bsr_model());
    const std::string pass =
        old_school::probes::bsr_stable_root_fingerprint(
            probe, "pass", model_fingerprint, provenance);
    const std::string force_spike =
        old_school::probes::bsr_stable_root_fingerprint(
            probe, "force-spike-gray-ogre",
            model_fingerprint, provenance);
    const std::string other_model =
        old_school::probes::bsr_stable_root_fingerprint(
            probe, "pass", model_fingerprint + "x",
            provenance);

    DecisionProbe hidden = probe;
    auto& opponent =
        hidden.state.players[1 - hidden.root_player];
    const std::size_t hand_size = opponent.hand.size();
    std::vector<CardId> unknown = opponent.hand;
    unknown.insert(
        unknown.end(), opponent.library.begin(),
        opponent.library.end());
    if (unknown.size() > 1) {
        std::rotate(
            unknown.begin(), unknown.begin() + 1,
            unknown.end());
        std::reverse(unknown.begin(), unknown.end());
    }
    const auto hand_end =
        unknown.begin() +
        static_cast<std::ptrdiff_t>(hand_size);
    opponent.hand.assign(unknown.begin(), hand_end);
    opponent.library.assign(hand_end, unknown.end());
    const std::string hidden_pass =
        old_school::probes::bsr_stable_root_fingerprint(
            hidden, "pass", model_fingerprint,
            provenance);

    expect(
        pass.size() == 16 &&
            pass != force_spike &&
            pass != other_model &&
            pass == hidden_pass,
        "BSR stable-root key failed to bind actual/model identity "
        "or leaked opponent hidden identities");
}

void test_bsr_paired_regret_math() {
    const auto estimate =
        old_school::probes::bsr_paired_regret_estimate(
            {0.9, 0.5, 0.9, 0.5},
            {0.5, 0.5, 0.5, 0.5});
    const double expected_se =
        std::sqrt(0.16 / 3.0 / 4.0);
    expect(
        std::abs(estimate.regret - 0.2) < 1.0e-12 &&
            std::abs(
                estimate.standard_error -
                expected_se) < 1.0e-12 &&
            std::abs(
                estimate.lower_95 -
                (0.2 - 1.96 * expected_se)) <
                1.0e-12,
        "BSR paired regret, standard error, or lower bound is "
        "incorrect");
    const auto constant =
        old_school::probes::bsr_paired_regret_estimate(
            {0.75, 0.75}, {0.50, 0.50});
    expect(
        constant.regret == 0.25 &&
            constant.standard_error == 0.0 &&
            constant.lower_95 == 0.25,
        "BSR zero-variance paired estimate is incorrect");
}

void test_bsr_retention_amendment_is_exact_and_permutation_safe() {
    std::vector<old_school::probes::BsrRetentionCandidate>
        candidates;
    const std::array<std::string, 3> stable_keys = {
        "z-information", "a-information", "b-information"};
    for (std::size_t opponent = 0;
         opponent < old_school::kDeckCount; ++opponent) {
        for (std::size_t loss = 0; loss < 5; ++loss) {
            const std::string loss_key =
                "d" + std::to_string(opponent) + ".l" +
                std::to_string(loss);
            for (std::size_t root = 0; root < 3; ++root) {
                candidates.push_back({
                    .opponent_deck =
                        static_cast<DeckId>(opponent),
                    .source_loss_key = loss_key,
                    .provenance_key =
                        loss_key + ".r" +
                        std::to_string(root),
                    .stable_selection_key =
                        stable_keys[root],
                });
            }
        }
    }

    const auto selected =
        old_school::probes::
            select_bsr_retained_candidate_indices(candidates);
    std::vector<old_school::probes::BsrRetentionCandidate>
        retained;
    std::vector<std::string> retained_identities;
    for (const std::size_t index : selected) {
        retained.push_back(candidates.at(index));
        const auto& candidate = candidates.at(index);
        retained_identities.push_back(
            std::to_string(static_cast<std::size_t>(
                candidate.opponent_deck)) +
            "|" + candidate.source_loss_key + "|" +
            candidate.provenance_key + "|" +
            candidate.stable_selection_key);
    }

    expect(
        retained.size() ==
                old_school::probes::kBsrRetainedRoots &&
            old_school::probes::
                bsr_retention_requirements_met(retained),
        "BSR amendment did not retain exact 8/opponent across "
        "at least four losses with a two-root loss cap");
    for (std::size_t opponent = 0;
         opponent < old_school::kDeckCount; ++opponent) {
        std::vector<std::string> loss_keys;
        std::size_t opponent_roots = 0;
        for (const auto& candidate : retained) {
            if (static_cast<std::size_t>(
                    candidate.opponent_deck) != opponent) {
                continue;
            }
            ++opponent_roots;
            loss_keys.push_back(candidate.source_loss_key);
            expect(
                candidate.stable_selection_key !=
                    "z-information",
                "BSR per-loss selection followed provenance "
                "instead of the information/action key");
        }
        std::sort(loss_keys.begin(), loss_keys.end());
        std::size_t distinct_losses = 0;
        for (auto loss = loss_keys.begin();
             loss != loss_keys.end();) {
            const auto loss_end =
                std::upper_bound(
                    loss, loss_keys.end(), *loss);
            expect(
                static_cast<std::size_t>(
                    std::distance(loss, loss_end)) <=
                    old_school::probes::kBsrRootsPerLoss,
                "BSR retained more than two roots from one loss");
            ++distinct_losses;
            loss = loss_end;
        }
        expect(
            opponent_roots ==
                    old_school::probes::kBsrRootsPerOpponent &&
                distinct_losses == 4,
            "BSR stratum did not retain eight roots from exactly "
            "four earliest source losses");
    }

    std::reverse(candidates.begin(), candidates.end());
    const auto reversed_selected =
        old_school::probes::
            select_bsr_retained_candidate_indices(candidates);
    std::vector<std::string> reversed_identities;
    for (const std::size_t index : reversed_selected) {
        const auto& candidate = candidates.at(index);
        reversed_identities.push_back(
            std::to_string(static_cast<std::size_t>(
                candidate.opponent_deck)) +
            "|" + candidate.source_loss_key + "|" +
            candidate.provenance_key + "|" +
            candidate.stable_selection_key);
    }
    expect(
        retained_identities == reversed_identities,
        "BSR retention changed when candidate input order changed");

    std::vector<old_school::probes::BsrRetentionCandidate>
        seven_in_one_stratum = retained;
    const auto green = std::find_if(
        seven_in_one_stratum.begin(),
        seven_in_one_stratum.end(),
        [](const auto& candidate) {
            return candidate.opponent_deck == DeckId::Green;
        });
    seven_in_one_stratum.erase(green);
    expect(
        !old_school::probes::bsr_retention_requirements_met(
            seven_in_one_stratum),
        "BSR retention accepted a seven-root opponent stratum");

    std::vector<old_school::probes::BsrRetentionCandidate>
        three_loss_stratum = retained;
    std::size_t green_root = 0;
    for (auto& candidate : three_loss_stratum) {
        if (candidate.opponent_deck != DeckId::Green) {
            continue;
        }
        candidate.source_loss_key =
            "green-concentrated-" +
            std::to_string(green_root % 3);
        ++green_root;
    }
    expect(
        !old_school::probes::bsr_retention_requirements_met(
            three_loss_stratum, 3, 8, 4),
        "BSR retention accepted eight roots spanning only three "
        "losses");
}

void test_bsr_practical_gate_boundaries_are_exact() {
    const double just_above_lower = std::nextafter(
        old_school::probes::kBsrPracticalLower95Threshold,
        std::numeric_limits<double>::infinity());
    const double just_below_regret = std::nextafter(
        old_school::probes::kBsrPracticalRegretThreshold,
        0.0);
    const auto estimate =
        [](double regret, double lower) {
            return old_school::probes::BsrPairedRegretEstimate{
                .regret = regret,
                .standard_error = 0.0,
                .lower_95 = lower,
            };
        };
    expect(
        old_school::probes::
            bsr_practical_high_cost_mistake(
                true, true,
                estimate(
                    old_school::probes::
                        kBsrPracticalRegretThreshold,
                    just_above_lower)) &&
            !old_school::probes::
                bsr_practical_high_cost_mistake(
                    true, true,
                    estimate(
                        old_school::probes::
                            kBsrPracticalRegretThreshold,
                        old_school::probes::
                            kBsrPracticalLower95Threshold)) &&
            !old_school::probes::
                bsr_practical_high_cost_mistake(
                    true, true,
                    estimate(
                        just_below_regret,
                        just_above_lower)) &&
            !old_school::probes::
                bsr_practical_high_cost_mistake(
                    false, true,
                    estimate(0.30, 0.20)) &&
            !old_school::probes::
                bsr_practical_high_cost_mistake(
                    true, false,
                    estimate(0.30, 0.20)),
        "BSR practical root gate lost inclusive 0.20 regret, "
        "strict >0.10 lower bound, or stability preconditions");
    const double just_above_zero =
        std::nextafter(
            0.0, std::numeric_limits<double>::infinity());
    const double just_below_diagnostic =
        std::nextafter(
            old_school::probes::
                kBsrDiagnosticRegretThreshold,
            0.0);
    expect(
        old_school::probes::bsr_diagnostic_stable_mistake(
            true, true,
            estimate(
                old_school::probes::
                    kBsrDiagnosticRegretThreshold,
                just_above_zero)) &&
            !old_school::probes::
                bsr_diagnostic_stable_mistake(
                    true, true,
                    estimate(
                        old_school::probes::
                            kBsrDiagnosticRegretThreshold,
                        0.0)) &&
            !old_school::probes::
                bsr_diagnostic_stable_mistake(
                    true, true,
                    estimate(
                        just_below_diagnostic,
                        just_above_zero)),
        "BSR diagnostic root gate lost inclusive 0.05 regret "
        "or strict positive lower-bound edges");
    expect(
        old_school::probes::bsr_practical_audit_gate(true, 1) &&
            !old_school::probes::bsr_practical_audit_gate(
                true, 0) &&
            !old_school::probes::bsr_practical_audit_gate(
                false, 1),
        "BSR aggregate practical gate ignored audit validity or "
        "mistake count");
}

void test_bsr_bounded_source_accounting_smoke() {
    old_school::probes::BsrAuditConfig config;
    config.source_seed = 0x1618033ULL;
    config.source_blocks = 1;
    config.source_max_turns = 3;
    config.production_worlds = 1;
    config.roots_per_loss = 1;
    config.roots_per_opponent = 1;
    config.minimum_losses_per_opponent = 1;
    config.reference.scout_worlds = 1;
    config.reference.confirmation_worlds = 1;
    config.reference.horizon_turns = 0;
    config.reference.rollouts_per_world = 1;
    config.reference.evaluation_threads = 1;
    config.required_model_fingerprint =
        old_school::learned_model_fingerprint(
            tiny_bsr_model());

    const auto report =
        old_school::probes::
            audit_bsr_blue_stack_regret(
                tiny_bsr_model(), config);
    expect(
        report.source_balance_passed &&
            report.traced_actions_valid &&
            report.accounting_passed &&
            report.bounds_passed &&
            report.source_games == 20 &&
            report.schedule.size() == 20,
        "bounded BSR source smoke failed exact balance, action "
        "trace, accounting, or bounds");
    for (const auto& cell : report.source_cells) {
        expect(
            cell.games == 1,
            "bounded BSR source cell did not contain one game");
    }
    for (const auto& deck : report.decks) {
        expect(
            deck.games == 4,
            "bounded BSR opponent stratum did not contain four "
            "games");
    }
    std::size_t expected_evaluations = 0;
    for (const auto& root : report.roots) {
        expected_evaluations +=
            root.action_count * 4;
    }
    expect(
        report.reference_rollout_evaluations ==
                expected_evaluations &&
            old_school::learned_model_fingerprint(
                tiny_bsr_model()) ==
                config.required_model_fingerprint,
        "bounded BSR reference accounting changed or mutated the "
        "frozen model");
}

} // namespace

int main() {
    TestRunner runner;
    runner.run("v3 corpus shape and candidate schema",
               test_corpus_shape_and_candidate_schema);
    runner.run("all v3 fixtures validate",
               test_every_probe_passes_each_validation_dimension);
    runner.run("exact physical conservation",
               test_card_conservation_rejects_missing_physical_card);
    runner.run("RU public exile conservation",
               test_exile_is_a_conserved_public_zone);
    runner.run("priority legality and completeness",
               test_priority_validation_rejects_illegal_or_incomplete_set);
    runner.run("Flying Men attack reachability",
               test_attack_reachability_checks_flying_through_moat);
    runner.run("hidden-zone clone invariance",
               test_hidden_zone_clones_are_observation_invariant);
    runner.run("Red last-opportunity timing",
               test_red_last_opportunity_timing);
    runner.run("White plan passes cannot heal",
               test_white_plan_passes_cannot_heal);
    runner.run("public mana supports deployed creatures",
               test_public_mana_supports_deployed_creatures);
    runner.run("Counterspell stack targets",
               test_counter_war_lists_every_legal_spell_target);
    runner.run("Force Spike live counter",
               test_force_spike_probe_is_a_live_mana_advantage_counter);
    runner.run("Force Spike policy controls",
               test_force_spike_policy_controls_isolate_payable_tax);
    runner.run("response priority context",
               test_response_windows_record_the_casters_pass);
    runner.run("unique v3 stable IDs and categories",
               test_corpus_rejects_duplicate_stable_id_and_category);
    runner.run("existing lethal stack traces",
               test_existing_lethal_stack_branches_apply_exactly);
    runner.run("Giant Growth saves Bolt target",
               test_giant_growth_saves_bolt_target);
    runner.run("Giant Growth push and hold traces",
               test_giant_growth_push_and_hold_traces);
    runner.run("RU land and blocker traces",
               test_ru_land_color_and_blocker_traces);
    runner.run("RU flying and Disintegrate traces",
               test_ru_flying_and_disintegrate_traces);
    runner.run("reject-only field corpus validation",
               test_field_regressions_are_separate_and_rules_valid);
    runner.run("field RU chump-block consequences",
               test_field_ru_chump_block_rules_consequences);
    runner.run("field sick-Bear Growth consequences",
               test_field_second_main_sick_bear_growth_consequences);
    runner.run("field Growth and linked attack consequences",
               test_field_growth_on_air_has_linked_attack_consequences);
    runner.run("harvested validation-v1 reproducibility",
               test_harvested_validation_probe_is_reproducible_and_valid);
    runner.run("harvested X=0 hold-versus-waste trace",
               test_harvested_x_zero_has_no_effect_and_pass_holds);
    runner.run("DC1 X=0 strict dominance",
               test_dc1_x_zero_is_strictly_dominated_and_hidden_exact);
    runner.run("DC1 payable Force Spike tradeoff",
               test_dc1_payable_force_spike_is_a_tradeoff);
    runner.run("DC1 productive-action controls",
               test_dc1_productive_actions_do_not_trigger);
    runner.run("DC1 order invariance and fail-closed roots",
               test_dc1_order_invariance_and_malformed_fail_closed);
    runner.run("DC1 exact-pair conflict dedupe",
               test_dc1_pair_label_dedupe_drops_conflicts);
    runner.run("DC1 exact 90-action mining bound",
               test_dc1_action_summary_enforces_exact_90_action_bound);
    runner.run("DC1 raw conservation and stop context",
               test_dc1_raw_settlements_conserve_cards_and_context);
    runner.run("DC1 Mox and Sol Ring exact ledgers",
               test_dc1_mox_and_sol_ring_ledgers_are_exact);
    runner.run("DC1 bounded deterministic mining smoke",
               test_dc1_bounded_mining_smoke_is_deterministic);
    runner.run("DC1 bounded deterministic action census",
               test_dc1_bounded_action_census_is_deterministic);
    runner.run("BSR exact source schedule",
               test_bsr_source_schedule_is_exactly_balanced);
    runner.run("BSR owner filter and binary action mapping",
               test_bsr_owner_filter_retains_binary_and_maps_action);
    runner.run("BSR reference invariances and seed split",
               test_bsr_reference_is_order_hidden_and_seed_invariant);
    runner.run("BSR stable root identity",
               test_bsr_stable_root_key_binds_actual_and_model_not_hidden);
    runner.run("BSR paired regret math",
               test_bsr_paired_regret_math);
    runner.run("BSR amended exact retention",
               test_bsr_retention_amendment_is_exact_and_permutation_safe);
    runner.run("BSR practical gate boundaries",
               test_bsr_practical_gate_boundaries_are_exact);
    runner.run("BSR bounded source accounting smoke",
               test_bsr_bounded_source_accounting_smoke);
    return runner.finish();
}
