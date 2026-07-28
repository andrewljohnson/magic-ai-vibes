#include "old_school/fq4_work0_cache.hpp"

#include "old_school/fq0_information_set.hpp"
#include "old_school/fq4_dev_schedule.hpp"
#include "old_school/fq4_neutral_supplement.hpp"
#include "old_school/probes.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace bundle = old_school::fq4_dev_bundle;
namespace cache = old_school::fq4_work0_cache;
namespace collection =
    old_school::fq4_priority_collection;
namespace information =
    old_school::fq0_information_set;
namespace neutral =
    old_school::fq4_neutral_supplement;
namespace probes = old_school::probes;
namespace schedule = old_school::fq4_dev_schedule;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Function>
void expect_rejected(
    Function&& function, std::string_view message) {
    bool rejected = false;
    try {
        function();
    } catch (const std::exception&) {
        rejected = true;
    }
    expect(rejected, message);
}

struct Fixture {
    probes::DecisionProbe probe;
    schedule::SourceGame source;
};

Fixture make_fixture() {
    const std::vector<probes::DecisionProbe> corpus =
        probes::make_probe_dev_v3();
    const auto games =
        schedule::source_schedule(
            schedule::Split::Fit);
    for (const probes::DecisionProbe& probe : corpus) {
        if (probe.decision_kind !=
                probes::DecisionKind::Priority ||
            probe.candidates.size() < 2 ||
            probe.root_player >= 2 ||
            probe.state.failed_draw !=
                std::array<bool, 2>{false, false}) {
            continue;
        }
        const std::size_t owner = probe.root_player;
        const auto found = std::find_if(
            games.begin(), games.end(),
            [&](const schedule::SourceGame& game) {
                return
                    game.seat_decks[owner] ==
                        probe.root_deck &&
                    game.seat_decks[1 - owner] ==
                        probe.opponent_deck &&
                    game.starting_player ==
                        probe.state.starting_player;
            });
        if (found != games.end()) {
            return {
                .probe = probe,
                .source = *found,
            };
        }
    }
    throw std::runtime_error(
        "no portable Priority probe matched the frozen schedule");
}

std::array<std::uint8_t, old_school::kCardCount>
composition(const std::vector<old_school::CardId>& deck) {
    std::array<
        std::uint8_t, old_school::kCardCount>
        result{};
    for (const old_school::CardId card : deck) {
        ++result[static_cast<std::size_t>(card)];
    }
    return result;
}

cache::OwnerVisibleState visible_state(
    const old_school::GameState& state,
    std::size_t observer) {
    const old_school::PlayerObservation observation =
        old_school::observe_game_state(
            state, observer);
    expect(
        !observation.revealed_opponent_hand.has_value(),
        "ordinary observation exposed opponent hand");
    return {
        .observer = observation.observer,
        .players = observation.players,
        .owner_hand = observation.hand,
        .stack = observation.stack,
        .extra_turns_pending =
            observation.extra_turns_pending,
        .active_player = observation.active_player,
        .starting_player =
            observation.starting_player,
        .turn_number = observation.turn_number,
        .failed_draw = state.failed_draw,
        .next_permanent_id =
            state.next_permanent_id,
        .next_stack_object_id =
            state.next_stack_object_id,
    };
}

cache::Root make_root(
    const Fixture& fixture,
    const old_school::GameState& state) {
    const std::size_t owner =
        fixture.probe.root_player;
    const old_school::LearnedDecisionContext context{
        .valid = true,
        .phase = fixture.probe.phase,
        .decision_player = owner,
        .consecutive_passes =
            fixture.probe.consecutive_passes,
        .sorcery_actions =
            fixture.probe.phase ==
                old_school::TurnPhase::FirstMain ||
            fixture.probe.phase ==
                old_school::TurnPhase::SecondMain,
    };
    const std::vector<old_school::PriorityAction> raw =
        old_school::legal_priority_actions(
            state, owner,
            context.sorcery_actions);
    const information::InformationSetKey key =
        information::make_information_set_key(
            state, context, raw);
    const auto canonical_rows =
        information::descriptor_canonical_action_rows(
            key);
    std::vector<old_school::PriorityAction>
        canonical_typed;
    std::vector<std::string> descriptors;
    canonical_typed.reserve(canonical_rows.size());
    descriptors.reserve(canonical_rows.size());
    for (const auto& row : canonical_rows) {
        canonical_typed.push_back(row.action);
        descriptors.push_back(row.descriptor);
    }
    const std::vector<cache::CanonicalAction>
        canonical =
            cache::bind_canonical_actions(
                raw, canonical_typed);
    const std::string information_fingerprint =
        collection::owner_information_action_fingerprint(
            old_school::observe_game_state(
                state, owner),
            context, canonical_typed,
            cache::kOwnerInformationSchema);
    const collection::RootLocator locator{
        .source_block =
            fixture.source.schedule_block,
        .source_seed_base =
            fixture.source.source_seed_base,
        .schedule_index =
            fixture.source.schedule_index,
        .game_seed = fixture.source.game_seed,
        .owner_seat = owner,
        .trace_ordinal = 17,
    };
    const cache::Hash256 information_sha =
        bundle::parse_sha256(
            information_fingerprint);
    const cache::Hash256 stable_sha =
        bundle::parse_sha256(
            collection::block_bound_stable_root_id(
                locator, information_fingerprint,
                bundle::kStableRootSchema));

    std::size_t pass_index = canonical.size();
    for (std::size_t index = 0;
         index < canonical.size(); ++index) {
        if (raw[canonical[index].raw_index].kind ==
            old_school::PriorityActionKind::Pass) {
            pass_index = index;
        }
    }
    expect(pass_index < canonical.size(),
           "canonical fixture has no Pass");

    cache::Root result{
        .source =
            cache::SourceFamily::Dev1Selected,
        .split = bundle::Split::Fit,
        .source_row = 0,
        .source_roles =
            static_cast<std::uint8_t>(
                bundle::Role::DominancePositive),
        .production_seed =
            neutral::production_seed_for_stable_root(
                stable_sha),
        .locator = locator,
        .owner_deck =
            static_cast<std::uint8_t>(
                fixture.probe.root_deck),
        .opponent_deck =
            static_cast<std::uint8_t>(
                fixture.probe.opponent_deck),
        .stable_root_id = stable_sha,
        .physical_game_sha256 =
            bundle::sha256(
                collection::
                    block_bound_physical_game_id(
                        locator)),
        .information_action_sha256 =
            information_sha,
        .descriptor_set_sha256 =
            bundle::descriptor_set_sha256(
                descriptors),
        .state = visible_state(state, owner),
        .context = context,
        .raw_actions = raw,
        .canonical_actions = canonical,
        .pass_index =
            static_cast<std::uint8_t>(
                pass_index),
    };
    for (std::size_t player = 0; player < 2;
         ++player) {
        result.deck_compositions[player] =
            composition(
                fixture.probe
                    .original_decks[player]);
    }
    result.raw_actions_sha256 =
        cache::raw_actions_sha256(
            result.raw_actions);
    result.canonical_actions_sha256 =
        cache::canonical_actions_sha256(
            result.canonical_actions);
    return result;
}

old_school::GameState hidden_repartition(
    const Fixture& fixture) {
    old_school::GameState result =
        fixture.probe.state;
    const std::size_t owner =
        fixture.probe.root_player;
    auto& opponent = result.players[1 - owner];
    for (std::size_t hand = 0;
         hand < opponent.hand.size(); ++hand) {
        for (std::size_t library = 0;
             library < opponent.library.size();
             ++library) {
            if (opponent.hand[hand] !=
                opponent.library[library]) {
                std::swap(
                    opponent.hand[hand],
                    opponent.library[library]);
                return result;
            }
        }
    }
    for (old_school::PlayerState* player :
         std::array{
             &opponent,
             &result.players[owner],
         }) {
        for (std::size_t first = 0;
             first < player->library.size(); ++first) {
            for (std::size_t second = first + 1;
                 second < player->library.size();
                 ++second) {
                if (player->library[first] !=
                    player->library[second]) {
                    std::swap(
                        player->library[first],
                        player->library[second]);
                    return result;
                }
            }
        }
    }
    throw std::runtime_error(
        "portable fixture has no distinct hidden repartition");
}

std::vector<cache::Root> synthetic_census() {
    std::vector<std::uint8_t> owner_decks;
    owner_decks.reserve(cache::kRootCount);
    for (std::size_t deck = 0;
         deck < cache::kOwnerDeckRootCounts.size();
         ++deck) {
        owner_decks.insert(
            owner_decks.end(),
            cache::kOwnerDeckRootCounts[deck],
            static_cast<std::uint8_t>(deck));
    }
    expect(owner_decks.size() == cache::kRootCount,
           "declared owner census does not sum to 512");

    std::vector<cache::Root> roots(
        cache::kRootCount);
    for (std::size_t index = 0;
         index < roots.size(); ++index) {
        const bool dev1 =
            index < cache::kDev1RootCount;
        const std::size_t local =
            dev1
                ? index
                : index - cache::kDev1RootCount;
        cache::Root& row = roots[index];
        row.source =
            dev1
                ? cache::SourceFamily::
                      Dev1Selected
                : cache::SourceFamily::Dev5Neutral;
        row.source_row =
            static_cast<std::uint32_t>(local);
        row.split =
            (dev1
                 ? local <
                       cache::kDev1FitRootCount
                 : local <
                       cache::kNeutralFitRootCount)
                ? bundle::Split::Fit
                : bundle::Split::Check;
        row.source_roles =
            dev1
                ? static_cast<std::uint8_t>(
                      local <
                              cache::
                                  kDev1PositiveRootCount
                          ? bundle::Role::
                                DominancePositive
                          : bundle::Role::
                                BackgroundControl)
                : 0;
        row.owner_deck = owner_decks[index];
        const std::size_t width =
            dev1
                ? (local < 181 ? 6U : 5U)
                : (local < 237 ? 3U : 2U);
        row.raw_actions.assign(
            width,
            old_school::PriorityAction::pass());
        row.stable_root_id =
            bundle::sha256(
                "synthetic-work0-census-" +
                std::to_string(index));
        row.locator = {
            .source_block = index % 4,
            .source_seed_base = 1,
            .schedule_index = index % 160,
            .game_seed = 10'000 + index,
            .owner_seat = index % 2,
            .trace_ordinal = index,
        };
    }
    return roots;
}

void test_synthetic_frozen_census() {
    const std::vector<cache::Root> valid =
        synthetic_census();
    cache::testing::validate_census(valid);

    auto changed = valid;
    changed.front().owner_deck =
        static_cast<std::uint8_t>(
            old_school::DeckId::Red);
    expect_rejected(
        [&] {
            cache::testing::validate_census(changed);
        },
        "owner-deck census mutation was accepted");

    changed = valid;
    changed.front().raw_actions.pop_back();
    expect_rejected(
        [&] {
            cache::testing::validate_census(changed);
        },
        "option census mutation was accepted");

    changed = valid;
    changed.front().source_roles |=
        static_cast<std::uint8_t>(
            bundle::Role::BackgroundControl);
    expect_rejected(
        [&] {
            cache::testing::validate_census(changed);
        },
        "source-role overlap was accepted");

    changed = valid;
    ++changed.front().source_row;
    expect_rejected(
        [&] {
            cache::testing::validate_census(changed);
        },
        "source-row order mutation was accepted");

    changed = valid;
    changed[1].stable_root_id =
        changed[0].stable_root_id;
    expect_rejected(
        [&] {
            cache::testing::validate_census(changed);
        },
        "duplicate stable root was accepted");
}

void test_root_codec_and_hidden_isolation() {
    const Fixture fixture = make_fixture();
    const cache::Root first =
        make_root(fixture, fixture.probe.state);
    const old_school::GameState repartitioned =
        hidden_repartition(fixture);
    const cache::Root second =
        make_root(fixture, repartitioned);
    expect(
        first == second,
        "hidden card identity entered cache Root");

    const std::string first_bytes =
        cache::testing::encode_root(first);
    const std::string repeated_bytes =
        cache::testing::encode_root(first);
    expect(
        first_bytes == repeated_bytes &&
            cache::testing::decode_root(
                first_bytes) == first &&
            cache::testing::encode_root(second) ==
                first_bytes,
        "root codec is nondeterministic or hidden-sensitive");

    const old_school::GameState world_a =
        cache::sample_world(first, 101);
    const old_school::GameState world_b =
        cache::sample_world(first, 202);
    expect(
        old_school::observe_game_state(
            world_a, first.state.observer) ==
                old_school::observe_game_state(
                    world_b, first.state.observer) &&
            old_school::legal_priority_actions(
                world_a,
                first.context.decision_player,
                first.context.sorcery_actions) ==
                first.raw_actions &&
            old_school::legal_priority_actions(
                world_b,
                first.context.decision_player,
                first.context.sorcery_actions) ==
                first.raw_actions,
        "common-world sampling changed owner information");

    std::string trailing = first_bytes;
    trailing.push_back('\0');
    expect_rejected(
        [&] {
            static_cast<void>(
                cache::testing::decode_root(
                    trailing));
        },
        "root codec accepted trailing bytes");
}

void test_action_codec_and_bijection() {
    using old_school::CardId;
    using old_school::PriorityAction;
    using old_school::Target;
    const std::vector<PriorityAction> variants{
        PriorityAction::pass(),
        PriorityAction::play_land(CardId::Forest),
        PriorityAction::cast_creature(
            CardId::GrizzlyBears),
        PriorityAction::cast_sorcery(
            CardId::TimeWalk),
        PriorityAction::cast_artifact(
            CardId::SolRing),
        PriorityAction::cast_enchantment(
            CardId::Moat),
        PriorityAction::cast_lightning_bolt(
            Target::player_target(1)),
        PriorityAction::cast_disintegrate(
            3, Target::player_target(1)),
        PriorityAction::cast_giant_growth(
            Target::creature_target(0, 7)),
        PriorityAction::cast_counterspell(11),
        PriorityAction::cast_ancestral_recall(
            Target::player_target(0)),
        PriorityAction::cast_braingeyser(
            2, Target::player_target(0)),
        PriorityAction::cast_force_spike(11),
        PriorityAction::activate_millstone(
            9, Target::player_target(1)),
    };
    const cache::Hash256 first =
        cache::raw_actions_sha256(variants);
    const std::string wire =
        cache::testing::encode_priority_actions(
            variants);
    expect(
        first ==
                cache::raw_actions_sha256(variants) &&
            cache::testing::decode_priority_actions(
                wire) == variants,
        "Priority action codec is nondeterministic");
    auto changed = variants;
    changed[7].x_value = 4;
    expect(
        cache::raw_actions_sha256(changed) != first,
        "Priority action wire omitted an X value");
    std::string trailing = wire;
    trailing.push_back('\0');
    expect_rejected(
        [&] {
            static_cast<void>(
                cache::testing::
                    decode_priority_actions(
                        trailing));
        },
        "Priority action codec accepted trailing bytes");
    std::string invalid_enum = wire;
    expect(invalid_enum.size() > 4,
           "Priority action wire is unexpectedly short");
    invalid_enum[4] = static_cast<char>(0xff);
    expect_rejected(
        [&] {
            static_cast<void>(
                cache::testing::
                    decode_priority_actions(
                        invalid_enum));
        },
        "Priority action codec accepted invalid enum");

    const Fixture fixture = make_fixture();
    const cache::Root root =
        make_root(fixture, fixture.probe.state);
    std::vector<PriorityAction> canonical;
    canonical.reserve(root.canonical_actions.size());
    for (const cache::CanonicalAction& action :
         root.canonical_actions) {
        canonical.push_back(
            root.raw_actions[action.raw_index]);
    }
    expect(
        cache::bind_canonical_actions(
            root.raw_actions, canonical) ==
            root.canonical_actions,
        "raw/canonical bijection did not reproduce");

    auto duplicate_raw = root.raw_actions;
    duplicate_raw.front() = duplicate_raw.back();
    expect_rejected(
        [&] {
            static_cast<void>(
                cache::bind_canonical_actions(
                    duplicate_raw, canonical));
        },
        "duplicate raw actions silently aliased one match");
}

void test_semantic_mutations_fail_closed() {
    const Fixture fixture = make_fixture();
    const cache::Root valid =
        make_root(fixture, fixture.probe.state);
    const auto rejects =
        [&](cache::Root changed,
            std::string_view message) {
            expect_rejected(
                [&] {
                    static_cast<void>(
                        cache::testing::encode_root(
                            changed));
                },
                message);
        };

    auto changed = valid;
    ++changed.production_seed;
    rejects(changed,
            "production-seed mutation was accepted");

    changed = valid;
    changed.state.failed_draw[0] = true;
    rejects(changed,
            "terminal failed-draw root was accepted");

    changed = valid;
    changed.locator.trace_ordinal =
        static_cast<std::size_t>(
            std::numeric_limits<std::uint32_t>::max()) +
        1U;
    rejects(changed,
            "out-of-wire trace ordinal was accepted");

    changed = valid;
    changed.state.turn_number = 1'000'001;
    rejects(changed,
            "out-of-wire turn number was accepted");

    changed = valid;
    changed.state.extra_turns_pending[0] =
        1'000'001;
    rejects(changed,
            "out-of-wire extra turns were accepted");

    changed = valid;
    changed.state.players[0].library_size = 513;
    rejects(changed,
            "oversized public hidden count was accepted");

    changed = valid;
    changed.state.players[0].graveyard.push_back(
        static_cast<old_school::CardId>(255));
    rejects(changed,
            "invalid graveyard card ID was accepted");

    changed = valid;
    changed.state.players[0].exile.push_back(
        static_cast<old_school::CardId>(255));
    rejects(changed,
            "invalid exile card ID was accepted");

    changed = valid;
    changed.state.players[0].lands.push_back({
        .card = static_cast<old_school::CardId>(255),
        .tapped = false,
    });
    rejects(changed,
            "invalid land card ID was accepted");

    changed = valid;
    changed.state.players[0].creatures.push_back({
        .id = 999'991,
        .card = static_cast<old_school::CardId>(255),
    });
    rejects(changed,
            "invalid creature card ID was accepted");

    changed = valid;
    changed.state.players[0].artifacts.push_back({
        .id = 999'992,
        .card = static_cast<old_school::CardId>(255),
    });
    rejects(changed,
            "invalid artifact card ID was accepted");

    changed = valid;
    changed.state.players[0].enchantments.push_back(
        static_cast<old_school::CardId>(255));
    rejects(changed,
            "invalid enchantment card ID was accepted");

    changed = valid;
    changed.state.owner_hand.front() =
        static_cast<old_school::CardId>(255);
    rejects(changed,
            "invalid owner-hand card ID was accepted");

    changed = valid;
    changed.state.players[0].mana_pool.generic =
        std::numeric_limits<int>::max();
    rejects(changed,
            "hostile mana integer was accepted");

    changed = valid;
    changed.state.players[0].life =
        std::numeric_limits<int>::max();
    rejects(changed,
            "hostile life integer was accepted");

    changed = valid;
    changed.state.next_permanent_id = 1'000'001;
    rejects(changed,
            "out-of-bound permanent ID was accepted");

    changed = valid;
    changed.raw_actions.front().x_value = -1;
    changed.raw_actions_sha256 =
        cache::raw_actions_sha256(
            changed.raw_actions);
    rejects(changed,
            "negative Priority X value was accepted");

    changed = valid;
    changed.context.phase =
        static_cast<old_school::TurnPhase>(255);
    rejects(changed,
            "out-of-wire phase enum was accepted");

    changed = valid;
    changed.source =
        static_cast<cache::SourceFamily>(255);
    rejects(changed,
            "out-of-wire source-family enum was accepted");

    changed = valid;
    changed.split =
        static_cast<
            old_school::fq4_dev_bundle::Split>(255);
    rejects(changed,
            "out-of-wire split enum was accepted");

    changed = valid;
    changed.state.stack.push_back({
        .kind =
            static_cast<
                old_school::StackObjectKind>(255),
        .id = changed.state.next_stack_object_id,
        .card = old_school::CardId::LightningBolt,
        .controller = 0,
    });
    ++changed.state.next_stack_object_id;
    rejects(changed,
            "out-of-wire stack kind was accepted");

    changed = valid;
    changed.state.starting_player =
        1 - changed.state.starting_player;
    rejects(changed,
            "source play/draw mutation was accepted");

    changed = valid;
    expect(
        changed.canonical_actions.size() >= 2,
        "mutation fixture lacks two actions");
    changed.canonical_actions[1].raw_index =
        changed.canonical_actions[0].raw_index;
    changed.canonical_actions_sha256 =
        cache::canonical_actions_sha256(
            changed.canonical_actions);
    rejects(changed,
            "duplicate canonical raw index was accepted");

    std::vector<cache::Root> ordered{valid};
    const cache::Hash256 order =
        cache::root_order_sha256(ordered);
    ++ordered.front().source_row;
    expect(
        cache::root_order_sha256(ordered) != order,
        "root-order hash omitted source row ordinal");
}

} // namespace

int main() {
    const std::vector<
        std::pair<std::string_view, std::function<void()>>>
        tests{
            {
                "synthetic frozen census",
                test_synthetic_frozen_census,
            },
            {
                "root codec and hidden isolation",
                test_root_codec_and_hidden_isolation,
            },
            {
                "action codec and bijection",
                test_action_codec_and_bijection,
            },
            {
                "semantic mutations fail closed",
                test_semantic_mutations_fail_closed,
            },
        };
    std::size_t passed = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
        } catch (const std::exception& error) {
            std::cerr
                << "FAIL: " << name << ": "
                << error.what() << '\n';
        }
    }
    std::cout
        << passed << "/" << tests.size()
        << " FQ4 WORK0 cache tests passed\n";
    return passed == tests.size() ? 0 : 1;
}
