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
using old_school::GameState;
using old_school::PermanentId;
using old_school::PriorityAction;
using old_school::PriorityActionKind;
using old_school::PriorityPassResult;
using old_school::PriorityState;
using old_school::TurnPhase;
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
        std::cout
            << passed_
            << " probe tests passed (20 dev fixtures + 1 harvested "
               "validation fixture + 2 supplemental Force Spike "
               "controls)\n";
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
    runner.run("harvested validation-v1 reproducibility",
               test_harvested_validation_probe_is_reproducible_and_valid);
    runner.run("harvested X=0 hold-versus-waste trace",
               test_harvested_x_zero_has_no_effect_and_pass_holds);
    return runner.finish();
}
