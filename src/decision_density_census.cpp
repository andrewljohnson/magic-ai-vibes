#include "old_school/decision_density_census.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/probes.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <ostream>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

namespace old_school::decision_density_census {
namespace {

constexpr std::string_view kInformationActionSchema =
    "old-school-aq16-dbc6-information-action-v1";
constexpr std::string_view kStableRootSchema =
    "old-school-aq16-dbc6-stable-root-v1";
constexpr std::string_view kManifestSchema =
    "old-school-aq16-dbc6-density-census-v1";
constexpr std::string_view kSourceRecipeSchema =
    "old-school-aq16-dbc6-c16-mirror-source-v1";

struct SourceRecipe {
    std::size_t max_turns = kSourceTurnCap;
    BotKind bot_kind = BotKind::Learned;
    LearnedVariant learned_variant =
        LearnedVariant::ValueSearchChampion;
    std::size_t worlds = kSourceWorlds;
    std::size_t rollouts_per_world =
        kSourceRolloutsPerWorld;
    std::size_t horizon_turns = kSourceHorizonTurns;
    bool blends_shallow_prior =
        kLearnedValueSearchBlendsShallowPrior;
    double exploration_rate = 0.0;
    double continuation_epsilon = 0.0;
    double priority_residual_weight = 0.0;
    bool pass_dominance = false;
    double resolved_shallow_prior_weight = 0.0;
    bool adversarial_blocks = false;
    bool actor_local_search = false;
    bool recursive_policy_improvement = false;
    LearnedContinuationController continuation_controller =
        LearnedContinuationController::Legacy;
    std::size_t training_games = 800;
    std::uint64_t training_seed = 424242;
    std::size_t learned_search_depth = 1;
    std::size_t recursive_evaluation_depth = 0;
    bool exact_combat_subgame = false;
};

inline constexpr SourceRecipe kSourceRecipe{};

using ActorGameKey =
    std::tuple<std::size_t, std::size_t, std::size_t, std::size_t>;

void require_parent(
    const std::shared_ptr<const LearnedModel>& parent) {
    if (!parent ||
        learned_model_fingerprint(parent) !=
            kRequiredParentFingerprint ||
        learned_critic_schema(parent) !=
            LearnedCriticSchema::LegacyStateOnly) {
        throw std::invalid_argument(
            "AQ16-DBC6 requires exact frozen C16");
    }
}

std::size_t deck_index(DeckId deck) {
    const std::size_t index =
        static_cast<std::size_t>(deck);
    if (index >= kDeckCount) {
        throw std::out_of_range(
            "AQ16-DBC6 deck is invalid");
    }
    return index;
}

bool sha256_is_canonical(std::string_view value) {
    return value.size() == 64 &&
           std::all_of(
               value.begin(), value.end(),
               [](char character) {
                   return
                       (character >= '0' &&
                        character <= '9') ||
                       (character >= 'a' &&
                        character <= 'f');
               });
}

bool consume_prefix(
    std::string_view& input, std::string_view prefix) {
    if (!input.starts_with(prefix)) {
        return false;
    }
    input.remove_prefix(prefix.size());
    return true;
}

bool take_before(
    std::string_view& input, std::string_view delimiter,
    std::string_view& token) {
    const std::size_t position = input.find(delimiter);
    if (position == std::string_view::npos) {
        return false;
    }
    token = input.substr(0, position);
    input.remove_prefix(position + delimiter.size());
    return true;
}

std::string_view take_component(std::string_view& input) {
    const std::size_t position = input.find('.');
    const std::string_view token =
        input.substr(0, position);
    input.remove_prefix(
        position == std::string_view::npos
            ? input.size()
            : position);
    return token;
}

bool parse_canonical_u64(
    std::string_view token, std::uint64_t& value) {
    if (token.empty()) {
        return false;
    }
    const auto result = std::from_chars(
        token.data(), token.data() + token.size(), value);
    return result.ec == std::errc{} &&
           result.ptr == token.data() + token.size() &&
           std::to_string(value) == token;
}

bool parse_canonical_int(
    std::string_view token, int& value) {
    if (token.empty()) {
        return false;
    }
    const auto result = std::from_chars(
        token.data(), token.data() + token.size(), value);
    return result.ec == std::errc{} &&
           result.ptr == token.data() + token.size() &&
           std::to_string(value) == token;
}

bool consume_u64_component(
    std::string_view& input, std::string_view prefix,
    std::uint64_t& value) {
    return consume_prefix(input, prefix) &&
           parse_canonical_u64(
               take_component(input), value);
}

bool priority_action_shape_is_canonical(
    const PriorityAction& action) {
    if (static_cast<std::size_t>(action.card) >=
            kCardCount ||
        (action.target.has_value() &&
         action.target->creature.has_value() &&
         *action.target->creature == 0) ||
        (action.spell_target.has_value() &&
         *action.spell_target == 0) ||
        (action.source_permanent.has_value() &&
         *action.source_permanent == 0)) {
        return false;
    }
    if (action.target.has_value() &&
        action.target->player >= 2) {
        return false;
    }
    switch (action.kind) {
    case PriorityActionKind::Pass:
        return action == PriorityAction::pass();
    case PriorityActionKind::PlayLand:
        return action ==
                   PriorityAction::play_land(action.card) &&
               card_definition(action.card).type ==
                   CardType::Land;
    case PriorityActionKind::CastCreature:
        return action ==
                   PriorityAction::cast_creature(
                       action.card) &&
               card_definition(action.card).type ==
                   CardType::Creature;
    case PriorityActionKind::CastSorcery:
        return action ==
                   PriorityAction::cast_sorcery(action.card) &&
               (action.card == CardId::Tsunami ||
                action.card == CardId::TimeWalk);
    case PriorityActionKind::CastArtifact:
        return action ==
                   PriorityAction::cast_artifact(
                       action.card) &&
               card_definition(action.card).type ==
                   CardType::Artifact;
    case PriorityActionKind::CastEnchantment:
        return action ==
                   PriorityAction::cast_enchantment(
                       action.card) &&
               card_definition(action.card).type ==
                   CardType::Enchantment;
    case PriorityActionKind::CastLightningBolt:
        return action.target.has_value() &&
               action ==
                   PriorityAction::cast_lightning_bolt(
                       *action.target);
    case PriorityActionKind::CastDisintegrate:
        return action.target.has_value() &&
               action.x_value >= 0 &&
               action ==
                   PriorityAction::cast_disintegrate(
                       action.x_value, *action.target);
    case PriorityActionKind::CastGiantGrowth:
        return action.target.has_value() &&
               action.target->creature.has_value() &&
               action ==
                   PriorityAction::cast_giant_growth(
                       *action.target);
    case PriorityActionKind::CastCounterspell:
        return action.spell_target.has_value() &&
               action ==
                   PriorityAction::cast_counterspell(
                       *action.spell_target);
    case PriorityActionKind::CastAncestralRecall:
        return action.target.has_value() &&
               !action.target->creature.has_value() &&
               action ==
                   PriorityAction::cast_ancestral_recall(
                       *action.target);
    case PriorityActionKind::CastBraingeyser:
        return action.target.has_value() &&
               !action.target->creature.has_value() &&
               action.x_value >= 0 &&
               action ==
                   PriorityAction::cast_braingeyser(
                       action.x_value, *action.target);
    case PriorityActionKind::CastForceSpike:
        return action.spell_target.has_value() &&
               action ==
                   PriorityAction::cast_force_spike(
                       *action.spell_target);
    case PriorityActionKind::ActivateMillstone:
        return action.target.has_value() &&
               !action.target->creature.has_value() &&
               action.source_permanent.has_value() &&
               action ==
                   PriorityAction::activate_millstone(
                       *action.source_permanent,
                       *action.target);
    }
    return false;
}

std::optional<PriorityAction>
parse_canonical_priority_action_descriptor(
    std::string_view descriptor) {
    std::string_view input = descriptor;
    std::string_view token;
    std::uint64_t kind = 0;
    std::uint64_t card = 0;
    int x_value = 0;
    if (!consume_prefix(input, "kind-") ||
        !take_before(input, ".card-", token) ||
        !parse_canonical_u64(token, kind) ||
        !take_before(input, ".x-", token) ||
        !parse_canonical_u64(token, card) ||
        !parse_canonical_int(
            take_component(input), x_value) ||
        kind >
            static_cast<std::uint64_t>(
                PriorityActionKind::ActivateMillstone) ||
        card >= kCardCount) {
        return std::nullopt;
    }
    PriorityAction action{
        .kind = static_cast<PriorityActionKind>(kind),
        .card = static_cast<CardId>(card),
        .x_value = x_value,
    };
    if (input.starts_with(".target-player-")) {
        std::uint64_t player = 0;
        if (!consume_u64_component(
                input, ".target-player-", player) ||
            player >
                std::numeric_limits<std::size_t>::max()) {
            return std::nullopt;
        }
        action.target = Target::player_target(
            static_cast<std::size_t>(player));
        if (input.starts_with(".creature-")) {
            std::uint64_t creature = 0;
            if (!consume_u64_component(
                    input, ".creature-", creature)) {
                return std::nullopt;
            }
            action.target->creature = creature;
        }
    }
    if (input.starts_with(".spell-")) {
        std::uint64_t spell = 0;
        if (!consume_u64_component(
                input, ".spell-", spell)) {
            return std::nullopt;
        }
        action.spell_target = spell;
    }
    if (input.starts_with(".source-")) {
        std::uint64_t source = 0;
        if (!consume_u64_component(
                input, ".source-", source)) {
            return std::nullopt;
        }
        action.source_permanent = source;
    }
    if (!input.empty() ||
        !priority_action_shape_is_canonical(action) ||
        probes::stable_priority_action_descriptor(action) !=
            descriptor) {
        return std::nullopt;
    }
    return action;
}

void checked_add(
    std::size_t& total, std::size_t increment,
    std::string_view label) {
    if (increment >
        std::numeric_limits<std::size_t>::max() - total) {
        throw std::overflow_error(
            "AQ16-DBC6 " + std::string(label) +
            " overflow");
    }
    total += increment;
}

void append_u64(
    std::string& output, std::uint64_t value) {
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        output.push_back(static_cast<char>(
            (value >> shift) & 0xffU));
    }
}

void append_size(
    std::string& output, std::size_t value) {
    append_u64(output, static_cast<std::uint64_t>(value));
}

void append_string(
    std::string& output, std::string_view value) {
    append_size(output, value.size());
    output.append(value);
}

void append_optional_u64(
    std::string& output,
    const std::optional<std::uint64_t>& value) {
    append_u64(output, value.has_value() ? 1U : 0U);
    if (value.has_value()) {
        append_u64(output, *value);
    }
}

void append_source_recipe(std::string& output) {
    const SourceRecipe& recipe = kSourceRecipe;
    append_string(output, kSourceRecipeSchema);
    append_size(output, recipe.max_turns);
    append_u64(
        output,
        static_cast<std::uint64_t>(recipe.bot_kind));
    append_u64(
        output,
        static_cast<std::uint64_t>(
            recipe.learned_variant));
    append_size(output, recipe.worlds);
    append_size(output, recipe.rollouts_per_world);
    append_size(output, recipe.horizon_turns);
    append_u64(output, recipe.blends_shallow_prior);
    append_u64(
        output,
        std::bit_cast<std::uint64_t>(
            recipe.exploration_rate));
    append_u64(
        output,
        std::bit_cast<std::uint64_t>(
            recipe.continuation_epsilon));
    append_u64(
        output,
        std::bit_cast<std::uint64_t>(
            recipe.priority_residual_weight));
    append_u64(output, recipe.pass_dominance);
    append_u64(
        output,
        std::bit_cast<std::uint64_t>(
            recipe.resolved_shallow_prior_weight));
    append_u64(output, recipe.adversarial_blocks);
    append_u64(output, recipe.actor_local_search);
    append_u64(
        output, recipe.recursive_policy_improvement);
    append_u64(
        output,
        static_cast<std::uint64_t>(
            recipe.continuation_controller));
    append_size(output, recipe.training_games);
    append_u64(output, recipe.training_seed);
    append_size(output, recipe.learned_search_depth);
    append_size(
        output, recipe.recursive_evaluation_depth);
    append_u64(output, recipe.exact_combat_subgame);
}

void append_action(
    std::string& output, const PriorityAction& action) {
    append_u64(
        output,
        static_cast<std::uint64_t>(action.kind));
    append_u64(
        output,
        static_cast<std::uint64_t>(action.card));
    append_u64(output, action.target.has_value() ? 1U : 0U);
    if (action.target.has_value()) {
        append_size(output, action.target->player);
        append_optional_u64(
            output, action.target->creature);
    }
    append_optional_u64(output, action.spell_target);
    append_optional_u64(output, action.source_permanent);
    append_u64(
        output,
        std::bit_cast<std::uint64_t>(
            static_cast<std::int64_t>(action.x_value)));
}

void append_coordinate(
    std::string& output,
    const RootCoordinate& coordinate) {
    append_u64(
        output,
        static_cast<std::uint64_t>(coordinate.split));
    append_size(output, coordinate.block_index);
    append_size(output, coordinate.schedule_index);
    append_size(output, coordinate.pairing_index);
    append_u64(output, coordinate.game_seed);
    append_size(output, coordinate.starting_player);
    for (const DeckId deck : coordinate.seat_decks) {
        append_u64(
            output, static_cast<std::uint64_t>(deck));
    }
    append_size(output, coordinate.actor);
    append_size(output, coordinate.trace_ordinal);
    append_size(output, coordinate.nontrivial_ordinal);
    append_size(
        output,
        coordinate.actor_game_nontrivial_roots);
}

void append_width(
    std::string& output, const WidthCensus& width) {
    append_size(output, width.legal_action_count);
    append_size(output, width.roots);
    append_size(output, width.options);
    append_size(output, width.potential_pairs);
}

void append_actor_game(
    std::string& output,
    const ActorGameCensus& actor_game) {
    append_u64(
        output,
        static_cast<std::uint64_t>(actor_game.split));
    append_size(output, actor_game.block_index);
    append_size(output, actor_game.schedule_index);
    append_size(output, actor_game.pairing_index);
    append_u64(output, actor_game.game_seed);
    append_size(output, actor_game.starting_player);
    for (const DeckId deck : actor_game.seat_decks) {
        append_u64(
            output, static_cast<std::uint64_t>(deck));
    }
    append_size(output, actor_game.actor);
    append_u64(
        output,
        static_cast<std::uint64_t>(actor_game.owner_deck));
    append_size(output, actor_game.roots);
    append_size(output, actor_game.options);
    append_size(output, actor_game.potential_pairs);
    append_size(output, actor_game.widths.size());
    for (const WidthCensus& width : actor_game.widths) {
        append_width(output, width);
    }
}

void append_deck(
    std::string& output, const DeckCensus& deck) {
    append_size(output, deck.actor_games);
    append_size(output, deck.roots);
    append_size(output, deck.options);
    append_size(output, deck.potential_pairs);
    append_size(output, deck.widths.size());
    for (const WidthCensus& width : deck.widths) {
        append_width(output, width);
    }
}

void append_split(
    std::string& output, const SplitCensus& split) {
    append_u64(
        output,
        static_cast<std::uint64_t>(split.split));
    append_size(output, split.games);
    append_size(output, split.actor_games);
    append_size(output, split.roots);
    append_size(output, split.options);
    append_size(output, split.potential_pairs);
    for (const DeckCensus& deck : split.decks) {
        append_deck(output, deck);
    }
    append_size(output, split.actor_game_rows.size());
    for (const ActorGameCensus& actor_game :
         split.actor_game_rows) {
        append_actor_game(output, actor_game);
    }
    append_size(output, split.widths.size());
    for (const WidthCensus& width : split.widths) {
        append_width(output, width);
    }
}

void append_manifest_root(
    std::string& output, const ManifestRoot& root) {
    append_coordinate(output, root.coordinate);
    append_size(output, root.legal_action_count);
    append_size(output, root.potential_pairs);
    append_size(output, root.action_descriptors.size());
    for (const std::string& descriptor :
         root.action_descriptors) {
        append_string(output, descriptor);
    }
    append_string(
        output, root.information_action_fingerprint);
    append_string(output, root.stable_root_id);
}

std::vector<CardId> cards_for_deck(DeckId deck) {
    switch (deck) {
    case DeckId::Green:
        return green_deck();
    case DeckId::Red:
        return red_deck();
    case DeckId::Blue:
        return blue_deck();
    case DeckId::White:
        return white_control_deck();
    case DeckId::RUAggro:
        return ru_aggro_deck();
    }
    throw std::invalid_argument(
        "AQ16-DBC6 source deck is invalid");
}

Split split_for_block(std::size_t block) {
    if (block == kTrainBlocks[0] ||
        block == kTrainBlocks[1]) {
        return Split::Train;
    }
    if (block == kDevBlock) {
        return Split::Dev;
    }
    throw std::out_of_range(
        "AQ16-DBC6 schedule block is invalid");
}

std::array<std::size_t, 3> all_blocks() {
    return {
        kTrainBlocks[0],
        kTrainBlocks[1],
        kDevBlock,
    };
}

void validate_schedule_block_impl(
    std::size_t block,
    std::span<const learned_iteration::ScheduledGame>
        games) {
    constexpr std::array<
        std::pair<DeckId, DeckId>,
        learned_iteration::kBalancedPairings>
        expected_pairings{{
            {DeckId::Green, DeckId::Red},
            {DeckId::Green, DeckId::Blue},
            {DeckId::Green, DeckId::White},
            {DeckId::Green, DeckId::RUAggro},
            {DeckId::Red, DeckId::Blue},
            {DeckId::Red, DeckId::White},
            {DeckId::Red, DeckId::RUAggro},
            {DeckId::Blue, DeckId::White},
            {DeckId::Blue, DeckId::RUAggro},
            {DeckId::White, DeckId::RUAggro},
        }};
    if ((block != kTrainBlocks[0] &&
         block != kTrainBlocks[1] &&
         block != kDevBlock) ||
        games.size() != kGamesPerBlock) {
        throw std::invalid_argument(
            "AQ16-DBC6 schedule block shape drifted");
    }

    std::array<
        std::array<std::array<std::size_t, 2>, 2>,
        learned_iteration::kBalancedPairings>
        exact_games{};
    std::array<
        std::size_t,
        learned_iteration::kBalancedPairings>
        pairing_games{};
    std::array<std::size_t, kDeckCount> appearances{};
    std::array<std::size_t, kDeckCount> seat_zero{};
    std::array<std::size_t, kDeckCount> seat_one{};
    std::array<std::size_t, kDeckCount> starts{};
    std::array<std::size_t, kDeckCount> draws{};
    std::set<std::uint64_t> seeds;

    for (std::size_t index = 0;
         index < games.size(); ++index) {
        const auto& game = games[index];
        if (game.schedule_index != index ||
            game.pairing_index >=
                expected_pairings.size() ||
            game.starting_player >= 2 ||
            game.seed !=
                learned_iteration::derive_seed(
                    kCollectionRootSeed,
                    learned_iteration::SeedDomain::
                        SelfPlayGame,
                    kScheduleGeneration, block, index) ||
            !seeds.insert(game.seed).second) {
            throw std::invalid_argument(
                "AQ16-DBC6 schedule coordinate drifted");
        }
        const auto [first, second] =
            expected_pairings[game.pairing_index];
        std::size_t orientation = 0;
        if (game.seat_decks ==
            std::array<DeckId, 2>{first, second}) {
            orientation = 0;
        } else if (
            game.seat_decks ==
            std::array<DeckId, 2>{second, first}) {
            orientation = 1;
        } else {
            throw std::invalid_argument(
                "AQ16-DBC6 pairing membership drifted");
        }
        if (++exact_games[game.pairing_index]
                         [orientation]
                         [game.starting_player] != 1) {
            throw std::invalid_argument(
                "AQ16-DBC6 duplicate seat/play-draw cell");
        }
        ++pairing_games[game.pairing_index];
        for (std::size_t seat = 0; seat < 2; ++seat) {
            const std::size_t deck =
                deck_index(game.seat_decks[seat]);
            ++appearances[deck];
            if (seat == 0) {
                ++seat_zero[deck];
            } else {
                ++seat_one[deck];
            }
            if (seat == game.starting_player) {
                ++starts[deck];
            } else {
                ++draws[deck];
            }
        }
    }
    for (std::size_t pairing = 0;
         pairing < expected_pairings.size(); ++pairing) {
        if (pairing_games[pairing] !=
            learned_iteration::kBalancedGamesPerPairing) {
            throw std::invalid_argument(
                "AQ16-DBC6 pairing count drifted");
        }
        for (std::size_t orientation = 0;
             orientation < 2; ++orientation) {
            for (std::size_t starting_player = 0;
                 starting_player < 2;
                 ++starting_player) {
                if (exact_games[pairing][orientation]
                               [starting_player] != 1) {
                    throw std::invalid_argument(
                        "AQ16-DBC6 seat/play-draw cell missing");
                }
            }
        }
    }
    for (std::size_t deck = 0; deck < kDeckCount;
         ++deck) {
        if (appearances[deck] != 16 ||
            seat_zero[deck] != 8 ||
            seat_one[deck] != 8 ||
            starts[deck] != 8 ||
            draws[deck] != 8) {
            throw std::invalid_argument(
                "AQ16-DBC6 deck seat/play-draw imbalance");
        }
    }
}

ActorGameKey actor_game_key(
    Split split, std::size_t block,
    std::size_t schedule_index, std::size_t actor) {
    return {
        split_index(split), block,
        schedule_index, actor,
    };
}

ActorGameKey actor_game_key(
    const RootCoordinate& coordinate) {
    return actor_game_key(
        coordinate.split, coordinate.block_index,
        coordinate.schedule_index, coordinate.actor);
}

void add_width(
    std::map<std::size_t, WidthCensus>& widths,
    std::size_t legal_actions) {
    WidthCensus& width = widths[legal_actions];
    width.legal_action_count = legal_actions;
    checked_add(width.roots, 1, "width root count");
    checked_add(
        width.options, legal_actions,
        "width option count");
    checked_add(
        width.potential_pairs,
        potential_pair_count(legal_actions),
        "width potential-pair count");
}

std::vector<WidthCensus> width_vector(
    const std::map<std::size_t, WidthCensus>& widths) {
    std::vector<WidthCensus> result;
    result.reserve(widths.size());
    for (const auto& [unused, width] : widths) {
        static_cast<void>(unused);
        result.push_back(width);
    }
    return result;
}

struct AggregateBuilder {
    std::array<SplitCensus, 2> splits{
        SplitCensus{.split = Split::Train},
        SplitCensus{.split = Split::Dev},
    };
    std::array<
        std::array<std::map<std::size_t, WidthCensus>,
                   kDeckCount>,
        2>
        deck_widths;
    std::array<std::map<std::size_t, WidthCensus>, 2>
        split_widths;
    std::vector<std::map<std::size_t, WidthCensus>>
        actor_game_widths;
    std::map<ActorGameKey, std::pair<std::size_t, std::size_t>>
        actor_game_locations;
};

AggregateBuilder empty_aggregate() {
    AggregateBuilder result;
    std::set<std::uint64_t> physical_game_seeds;
    for (const std::size_t block : all_blocks()) {
        const Split split = split_for_block(block);
        SplitCensus& split_row =
            result.splits[split_index(split)];
        const auto schedule =
            learned_iteration::balanced_schedule(
                kCollectionRootSeed,
                kScheduleGeneration, block);
        validate_schedule_block_impl(block, schedule);
        checked_add(
            split_row.games, schedule.size(),
            "split game count");
        for (const auto& scheduled : schedule) {
            if (!physical_game_seeds.insert(
                    scheduled.seed).second) {
                throw std::logic_error(
                    "AQ16-DBC6 schedule reused a physical game");
            }
            for (std::size_t actor = 0;
                 actor < 2; ++actor) {
                const std::size_t row_index =
                    split_row.actor_game_rows.size();
                const ActorGameKey key =
                    actor_game_key(
                        split, block,
                        scheduled.schedule_index, actor);
                if (!result.actor_game_locations
                         .emplace(
                             key,
                             std::pair{
                                 split_index(split),
                                 row_index})
                         .second) {
                    throw std::logic_error(
                        "AQ16-DBC6 schedule duplicated an actor-game");
                }
                split_row.actor_game_rows.push_back({
                    .split = split,
                    .block_index = block,
                    .schedule_index =
                        scheduled.schedule_index,
                    .pairing_index =
                        scheduled.pairing_index,
                    .game_seed = scheduled.seed,
                    .starting_player =
                        scheduled.starting_player,
                    .seat_decks = scheduled.seat_decks,
                    .actor = actor,
                    .owner_deck =
                        scheduled.seat_decks[actor],
                });
                result.actor_game_widths.emplace_back();
                ++split_row.actor_games;
                ++split_row.decks[
                    deck_index(
                        scheduled.seat_decks[actor])]
                      .actor_games;
            }
        }
    }
    return result;
}

void validate_coordinate_against_actor_game(
    const RootCoordinate& coordinate,
    const ActorGameCensus& actor_game) {
    if (coordinate.split != actor_game.split ||
        coordinate.block_index !=
            actor_game.block_index ||
        coordinate.schedule_index !=
            actor_game.schedule_index ||
        coordinate.pairing_index !=
            actor_game.pairing_index ||
        coordinate.game_seed != actor_game.game_seed ||
        coordinate.starting_player !=
            actor_game.starting_player ||
        coordinate.seat_decks !=
            actor_game.seat_decks ||
        coordinate.actor != actor_game.actor ||
        coordinate.owner_deck() !=
            actor_game.owner_deck) {
        throw std::invalid_argument(
            "AQ16-DBC6 root schedule/actor coordinate drifted");
    }
}

std::array<SplitCensus, 2> summarize_roots(
    std::span<const ManifestRoot> roots) {
    AggregateBuilder aggregate = empty_aggregate();
    std::set<std::string> stable_root_ids;
    std::optional<ActorGameKey> previous_key;
    std::size_t expected_nontrivial_ordinal = 0;
    std::size_t previous_trace_ordinal = 0;

    for (const ManifestRoot& root : roots) {
        validate_manifest_root(root);
        if (!stable_root_ids.insert(
                root.stable_root_id).second) {
            throw std::invalid_argument(
                "AQ16-DBC6 duplicate stable root ID");
        }
        const ActorGameKey key =
            actor_game_key(root.coordinate);
        const auto location =
            aggregate.actor_game_locations.find(key);
        if (location ==
            aggregate.actor_game_locations.end()) {
            throw std::invalid_argument(
                "AQ16-DBC6 root is outside the frozen schedule");
        }
        const auto [split_position, actor_game_position] =
            location->second;
        ActorGameCensus& actor_game =
            aggregate.splits[split_position]
                .actor_game_rows[actor_game_position];
        validate_coordinate_against_actor_game(
            root.coordinate, actor_game);

        if (!previous_key.has_value() ||
            *previous_key != key) {
            if (previous_key.has_value() &&
                *previous_key > key) {
                throw std::invalid_argument(
                    "AQ16-DBC6 root row order is noncanonical");
            }
            previous_key = key;
            expected_nontrivial_ordinal = 0;
            previous_trace_ordinal = 0;
        }
        if (root.coordinate.nontrivial_ordinal !=
                expected_nontrivial_ordinal ||
            (expected_nontrivial_ordinal != 0 &&
             root.coordinate.trace_ordinal <=
                 previous_trace_ordinal)) {
            throw std::invalid_argument(
                "AQ16-DBC6 root trace order drifted");
        }
        ++expected_nontrivial_ordinal;
        previous_trace_ordinal =
            root.coordinate.trace_ordinal;

        SplitCensus& split =
            aggregate.splits[split_position];
        DeckCensus& deck =
            split.decks[
                deck_index(actor_game.owner_deck)];
        for (auto* row :
             {&actor_game.roots, &deck.roots,
              &split.roots}) {
            checked_add(*row, 1, "root count");
        }
        for (auto* row :
             {&actor_game.options, &deck.options,
              &split.options}) {
            checked_add(
                *row, root.legal_action_count,
                "option count");
        }
        for (auto* row :
             {&actor_game.potential_pairs,
              &deck.potential_pairs,
              &split.potential_pairs}) {
            checked_add(
                *row, root.potential_pairs,
                "potential-pair count");
        }
        add_width(
            aggregate.actor_game_widths[
                actor_game_position +
                (split_position == 0
                     ? 0
                     : kTrainActorGames)],
            root.legal_action_count);
        add_width(
            aggregate.deck_widths[split_position]
                [deck_index(actor_game.owner_deck)],
            root.legal_action_count);
        add_width(
            aggregate.split_widths[split_position],
            root.legal_action_count);
    }

    std::size_t global_actor_game_position = 0;
    for (std::size_t split_position = 0;
         split_position < aggregate.splits.size();
         ++split_position) {
        SplitCensus& split =
            aggregate.splits[split_position];
        for (ActorGameCensus& actor_game :
             split.actor_game_rows) {
            actor_game.widths =
                width_vector(
                    aggregate.actor_game_widths[
                        global_actor_game_position++]);
        }
        for (std::size_t deck = 0;
             deck < kDeckCount; ++deck) {
            split.decks[deck].widths =
                width_vector(
                    aggregate.deck_widths[
                        split_position][deck]);
        }
        split.widths =
            width_vector(
                aggregate.split_widths[split_position]);
    }

    std::map<ActorGameKey, std::size_t> root_counts;
    for (const ManifestRoot& root : roots) {
        ++root_counts[actor_game_key(root.coordinate)];
    }
    for (const ManifestRoot& root : roots) {
        if (root.coordinate.actor_game_nontrivial_roots !=
            root_counts[actor_game_key(root.coordinate)]) {
            throw std::invalid_argument(
                "AQ16-DBC6 actor-game root count drifted");
        }
    }
    return aggregate.splits;
}

ManifestRoot make_manifest_root_impl(
    const RootCoordinate& coordinate,
    std::vector<PriorityAction> actions,
    std::vector<std::vector<double>> option_rows) {
    std::vector<std::string> descriptors;
    descriptors.reserve(actions.size());
    for (const PriorityAction& action : actions) {
        descriptors.push_back(
            probes::stable_priority_action_descriptor(
                action));
    }
    ManifestRoot root{
        .coordinate = coordinate,
        .legal_action_count = actions.size(),
        .potential_pairs =
            potential_pair_count(actions.size()),
        .action_descriptors = std::move(descriptors),
    };
    root.information_action_fingerprint =
        canonical_information_action_fingerprint(
            actions, root.action_descriptors,
            option_rows);
    root.stable_root_id =
        stable_root_id(
            root.coordinate,
            root.action_descriptors,
            root.information_action_fingerprint,
            root.legal_action_count,
            root.potential_pairs);
    validate_manifest_root(root);
    return root;
}

struct LiveRootMaterial {
    ManifestRoot manifest;
    std::vector<double> observation;
    std::vector<PriorityAction> actions;
    std::vector<std::vector<double>> option_rows;
};

LiveRootMaterial make_live_material(
    const LearnedDecisionTracePoint& point,
    const RootCoordinate& coordinate) {
    if (!point.context.valid ||
        point.context.decision_player !=
            coordinate.actor) {
        throw std::invalid_argument(
            "AQ16-DBC6 live root context drifted");
    }
    std::vector<PriorityAction> actions =
        legal_priority_actions(
            point.state, coordinate.actor,
            point.context.sorcery_actions);
    if (actions.size() < 2) {
        throw std::invalid_argument(
            "AQ16-DBC6 retained a trivial Priority root");
    }
    std::vector<std::vector<double>> option_rows;
    option_rows.reserve(actions.size());
    for (const PriorityAction& action : actions) {
        option_rows.push_back(
            learned_priority_policy_features(
                point.state, coordinate.actor, action,
                point.context.sorcery_actions,
                point.context.phase,
                point.context.consecutive_passes));
    }
    ManifestRoot manifest =
        make_manifest_root_impl(
            coordinate, actions,
            option_rows);
    return {
        .manifest = std::move(manifest),
        .observation =
            learned_observation(
                point.state, coordinate.actor),
        .actions = std::move(actions),
        .option_rows = std::move(option_rows),
    };
}

std::optional<GameState> hidden_repartition_impl(
    const GameState& state, std::size_t observer) {
    if (observer >= state.players.size()) {
        throw std::out_of_range(
            "AQ16-DBC6 hidden observer is invalid");
    }
    GameState clone = state;
    PlayerState& hidden =
        clone.players[1U - observer];
    const std::size_t hand_size = hidden.hand.size();
    std::vector<CardId> hidden_cards = hidden.hand;
    hidden_cards.insert(
        hidden_cards.end(),
        hidden.library.begin(), hidden.library.end());
    if (hidden_cards.size() < 2) {
        return std::nullopt;
    }
    std::optional<std::pair<std::size_t, std::size_t>>
        swap_positions;
    for (std::size_t hand = 0;
         hand < hand_size &&
         !swap_positions.has_value();
         ++hand) {
        for (std::size_t library = hand_size;
             library < hidden_cards.size(); ++library) {
            if (hidden_cards[hand] !=
                hidden_cards[library]) {
                swap_positions =
                    std::pair{hand, library};
                break;
            }
        }
    }
    for (std::size_t first = 0;
         first < hidden_cards.size() &&
         !swap_positions.has_value();
         ++first) {
        for (std::size_t second = first + 1;
             second < hidden_cards.size(); ++second) {
            if (hidden_cards[first] !=
                hidden_cards[second]) {
                swap_positions =
                    std::pair{first, second};
                break;
            }
        }
    }
    if (!swap_positions.has_value()) {
        return std::nullopt;
    }
    std::swap(
        hidden_cards[swap_positions->first],
        hidden_cards[swap_positions->second]);
    const auto hand_end =
        hidden_cards.begin() +
        static_cast<std::ptrdiff_t>(hand_size);
    hidden.hand.assign(
        hidden_cards.begin(), hand_end);
    hidden.library.assign(
        hand_end, hidden_cards.end());
    if (clone == state) {
        return std::nullopt;
    }
    if (observe_game_state(state, observer) !=
        observe_game_state(clone, observer)) {
        throw std::logic_error(
            "AQ16-DBC6 hidden repartition changed owner information");
    }
    return clone;
}

bool bit_identical(
    std::span<const double> left,
    std::span<const double> right) {
    return left.size() == right.size() &&
           std::equal(
               left.begin(), left.end(), right.begin(),
               [](double first, double second) {
                   return
                       std::bit_cast<std::uint64_t>(first) ==
                       std::bit_cast<std::uint64_t>(second);
               });
}

bool bit_identical(
    std::span<const std::vector<double>> left,
    std::span<const std::vector<double>> right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < left.size(); ++index) {
        if (!bit_identical(left[index], right[index])) {
            return false;
        }
    }
    return true;
}

bool hidden_witness_matches(
    const LearnedDecisionTracePoint& original,
    const RootCoordinate& coordinate,
    const LiveRootMaterial& original_material,
    const GameState& hidden_clone) {
    LearnedDecisionTracePoint clone = original;
    clone.state = hidden_clone;
    const LiveRootMaterial clone_material =
        make_live_material(clone, coordinate);
    return
        observe_game_state(
            original.state, coordinate.actor) ==
            observe_game_state(
                clone.state, coordinate.actor) &&
        bit_identical(
            original_material.observation,
            clone_material.observation) &&
        original_material.actions ==
            clone_material.actions &&
        original_material.manifest.action_descriptors ==
            clone_material.manifest.action_descriptors &&
        bit_identical(
            original_material.option_rows,
            clone_material.option_rows) &&
        original_material.manifest.legal_action_count ==
            clone_material.manifest.legal_action_count &&
        original_material.manifest.potential_pairs ==
            clone_material.manifest.potential_pairs &&
        original_material.manifest
                .information_action_fingerprint ==
            clone_material.manifest
                .information_action_fingerprint &&
        original_material.manifest.stable_root_id ==
            clone_material.manifest.stable_root_id;
}

} // namespace

DeckId RootCoordinate::owner_deck() const {
    if (actor >= seat_decks.size()) {
        throw std::out_of_range(
            "AQ16-DBC6 root actor is invalid");
    }
    return seat_decks[actor];
}

std::optional<Command> parse_command(
    std::span<const std::string_view> arguments) {
    if (arguments.size() == 1 &&
        arguments.front() == "--census") {
        return Command::Census;
    }
    return std::nullopt;
}

void print_usage(std::ostream& output) {
    output
        << "Usage: old-school-decision-density-census --census\n";
}

std::size_t split_index(Split split) {
    const std::size_t index =
        static_cast<std::size_t>(split);
    if (index >= 2) {
        throw std::out_of_range(
            "AQ16-DBC6 split is invalid");
    }
    return index;
}

std::size_t potential_pair_count(
    std::size_t legal_actions) {
    if (legal_actions < 2) {
        return 0;
    }
    const std::size_t even =
        legal_actions % 2 == 0
            ? legal_actions / 2
            : legal_actions;
    const std::size_t other =
        legal_actions % 2 == 0
            ? legal_actions - 1
            : (legal_actions - 1) / 2;
    if (other != 0 &&
        even >
            std::numeric_limits<std::size_t>::max() /
                other) {
        throw std::overflow_error(
            "AQ16-DBC6 potential-pair count overflow");
    }
    return even * other;
}

GameConfig source_game_config(
    std::shared_ptr<const LearnedModel> parent,
    std::size_t starting_player) {
    require_parent(parent);
    if (starting_player >= 2) {
        throw std::out_of_range(
            "AQ16-DBC6 starting player is invalid");
    }
    const SourceRecipe& recipe = kSourceRecipe;
    const BotConfig bot{
        .kind = recipe.bot_kind,
        .learned_variant =
            recipe.learned_variant,
        .rollouts_per_action = recipe.worlds,
        .exploration_rate = recipe.exploration_rate,
        .value_continuation_epsilon =
            recipe.continuation_epsilon,
        .value_priority_residual_weight =
            recipe.priority_residual_weight,
        .value_pass_dominance =
            recipe.pass_dominance,
        .value_resolved_shallow_prior_weight =
            recipe.resolved_shallow_prior_weight,
        .value_adversarial_blocks =
            recipe.adversarial_blocks,
        .value_actor_local_search =
            recipe.actor_local_search,
        .value_recursive_policy_improvement =
            recipe.recursive_policy_improvement,
        .value_continuation_controller =
            recipe.continuation_controller,
        .training_games = recipe.training_games,
        .learned_model = parent,
    };
    return {
        .max_turns = recipe.max_turns,
        .starting_player = starting_player,
        .bots = {bot, bot},
        .learned_training_seed = recipe.training_seed,
        .learned_model = std::move(parent),
        .learned_search_depth =
            recipe.learned_search_depth,
        .recursive_policy_improvement_evaluation_depth =
            recipe.recursive_evaluation_depth,
        .learned_evaluation_exact_combat_subgame =
            recipe.exact_combat_subgame,
    };
}

std::string canonical_information_action_fingerprint(
    std::span<const PriorityAction> actions,
    std::span<const std::string> descriptors,
    std::span<const std::vector<double>> option_rows) {
    if (actions.size() < 2 ||
        descriptors.size() != actions.size() ||
        option_rows.size() != actions.size() ||
        actions.front().kind !=
            PriorityActionKind::Pass) {
        throw std::invalid_argument(
            "AQ16-DBC6 information/action shape is invalid");
    }
    std::set<std::string> descriptor_set;
    std::string payload;
    append_string(payload, kInformationActionSchema);
    append_size(payload, kPolicyFeatureCount);
    append_size(payload, actions.size());
    for (std::size_t index = 0;
         index < actions.size(); ++index) {
        if (std::find(
                actions.begin(),
                actions.begin() +
                    static_cast<std::ptrdiff_t>(index),
                actions[index]) !=
                actions.begin() +
                    static_cast<std::ptrdiff_t>(index) ||
            descriptors[index] !=
                probes::stable_priority_action_descriptor(
                    actions[index]) ||
            !descriptor_set.insert(
                 descriptors[index]).second ||
            option_rows[index].size() !=
                kPolicyFeatureCount ||
            !std::all_of(
                option_rows[index].begin(),
                option_rows[index].end(),
                [](double value) {
                    return std::isfinite(value);
                })) {
            throw std::invalid_argument(
                "AQ16-DBC6 action/feature row is invalid");
        }
        append_action(payload, actions[index]);
        append_string(payload, descriptors[index]);
        append_size(payload, option_rows[index].size());
        for (const double value : option_rows[index]) {
            append_u64(
                payload,
                std::bit_cast<std::uint64_t>(value));
        }
    }
    return artifact_integrity::sha256_string(payload);
}

std::string stable_root_id(
    const RootCoordinate& coordinate,
    std::span<const std::string> action_descriptors,
    std::string_view information_action_fingerprint,
    std::size_t legal_action_count,
    std::size_t potential_pairs) {
    if (!sha256_is_canonical(
            information_action_fingerprint) ||
        legal_action_count < 2 ||
        action_descriptors.size() !=
            legal_action_count ||
        potential_pairs !=
            potential_pair_count(legal_action_count)) {
        throw std::invalid_argument(
            "AQ16-DBC6 stable-root preimage is invalid");
    }
    std::string payload;
    append_string(payload, kStableRootSchema);
    append_coordinate(payload, coordinate);
    append_size(payload, legal_action_count);
    append_size(payload, potential_pairs);
    append_size(payload, action_descriptors.size());
    for (const std::string& descriptor :
         action_descriptors) {
        append_string(payload, descriptor);
    }
    append_string(
        payload, information_action_fingerprint);
    return artifact_integrity::sha256_string(payload);
}

std::string canonical_manifest_hash(
    const Census& census) {
    std::string payload;
    append_string(payload, kManifestSchema);
    append_string(payload, kIdentifier);
    append_u64(payload, census.root_seed);
    append_size(payload, kScheduleGeneration);
    append_size(payload, kTrainBlocks.size());
    for (const std::size_t block : kTrainBlocks) {
        append_size(payload, block);
    }
    append_size(payload, kDevBlock);
    append_source_recipe(payload);
    append_string(payload, census.parent_fingerprint);
    for (const SplitCensus& split : census.splits) {
        append_split(payload, split);
    }
    append_size(payload, census.roots.size());
    for (const ManifestRoot& root : census.roots) {
        append_manifest_root(payload, root);
    }
    return artifact_integrity::sha256_string(payload);
}

void validate_manifest_root(const ManifestRoot& root) {
    const auto& coordinate = root.coordinate;
    static_cast<void>(split_index(coordinate.split));
    if (coordinate.actor >= 2 ||
        coordinate.starting_player >= 2 ||
        coordinate.block_index !=
            (coordinate.split == Split::Dev
                 ? kDevBlock
                 : coordinate.block_index) ||
        (coordinate.split == Split::Train &&
         coordinate.block_index != kTrainBlocks[0] &&
         coordinate.block_index != kTrainBlocks[1]) ||
        coordinate.schedule_index >= kGamesPerBlock ||
        coordinate.pairing_index >=
            learned_iteration::kBalancedPairings ||
        deck_index(coordinate.seat_decks[0]) >= kDeckCount ||
        deck_index(coordinate.seat_decks[1]) >= kDeckCount ||
        coordinate.seat_decks[0] ==
            coordinate.seat_decks[1] ||
        coordinate.actor_game_nontrivial_roots == 0 ||
        coordinate.nontrivial_ordinal >=
            coordinate.actor_game_nontrivial_roots ||
        root.legal_action_count < 2 ||
        root.action_descriptors.size() !=
            root.legal_action_count ||
        root.potential_pairs !=
            potential_pair_count(
                root.legal_action_count) ||
        root.action_descriptors.front() !=
            probes::stable_priority_action_descriptor(
                PriorityAction::pass()) ||
        !sha256_is_canonical(
            root.information_action_fingerprint) ||
        !sha256_is_canonical(root.stable_root_id)) {
        throw std::invalid_argument(
            "AQ16-DBC6 manifest root shape is invalid");
    }
    std::set<std::string> descriptors;
    for (const std::string& descriptor :
         root.action_descriptors) {
        if (descriptor.empty() ||
            !parse_canonical_priority_action_descriptor(
                 descriptor)
                 .has_value() ||
            !descriptors.insert(descriptor).second) {
            throw std::invalid_argument(
                "AQ16-DBC6 actions are duplicate or noncanonical");
        }
    }
    if (root.stable_root_id !=
        stable_root_id(
            coordinate,
            root.action_descriptors,
            root.information_action_fingerprint,
            root.legal_action_count,
            root.potential_pairs)) {
        throw std::invalid_argument(
            "AQ16-DBC6 stable root ID drifted");
    }
}

void validate_census(const Census& census) {
    if (census.root_seed != kCollectionRootSeed ||
        census.parent_fingerprint !=
            kRequiredParentFingerprint ||
        !sha256_is_canonical(census.manifest_hash)) {
        throw std::invalid_argument(
            "AQ16-DBC6 census identity is invalid");
    }
    const auto expected_splits =
        summarize_roots(census.roots);
    if (census.splits != expected_splits ||
        census.splits[0].split != Split::Train ||
        census.splits[1].split != Split::Dev ||
        census.splits[0].games != kTrainGames ||
        census.splits[1].games != kDevGames ||
        census.splits[0].actor_games !=
            kTrainActorGames ||
        census.splits[1].actor_games !=
            kDevActorGames ||
        census.splits[0].actor_game_rows.size() !=
            kTrainActorGames ||
        census.splits[1].actor_game_rows.size() !=
            kDevActorGames) {
        throw std::invalid_argument(
            "AQ16-DBC6 census cross-sum or schedule drifted");
    }
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        if (census.splits[0].decks[deck].actor_games !=
                kTrainActorGamesPerDeck ||
            census.splits[1].decks[deck].actor_games !=
                kDevActorGamesPerDeck) {
            throw std::invalid_argument(
                "AQ16-DBC6 deck/seat/play-draw balance drifted");
        }
    }
    if (census.manifest_hash !=
        canonical_manifest_hash(census)) {
        throw std::invalid_argument(
            "AQ16-DBC6 manifest hash drifted");
    }
}

Collection collect_census_impl(
    std::shared_ptr<const LearnedModel> parent,
    const AuthenticatedRootVisitor* visitor) {
    require_parent(parent);
    std::vector<ManifestRoot> roots;
    bool hidden_witness = false;
    std::string hidden_witness_root_id;

    for (const std::size_t block : all_blocks()) {
        const Split split = split_for_block(block);
        const auto schedule =
            learned_iteration::balanced_schedule(
                kCollectionRootSeed,
                kScheduleGeneration, block);
        for (const auto& scheduled : schedule) {
            const std::array<std::vector<CardId>, 2> decks{
                cards_for_deck(scheduled.seat_decks[0]),
                cards_for_deck(scheduled.seat_decks[1]),
            };
            Game game(
                decks[0], decks[1], scheduled.seed,
                source_game_config(
                    parent,
                    scheduled.starting_player));
            std::vector<LearnedDecisionTracePoint> trace;
            static_cast<void>(
                game.run_with_priority_root_trace(trace));
            if (std::any_of(
                    trace.begin(), trace.end(),
                    [](const LearnedDecisionTracePoint& point) {
                        return
                            !point.context.valid ||
                            point.context.decision_player >= 2;
                    })) {
                throw std::runtime_error(
                    "AQ16-DBC6 source trace context drifted");
            }

            for (std::size_t actor = 0;
                 actor < 2; ++actor) {
                std::vector<std::size_t> nontrivial;
                for (std::size_t trace_ordinal = 0;
                     trace_ordinal < trace.size();
                     ++trace_ordinal) {
                    const auto& point = trace[trace_ordinal];
                    if (point.context.valid &&
                        point.context.decision_player == actor &&
                        legal_priority_actions(
                            point.state, actor,
                            point.context.sorcery_actions)
                                .size() >= 2) {
                        nontrivial.push_back(trace_ordinal);
                    }
                }
                for (std::size_t ordinal = 0;
                     ordinal < nontrivial.size();
                     ++ordinal) {
                    const RootCoordinate coordinate{
                        .split = split,
                        .block_index = block,
                        .schedule_index =
                            scheduled.schedule_index,
                        .pairing_index =
                            scheduled.pairing_index,
                        .game_seed = scheduled.seed,
                        .starting_player =
                            scheduled.starting_player,
                        .seat_decks =
                            scheduled.seat_decks,
                        .actor = actor,
                        .trace_ordinal =
                            nontrivial[ordinal],
                        .nontrivial_ordinal = ordinal,
                        .actor_game_nontrivial_roots =
                            nontrivial.size(),
                    };
                    const auto& point =
                        trace[nontrivial[ordinal]];
                    LiveRootMaterial material =
                        make_live_material(
                            point, coordinate);
                    bool root_hidden_witness = false;
                    if (!hidden_witness || visitor != nullptr) {
                        const auto hidden =
                            hidden_repartition_impl(
                                point.state, actor);
                        if (hidden.has_value()) {
                            if (!hidden_witness_matches(
                                    point, coordinate,
                                    material, *hidden)) {
                                throw std::runtime_error(
                                    "AQ16-DBC6 hidden repartition "
                                    "changed owner-safe root material");
                            }
                            root_hidden_witness = true;
                            if (!hidden_witness) {
                                hidden_witness = true;
                                hidden_witness_root_id =
                                    material.manifest
                                        .stable_root_id;
                            }
                        }
                    }
                    if (visitor != nullptr) {
                        (*visitor)({
                            .manifest = material.manifest,
                            .observation =
                                material.observation,
                            .actions = material.actions,
                            .option_rows =
                                material.option_rows,
                            .hidden_repartition_witness =
                                root_hidden_witness,
                        });
                    }
                    roots.push_back(
                        std::move(material.manifest));
                }
            }
        }
    }
    if (!hidden_witness) {
        throw std::runtime_error(
            "AQ16-DBC6 hidden-repartition witness was vacuous");
    }
    Census census =
        testing::make_census(
            learned_model_fingerprint(parent),
            std::move(roots));
    return {
        .census = std::move(census),
        .hidden_repartition_witness =
            hidden_witness,
        .hidden_witness_root_id =
            std::move(hidden_witness_root_id),
    };
}

Collection collect_census(
    std::shared_ptr<const LearnedModel> parent) {
    return collect_census_impl(
        std::move(parent), nullptr);
}

Collection collect_census(
    std::shared_ptr<const LearnedModel> parent,
    const AuthenticatedRootVisitor& visitor) {
    if (!visitor) {
        throw std::invalid_argument(
            "AQ16-DBC6 authenticated visitor is empty");
    }
    return collect_census_impl(
        std::move(parent), &visitor);
}

RunReport run(std::shared_ptr<const LearnedModel> parent) {
    require_parent(parent);
    const Collection first = collect_census(parent);
    const Collection repeated = collect_census(parent);
    if (first != repeated) {
        throw std::runtime_error(
            "AQ16-DBC6 repeated collection was not bit-identical");
    }
    return {
        .census = first.census,
        .repeated_collection_bit_identical = true,
        .hidden_repartition_witness =
            first.hidden_repartition_witness,
        .hidden_witness_root_id =
            first.hidden_witness_root_id,
        .source_collections = 2,
    };
}

void print_report(
    std::ostream& output, const RunReport& report) {
    validate_census(report.census);
    if (!report.repeated_collection_bit_identical ||
        !report.hidden_repartition_witness ||
        report.source_collections != 2 ||
        !sha256_is_canonical(
            report.hidden_witness_root_id) ||
        std::count_if(
            report.census.roots.begin(),
            report.census.roots.end(),
            [&](const ManifestRoot& root) {
                return root.stable_root_id ==
                       report.hidden_witness_root_id;
            }) != 1) {
        throw std::invalid_argument(
            "AQ16-DBC6 report gates are invalid");
    }
    output
        << "result=PASS"
        << " disposition=CENSUS_ONLY"
        << " identifier=" << kIdentifier
        << " parent=" << report.census.parent_fingerprint
        << " manifest=" << report.census.manifest_hash
        << " root_seed=" << report.census.root_seed
        << " generation=" << kScheduleGeneration
        << " collections=" << report.source_collections
        << " repeated_bit_identical=1"
        << " hidden_repartition_witness=1"
        << " hidden_witness_root="
        << report.hidden_witness_root_id
        << " teacher_labels=0"
        << " candidate_scores=0"
        << " model_created=0"
        << " selector_opened=0"
        << " artifact_published=0\n";
    for (const SplitCensus& split : report.census.splits) {
        const std::string_view split_name =
            split.split == Split::Train
                ? "TRAIN"
                : "DEV";
        output
            << "split=" << split_name
            << " games=" << split.games
            << " actor_games=" << split.actor_games
            << " roots=" << split.roots
            << " options=" << split.options
            << " potential_pairs="
            << split.potential_pairs << '\n';
        for (std::size_t deck = 0;
             deck < kDeckCount; ++deck) {
            const DeckCensus& row = split.decks[deck];
            output
                << "deck split=" << split_name
                << " deck="
                << deck_name(
                       static_cast<DeckId>(deck))
                << " actor_games=" << row.actor_games
                << " roots=" << row.roots
                << " options=" << row.options
                << " potential_pairs="
                << row.potential_pairs << '\n';
            for (const WidthCensus& width :
                 row.widths) {
                output
                    << "width split=" << split_name
                    << " deck="
                    << deck_name(
                           static_cast<DeckId>(deck))
                    << " legal_actions="
                    << width.legal_action_count
                    << " roots=" << width.roots
                    << " options=" << width.options
                    << " potential_pairs="
                    << width.potential_pairs << '\n';
            }
        }
        for (const ActorGameCensus& row :
             split.actor_game_rows) {
            output
                << "actor_game split=" << split_name
                << " block=" << row.block_index
                << " schedule=" << row.schedule_index
                << " actor=" << row.actor
                << " deck=" << deck_name(row.owner_deck)
                << " roots=" << row.roots
                << " options=" << row.options
                << " potential_pairs="
                << row.potential_pairs << '\n';
            for (const WidthCensus& width :
                 row.widths) {
                output
                    << "actor_game_width split="
                    << split_name
                    << " block=" << row.block_index
                    << " schedule=" << row.schedule_index
                    << " actor=" << row.actor
                    << " legal_actions="
                    << width.legal_action_count
                    << " roots=" << width.roots
                    << " options=" << width.options
                    << " potential_pairs="
                    << width.potential_pairs << '\n';
            }
        }
        for (const WidthCensus& width :
             split.widths) {
            output
                << "split_width split=" << split_name
                << " legal_actions="
                << width.legal_action_count
                << " roots=" << width.roots
                << " options=" << width.options
                << " potential_pairs="
                << width.potential_pairs << '\n';
        }
    }
}

namespace testing {

void validate_schedule_block(
    std::size_t block_index,
    std::span<const learned_iteration::ScheduledGame> games) {
    validate_schedule_block_impl(block_index, games);
}

ManifestRoot make_manifest_root(
    const RootCoordinate& coordinate,
    std::vector<PriorityAction> actions,
    std::vector<std::vector<double>> option_rows) {
    return make_manifest_root_impl(
        coordinate, std::move(actions),
        std::move(option_rows));
}

Census make_census(
    std::string parent_fingerprint,
    std::vector<ManifestRoot> roots) {
    Census census{
        .root_seed = kCollectionRootSeed,
        .parent_fingerprint =
            std::move(parent_fingerprint),
        .roots = std::move(roots),
    };
    census.splits = summarize_roots(census.roots);
    census.manifest_hash =
        canonical_manifest_hash(census);
    validate_census(census);
    return census;
}

std::optional<GameState> hidden_repartition(
    const GameState& state, std::size_t observer) {
    return hidden_repartition_impl(state, observer);
}

ManifestRoot make_live_manifest_root(
    const LearnedDecisionTracePoint& point,
    const RootCoordinate& coordinate) {
    return make_live_material(
               point, coordinate)
        .manifest;
}

} // namespace testing

} // namespace old_school::decision_density_census
