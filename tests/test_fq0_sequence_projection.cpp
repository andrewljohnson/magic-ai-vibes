#include "old_school/artifact_integrity.hpp"
#include "old_school/fq0_information_set.hpp"
#include "old_school/fq0_sequence_projection.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fq0 = old_school::fq0_information_set;
namespace projection =
    old_school::fq0_sequence_projection;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

old_school::GameState projection_state() {
    using old_school::CardId;
    old_school::GameState state;
    state.active_player = 0;
    state.starting_player = 1;
    state.turn_number = 5;
    state.next_permanent_id = 1;
    state.next_stack_object_id = 1;

    state.players[0].life = 15;
    state.players[0].library = {
        CardId::Forest,
        CardId::GiantGrowth,
        CardId::IronrootTreefolk,
    };
    state.players[0].hand = {
        CardId::GrizzlyBears,
        CardId::Forest,
    };
    state.players[0].graveyard = {
        CardId::LightningBolt,
        CardId::GrizzlyBears,
    };
    state.players[0].lands = {
        {.card = CardId::Forest, .tapped = false},
        {.card = CardId::Forest, .tapped = false},
    };

    state.players[1].life = 17;
    state.players[1].library = {
        CardId::AirElemental,
        CardId::Mountain,
        CardId::Counterspell,
    };
    state.players[1].hand = {
        CardId::Island,
        CardId::ForceSpike,
    };
    state.players[1].graveyard = {
        CardId::FlyingMen,
        CardId::Counterspell,
    };
    state.players[1].lands = {
        {.card = CardId::Island, .tapped = false},
    };
    return state;
}

old_school::LearnedDecisionContext projection_context() {
    return {
        .valid = true,
        .phase = old_school::TurnPhase::FirstMain,
        .decision_player = 0,
        .consecutive_passes = 0,
        .sorcery_actions = true,
    };
}

std::vector<old_school::PriorityAction> actions_for(
    const old_school::GameState& state) {
    const auto context = projection_context();
    const auto actions =
        old_school::legal_priority_actions(
            state, context.decision_player,
            context.sorcery_actions);
    expect(
        actions.size() >= 2,
        "projection fixture lost its multi-action root");
    return actions;
}

std::string legacy_information_hash(
    const old_school::GameState& state,
    const std::vector<old_school::PriorityAction>& actions) {
    return fq0::information_set_sha256(
        fq0::make_information_set_key(
            state, projection_context(), actions));
}

std::string wrapped(
    std::string_view domain,
    std::string_view legacy_sha256) {
    std::string payload;
    payload.reserve(
        domain.size() + 1U + legacy_sha256.size());
    payload.append(domain);
    payload.push_back('\0');
    payload.append(legacy_sha256);
    return old_school::artifact_integrity::sha256_string(
        payload);
}

void sort_graveyards(old_school::GameState& state) {
    for (old_school::PlayerState& player :
         state.players) {
        std::sort(
            player.graveyard.begin(),
            player.graveyard.end());
    }
}

void test_graveyard_order_is_quotiented() {
    const old_school::GameState baseline =
        projection_state();
    old_school::GameState permuted = baseline;
    std::reverse(
        permuted.players[0].graveyard.begin(),
        permuted.players[0].graveyard.end());
    std::reverse(
        permuted.players[1].graveyard.begin(),
        permuted.players[1].graveyard.end());
    const auto actions = actions_for(baseline);
    const auto context = projection_context();
    const auto pass = old_school::PriorityAction::pass();

    expect(
        legacy_information_hash(baseline, actions) !=
            legacy_information_hash(permuted, actions),
        "legacy information hash did not retain graveyard order");
    expect(
        fq0::redacted_leaf_consequence_sha256(
            baseline, 0, context) !=
            fq0::redacted_leaf_consequence_sha256(
                permuted, 0, context),
        "legacy leaf hash did not retain graveyard order");
    expect(
        fq0::canonical_priority_consequence_sha256(
            baseline, 0, context, pass) !=
            fq0::canonical_priority_consequence_sha256(
                permuted, 0, context, pass),
        "legacy Priority hash did not retain graveyard order");

    expect(
        projection::
                graveyard_quotient_information_set_sha256(
                    baseline, context, actions) ==
            projection::
                graveyard_quotient_information_set_sha256(
                    permuted, context, actions),
        "graveyard order changed quotient information identity");
    expect(
        projection::
                graveyard_quotient_leaf_consequence_sha256(
                    baseline, 0, context) ==
            projection::
                graveyard_quotient_leaf_consequence_sha256(
                    permuted, 0, context),
        "graveyard order changed quotient leaf consequence");
    expect(
        projection::
                graveyard_quotient_priority_consequence_sha256(
                    baseline, 0, context, pass) ==
            projection::
                graveyard_quotient_priority_consequence_sha256(
                    permuted, 0, context, pass),
        "graveyard order changed quotient Priority consequence");
}

void test_public_semantics_remain_distinct() {
    const old_school::GameState baseline =
        projection_state();
    const auto actions = actions_for(baseline);
    const auto context = projection_context();
    const auto pass = old_school::PriorityAction::pass();
    const auto information =
        projection::
            graveyard_quotient_information_set_sha256(
                baseline, context, actions);
    const auto leaf =
        projection::
            graveyard_quotient_leaf_consequence_sha256(
                baseline, 0, context);
    const auto priority =
        projection::
            graveyard_quotient_priority_consequence_sha256(
                baseline, 0, context, pass);

    old_school::GameState changed_graveyard = baseline;
    changed_graveyard.players[0].graveyard[0] =
        old_school::CardId::HillGiant;
    expect(
        projection::
                graveyard_quotient_information_set_sha256(
                    changed_graveyard, context, actions) !=
            information &&
            projection::
                    graveyard_quotient_leaf_consequence_sha256(
                        changed_graveyard, 0, context) !=
                leaf &&
            projection::
                    graveyard_quotient_priority_consequence_sha256(
                        changed_graveyard, 0, context, pass) !=
                priority,
        "changed graveyard multiset collapsed in quotient hashes");

    old_school::GameState changed_life = baseline;
    --changed_life.players[1].life;
    expect(
        projection::
                graveyard_quotient_information_set_sha256(
                    changed_life, context, actions) !=
            information &&
            projection::
                    graveyard_quotient_leaf_consequence_sha256(
                        changed_life, 0, context) !=
                leaf &&
            projection::
                    graveyard_quotient_priority_consequence_sha256(
                        changed_life, 0, context, pass) !=
                priority,
        "changed public life total collapsed in quotient hashes");
}

void test_hidden_repartition_remains_aliased() {
    const old_school::GameState baseline =
        projection_state();
    old_school::GameState hidden = baseline;
    hidden.players[1].hand = {
        old_school::CardId::AirElemental,
        old_school::CardId::Mountain,
    };
    hidden.players[1].library = {
        old_school::CardId::Island,
        old_school::CardId::ForceSpike,
        old_school::CardId::Counterspell,
    };
    const auto actions = actions_for(baseline);
    const auto context = projection_context();
    const auto pass = old_school::PriorityAction::pass();

    expect(
        actions_for(hidden) == actions,
        "hidden repartition changed authoritative actions");
    expect(
        projection::
                graveyard_quotient_information_set_sha256(
                    hidden, context, actions) ==
            projection::
                graveyard_quotient_information_set_sha256(
                    baseline, context, actions) &&
            projection::
                    graveyard_quotient_leaf_consequence_sha256(
                        hidden, 0, context) ==
                projection::
                    graveyard_quotient_leaf_consequence_sha256(
                        baseline, 0, context) &&
            projection::
                    graveyard_quotient_priority_consequence_sha256(
                        hidden, 0, context, pass) ==
                projection::
                    graveyard_quotient_priority_consequence_sha256(
                        baseline, 0, context, pass),
        "opponent hidden hand/library repartition entered "
        "quotient hashes");
}

void test_domains_are_exact_and_source_is_unchanged() {
    const old_school::GameState source =
        projection_state();
    const old_school::GameState before = source;
    const auto actions = actions_for(source);
    const auto actions_before = actions;
    const auto context = projection_context();
    const auto pass = old_school::PriorityAction::pass();

    const auto information =
        projection::
            graveyard_quotient_information_set_sha256(
                source, context, actions);
    const auto leaf =
        projection::
            graveyard_quotient_leaf_consequence_sha256(
                source, 0, context);
    const auto priority =
        projection::
            graveyard_quotient_priority_consequence_sha256(
                source, 0, context, pass);

    old_school::GameState canonical = source;
    sort_graveyards(canonical);
    const auto canonical_actions = actions_for(canonical);
    expect(
        information ==
            wrapped(
                "old-school-fq0-graveyard-quotient-"
                "information-set-v1",
                legacy_information_hash(
                    canonical, canonical_actions)),
        "information quotient domain framing drifted");
    expect(
        leaf ==
            wrapped(
                "old-school-fq0-graveyard-quotient-"
                "leaf-consequence-v1",
                fq0::redacted_leaf_consequence_sha256(
                    canonical, 0, context)),
        "leaf quotient domain framing drifted");
    expect(
        priority ==
            wrapped(
                "old-school-fq0-graveyard-quotient-"
                "priority-consequence-v1",
                fq0::canonical_priority_consequence_sha256(
                    canonical, 0, context, pass)),
        "Priority quotient domain framing drifted");
    expect(
        information.size() == 64 && leaf.size() == 64 &&
            priority.size() == 64 &&
            information != leaf && leaf != priority &&
            information != priority,
        "quotient hashes are not stable domain-separated SHA-256");

    expect(
        projection::
                graveyard_quotient_information_set_sha256(
                    source, context, actions) ==
            information &&
            projection::
                    graveyard_quotient_leaf_consequence_sha256(
                        source, 0, context) ==
                leaf &&
            projection::
                    graveyard_quotient_priority_consequence_sha256(
                        source, 0, context, pass) ==
                priority,
        "repeated quotient hashing was not deterministic");
    expect(
        source == before && actions == actions_before,
        "quotient hashing mutated its source state or actions");
}

} // namespace

int main() {
    try {
        test_graveyard_order_is_quotiented();
        test_public_semantics_remain_distinct();
        test_hidden_repartition_remains_aliased();
        test_domains_are_exact_and_source_is_unchanged();
        std::cout << "4 FQ0 sequence projection tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "FQ0 sequence projection test failure: "
            << error.what() << '\n';
        return 1;
    }
}
