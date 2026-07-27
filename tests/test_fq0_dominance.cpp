#include "old_school/fq0_dominance.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace dominance = old_school::fq0_dominance;
using old_school::ArtifactPermanent;
using old_school::CardId;
using old_school::CreaturePermanent;
using old_school::LandPermanent;
using old_school::TurnPhase;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

dominance::Settlement base_settlement() {
    dominance::Settlement settlement;
    settlement.root_information_fingerprint =
        "test-root-information";
    settlement.root_state.active_player = 0;
    settlement.root_state.starting_player = 0;
    settlement.root_state.turn_number = 9;
    settlement.root_state.players[0].library = {
        CardId::Forest, CardId::GrizzlyBears};
    settlement.root_state.players[1].library = {
        CardId::Mountain, CardId::LightningBolt};
    settlement.boundary_state = settlement.root_state;
    settlement.boundary_state.active_player = 1;
    settlement.boundary_state.turn_number = 10;
    settlement.boundary_context = {
        .valid = true,
        .phase = TurnPhase::FirstMain,
        .decision_player = 1,
        .consecutive_passes = 0,
        .sorcery_actions = true,
    };
    settlement.complete = true;
    return settlement;
}

dominance::ResourceSource land_source(
    std::size_t index, CardId card) {
    return {
        .kind = dominance::SourceKind::Land,
        .key = static_cast<std::uint64_t>(index),
        .card = card,
    };
}

void add_spell_cost(dominance::Settlement& settlement,
                    CardId card, CardId land,
                    std::size_t land_index) {
    auto& cost = settlement.costs[0];
    cost.hand_cards_consumed[
        static_cast<std::size_t>(card)] = 1;
    if (land == CardId::Forest) {
        cost.mana_depleted.green = 1;
    } else if (land == CardId::Mountain) {
        cost.mana_depleted.red = 1;
    } else if (land == CardId::Island) {
        cost.mana_depleted.blue = 1;
    } else if (land == CardId::Plains) {
        cost.mana_depleted.white = 1;
    } else {
        throw std::runtime_error("test source is not a basic land");
    }
    cost.preexisting_sources_newly_tapped.push_back(
        land_source(land_index, land));
}

std::pair<dominance::Settlement, dominance::Settlement>
no_effect_spell_pair(CardId spell, CardId land) {
    dominance::Settlement pass = base_settlement();
    pass.root_state.players[0].hand = {spell};
    pass.root_state.players[0].lands = {{.card = land}};
    pass.boundary_state = pass.root_state;
    pass.boundary_state.active_player = 1;
    pass.boundary_state.turn_number = 10;

    dominance::Settlement cast = pass;
    cast.boundary_state.players[0].hand.clear();
    cast.boundary_state.players[0].graveyard.push_back(spell);
    cast.boundary_state.players[0].lands[0].tapped = true;
    add_spell_cost(cast, spell, land, 0);
    return {std::move(pass), std::move(cast)};
}

void test_pass_dominates_sick_bear_growth() {
    auto [pass, growth] = no_effect_spell_pair(
        CardId::GiantGrowth, CardId::Forest);
    pass.root_state.players[0].creatures.push_back({
        .id = 1,
        .card = CardId::GrizzlyBears,
        .summoning_sick = true,
    });
    pass.boundary_state.players[0].creatures =
        pass.root_state.players[0].creatures;
    growth.root_state = pass.root_state;
    growth.boundary_state.players[0].creatures =
        pass.boundary_state.players[0].creatures;

    const dominance::Comparison comparison =
        dominance::compare(pass, growth, 0);
    expect(
        comparison.orientation ==
            dominance::Orientation::FirstDominatesSecond,
        "Pass did not dominate second-main sick-Bear Growth");
    expect(
        comparison.first_normalized &&
            comparison.second_normalized &&
            comparison.consequences_equal,
        "sick-Bear branches did not normalize exactly");
}

void test_pass_dominates_x_zero_spends() {
    for (const auto [spell, land] :
         std::vector<std::pair<CardId, CardId>>{
             {CardId::Braingeyser, CardId::Island},
             {CardId::Disintegrate, CardId::Mountain}}) {
        const auto [pass, cast] =
            no_effect_spell_pair(spell, land);
        expect(
            dominance::compare(pass, cast, 0).orientation ==
                dominance::Orientation::
                    FirstDominatesSecond,
            "Pass did not dominate an X=0 no-effect spend");
    }
}

std::pair<dominance::Settlement, dominance::Settlement>
force_spike_pair(bool live) {
    dominance::Settlement pass = base_settlement();
    pass.root_information_fingerprint =
        live ? "live-force" : "payable-force";
    pass.root_state.active_player = 1;
    pass.root_state.players[0].hand = {CardId::ForceSpike};
    pass.root_state.players[0].lands = {
        {.card = CardId::Island}};
    pass.root_state.players[1].lands = {
        {.card = CardId::Mountain}};
    pass.boundary_state = pass.root_state;
    pass.boundary_state.active_player = 0;
    pass.boundary_state.turn_number = 10;
    pass.boundary_context.decision_player = 0;
    pass.boundary_state.players[1].creatures.push_back({
        .id = 20,
        .card = CardId::GrayOgre,
        .summoning_sick = true,
    });

    dominance::Settlement spike = pass;
    spike.boundary_state.players[0].hand.clear();
    spike.boundary_state.players[0].graveyard.push_back(
        CardId::ForceSpike);
    add_spell_cost(
        spike, CardId::ForceSpike, CardId::Island, 0);
    if (live) {
        spike.boundary_state.players[1].creatures.clear();
        spike.boundary_state.players[1].graveyard.push_back(
            CardId::GrayOgre);
    } else {
        spike.boundary_state.players[1].lands[0].tapped = true;
        auto& opponent_cost = spike.costs[1];
        opponent_cost.mana_depleted.red = 1;
        opponent_cost.preexisting_sources_newly_tapped.push_back(
            land_source(0, CardId::Mountain));
    }
    return {std::move(pass), std::move(spike)};
}

void test_payable_and_live_force_are_incomparable() {
    const auto [payable_pass, payable_spike] =
        force_spike_pair(false);
    const dominance::Comparison payable =
        dominance::compare(payable_pass, payable_spike, 0);
    expect(
        payable.first_normalized &&
            payable.second_normalized &&
            payable.consequences_equal &&
            payable.orientation ==
                dominance::Orientation::Incomparable,
        "payable Force Spike lost its two-sided tradeoff");

    const auto [live_pass, live_spike] =
        force_spike_pair(true);
    const dominance::Comparison live =
        dominance::compare(live_pass, live_spike, 0);
    expect(
        live.first_normalized && live.second_normalized &&
            !live.consequences_equal &&
            live.orientation ==
                dominance::Orientation::Incomparable,
        "live Force Spike's different public effect was erased");
}

void test_transient_combat_effect_fails_closed() {
    auto [pass, growth] = no_effect_spell_pair(
        CardId::GiantGrowth, CardId::Forest);
    growth.unresolved_transient_choice_effect = true;
    const dominance::Comparison comparison =
        dominance::compare(pass, growth, 0);
    expect(
        comparison.orientation ==
                dominance::Orientation::Incomparable &&
            comparison.first_normalized &&
            !comparison.second_normalized,
        "begin-combat transient option value did not fail closed");
}

dominance::Settlement permutation_settlement(bool reversed) {
    dominance::Settlement settlement = base_settlement();
    settlement.root_information_fingerprint =
        "permutation-root";
    settlement.root_state.players[0].hand = {
        CardId::Disintegrate};
    settlement.root_state.players[0].lands = reversed
        ? std::vector<LandPermanent>{
              {.card = CardId::Mountain},
              {.card = CardId::Forest}}
        : std::vector<LandPermanent>{
              {.card = CardId::Forest},
              {.card = CardId::Mountain}};
    settlement.boundary_state = settlement.root_state;
    settlement.boundary_state.active_player = 1;
    settlement.boundary_state.turn_number = 10;
    settlement.boundary_state.players[0].hand.clear();
    settlement.boundary_state.players[0].graveyard = {
        CardId::Disintegrate};
    const std::size_t mountain = reversed ? 0 : 1;
    settlement.boundary_state.players[0]
        .lands[mountain]
        .tapped = true;
    add_spell_cost(
        settlement, CardId::Disintegrate,
        CardId::Mountain, mountain);

    settlement.boundary_state.players[0].creatures = {
        {
            .id = reversed ? 900U : 10U,
            .card = CardId::AirElemental,
            .summoning_sick = false,
        },
        {
            .id = reversed ? 800U : 20U,
            .card = CardId::GrizzlyBears,
            .summoning_sick = false,
        },
    };
    settlement.boundary_state.players[0].artifacts = {
        {
            .id = reversed ? 700U : 30U,
            .card = CardId::Millstone,
        },
        {
            .id = reversed ? 600U : 40U,
            .card = CardId::SolRing,
        },
    };
    if (reversed) {
        std::reverse(
            settlement.boundary_state.players[0]
                .creatures.begin(),
            settlement.boundary_state.players[0]
                .creatures.end());
        std::reverse(
            settlement.boundary_state.players[0]
                .artifacts.begin(),
            settlement.boundary_state.players[0]
                .artifacts.end());
        std::reverse(
            settlement.boundary_state.players[0].lands.begin(),
            settlement.boundary_state.players[0].lands.end());
    }
    settlement.boundary_state.next_permanent_id =
        reversed ? 999 : 50;
    settlement.boundary_state.next_stack_object_id =
        reversed ? 888 : 60;
    settlement.boundary_state.stats[0].decisions =
        reversed ? 123 : 1;
    return settlement;
}

void test_object_and_source_permutations_are_equivalent() {
    dominance::Settlement first =
        permutation_settlement(false);
    dominance::Settlement second =
        permutation_settlement(true);
    const dominance::CanonicalSettlement canonical_first =
        dominance::canonicalize_settlement(first, 0);
    const dominance::CanonicalSettlement canonical_second =
        dominance::canonicalize_settlement(second, 0);
    expect(
        canonical_first.valid && canonical_second.valid &&
            canonical_first == canonical_second,
        "permutation-equivalent objects or sources changed "
        "canonical settlement");
    const dominance::Comparison comparison =
        dominance::compare(first, second, 0);
    expect(
        comparison.consequences_equal &&
            comparison.orientation ==
                dominance::Orientation::Incomparable,
        "equal-cost permutations produced strict dominance");
}

void test_hidden_zones_are_redacted_but_own_hand_is_not() {
    dominance::Settlement original = base_settlement();
    original.root_information_fingerprint = "hidden-root";
    original.boundary_state.players[0].hand = {
        CardId::Forest};
    original.boundary_state.players[0].library = {
        CardId::GiantGrowth, CardId::GrizzlyBears};
    original.boundary_state.players[1].hand = {
        CardId::LightningBolt, CardId::Mountain};
    original.boundary_state.players[1].library = {
        CardId::GrayOgre, CardId::HillGiant};

    dominance::Settlement repartition = original;
    std::swap(
        repartition.boundary_state.players[1].hand[0],
        repartition.boundary_state.players[1].library[0]);
    std::reverse(
        repartition.boundary_state.players[0].library.begin(),
        repartition.boundary_state.players[0].library.end());
    std::reverse(
        repartition.boundary_state.players[1].library.begin(),
        repartition.boundary_state.players[1].library.end());
    expect(
        dominance::canonicalize_settlement(original, 0) ==
            dominance::canonicalize_settlement(
                repartition, 0),
        "opponent hidden repartition or future library order "
        "leaked into the consequence");

    dominance::Settlement own_hand = original;
    own_hand.boundary_state.players[0].hand[0] =
        CardId::Mountain;
    const auto own_hand_canonical =
        dominance::canonicalize_settlement(own_hand, 0);
    expect(
        own_hand_canonical.valid &&
            own_hand_canonical !=
                dominance::canonicalize_settlement(
                    original, 0),
        "owner hand identity was redacted");
}

void test_ambiguous_restoration_and_source_removal_fail_closed() {
    auto [pass, ambiguous] = no_effect_spell_pair(
        CardId::GiantGrowth, CardId::Forest);
    static_cast<void>(pass);
    ambiguous.boundary_state.players[0].graveyard.push_back(
        CardId::GiantGrowth);
    expect(
        !dominance::canonicalize_settlement(ambiguous, 0).valid,
        "ambiguous consumed-card destination was guessed");

    auto [held, removed] = no_effect_spell_pair(
        CardId::GiantGrowth, CardId::Forest);
    removed.boundary_state.players[0].lands.clear();
    const dominance::Comparison comparison =
        dominance::compare(held, removed, 0);
    expect(
        !comparison.second_normalized &&
            comparison.orientation ==
                dominance::Orientation::Incomparable,
        "removed preexisting source was restored speculatively");

    dominance::Settlement invented = base_settlement();
    invented.costs[0].hand_cards_consumed[
        static_cast<std::size_t>(CardId::GiantGrowth)] = 1;
    invented.boundary_state.players[0].graveyard = {
        CardId::GiantGrowth};
    expect(
        !dominance::canonicalize_settlement(invented, 0).valid,
        "resource ledger consumed a card absent from the root hand");
}

class Runner {
  public:
    void run(std::string name,
             const std::function<void()>& test) {
        try {
            test();
            ++passed_;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& failure) {
            ++failed_;
            std::cerr << "[FAIL] " << name << ": "
                      << failure.what() << '\n';
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

} // namespace

int main() {
    Runner runner;
    runner.run("Pass dominates sick-Bear Growth",
               test_pass_dominates_sick_bear_growth);
    runner.run("Pass dominates X=0 spends",
               test_pass_dominates_x_zero_spends);
    runner.run("Force Spike controls stay incomparable",
               test_payable_and_live_force_are_incomparable);
    runner.run("transient combat effect fails closed",
               test_transient_combat_effect_fails_closed);
    runner.run("object and source permutation equivalence",
               test_object_and_source_permutations_are_equivalent);
    runner.run("hidden redaction and own-hand sensitivity",
               test_hidden_zones_are_redacted_but_own_hand_is_not);
    runner.run("ambiguous restoration fails closed",
               test_ambiguous_restoration_and_source_removal_fail_closed);
    return runner.finish();
}
