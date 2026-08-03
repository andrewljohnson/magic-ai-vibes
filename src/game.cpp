#include "old_school/game.hpp"

#include <algorithm>
#include <atomic>
#include <bitset>
#include <array>
#include <bit>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <numeric>
#include <span>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

namespace old_school {

namespace {

constexpr std::array<CardDefinition, kCardCount> kCardDefinitions = {{
    {CardId::Forest, "Forest", CardType::Land, {}, 0, 0, 0},
    {CardId::Mountain, "Mountain", CardType::Land, {}, 0, 0, 0},
    {CardId::GrizzlyBears,
     "Grizzly Bears",
     CardType::Creature,
     {.generic = 1, .green = 1, .red = 0},
     2,
     2,
     0},
    {CardId::LightningBolt,
     "Lightning Bolt",
     CardType::Instant,
     {.generic = 0, .green = 0, .red = 1},
     0,
     0,
     3},
    {CardId::IronrootTreefolk,
     "Ironroot Treefolk",
     CardType::Creature,
     {.generic = 4, .green = 1, .red = 0},
     3,
     5,
     0},
    {CardId::FireElemental,
     "Fire Elemental",
     CardType::Creature,
     {.generic = 3, .green = 0, .red = 2},
     5,
     4,
     0},
    {CardId::Island, "Island", CardType::Land, {}, 0, 0, 0},
    {CardId::Counterspell,
     "Counterspell",
     CardType::Instant,
     {.generic = 0, .green = 0, .red = 0, .blue = 2},
     0,
     0,
     0},
    {CardId::WaterElemental,
     "Water Elemental",
     CardType::Creature,
     {.generic = 3, .green = 0, .red = 0, .blue = 2},
     5,
     4,
     0},
    {CardId::Tsunami,
     "Tsunami",
     CardType::Sorcery,
     {.generic = 3, .green = 1, .red = 0, .blue = 0},
     0,
     0,
     0},
    {CardId::Plains, "Plains", CardType::Land, {}, 0, 0, 0},
    {CardId::Millstone,
     "Millstone",
     CardType::Artifact,
     {.generic = 2},
     0,
     0,
     0},
    {CardId::Moat,
     "Moat",
     CardType::Enchantment,
     {.generic = 2,
      .green = 0,
      .red = 0,
      .blue = 0,
      .white = 2},
     0,
     0,
     0},
    {CardId::FlyingMen,
     "Flying Men",
     CardType::Creature,
     {.generic = 0, .blue = 1},
     1,
     1,
     0,
     true},
    {CardId::IronclawOrcs,
     "Ironclaw Orcs",
     CardType::Creature,
     {.generic = 1, .red = 1},
     2,
     2,
     0,
     false,
     2},
    {CardId::GrayOgre,
     "Gray Ogre",
     CardType::Creature,
     {.generic = 2, .red = 1},
     2,
     2,
     0},
    {CardId::HillGiant,
     "Hill Giant",
     CardType::Creature,
     {.generic = 3, .red = 1},
     3,
     3,
     0},
    {CardId::Disintegrate,
     "Disintegrate",
     CardType::Sorcery,
     {.generic = 0, .red = 1},
     0,
     0,
     0},
    {CardId::GiantGrowth,
     "Giant Growth",
     CardType::Instant,
     {.generic = 0, .green = 1},
     0,
     0,
     0},
    {CardId::MoxSapphire,
     "Mox Sapphire",
     CardType::Artifact,
     {},
     0,
     0,
     0},
    {CardId::SolRing,
     "Sol Ring",
     CardType::Artifact,
     {.generic = 1},
     0,
     0,
     0},
    {CardId::AncestralRecall,
     "Ancestral Recall",
     CardType::Instant,
     {.blue = 1},
     0,
     0,
     0},
    {CardId::TimeWalk,
     "Time Walk",
     CardType::Sorcery,
     {.generic = 1, .blue = 1},
     0,
     0,
     0},
    {CardId::Braingeyser,
     "Braingeyser",
     CardType::Sorcery,
     {.blue = 2},
     0,
     0,
     0},
    {CardId::ForceSpike,
     "Force Spike",
     CardType::Instant,
     {.blue = 1},
     0,
     0,
     0},
    {CardId::AirElemental,
     "Air Elemental",
     CardType::Creature,
     {.generic = 3, .blue = 2},
     4,
     4,
     0,
     true},
    {CardId::BlackLotus,
     "Black Lotus",
     CardType::Artifact,
     {},
     0,
     0,
     0,
     false},
    {CardId::Channel,
     "Channel",
     CardType::Sorcery,
     {.green = 2},
     0,
     0,
     0,
     false},
    {CardId::LlanowarElves,
     "Llanowar Elves",
     CardType::Creature,
     {.green = 1},
     1,
     1,
     0,
     false},
    {CardId::MossBeast,
     "Moss Beast",
     CardType::Creature,
     {.generic = 2, .green = 2},
     4,
     5,
     0,
     false},
    {CardId::ForestColossus,
     "Forest Colossus",
     CardType::Creature,
     {.generic = 4, .green = 2},
     7,
     7,
     0,
     false},
    {CardId::SavannahLions,
     "Savannah Lions",
     CardType::Creature,
     {.white = 1},
     2,
     1,
     0,
     false},
    {CardId::SerendibEfreet,
     "Serendib Efreet",
     CardType::Creature,
     {.generic = 2, .blue = 1},
     3,
     4,
     0,
     true},
    {CardId::SerraAngel,
     "Serra Angel",
     CardType::Creature,
     {.generic = 3, .white = 2},
     4,
     4,
     0,
     true,
     0,
     true},
    {CardId::BlackVise,
     "Black Vise",
     CardType::Artifact,
     {.generic = 1},
     0,
     0,
     0,
     false},
    {CardId::MoxPearl,
     "Mox Pearl",
     CardType::Artifact,
     {},
     0,
     0,
     0,
     false},
    {CardId::MoxRuby,
     "Mox Ruby",
     CardType::Artifact,
     {},
     0,
     0,
     0,
     false},
    {CardId::Disenchant,
     "Disenchant",
     CardType::Instant,
     {.generic = 1, .white = 1},
     0,
     0,
     0,
     false},
    {CardId::PsionicBlast,
     "Psionic Blast",
     CardType::Instant,
     {.generic = 2, .blue = 1},
     0,
     0,
     4,
     false},
    {CardId::SwordsToPlowshares,
     "Swords to Plowshares",
     CardType::Instant,
     {.white = 1},
     0,
     0,
     0,
     false},
    {CardId::Plateau,
     "Plateau",
     CardType::Land,
     {},
     0,
     0,
     0,
     false},
    {CardId::Tundra,
     "Tundra",
     CardType::Land,
     {},
     0,
     0,
     0,
     false},
    {CardId::VolcanicIsland,
     "Volcanic Island",
     CardType::Land,
     {},
     0,
     0,
     0,
     false},
    {CardId::WheelOfFortune,
     "Wheel of Fortune",
     CardType::Sorcery,
     {.generic = 2, .red = 1},
     0,
     0,
     0,
     false},
    {CardId::LibraryOfAlexandria,
     "Library of Alexandria",
     CardType::Land,
     {},
     0,
     0,
     0,
     false},
    {CardId::MishrasFactory,
     "Mishra's Factory",
     CardType::Land,
     {},
     2,
     2,
     0,
     false},
    {CardId::StripMine,
     "Strip Mine",
     CardType::Land,
     {},
     0,
     0,
     0,
     false},
    {CardId::SuChi, "Su-Chi", CardType::Creature, {.generic = 4}, 4, 4,
     0, false},
    {CardId::SageOfLatNam,
     "Sage of Lat-Nam",
     CardType::Creature,
     {.generic = 1, .blue = 1},
     1,
     2,
     0,
     false},
    {CardId::Triskelion,
     "Triskelion",
     CardType::Creature,
     {.generic = 6},
     1,
     1,
     0,
     false},
    {CardId::ManaDrain,
     "Mana Drain",
     CardType::Instant,
     {.blue = 2},
     0,
     0,
     0,
     false},
    {CardId::Armageddon,
     "Armageddon",
     CardType::Sorcery,
     {.generic = 3, .white = 1},
     0,
     0,
     0,
     false},
    {CardId::DemonicTutor,
     "Demonic Tutor",
     CardType::Sorcery,
     {.generic = 1, .black = 1},
     0,
     0,
     0,
     false},
    {CardId::MindTwist,
     "Mind Twist",
     CardType::Sorcery,
     {.black = 1},
     0,
     0,
     0,
     false},
    {CardId::Recall,
     "Recall",
     CardType::Sorcery,
     {.blue = 1},
     0,
     0,
     0,
     false},
    {CardId::CopyArtifact,
     "Copy Artifact",
     CardType::Enchantment,
     {.generic = 1, .blue = 1},
     0,
     0,
     0,
     false},
    {CardId::FellwarStone,
     "Fellwar Stone",
     CardType::Artifact,
     {.generic = 2},
     0,
     0,
     0,
     false},
    {CardId::MoxEmerald,
     "Mox Emerald",
     CardType::Artifact,
     {},
     0,
     0,
     0,
     false},
    {CardId::MoxJet,
     "Mox Jet",
     CardType::Artifact,
     {},
     0,
     0,
     0,
     false},
    {CardId::CityOfBrass,
     "City of Brass",
     CardType::Land,
     {},
     0,
     0,
     0,
     false},
    {CardId::UndergroundSea,
     "Underground Sea",
     CardType::Land,
     {},
     0,
     0,
     0,
     false},
    {CardId::Badlands,
     "Badlands",
     CardType::Land,
     {},
     0,
     0,
     0,
     false},
    {CardId::Timetwister,
     "Timetwister",
     CardType::Sorcery,
     {.generic = 2, .blue = 1},
     0,
     0,
     0,
     false},
    {CardId::BenalishHero,
     "Benalish Hero",
     CardType::Creature,
     {.white = 1},
     1,
     1,
     0,
     false},
    {CardId::MesaPegasus,
     "Mesa Pegasus",
     CardType::Creature,
     {.generic = 1, .white = 1},
     1,
     1,
     0,
     true},
    {CardId::ThunderSpirit,
     "Thunder Spirit",
     CardType::Creature,
     {.generic = 1, .white = 2},
     2,
     2,
     0,
     true},
    {CardId::WhiteKnight,
     "White Knight",
     CardType::Creature,
     {.white = 2},
     2,
     2,
     0,
     false},
    {CardId::ChaosOrb,
     "Chaos Orb",
     CardType::Artifact,
     {.generic = 2},
     0,
     0,
     0,
     false},
    {CardId::JalumTome,
     "Jalum Tome",
     CardType::Artifact,
     {.generic = 3},
     0,
     0,
     0,
     false},
    {CardId::Crusade,
     "Crusade",
     CardType::Enchantment,
     {.white = 2},
     0,
     0,
     0,
     false},
    {CardId::JuzamDjinn,
     "Juzam Djinn",
     CardType::Creature,
     {.generic = 2, .black = 2},
     5,
     5,
     0,
     false},
    {CardId::SedgeTroll,
     "Sedge Troll",
     CardType::Creature,
     {.generic = 2, .red = 1},
     2,
     2,
     0,
     false},
    {CardId::HypnoticSpecter,
     "Hypnotic Specter",
     CardType::Creature,
     {.generic = 1, .black = 2},
     2,
     2,
     0,
     true},
    {CardId::DarkRitual,
     "Dark Ritual",
     CardType::Instant,
     {.black = 1},
     0,
     0,
     0,
     false},
    {CardId::Shatter,
     "Shatter",
     CardType::Instant,
     {.generic = 1, .red = 1},
     0,
     0,
     0,
     false},
    {CardId::Swamp,
     "Swamp",
     CardType::Land,
     {},
     0,
     0,
     0,
     false},
}};

constexpr std::array<CardId, 25> kCreatureCards = {
    CardId::GrizzlyBears,
    CardId::IronrootTreefolk,
    CardId::FireElemental,
    CardId::WaterElemental,
    CardId::FlyingMen,
    CardId::IronclawOrcs,
    CardId::GrayOgre,
    CardId::HillGiant,
    CardId::AirElemental,
    CardId::LlanowarElves,
    CardId::MossBeast,
    CardId::ForestColossus,
    CardId::SavannahLions,
    CardId::SerendibEfreet,
    CardId::SerraAngel,
    CardId::SuChi,
    CardId::SageOfLatNam,
    CardId::Triskelion,
    CardId::BenalishHero,
    CardId::MesaPegasus,
    CardId::ThunderSpirit,
    CardId::WhiteKnight,
    CardId::JuzamDjinn,
    CardId::SedgeTroll,
    CardId::HypnoticSpecter,
};

constexpr std::array<CardId, 6> kSorceryCards = {
    CardId::Tsunami,
    CardId::TimeWalk,
    CardId::Channel,
    CardId::WheelOfFortune,
    CardId::Armageddon,
    CardId::Timetwister,
};

constexpr std::array<CardId, 12> kArtifactCards = {
    CardId::Millstone,
    CardId::MoxSapphire,
    CardId::SolRing,
    CardId::BlackLotus,
    CardId::BlackVise,
    CardId::MoxPearl,
    CardId::MoxRuby,
    CardId::FellwarStone,
    CardId::MoxEmerald,
    CardId::MoxJet,
    CardId::ChaosOrb,
    CardId::JalumTome,
};

constexpr std::array<CardId, 2> kEnchantmentCards = {
    CardId::Moat,
    CardId::Crusade,
};

constexpr ManaCost kMillstoneActivationCost = {.generic = 2};
// Cards milled per Millstone activation. Overridable at compile time
// for dose-response diagnostics (payoff distance vs learnability).
#ifndef MILLSTONE_CARDS
#define MILLSTONE_CARDS 2
#endif
constexpr int kMillstoneCards = MILLSTONE_CARDS;

ManaCost disintegrate_cost(int x_value) {
    return {.generic = x_value, .red = 1};
}

ManaCost braingeyser_cost(int x_value) {
    return {.generic = x_value, .blue = 2};
}


ManaCost mind_twist_cost(int x_value) {
    return {.generic = x_value, .black = 1};
}

ManaCost recall_cost(int x_value) {
    return {.generic = x_value, .blue = 1};
}

bool has_card(const std::vector<CardId>& cards, CardId wanted) {
    return std::find(cards.begin(), cards.end(), wanted) != cards.end();
}

bool remove_card(std::vector<CardId>& cards, CardId wanted) {
    const auto position = std::find(cards.begin(), cards.end(), wanted);
    if (position == cards.end()) {
        return false;
    }
    cards.erase(position);
    return true;
}

using CardCounts = std::array<std::size_t, kCardDefinitions.size()>;

std::size_t checked_card_index(CardId card) {
    const auto index = static_cast<std::size_t>(card);
    if (index >= kCardDefinitions.size()) {
        throw std::invalid_argument("state contains an unknown card");
    }
    return index;
}

void add_card(CardCounts& counts, CardId card) {
    ++counts[checked_card_index(card)];
}

void subtract_card(CardCounts& counts, CardId card) {
    auto& count = counts[checked_card_index(card)];
    if (count == 0) {
        throw std::invalid_argument(
            "public state is inconsistent with original deck");
    }
    --count;
}

CardCounts card_counts(const std::vector<CardId>& cards) {
    CardCounts counts{};
    for (const CardId card : cards) {
        add_card(counts, card);
    }
    return counts;
}

void subtract_public_cards(CardCounts& remaining,
                           const PlayerState& player) {
    for (const CardId card : player.graveyard) {
        subtract_card(remaining, card);
    }
    for (const CardId card : player.exile) {
        subtract_card(remaining, card);
    }
    for (const auto& land : player.lands) {
        subtract_card(remaining, land.card);
    }
    for (const auto& creature : player.creatures) {
        subtract_card(remaining, creature.card);
    }
    for (const auto& artifact : player.artifacts) {
        subtract_card(remaining, artifact.card);
    }
    for (const CardId card : player.enchantments) {
        subtract_card(remaining, card);
    }
}

std::vector<CardId> expand_card_counts(const CardCounts& counts) {
    std::vector<CardId> cards;
    for (std::size_t index = 0; index < counts.size(); ++index) {
        cards.insert(cards.end(), counts[index],
                     static_cast<CardId>(index));
    }
    return cards;
}

CardCounts physical_card_counts(const GameState& state,
                                std::size_t player) {
    CardCounts counts{};
    const auto& player_state = state.players[player];
    for (const CardId card : player_state.library) {
        add_card(counts, card);
    }
    for (const CardId card : player_state.hand) {
        add_card(counts, card);
    }
    for (const CardId card : player_state.graveyard) {
        add_card(counts, card);
    }
    for (const CardId card : player_state.exile) {
        add_card(counts, card);
    }
    for (const auto& land : player_state.lands) {
        add_card(counts, land.card);
    }
    for (const auto& creature : player_state.creatures) {
        add_card(counts, creature.card);
    }
    for (const auto& artifact : player_state.artifacts) {
        add_card(counts, artifact.card);
    }
    for (const CardId card : player_state.enchantments) {
        add_card(counts, card);
    }
    for (const auto& object : state.stack) {
        if (object.controller == player &&
            object.kind == StackObjectKind::Spell) {
            add_card(counts, object.card);
        }
    }
    return counts;
}

bool is_land(CardId card) {
    return card_definition(card).type == CardType::Land;
}

int mana_total(const ManaCost& mana) {
    return mana.generic + mana.green + mana.red + mana.blue +
           mana.white + mana.black;
}

void add_mana(ManaCost& pool, const ManaCost& produced) {
    pool.generic += produced.generic;
    pool.green += produced.green;
    pool.red += produced.red;
    pool.blue += produced.blue;
    pool.white += produced.white;
    pool.black += produced.black;
}

ManaCost land_mana(CardId card) {
    switch (card) {
    case CardId::Forest:
        return {.green = 1};
    case CardId::Mountain:
        return {.red = 1};
    case CardId::Island:
        return {.blue = 1};
    case CardId::Plains:
        return {.white = 1};
    // Dual lands: land_mana reports one face for aggregate counting;
    // the payment planner treats them as either color via
    // land_provides().
    case CardId::Plateau:
        return {.red = 1};
    case CardId::Tundra:
        return {.white = 1};
    case CardId::VolcanicIsland:
        return {.blue = 1};
    case CardId::LibraryOfAlexandria:
    case CardId::MishrasFactory:
    case CardId::StripMine:
        return {.generic = 1};
    case CardId::UndergroundSea:
        return {.blue = 1};
    case CardId::Badlands:
        return {.red = 1};
    case CardId::CityOfBrass:
        return {.generic = 1};
    case CardId::Swamp:
        return {.black = 1};
    default:
        return {};
    }
}

ManaCost artifact_mana(CardId card) {
    switch (card) {
    case CardId::MoxSapphire:
        return {.blue = 1};
    case CardId::MoxPearl:
        return {.white = 1};
    case CardId::MoxRuby:
        return {.red = 1};
    case CardId::SolRing:
        return {.generic = 2};
    case CardId::MoxEmerald:
        return {.green = 1};
    case CardId::MoxJet:
        return {.black = 1};
    case CardId::FellwarStone:
        return {.generic = 1};
    default:
        return {};
    }
}

// Zone sizes are bounded by the sixty-card deck, so payment plans use
// fixed bitsets: planning allocates nothing and copies are trivial.
inline constexpr std::size_t kPlanSlots = 64;

struct ManaPaymentPlan {
    bool possible = false;
    std::bitset<kPlanSlots> tap_lands;
    std::bitset<kPlanSlots> tap_artifacts;
    // Mana creatures (Llanowar Elves) tap for their color; summoning
    // sickness gates the tap ability like attacking.
    std::bitset<kPlanSlots> tap_creatures;
    // Black Lotus is sacrificed, not tapped, and yields three mana of one
    // chosen color (tracked by adding it directly to the pool).
    std::bitset<kPlanSlots> sacrifice_artifacts;
    // Channel converts life into mana, one for one, down to one life.
    int channel_life = 0;
    ManaCost remaining_pool;
};

// A Copy Artifact permanent behaves on the battlefield as the artifact
// it entered copying; the physical card (graveyards, conservation)
// stays CopyArtifact.
CardId artifact_face(const ArtifactPermanent& artifact) {
    return artifact.is_copy ? artifact.copy_of : artifact.card;
}

CardId creature_face(const CreaturePermanent& creature) {
    return creature.is_copy ? creature.copy_of : creature.card;
}

CardId land_face(const LandPermanent& land) {
    return land.is_copy ? land.copy_of : land.card;
}

// Type-line fact: which creatures are artifact creatures (and thus
// legal Copy Artifact subjects). The animated Factory qualifies.
bool is_artifact_creature(CardId card) {
    return card == CardId::SuChi || card == CardId::Triskelion ||
           card == CardId::MishrasFactory;
}

ManaPaymentPlan plan_mana_payment(const PlayerState& player,
                                  const ManaCost& cost) {
    const std::array<int, 6> cost_parts = {
        cost.generic, cost.green,  cost.red,
        cost.blue,    cost.white,  cost.black,
    };
    if (std::any_of(cost_parts.begin(), cost_parts.end(),
                    [](int amount) { return amount < 0; })) {
        return {};
    }

    ManaPaymentPlan plan;
    plan.remaining_pool = player.mana_pool;

    const auto tap_land = [&](std::size_t index) {
        plan.tap_lands[index] = true;
        add_mana(plan.remaining_pool,
                 land_mana(land_face(player.lands[index])));
    };
    const auto tap_artifact = [&](std::size_t index) {
        plan.tap_artifacts[index] = true;
        add_mana(plan.remaining_pool,
                 artifact_mana(
                     artifact_face(player.artifacts[index])));
    };
    const auto tap_elf = [&]() {
        for (std::size_t index = 0; index < player.creatures.size();
             ++index) {
            const auto& creature = player.creatures[index];
            if (creature.card == CardId::LlanowarElves &&
                !creature.tapped && !creature.summoning_sick &&
                !plan.tap_creatures[index]) {
                plan.tap_creatures[index] = true;
                plan.remaining_pool.green += 1;
                return true;
            }
        }
        return false;
    };
    const auto sacrifice_lotus_for = [&](int ManaCost::*color) {
        for (std::size_t index = 0; index < player.artifacts.size();
             ++index) {
            if (!player.artifacts[index].tapped &&
                !plan.tap_artifacts[index] &&
                !plan.sacrifice_artifacts[index] &&
                artifact_face(player.artifacts[index]) ==
                    CardId::BlackLotus) {
                plan.sacrifice_artifacts[index] = true;
                plan.remaining_pool.*color += 3;
                return true;
            }
        }
        return false;
    };
    const auto mox_for_color = [](int ManaCost::*color) {
        if (color == &ManaCost::blue) return CardId::MoxSapphire;
        if (color == &ManaCost::white) return CardId::MoxPearl;
        if (color == &ManaCost::red) return CardId::MoxRuby;
        if (color == &ManaCost::green) return CardId::MoxEmerald;
        if (color == &ManaCost::black) return CardId::MoxJet;
        return CardId::Forest;  // sentinel: no mox for this color
    };
    const auto select_color = [&](int ManaCost::*color, int needed) {
        // Color-matched moxen first (they only ever make this color),
        // then basics, then duals (kept flexible as long as possible),
        // then elves for green, then Lotus.
        const CardId mox = mox_for_color(color);
        for (std::size_t index = 0;
             index < player.artifacts.size() && needed > 0; ++index) {
            if (!player.artifacts[index].tapped &&
                !plan.tap_artifacts[index] &&
                artifact_face(player.artifacts[index]) == mox &&
                mox != CardId::Forest) {
                tap_artifact(index);
                --needed;
            }
        }
        // Fellwar Stone: one mana of any color (house simplification).
        for (std::size_t index = 0;
             index < player.artifacts.size() && needed > 0; ++index) {
            if (!player.artifacts[index].tapped &&
                !plan.tap_artifacts[index] &&
                artifact_face(player.artifacts[index]) ==
                    CardId::FellwarStone) {
                plan.tap_artifacts[index] = true;
                plan.remaining_pool.*color += 1;
                --needed;
            }
        }
        // 0 = basics, 1 = painless duals, 2 = City of Brass (the
        // life ping makes it the last colored source touched).
        const auto tap_matching = [&](int tier, int remaining) {
            for (std::size_t index = 0;
                 index < player.lands.size() && remaining > 0;
                 ++index) {
                const CardId land = land_face(player.lands[index]);
                const bool city = land == CardId::CityOfBrass;
                const bool dual =
                    land == CardId::Plateau ||
                    land == CardId::Tundra ||
                    land == CardId::VolcanicIsland ||
                    land == CardId::UndergroundSea ||
                    land == CardId::Badlands;
                const int land_tier = city ? 2 : dual ? 1 : 0;
                if (land_tier == tier &&
                    !player.lands[index].tapped &&
                    !plan.tap_lands[index] &&
                    land_provides(land, color)) {
                    if (dual || city) {
                        // A flexible land makes the face we ask of
                        // it, not the fixed land_mana() face.
                        plan.tap_lands[index] = true;
                        plan.remaining_pool.*color += 1;
                    } else {
                        tap_land(index);
                    }
                    --remaining;
                }
            }
            return remaining;
        };
        needed = tap_matching(0, needed);
        needed = tap_matching(1, needed);
        needed = tap_matching(2, needed);
        while (needed > 0 && color == &ManaCost::green && tap_elf()) {
            --needed;
        }
        while (needed > 0) {
            if (!sacrifice_lotus_for(color)) {
                break;
            }
            needed = std::max(0, needed - 3);
        }
        return needed == 0;
    };

    const int green_needed =
        std::max(0, cost.green - plan.remaining_pool.green);
    const int red_needed =
        std::max(0, cost.red - plan.remaining_pool.red);
    const int blue_needed =
        std::max(0, cost.blue - plan.remaining_pool.blue);
    const int white_needed =
        std::max(0, cost.white - plan.remaining_pool.white);
    const int black_needed =
        std::max(0, cost.black - plan.remaining_pool.black);
    if (!select_color(&ManaCost::green, green_needed) ||
        !select_color(&ManaCost::red, red_needed) ||
        !select_color(&ManaCost::blue, blue_needed) ||
        !select_color(&ManaCost::white, white_needed) ||
        !select_color(&ManaCost::black, black_needed)) {
        return plan;
    }

    const int required_total = cost.generic + cost.green + cost.red +
                               cost.blue + cost.white + cost.black;
    const auto needs_more_mana = [&]() {
        return mana_total(plan.remaining_pool) < required_total;
    };

    // Generic costs prefer Sol Ring so colored sources remain available.
    // Its full two mana enters the phase-local pool; any excess is retained.
    for (std::size_t index = 0;
         index < player.artifacts.size() && needs_more_mana();
         ++index) {
        if (!player.artifacts[index].tapped &&
            !plan.tap_artifacts[index] &&
            artifact_face(player.artifacts[index]) ==
                CardId::SolRing) {
            tap_artifact(index);
        }
    }
    // Mana artifacts (moxen, Fellwar copies) go right after Sol
    // Ring, ranked by how little the hand wants their color: holding
    // a Lightning Bolt, the Pearl pays generic and the Ruby stays up.
    {
        std::array<int, 5> rock_demand{};
        for (const CardId held : player.hand) {
            const auto& held_cost = card_definition(held).cost;
            rock_demand[0] += held_cost.green > 0 ? 1 : 0;
            rock_demand[1] += held_cost.red > 0 ? 1 : 0;
            rock_demand[2] += held_cost.blue > 0 ? 1 : 0;
            rock_demand[3] += held_cost.white > 0 ? 1 : 0;
            rock_demand[4] += held_cost.black > 0 ? 1 : 0;
        }
        const auto rock_score = [&](CardId face) {
            const ManaCost mana = artifact_mana(face);
            if (face == CardId::FellwarStone) {
                // Any-color: as flexible as it gets, spend last.
                return 100;
            }
            return mana.green * (1 + rock_demand[0] * 4) +
                   mana.red * (1 + rock_demand[1] * 4) +
                   mana.blue * (1 + rock_demand[2] * 4) +
                   mana.white * (1 + rock_demand[3] * 4) +
                   mana.black * (1 + rock_demand[4] * 4);
        };
        std::array<std::size_t, kPlanSlots> rock_order;
        std::size_t rock_count = 0;
        for (std::size_t index = 0; index < player.artifacts.size();
             ++index) {
            if (!player.artifacts[index].tapped &&
                !plan.tap_artifacts[index] &&
                mana_total(artifact_mana(
                    artifact_face(player.artifacts[index]))) > 0) {
                rock_order[rock_count++] = index;
            }
        }
        std::stable_sort(
            rock_order.begin(),
            rock_order.begin() +
                static_cast<std::ptrdiff_t>(rock_count),
            [&](std::size_t left, std::size_t right) {
                return rock_score(artifact_face(
                           player.artifacts[left])) <
                       rock_score(artifact_face(
                           player.artifacts[right]));
            });
        for (std::size_t position = 0;
             position < rock_count && needs_more_mana();
             ++position) {
            tap_artifact(rock_order[position]);
        }
    }
    {
        // Generic payment then walks the lands in strict tiers -
        // basics, duals, City of Brass (life ping), ability lands -
        // with each tier ranked by how little the rest of the hand
        // wants that land's colors.
        std::array<int, 5> demand{};  // green, red, blue, white, black
        for (const CardId held : player.hand) {
            const auto& held_cost = card_definition(held).cost;
            demand[0] += held_cost.green > 0 ? 1 : 0;
            demand[1] += held_cost.red > 0 ? 1 : 0;
            demand[2] += held_cost.blue > 0 ? 1 : 0;
            demand[3] += held_cost.white > 0 ? 1 : 0;
            demand[4] += held_cost.black > 0 ? 1 : 0;
        }
        const auto land_score = [&](CardId land) {
            int score = 0;
            if (land_provides(land, &ManaCost::green)) {
                score += 1 + demand[0] * 4;
            }
            if (land_provides(land, &ManaCost::red)) {
                score += 1 + demand[1] * 4;
            }
            if (land_provides(land, &ManaCost::blue)) {
                score += 1 + demand[2] * 4;
            }
            if (land_provides(land, &ManaCost::white)) {
                score += 1 + demand[3] * 4;
            }
            if (land_provides(land, &ManaCost::black)) {
                score += 1 + demand[4] * 4;
            }
            return score;
        };
        const auto land_tier = [](CardId land) {
            switch (land) {
            case CardId::Plateau:
            case CardId::Tundra:
            case CardId::VolcanicIsland:
            case CardId::UndergroundSea:
            case CardId::Badlands:
                return 1;  // duals after basics
            case CardId::CityOfBrass:
                return 2;  // the life ping
            case CardId::StripMine:
                return 3;
            case CardId::MishrasFactory:
                return 4;
            case CardId::LibraryOfAlexandria:
                return 5;  // ability lands dead last
            default:
                return 0;  // basics
            }
        };
        std::array<std::size_t, kPlanSlots> order;
        std::size_t order_count = 0;
        for (std::size_t index = 0; index < player.lands.size();
             ++index) {
            if (!player.lands[index].tapped &&
                !plan.tap_lands[index]) {
                order[order_count++] = index;
            }
        }
        std::stable_sort(
            order.begin(),
            order.begin() +
                static_cast<std::ptrdiff_t>(order_count),
            [&](std::size_t left, std::size_t right) {
                const CardId left_card =
                    land_face(player.lands[left]);
                const CardId right_card =
                    land_face(player.lands[right]);
                if (land_tier(left_card) != land_tier(right_card)) {
                    return land_tier(left_card) <
                           land_tier(right_card);
                }
                return land_score(left_card) < land_score(right_card);
            });
        for (std::size_t position = 0; position < order_count;
             ++position) {
            if (!needs_more_mana()) {
                break;
            }
            tap_land(order[position]);
        }
    }
    while (needs_more_mana() && tap_elf()) {
    }
    while (needs_more_mana() &&
           sacrifice_lotus_for(&ManaCost::generic)) {
    }
    if (player.channel_active && needs_more_mana()) {
        const int shortfall = required_total -
                              mana_total(plan.remaining_pool);
        const int payable = std::max(0, player.life - 1);
        plan.channel_life = std::min(shortfall, payable);
        plan.remaining_pool.generic += plan.channel_life;
    }

    if (plan.remaining_pool.green < cost.green ||
        plan.remaining_pool.red < cost.red ||
        plan.remaining_pool.blue < cost.blue ||
        plan.remaining_pool.white < cost.white ||
        plan.remaining_pool.black < cost.black ||
        mana_total(plan.remaining_pool) < required_total) {
        return plan;
    }

    // Trim overshoot: a Lotus chunk (or Sol Ring) can overpay, and
    // the greedy walk may have tapped small sources it no longer
    // needs. Release planned taps whose whole contribution is spare,
    // most-wanted-untapped first (a Mox in the hand's colors beats a
    // Factory tap that floats away). Colored requirements re-checked
    // per release.
    {
        const auto releasable = [&](const ManaCost& contribution) {
            const ManaCost after = {
                .generic =
                    plan.remaining_pool.generic - contribution.generic,
                .green =
                    plan.remaining_pool.green - contribution.green,
                .red = plan.remaining_pool.red - contribution.red,
                .blue = plan.remaining_pool.blue - contribution.blue,
                .white =
                    plan.remaining_pool.white - contribution.white,
                .black =
                    plan.remaining_pool.black - contribution.black,
            };
            return after.green >= cost.green &&
                   after.red >= cost.red && after.blue >= cost.blue &&
                   after.white >= cost.white &&
                   after.black >= cost.black &&
                   mana_total(after) >= required_total;
        };
        const auto remove_contribution =
            [&](const ManaCost& contribution) {
                plan.remaining_pool.generic -= contribution.generic;
                plan.remaining_pool.green -= contribution.green;
                plan.remaining_pool.red -= contribution.red;
                plan.remaining_pool.blue -= contribution.blue;
                plan.remaining_pool.white -= contribution.white;
                plan.remaining_pool.black -= contribution.black;
            };
        std::array<int, 5> keep_demand{};
        for (const CardId held : player.hand) {
            const auto& held_cost = card_definition(held).cost;
            keep_demand[0] += held_cost.green > 0 ? 1 : 0;
            keep_demand[1] += held_cost.red > 0 ? 1 : 0;
            keep_demand[2] += held_cost.blue > 0 ? 1 : 0;
            keep_demand[3] += held_cost.white > 0 ? 1 : 0;
            keep_demand[4] += held_cost.black > 0 ? 1 : 0;
        }
        const auto keep_score = [&](const ManaCost& mana) {
            return mana.green * (1 + keep_demand[0] * 4) +
                   mana.red * (1 + keep_demand[1] * 4) +
                   mana.blue * (1 + keep_demand[2] * 4) +
                   mana.white * (1 + keep_demand[3] * 4) +
                   mana.black * (1 + keep_demand[4] * 4) +
                   mana.generic;
        };
        struct Release {
            bool is_land = false;
            std::size_t index = 0;
            ManaCost contribution;
            int score = 0;
        };
        std::vector<Release> candidates;
        for (std::size_t index = 0; index < player.artifacts.size();
             ++index) {
            if (plan.tap_artifacts[index]) {
                const ManaCost mana = artifact_mana(
                    artifact_face(player.artifacts[index]));
                if (mana_total(mana) > 0) {
                    candidates.push_back({false, index, mana,
                                          keep_score(mana)});
                }
            }
        }
        for (std::size_t index = 0; index < player.lands.size();
             ++index) {
            if (!plan.tap_lands[index]) {
                continue;
            }
            // Flexible lands (duals, City) credited whichever face
            // the colored pass asked for - the trim cannot know
            // which, so only fixed-face lands are releasable.
            const CardId face = land_face(player.lands[index]);
            int provided_colors = 0;
            provided_colors +=
                land_provides(face, &ManaCost::green) ? 1 : 0;
            provided_colors +=
                land_provides(face, &ManaCost::red) ? 1 : 0;
            provided_colors +=
                land_provides(face, &ManaCost::blue) ? 1 : 0;
            provided_colors +=
                land_provides(face, &ManaCost::white) ? 1 : 0;
            provided_colors +=
                land_provides(face, &ManaCost::black) ? 1 : 0;
            if (provided_colors > 1) {
                continue;
            }
            const ManaCost mana = land_mana(face);
            if (mana_total(mana) > 0) {
                candidates.push_back(
                    {true, index, mana, keep_score(mana)});
            }
        }
        std::stable_sort(candidates.begin(), candidates.end(),
                         [](const Release& left,
                            const Release& right) {
                             return left.score > right.score;
                         });
        for (const Release& candidate : candidates) {
            if (!releasable(candidate.contribution)) {
                continue;
            }
            remove_contribution(candidate.contribution);
            if (candidate.is_land) {
                plan.tap_lands[candidate.index] = false;
            } else {
                plan.tap_artifacts[candidate.index] = false;
            }
        }
    }

    plan.remaining_pool.green -= cost.green;
    plan.remaining_pool.red -= cost.red;
    plan.remaining_pool.blue -= cost.blue;
    plan.remaining_pool.white -= cost.white;
    plan.remaining_pool.black -= cost.black;

    int generic_remaining = cost.generic;
    const auto spend_generic = [&](int& available) {
        const int spent = std::min(available, generic_remaining);
        available -= spent;
        generic_remaining -= spent;
    };
    spend_generic(plan.remaining_pool.generic);
    spend_generic(plan.remaining_pool.green);
    spend_generic(plan.remaining_pool.red);
    spend_generic(plan.remaining_pool.blue);
    spend_generic(plan.remaining_pool.white);
    spend_generic(plan.remaining_pool.black);
    if (generic_remaining != 0) {
        return plan;
    }

    plan.possible = true;
    return plan;
}

}  // namespace

// True when the land can produce the given colored mana.
bool land_provides(CardId land, int ManaCost::*color) {
    if (land == CardId::CityOfBrass) {
        return color == &ManaCost::green || color == &ManaCost::red ||
               color == &ManaCost::blue || color == &ManaCost::white ||
               color == &ManaCost::black;
    }
    if (color == &ManaCost::green) {
        return land == CardId::Forest;
    }
    if (color == &ManaCost::red) {
        return land == CardId::Mountain || land == CardId::Plateau ||
               land == CardId::VolcanicIsland ||
               land == CardId::Badlands;
    }
    if (color == &ManaCost::blue) {
        return land == CardId::Island || land == CardId::Tundra ||
               land == CardId::VolcanicIsland ||
               land == CardId::UndergroundSea;
    }
    if (color == &ManaCost::white) {
        return land == CardId::Plains || land == CardId::Plateau ||
               land == CardId::Tundra;
    }
    if (color == &ManaCost::black) {
        return land == CardId::UndergroundSea ||
               land == CardId::Badlands || land == CardId::Swamp;
    }
    return false;
}


std::vector<std::vector<PaymentTap>> alternative_payments(
    const PlayerState& player, const ManaCost& cost,
    std::size_t max_variants) {
    // Branch only where payment choice genuinely matters:
    //  1. ability lands (Library / Factory / Strip Mine): whichever
    //     way the auto plan went, offer the other direction — the
    //     cover that avoids them, or the cover that spends them to
    //     keep basics/duals/rocks open;
    //  2. a colored need payable by different dual-land types
    //     (Volcanic Island vs Tundra for {U}) -> one swap per type.
    // The base tiers (rocks, basics, duals, City) stay the planner's.
    const auto is_ability_land = [](CardId land) {
        return land == CardId::LibraryOfAlexandria ||
               land == CardId::MishrasFactory ||
               land == CardId::StripMine;
    };
    const auto is_dual = [](CardId land) {
        return land == CardId::Plateau || land == CardId::Tundra ||
               land == CardId::VolcanicIsland ||
               land == CardId::UndergroundSea ||
               land == CardId::Badlands;
    };
    const ManaPaymentPlan plan = plan_mana_payment(player, cost);
    if (!plan.possible || max_variants == 0) {
        return {};
    }
    std::vector<PermanentId> default_set;
    bool default_taps_ability = false;
    for (std::size_t index = 0; index < player.lands.size(); ++index) {
        if (plan.tap_lands[index]) {
            default_set.push_back(player.lands[index].id);
            default_taps_ability =
                default_taps_ability ||
                is_ability_land(player.lands[index].card);
        }
    }
    for (std::size_t index = 0; index < player.artifacts.size();
         ++index) {
        if (plan.tap_artifacts[index]) {
            default_set.push_back(player.artifacts[index].id);
        }
    }
    std::sort(default_set.begin(), default_set.end());

    static constexpr std::array<int ManaCost::*, 6> kFaces = {
        &ManaCost::generic, &ManaCost::green, &ManaCost::red,
        &ManaCost::blue,    &ManaCost::white, &ManaCost::black,
    };
    struct Source {
        PermanentId id = 0;
        CardId card = CardId::Forest;
        bool is_land = false;
        int amount = 1;  // generic contribution (Sol Ring = 2)
    };
    const auto provides_face = [&](const Source& source, int face) {
        if (face == 0) {
            if (source.is_land) {
                // City of Brass makes colored mana only.
                return source.card != CardId::CityOfBrass &&
                       land_mana(source.card).generic > 0;
            }
            return artifact_mana(source.card).generic > 0;
        }
        if (source.is_land) {
            return land_provides(
                source.card,
                kFaces[static_cast<std::size_t>(face)]);
        }
        if (source.card == CardId::FellwarStone) {
            return true;
        }
        return artifact_mana(source.card).*kFaces[static_cast<
                   std::size_t>(face)] > 0;
    };
    // Greedy cover in tier order. Mode 0 mirrors the planner
    // (ability lands dead last), mode 1 excludes ability lands, mode
    // 2 spends ability lands FIRST so everything else stays open.
    // `prefer_face`/`prefer_dual` bumps one dual type to the front of
    // that face's colored payment.
    enum { kLikePlanner = 0, kAvoidAbility = 1, kSpendAbility = 2 };
    const auto cover = [&](int mode, int prefer_face,
                           CardId prefer_dual)
        -> std::optional<std::vector<PaymentTap>> {
        std::vector<Source> pool;
        for (const auto& artifact : player.artifacts) {
            if (artifact.tapped) {
                continue;
            }
            const CardId face_card = artifact_face(artifact);
            if (face_card == CardId::FellwarStone) {
                pool.push_back({artifact.id, face_card, false, 1});
            } else if (mana_total(artifact_mana(face_card)) > 0) {
                pool.push_back(
                    {artifact.id, face_card, false,
                     std::max(1,
                              artifact_mana(face_card).generic)});
            }
        }
        for (const auto& land : player.lands) {
            if (land.tapped ||
                (mode == kAvoidAbility &&
                 is_ability_land(land_face(land)))) {
                continue;
            }
            pool.push_back({land.id, land_face(land), true, 1});
        }
        const auto spend_rank = [&](const Source& source) {
            if (!source.is_land) {
                return 1;
            }
            if (is_ability_land(source.card)) {
                return mode == kSpendAbility ? 0 : 5;
            }
            if (is_dual(source.card)) {
                return 3;
            }
            return source.card == CardId::CityOfBrass ? 4 : 2;
        };
        std::stable_sort(
            pool.begin(), pool.end(),
            [&](const Source& left, const Source& right) {
                return spend_rank(left) < spend_rank(right);
            });

        std::vector<PaymentTap> taps;
        std::vector<bool> used(pool.size(), false);
        int pool_spent_on_color = 0;
        for (int face = 1; face <= 5; ++face) {
            const int have =
                player.mana_pool.*kFaces[static_cast<std::size_t>(
                    face)];
            const int want =
                cost.*kFaces[static_cast<std::size_t>(face)];
            pool_spent_on_color += std::min(have, want);
            int needed = std::max(0, want - have);
            // Preferred dual first for the flagged face.
            if (needed > 0 && face == prefer_face) {
                for (std::size_t index = 0;
                     index < pool.size() && needed > 0; ++index) {
                    if (!used[index] &&
                        pool[index].card == prefer_dual &&
                        provides_face(pool[index], face)) {
                        used[index] = true;
                        taps.push_back({pool[index].id, face});
                        --needed;
                    }
                }
            }
            for (std::size_t index = 0;
                 index < pool.size() && needed > 0; ++index) {
                if (used[index] ||
                    !provides_face(pool[index], face)) {
                    continue;
                }
                used[index] = true;
                taps.push_back({pool[index].id, face});
                --needed;
            }
            if (needed > 0) {
                return std::nullopt;
            }
        }
        const int pool_total =
            player.mana_pool.generic + player.mana_pool.green +
            player.mana_pool.red + player.mana_pool.blue +
            player.mana_pool.white + player.mana_pool.black;
        int generic_needed = std::max(
            0, cost.generic - (pool_total - pool_spent_on_color));
        for (std::size_t index = 0;
             index < pool.size() && generic_needed > 0; ++index) {
            if (used[index]) {
                continue;
            }
            int face = 0;
            if (!provides_face(pool[index], 0)) {
                face = 1;
                while (face <= 5 &&
                       !provides_face(pool[index], face)) {
                    ++face;
                }
                if (face > 5) {
                    continue;
                }
            }
            used[index] = true;
            taps.push_back({pool[index].id, face});
            generic_needed -= pool[index].amount;
        }
        if (generic_needed > 0 || taps.empty()) {
            return std::nullopt;
        }
        return taps;
    };

    std::vector<std::vector<PaymentTap>> variants;
    std::vector<std::vector<PermanentId>> seen = {default_set};
    const auto emit = [&](std::optional<std::vector<PaymentTap>>
                              candidate) {
        if (!candidate.has_value() ||
            variants.size() >= max_variants) {
            return;
        }
        std::vector<PermanentId> signature;
        for (const PaymentTap& tap : *candidate) {
            signature.push_back(tap.permanent);
        }
        std::sort(signature.begin(), signature.end());
        if (std::find(seen.begin(), seen.end(), signature) !=
            seen.end()) {
            return;
        }
        seen.push_back(std::move(signature));
        variants.push_back(std::move(*candidate));
    };
    // Both directions of the ability-land decision reach the search:
    // a plan that tapped one offers the avoiding cover, and a plan
    // that spared one offers the cover spending it.
    bool has_untapped_ability = false;
    for (const auto& land : player.lands) {
        has_untapped_ability =
            has_untapped_ability ||
            (!land.tapped && is_ability_land(land_face(land)));
    }
    if (default_taps_ability) {
        emit(cover(kAvoidAbility, 0, CardId::Forest));
    } else if (has_untapped_ability) {
        emit(cover(kSpendAbility, 0, CardId::Forest));
    }
    // Dual-choice branching: a colored need payable by two or more
    // distinct dual types gets one variant per alternative type.
    for (int face = 1; face <= 5; ++face) {
        if (cost.*kFaces[static_cast<std::size_t>(face)] <= 0) {
            continue;
        }
        std::vector<CardId> dual_types;
        for (const auto& land : player.lands) {
            if (!land.tapped && is_dual(land_face(land)) &&
                land_provides(land_face(land),
                              kFaces[static_cast<std::size_t>(
                                  face)]) &&
                std::find(dual_types.begin(), dual_types.end(),
                          land_face(land)) == dual_types.end()) {
                dual_types.push_back(land_face(land));
            }
        }
        if (dual_types.size() < 2) {
            continue;
        }
        for (const CardId dual : dual_types) {
            emit(cover(kLikePlanner, face, dual));
        }
    }
    return variants;
}

bool can_pay(const PlayerState& player, const ManaCost& cost) {
    return plan_mana_payment(player, cost).possible;
}

bool pay_mana(PlayerState& player, const ManaCost& cost) {
    const ManaPaymentPlan plan = plan_mana_payment(player, cost);
    if (!plan.possible) {
        return false;
    }
    for (std::size_t index = 0; index < player.lands.size(); ++index) {
        if (plan.tap_lands[index]) {
            player.lands[index].tapped = true;
            if (land_face(player.lands[index]) ==
                CardId::CityOfBrass) {
                player.life -= 1;
            }
        }
    }
    for (std::size_t index = 0;
         index < player.artifacts.size(); ++index) {
        if (plan.tap_artifacts[index]) {
            player.artifacts[index].tapped = true;
        }
    }
    for (std::size_t index = 0;
         index < player.creatures.size(); ++index) {
        if (plan.tap_creatures[index]) {
            player.creatures[index].tapped = true;
        }
    }
    // Sacrificed Black Lotuses leave the battlefield for the graveyard.
    for (std::size_t index = player.artifacts.size(); index-- > 0;) {
        if (plan.sacrifice_artifacts[index]) {
            player.graveyard.push_back(player.artifacts[index].card);
            player.artifacts.erase(
                player.artifacts.begin() +
                static_cast<std::ptrdiff_t>(index));
        }
    }
    player.life -= plan.channel_life;
    player.mana_pool = plan.remaining_pool;
    return true;
}

int maximum_available_mana(const PlayerState& player) {
    int total = mana_total(player.mana_pool);
    if (player.channel_active) {
        total += std::max(0, player.life - 1);
    }
    for (const auto& creature : player.creatures) {
        if (creature.card == CardId::LlanowarElves &&
            !creature.tapped && !creature.summoning_sick) {
            total += 1;
        }
    }
    for (const auto& land : player.lands) {
        if (!land.tapped) {
            total += mana_total(land_mana(land_face(land)));
        }
    }
    for (const auto& artifact : player.artifacts) {
        if (!artifact.tapped) {
            total += mana_total(
                artifact_mana(artifact_face(artifact)));
            if (artifact_face(artifact) == CardId::BlackLotus) {
                total += 3;
            }
        }
    }
    return total;
}

namespace {

CreaturePermanent* find_creature(PlayerState& player, PermanentId id) {
    const auto position = std::find_if(
        player.creatures.begin(), player.creatures.end(),
        [id](const CreaturePermanent& creature) { return creature.id == id; });
    return position == player.creatures.end() ? nullptr : &*position;
}

const CreaturePermanent* find_creature(
    const PlayerState& player, PermanentId id) {
    const auto position = std::find_if(
        player.creatures.begin(), player.creatures.end(),
        [id](const CreaturePermanent& creature) {
            return creature.id == id;
        });
    return position == player.creatures.end() ? nullptr : &*position;
}

int creature_power(const CreaturePermanent& creature) {
    return card_definition(creature_face(creature)).power +
           creature.temporary_power_bonus + creature.plus_counters +
           creature.crusade_bonus;
}

int creature_toughness(const CreaturePermanent& creature) {
    return card_definition(creature_face(creature)).toughness +
           creature.temporary_toughness_bonus +
           creature.plus_counters + creature.crusade_bonus;
}

// Crusade is the pool's only global static pump: every white creature
// (printed cost includes white) gets +1/+1 per Crusade in play, either
// player's. Recomputed after every resolution so enter/leave events
// need no individual triggers.
void refresh_crusade_buffs(GameState& state) {
    int crusades = 0;
    for (const auto& player : state.players) {
        for (const CardId enchantment : player.enchantments) {
            crusades += enchantment == CardId::Crusade ? 1 : 0;
        }
    }
    for (auto& player : state.players) {
        bool controls_swamp = false;
        for (const auto& land : player.lands) {
            controls_swamp =
                controls_swamp || land_face(land) == CardId::Swamp;
        }
        for (auto& creature : player.creatures) {
            int bonus =
                card_definition(creature_face(creature))
                            .cost.white > 0
                    ? crusades
                    : 0;
            // Sedge Troll: +1/+1 while its controller has a Swamp.
            if (creature_face(creature) == CardId::SedgeTroll &&
                controls_swamp) {
                bonus += 1;
            }
            creature.crusade_bonus = bonus;
        }
    }
}

ArtifactPermanent* find_artifact(PlayerState& player, PermanentId id) {
    const auto position = std::find_if(
        player.artifacts.begin(), player.artifacts.end(),
        [id](const ArtifactPermanent& artifact) {
            return artifact.id == id;
        });
    return position == player.artifacts.end() ? nullptr : &*position;
}

bool moat_on_battlefield(const GameState& state) {
    return std::any_of(
        state.players.begin(), state.players.end(),
        [](const PlayerState& player) {
            return std::find(player.enchantments.begin(),
                             player.enchantments.end(),
                             CardId::Moat) != player.enchantments.end();
        });
}

bool can_attack_through_moat(const GameState& state,
                             const CreaturePermanent& creature) {
    return !moat_on_battlefield(state) ||
           card_definition(creature_face(creature)).flying;
}

bool can_block_creature(const CreaturePermanent& attacker,
                        const CreaturePermanent& blocker) {
    const auto& attacker_definition =
        card_definition(attacker.card);
    const auto& blocker_definition =
        card_definition(blocker.card);
    if (attacker_definition.flying && !blocker_definition.flying) {
        return false;
    }
    return blocker_definition.cannot_block_power_at_least == 0 ||
           creature_power(attacker) <
               blocker_definition.cannot_block_power_at_least;
}

bool handcrafted_favorable_attack(
    const CreaturePermanent& attacker,
    const PlayerState& defending_state) {
    bool has_legal_blocker = false;
    for (const auto& blocker : defending_state.creatures) {
        if (blocker.tapped ||
            !can_block_creature(attacker, blocker)) {
            continue;
        }
        has_legal_blocker = true;
        if (creature_power(attacker) >=
            creature_toughness(blocker)) {
            return true;
        }
    }
    return !has_legal_blocker;
}

std::vector<PermanentId> legal_attackers_for_blocker(
    const GameState& state, std::size_t attacking_player,
    PermanentId blocker_id,
    const std::vector<PermanentId>& attackers) {
    const std::size_t defending_player = 1 - attacking_player;
    const auto* blocker = find_creature(
        state.players[defending_player], blocker_id);
    if (blocker == nullptr) {
        throw std::logic_error(
            "block decision references a missing blocker");
    }
    std::vector<PermanentId> legal_attackers;
    for (const PermanentId attacker_id : attackers) {
        const auto* attacker = find_creature(
            state.players[attacking_player], attacker_id);
        if (attacker == nullptr) {
            throw std::logic_error(
                "block decision references a missing attacker");
        }
        if (can_block_creature(*attacker, *blocker)) {
            legal_attackers.push_back(attacker_id);
        }
    }
    return legal_attackers;
}

void remove_dead_creatures(PlayerState& player) {
    auto creature = player.creatures.begin();
    while (creature != player.creatures.end()) {
        if (creature->damage >= creature_toughness(*creature)) {
            // Su-Chi's death rebate: {4} into its controller's pool
            // (house rule: no mana burn, so the mana simply banks).
            // Exile (Disintegrate lethal) is not a death.
            if (creature_face(*creature) == CardId::SuChi &&
                !creature->exile_on_death_this_turn) {
                player.mana_pool.generic += 4;
            }
            auto& destination =
                creature->exile_on_death_this_turn
                    ? player.exile
                    : player.graveyard;
            destination.push_back(creature->card);
            creature = player.creatures.erase(creature);
        } else {
            ++creature;
        }
    }
}

bool contains_action(const std::vector<PriorityAction>& actions,
                     const PriorityAction& wanted) {
    return std::find(actions.begin(), actions.end(), wanted) != actions.end();
}

std::size_t opponent_of(std::size_t player) {
    return 1 - player;
}

double handcrafted_card_value(CardId card) {
    switch (card) {
    case CardId::SuChi:
        return 1200.0;
    case CardId::SageOfLatNam:
        return 700.0;
    case CardId::Triskelion:
        return 1500.0;
    case CardId::ManaDrain:
        return 1100.0;
    case CardId::Armageddon:
        return 900.0;
    case CardId::DemonicTutor:
        return 1300.0;
    case CardId::MindTwist:
        return 1000.0;
    case CardId::Recall:
        return 650.0;
    case CardId::CopyArtifact:
        return 800.0;
    case CardId::FellwarStone:
        return 500.0;
    case CardId::MoxEmerald:
    case CardId::MoxJet:
        return 1600.0;
    case CardId::CityOfBrass:
        return 200.0;
    case CardId::UndergroundSea:
    case CardId::Badlands:
        return 150.0;
    case CardId::Timetwister:
        return 1200.0;
    case CardId::BenalishHero:
        return 300.0;
    case CardId::MesaPegasus:
        return 450.0;
    case CardId::ThunderSpirit:
        return 900.0;
    case CardId::WhiteKnight:
        return 700.0;
    case CardId::ChaosOrb:
        return 1400.0;
    case CardId::JalumTome:
        return 600.0;
    case CardId::Crusade:
        return 900.0;
    case CardId::JuzamDjinn:
        return 1500.0;
    case CardId::SedgeTroll:
        return 750.0;
    case CardId::HypnoticSpecter:
        return 1300.0;
    case CardId::DarkRitual:
        return 500.0;
    case CardId::Shatter:
        return 650.0;
    case CardId::Swamp:
        return 100.0;
    case CardId::Forest:
    case CardId::Mountain:
    case CardId::Island:
    case CardId::Plains:
        return 100.0;
    case CardId::GrizzlyBears:
        return 400.0;
    case CardId::IronrootTreefolk:
        return 700.0;
    case CardId::FireElemental:
    case CardId::WaterElemental:
        return 900.0;
    case CardId::LightningBolt:
        return 800.0;
    case CardId::Counterspell:
        return 1'000.0;
    case CardId::Tsunami:
        return 700.0;
    case CardId::Millstone:
        return 1'100.0;
    case CardId::Moat:
        return 1'300.0;
    case CardId::FlyingMen:
        return 350.0;
    case CardId::IronclawOrcs:
    case CardId::GrayOgre:
        return 400.0;
    case CardId::HillGiant:
        return 550.0;
    case CardId::Disintegrate:
        return 900.0;
    case CardId::GiantGrowth:
        return 650.0;
    case CardId::MoxSapphire:
        return 1'600.0;
    case CardId::SolRing:
        return 1'500.0;
    case CardId::AncestralRecall:
        return 1'800.0;
    case CardId::TimeWalk:
        return 1'700.0;
    case CardId::Braingeyser:
        return 1'200.0;
    case CardId::ForceSpike:
        return 750.0;
    case CardId::AirElemental:
        return 1'050.0;
    case CardId::BlackLotus:
        return 1'650.0;
    case CardId::Channel:
        return 900.0;
    case CardId::LlanowarElves:
        return 750.0;
    case CardId::MossBeast:
        return 950.0;
    case CardId::ForestColossus:
        return 1'500.0;
    case CardId::SavannahLions:
        return 500.0;
    case CardId::SerendibEfreet:
        return 950.0;
    case CardId::SerraAngel:
        return 1'400.0;
    case CardId::BlackVise:
        return 850.0;
    case CardId::MoxPearl:
    case CardId::MoxRuby:
        return 1'600.0;
    case CardId::Disenchant:
        return 600.0;
    case CardId::PsionicBlast:
        return 850.0;
    case CardId::SwordsToPlowshares:
        return 900.0;
    case CardId::Plateau:
    case CardId::Tundra:
    case CardId::VolcanicIsland:
        return 150.0;
    case CardId::WheelOfFortune:
        return 1'200.0;
    case CardId::LibraryOfAlexandria:
        return 400.0;
    case CardId::MishrasFactory:
        return 300.0;
    case CardId::StripMine:
        return 200.0;
    }
    return 0.0;
}

} // namespace

const CardDefinition& card_definition(CardId card) {
    const auto index = static_cast<std::size_t>(card);
    if (index >= kCardDefinitions.size()) {
        throw std::out_of_range("unknown card ID");
    }
    return kCardDefinitions[index];
}

std::vector<CardId> green_deck() {
    std::vector<CardId> deck(16, CardId::Forest);
    deck.insert(deck.end(), 4, CardId::LlanowarElves);
    deck.insert(deck.end(), 6, CardId::GrizzlyBears);
    deck.insert(deck.end(), 2, CardId::IronrootTreefolk);
    deck.insert(deck.end(), 4, CardId::MossBeast);
    deck.insert(deck.end(), 4, CardId::ForestColossus);
    deck.insert(deck.end(), 4, CardId::GiantGrowth);
    return deck;
}

std::vector<CardId> red_deck() {
    std::vector<CardId> deck(15, CardId::Mountain);
    deck.insert(deck.end(), 9, CardId::LightningBolt);
    deck.insert(deck.end(), 7, CardId::IronclawOrcs);
    deck.insert(deck.end(), 4, CardId::GrayOgre);
    deck.insert(deck.end(), 3, CardId::HillGiant);
    deck.insert(deck.end(), 2, CardId::FireElemental);
    return deck;
}

std::vector<CardId> lotus_combo_deck() {
    std::vector<CardId> deck;
    deck.insert(deck.end(), 10, CardId::Disintegrate);
    deck.insert(deck.end(), 10, CardId::Channel);
    deck.insert(deck.end(), 20, CardId::BlackLotus);
    return deck;
}

std::vector<CardId> burn_deck() {
    std::vector<CardId> deck(12, CardId::Mountain);
    deck.insert(deck.end(), 18, CardId::LightningBolt);
    return deck;
}

std::vector<CardId> uwr_deck() {
    std::vector<CardId> deck;
    deck.insert(deck.end(), 4, CardId::SavannahLions);
    deck.insert(deck.end(), 4, CardId::SerendibEfreet);
    deck.insert(deck.end(), 2, CardId::SerraAngel);
    deck.insert(deck.end(), 4, CardId::BlackVise);
    deck.push_back(CardId::MoxPearl);
    deck.push_back(CardId::MoxRuby);
    deck.push_back(CardId::MoxSapphire);
    deck.push_back(CardId::SolRing);
    deck.push_back(CardId::AncestralRecall);
    deck.insert(deck.end(), 4, CardId::Disenchant);
    deck.insert(deck.end(), 8, CardId::LightningBolt);
    deck.insert(deck.end(), 4, CardId::PsionicBlast);
    deck.insert(deck.end(), 3, CardId::SwordsToPlowshares);
    deck.push_back(CardId::TimeWalk);
    deck.push_back(CardId::WheelOfFortune);
    deck.push_back(CardId::Timetwister);
    deck.push_back(CardId::LibraryOfAlexandria);
    deck.insert(deck.end(), 4, CardId::MishrasFactory);
    deck.push_back(CardId::Plains);
    deck.insert(deck.end(), 4, CardId::Plateau);
    deck.push_back(CardId::StripMine);
    deck.insert(deck.end(), 4, CardId::Tundra);
    deck.insert(deck.end(), 4, CardId::VolcanicIsland);
    return deck;
}

std::vector<CardId> blue_deck() {
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

std::vector<CardId> white_control_deck() {
    std::vector<CardId> deck(22, CardId::Plains);
    deck.insert(deck.end(), 3, CardId::Millstone);
    deck.insert(deck.end(), 15, CardId::Moat);
    return deck;
}

std::vector<CardId> ru_aggro_deck() {
    std::vector<CardId> deck(13, CardId::Mountain);
    deck.insert(deck.end(), 4, CardId::Island);
    deck.insert(deck.end(), 3, CardId::FlyingMen);
    deck.insert(deck.end(), 5, CardId::IronclawOrcs);
    deck.insert(deck.end(), 2, CardId::GrayOgre);
    deck.insert(deck.end(), 8, CardId::HillGiant);
    deck.insert(deck.end(), 3, CardId::LightningBolt);
    deck.insert(deck.end(), 2, CardId::Disintegrate);
    return deck;
}

std::vector<CardId> white_weenie_deck() {
    std::vector<CardId> deck;
    deck.insert(deck.end(), 4, CardId::BenalishHero);
    deck.insert(deck.end(), 3, CardId::MesaPegasus);
    deck.insert(deck.end(), 4, CardId::ThunderSpirit);
    deck.insert(deck.end(), 4, CardId::SavannahLions);
    deck.insert(deck.end(), 3, CardId::SerraAngel);
    deck.insert(deck.end(), 4, CardId::WhiteKnight);
    deck.push_back(CardId::ChaosOrb);
    deck.push_back(CardId::JalumTome);
    deck.push_back(CardId::MoxPearl);
    deck.insert(deck.end(), 4, CardId::Disenchant);
    deck.insert(deck.end(), 4, CardId::SwordsToPlowshares);
    deck.insert(deck.end(), 3, CardId::Armageddon);
    deck.insert(deck.end(), 4, CardId::Crusade);
    deck.insert(deck.end(), 14, CardId::Plains);
    deck.push_back(CardId::LibraryOfAlexandria);
    deck.insert(deck.end(), 4, CardId::MishrasFactory);
    deck.push_back(CardId::StripMine);
    return deck;
}

std::vector<CardId> br_midrange_deck() {
    std::vector<CardId> deck;
    deck.insert(deck.end(), 4, CardId::JuzamDjinn);
    deck.insert(deck.end(), 4, CardId::SedgeTroll);
    deck.insert(deck.end(), 4, CardId::HypnoticSpecter);
    deck.insert(deck.end(), 4, CardId::SuChi);
    deck.insert(deck.end(), 7, CardId::LightningBolt);
    deck.insert(deck.end(), 3, CardId::Shatter);
    deck.push_back(CardId::AncestralRecall);
    deck.push_back(CardId::DarkRitual);
    deck.push_back(CardId::DemonicTutor);
    deck.insert(deck.end(), 2, CardId::Disintegrate);
    deck.push_back(CardId::MindTwist);
    deck.push_back(CardId::TimeWalk);
    deck.push_back(CardId::WheelOfFortune);
    deck.push_back(CardId::BlackLotus);
    deck.push_back(CardId::ChaosOrb);
    deck.push_back(CardId::MoxJet);
    deck.push_back(CardId::MoxRuby);
    deck.push_back(CardId::MoxSapphire);
    deck.push_back(CardId::SolRing);
    deck.insert(deck.end(), 7, CardId::Swamp);
    deck.insert(deck.end(), 4, CardId::Badlands);
    deck.insert(deck.end(), 3, CardId::MishrasFactory);
    deck.insert(deck.end(), 2, CardId::CityOfBrass);
    deck.insert(deck.end(), 2, CardId::VolcanicIsland);
    deck.push_back(CardId::LibraryOfAlexandria);
    deck.push_back(CardId::StripMine);
    deck.push_back(CardId::UndergroundSea);
    return deck;
}

std::vector<CardId> robots_deck() {
    std::vector<CardId> deck;
    deck.insert(deck.end(), 4, CardId::SuChi);
    deck.insert(deck.end(), 3, CardId::SageOfLatNam);
    deck.insert(deck.end(), 3, CardId::Triskelion);
    deck.insert(deck.end(), 3, CardId::Counterspell);
    deck.insert(deck.end(), 4, CardId::LightningBolt);
    deck.insert(deck.end(), 2, CardId::PsionicBlast);
    deck.push_back(CardId::AncestralRecall);
    deck.push_back(CardId::ManaDrain);
    deck.push_back(CardId::Armageddon);
    deck.push_back(CardId::Braingeyser);
    deck.push_back(CardId::DemonicTutor);
    deck.push_back(CardId::Disintegrate);
    deck.push_back(CardId::MindTwist);
    deck.push_back(CardId::Recall);
    deck.push_back(CardId::TimeWalk);
    deck.push_back(CardId::WheelOfFortune);
    deck.insert(deck.end(), 4, CardId::CopyArtifact);
    deck.push_back(CardId::BlackLotus);
    deck.push_back(CardId::FellwarStone);
    deck.push_back(CardId::MoxEmerald);
    deck.push_back(CardId::MoxJet);
    deck.push_back(CardId::MoxPearl);
    deck.push_back(CardId::MoxRuby);
    deck.push_back(CardId::MoxSapphire);
    deck.push_back(CardId::SolRing);
    deck.insert(deck.end(), 4, CardId::MishrasFactory);
    deck.insert(deck.end(), 4, CardId::Tundra);
    deck.insert(deck.end(), 3, CardId::CityOfBrass);
    deck.insert(deck.end(), 3, CardId::VolcanicIsland);
    deck.insert(deck.end(), 2, CardId::UndergroundSea);
    deck.push_back(CardId::Badlands);
    deck.push_back(CardId::LibraryOfAlexandria);
    deck.push_back(CardId::StripMine);
    return deck;
}

GameState white_lock_plan_diagnostic_state() {
    GameState state;
    state.active_player = 0;
    state.starting_player = 0;
    state.turn_number = 13;
    state.next_permanent_id = 3;
    state.next_stack_object_id = 4;

    auto& white = state.players[0];
    white.library.assign(18, CardId::Plains);
    white.library.insert(
        white.library.end(), 2, CardId::Millstone);
    white.library.insert(white.library.end(), 7, CardId::Moat);
    white.hand.assign(7, CardId::Moat);
    white.lands.assign(
        4, LandPermanent{.card = CardId::Plains, .tapped = false});
    white.artifacts = {
        ArtifactPermanent{
            .id = 1,
            .card = CardId::Millstone,
            .tapped = false,
        },
    };
    white.enchantments = {CardId::Moat};
    white.land_played_this_turn = true;

    auto& red = state.players[1];
    red.library.assign(10, CardId::Mountain);
    red.library.insert(
        red.library.end(), 2, CardId::LightningBolt);
    red.library.insert(
        red.library.end(), 1, CardId::FireElemental);
    red.library.insert(
        red.library.end(), 7, CardId::IronclawOrcs);
    red.library.insert(
        red.library.end(), 4, CardId::GrayOgre);
    red.library.insert(
        red.library.end(), 3, CardId::HillGiant);
    red.hand.assign(7, CardId::LightningBolt);
    red.lands.assign(
        5, LandPermanent{.card = CardId::Mountain, .tapped = false});
    red.creatures = {
        CreaturePermanent{
            .id = 2,
            .card = CardId::FireElemental,
            .tapped = false,
            .summoning_sick = false,
            .damage = 0,
        },
    };

    return state;
}

GameState sample_determinization(
    const GameState& state,
    const std::array<std::vector<CardId>, 2>& original_decks,
    std::size_t observer, std::uint64_t seed) {
    if (observer >= state.players.size()) {
        throw std::out_of_range("observer must be player 0 or 1");
    }
    for (const auto& object : state.stack) {
        if (object.controller >= state.players.size()) {
            throw std::invalid_argument(
                "stack object has an invalid controller");
        }
    }

    GameState sampled = state;
    std::mt19937_64 random(seed);
    for (std::size_t player = 0; player < state.players.size();
         ++player) {
        CardCounts remaining = card_counts(original_decks[player]);
        subtract_public_cards(remaining, state.players[player]);
        for (const auto& object : state.stack) {
            if (object.controller == player &&
                object.kind == StackObjectKind::Spell) {
                subtract_card(remaining, object.card);
            }
        }

        if (player == observer) {
            for (const CardId card : state.players[player].hand) {
                subtract_card(remaining, card);
            }
        }

        auto hidden_cards = expand_card_counts(remaining);
        const std::size_t expected_hidden =
            player == observer
                ? state.players[player].library.size()
                : state.players[player].hand.size() +
                      state.players[player].library.size();
        if (hidden_cards.size() != expected_hidden) {
            throw std::invalid_argument(
                "hidden zone sizes are inconsistent with original deck");
        }
        std::shuffle(hidden_cards.begin(), hidden_cards.end(), random);

        if (player == observer) {
            sampled.players[player].hand = state.players[player].hand;
            sampled.players[player].library = std::move(hidden_cards);
            continue;
        }

        const std::size_t hand_size = state.players[player].hand.size();
        const auto hand_end = hidden_cards.begin() +
                              static_cast<std::ptrdiff_t>(hand_size);
        sampled.players[player].hand.assign(hidden_cards.begin(),
                                            hand_end);
        sampled.players[player].library.assign(hand_end,
                                               hidden_cards.end());
    }

    for (std::size_t player = 0; player < state.players.size();
         ++player) {
        if (physical_card_counts(sampled, player) !=
            card_counts(original_decks[player])) {
            throw std::logic_error(
                "determinization failed physical card conservation");
        }
    }
    return sampled;
}

Target Target::player_target(std::size_t player_index) {
    return {.player = player_index, .creature = std::nullopt};
}

Target Target::creature_target(std::size_t controller,
                               PermanentId creature_id) {
    return {.player = controller, .creature = creature_id};
}

PriorityAction PriorityAction::pass() {
    return {};
}

PriorityAction PriorityAction::play_land(CardId land) {
    return {.kind = PriorityActionKind::PlayLand, .card = land};
}

PriorityAction PriorityAction::cast_creature(CardId creature) {
    return {.kind = PriorityActionKind::CastCreature, .card = creature};
}

PriorityAction PriorityAction::cast_sorcery(CardId sorcery) {
    return {.kind = PriorityActionKind::CastSorcery, .card = sorcery};
}

PriorityAction PriorityAction::cast_artifact(CardId artifact) {
    return {.kind = PriorityActionKind::CastArtifact, .card = artifact};
}

PriorityAction
PriorityAction::cast_enchantment(CardId enchantment) {
    return {
        .kind = PriorityActionKind::CastEnchantment,
        .card = enchantment,
    };
}

PriorityAction PriorityAction::cast_lightning_bolt(Target bolt_target) {
    return {.kind = PriorityActionKind::CastLightningBolt,
            .card = CardId::LightningBolt,
            .target = bolt_target};
}

PriorityAction PriorityAction::cast_disintegrate(
    int x_value, Target disintegrate_target) {
    return {
        .kind = PriorityActionKind::CastDisintegrate,
        .card = CardId::Disintegrate,
        .target = disintegrate_target,
        .x_value = x_value,
    };
}

PriorityAction PriorityAction::cast_giant_growth(
    Target growth_target) {
    return {
        .kind = PriorityActionKind::CastGiantGrowth,
        .card = CardId::GiantGrowth,
        .target = growth_target,
    };
}

PriorityAction
PriorityAction::cast_counterspell(StackObjectId target_spell) {
    return {
        .kind = PriorityActionKind::CastCounterspell,
        .card = CardId::Counterspell,
        .target = std::nullopt,
        .spell_target = target_spell,
    };
}

PriorityAction PriorityAction::cast_ancestral_recall(
    Target draw_target) {
    return {
        .kind = PriorityActionKind::CastAncestralRecall,
        .card = CardId::AncestralRecall,
        .target = draw_target,
    };
}

PriorityAction PriorityAction::cast_braingeyser(
    int x_value, Target draw_target) {
    return {
        .kind = PriorityActionKind::CastBraingeyser,
        .card = CardId::Braingeyser,
        .target = draw_target,
        .x_value = x_value,
    };
}

PriorityAction
PriorityAction::cast_force_spike(StackObjectId target_spell) {
    return {
        .kind = PriorityActionKind::CastForceSpike,
        .card = CardId::ForceSpike,
        .spell_target = target_spell,
    };
}

PriorityAction PriorityAction::cast_psionic_blast(Target blast_target) {
    return {.kind = PriorityActionKind::CastPsionicBlast,
            .card = CardId::PsionicBlast,
            .target = blast_target};
}

PriorityAction PriorityAction::cast_swords(Target creature_target) {
    return {.kind = PriorityActionKind::CastSwordsToPlowshares,
            .card = CardId::SwordsToPlowshares,
            .target = creature_target};
}

PriorityAction PriorityAction::cast_disenchant_artifact(
    std::size_t owner, PermanentId artifact) {
    return {.kind = PriorityActionKind::CastDisenchant,
            .card = CardId::Disenchant,
            .target = Target::creature_target(owner, artifact)};
}

PriorityAction PriorityAction::cast_disenchant_enchantment(
    std::size_t owner, int index) {
    return {.kind = PriorityActionKind::CastDisenchant,
            .card = CardId::Disenchant,
            .target = Target::player_target(owner),
            .x_value = index};
}

PriorityAction PriorityAction::activate_library(PermanentId library) {
    return {.kind = PriorityActionKind::ActivateLibrary,
            .card = CardId::LibraryOfAlexandria,
            .source_permanent = library};
}

PriorityAction PriorityAction::animate_factory(PermanentId factory) {
    return {.kind = PriorityActionKind::ActivateMishrasFactory,
            .card = CardId::MishrasFactory,
            .source_permanent = factory};
}

PriorityAction PriorityAction::activate_strip_mine(
    PermanentId strip_mine, std::size_t owner, PermanentId land) {
    return {.kind = PriorityActionKind::ActivateStripMine,
            .card = CardId::StripMine,
            .target = Target::creature_target(owner, land),
            .source_permanent = strip_mine};
}

PriorityAction PriorityAction::cast_mana_drain(
    StackObjectId target_spell) {
    return {.kind = PriorityActionKind::CastManaDrain,
            .card = CardId::ManaDrain,
            .spell_target = target_spell};
}


PriorityAction PriorityAction::cast_mind_twist(
    int x_value, std::size_t target_player) {
    return {.kind = PriorityActionKind::CastMindTwist,
            .card = CardId::MindTwist,
            .target = Target::player_target(target_player),
            .x_value = x_value};
}

PriorityAction PriorityAction::cast_recall(int x_value) {
    return {.kind = PriorityActionKind::CastRecall,
            .card = CardId::Recall,
            .x_value = x_value};
}

PriorityAction PriorityAction::cast_demonic_tutor(CardId chosen) {
    PriorityAction action{
        .kind = PriorityActionKind::CastDemonicTutor,
        .card = CardId::DemonicTutor};
    action.chosen_card = chosen;
    return action;
}

PriorityAction PriorityAction::cast_copy_artifact(
    std::size_t owner, PermanentId artifact) {
    return {.kind = PriorityActionKind::CastCopyArtifact,
            .card = CardId::CopyArtifact,
            .target = Target::creature_target(owner, artifact)};
}

PriorityAction PriorityAction::activate_sage(PermanentId sage,
                                             PermanentId artifact) {
    PriorityAction action{
        .kind = PriorityActionKind::ActivateSage,
        .card = CardId::SageOfLatNam,
        .target = Target::creature_target(0, artifact)};
    action.source_permanent = sage;
    return action;
}

PriorityAction PriorityAction::activate_triskelion(
    PermanentId triskelion, Target target) {
    PriorityAction action{
        .kind = PriorityActionKind::ActivateTriskelion,
        .card = CardId::Triskelion,
        .target = target};
    action.source_permanent = triskelion;
    return action;
}

PriorityAction PriorityAction::activate_chaos_orb(PermanentId orb,
                                                  Target target) {
    PriorityAction action{
        .kind = PriorityActionKind::ActivateChaosOrb,
        .card = CardId::ChaosOrb,
        .target = target};
    action.source_permanent = orb;
    return action;
}

PriorityAction PriorityAction::activate_chaos_orb_enchantment(
    PermanentId orb, std::size_t owner, int index) {
    PriorityAction action{
        .kind = PriorityActionKind::ActivateChaosOrb,
        .card = CardId::ChaosOrb,
        .target = Target::player_target(owner)};
    action.source_permanent = orb;
    action.x_value = index;
    return action;
}

PriorityAction PriorityAction::activate_jalum_tome(PermanentId tome) {
    PriorityAction action{
        .kind = PriorityActionKind::ActivateJalumTome,
        .card = CardId::JalumTome};
    action.source_permanent = tome;
    return action;
}

PriorityAction PriorityAction::cast_dark_ritual() {
    return {.kind = PriorityActionKind::CastDarkRitual,
            .card = CardId::DarkRitual};
}


PriorityAction PriorityAction::cast_shatter(std::size_t owner,
                                            PermanentId artifact) {
    return {.kind = PriorityActionKind::CastShatter,
            .card = CardId::Shatter,
            .target = Target::creature_target(owner, artifact)};
}

PriorityAction PriorityAction::tap_land_for_mana(PermanentId land,
                                                 int color_index) {
    PriorityAction action{
        .kind = PriorityActionKind::TapLandForMana};
    action.source_permanent = land;
    action.x_value = color_index;
    return action;
}

PriorityAction
PriorityAction::activate_millstone(PermanentId millstone,
                                   Target mill_target) {
    return {
        .kind = PriorityActionKind::ActivateMillstone,
        .card = CardId::Millstone,
        .target = mill_target,
        .spell_target = std::nullopt,
        .source_permanent = millstone,
    };
}

std::vector<PriorityAction>
legal_priority_actions(const GameState& state, std::size_t player,
                       bool sorcery_actions) {
    if (player >= state.players.size()) {
        return {};
    }

    const auto& player_state = state.players[player];
    std::vector<PriorityAction> actions = {PriorityAction::pass()};
    const bool has_sorcery_timing =
        sorcery_actions && player == state.active_player &&
        state.stack.empty();

    // The player's mana does not change while enumerating, so identical
    // costs (eight Bolts, four Psionics...) plan payment exactly once.
    struct CostMemo {
        ManaCost cost;
        bool payable;
    };
    std::array<CostMemo, 12> cost_memo{};
    std::size_t cost_memo_count = 0;
    const auto can_pay_memo = [&](const ManaCost& cost) {
        for (std::size_t entry = 0; entry < cost_memo_count; ++entry) {
            const ManaCost& seen = cost_memo[entry].cost;
            if (seen.generic == cost.generic &&
                seen.green == cost.green && seen.red == cost.red &&
                seen.blue == cost.blue && seen.white == cost.white &&
                seen.black == cost.black) {
                return cost_memo[entry].payable;
            }
        }
        const bool payable = can_pay(player_state, cost);
        if (cost_memo_count < cost_memo.size()) {
            cost_memo[cost_memo_count++] = {cost, payable};
        }
        return payable;
    };

    if (has_sorcery_timing && !player_state.land_played_this_turn) {
        std::vector<CardId> seen_lands;
        for (const CardId card : player_state.hand) {
            if (is_land(card) &&
                std::find(seen_lands.begin(), seen_lands.end(), card) ==
                    seen_lands.end()) {
                seen_lands.push_back(card);
                actions.push_back(PriorityAction::play_land(card));
            }
        }
    }

    if (has_sorcery_timing) {
        for (const CardId creature : kCreatureCards) {
            const auto& definition = card_definition(creature);
            if (has_card(player_state.hand, creature) &&
                can_pay_memo(definition.cost)) {
                actions.push_back(
                    PriorityAction::cast_creature(creature));
            }
        }
        for (const CardId sorcery : kSorceryCards) {
            const auto& definition = card_definition(sorcery);
            if (has_card(player_state.hand, sorcery) &&
                can_pay_memo(definition.cost)) {
                actions.push_back(
                    PriorityAction::cast_sorcery(sorcery));
            }
        }
        for (const CardId artifact : kArtifactCards) {
            const auto& definition = card_definition(artifact);
            if (has_card(player_state.hand, artifact) &&
                can_pay_memo(definition.cost)) {
                actions.push_back(
                    PriorityAction::cast_artifact(artifact));
            }
        }
        for (const CardId enchantment : kEnchantmentCards) {
            const auto& definition = card_definition(enchantment);
            if (has_card(player_state.hand, enchantment) &&
                can_pay_memo(definition.cost)) {
                actions.push_back(
                    PriorityAction::cast_enchantment(enchantment));
            }
        }
        if (has_card(player_state.hand, CardId::Disintegrate)) {
            for (int x_value = 0;
                 x_value <= maximum_available_mana(player_state) &&
                 can_pay(player_state,
                         disintegrate_cost(x_value));
                 ++x_value) {
                for (std::size_t controller = 0;
                     controller < state.players.size();
                     ++controller) {
                    actions.push_back(
                        PriorityAction::cast_disintegrate(
                            x_value,
                            Target::player_target(controller)));
                    for (const auto& creature :
                         state.players[controller].creatures) {
                        actions.push_back(
                            PriorityAction::cast_disintegrate(
                                x_value,
                                Target::creature_target(
                                    controller, creature.id)));
                    }
                }
            }
        }
        if (has_card(player_state.hand, CardId::MindTwist)) {
            for (int x_value = 1;
                 x_value <= maximum_available_mana(player_state) &&
                 can_pay(player_state, mind_twist_cost(x_value));
                 ++x_value) {
                actions.push_back(PriorityAction::cast_mind_twist(
                    x_value, opponent_of(player)));
            }
        }
        if (has_card(player_state.hand, CardId::Recall)) {
            const int recall_limit = static_cast<int>(
                std::min(player_state.hand.size() - 1,
                         player_state.graveyard.size()));
            for (int x_value = 1;
                 x_value <= recall_limit &&
                 x_value <= maximum_available_mana(player_state) &&
                 can_pay(player_state, recall_cost(x_value));
                 ++x_value) {
                actions.push_back(
                    PriorityAction::cast_recall(x_value));
            }
        }
        if (has_card(player_state.hand, CardId::DemonicTutor) &&
            can_pay_memo(
                card_definition(CardId::DemonicTutor).cost)) {
            std::vector<CardId> seen;
            for (const CardId card : player_state.library) {
                if (std::find(seen.begin(), seen.end(), card) ==
                    seen.end()) {
                    seen.push_back(card);
                    actions.push_back(
                        PriorityAction::cast_demonic_tutor(card));
                }
            }
        }
        if (has_card(player_state.hand, CardId::CopyArtifact) &&
            can_pay_memo(
                card_definition(CardId::CopyArtifact).cost)) {
            for (std::size_t owner = 0;
                 owner < state.players.size(); ++owner) {
                for (const auto& artifact :
                     state.players[owner].artifacts) {
                    actions.push_back(
                        PriorityAction::cast_copy_artifact(
                            owner, artifact.id));
                }
                // Artifact creatures (Su-Chi, Triskelion, an
                // animated Factory) are legal subjects too.
                for (const auto& creature :
                     state.players[owner].creatures) {
                    if (is_artifact_creature(
                            creature_face(creature))) {
                        actions.push_back(
                            PriorityAction::cast_copy_artifact(
                                owner, creature.id));
                    }
                }
            }
        }
        if (has_card(player_state.hand, CardId::Braingeyser)) {
            for (int x_value = 0;
                 x_value <= maximum_available_mana(player_state) &&
                 can_pay(player_state,
                         braingeyser_cost(x_value));
                 ++x_value) {
                for (std::size_t target = 0;
                     target < state.players.size(); ++target) {
                    actions.push_back(
                        PriorityAction::cast_braingeyser(
                            x_value,
                            Target::player_target(target)));
                }
            }
        }
    }

    const auto& ancestral =
        card_definition(CardId::AncestralRecall);
    if (has_card(player_state.hand, CardId::AncestralRecall) &&
        can_pay_memo(ancestral.cost)) {
        for (std::size_t target = 0;
             target < state.players.size(); ++target) {
            actions.push_back(
                PriorityAction::cast_ancestral_recall(
                    Target::player_target(target)));
        }
    }

    const auto& bolt = card_definition(CardId::LightningBolt);
    if (has_card(player_state.hand, CardId::LightningBolt) &&
        can_pay_memo(bolt.cost)) {
        for (std::size_t controller = 0; controller < state.players.size();
             ++controller) {
            actions.push_back(PriorityAction::cast_lightning_bolt(
                Target::player_target(controller)));
            for (const auto& creature : state.players[controller].creatures) {
                actions.push_back(PriorityAction::cast_lightning_bolt(
                    Target::creature_target(controller, creature.id)));
            }
        }
    }

    const auto& psionic = card_definition(CardId::PsionicBlast);
    if (has_card(player_state.hand, CardId::PsionicBlast) &&
        can_pay_memo(psionic.cost)) {
        for (std::size_t controller = 0;
             controller < state.players.size(); ++controller) {
            actions.push_back(PriorityAction::cast_psionic_blast(
                Target::player_target(controller)));
            for (const auto& creature :
                 state.players[controller].creatures) {
                actions.push_back(PriorityAction::cast_psionic_blast(
                    Target::creature_target(controller, creature.id)));
            }
        }
    }

    const auto& swords = card_definition(CardId::SwordsToPlowshares);
    if (has_card(player_state.hand, CardId::SwordsToPlowshares) &&
        can_pay_memo(swords.cost)) {
        for (std::size_t controller = 0;
             controller < state.players.size(); ++controller) {
            for (const auto& creature :
                 state.players[controller].creatures) {
                actions.push_back(PriorityAction::cast_swords(
                    Target::creature_target(controller, creature.id)));
            }
        }
    }

    const auto& disenchant = card_definition(CardId::Disenchant);
    if (has_card(player_state.hand, CardId::Disenchant) &&
        can_pay_memo(disenchant.cost)) {
        for (std::size_t owner = 0; owner < state.players.size();
             ++owner) {
            for (const auto& artifact :
                 state.players[owner].artifacts) {
                actions.push_back(
                    PriorityAction::cast_disenchant_artifact(
                        owner, artifact.id));
            }
            for (std::size_t index = 0;
                 index < state.players[owner].enchantments.size();
                 ++index) {
                actions.push_back(
                    PriorityAction::cast_disenchant_enchantment(
                        owner, static_cast<int>(index)));
            }
        }
    }

    if (has_card(player_state.hand, CardId::Shatter) &&
        can_pay_memo(card_definition(CardId::Shatter).cost)) {
        for (std::size_t owner = 0; owner < state.players.size();
             ++owner) {
            for (const auto& artifact :
                 state.players[owner].artifacts) {
                actions.push_back(PriorityAction::cast_shatter(
                    owner, artifact.id));
            }
        }
    }

    if (has_card(player_state.hand, CardId::DarkRitual) &&
        can_pay_memo(card_definition(CardId::DarkRitual).cost)) {
        actions.push_back(PriorityAction::cast_dark_ritual());
    }

    const auto& giant_growth =
        card_definition(CardId::GiantGrowth);
    if (has_card(player_state.hand, CardId::GiantGrowth) &&
        can_pay_memo(giant_growth.cost)) {
        for (std::size_t controller = 0;
             controller < state.players.size(); ++controller) {
            for (const auto& creature :
                 state.players[controller].creatures) {
                actions.push_back(
                    PriorityAction::cast_giant_growth(
                        Target::creature_target(
                            controller, creature.id)));
            }
        }
    }

    const auto& counterspell = card_definition(CardId::Counterspell);
    if (has_card(player_state.hand, CardId::Counterspell) &&
        can_pay_memo(counterspell.cost)) {
        for (const auto& spell : state.stack) {
            if (spell.kind == StackObjectKind::Spell) {
                actions.push_back(
                    PriorityAction::cast_counterspell(spell.id));
            }
        }
    }

    const auto& mana_drain = card_definition(CardId::ManaDrain);
    if (has_card(player_state.hand, CardId::ManaDrain) &&
        can_pay_memo(mana_drain.cost)) {
        for (const auto& spell : state.stack) {
            if (spell.kind == StackObjectKind::Spell) {
                actions.push_back(
                    PriorityAction::cast_mana_drain(spell.id));
            }
        }
    }

    const auto& force_spike =
        card_definition(CardId::ForceSpike);
    if (has_card(player_state.hand, CardId::ForceSpike) &&
        can_pay_memo(force_spike.cost)) {
        for (const auto& spell : state.stack) {
            if (spell.kind == StackObjectKind::Spell) {
                actions.push_back(
                    PriorityAction::cast_force_spike(spell.id));
            }
        }
    }

    if (player_state.hand.size() == 7) {
        for (const auto& land : player_state.lands) {
            if (land.card == CardId::LibraryOfAlexandria &&
                !land.tapped) {
                actions.push_back(
                    PriorityAction::activate_library(land.id));
            }
        }
    }
    for (const auto& land : player_state.lands) {
        if (land_face(land) != CardId::MishrasFactory) {
            continue;
        }
        // The factory cannot pay for its own animation (the apply
        // shields it), so an untapped factory must find one generic
        // among the OTHER sources: two total, one of which is itself.
        const ManaCost animation_cost =
            land.tapped ? ManaCost{.generic = 1}
                        : ManaCost{.generic = 2};
        if (can_pay_memo(animation_cost)) {
            actions.push_back(
                PriorityAction::animate_factory(land.id));
        }
    }
    for (const auto& land : player_state.lands) {
        if (land.card != CardId::StripMine) {
            continue;
        }
        for (std::size_t owner = 0; owner < state.players.size();
             ++owner) {
            for (const auto& target_land :
                 state.players[owner].lands) {
                if (target_land.id == land.id) {
                    continue;
                }
                actions.push_back(PriorityAction::activate_strip_mine(
                    land.id, owner, target_land.id));
            }
            // An animated land (a creature whose printed card is a
            // land, e.g. a fighting Factory) is still a land and can
            // be stripped.
            for (const auto& creature :
                 state.players[owner].creatures) {
                if (card_definition(creature_face(creature)).type ==
                    CardType::Land) {
                    actions.push_back(
                        PriorityAction::activate_strip_mine(
                            land.id, owner, creature.id));
                }
            }
        }
    }
    for (const auto& sage : player_state.creatures) {
        if (sage.card != CardId::SageOfLatNam || sage.tapped ||
            sage.summoning_sick) {
            continue;
        }
        for (const auto& artifact : player_state.artifacts) {
            actions.push_back(PriorityAction::activate_sage(
                sage.id, artifact.id));
        }
    }
    for (const auto& trike : player_state.creatures) {
        if (creature_face(trike) != CardId::Triskelion ||
            trike.plus_counters <= 0) {
            continue;
        }
        for (std::size_t owner = 0; owner < state.players.size();
             ++owner) {
            actions.push_back(PriorityAction::activate_triskelion(
                trike.id, Target::player_target(owner)));
            for (const auto& creature :
                 state.players[owner].creatures) {
                actions.push_back(
                    PriorityAction::activate_triskelion(
                        trike.id, Target::creature_target(
                                      owner, creature.id)));
            }
        }
    }
    for (const auto& orb : player_state.artifacts) {
        if (artifact_face(orb) != CardId::ChaosOrb || orb.tapped) {
            continue;
        }
        for (std::size_t owner = 0; owner < state.players.size();
             ++owner) {
            const auto& side = state.players[owner];
            for (const auto& creature : side.creatures) {
                actions.push_back(PriorityAction::activate_chaos_orb(
                    orb.id,
                    Target::creature_target(owner, creature.id)));
            }
            for (const auto& land : side.lands) {
                actions.push_back(PriorityAction::activate_chaos_orb(
                    orb.id, Target::creature_target(owner, land.id)));
            }
            for (const auto& artifact : side.artifacts) {
                if (artifact.id == orb.id) {
                    continue;  // it is already being sacrificed
                }
                actions.push_back(PriorityAction::activate_chaos_orb(
                    orb.id,
                    Target::creature_target(owner, artifact.id)));
            }
            for (std::size_t index = 0;
                 index < side.enchantments.size(); ++index) {
                actions.push_back(
                    PriorityAction::activate_chaos_orb_enchantment(
                        orb.id, owner, static_cast<int>(index)));
            }
        }
    }
    if (can_pay_memo(ManaCost{.generic = 2})) {
        for (const auto& tome : player_state.artifacts) {
            if (artifact_face(tome) == CardId::JalumTome &&
                !tome.tapped && !player_state.hand.empty()) {
                actions.push_back(
                    PriorityAction::activate_jalum_tome(tome.id));
            }
        }
    }
    if (can_pay_memo(kMillstoneActivationCost)) {
        for (const auto& artifact : player_state.artifacts) {
            if (artifact_face(artifact) != CardId::Millstone ||
                artifact.tapped) {
                continue;
            }
            for (std::size_t target = 0;
                 target < state.players.size(); ++target) {
                actions.push_back(PriorityAction::activate_millstone(
                    artifact.id, Target::player_target(target)));
            }
        }
    }

    return actions;
}

bool apply_priority_action(GameState& state, std::size_t player,
                           const PriorityAction& action,
                           bool sorcery_actions) {
    // A payment-variant action floats its exact taps first, then pays
    // as the same spell with the pool consumed before anything else.
    // On any failure the state is poisoned (like every partial apply)
    // and the caller must discard it.
    if (!action.pre_taps.empty()) {
        for (const PaymentTap& tap : action.pre_taps) {
            if (!apply_priority_action(
                    state, player,
                    PriorityAction::tap_land_for_mana(tap.permanent,
                                                      tap.face),
                    sorcery_actions)) {
                return false;
            }
        }
        PriorityAction stripped = action;
        stripped.pre_taps.clear();
        return apply_priority_action(state, player, stripped,
                                     sorcery_actions);
    }
    // Human-only manual mana taps are injected by the game loop, not
    // enumerated; their apply case below fully validates them.
    if (action.kind != PriorityActionKind::TapLandForMana) {
        const auto actions =
            legal_priority_actions(state, player, sorcery_actions);
        if (!contains_action(actions, action)) {
            return false;
        }
    }

    auto& player_state = state.players[player];
    switch (action.kind) {
    case PriorityActionKind::Pass:
        return true;

    case PriorityActionKind::PlayLand:
        if (!is_land(action.card) ||
            !remove_card(player_state.hand, action.card)) {
            return false;
        }
        {
            LandPermanent played{.card = action.card,
                                 .tapped = false,
                                 .id = state.next_permanent_id++};
            played.entered_this_turn = true;
            player_state.lands.push_back(played);
        }
        player_state.land_played_this_turn = true;
        ++state.stats[player].lands_played;
        refresh_crusade_buffs(state);
        return true;

    case PriorityActionKind::CastCreature: {
        const auto& definition = card_definition(action.card);
        if (definition.type != CardType::Creature) {
            return false;
        }
        if (!pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, action.card)) {
            return false;
        }
        state.stack.push_back({
            .id = state.next_stack_object_id++,
            .card = action.card,
            .controller = player,
            .target = std::nullopt,
            .spell_target = std::nullopt,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastSorcery: {
        const auto& definition = card_definition(action.card);
        if (definition.type != CardType::Sorcery ||
            !pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, action.card)) {
            return false;
        }
        state.stack.push_back({
            .id = state.next_stack_object_id++,
            .card = action.card,
            .controller = player,
            .target = std::nullopt,
            .spell_target = std::nullopt,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastArtifact: {
        const auto& definition = card_definition(action.card);
        if (definition.type != CardType::Artifact ||
            !pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, action.card)) {
            return false;
        }
        state.stack.push_back({
            .id = state.next_stack_object_id++,
            .card = action.card,
            .controller = player,
            .target = std::nullopt,
            .spell_target = std::nullopt,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastEnchantment: {
        const auto& definition = card_definition(action.card);
        if (definition.type != CardType::Enchantment ||
            !pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, action.card)) {
            return false;
        }
        state.stack.push_back({
            .id = state.next_stack_object_id++,
            .card = action.card,
            .controller = player,
            .target = std::nullopt,
            .spell_target = std::nullopt,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastLightningBolt: {
        if (!action.target.has_value()) {
            return false;
        }
        const auto& definition = card_definition(CardId::LightningBolt);
        if (!pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, CardId::LightningBolt)) {
            return false;
        }
        state.stack.push_back({
            .id = state.next_stack_object_id++,
            .card = CardId::LightningBolt,
            .controller = player,
            .target = action.target,
            .spell_target = std::nullopt,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastDisintegrate: {
        if (!action.target.has_value() || action.x_value < 0) {
            return false;
        }
        if (!pay_mana(
                player_state,
                disintegrate_cost(action.x_value)) ||
            !remove_card(
                player_state.hand, CardId::Disintegrate)) {
            return false;
        }
        state.stack.push_back({
            .id = state.next_stack_object_id++,
            .card = CardId::Disintegrate,
            .controller = player,
            .target = action.target,
            .spell_target = std::nullopt,
            .x_value = action.x_value,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastGiantGrowth: {
        if (!action.target.has_value() ||
            !action.target->creature.has_value()) {
            return false;
        }
        const auto& definition =
            card_definition(CardId::GiantGrowth);
        if (!pay_mana(player_state, definition.cost) ||
            !remove_card(
                player_state.hand, CardId::GiantGrowth)) {
            return false;
        }
        state.stack.push_back({
            .id = state.next_stack_object_id++,
            .card = CardId::GiantGrowth,
            .controller = player,
            .target = action.target,
            .spell_target = std::nullopt,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastCounterspell: {
        const auto& definition = card_definition(CardId::Counterspell);
        if (!action.spell_target.has_value() ||
            !pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, CardId::Counterspell)) {
            return false;
        }
        state.stack.push_back({
            .id = state.next_stack_object_id++,
            .card = CardId::Counterspell,
            .controller = player,
            .target = std::nullopt,
            .spell_target = action.spell_target,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastAncestralRecall: {
        if (!action.target.has_value() ||
            action.target->creature.has_value()) {
            return false;
        }
        const auto& definition =
            card_definition(CardId::AncestralRecall);
        if (!pay_mana(player_state, definition.cost) ||
            !remove_card(
                player_state.hand, CardId::AncestralRecall)) {
            return false;
        }
        state.stack.push_back({
            .id = state.next_stack_object_id++,
            .card = CardId::AncestralRecall,
            .controller = player,
            .target = action.target,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastBraingeyser: {
        if (!action.target.has_value() ||
            action.target->creature.has_value() ||
            action.x_value < 0) {
            return false;
        }
        if (!pay_mana(
                player_state,
                braingeyser_cost(action.x_value)) ||
            !remove_card(
                player_state.hand, CardId::Braingeyser)) {
            return false;
        }
        state.stack.push_back({
            .id = state.next_stack_object_id++,
            .card = CardId::Braingeyser,
            .controller = player,
            .target = action.target,
            .x_value = action.x_value,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastForceSpike: {
        if (!action.spell_target.has_value()) {
            return false;
        }
        const auto& definition =
            card_definition(CardId::ForceSpike);
        if (!pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, CardId::ForceSpike)) {
            return false;
        }
        state.stack.push_back({
            .id = state.next_stack_object_id++,
            .card = CardId::ForceSpike,
            .controller = player,
            .spell_target = action.spell_target,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastPsionicBlast:
    case PriorityActionKind::CastSwordsToPlowshares:
    case PriorityActionKind::CastDisenchant: {
        const auto& definition = card_definition(action.card);
        if (!action.target.has_value() ||
            !pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, action.card)) {
            return false;
        }
        state.stack.push_back({
            .kind = StackObjectKind::Spell,
            .id = state.next_stack_object_id++,
            .card = action.card,
            .controller = player,
            .target = action.target,
            .spell_target = std::nullopt,
            .x_value = action.x_value,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastManaDrain: {
        const auto& definition = card_definition(CardId::ManaDrain);
        if (!action.spell_target.has_value() ||
            !pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, CardId::ManaDrain)) {
            return false;
        }
        state.stack.push_back({
            .kind = StackObjectKind::Spell,
            .id = state.next_stack_object_id++,
            .card = CardId::ManaDrain,
            .controller = player,
            .spell_target = action.spell_target,
        });
        ++state.stats[player].spells_cast;
        return true;
    }


    case PriorityActionKind::CastMindTwist: {
        if (!action.target.has_value() ||
            !pay_mana(player_state,
                      mind_twist_cost(action.x_value)) ||
            !remove_card(player_state.hand, CardId::MindTwist)) {
            return false;
        }
        state.stack.push_back({
            .kind = StackObjectKind::Spell,
            .id = state.next_stack_object_id++,
            .card = CardId::MindTwist,
            .controller = player,
            .target = action.target,
            .x_value = action.x_value,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastRecall: {
        if (action.x_value < 1 ||
            !pay_mana(player_state, recall_cost(action.x_value)) ||
            !remove_card(player_state.hand, CardId::Recall)) {
            return false;
        }
        state.stack.push_back({
            .kind = StackObjectKind::Spell,
            .id = state.next_stack_object_id++,
            .card = CardId::Recall,
            .controller = player,
            .x_value = action.x_value,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastDemonicTutor: {
        const auto& definition =
            card_definition(CardId::DemonicTutor);
        if (!action.chosen_card.has_value() ||
            !pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, CardId::DemonicTutor)) {
            return false;
        }
        StackObject spell{
            .kind = StackObjectKind::Spell,
            .id = state.next_stack_object_id++,
            .card = CardId::DemonicTutor,
            .controller = player,
        };
        spell.chosen_card = action.chosen_card;
        state.stack.push_back(spell);
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastCopyArtifact: {
        const auto& definition =
            card_definition(CardId::CopyArtifact);
        if (!action.target.has_value() ||
            !action.target->creature.has_value() ||
            !pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, CardId::CopyArtifact)) {
            return false;
        }
        state.stack.push_back({
            .kind = StackObjectKind::Spell,
            .id = state.next_stack_object_id++,
            .card = CardId::CopyArtifact,
            .controller = player,
            .target = action.target,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::ActivateSage: {
        if (!action.source_permanent.has_value() ||
            !action.target.has_value() ||
            !action.target->creature.has_value()) {
            return false;
        }
        auto* sage =
            find_creature(player_state, *action.source_permanent);
        if (sage == nullptr || sage->card != CardId::SageOfLatNam ||
            sage->tapped || sage->summoning_sick) {
            return false;
        }
        bool sacrificed = false;
        for (std::size_t index = 0;
             index < player_state.artifacts.size(); ++index) {
            if (player_state.artifacts[index].id ==
                *action.target->creature) {
                player_state.graveyard.push_back(
                    player_state.artifacts[index].card);
                player_state.artifacts.erase(
                    player_state.artifacts.begin() +
                    static_cast<std::ptrdiff_t>(index));
                sacrificed = true;
                break;
            }
        }
        if (!sacrificed) {
            return false;
        }
        sage->tapped = true;
        state.stack.push_back({
            .kind = StackObjectKind::ActivatedAbility,
            .id = state.next_stack_object_id++,
            .card = CardId::SageOfLatNam,
            .controller = player,
        });
        return true;
    }

    case PriorityActionKind::ActivateTriskelion: {
        if (!action.source_permanent.has_value() ||
            !action.target.has_value()) {
            return false;
        }
        auto* trike =
            find_creature(player_state, *action.source_permanent);
        if (trike == nullptr ||
            creature_face(*trike) != CardId::Triskelion ||
            trike->plus_counters <= 0) {
            return false;
        }
        trike->plus_counters -= 1;
        state.stack.push_back({
            .kind = StackObjectKind::ActivatedAbility,
            .id = state.next_stack_object_id++,
            .card = CardId::Triskelion,
            .controller = player,
            .target = action.target,
        });
        return true;
    }

    case PriorityActionKind::ActivateLibrary: {
        if (!action.source_permanent.has_value() ||
            player_state.hand.size() != 7) {
            return false;
        }
        for (auto& land : player_state.lands) {
            if (land.id == *action.source_permanent &&
                land.card == CardId::LibraryOfAlexandria &&
                !land.tapped) {
                land.tapped = true;
                state.stack.push_back({
                    .kind = StackObjectKind::ActivatedAbility,
                    .id = state.next_stack_object_id++,
                    .card = CardId::LibraryOfAlexandria,
                    .controller = player,
                });
                return true;
            }
        }
        return false;
    }

    case PriorityActionKind::ActivateMishrasFactory: {
        constexpr ManaCost kFactoryCost = {.generic = 1};
        if (!action.source_permanent.has_value()) {
            return false;
        }
        auto factory = std::find_if(
            player_state.lands.begin(), player_state.lands.end(),
            [&](const LandPermanent& land) {
                return land.id == *action.source_permanent &&
                       land_face(land) == CardId::MishrasFactory;
            });
        if (factory == player_state.lands.end()) {
            return false;
        }
        // The factory itself must not pay for its own animation:
        // shield it from the auto-payer while the cost is paid.
        const bool was_tapped = factory->tapped;
        factory->tapped = true;
        if (!pay_mana(player_state, kFactoryCost)) {
            factory->tapped = was_tapped;
            return false;
        }
        factory->tapped = was_tapped;
        state.stack.push_back({
            .kind = StackObjectKind::ActivatedAbility,
            .id = state.next_stack_object_id++,
            .card = CardId::MishrasFactory,
            .controller = player,
            .target = Target::creature_target(
                player, *action.source_permanent),
        });
        return true;
    }

    case PriorityActionKind::ActivateStripMine: {
        if (!action.source_permanent.has_value() ||
            !action.target.has_value() ||
            !action.target->creature.has_value()) {
            return false;
        }
        // Sacrificing the Strip Mine is the activation cost.
        bool sacrificed = false;
        for (std::size_t index = 0;
             index < player_state.lands.size(); ++index) {
            if (player_state.lands[index].id ==
                    *action.source_permanent &&
                player_state.lands[index].card == CardId::StripMine) {
                player_state.graveyard.push_back(CardId::StripMine);
                player_state.lands.erase(
                    player_state.lands.begin() +
                    static_cast<std::ptrdiff_t>(index));
                sacrificed = true;
                break;
            }
        }
        if (!sacrificed) {
            return false;
        }
        state.stack.push_back({
            .kind = StackObjectKind::ActivatedAbility,
            .id = state.next_stack_object_id++,
            .card = CardId::StripMine,
            .controller = player,
            .target = action.target,
        });
        return true;
    }

    case PriorityActionKind::ActivateMillstone: {
        if (!action.source_permanent.has_value() ||
            !action.target.has_value() ||
            action.target->creature.has_value()) {
            return false;
        }
        auto* millstone =
            find_artifact(player_state, *action.source_permanent);
        if (millstone == nullptr || millstone->tapped ||
            artifact_face(*millstone) != CardId::Millstone ||
            !pay_mana(player_state, kMillstoneActivationCost)) {
            return false;
        }
        millstone->tapped = true;
        state.stack.push_back({
            .kind = StackObjectKind::ActivatedAbility,
            .id = state.next_stack_object_id++,
            .card = CardId::Millstone,
            .controller = player,
            .target = action.target,
            .spell_target = std::nullopt,
        });
        return true;
    }

    case PriorityActionKind::ActivateChaosOrb: {
        if (!action.source_permanent.has_value() ||
            !action.target.has_value()) {
            return false;
        }
        const auto orb = std::find_if(
            player_state.artifacts.begin(),
            player_state.artifacts.end(),
            [&](const ArtifactPermanent& candidate) {
                return candidate.id == *action.source_permanent;
            });
        if (orb == player_state.artifacts.end() || orb->tapped ||
            artifact_face(*orb) != CardId::ChaosOrb) {
            return false;
        }
        // Tap and sacrifice as the cost; the physical card (a copy
        // stays Copy Artifact) goes to the graveyard now.
        player_state.graveyard.push_back(orb->card);
        player_state.artifacts.erase(orb);
        StackObject ability{
            .kind = StackObjectKind::ActivatedAbility,
            .id = state.next_stack_object_id++,
            .card = CardId::ChaosOrb,
            .controller = player,
            .target = action.target,
            .spell_target = std::nullopt,
        };
        ability.x_value = action.x_value;
        state.stack.push_back(ability);
        return true;
    }

    case PriorityActionKind::ActivateJalumTome: {
        if (!action.source_permanent.has_value()) {
            return false;
        }
        const auto tome = std::find_if(
            player_state.artifacts.begin(),
            player_state.artifacts.end(),
            [&](const ArtifactPermanent& candidate) {
                return candidate.id == *action.source_permanent;
            });
        if (tome == player_state.artifacts.end() || tome->tapped ||
            artifact_face(*tome) != CardId::JalumTome ||
            player_state.hand.empty() ||
            !pay_mana(player_state, ManaCost{.generic = 2})) {
            return false;
        }
        tome->tapped = true;
        state.stack.push_back({
            .kind = StackObjectKind::ActivatedAbility,
            .id = state.next_stack_object_id++,
            .card = CardId::JalumTome,
            .controller = player,
            .target = std::nullopt,
            .spell_target = std::nullopt,
        });
        return true;
    }

    case PriorityActionKind::CastDarkRitual: {
        const auto& definition = card_definition(CardId::DarkRitual);
        if (!pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, CardId::DarkRitual)) {
            return false;
        }
        state.stack.push_back({
            .kind = StackObjectKind::Spell,
            .id = state.next_stack_object_id++,
            .card = CardId::DarkRitual,
            .controller = player,
        });
        ++state.stats[player].spells_cast;
        return true;
    }


    case PriorityActionKind::CastShatter: {
        const auto& definition = card_definition(CardId::Shatter);
        if (!action.target.has_value() ||
            !action.target->creature.has_value() ||
            !pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, CardId::Shatter)) {
            return false;
        }
        state.stack.push_back({
            .kind = StackObjectKind::Spell,
            .id = state.next_stack_object_id++,
            .card = CardId::Shatter,
            .controller = player,
            .target = action.target,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::TapLandForMana: {
        if (!action.source_permanent.has_value()) {
            return false;
        }
        static constexpr std::array<int ManaCost::*, 6> kFaces = {
            &ManaCost::generic, &ManaCost::green, &ManaCost::red,
            &ManaCost::blue,    &ManaCost::white, &ManaCost::black,
        };
        if (action.x_value < 0 ||
            action.x_value >= static_cast<int>(kFaces.size())) {
            return false;
        }
        const auto face =
            kFaces[static_cast<std::size_t>(action.x_value)];
        const auto land = std::find_if(
            player_state.lands.begin(), player_state.lands.end(),
            [&](const LandPermanent& candidate) {
                return candidate.id == *action.source_permanent;
            });
        if (land != player_state.lands.end()) {
            if (land->tapped) {
                return false;
            }
            if (action.x_value == 0) {
                // City of Brass makes colored mana only.
                if (land_face(*land) == CardId::CityOfBrass ||
                    land_mana(land_face(*land)).generic <= 0) {
                    return false;
                }
            } else if (!land_provides(land_face(*land), face)) {
                return false;
            }
            land->tapped = true;
            if (land_face(*land) == CardId::CityOfBrass) {
                player_state.life -= 1;
            }
            player_state.mana_pool.*face += 1;
            return true;
        }
        const auto artifact = std::find_if(
            player_state.artifacts.begin(),
            player_state.artifacts.end(),
            [&](const ArtifactPermanent& candidate) {
                return candidate.id == *action.source_permanent;
            });
        if (artifact != player_state.artifacts.end()) {
            if (artifact->tapped) {
                return false;
            }
            const CardId artifact_card = artifact_face(*artifact);
            if (artifact_card == CardId::FellwarStone) {
                // Any single color, never colorless.
                if (action.x_value == 0) {
                    return false;
                }
                artifact->tapped = true;
                player_state.mana_pool.*face += 1;
                return true;
            }
            const int amount = artifact_mana(artifact_card).*face;
            if (amount <= 0) {
                return false;
            }
            artifact->tapped = true;
            player_state.mana_pool.*face += amount;
            return true;
        }
        const auto creature = std::find_if(
            player_state.creatures.begin(),
            player_state.creatures.end(),
            [&](const CreaturePermanent& candidate) {
                return candidate.id == *action.source_permanent;
            });
        if (creature != player_state.creatures.end()) {
            if (creature->card != CardId::LlanowarElves ||
                creature->tapped || creature->summoning_sick ||
                face != &ManaCost::green) {
                return false;
            }
            creature->tapped = true;
            player_state.mana_pool.green += 1;
            return true;
        }
        return false;
    }
    }

    return false;
}

namespace {
bool resolve_top_of_stack_inner(
    GameState& state, ForceSpikePaymentChoice force_spike_payment);
}  // namespace

bool resolve_top_of_stack(
    GameState& state,
    ForceSpikePaymentChoice force_spike_payment) {
    const bool resolved =
        resolve_top_of_stack_inner(state, force_spike_payment);
    // Static board effects (Crusade) recompute after every
    // resolution: enters, leaves, and every creature arrival funnel
    // through here.
    refresh_crusade_buffs(state);
    return resolved;
}

namespace {
bool resolve_top_of_stack_inner(
    GameState& state,
    ForceSpikePaymentChoice force_spike_payment) {
    if (state.stack.empty()) {
        return false;
    }

    const StackObject spell = state.stack.back();
    state.stack.pop_back();
    auto& controller = state.players[spell.controller];
    const auto& definition = card_definition(spell.card);

    if (spell.kind == StackObjectKind::ActivatedAbility) {
        if (spell.card == CardId::Millstone) {
            if (!spell.target.has_value() ||
                spell.target->creature.has_value()) {
                return false;
            }
            auto& target = state.players[spell.target->player];
            for (int card = 0;
                 card < kMillstoneCards && !target.library.empty();
                 ++card) {
                target.graveyard.push_back(target.library.back());
                target.library.pop_back();
            }
            return true;
        }
        if (spell.card == CardId::LibraryOfAlexandria) {
            if (controller.library.empty()) {
                state.failed_draw[spell.controller] = true;
                return true;
            }
            controller.hand.push_back(controller.library.back());
            controller.library.pop_back();
            ++state.stats[spell.controller].cards_drawn;
            return true;
        }
        if (spell.card == CardId::MishrasFactory) {
            // Animate: the land fights as a 2/2 until cleanup.
            if (!spell.target.has_value() ||
                !spell.target->creature.has_value()) {
                return false;
            }
            for (std::size_t index = 0;
                 index < controller.lands.size(); ++index) {
                if (controller.lands[index].id ==
                    *spell.target->creature) {
                    CreaturePermanent animated{
                        .id = controller.lands[index].id,
                        .card = controller.lands[index].card,
                        .tapped = controller.lands[index].tapped,
                        // A land that entered this turn animates
                        // summoning-sick, like any fresh creature.
                        .summoning_sick =
                            controller.lands[index]
                                .entered_this_turn,
                    };
                    animated.copy_of =
                        controller.lands[index].copy_of;
                    animated.is_copy =
                        controller.lands[index].is_copy;
                    if (!animated.is_copy) {
                        animated.card = CardId::MishrasFactory;
                    }
                    controller.creatures.push_back(animated);
                    controller.lands.erase(
                        controller.lands.begin() +
                        static_cast<std::ptrdiff_t>(index));
                    return true;
                }
            }
            return true;  // already animated or gone: fizzle quietly
        }
        if (spell.card == CardId::SageOfLatNam) {
            auto& owner = state.players[spell.controller];
            if (owner.library.empty()) {
                state.failed_draw[spell.controller] = true;
            } else {
                owner.hand.push_back(owner.library.back());
                owner.library.pop_back();
                ++state.stats[spell.controller].cards_drawn;
            }
            return true;
        }
        if (spell.card == CardId::Triskelion) {
            if (!spell.target.has_value()) {
                return false;
            }
            if (!spell.target->creature.has_value()) {
                state.players[spell.target->player].life -= 1;
                state.stats[spell.controller].damage_to_opponent +=
                    spell.target->player ==
                            opponent_of(spell.controller)
                        ? 1
                        : 0;
                return true;
            }
            auto& owner = state.players[spell.target->player];
            auto* creature =
                find_creature(owner, *spell.target->creature);
            if (creature == nullptr) {
                return true;  // fizzles
            }
            creature->damage += 1;
            remove_dead_creatures(owner);
            return true;
        }
        if (spell.card == CardId::ChaosOrb) {
            if (!spell.target.has_value()) {
                return false;
            }
            auto& owner = state.players[spell.target->player];
            if (!spell.target->creature.has_value()) {
                // Enchantment by row index (rides in x_value).
                const auto index =
                    static_cast<std::size_t>(spell.x_value);
                if (index < owner.enchantments.size()) {
                    owner.graveyard.push_back(
                        owner.enchantments[index]);
                    owner.enchantments.erase(
                        owner.enchantments.begin() +
                        static_cast<std::ptrdiff_t>(index));
                }
                return true;
            }
            const PermanentId target_id = *spell.target->creature;
            for (std::size_t index = 0; index < owner.lands.size();
                 ++index) {
                if (owner.lands[index].id == target_id) {
                    owner.graveyard.push_back(
                        owner.lands[index].card);
                    owner.lands.erase(
                        owner.lands.begin() +
                        static_cast<std::ptrdiff_t>(index));
                    return true;
                }
            }
            for (std::size_t index = 0;
                 index < owner.creatures.size(); ++index) {
                const auto& creature = owner.creatures[index];
                if (creature.id != target_id) {
                    continue;
                }
                // Destruction is a death: Su-Chi's rebate applies.
                if (creature_face(creature) == CardId::SuChi) {
                    owner.mana_pool.generic += 4;
                }
                owner.graveyard.push_back(creature.card);
                owner.creatures.erase(
                    owner.creatures.begin() +
                    static_cast<std::ptrdiff_t>(index));
                return true;
            }
            for (std::size_t index = 0;
                 index < owner.artifacts.size(); ++index) {
                if (owner.artifacts[index].id == target_id) {
                    owner.graveyard.push_back(
                        owner.artifacts[index].card);
                    owner.artifacts.erase(
                        owner.artifacts.begin() +
                        static_cast<std::ptrdiff_t>(index));
                    return true;
                }
            }
            return true;  // target already gone: fizzles
        }
        if (spell.card == CardId::JalumTome) {
            auto& owner = state.players[spell.controller];
            if (!owner.library.empty()) {
                owner.hand.push_back(owner.library.back());
                owner.library.pop_back();
            } else {
                state.failed_draw[spell.controller] = true;
            }
            if (!owner.hand.empty()) {
                // House determinism: the worst card (by the shared
                // handcrafted valuation) is discarded.
                std::size_t worst = 0;
                for (std::size_t index = 1;
                     index < owner.hand.size(); ++index) {
                    if (handcrafted_card_value(owner.hand[index]) <
                        handcrafted_card_value(owner.hand[worst])) {
                        worst = index;
                    }
                }
                owner.graveyard.push_back(owner.hand[worst]);
                owner.hand.erase(
                    owner.hand.begin() +
                    static_cast<std::ptrdiff_t>(worst));
            }
            return true;
        }
        if (spell.card == CardId::StripMine) {
            if (!spell.target.has_value() ||
                !spell.target->creature.has_value()) {
                return false;
            }
            auto& owner = state.players[spell.target->player];
            for (std::size_t index = 0; index < owner.lands.size();
                 ++index) {
                if (owner.lands[index].id == *spell.target->creature) {
                    owner.graveyard.push_back(
                        owner.lands[index].card);
                    owner.lands.erase(
                        owner.lands.begin() +
                        static_cast<std::ptrdiff_t>(index));
                    return true;
                }
            }
            // Animated lands are still lands while they fight.
            for (std::size_t index = 0;
                 index < owner.creatures.size(); ++index) {
                const auto& creature = owner.creatures[index];
                if (creature.id == *spell.target->creature &&
                    card_definition(creature_face(creature)).type ==
                        CardType::Land) {
                    owner.graveyard.push_back(creature.card);
                    owner.creatures.erase(
                        owner.creatures.begin() +
                        static_cast<std::ptrdiff_t>(index));
                    return true;
                }
            }
            return true;  // land already gone: fizzles
        }
        return false;
    }

    if (definition.type == CardType::Creature) {
        controller.creatures.push_back(
            {.id = state.next_permanent_id++,
             .card = spell.card,
             .tapped = false,
             .summoning_sick = true,
             .damage = 0});
        if (spell.card == CardId::Triskelion) {
            controller.creatures.back().plus_counters = 3;
        }
        return true;
    }

    if (definition.type == CardType::Artifact) {
        controller.artifacts.push_back(
            {.id = state.next_permanent_id++,
             .card = spell.card,
             .tapped = false});
        return true;
    }

    if (definition.type == CardType::Enchantment &&
        spell.card != CardId::CopyArtifact) {
        controller.enchantments.push_back(spell.card);
        return true;
    }

    if (spell.card == CardId::LightningBolt) {
        if (spell.target.has_value()) {
            const Target& target = *spell.target;
            if (target.creature.has_value()) {
                auto* creature = find_creature(
                    state.players[target.player], *target.creature);
                if (creature != nullptr) {
                    creature->damage += definition.effect_damage;
                    remove_dead_creatures(state.players[target.player]);
                }
            } else {
                state.players[target.player].life -=
                    definition.effect_damage;
                if (target.player == opponent_of(spell.controller)) {
                    state.stats[spell.controller].damage_to_opponent +=
                        static_cast<std::size_t>(
                            definition.effect_damage);
                }
            }
        }
        controller.graveyard.push_back(spell.card);
        return true;
    }

    if (spell.card == CardId::GiantGrowth) {
        if (spell.target.has_value() &&
            spell.target->creature.has_value()) {
            auto* creature = find_creature(
                state.players[spell.target->player],
                *spell.target->creature);
            if (creature != nullptr) {
                creature->temporary_power_bonus += 3;
                creature->temporary_toughness_bonus += 3;
            }
        }
        controller.graveyard.push_back(spell.card);
        return true;
    }

    if (spell.card == CardId::Disintegrate) {
        if (spell.target.has_value()) {
            const Target& target = *spell.target;
            if (target.creature.has_value()) {
                auto* creature = find_creature(
                    state.players[target.player],
                    *target.creature);
                if (creature != nullptr) {
                    if (spell.x_value > 0) {
                        creature->exile_on_death_this_turn = true;
                    }
                    creature->damage += spell.x_value;
                    remove_dead_creatures(
                        state.players[target.player]);
                }
            } else {
                state.players[target.player].life -= spell.x_value;
                if (target.player ==
                    opponent_of(spell.controller)) {
                    state.stats[spell.controller]
                        .damage_to_opponent +=
                        static_cast<std::size_t>(spell.x_value);
                }
            }
        }
        controller.graveyard.push_back(spell.card);
        return true;
    }

    if (spell.card == CardId::AncestralRecall ||
        spell.card == CardId::Braingeyser) {
        if (spell.target.has_value() &&
            !spell.target->creature.has_value()) {
            const std::size_t target_player =
                spell.target->player;
            const int cards =
                spell.card == CardId::AncestralRecall
                    ? 3
                    : spell.x_value;
            auto& target = state.players[target_player];
            for (int card = 0; card < cards; ++card) {
                if (target.library.empty()) {
                    state.failed_draw[target_player] = true;
                    break;
                }
                target.hand.push_back(target.library.back());
                target.library.pop_back();
                ++state.stats[target_player].cards_drawn;
            }
        }
        controller.graveyard.push_back(spell.card);
        return true;
    }

    if (spell.card == CardId::ForceSpike) {
        if (spell.spell_target.has_value()) {
            const auto target = std::find_if(
                state.stack.begin(), state.stack.end(),
                [&](const StackObject& candidate) {
                    return candidate.id == *spell.spell_target;
                });
            if (target != state.stack.end() &&
                target->kind == StackObjectKind::Spell) {
                const bool paid =
                    force_spike_payment ==
                        ForceSpikePaymentChoice::PayIfAble &&
                    pay_mana(
                        state.players[target->controller],
                        ManaCost{.generic = 1});
                if (!paid) {
                    state.players[target->controller]
                        .graveyard.push_back(target->card);
                    state.stack.erase(target);
                    ++state.stats[spell.controller]
                          .spells_countered;
                }
            }
        }
        controller.graveyard.push_back(spell.card);
        return true;
    }

    if (spell.card == CardId::Counterspell) {
        if (spell.spell_target.has_value()) {
            const auto target = std::find_if(
                state.stack.begin(), state.stack.end(),
                [&](const StackObject& candidate) {
                    return candidate.id == *spell.spell_target;
                });
            if (target != state.stack.end()) {
                state.players[target->controller].graveyard.push_back(
                    target->card);
                state.stack.erase(target);
                ++state.stats[spell.controller].spells_countered;
            }
        }
        controller.graveyard.push_back(spell.card);
        return true;
    }

    if (spell.card == CardId::TimeWalk) {
        auto& queued = state.extra_turns_pending[spell.controller];
        if (queued == std::numeric_limits<std::size_t>::max()) {
            throw std::overflow_error(
                "Time Walk extra-turn queue overflow");
        }
        ++queued;
        controller.graveyard.push_back(spell.card);
        return true;
    }

    if (spell.card == CardId::Tsunami) {
        for (auto& player : state.players) {
            auto land = player.lands.begin();
            while (land != player.lands.end()) {
                if (land->card == CardId::Island) {
                    player.graveyard.push_back(land->card);
                    land = player.lands.erase(land);
                } else {
                    ++land;
                }
            }
        }
        controller.graveyard.push_back(spell.card);
        return true;
    }

    if (spell.card == CardId::PsionicBlast) {
        if (!spell.target.has_value()) {
            return false;
        }
        controller.graveyard.push_back(spell.card);
        controller.life -= 2;
        if (!spell.target->creature.has_value()) {
            state.players[spell.target->player].life -= 4;
            state.stats[spell.controller].damage_to_opponent +=
                spell.target->player == opponent_of(spell.controller)
                    ? 4
                    : 0;
            return true;
        }
        auto& owner = state.players[spell.target->player];
        auto* creature =
            find_creature(owner, *spell.target->creature);
        if (creature == nullptr) {
            return true;  // fizzles
        }
        creature->damage += 4;
        remove_dead_creatures(owner);
        return true;
    }

    if (spell.card == CardId::SwordsToPlowshares) {
        if (!spell.target.has_value() ||
            !spell.target->creature.has_value()) {
            return false;
        }
        controller.graveyard.push_back(spell.card);
        auto& owner = state.players[spell.target->player];
        auto* creature =
            find_creature(owner, *spell.target->creature);
        if (creature == nullptr) {
            return true;  // fizzles
        }
        owner.life += creature_power(*creature);
        owner.exile.push_back(creature->card);
        owner.creatures.erase(
            std::find_if(owner.creatures.begin(),
                         owner.creatures.end(),
                         [&](const CreaturePermanent& candidate) {
                             return candidate.id ==
                                    *spell.target->creature;
                         }));
        return true;
    }

    if (spell.card == CardId::Disenchant) {
        if (!spell.target.has_value()) {
            return false;
        }
        controller.graveyard.push_back(spell.card);
        auto& owner = state.players[spell.target->player];
        if (spell.target->creature.has_value()) {
            for (std::size_t index = 0;
                 index < owner.artifacts.size(); ++index) {
                if (owner.artifacts[index].id ==
                    *spell.target->creature) {
                    owner.graveyard.push_back(
                        owner.artifacts[index].card);
                    owner.artifacts.erase(
                        owner.artifacts.begin() +
                        static_cast<std::ptrdiff_t>(index));
                    return true;
                }
            }
            return true;  // fizzles
        }
        const std::size_t index =
            static_cast<std::size_t>(std::max(0, spell.x_value));
        if (index < owner.enchantments.size()) {
            owner.graveyard.push_back(owner.enchantments[index]);
            owner.enchantments.erase(
                owner.enchantments.begin() +
                static_cast<std::ptrdiff_t>(index));
        }
        return true;
    }

    if (spell.card == CardId::WheelOfFortune) {
        for (auto& wheel_player : state.players) {
            for (const CardId held : wheel_player.hand) {
                wheel_player.graveyard.push_back(held);
            }
            wheel_player.hand.clear();
        }
        for (std::size_t seat = 0; seat < 2; ++seat) {
            auto& wheel_player = state.players[seat];
            for (int draw = 0; draw < 7; ++draw) {
                if (wheel_player.library.empty()) {
                    state.failed_draw[seat] = true;
                    break;
                }
                wheel_player.hand.push_back(
                    wheel_player.library.back());
                wheel_player.library.pop_back();
                ++state.stats[seat].cards_drawn;
            }
        }
        controller.graveyard.push_back(spell.card);
        return true;
    }

    if (spell.card == CardId::ManaDrain) {
        if (spell.spell_target.has_value()) {
            const auto target = std::find_if(
                state.stack.begin(), state.stack.end(),
                [&](const StackObject& candidate) {
                    return candidate.id == *spell.spell_target;
                });
            if (target != state.stack.end() &&
                target->kind == StackObjectKind::Spell) {
                // The countered spell's mana is owed at the caster's
                // next turn (house timing for Mana Drain).
                controller.pending_mana +=
                    mana_total(
                        card_definition(target->card).cost) +
                    std::max(0, target->x_value);
                state.players[target->controller]
                    .graveyard.push_back(target->card);
                state.stack.erase(target);
                ++state.stats[spell.controller].spells_countered;
            }
        }
        controller.graveyard.push_back(spell.card);
        return true;
    }


    if (spell.card == CardId::MindTwist) {
        controller.graveyard.push_back(spell.card);
        if (!spell.target.has_value()) {
            return false;
        }
        auto& victim = state.players[spell.target->player];
        for (int discard = 0;
             discard < spell.x_value && !victim.hand.empty();
             ++discard) {
            victim.graveyard.push_back(victim.hand.front());
            victim.hand.erase(victim.hand.begin());
        }
        return true;
    }

    if (spell.card == CardId::Recall) {
        controller.graveyard.push_back(spell.card);
        // House simplification: discard the first X cards of hand,
        // then return the X most recent graveyard cards to hand.
        for (int discard = 0;
             discard < spell.x_value && !controller.hand.empty();
             ++discard) {
            controller.graveyard.push_back(controller.hand.front());
            controller.hand.erase(controller.hand.begin());
        }
        for (int returned = 0;
             returned < spell.x_value && !controller.graveyard.empty();
             ++returned) {
            controller.hand.push_back(controller.graveyard.back());
            controller.graveyard.pop_back();
        }
        return true;
    }

    if (spell.card == CardId::DemonicTutor) {
        controller.graveyard.push_back(spell.card);
        if (spell.chosen_card.has_value()) {
            const auto found = std::find(controller.library.begin(),
                                         controller.library.end(),
                                         *spell.chosen_card);
            if (found != controller.library.end()) {
                controller.hand.push_back(*found);
                controller.library.erase(found);
            }
        }
        return true;
    }

    if (spell.card == CardId::CopyArtifact) {
        if (!spell.target.has_value() ||
            !spell.target->creature.has_value()) {
            return false;
        }
        const auto& owner = state.players[spell.target->player];
        const ArtifactPermanent* original = nullptr;
        for (const auto& artifact : owner.artifacts) {
            if (artifact.id == *spell.target->creature) {
                original = &artifact;
                break;
            }
        }
        if (original != nullptr) {
            ArtifactPermanent copy{
                .id = state.next_permanent_id++,
                .card = CardId::CopyArtifact,
                .tapped = false,
            };
            copy.copy_of = original->is_copy ? original->copy_of
                                             : original->card;
            copy.is_copy = true;
            controller.artifacts.push_back(copy);
            return true;
        }
        for (const auto& creature : owner.creatures) {
            if (creature.id != *spell.target->creature ||
                !is_artifact_creature(creature_face(creature))) {
                continue;
            }
            const CardId face = creature_face(creature);
            if (face == CardId::MishrasFactory) {
                // Copies see the printed card: an animated Factory
                // copies as an UN-animated Factory land.
                LandPermanent copy{
                    .card = CardId::CopyArtifact,
                    .tapped = false,
                    .id = state.next_permanent_id++};
                copy.copy_of = CardId::MishrasFactory;
                copy.is_copy = true;
                copy.entered_this_turn = true;
                controller.lands.push_back(copy);
                return true;
            }
            CreaturePermanent copy{
                .id = state.next_permanent_id++,
                .card = CardId::CopyArtifact,
                .tapped = false,
                .summoning_sick = true,
            };
            copy.copy_of = face;
            copy.is_copy = true;
            if (face == CardId::Triskelion) {
                copy.plus_counters = 3;
            }
            controller.creatures.push_back(copy);
            return true;
        }
        controller.graveyard.push_back(spell.card);
        return true;  // fizzles
    }

    if (spell.card == CardId::DarkRitual) {
        controller.mana_pool.black += 3;
        controller.graveyard.push_back(spell.card);
        return true;
    }


    if (spell.card == CardId::Shatter) {
        controller.graveyard.push_back(spell.card);
        if (!spell.target.has_value() ||
            !spell.target->creature.has_value()) {
            return false;
        }
        auto& owner = state.players[spell.target->player];
        for (std::size_t index = 0; index < owner.artifacts.size();
             ++index) {
            if (owner.artifacts[index].id ==
                *spell.target->creature) {
                owner.graveyard.push_back(
                    owner.artifacts[index].card);
                owner.artifacts.erase(
                    owner.artifacts.begin() +
                    static_cast<std::ptrdiff_t>(index));
                return true;
            }
        }
        return true;  // fizzles
    }

    if (spell.card == CardId::Armageddon) {
        for (auto& player_side : state.players) {
            for (const auto& land : player_side.lands) {
                player_side.graveyard.push_back(land.card);
            }
            player_side.lands.clear();
            // Animated lands die with the rest.
            for (std::size_t index = player_side.creatures.size();
                 index-- > 0;) {
                if (card_definition(creature_face(
                        player_side.creatures[index]))
                        .type == CardType::Land) {
                    player_side.graveyard.push_back(
                        player_side.creatures[index].card);
                    player_side.creatures.erase(
                        player_side.creatures.begin() +
                        static_cast<std::ptrdiff_t>(index));
                }
            }
        }
        controller.graveyard.push_back(spell.card);
        return true;
    }

    if (spell.card == CardId::Timetwister) {
        // House determinism: each player's hand and graveyard go under
        // the library in fixed order, then both draw seven.
        for (std::size_t seat = 0; seat < 2; ++seat) {
            auto& twisted = state.players[seat];
            std::vector<CardId> pile;
            if (seat == spell.controller) {
                // The spell itself returns to the shuffled pile too.
                pile.push_back(CardId::Timetwister);
            }
            pile.insert(pile.end(), twisted.hand.begin(),
                        twisted.hand.end());
            pile.insert(pile.end(), twisted.graveyard.begin(),
                        twisted.graveyard.end());
            twisted.hand.clear();
            twisted.graveyard.clear();
            twisted.library.insert(twisted.library.begin(),
                                   pile.begin(), pile.end());
            for (int draw = 0; draw < 7; ++draw) {
                if (twisted.library.empty()) {
                    state.failed_draw[seat] = true;
                    break;
                }
                twisted.hand.push_back(twisted.library.back());
                twisted.library.pop_back();
                ++state.stats[seat].cards_drawn;
            }
        }
        return true;
    }

    if (spell.card == CardId::Channel) {
        // Until end of turn the controller may pay life for mana; the
        // payment itself happens inside mana planning.
        controller.channel_active = true;
        controller.graveyard.push_back(spell.card);
        return true;
    }

    return false;
}
}  // namespace

PriorityPassResult pass_priority(GameState& state,
                                 PriorityState& priority) {
    if (priority.player >= state.players.size()) {
        throw std::out_of_range("priority player must be 0 or 1");
    }

    ++priority.consecutive_passes;
    if (priority.consecutive_passes < 2) {
        priority.player = opponent_of(priority.player);
        return PriorityPassResult::Passed;
    }

    if (state.stack.empty()) {
        for (auto& player : state.players) {
            player.mana_pool = {};
        }
        return PriorityPassResult::WindowEnded;
    }
    if (!resolve_top_of_stack(state)) {
        throw std::logic_error("failed to resolve the stack");
    }

    priority.player = state.active_player;
    priority.consecutive_passes = 0;
    return PriorityPassResult::StackObjectResolved;
}

std::optional<ResolvedPriorityActionConsequence>
resolve_priority_action_consequence(
    const GameState& state, std::size_t player, bool sorcery_actions,
    int consecutive_passes, const PriorityAction& action) {
    if (player >= state.players.size()) {
        throw std::out_of_range(
            "resolved Priority consequence player must be 0 or 1");
    }
    if (consecutive_passes < 0 || consecutive_passes > 1) {
        throw std::out_of_range(
            "resolved Priority consequence pass count must be zero or one");
    }

    ResolvedPriorityActionConsequence result{
        .state = state,
        .priority = {
            .player = player,
            .consecutive_passes = consecutive_passes,
        },
    };
    std::optional<StackObjectId> object_to_resolve;
    if (action.kind == PriorityActionKind::Pass) {
        if (!result.state.stack.empty()) {
            object_to_resolve = result.state.stack.back().id;
        }
    } else {
        const std::size_t stack_size = result.state.stack.size();
        if (!apply_priority_action(
                result.state, player, action, sorcery_actions)) {
            return std::nullopt;
        }
        result.priority = {
            .player = player,
            .consecutive_passes = 0,
        };
        if (result.state.stack.size() > stack_size) {
            object_to_resolve = result.state.stack.back().id;
        }
    }

    const auto update_terminal =
        [&]() {
            if (result.state.failed_draw[0] ||
                result.state.failed_draw[1]) {
                result.terminal = true;
                result.winner =
                    result.state.failed_draw[0] ==
                            result.state.failed_draw[1]
                        ? -1
                        : (result.state.failed_draw[0] ? 1 : 0);
                return;
            }
            const bool zero_lost =
                result.state.players[0].life <= 0;
            const bool one_lost =
                result.state.players[1].life <= 0;
            result.terminal = zero_lost || one_lost;
            if (!result.terminal || zero_lost == one_lost) {
                result.winner = -1;
            } else {
                result.winner = zero_lost ? 1 : 0;
            }
        };
    const auto take_pass =
        [&]() {
            const PriorityPassResult pass =
                pass_priority(result.state, result.priority);
            ++result.priority_passes;
            if (pass == PriorityPassResult::WindowEnded) {
                result.window_ended = true;
            } else if (
                pass == PriorityPassResult::StackObjectResolved) {
                ++result.stack_resolutions;
                update_terminal();
            }
        };

    if (action.kind == PriorityActionKind::Pass) {
        take_pass();
    }
    constexpr std::size_t kMaximumConsequencePasses = 1024;
    while (object_to_resolve.has_value() &&
           !result.terminal &&
           std::any_of(
               result.state.stack.begin(),
               result.state.stack.end(),
               [&](const StackObject& object) {
                   return object.id == *object_to_resolve;
               })) {
        if (result.priority_passes >=
            kMaximumConsequencePasses) {
            return std::nullopt;
        }
        take_pass();
    }
    update_terminal();
    return result;
}

namespace {

enum class PassDominanceSourceKind : std::uint8_t {
    Land,
    Artifact,
};

struct PassDominanceManaSource {
    PassDominanceSourceKind kind = PassDominanceSourceKind::Land;
    std::uint64_t key = 0;
    CardId card = CardId::Forest;

    bool operator==(const PassDominanceManaSource&) const = default;
    auto operator<=>(const PassDominanceManaSource&) const = default;
};

struct PassDominanceResourceCost {
    std::array<std::size_t, kCardCount> hand_cards_consumed{};
    ManaCost mana_depleted;
    std::vector<PassDominanceManaSource>
        preexisting_sources_newly_tapped;
    bool land_play_entitlement_consumed = false;

    bool operator==(const PassDominanceResourceCost&) const = default;
};

struct PassDominanceSettlement {
    GameState state;
    std::array<PassDominanceResourceCost, 2> resources;
    TurnPhase phase = TurnPhase::FirstMain;
    std::size_t final_priority_player = 0;
    int final_consecutive_passes = 0;
    bool terminal = false;
    bool window_ended = false;
};

void add_pass_dominance_mana(ManaCost& destination,
                             const ManaCost& value) {
    destination.generic += value.generic;
    destination.green += value.green;
    destination.red += value.red;
    destination.blue += value.blue;
    destination.white += value.white;
    destination.black += value.black;
}

ManaCost pass_dominance_available_mana(
    const PlayerState& player) {
    ManaCost available = player.mana_pool;
    for (const LandPermanent& land : player.lands) {
        if (land.tapped) {
            continue;
        }
        switch (land.card) {
        case CardId::Forest:
            ++available.green;
            break;
        case CardId::Mountain:
            ++available.red;
            break;
        case CardId::Island:
            ++available.blue;
            break;
        case CardId::Plains:
            ++available.white;
            break;
        case CardId::Swamp:
            ++available.black;
            break;
        default:
            break;
        }
    }
    for (const ArtifactPermanent& artifact : player.artifacts) {
        if (artifact.tapped) {
            continue;
        }
        const CardId face = artifact_face(artifact);
        if (face == CardId::MoxSapphire) {
            ++available.blue;
        } else if (face == CardId::SolRing) {
            available.generic += 2;
        }
    }
    return available;
}

ManaCost pass_dominance_positive_mana_difference(
    const ManaCost& before, const ManaCost& after) {
    return {
        .generic = std::max(0, before.generic - after.generic),
        .green = std::max(0, before.green - after.green),
        .red = std::max(0, before.red - after.red),
        .blue = std::max(0, before.blue - after.blue),
        .white = std::max(0, before.white - after.white),
        .black = std::max(0, before.black - after.black),
    };
}

std::array<std::size_t, kCardCount>
pass_dominance_hand_counts(const PlayerState& player) {
    std::array<std::size_t, kCardCount> counts{};
    for (const CardId card : player.hand) {
        ++counts[static_cast<std::size_t>(card)];
    }
    return counts;
}

void add_pass_dominance_newly_tapped_sources(
    const GameState& root, const GameState& before,
    const GameState& after, std::size_t player,
    std::vector<PassDominanceManaSource>& destination) {
    const auto add_unique =
        [&](PassDominanceManaSource source) {
            if (std::find(destination.begin(), destination.end(),
                          source) == destination.end()) {
                destination.push_back(source);
            }
        };

    const auto& root_player = root.players[player];
    const auto& before_player = before.players[player];
    const auto& after_player = after.players[player];
    const std::size_t land_count =
        std::min({root_player.lands.size(),
                  before_player.lands.size(),
                  after_player.lands.size()});
    for (std::size_t index = 0; index < land_count; ++index) {
        if (root_player.lands[index].card ==
                before_player.lands[index].card &&
            root_player.lands[index].card ==
                after_player.lands[index].card &&
            !root_player.lands[index].tapped &&
            !before_player.lands[index].tapped &&
            after_player.lands[index].tapped) {
            add_unique({
                .kind = PassDominanceSourceKind::Land,
                .key = static_cast<std::uint64_t>(index),
                .card = root_player.lands[index].card,
            });
        }
    }

    for (const ArtifactPermanent& root_artifact :
         root_player.artifacts) {
        if (root_artifact.tapped) {
            continue;
        }
        const auto before_artifact = std::find_if(
            before_player.artifacts.begin(),
            before_player.artifacts.end(),
            [&](const ArtifactPermanent& candidate) {
                return candidate.id == root_artifact.id &&
                       candidate.card == root_artifact.card;
            });
        const auto after_artifact = std::find_if(
            after_player.artifacts.begin(),
            after_player.artifacts.end(),
            [&](const ArtifactPermanent& candidate) {
                return candidate.id == root_artifact.id &&
                       candidate.card == root_artifact.card;
            });
        if (before_artifact != before_player.artifacts.end() &&
            after_artifact != after_player.artifacts.end() &&
            !before_artifact->tapped && after_artifact->tapped) {
            add_unique({
                .kind = PassDominanceSourceKind::Artifact,
                .key = root_artifact.id,
                .card = root_artifact.card,
            });
        }
    }
}

void accumulate_pass_dominance_operation(
    const GameState& root, const GameState& before,
    const GameState& after,
    std::array<PassDominanceResourceCost, 2>& resources,
    bool include_hand_and_land_costs) {
    for (std::size_t player = 0;
         player < root.players.size(); ++player) {
        add_pass_dominance_mana(
            resources[player].mana_depleted,
            pass_dominance_positive_mana_difference(
                pass_dominance_available_mana(
                    before.players[player]),
                pass_dominance_available_mana(
                    after.players[player])));
        add_pass_dominance_newly_tapped_sources(
            root, before, after, player,
            resources[player].preexisting_sources_newly_tapped);

        if (!include_hand_and_land_costs) {
            continue;
        }
        const auto before_hand =
            pass_dominance_hand_counts(before.players[player]);
        const auto after_hand =
            pass_dominance_hand_counts(after.players[player]);
        for (std::size_t card = 0; card < kCardCount; ++card) {
            if (before_hand[card] > after_hand[card]) {
                resources[player].hand_cards_consumed[card] +=
                    before_hand[card] - after_hand[card];
            }
        }
        if (!before.players[player].land_played_this_turn &&
            after.players[player].land_played_this_turn) {
            resources[player].land_play_entitlement_consumed = true;
        }
    }
}

bool pass_dominance_terminal(const GameState& state) {
    return state.players[0].life <= 0 ||
           state.players[1].life <= 0 ||
           state.failed_draw[0] || state.failed_draw[1];
}

std::optional<PassDominanceSettlement>
settle_pass_dominance_action(
    const GameState& root, std::size_t player,
    bool sorcery_actions, TurnPhase phase, int consecutive_passes,
    const PriorityAction& action) {
    PassDominanceSettlement settlement{
        .state = root,
        .phase = phase,
    };
    PriorityState priority{
        .player = player,
        .consecutive_passes = consecutive_passes,
    };

    const auto take_pass =
        [&]() -> std::optional<PriorityPassResult> {
            try {
                const GameState before = settlement.state;
                const PriorityPassResult result =
                    pass_priority(settlement.state, priority);
                if (result ==
                    PriorityPassResult::StackObjectResolved) {
                    accumulate_pass_dominance_operation(
                        root, before, settlement.state,
                        settlement.resources, false);
                    settlement.terminal =
                        pass_dominance_terminal(settlement.state);
                } else if (result ==
                           PriorityPassResult::WindowEnded) {
                    settlement.window_ended = true;
                }
                return result;
            } catch (const std::exception&) {
                return std::nullopt;
            }
        };

    if (action.kind == PriorityActionKind::Pass) {
        if (!take_pass().has_value()) {
            return std::nullopt;
        }
    } else {
        const GameState before = settlement.state;
        try {
            if (!apply_priority_action(
                    settlement.state, player, action,
                    sorcery_actions)) {
                return std::nullopt;
            }
        } catch (const std::exception&) {
            return std::nullopt;
        }
        accumulate_pass_dominance_operation(
            root, before, settlement.state,
            settlement.resources, true);
        priority = {
            .player = player,
            .consecutive_passes = 0,
        };
    }

    constexpr std::size_t kMaximumPassSteps = 1024;
    std::size_t steps = 0;
    while (!settlement.terminal && !settlement.window_ended) {
        if (++steps > kMaximumPassSteps ||
            !take_pass().has_value()) {
            return std::nullopt;
        }
    }
    for (auto& resource : settlement.resources) {
        std::sort(
            resource.preexisting_sources_newly_tapped.begin(),
            resource.preexisting_sources_newly_tapped.end());
    }
    settlement.final_priority_player = priority.player;
    settlement.final_consecutive_passes =
        priority.consecutive_passes;
    return settlement;
}

void remove_pass_dominance_graveyard_cost(
    std::vector<CardId>& graveyard,
    const std::vector<CardId>& root_graveyard, CardId card,
    std::size_t maximum) {
    const std::size_t root_count =
        static_cast<std::size_t>(std::count(
            root_graveyard.begin(), root_graveyard.end(), card));
    const std::size_t current_count =
        static_cast<std::size_t>(std::count(
            graveyard.begin(), graveyard.end(), card));
    std::size_t removable =
        std::min(maximum, current_count > root_count
                              ? current_count - root_count
                              : std::size_t{0});
    while (removable > 0) {
        const auto found =
            std::find(graveyard.rbegin(), graveyard.rend(), card);
        if (found == graveyard.rend()) {
            break;
        }
        graveyard.erase(std::next(found).base());
        --removable;
    }
}

void restore_pass_dominance_source(
    PlayerState& player, const PlayerState& root,
    const PassDominanceManaSource& source) {
    if (source.kind == PassDominanceSourceKind::Land) {
        const std::size_t index =
            static_cast<std::size_t>(source.key);
        if (index < player.lands.size() &&
            index < root.lands.size() &&
            player.lands[index].card == source.card &&
            root.lands[index].card == source.card) {
            player.lands[index].tapped =
                root.lands[index].tapped;
        }
        return;
    }

    const auto root_artifact = std::find_if(
        root.artifacts.begin(), root.artifacts.end(),
        [&](const ArtifactPermanent& candidate) {
            return candidate.id == source.key &&
                   candidate.card == source.card;
        });
    const auto artifact = std::find_if(
        player.artifacts.begin(), player.artifacts.end(),
        [&](const ArtifactPermanent& candidate) {
            return candidate.id == source.key &&
                   candidate.card == source.card;
        });
    if (root_artifact != root.artifacts.end() &&
        artifact != player.artifacts.end()) {
        artifact->tapped = root_artifact->tapped;
    }
}

GameState normalized_pass_dominance_observation(
    const GameState& root,
    const PassDominanceSettlement& settlement,
    std::size_t observer) {
    GameState normalized = settlement.state;
    for (std::size_t player = 0;
         player < normalized.players.size(); ++player) {
        PlayerState& state = normalized.players[player];
        const PlayerState& root_state = root.players[player];
        const auto& resource = settlement.resources[player];
        for (std::size_t card = 0; card < kCardCount; ++card) {
            const std::size_t consumed =
                resource.hand_cards_consumed[card];
            const CardId card_id = static_cast<CardId>(card);
            state.hand.insert(
                state.hand.end(), consumed, card_id);
            remove_pass_dominance_graveyard_cost(
                state.graveyard, root_state.graveyard,
                card_id, consumed);
        }
        for (const PassDominanceManaSource& source :
             resource.preexisting_sources_newly_tapped) {
            restore_pass_dominance_source(
                state, root_state, source);
        }
        state.mana_pool = root_state.mana_pool;
        state.land_played_this_turn =
            root_state.land_played_this_turn;

        // Libraries and the opponent's hand are hidden. Their sizes remain
        // observable; their identities and ordering cannot enter the proof.
        state.library.assign(
            state.library.size(), CardId::Forest);
        if (player != observer) {
            state.hand.assign(
                state.hand.size(), CardId::Forest);
        }
        std::sort(state.hand.begin(), state.hand.end());
        std::sort(state.graveyard.begin(), state.graveyard.end());
        std::sort(state.exile.begin(), state.exile.end());
        std::sort(
            state.enchantments.begin(),
            state.enchantments.end());
        std::sort(
            state.lands.begin(), state.lands.end(),
            [](const LandPermanent& left,
               const LandPermanent& right) {
                return std::tie(left.card, left.tapped) <
                       std::tie(right.card, right.tapped);
            });
        std::sort(
            state.creatures.begin(), state.creatures.end(),
            [](const CreaturePermanent& left,
               const CreaturePermanent& right) {
                return left.id < right.id;
            });
        std::sort(
            state.artifacts.begin(), state.artifacts.end(),
            [](const ArtifactPermanent& left,
               const ArtifactPermanent& right) {
                return left.id < right.id;
            });
    }
    normalized.stats = {};
    normalized.next_permanent_id = 0;
    normalized.next_stack_object_id = 0;
    return normalized;
}

bool pass_dominance_mana_leq(
    const ManaCost& left, const ManaCost& right) {
    return left.generic <= right.generic &&
           left.green <= right.green &&
           left.red <= right.red &&
           left.blue <= right.blue &&
           left.white <= right.white &&
           left.black <= right.black;
}

bool pass_dominance_source_leq(
    const std::vector<PassDominanceManaSource>& left,
    const std::vector<PassDominanceManaSource>& right) {
    return std::includes(
        right.begin(), right.end(), left.begin(), left.end());
}

bool pass_dominance_resource_leq(
    const PassDominanceResourceCost& left,
    const PassDominanceResourceCost& right) {
    for (std::size_t card = 0; card < kCardCount; ++card) {
        if (left.hand_cards_consumed[card] >
            right.hand_cards_consumed[card]) {
            return false;
        }
    }
    return pass_dominance_mana_leq(
               left.mana_depleted, right.mana_depleted) &&
           pass_dominance_source_leq(
               left.preexisting_sources_newly_tapped,
               right.preexisting_sources_newly_tapped) &&
           (!left.land_play_entitlement_consumed ||
            right.land_play_entitlement_consumed);
}

bool pass_strictly_dominates_candidate(
    const GameState& root, std::size_t actor,
    const PassDominanceSettlement& pass,
    const PassDominanceSettlement& candidate) {
    if (pass.phase != candidate.phase ||
        pass.final_priority_player !=
            candidate.final_priority_player ||
        pass.final_consecutive_passes !=
            candidate.final_consecutive_passes ||
        pass.terminal != candidate.terminal ||
        pass.window_ended != candidate.window_ended ||
        normalized_pass_dominance_observation(
            root, pass, actor) !=
            normalized_pass_dominance_observation(
                root, candidate, actor)) {
        return false;
    }

    const std::size_t opponent = opponent_of(actor);
    return pass.resources[opponent] ==
               candidate.resources[opponent] &&
           pass_dominance_resource_leq(
               pass.resources[actor],
               candidate.resources[actor]) &&
           pass.resources[actor] != candidate.resources[actor];
}

GameState pass_dominance_information_state(
    const GameState& state, std::size_t observer) {
    GameState information = state;
    for (auto& player : information.players) {
        player.library.assign(
            player.library.size(), CardId::Forest);
    }
    const std::size_t opponent = opponent_of(observer);
    information.players[opponent].hand.assign(
        information.players[opponent].hand.size(),
        CardId::Forest);
    return information;
}

ValuePassDominanceDiagnostic
value_pass_dominance_for_actions(
    const GameState& state, std::size_t player,
    bool sorcery_actions, TurnPhase phase, int consecutive_passes,
    const std::vector<PriorityAction>& actions) {
    ValuePassDominanceDiagnostic diagnostic;
    diagnostic.actions.reserve(actions.size());
    for (const PriorityAction& action : actions) {
        diagnostic.actions.push_back({.action = action});
    }
    const auto pass = std::find_if(
        actions.begin(), actions.end(),
        [](const PriorityAction& action) {
            return action.kind == PriorityActionKind::Pass;
        });
    if (pass == actions.end()) {
        diagnostic.failed_comparisons = actions.size();
        return diagnostic;
    }
    diagnostic.pass_index = static_cast<std::size_t>(
        std::distance(actions.begin(), pass));
    const GameState information_state =
        pass_dominance_information_state(state, player);
    const auto pass_settlement =
        settle_pass_dominance_action(
            information_state, player, sorcery_actions, phase,
            consecutive_passes, *pass);
    if (!pass_settlement.has_value()) {
        diagnostic.failed_comparisons = actions.size();
        return diagnostic;
    }
    diagnostic.pass_settled = true;
    diagnostic.actions[diagnostic.pass_index]
        .comparison_settled = true;
    for (std::size_t index = 0; index < actions.size(); ++index) {
        if (index == diagnostic.pass_index) {
            continue;
        }
        const auto candidate =
            settle_pass_dominance_action(
                information_state, player, sorcery_actions, phase,
                consecutive_passes, actions[index]);
        if (!candidate.has_value()) {
            ++diagnostic.failed_comparisons;
            continue;
        }
        diagnostic.actions[index].comparison_settled = true;
        diagnostic.actions[index]
            .strictly_dominated_by_pass =
            pass_strictly_dominates_candidate(
                information_state, player, *pass_settlement,
                *candidate);
    }
    return diagnostic;
}

} // namespace

std::vector<PriorityAction>
ValuePassDominanceDiagnostic::retained_actions() const {
    std::vector<PriorityAction> retained;
    retained.reserve(actions.size());
    for (const auto& action : actions) {
        if (!action.strictly_dominated_by_pass) {
            retained.push_back(action.action);
        }
    }
    return retained;
}

ValuePassDominanceDiagnostic diagnose_value_pass_dominance(
    const GameState& state, std::size_t player,
    bool sorcery_actions, TurnPhase phase, int consecutive_passes) {
    if (player >= state.players.size()) {
        throw std::out_of_range(
            "Pass-dominance player must be 0 or 1");
    }
    if (consecutive_passes < 0 || consecutive_passes > 1) {
        throw std::out_of_range(
            "Pass-dominance pass count must be zero or one");
    }
    return value_pass_dominance_for_actions(
        state, player, sorcery_actions, phase,
        consecutive_passes,
        legal_priority_actions(state, player, sorcery_actions));
}

std::size_t advance_turn_player(GameState& state) {
    if (state.active_player >= state.players.size()) {
        throw std::out_of_range("active player must be 0 or 1");
    }
    auto& queued = state.extra_turns_pending[state.active_player];
    if (queued > 0) {
        --queued;
    } else {
        state.active_player = opponent_of(state.active_player);
    }
    return state.active_player;
}

std::vector<PermanentId> legal_attackers(
    const GameState& state, std::size_t attacking_player) {
    if (attacking_player >= state.players.size()) {
        throw std::out_of_range(
            "attacking player must be 0 or 1");
    }
    std::vector<PermanentId> result;
    for (const CreaturePermanent& creature :
         state.players[attacking_player].creatures) {
        if (!creature.tapped && !creature.summoning_sick &&
            can_attack_through_moat(state, creature)) {
            result.push_back(creature.id);
        }
    }
    return result;
}

bool resolve_combat(
    GameState& state, std::size_t attacking_player,
    const std::vector<PermanentId>& attackers,
    const std::vector<std::pair<PermanentId, PermanentId>>& blocks) {
    if (attacking_player >= state.players.size()) {
        return false;
    }
    const std::size_t defending_player = opponent_of(attacking_player);

    std::unordered_set<PermanentId> attacker_ids;
    for (const PermanentId attacker_id : attackers) {
        const auto* creature =
            find_creature(state.players[attacking_player], attacker_id);
        if (creature == nullptr || creature->tapped ||
            creature->summoning_sick ||
            !can_attack_through_moat(state, *creature) ||
            !attacker_ids.insert(attacker_id).second) {
            return false;
        }
    }

    std::unordered_set<PermanentId> blocker_ids;
    for (const auto& [attacker_id, blocker_id] : blocks) {
        if (!attacker_ids.contains(attacker_id)) {
            return false;
        }
        const auto* attacker =
            find_creature(state.players[attacking_player], attacker_id);
        const auto* blocker =
            find_creature(state.players[defending_player], blocker_id);
        if (blocker == nullptr || blocker->tapped ||
            attacker == nullptr ||
            !can_block_creature(*attacker, *blocker) ||
            !blocker_ids.insert(blocker_id).second) {
            return false;
        }
    }

    std::unordered_map<PermanentId, std::vector<PermanentId>>
        blockers_by_attacker;
    for (const auto& [attacker_id, blocker_id] : blocks) {
        blockers_by_attacker[attacker_id].push_back(blocker_id);
    }

    for (const PermanentId attacker_id : attackers) {
        auto* attacker =
            find_creature(state.players[attacking_player], attacker_id);
        if (!card_definition(creature_face(*attacker)).vigilance) {
            attacker->tapped = true;
        }
        const int attacker_power = creature_power(*attacker);
        const auto blocker_group = blockers_by_attacker.find(attacker_id);

        if (blocker_group == blockers_by_attacker.end()) {
            state.players[defending_player].life -=
                attacker_power;
            state.stats[attacking_player].damage_to_opponent +=
                static_cast<std::size_t>(attacker_power);
            // Hypnotic Specter: combat damage to a player forces a
            // discard (house determinism: the first card in hand).
            if (creature_face(*attacker) ==
                    CardId::HypnoticSpecter &&
                attacker_power > 0 &&
                !state.players[defending_player].hand.empty()) {
                auto& victim = state.players[defending_player];
                victim.graveyard.push_back(victim.hand.front());
                victim.hand.erase(victim.hand.begin());
            }
            continue;
        }

        int attacker_damage = 0;
        for (const PermanentId blocker_id : blocker_group->second) {
            const auto* blocker =
                find_creature(state.players[defending_player], blocker_id);
            attacker_damage += creature_power(*blocker);
        }
        attacker->damage += attacker_damage;

        int damage_remaining = attacker_power;
        for (const PermanentId blocker_id : blocker_group->second) {
            auto* blocker =
                find_creature(state.players[defending_player], blocker_id);
            const int lethal_damage =
                std::max(0, creature_toughness(*blocker) -
                                blocker->damage);
            const int assigned_damage =
                std::min(damage_remaining, lethal_damage);
            blocker->damage += assigned_damage;
            damage_remaining -= assigned_damage;
        }
    }

    remove_dead_creatures(state.players[attacking_player]);
    remove_dead_creatures(state.players[defending_player]);
    return true;
}

bool default_mulligan_choice(const std::vector<CardId>& hand) {
    if (hand.size() < 6) {
        return false;
    }
    std::size_t sources = 0;
    for (const CardId card : hand) {
        const auto& definition = card_definition(card);
        if (definition.type == CardType::Land ||
            card == CardId::MoxSapphire || card == CardId::MoxPearl ||
            card == CardId::MoxRuby || card == CardId::MoxEmerald ||
            card == CardId::MoxJet || card == CardId::FellwarStone ||
            card == CardId::SolRing || card == CardId::BlackLotus ||
            card == CardId::LlanowarElves) {
            ++sources;
        }
    }
    return sources < 2 || sources > 5;
}

bool handcrafted_mulligan_choice(const std::vector<CardId>& hand,
                                 const std::vector<CardId>& deck) {
    if (hand.size() <= 4) {
        return false;
    }
    const auto count_in = [](const std::vector<CardId>& cards,
                             CardId card) {
        return std::count(cards.begin(), cards.end(), card);
    };
    const bool lotus_combo_deck =
        count_in(deck, CardId::BlackLotus) > 0 &&
        count_in(deck, CardId::Channel) > 0 &&
        count_in(deck, CardId::Disintegrate) > 0;
    if (lotus_combo_deck) {
        // Dig for the kill: Lotus, Lotus, Channel, Disintegrate.
        return count_in(hand, CardId::BlackLotus) < 2 ||
               count_in(hand, CardId::Channel) < 1 ||
               count_in(hand, CardId::Disintegrate) < 1;
    }
    std::size_t sources = 0;
    for (const CardId card : hand) {
        if (card_definition(card).type == CardType::Land ||
            card == CardId::MoxSapphire || card == CardId::MoxPearl ||
            card == CardId::MoxRuby || card == CardId::MoxEmerald ||
            card == CardId::MoxJet || card == CardId::FellwarStone ||
            card == CardId::SolRing || card == CardId::BlackLotus) {
            ++sources;
        }
    }
    // Land requirement follows the deck's curve: an all-cheap deck
    // (nothing above two mana - Burn) is happy on two sources; a
    // normal curve wants three at seven cards. Both ease one per
    // mulligan so the Paris ladder converges.
    int deck_top_cost = 0;
    for (const CardId card : deck) {
        const auto& definition = card_definition(card);
        if (definition.type != CardType::Land) {
            const ManaCost& cost = definition.cost;
            deck_top_cost = std::max(
                deck_top_cost, cost.generic + cost.green + cost.red +
                                   cost.blue + cost.white +
                                   cost.black);
        }
    }
    const std::size_t baseline = deck_top_cost <= 2 ? 2 : 3;
    const std::size_t needed =
        hand.size() >= 7 ? baseline
        : hand.size() == 6
            ? std::max<std::size_t>(1, baseline - 1)
            : 1;
    return sources < needed;
}

void begin_turn(GameState& state, std::size_t player) {
    for (auto& participant : state.players) {
        participant.mana_pool = {};
    }
    auto& player_state = state.players.at(player);
    player_state.land_played_this_turn = false;
    // Mana Drain: the countered spell's mana arrives at this turn's
    // beginning and lasts through the first main phase.
    if (player_state.pending_mana > 0) {
        player_state.mana_pool.generic += player_state.pending_mana;
        player_state.pending_mana = 0;
    }
    // Upkeep triggers, resolved deterministically at turn start:
    // each Serendib Efreet stings its controller; each opposing Black
    // Vise squeezes the active player for cards above four in hand.
    for (const auto& creature : player_state.creatures) {
        if (creature_face(creature) == CardId::JuzamDjinn) {
            player_state.life -= 1;
        }
        if (creature.card == CardId::SerendibEfreet) {
            player_state.life -= 1;
        }
    }
    const auto& opponent_state = state.players.at(opponent_of(player));
    for (const auto& artifact : opponent_state.artifacts) {
        if (artifact_face(artifact) == CardId::BlackVise) {
            player_state.life -= std::max(
                0, static_cast<int>(player_state.hand.size()) - 4);
        }
    }
    for (auto& land : player_state.lands) {
        land.tapped = false;
        land.entered_this_turn = false;
    }
    for (auto& creature : player_state.creatures) {
        creature.tapped = false;
        creature.summoning_sick = false;
    }
    for (auto& artifact : player_state.artifacts) {
        artifact.tapped = false;
    }
}

std::vector<CardId> cleanup_turn(
    GameState& state, std::size_t active_player,
    const std::vector<std::size_t>& discard_indices) {
    if (active_player >= state.players.size()) {
        throw std::out_of_range(
            "cleanup active player must be 0 or 1");
    }
    if (active_player != state.active_player) {
        throw std::invalid_argument(
            "cleanup may discard only for the active player");
    }

    const auto& active_hand =
        state.players[active_player].hand;
    const std::size_t excess =
        active_hand.size() > kMaximumHandSize
            ? active_hand.size() - kMaximumHandSize
            : 0;
    if (discard_indices.size() != excess) {
        throw std::invalid_argument(
            "cleanup must discard exactly the excess hand cards");
    }

    std::vector<std::size_t> ordered_indices =
        discard_indices;
    std::sort(ordered_indices.begin(), ordered_indices.end());
    if (std::adjacent_find(
            ordered_indices.begin(),
            ordered_indices.end()) != ordered_indices.end()) {
        throw std::invalid_argument(
            "cleanup discard positions must be unique");
    }
    if (!ordered_indices.empty() &&
        ordered_indices.back() >= active_hand.size()) {
        throw std::invalid_argument(
            "cleanup discard position is outside the hand");
    }

    std::vector<CardId> discarded;
    discarded.reserve(excess);
    std::vector<CardId> retained;
    retained.reserve(active_hand.size() - excess);
    std::size_t discard_cursor = 0;
    for (std::size_t hand_index = 0;
         hand_index < active_hand.size(); ++hand_index) {
        if (discard_cursor < ordered_indices.size() &&
            ordered_indices[discard_cursor] == hand_index) {
            discarded.push_back(active_hand[hand_index]);
            ++discard_cursor;
        } else {
            retained.push_back(active_hand[hand_index]);
        }
    }

    // Complete all potentially allocating work before mutating the state so
    // malformed input (and allocation failures) cannot produce a partial
    // cleanup.
    std::vector<CardId> graveyard =
        state.players[active_player].graveyard;
    graveyard.insert(
        graveyard.end(), discarded.begin(), discarded.end());
    state.players[active_player].hand = std::move(retained);
    state.players[active_player].graveyard =
        std::move(graveyard);

    // Cleanup discards precede the removal of damage and until-end-of-turn
    // effects.
    for (auto& player : state.players) {
        player.mana_pool = {};
        player.channel_active = false;
        for (std::size_t index = player.creatures.size();
             index-- > 0;) {
            if (creature_face(player.creatures[index]) ==
                CardId::MishrasFactory) {
                LandPermanent reverted{
                    .card = player.creatures[index].card,
                    .tapped = player.creatures[index].tapped,
                    .id = player.creatures[index].id};
                reverted.copy_of = player.creatures[index].copy_of;
                reverted.is_copy = player.creatures[index].is_copy;
                player.lands.push_back(reverted);
                player.creatures.erase(
                    player.creatures.begin() +
                    static_cast<std::ptrdiff_t>(index));
            }
        }
        for (auto& creature : player.creatures) {
            creature.damage = 0;
            creature.temporary_power_bonus = 0;
            creature.temporary_toughness_bonus = 0;
            creature.exile_on_death_this_turn = false;
        }
    }
    return discarded;
}

PlayerObservation observe_game_state(const GameState& state,
                                     std::size_t observer) {
    if (observer >= state.players.size()) {
        throw std::out_of_range("observer must be 0 or 1");
    }

    PlayerObservation observation{
        .observer = observer,
        .players = {},
        .hand = state.players[observer].hand,
        .revealed_opponent_hand = std::nullopt,
        .stack = state.stack,
        .extra_turns_pending = state.extra_turns_pending,
        .active_player = state.active_player,
        .starting_player = state.starting_player,
        .turn_number = state.turn_number,
    };
    for (std::size_t player = 0; player < state.players.size(); ++player) {
        const auto& source = state.players[player];
        observation.players[player] = {
            .life = source.life,
            .channel_active = source.channel_active,
            .library_size = source.library.size(),
            .hand_size = source.hand.size(),
            .graveyard = source.graveyard,
            .exile = source.exile,
            .lands = source.lands,
            .creatures = source.creatures,
            .artifacts = source.artifacts,
            .enchantments = source.enchantments,
            .mana_pool = source.mana_pool,
            .pending_mana = source.pending_mana,
            .land_played_this_turn = source.land_played_this_turn,
        };
    }
    return observation;
}

Game::Game(std::vector<CardId> player_zero_deck,
           std::vector<CardId> player_one_deck, std::uint64_t seed,
           GameConfig config)
    : decks_({std::move(player_zero_deck), std::move(player_one_deck)}),
      random_(seed), config_(config) {
    if (config_.starting_player.has_value() &&
        *config_.starting_player >= state_.players.size()) {
        throw std::invalid_argument("starting player must be 0 or 1");
    }
    if (config_.max_turns == 0) {
        throw std::invalid_argument("maximum turns must be positive");
    }
    for (const auto& bot : config_.bots) {
        if ((bot.kind == BotKind::MonteCarlo ||
             bot.kind == BotKind::DeepMonteCarlo) &&
            bot.rollouts_per_action == 0) {
            throw std::invalid_argument(
                "Monte Carlo rollouts per action must be positive");
        }
    }
    for (const auto& controller : config_.human_controllers) {
        if (!controller.has_value()) {
            continue;
        }
        if (!controller->choose_priority_action ||
            !controller->choose_attackers ||
            !controller->choose_blockers ||
            !controller->choose_damage_order ||
            !controller->choose_cleanup_discards) {
            throw std::invalid_argument(
                "human controller requires every decision callback");
        }
    }
}

const HumanController*
Game::human_controller(std::size_t player) const {
    if (player >= config_.human_controllers.size() ||
        !config_.human_controllers[player].has_value()) {
        return nullptr;
    }
    return &*config_.human_controllers[player];
}

PlayerObservation
Game::human_observation(std::size_t player) const {
    PlayerObservation observation =
        observe_game_state(state_, player);
    const auto* controller = human_controller(player);
    if (controller != nullptr &&
        controller->reveal_opponent_hand) {
        observation.revealed_opponent_hand =
            state_.players[1 - player].hand;
    }
    return observation;
}

bool Game::has_human_observer() const {
    return std::any_of(
        config_.human_controllers.begin(),
        config_.human_controllers.end(),
        [](const std::optional<HumanController>& controller) {
            return controller.has_value() &&
                   static_cast<bool>(controller->observe);
        });
}

void Game::notify_human_observers(const GameEvent& event) const {
    for (std::size_t player = 0;
         player < config_.human_controllers.size(); ++player) {
        const auto* controller = human_controller(player);
        if (controller != nullptr && controller->observe) {
            controller->observe(human_observation(player), event);
        }
    }
}

std::vector<std::size_t>
Game::choose_cleanup_discards(std::size_t player,
                              std::size_t excess) {
    if (player >= state_.players.size()) {
        throw std::out_of_range(
            "cleanup player must be 0 or 1");
    }
    const auto& hand = state_.players[player].hand;
    if (hand.size() <= kMaximumHandSize ||
        excess != hand.size() - kMaximumHandSize) {
        throw std::invalid_argument(
            "cleanup discard count does not match hand size");
    }

    ++state_.stats[player].decisions;
    if (const auto* controller = human_controller(player);
        controller != nullptr) {
        return controller->choose_cleanup_discards(
            human_observation(player), excess);
    }

    const BotKind kind = config_.bots[player].kind;
    if (kind == BotKind::Handcrafted) {
        std::vector<std::size_t> positions(hand.size());
        std::iota(positions.begin(), positions.end(), 0);
        std::stable_sort(
            positions.begin(), positions.end(),
            [&hand](std::size_t left, std::size_t right) {
                return handcrafted_card_value(hand[left]) <
                       handcrafted_card_value(hand[right]);
            });
        positions.resize(excess);
        std::sort(positions.begin(), positions.end());
        return positions;
    }

    // Random and rollout-based policies use an unbiased legal cleanup
    // choice. The seeded game RNG keeps the choice reproducible.
    std::vector<std::size_t> positions(hand.size());
    std::iota(positions.begin(), positions.end(), 0);
    std::shuffle(
        positions.begin(), positions.end(), random_);
    positions.resize(excess);
    std::sort(positions.begin(), positions.end());
    return positions;
}

void Game::perform_cleanup() {
    const std::size_t active_player = state_.active_player;
    if (active_player >= state_.players.size()) {
        throw std::logic_error(
            "game cleanup has an invalid active player");
    }
    const std::size_t hand_size =
        state_.players[active_player].hand.size();
    const std::size_t excess =
        hand_size > kMaximumHandSize
            ? hand_size - kMaximumHandSize
            : 0;
    std::vector<std::size_t> selected;
    if (excess != 0) {
        selected =
            choose_cleanup_discards(active_player, excess);
    }
    const std::vector<CardId> discarded =
        cleanup_turn(state_, active_player, selected);
    if (!discarded.empty() && has_human_observer()) {
        notify_human_observers({
            .kind = GameEventKind::CardsDiscarded,
            .player = active_player,
            .phase = TurnPhase::SecondMain,
            .cards = discarded,
        });
    }
}

void Game::initialize() {
    state_ = GameState{};
    setup_result_.reset();

    if (config_.starting_player.has_value()) {
        state_.starting_player = *config_.starting_player;
    } else {
        std::uniform_int_distribution<std::size_t> choose_player(0, 1);
        state_.starting_player = choose_player(random_);
    }

    for (std::size_t player = 0; player < state_.players.size(); ++player) {
        state_.players[player].library = decks_[player];
        std::shuffle(state_.players[player].library.begin(),
                     state_.players[player].library.end(), random_);
    }

    for (int card = 0; card < 7; ++card) {
        for (std::size_t player = 0; player < state_.players.size(); ++player) {
            if (!draw_card(player)) {
                setup_result_ =
                    make_result(static_cast<int>(opponent_of(player)),
                                EndReason::EmptyLibrary);
                return;
            }
        }
    }

    // Paris mulligans: starting player decides first each round; a
    // mulligan reshuffles the hand and draws one card fewer.
    const std::array<std::size_t, 2> order = {
        state_.starting_player, opponent_of(state_.starting_player)};
    std::array<bool, 2> kept = {false, false};
    while (!(kept[0] && kept[1])) {
        bool any_mulligan = false;
        for (const std::size_t player : order) {
            if (kept[player]) {
                continue;
            }
            auto& player_state = state_.players[player];
            if (player_state.hand.empty()) {
                kept[player] = true;
                continue;
            }
            bool mulligan = false;
            if (const auto* controller = human_controller(player)) {
                mulligan = controller->choose_mulligan &&
                           controller->choose_mulligan(
                               human_observation(player));
            } else if (config_.bots[player].kind ==
                       BotKind::Handcrafted) {
                mulligan = handcrafted_mulligan_choice(
                    player_state.hand, decks_[player]);
            } else {
                mulligan = default_mulligan_choice(player_state.hand);
            }
            if (!mulligan) {
                kept[player] = true;
                continue;
            }
            const std::size_t next_size = player_state.hand.size() - 1;
            player_state.library.insert(player_state.library.end(),
                                        player_state.hand.begin(),
                                        player_state.hand.end());
            player_state.hand.clear();
            std::shuffle(player_state.library.begin(),
                         player_state.library.end(), random_);
            for (std::size_t card = 0; card < next_size; ++card) {
                draw_card(player);
            }
            ++state_.stats[player].mulligans;
            any_mulligan = true;
            if (has_human_observer()) {
                notify_human_observers({
                    .kind = GameEventKind::MulliganTaken,
                    .player = player,
                });
            }
        }
        if (!any_mulligan) {
            break;
        }
    }
}

bool Game::draw_card(std::size_t player) {
    auto& player_state = state_.players[player];
    if (player_state.library.empty()) {
        return false;
    }
    player_state.hand.push_back(player_state.library.back());
    player_state.library.pop_back();
    ++state_.stats[player].cards_drawn;
    return true;
}

GameResult Game::make_result(int winner, EndReason reason) const {
    return {
        .winner = winner,
        .reason = reason,
        .turns = state_.turn_number,
        .starting_player = state_.starting_player,
        .ending_life = {
            state_.players[0].life,
            state_.players[1].life,
        },
        .player_stats = state_.stats,
        .bots = {
            config_.bots[0].kind,
            config_.bots[1].kind,
        },
    };
}

std::optional<GameResult> Game::life_total_result() const {
    if (state_.failed_draw[0] || state_.failed_draw[1]) {
        int winner = -1;
        if (state_.failed_draw[0] != state_.failed_draw[1]) {
            winner = state_.failed_draw[0] ? 1 : 0;
        }
        return make_result(winner, EndReason::EmptyLibrary);
    }

    const bool player_zero_lost = state_.players[0].life <= 0;
    const bool player_one_lost = state_.players[1].life <= 0;
    if (!player_zero_lost && !player_one_lost) {
        return std::nullopt;
    }

    int winner = -1;
    if (player_zero_lost != player_one_lost) {
        winner = player_zero_lost ? 1 : 0;
    }
    return make_result(winner, EndReason::LifeTotal);
}

std::optional<GameResult>
Game::play_priority_window(bool sorcery_actions, TurnPhase phase) {
    PriorityState priority = {
        .player = state_.active_player,
        .consecutive_passes = 0,
    };
    return continue_priority_window(
        sorcery_actions, phase, priority);
}

std::optional<GameResult>
Game::continue_priority_window(bool sorcery_actions,
                               TurnPhase phase,
                               PriorityState priority) {
    const bool notify_observers = has_human_observer();
    while (true) {
        auto actions =
            legal_priority_actions(state_, priority.player,
                                   sorcery_actions);
        const HumanController* controller =
            human_controller(priority.player);
        if (controller != nullptr && controller->offers_mana_taps) {
            // Manual mana floating for the arena's human seat: one
            // option per producible face of each untapped land. Bot
            // seats and SPZ search never see these.
            static constexpr std::array<int ManaCost::*, 6> kFaces = {
                &ManaCost::generic, &ManaCost::green, &ManaCost::red,
                &ManaCost::blue,    &ManaCost::white, &ManaCost::black,
            };
            for (const auto& land :
                 state_.players[priority.player].lands) {
                if (land.tapped) {
                    continue;
                }
                if (land_face(land) != CardId::CityOfBrass &&
                    land_mana(land_face(land)).generic > 0) {
                    actions.push_back(
                        PriorityAction::tap_land_for_mana(land.id, 0));
                }
                for (std::size_t face = 1; face < kFaces.size();
                     ++face) {
                    if (land_provides(land_face(land),
                                      kFaces[face])) {
                        actions.push_back(
                            PriorityAction::tap_land_for_mana(
                                land.id, static_cast<int>(face)));
                    }
                }
            }
            for (const auto& artifact :
                 state_.players[priority.player].artifacts) {
                if (artifact.tapped) {
                    continue;
                }
                const CardId artifact_card =
                    artifact.is_copy ? artifact.copy_of
                                     : artifact.card;
                if (artifact_card == CardId::FellwarStone) {
                    for (std::size_t face = 1; face < kFaces.size();
                         ++face) {
                        actions.push_back(
                            PriorityAction::tap_land_for_mana(
                                artifact.id,
                                static_cast<int>(face)));
                    }
                    continue;
                }
                for (std::size_t face = 0; face < kFaces.size();
                     ++face) {
                    if (artifact_mana(artifact_card).*kFaces[face] >
                        0) {
                        actions.push_back(
                            PriorityAction::tap_land_for_mana(
                                artifact.id,
                                static_cast<int>(face)));
                    }
                }
            }
            for (const auto& creature :
                 state_.players[priority.player].creatures) {
                if (creature.card == CardId::LlanowarElves &&
                    !creature.tapped && !creature.summoning_sick) {
                    actions.push_back(
                        PriorityAction::tap_land_for_mana(
                            creature.id, 1));
                }
            }
        }
        if (controller != nullptr &&
            controller->offers_payment_variants) {
            // Payment-plan branching: every cast also appears with up
            // to two explicit alternative payments, so the seat's
            // search can judge WHICH sources to spend, not just what
            // to cast.
            const auto action_cost =
                [](const PriorityAction& action)
                -> std::optional<ManaCost> {
                switch (action.kind) {
                case PriorityActionKind::CastDisintegrate:
                    return disintegrate_cost(action.x_value);
                case PriorityActionKind::CastBraingeyser:
                    return braingeyser_cost(action.x_value);
                case PriorityActionKind::CastMindTwist:
                    return mind_twist_cost(action.x_value);
                case PriorityActionKind::CastRecall:
                    return recall_cost(action.x_value);
                case PriorityActionKind::ActivateMillstone:
                    return kMillstoneActivationCost;
                case PriorityActionKind::ActivateMishrasFactory:
                    return ManaCost{.generic = 1};
                case PriorityActionKind::Pass:
                case PriorityActionKind::PlayLand:
                case PriorityActionKind::ActivateLibrary:
                case PriorityActionKind::ActivateStripMine:
                case PriorityActionKind::ActivateSage:
                case PriorityActionKind::ActivateTriskelion:
                case PriorityActionKind::TapLandForMana:
                    return std::nullopt;
                default: {
                    const ManaCost cost =
                        card_definition(action.card).cost;
                    return mana_total(cost) > 0
                               ? std::optional<ManaCost>(cost)
                               : std::nullopt;
                }
                }
            };
            const std::size_t base_count = actions.size();
            for (std::size_t index = 0; index < base_count; ++index) {
                const auto cost = action_cost(actions[index]);
                if (!cost.has_value()) {
                    continue;
                }
                for (auto& taps : alternative_payments(
                         state_.players[priority.player], *cost, 3)) {
                    PriorityAction variant = actions[index];
                    variant.pre_taps = std::move(taps);
                    actions.push_back(std::move(variant));
                }
            }
        }
        const PriorityAction action =
            choose_priority_action(actions, priority.player,
                                   sorcery_actions, phase);
        if (action.kind == PriorityActionKind::Pass) {
            if (notify_observers) {
                notify_human_observers({
                    .kind =
                        GameEventKind::PriorityActionSelected,
                    .player = priority.player,
                    .phase = phase,
                    .priority_action = action,
                });
            }
            const std::optional<StackObject> resolving =
                !notify_observers || state_.stack.empty()
                    ? std::nullopt
                    : std::optional<StackObject>(
                          state_.stack.back());
            const PriorityPassResult pass =
                pass_priority(state_, priority);
            if (pass == PriorityPassResult::Passed) {
                continue;
            }
            if (pass == PriorityPassResult::WindowEnded) {
                return std::nullopt;
            }
            if (notify_observers) {
                if (!resolving.has_value()) {
                    throw std::logic_error(
                        "stack resolved without a stack object");
                }
                notify_human_observers({
                    .kind = GameEventKind::StackObjectResolved,
                    .player = resolving->controller,
                    .phase = phase,
                    .stack_object = resolving,
                });
            }
            if (const auto result = life_total_result();
                result.has_value()) {
                return result;
            }
            continue;
        }

        if (!apply_priority_action(state_, priority.player, action,
                                   sorcery_actions)) {
            {
                std::string detail =
                    "bot policy selected an illegal action: kind " +
                    std::to_string(static_cast<int>(action.kind)) +
                    " card " +
                    std::to_string(static_cast<int>(action.card)) +
                    " lands";
                for (const auto& land :
                     state_.players[priority.player].lands) {
                    detail += ' ';
                    detail +=
                        std::to_string(static_cast<int>(land.card)) +
                        (land.tapped ? "T" : "u");
                }
                detail += " artifacts";
                for (const auto& artifact :
                     state_.players[priority.player].artifacts) {
                    detail += ' ';
                    detail += std::to_string(
                                  static_cast<int>(artifact.card)) +
                              (artifact.tapped ? "T" : "u");
                }
                detail += " pool g" +
                          std::to_string(state_.players[priority.player]
                                             .mana_pool.generic);
                throw std::logic_error(detail);
            }
        }
        if (notify_observers) {
            notify_human_observers({
                .kind =
                    GameEventKind::PriorityActionSelected,
                .player = priority.player,
                .phase = phase,
                .priority_action = action,
            });
        }
        // The player who acted receives priority again.
        priority.consecutive_passes = 0;
    }
}

PriorityAction Game::choose_priority_action(
    const std::vector<PriorityAction>& actions, std::size_t player,
    bool sorcery_actions, TurnPhase phase) {
    if (actions.empty()) {
        throw std::logic_error("priority window has no pass action");
    }
    if (trace_ != nullptr && actions.size() > 1) {
        const bool stack_choice = !state_.stack.empty();
        const bool activated_ability_choice = std::any_of(
            actions.begin(), actions.end(),
            [](const PriorityAction& action) {
                return action.kind ==
                       PriorityActionKind::ActivateMillstone;
            });
        if (stack_choice || activated_ability_choice) {
            trace_->push_back(state_);
        }
    }
    const auto* controller = human_controller(player);
    if (actions.size() == 1 &&
        (controller == nullptr || !controller->bluff_mode)) {
        return actions.front();
    }

    if (actions.size() > 1) {
        ++state_.stats[player].decisions;
    }
    if (controller != nullptr) {
        const std::size_t chosen =
            controller->choose_priority_action(
                human_observation(player), phase,
                actions);
        if (chosen >= actions.size()) {
            throw std::invalid_argument(
                "human selected an invalid priority action");
        }
        return actions[chosen];
    }
    const auto& bot = config_.bots[player];
    if (bot.kind == BotKind::Random) {
        std::uniform_int_distribution<std::size_t> choose_action(
            0, actions.size() - 1);
        const std::size_t chosen = choose_action(random_);
        return actions[chosen];
    }
    if (bot.kind == BotKind::Handcrafted) {
        return choose_handcrafted_action(actions, player, phase);
    }

    std::vector<double> scores(actions.size(), 0.0);
    for (std::size_t action_index = 0; action_index < actions.size();
         ++action_index) {
        for (std::size_t rollout = 0;
             rollout < bot.rollouts_per_action; ++rollout) {
            scores[action_index] +=
                rollout_action(actions[action_index], player,
                               sorcery_actions, random_());
        }
    }
    state_.stats[player].monte_carlo_rollouts +=
        actions.size() * bot.rollouts_per_action;

    const double best_score =
        *std::max_element(scores.begin(), scores.end());
    std::vector<std::size_t> best_actions;
    for (std::size_t action_index = 0; action_index < scores.size();
         ++action_index) {
        if (scores[action_index] == best_score) {
            best_actions.push_back(action_index);
        }
    }
    std::uniform_int_distribution<std::size_t> break_tie(
        0, best_actions.size() - 1);
    return actions[best_actions[break_tie(random_)]];
}

PriorityAction Game::choose_handcrafted_action(
    const std::vector<PriorityAction>& actions, std::size_t player,
    TurnPhase phase) {
    std::vector<double> scores;
    scores.reserve(actions.size());
    for (const auto& action : actions) {
        scores.push_back(
            handcrafted_action_score(action, player, phase));
    }

    const double best_score =
        *std::max_element(scores.begin(), scores.end());
    std::vector<std::size_t> best_actions;
    for (std::size_t index = 0; index < scores.size(); ++index) {
        if (scores[index] == best_score) {
            best_actions.push_back(index);
        }
    }
    std::uniform_int_distribution<std::size_t> break_tie(
        0, best_actions.size() - 1);
    return actions[best_actions[break_tie(random_)]];
}

double Game::handcrafted_action_score(const PriorityAction& action,
                                      std::size_t player,
                                      TurnPhase phase) const {
    const auto& player_state = state_.players[player];
    const std::size_t opponent = opponent_of(player);
    const auto& opponent_state = state_.players[opponent];

    // Greedy ceiling on face damage castable this turn from hand burn:
    // each Lightning Bolt is three damage for one mana, one Disintegrate
    // then spends whatever mana remains (X plus its red). The mana
    // ceiling already counts Black Lotuses and an active Channel's life.
    const auto potential_burn_damage = [](const PlayerState& caster) {
        int mana = maximum_available_mana(caster);
        int damage = 0;
        int bolts = 0;
        bool holds_disintegrate = false;
        for (const CardId card : caster.hand) {
            bolts += card == CardId::LightningBolt ? 1 : 0;
            holds_disintegrate =
                holds_disintegrate || card == CardId::Disintegrate;
        }
        const int cast_bolts = std::min(bolts, mana);
        damage += 3 * cast_bolts;
        mana -= cast_bolts;
        if (holds_disintegrate && mana >= 2) {
            damage += mana - 1;
        }
        return damage;
    };
    const bool burn_lethal_this_turn =
        potential_burn_damage(player_state) >= opponent_state.life;

    switch (action.kind) {
    case PriorityActionKind::TapLandForMana:
        // Human-only manual float; bots never receive it.
        return -10'000.0;
    case PriorityActionKind::ActivateChaosOrb: {
        // One shot: spend it on the opponent's best permanent.
        if (!action.target.has_value() ||
            action.target->player != opponent) {
            return -400.0;
        }
        double best = 250.0;
        if (action.target->creature.has_value()) {
            const PermanentId id = *action.target->creature;
            for (const auto& creature : opponent_state.creatures) {
                if (creature.id == id) {
                    best = 400.0 +
                           handcrafted_card_value(creature.card) /
                               2.0;
                }
            }
            for (const auto& land : opponent_state.lands) {
                if (land.id == id) {
                    best = 150.0 +
                           handcrafted_card_value(land.card) / 2.0;
                }
            }
            for (const auto& artifact : opponent_state.artifacts) {
                if (artifact.id == id) {
                    best = 300.0 +
                           handcrafted_card_value(artifact.card) /
                               2.0;
                }
            }
        } else {
            const auto index =
                static_cast<std::size_t>(action.x_value);
            if (index < opponent_state.enchantments.size()) {
                best = 300.0 +
                       handcrafted_card_value(
                           opponent_state.enchantments[index]) /
                           2.0;
            }
        }
        return best;
    }
    case PriorityActionKind::ActivateJalumTome:
        // Card filtering: fine when mana is spare.
        return 350.0;
    case PriorityActionKind::CastDarkRitual:
        // Only worthwhile as a ramp into a real play; the searchers
        // judge that. For the rules bot, mild positive when the hand
        // still holds an expensive spell.
        for (const CardId held : player_state.hand) {
            const auto& held_cost = card_definition(held).cost;
            if (held_cost.generic + held_cost.black >= 3 &&
                held != CardId::DarkRitual) {
                return 800.0;
            }
        }
        return -300.0;
    case PriorityActionKind::CastShatter: {
        if (!action.target.has_value() ||
            !action.target->creature.has_value() ||
            action.target->player == player) {
            return -400.0;
        }
        for (const auto& artifact : opponent_state.artifacts) {
            if (artifact.id == *action.target->creature) {
                return 500.0 +
                       handcrafted_card_value(
                           artifact_face(artifact)) /
                           2.0;
            }
        }
        return -400.0;
    }
    case PriorityActionKind::CastManaDrain:
        return 2'700.0;
    case PriorityActionKind::CastMindTwist:
        return 700.0 +
               180.0 * std::min<int>(
                           action.x_value,
                           static_cast<int>(
                               state_.players[opponent].hand.size()));
    case PriorityActionKind::CastRecall:
        return 550.0;
    case PriorityActionKind::CastDemonicTutor:
        return 2'000.0 +
               (action.chosen_card.has_value()
                    ? handcrafted_card_value(*action.chosen_card) /
                          2.0
                    : 0.0);
    case PriorityActionKind::CastCopyArtifact:
        return 900.0;
    case PriorityActionKind::ActivateSage: {
        // A card is worth roughly a thousand points; sacrificing a
        // better artifact than that is value destruction.
        if (action.target.has_value() &&
            action.target->creature.has_value()) {
            for (const auto& artifact : player_state.artifacts) {
                if (artifact.id == *action.target->creature) {
                    return 1'000.0 -
                           handcrafted_card_value(
                               artifact_face(artifact));
                }
            }
        }
        return -500.0;
    }
    case PriorityActionKind::ActivateTriskelion: {
        if (action.target.has_value() &&
            action.target->creature.has_value() &&
            action.target->player == opponent) {
            const auto* victim = find_creature(
                state_.players[opponent], *action.target->creature);
            if (victim != nullptr &&
                creature_toughness(*victim) - victim->damage <= 1) {
                return 2'000.0 +
                       handcrafted_card_value(victim->card) / 2.0;
            }
            return -300.0;
        }
        if (action.target.has_value() &&
            action.target->player == opponent &&
            state_.players[opponent].life <= 3) {
            return 9'000.0;
        }
        return -200.0;
    }
    case PriorityActionKind::Pass:
        if (!state_.stack.empty() &&
            state_.stack.back().controller == player) {
            return 5'000.0;
        }
        return state_.stack.empty() ? -10.0 : 0.0;

    case PriorityActionKind::PlayLand:
        {
            PlayerState with_land = player_state;
            with_land.lands.push_back({
                .card = action.card,
                .tapped = false,
            });
            double score = 4'000.0;
            for (const CardId card : player_state.hand) {
                const auto& definition = card_definition(card);
                if (definition.type == CardType::Land) {
                    continue;
                }
                if (!can_pay(player_state, definition.cost) &&
                    can_pay(with_land, definition.cost)) {
                    score +=
                        500.0 + handcrafted_card_value(card);
                }

                int matching_symbols = 0;
                switch (action.card) {
                case CardId::Forest:
                    matching_symbols = definition.cost.green;
                    break;
                case CardId::Mountain:
                    matching_symbols = definition.cost.red;
                    break;
                case CardId::Island:
                    matching_symbols = definition.cost.blue;
                    break;
                case CardId::Plains:
                    matching_symbols = definition.cost.white;
                    break;
                default:
                    break;
                }
                score += 25.0 *
                         static_cast<double>(matching_symbols);
            }
            return score;
        }

    case PriorityActionKind::CastCreature: {
        double score = 1'200.0 + handcrafted_card_value(action.card);
        const bool holding_counterspell =
            has_card(player_state.hand, CardId::Counterspell);
        const int available_mana =
            maximum_available_mana(player_state);
        const auto& cost = card_definition(action.card).cost;
        const int total_cost = cost.generic + cost.green + cost.red +
                               cost.blue + cost.white;
        if (holding_counterspell &&
            available_mana < total_cost + 2) {
            score -= 1'500.0;
        }
        return score;
    }

    case PriorityActionKind::CastSorcery: {
        if (action.card == CardId::TimeWalk) {
            return 4'500.0;
        }
        if (action.card == CardId::Channel) {
            if (player_state.channel_active) {
                return -1'000.0;
            }
            PlayerState channeled = player_state;
            channeled.channel_active = true;
            // Channel's own GG plus Disintegrate's red leave the rest as X.
            const int x_ceiling =
                maximum_available_mana(channeled) - 3;
            const bool holds_disintegrate =
                has_card(player_state.hand, CardId::Disintegrate);
            if (holds_disintegrate &&
                x_ceiling >= opponent_state.life) {
                return 9'800.0;
            }
            return -200.0;
        }
        if (action.card == CardId::Armageddon) {
            // Worth casting only from ahead: rocks survive the wrath
            // and a bigger board closes the game it stalls.
            const auto rocks = [](const PlayerState& side) {
                int count = 0;
                for (const auto& artifact : side.artifacts) {
                    const CardId face = artifact_face(artifact);
                    if (mana_total(artifact_mana(face)) > 0 ||
                        face == CardId::BlackLotus) {
                        ++count;
                    }
                }
                return count;
            };
            const auto board_power = [](const PlayerState& side) {
                int total = 0;
                for (const auto& creature : side.creatures) {
                    total += creature_power(creature);
                }
                return total;
            };
            return -600.0 +
                   500.0 * (rocks(player_state) -
                            rocks(opponent_state)) +
                   60.0 * (board_power(player_state) -
                           board_power(opponent_state));
        }
        if (action.card == CardId::WheelOfFortune ||
            action.card == CardId::Timetwister) {
            // Refill spells: strong with a spent hand, waste with a
            // full one. Count cards drawn beyond what is discarded.
            const int kept =
                static_cast<int>(player_state.hand.size()) - 1;
            return 250.0 * (7 - kept) - 200.0;
        }
        const auto count_islands = [](const PlayerState& state) {
            return std::count_if(
                state.lands.begin(), state.lands.end(),
                [](const LandPermanent& land) {
                    return land.card == CardId::Island;
                });
        };
        const auto enemy_islands = count_islands(opponent_state);
        const auto own_islands = count_islands(player_state);
        return 700.0 + 800.0 * static_cast<double>(enemy_islands) -
               800.0 * static_cast<double>(own_islands);
    }

    case PriorityActionKind::CastArtifact:
        if (action.card == CardId::MoxSapphire) {
            return 5'000.0;
        }
        if (action.card == CardId::SolRing) {
            return 4'500.0;
        }
        return 1'500.0;

    case PriorityActionKind::CastEnchantment: {
        const double battlefield_swing =
            250.0 * static_cast<double>(opponent_state.creatures.size()) -
            150.0 * static_cast<double>(player_state.creatures.size());
        return 1'800.0 + battlefield_swing;
    }

    case PriorityActionKind::CastLightningBolt:
        if (!action.target.has_value()) {
            return -10'000.0;
        }
        if (!action.target->creature.has_value()) {
            if (action.target->player == player) {
                return -10'000.0;
            }
            if (opponent_state.life <= 3) {
                return 10'000.0;
            }
            if (burn_lethal_this_turn) {
                return 9'500.0;
            }
            return 900.0 +
                   10.0 * static_cast<double>(20 - opponent_state.life);
        } else {
            if (action.target->player == player) {
                return -10'000.0;
            }
            const auto target = std::find_if(
                opponent_state.creatures.begin(),
                opponent_state.creatures.end(),
                [&](const CreaturePermanent& creature) {
                    return creature.id == *action.target->creature;
                });
            if (target == opponent_state.creatures.end()) {
                return -10'000.0;
            }
            const bool lethal =
                target->damage + card_definition(CardId::LightningBolt)
                                     .effect_damage >=
                creature_toughness(*target);
            if (!lethal) {
                // Chip damage on a creature is nearly always waste.
                return 150.0 +
                       handcrafted_card_value(target->card) / 4.0;
            }
            // Race-aware allocation: burn in hand wants faces, not
            // every two-drop. Kills stay premium only while the
            // opponent's board actually threatens to win first, or
            // when the target itself is a premium card.
            int their_clock = 0;
            for (const auto& creature : opponent_state.creatures) {
                if (!creature.tapped && !creature.summoning_sick) {
                    their_clock += creature_power(creature);
                }
            }
            const bool under_pressure =
                their_clock * 2 >= player_state.life;
            const double value =
                handcrafted_card_value(target->card);
            if (under_pressure || value >= 900.0 ||
                creature_power(*target) >= 3) {
                return 2'000.0 + value;
            }
            // Winning or even race, small target: below the face
            // line so the burn keeps racing.
            return 650.0 + value / 2.0;
        }

    case PriorityActionKind::CastDisintegrate:
        if (!action.target.has_value() ||
            action.target->player == player) {
            return -10'000.0;
        }
        if (action.x_value == 0) {
            return -100.0;
        }
        if (!action.target->creature.has_value()) {
            if (opponent_state.life <= action.x_value) {
                // Cheapest sufficient kill: extra X buys nothing and a
                // Channel-funded overpay can hand the response window a
                // lethal burn spell against us.
                return 10'000.0 -
                       10.0 * static_cast<double>(action.x_value);
            }
            if (burn_lethal_this_turn) {
                // Part of a lethal sequence, but never above the direct
                // kill: 9000 + 40 * X stays under any lethal X's score.
                return 9'000.0 +
                       40.0 * static_cast<double>(action.x_value);
            }
            if (player_state.channel_active) {
                // A non-lethal X funded by Channel life is suicide.
                return -5'000.0;
            }
            return 700.0 +
                   150.0 * static_cast<double>(action.x_value) +
                   10.0 *
                       static_cast<double>(20 - opponent_state.life);
        } else {
            const auto target = std::find_if(
                opponent_state.creatures.begin(),
                opponent_state.creatures.end(),
                [&](const CreaturePermanent& creature) {
                    return creature.id ==
                           *action.target->creature;
                });
            if (target == opponent_state.creatures.end()) {
                return -10'000.0;
            }
            const bool lethal =
                target->damage + action.x_value >=
                creature_toughness(*target);
            if (lethal) {
                return 2'000.0 +
                       handcrafted_card_value(target->card);
            }
            return 300.0 +
                   100.0 * static_cast<double>(action.x_value);
        }

    case PriorityActionKind::CastGiantGrowth:
        if (!action.target.has_value() ||
            !action.target->creature.has_value() ||
            action.target->player != player) {
            return -10'000.0;
        }
        {
            const auto target = std::find_if(
                player_state.creatures.begin(),
                player_state.creatures.end(),
                [&](const CreaturePermanent& creature) {
                    return creature.id ==
                           *action.target->creature;
                });
            if (target == player_state.creatures.end()) {
                return -10'000.0;
            }

            if (!state_.stack.empty()) {
                const auto& pending = state_.stack.back();
                if (pending.controller == opponent &&
                    pending.target.has_value() &&
                    pending.target->player == player &&
                    pending.target->creature ==
                        action.target->creature) {
                    int pending_damage = 0;
                    if (pending.card == CardId::LightningBolt) {
                        pending_damage =
                            card_definition(CardId::LightningBolt)
                                .effect_damage;
                    } else if (
                        pending.card == CardId::Disintegrate) {
                        pending_damage = pending.x_value;
                    }
                    const int damage_after_resolution =
                        target->damage + pending_damage;
                    if (damage_after_resolution >=
                            creature_toughness(*target) &&
                        damage_after_resolution <
                            creature_toughness(*target) + 3) {
                        return 9'000.0;
                    }
                }
            }

            if (state_.active_player == player) {
                int available_power = 0;
                for (const auto& creature :
                     player_state.creatures) {
                    if (!creature.tapped &&
                        !creature.summoning_sick &&
                        can_attack_through_moat(
                            state_, creature)) {
                        available_power +=
                            creature_power(creature);
                    }
                }
                const bool target_can_attack =
                    !target->tapped &&
                    !target->summoning_sick &&
                    can_attack_through_moat(state_, *target);
                if (phase == TurnPhase::BeginCombat &&
                    target_can_attack &&
                    available_power <
                        opponent_state.life &&
                    available_power + 3 >=
                        opponent_state.life) {
                    return 9'500.0;
                }
            }

            const bool target_can_attack =
                state_.active_player == player &&
                phase == TurnPhase::BeginCombat &&
                !target->tapped &&
                !target->summoning_sick &&
                can_attack_through_moat(state_, *target);
            if (!target_can_attack) {
                return -100.0;
            }
            return 1'200.0 +
                   100.0 *
                       static_cast<double>(
                           creature_power(*target)) +
                   200.0 *
                       static_cast<double>(target->damage);
        }

    case PriorityActionKind::CastCounterspell: {
        if (!action.spell_target.has_value()) {
            return -10'000.0;
        }
        const auto target = std::find_if(
            state_.stack.begin(), state_.stack.end(),
            [&](const StackObject& object) {
                return object.id == *action.spell_target;
            });
        if (target == state_.stack.end() ||
            target->controller == player) {
            return -10'000.0;
        }
        return 3'000.0 + handcrafted_card_value(target->card);
    }

    case PriorityActionKind::CastAncestralRecall:
        if (!action.target.has_value() ||
            action.target->creature.has_value()) {
            return -10'000.0;
        }
        if (action.target->player == player) {
            return 5'500.0;
        }
        return opponent_state.library.size() < 3
                   ? 10'000.0
                   : -10'000.0;

    case PriorityActionKind::CastBraingeyser:
        if (!action.target.has_value() ||
            action.target->creature.has_value() ||
            action.x_value < 0) {
            return -10'000.0;
        }
        if (action.x_value == 0) {
            return -100.0;
        }
        if (action.target->player == player) {
            return 1'800.0 +
                   350.0 * static_cast<double>(action.x_value);
        }
        return static_cast<std::size_t>(action.x_value) >
                       opponent_state.library.size()
                   ? 10'000.0
                   : -500.0;

    case PriorityActionKind::CastForceSpike: {
        if (!action.spell_target.has_value()) {
            return -10'000.0;
        }
        const auto target = std::find_if(
            state_.stack.begin(), state_.stack.end(),
            [&](const StackObject& object) {
                return object.id == *action.spell_target;
            });
        if (target == state_.stack.end() ||
            target->kind != StackObjectKind::Spell ||
            target->controller == player) {
            return -10'000.0;
        }
        if (can_pay(
                state_.players[target->controller],
                ManaCost{.generic = 1})) {
            return -100.0;
        }
        return 3'000.0 + handcrafted_card_value(target->card);
    }

    case PriorityActionKind::CastPsionicBlast:
        if (!action.target.has_value()) {
            return -10'000.0;
        }
        if (!action.target->creature.has_value()) {
            if (action.target->player == player) {
                return -10'000.0;
            }
            if (opponent_state.life <= 4) {
                return 10'000.0;
            }
            if (burn_lethal_this_turn) {
                return 9'400.0;
            }
            return 850.0 +
                   10.0 * static_cast<double>(20 - opponent_state.life);
        }
        if (action.target->player == player) {
            return -10'000.0;
        }
        {
            const auto blast_target = std::find_if(
                opponent_state.creatures.begin(),
                opponent_state.creatures.end(),
                [&](const CreaturePermanent& creature) {
                    return creature.id == *action.target->creature;
                });
            if (blast_target == opponent_state.creatures.end()) {
                return -10'000.0;
            }
            const bool lethal =
                blast_target->damage + 4 >=
                creature_toughness(*blast_target);
            return (lethal ? 1'900.0 : 450.0) +
                   handcrafted_card_value(blast_target->card);
        }

    case PriorityActionKind::CastSwordsToPlowshares:
        if (!action.target.has_value() ||
            !action.target->creature.has_value() ||
            action.target->player == player) {
            return -10'000.0;
        }
        {
            const auto swords_target = std::find_if(
                opponent_state.creatures.begin(),
                opponent_state.creatures.end(),
                [&](const CreaturePermanent& creature) {
                    return creature.id == *action.target->creature;
                });
            if (swords_target == opponent_state.creatures.end()) {
                return -10'000.0;
            }
            return 2'100.0 +
                   handcrafted_card_value(swords_target->card);
        }

    case PriorityActionKind::CastDisenchant:
        if (!action.target.has_value() ||
            action.target->player == player) {
            return -10'000.0;
        }
        return 1'400.0;

    case PriorityActionKind::ActivateLibrary:
        return 4'200.0;

    case PriorityActionKind::ActivateMishrasFactory:
        // Animate before combat when attacking is plausible.
        return phase == TurnPhase::FirstMain &&
                       state_.active_player == player
                   ? 900.0
                   : -200.0;

    case PriorityActionKind::ActivateStripMine:
        if (!action.target.has_value() ||
            action.target->player == player) {
            return -10'000.0;
        }
        return 650.0;

    case PriorityActionKind::ActivateMillstone:
        if (!action.target.has_value() ||
            action.target->player == player) {
            return -10'000.0;
        }
        if (opponent_state.library.size() <= 2) {
            return 10'000.0;
        }
        return 1'600.0 +
               static_cast<double>(40 - opponent_state.library.size()) *
                   10.0;
    }
    return -10'000.0;
}

double Game::rollout_action(const PriorityAction& action,
                            std::size_t player, bool sorcery_actions,
                            std::uint64_t seed) const {
    Game rollout = *this;
    rollout.random_.seed(seed);
    rollout.config_.human_controllers = {};
    rollout.config_.bots = {
        BotConfig{.kind = BotKind::Random},
        BotConfig{.kind = BotKind::Random},
    };

    // The order of each library is hidden information. Re-randomizing it makes
    // each rollout a separate determinization instead of letting the bot peek
    // at the already-shuffled future.
    for (auto& player_state : rollout.state_.players) {
        std::shuffle(player_state.library.begin(),
                     player_state.library.end(), rollout.random_);
    }

    if (action.kind != PriorityActionKind::Pass &&
        !apply_priority_action(rollout.state_, player, action,
                               sorcery_actions)) {
        return -std::numeric_limits<double>::infinity();
    }

    // Resolve the candidate and anything already below it, then use a complete
    // random continuation from the following turn. This keeps rollout cost
    // bounded while the real game still uses normal stack priority.
    while (!rollout.state_.stack.empty()) {
        if (!resolve_top_of_stack(rollout.state_)) {
            throw std::logic_error("rollout failed to resolve the stack");
        }
        if (const auto result = rollout.life_total_result();
            result.has_value()) {
            if (result->winner < 0) {
                return 0.5;
            }
            return result->winner == static_cast<int>(player) ? 1.0 : 0.0;
        }
    }

    rollout.perform_cleanup();
    const GameResult result =
        rollout.run_from_turn(rollout.state_.turn_number + 1);
    if (result.winner < 0) {
        return 0.5;
    }
    return result.winner == static_cast<int>(player) ? 1.0 : 0.0;
}

std::optional<GameResult> Game::play_combat() {
    if (const auto result = play_priority_window(
            false, TurnPhase::BeginCombat);
        result.has_value()) {
        return result;
    }
    return play_combat_after_beginning();
}

std::optional<GameResult> Game::play_combat_after_beginning() {
    auto& attacking_state = state_.players[state_.active_player];
    const std::size_t defending_player = opponent_of(state_.active_player);
    auto& defending_state = state_.players[defending_player];

    std::vector<PermanentId> attackers;
    const bool handcrafted_attacker =
        config_.bots[state_.active_player].kind == BotKind::Handcrafted;
    const auto* human_attacker =
        human_controller(state_.active_player);
    if (human_attacker != nullptr) {
        std::vector<PermanentId> legal_attackers;
        for (const auto& creature : attacking_state.creatures) {
            if (!creature.tapped && !creature.summoning_sick &&
                can_attack_through_moat(state_, creature)) {
                legal_attackers.push_back(creature.id);
            }
        }
        attackers = human_attacker->choose_attackers(
            human_observation(state_.active_player),
            legal_attackers);
        std::unordered_set<PermanentId> selected;
        for (const PermanentId attacker : attackers) {
            if (std::find(legal_attackers.begin(),
                          legal_attackers.end(),
                          attacker) == legal_attackers.end() ||
                !selected.insert(attacker).second) {
                throw std::invalid_argument(
                    "human selected an invalid attacker set");
            }
        }
    } else if (handcrafted_attacker) {
        std::vector<const CreaturePermanent*> legal_attackers;
        int total_power = 0;
        for (const auto& creature : attacking_state.creatures) {
            if (!creature.tapped && !creature.summoning_sick &&
                can_attack_through_moat(state_, creature)) {
                legal_attackers.push_back(&creature);
                total_power += creature_power(creature);
            }
        }

        const bool attack_for_lethal =
            total_power >= defending_state.life;
        for (const auto* creature : legal_attackers) {
            if (attack_for_lethal ||
                handcrafted_favorable_attack(
                    *creature, defending_state)) {
                attackers.push_back(creature->id);
            }
        }
    } else {
        std::uniform_int_distribution<int> attack_or_not(0, 1);
        std::vector<PermanentId> legal_attackers;
        for (const auto& creature : attacking_state.creatures) {
            if (!creature.tapped && !creature.summoning_sick &&
                can_attack_through_moat(state_, creature)) {
                legal_attackers.push_back(creature.id);
            }
        }
        for (std::size_t index = 0;
             index < legal_attackers.size(); ++index) {
            const std::size_t chosen =
                static_cast<std::size_t>(attack_or_not(random_));
            if (chosen == 1) {
                attackers.push_back(legal_attackers[index]);
            }
        }
    }

    return play_combat_with_attackers(std::move(attackers));
}

std::optional<GameResult> Game::play_combat_with_attackers(
    std::vector<PermanentId> attackers) {
    auto& attacking_state = state_.players[state_.active_player];
    const std::size_t defending_player =
        opponent_of(state_.active_player);
    auto& defending_state = state_.players[defending_player];
    const bool handcrafted_attacker =
        config_.bots[state_.active_player].kind ==
        BotKind::Handcrafted;
    if (has_human_observer()) {
        notify_human_observers({
            .kind = GameEventKind::AttackersDeclared,
            .player = state_.active_player,
            .phase = TurnPhase::DeclareAttackers,
            .attackers = attackers,
        });
    }
    if (attackers.empty()) {
        return play_priority_window(false, TurnPhase::EndCombat);
    }

    std::vector<PermanentId> available_blockers;
    for (const auto& creature : defending_state.creatures) {
        if (!creature.tapped) {
            available_blockers.push_back(creature.id);
        }
    }
    std::unordered_map<PermanentId, std::vector<PermanentId>>
        blockers_by_attacker;
    const bool handcrafted_defender =
        config_.bots[defending_player].kind == BotKind::Handcrafted;
    const auto* human_defender =
        human_controller(defending_player);
    if (human_defender != nullptr) {
        std::vector<LegalBlockerChoice> choices;
        choices.reserve(available_blockers.size());
        for (const PermanentId blocker : available_blockers) {
            LegalBlockerChoice choice{
                .blocker = blocker,
                .legal_attackers =
                    legal_attackers_for_blocker(
                        state_, state_.active_player,
                        blocker, attackers),
            };
            if (!choice.legal_attackers.empty()) {
                choices.push_back(std::move(choice));
            }
        }
        const auto selected =
            human_defender->choose_blockers(
                human_observation(defending_player),
                attackers, choices);
        std::unordered_set<PermanentId> assigned_blockers;
        for (const auto& [attacker, blocker] : selected) {
            const auto choice = std::find_if(
                choices.begin(), choices.end(),
                [blocker](const LegalBlockerChoice& candidate) {
                    return candidate.blocker == blocker;
                });
            if (choice == choices.end() ||
                std::find(choice->legal_attackers.begin(),
                          choice->legal_attackers.end(),
                          attacker) ==
                    choice->legal_attackers.end() ||
                !assigned_blockers.insert(blocker).second) {
                throw std::invalid_argument(
                    "human selected an invalid blocker assignment");
            }
            blockers_by_attacker[attacker].push_back(blocker);
        }
    } else if (handcrafted_defender) {
        std::vector<PermanentId> unassigned = available_blockers;
        std::vector<PermanentId> ordered_attackers = attackers;
        std::sort(ordered_attackers.begin(), ordered_attackers.end(),
                  [&](PermanentId left, PermanentId right) {
                      const auto* left_creature =
                          find_creature(attacking_state, left);
                      const auto* right_creature =
                          find_creature(attacking_state, right);
                      return creature_power(*left_creature) >
                             creature_power(*right_creature);
                  });

        int incoming_power = 0;
        for (const PermanentId attacker_id : attackers) {
            const auto* attacker =
                find_creature(attacking_state, attacker_id);
            incoming_power += creature_power(*attacker);
        }
        const bool must_block = incoming_power >= defending_state.life;

        for (const PermanentId attacker_id : ordered_attackers) {
            const auto* attacker =
                find_creature(attacking_state, attacker_id);
            auto best_blocker = unassigned.end();
            double best_score = must_block ? 1.0 : 0.0;

            for (auto blocker = unassigned.begin();
                 blocker != unassigned.end(); ++blocker) {
                const auto* blocker_creature =
                    find_creature(defending_state, *blocker);
                if (!can_block_creature(
                        *attacker, *blocker_creature)) {
                    continue;
                }
                const bool kills_attacker =
                    creature_power(*blocker_creature) >=
                    creature_toughness(*attacker);
                const bool survives =
                    creature_toughness(*blocker_creature) >
                    creature_power(*attacker);
                double score =
                    20.0 *
                    static_cast<double>(creature_power(*attacker));
                if (kills_attacker) {
                    score += 1'000.0 +
                             handcrafted_card_value(attacker->card);
                }
                if (survives) {
                    score += 500.0;
                } else {
                    score -= handcrafted_card_value(
                        blocker_creature->card);
                }
                if (score > best_score) {
                    best_score = score;
                    best_blocker = blocker;
                }
            }
            if (best_blocker != unassigned.end()) {
                blockers_by_attacker[attacker_id].push_back(
                    *best_blocker);
                unassigned.erase(best_blocker);
            }
        }
    } else {
        std::shuffle(available_blockers.begin(),
                     available_blockers.end(), random_);
        for (std::size_t index = 0;
             index < available_blockers.size(); ++index) {
            const auto legal_attackers =
                legal_attackers_for_blocker(
                    state_, state_.active_player,
                    available_blockers[index], attackers);
            // Zero means no block; other values select an attacker. Multiple
            // blockers may legally select the same attacker.
            std::uniform_int_distribution<std::size_t> choose_block(
                0, legal_attackers.size());
            const std::size_t choice = choose_block(random_);
            if (choice != 0) {
                blockers_by_attacker[
                    legal_attackers[choice - 1]]
                    .push_back(available_blockers[index]);
            }
        }
    }

    if (has_human_observer()) {
        std::vector<std::pair<PermanentId, PermanentId>>
            declared_blocks;
        for (const PermanentId attacker : attackers) {
            for (const PermanentId blocker :
                 blockers_by_attacker[attacker]) {
                declared_blocks.emplace_back(attacker, blocker);
            }
        }
        notify_human_observers({
            .kind = GameEventKind::BlockersDeclared,
            .player = defending_player,
            .phase = TurnPhase::DeclareBlockers,
            .attackers = attackers,
            .blocks = declared_blocks,
        });
    }

    // Both players may respond with instants after blockers are declared
    // and before combat damage. Responses can remove or alter declared
    // participants, so combat is re-sanitized before resolution: dead
    // creatures leave combat and a block whose pairing became illegal is
    // dropped while its attacker fights on.
    if (const auto result = play_priority_window(
            false, TurnPhase::DeclareBlockers);
        result.has_value()) {
        return result;
    }
    {
        std::vector<PermanentId> surviving_attackers;
        surviving_attackers.reserve(attackers.size());
        for (const PermanentId attacker : attackers) {
            const auto* creature =
                find_creature(attacking_state, attacker);
            // An attacker tapped during the response window (a mana
            // creature paying for its controller's own trick) leaves
            // combat, mirroring how dead attackers drop out.
            if (creature != nullptr && !creature->tapped) {
                surviving_attackers.push_back(attacker);
            }
        }
        attackers = std::move(surviving_attackers);
        for (auto& [attacker_id, blocker_group] : blockers_by_attacker) {
            const auto* attacker =
                find_creature(attacking_state, attacker_id);
            std::vector<PermanentId> surviving_blockers;
            surviving_blockers.reserve(blocker_group.size());
            for (const PermanentId blocker_id : blocker_group) {
                const auto* blocker =
                    find_creature(defending_state, blocker_id);
                if (attacker != nullptr && blocker != nullptr &&
                    !blocker->tapped &&
                    can_block_creature(*attacker, *blocker)) {
                    surviving_blockers.push_back(blocker_id);
                }
            }
            blocker_group = std::move(surviving_blockers);
        }
    }
    if (attackers.empty()) {
        return play_priority_window(false, TurnPhase::EndCombat);
    }

    std::vector<std::pair<PermanentId, PermanentId>> blocks;
    const auto* damage_order_controller =
        human_controller(state_.active_player);
    for (const PermanentId attacker : attackers) {
        auto& blockers = blockers_by_attacker[attacker];
        // The attacking player chooses damage assignment order. The
        // handcrafted
        // policy orders the easiest creatures to kill first.
        if (damage_order_controller != nullptr &&
            blockers.size() > 1) {
            const std::vector<PermanentId> ordered =
                damage_order_controller->choose_damage_order(
                    human_observation(state_.active_player),
                    attacker, blockers);
            std::unordered_set<PermanentId> remaining(
                blockers.begin(), blockers.end());
            bool valid = ordered.size() == blockers.size();
            for (const PermanentId blocker : ordered) {
                valid = valid && remaining.erase(blocker) == 1;
            }
            if (!valid || !remaining.empty()) {
                throw std::invalid_argument(
                    "human selected an invalid combat damage order");
            }
            blockers = ordered;
        } else if (handcrafted_attacker) {
            std::sort(blockers.begin(), blockers.end(),
                      [&](PermanentId left, PermanentId right) {
                          const auto* left_creature =
                              find_creature(defending_state, left);
                          const auto* right_creature =
                              find_creature(defending_state, right);
                          return creature_toughness(*left_creature) <
                                 creature_toughness(*right_creature);
                      });
        } else {
            std::shuffle(blockers.begin(), blockers.end(), random_);
        }
        for (const PermanentId blocker : blockers) {
            blocks.emplace_back(attacker, blocker);
        }
    }

    if (has_human_observer()) {
        notify_human_observers({
            .kind = GameEventKind::DamageOrderChosen,
            .player = state_.active_player,
            .phase = TurnPhase::DamageOrder,
            .attackers = attackers,
            .blocks = blocks,
        });
    }
    if (!resolve_combat(state_, state_.active_player, attackers, blocks)) {
        throw std::logic_error("random policy declared illegal combat");
    }
    if (has_human_observer()) {
        notify_human_observers({
            .kind = GameEventKind::CombatResolved,
            .player = state_.active_player,
            .phase = TurnPhase::EndCombat,
            .attackers = attackers,
            .blocks = blocks,
        });
    }
    if (const auto result = life_total_result(); result.has_value()) {
        return result;
    }
    return play_priority_window(false, TurnPhase::EndCombat);
}

namespace {

} // namespace

GameResult Game::run() {
    initialize();
    if (setup_result_.has_value()) {
        return *setup_result_;
    }
    return run_from_turn(1);
}

GameResult Game::run_with_trace(std::vector<GameState>& trace) {
    trace.clear();
    trace_ = &trace;
    const GameResult result = run();
    trace_ = nullptr;
    return result;
}

GameResult Game::run_from_turn(std::size_t first_turn) {
    for (std::size_t turn = first_turn; turn <= config_.max_turns;
         ++turn) {
        state_.turn_number = turn;
        if (turn == 1) {
            state_.active_player = state_.starting_player;
        } else {
            advance_turn_player(state_);
        }
        begin_turn(state_, state_.active_player);
        if (const auto upkeep_result = life_total_result()) {
            return *upkeep_result;
        }

        const bool starting_player_first_turn =
            turn == 1 && state_.active_player == state_.starting_player;
        if (!starting_player_first_turn &&
            !draw_card(state_.active_player)) {
            return make_result(
                static_cast<int>(opponent_of(state_.active_player)),
                EndReason::EmptyLibrary);
        }
        if (has_human_observer()) {
            notify_human_observers({
                .kind = GameEventKind::TurnStarted,
                .player = state_.active_player,
                .phase = TurnPhase::FirstMain,
            });
        }
        if (trace_ != nullptr) {
            trace_->push_back(state_);
        }

        if (const auto result = play_priority_window(
                true, TurnPhase::FirstMain);
            result.has_value()) {
            return *result;
        }
        if (const auto result = play_combat(); result.has_value()) {
            return *result;
        }
        if (const auto result = play_priority_window(
                true, TurnPhase::SecondMain);
            result.has_value()) {
            return *result;
        }
        perform_cleanup();
    }

    return make_result(-1, EndReason::TurnLimit);
}

const GameState& Game::state() const {
    return state_;
}

double SimulationSummary::average_turns() const {
    return games == 0 ? 0.0
                      : static_cast<double>(total_turns) /
                            static_cast<double>(games);
}

namespace {

double percentage(std::size_t numerator, std::size_t denominator) {
    return denominator == 0
               ? 0.0
               : 100.0 * static_cast<double>(numerator) /
                     static_cast<double>(denominator);
}

double average(std::int64_t total, std::size_t count) {
    return count == 0
               ? 0.0
               : static_cast<double>(total) / static_cast<double>(count);
}

} // namespace

double DeckSimulationStats::win_rate() const {
    return percentage(wins, games);
}

double DeckSimulationStats::on_play_win_rate() const {
    return percentage(on_play_wins, on_play_games);
}

double DeckSimulationStats::on_draw_win_rate() const {
    return percentage(on_draw_wins, on_draw_games);
}

double DeckSimulationStats::average_ending_life() const {
    return average(total_ending_life, games);
}

double DeckSimulationStats::average_cards_drawn() const {
    return average(static_cast<std::int64_t>(total_cards_drawn), games);
}

double DeckSimulationStats::average_lands_played() const {
    return average(static_cast<std::int64_t>(total_lands_played), games);
}

double DeckSimulationStats::average_spells_cast() const {
    return average(static_cast<std::int64_t>(total_spells_cast), games);
}

double DeckSimulationStats::average_spells_countered() const {
    return average(static_cast<std::int64_t>(total_spells_countered),
                   games);
}

double DeckSimulationStats::average_damage_to_opponent() const {
    return average(static_cast<std::int64_t>(total_damage_to_opponent),
                   games);
}

double BotSimulationStats::win_rate() const {
    return percentage(wins, games);
}

double BotSimulationStats::average_decisions() const {
    return average(static_cast<std::int64_t>(total_decisions), games);
}

double BotSimulationStats::average_rollouts() const {
    return average(static_cast<std::int64_t>(total_rollouts), games);
}

double BotSimulationStats::average_rollouts_per_decision() const {
    return average(static_cast<std::int64_t>(total_rollouts),
                   total_decisions);
}

double BotMatchupStats::first_win_rate() const {
    return percentage(first_wins, games);
}

double BotMatchupStats::second_win_rate() const {
    return percentage(second_wins, games);
}

namespace {

std::array<BotMatchupStats, kBotMatchupCount>
empty_bot_matchups() {
    std::array<BotMatchupStats, kBotMatchupCount> matchups;
    std::size_t matchup = 0;
    for (std::size_t first = 0; first < kBotKindCount; ++first) {
        for (std::size_t second = first + 1;
             second < kBotKindCount; ++second) {
            matchups[matchup++] = {
                .first_bot = static_cast<BotKind>(first),
                .second_bot = static_cast<BotKind>(second),
            };
        }
    }
    return matchups;
}

std::size_t bot_matchup_index(BotKind first, BotKind second) {
    const auto low = std::min(static_cast<std::size_t>(first),
                              static_cast<std::size_t>(second));
    const auto high = std::max(static_cast<std::size_t>(first),
                               static_cast<std::size_t>(second));
    std::size_t matchup = 0;
    for (std::size_t candidate_low = 0;
         candidate_low < kBotKindCount; ++candidate_low) {
        for (std::size_t candidate_high = candidate_low + 1;
             candidate_high < kBotKindCount;
             ++candidate_high, ++matchup) {
            if (low == candidate_low && high == candidate_high) {
                return matchup;
            }
        }
    }
    throw std::logic_error("bot matchup requires two different bots");
}

void record_deck_result(DeckSimulationStats& deck,
                        const GameResult& result,
                        std::size_t player) {
    ++deck.games;
    if (result.winner < 0) {
        ++deck.draws;
    } else if (result.winner == static_cast<int>(player)) {
        ++deck.wins;
    } else {
        ++deck.losses;
    }

    if (result.starting_player == player) {
        ++deck.on_play_games;
        if (result.winner == static_cast<int>(player)) {
            ++deck.on_play_wins;
        }
    } else {
        ++deck.on_draw_games;
        if (result.winner == static_cast<int>(player)) {
            ++deck.on_draw_wins;
        }
    }

    deck.total_ending_life += result.ending_life[player];
    deck.total_cards_drawn += result.player_stats[player].cards_drawn;
    deck.total_lands_played += result.player_stats[player].lands_played;
    deck.total_spells_cast += result.player_stats[player].spells_cast;
    deck.total_spells_countered +=
        result.player_stats[player].spells_countered;
    deck.total_damage_to_opponent +=
        result.player_stats[player].damage_to_opponent;
}

void record_bot_result(BotSimulationStats& bot,
                       const GameResult& result,
                       std::size_t player) {
    ++bot.games;
    if (result.winner < 0) {
        ++bot.draws;
    } else if (result.winner == static_cast<int>(player)) {
        ++bot.wins;
    } else {
        ++bot.losses;
    }
    bot.total_decisions += result.player_stats[player].decisions;
    bot.total_rollouts +=
        result.player_stats[player].monte_carlo_rollouts;
}

SimulationSummary run_matchup(const std::vector<CardId>& first_deck,
                              const std::vector<CardId>& second_deck,
                              std::size_t games, std::uint64_t seed,
                              GameConfig game_config) {
    SimulationSummary summary;
    summary.games = games;
    summary.bot_matchups = empty_bot_matchups();
    std::mt19937_64 seed_generator(seed);

    std::vector<GameConfig> configs(games, game_config);
    std::vector<std::uint64_t> game_seeds(games);
    for (std::size_t game_index = 0; game_index < games; ++game_index) {
        game_seeds[game_index] = seed_generator();
    }

    std::vector<GameResult> results(games);
    std::atomic_size_t next_game = 0;
    const std::size_t worker_count = std::min<std::size_t>(
        games, std::max(1U, std::thread::hardware_concurrency()));
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (std::size_t worker = 0; worker < worker_count; ++worker) {
        workers.emplace_back([&] {
            while (true) {
                const std::size_t game_index =
                    next_game.fetch_add(1, std::memory_order_relaxed);
                if (game_index >= games) {
                    return;
                }
                Game game(first_deck, second_deck,
                          game_seeds[game_index],
                          configs[game_index]);
                results[game_index] = game.run();
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    for (const GameResult& result : results) {
        summary.total_turns += result.turns;

        for (std::size_t player = 0; player < summary.decks.size();
             ++player) {
            auto& deck = summary.decks[player];
            const auto bot_index =
                static_cast<std::size_t>(result.bots[player]);
            record_deck_result(deck, result, player);
            record_deck_result(
                summary.deck_bots[player][bot_index], result, player);

            record_bot_result(summary.bots[bot_index], result, player);
        }

        if (result.bots[0] != result.bots[1]) {
            auto& matchup = summary.bot_matchups[bot_matchup_index(
                result.bots[0], result.bots[1])];
            ++matchup.games;
            if (result.winner < 0) {
                ++matchup.draws;
            } else if (result.bots[static_cast<std::size_t>(
                           result.winner)] == matchup.first_bot) {
                ++matchup.first_wins;
            } else {
                ++matchup.second_wins;
            }
        }

        if (result.winner < 0) {
            ++summary.draws;
        }

        switch (result.reason) {
        case EndReason::LifeTotal:
            ++summary.life_total_finishes;
            break;
        case EndReason::EmptyLibrary:
            ++summary.empty_library_finishes;
            break;
        case EndReason::TurnLimit:
            ++summary.turn_limit_draws;
            break;
        }
    }

    return summary;
}

std::vector<CardId> deck_cards(DeckId deck) {
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
    case DeckId::LotusCombo:
        return lotus_combo_deck();
    case DeckId::Burn:
        return burn_deck();
    case DeckId::UWR:
        return uwr_deck();
    case DeckId::Robots:
        return robots_deck();
    case DeckId::WhiteWeenie:
        return white_weenie_deck();
    case DeckId::BRMidrange:
        return br_midrange_deck();
    }
    throw std::out_of_range("unknown deck ID");
}

} // namespace

SimulationSummary run_simulation(std::size_t games, std::uint64_t seed,
                                 GameConfig game_config) {
    return run_matchup(green_deck(), red_deck(), games, seed,
                       game_config);
}

std::string_view deck_name(DeckId deck) {
    switch (deck) {
    case DeckId::Green:
        return "Green Growth";
    case DeckId::Red:
        return "Creatures & Bolts";
    case DeckId::Blue:
        return "Counter Flyer";
    case DeckId::White:
        return "Moat Mill";
    case DeckId::RUAggro:
        return "RU Aggro";
    case DeckId::LotusCombo:
        return "Lotus Combo";
    case DeckId::Burn:
        return "Burn";
    case DeckId::UWR:
        return "Lion-dib-bolt";
    case DeckId::Robots:
        return "Robots";
    case DeckId::WhiteWeenie:
        return "White Weenie";
    case DeckId::BRMidrange:
        return "BR Midrange";
    }
    return "Unknown";
}

std::string_view deck_list(DeckId deck) {
    switch (deck) {
    case DeckId::Green:
        return "16 Forest / 4 Llanowar Elves / 6 Grizzly Bears / "
               "2 Ironroot Treefolk / 4 Moss Beast / "
               "4 Forest Colossus / 4 Giant Growth";
    case DeckId::Red:
        return "15 Mountain / 9 Lightning Bolt / 7 Ironclaw Orcs / "
               "4 Gray Ogre / 3 Hill Giant / 2 Fire Elemental";
    case DeckId::Blue:
        return "15 Island / 1 Mox Sapphire / 1 Sol Ring / "
               "1 Ancestral Recall / 1 Time Walk / 1 Braingeyser / "
               "4 Flying Men / 4 Force Spike / 8 Counterspell / "
               "4 Air Elemental";
    case DeckId::White:
        return "22 Plains / 3 Millstone / 15 Moat";
    case DeckId::RUAggro:
        return "13 Mountain / 4 Island / 3 Flying Men / "
               "5 Ironclaw Orcs / 2 Gray Ogre / 8 Hill Giant / "
               "3 Lightning Bolt / 2 Disintegrate";
    case DeckId::LotusCombo:
        return "20 Black Lotus / 10 Channel / 10 Disintegrate";
    case DeckId::Burn:
        return "12 Mountain / 18 Lightning Bolt";
    case DeckId::UWR:
        return "4 Savannah Lions / 4 Serendib Efreet / 2 Serra Angel / "
               "4 Black Vise / Mox Pearl / Mox Ruby / Mox Sapphire / "
               "Sol Ring / Ancestral Recall / 4 Disenchant / "
               "8 Lightning Bolt / 4 Psionic Blast / "
               "3 Swords to Plowshares / Time Walk / "
               "Wheel of Fortune / Timetwister / "
               "Library of Alexandria / "
               "4 Mishra's Factory / 1 Plains / 4 Plateau / "
               "Strip Mine / 4 Tundra / 4 Volcanic Island";
    case DeckId::Robots:
        return "4 Su-Chi / 3 Sage of Lat-Nam / 3 Triskelion / "
               "3 Counterspell / 4 Lightning Bolt / 2 Psionic Blast / "
               "Ancestral Recall / Mana Drain / Armageddon / "
               "Demonic Tutor / Disintegrate / Mind Twist / Recall / "
               "Time Walk / Wheel of Fortune / 4 Copy Artifact / "
               "Black Lotus / Fellwar Stone / Mox Emerald / Mox Jet / "
               "Mox Pearl / Mox Ruby / Mox Sapphire / Sol Ring / "
               "4 Mishra's Factory / 4 Tundra / 3 City of Brass / "
               "3 Volcanic Island / 2 Underground Sea / Badlands / "
               "Library of Alexandria / Strip Mine";
    case DeckId::WhiteWeenie:
        return "4 Benalish Hero / 3 Mesa Pegasus / 4 Thunder Spirit / "
               "4 Savannah Lions / 3 Serra Angel / 4 White Knight / "
               "Chaos Orb / Jalum Tome / Mox Pearl / 4 Disenchant / "
               "4 Swords to Plowshares / 3 Armageddon / 4 Crusade / "
               "14 Plains / Library of Alexandria / "
               "4 Mishra's Factory / Strip Mine";
    case DeckId::BRMidrange:
        return "4 Juzam Djinn / 4 Sedge Troll / 4 Hypnotic Specter / "
               "4 Su-Chi / 7 Lightning Bolt / 3 Shatter / "
               "Ancestral Recall / Dark Ritual / "
               "Demonic Tutor / 2 Disintegrate / Mind Twist / "
               "Time Walk / Wheel of Fortune / Black Lotus / "
               "Chaos Orb / Mox Jet / Mox Ruby / Mox Sapphire / "
               "Sol Ring / 7 Swamp / 4 Badlands / 3 Mishra's Factory / "
               "2 City of Brass / 2 Volcanic Island / "
               "Library of Alexandria / Strip Mine / Underground Sea";
    }
    return "Unknown";
}

std::string_view bot_name(BotKind bot) {
    switch (bot) {
    case BotKind::Random:
        return "Random";
    case BotKind::MonteCarlo:
        return "Monte Carlo";
    case BotKind::DeepMonteCarlo:
        return "Deep Monte Carlo";
    case BotKind::Handcrafted:
        return "Handcrafted Policy";
    }
    return "Unknown";
}

namespace {

const CreaturePermanent*
find_creature_for_policy(const PlayerState& player, PermanentId id) {
    const auto creature = std::find_if(
        player.creatures.begin(), player.creatures.end(),
        [id](const CreaturePermanent& candidate) {
            return candidate.id == id;
        });
    return creature == player.creatures.end() ? nullptr : &*creature;
}

std::vector<PermanentId> validate_binary_attack_context(
    const GameState& state, std::size_t attacking_player,
    const std::vector<PermanentId>& selected_attackers,
    PermanentId subject,
    const std::vector<PermanentId>& remaining_attackers) {
    if (attacking_player >= state.players.size()) {
        throw std::out_of_range(
            "attacking player must be 0 or 1");
    }
    if (state.active_player != attacking_player) {
        throw std::invalid_argument(
            "binary attack evaluation requires the active player");
    }
    if (!state.stack.empty()) {
        throw std::invalid_argument(
            "binary attack evaluation requires an empty stack");
    }

    const std::vector<PermanentId> legal =
        legal_attackers(state, attacking_player);
    const auto subject_position =
        std::find(legal.begin(), legal.end(), subject);
    if (subject_position == legal.end()) {
        throw std::invalid_argument(
            "binary attack subject cannot legally attack");
    }
    const std::size_t subject_index =
        static_cast<std::size_t>(
            std::distance(legal.begin(), subject_position));

    std::unordered_set<PermanentId> supplied;
    std::size_t prior_position = 0;
    bool have_prior_position = false;
    for (const PermanentId selected : selected_attackers) {
        const auto position =
            std::find(legal.begin(), subject_position, selected);
        if (position == subject_position ||
            !supplied.insert(selected).second) {
            throw std::invalid_argument(
                "selected attackers must be a unique legal prefix");
        }
        const std::size_t index =
            static_cast<std::size_t>(
                std::distance(legal.begin(), position));
        if (have_prior_position && index <= prior_position) {
            throw std::invalid_argument(
                "selected attackers are outside deployed order");
        }
        prior_position = index;
        have_prior_position = true;
    }
    if (!supplied.insert(subject).second) {
        throw std::invalid_argument(
            "binary attack subject appears in the selected prefix");
    }

    const std::vector<PermanentId> expected_remaining(
        legal.begin() +
            static_cast<std::ptrdiff_t>(subject_index + 1),
        legal.end());
    if (remaining_attackers != expected_remaining) {
        throw std::invalid_argument(
            "remaining attackers must be the complete deployed suffix");
    }
    for (const PermanentId remaining : remaining_attackers) {
        if (!supplied.insert(remaining).second) {
            throw std::invalid_argument(
                "binary attack context contains a duplicate creature");
        }
    }
    return legal;
}

} // namespace

std::vector<double> handcrafted_priority_scores(
    const GameState& state, std::size_t player,
    const std::vector<PriorityAction>& candidates,
    TurnPhase phase) {
    if (player >= state.players.size()) {
        throw std::out_of_range(
            "Handcrafted priority player must be 0 or 1");
    }
    if (candidates.empty()) {
        throw std::invalid_argument(
            "Handcrafted priority scoring requires candidates");
    }
    std::vector<PriorityAction> seen;
    seen.reserve(candidates.size());
    for (const PriorityAction& candidate : candidates) {
        if (contains_action(seen, candidate)) {
            throw std::invalid_argument(
                "Handcrafted priority candidates must be unique");
        }
        seen.push_back(candidate);
    }

    Game evaluator({}, {}, 0);
    evaluator.state_ = state;
    std::vector<double> scores;
    scores.reserve(candidates.size());
    for (const PriorityAction& candidate : candidates) {
        scores.push_back(
            evaluator.handcrafted_action_score(
                candidate, player, phase));
    }
    return scores;
}

std::array<double, 2> handcrafted_binary_attack_scores(
    const GameState& state, std::size_t attacking_player,
    const std::vector<PermanentId>& selected_attackers,
    PermanentId subject,
    const std::vector<PermanentId>& remaining_attackers) {
    const std::vector<PermanentId> legal =
        validate_binary_attack_context(
            state, attacking_player, selected_attackers, subject,
            remaining_attackers);
    const std::size_t defending_player =
        opponent_of(attacking_player);
    const PlayerState& attacking_state =
        state.players[attacking_player];
    const PlayerState& defending_state =
        state.players[defending_player];

    int total_power = 0;
    for (const PermanentId attacker : legal) {
        const auto* creature =
            find_creature_for_policy(attacking_state, attacker);
        total_power += creature_power(*creature);
    }
    const auto* subject_creature =
        find_creature_for_policy(attacking_state, subject);
    const bool include =
        total_power >= defending_state.life ||
        handcrafted_favorable_attack(
            *subject_creature, defending_state);
    return include ? std::array<double, 2>{0.0, 1.0}
                   : std::array<double, 2>{1.0, 0.0};
}

double discounted_terminal_target(
    const GameResult& result, std::size_t perspective) {
    if (perspective >= 2) {
        throw std::invalid_argument(
            "terminal-target perspective must be 0 or 1");
    }
    if (result.winner < 0) {
        return 0.5;
    }
    const double discounted_outcome =
        0.5 * std::pow(
                  0.985, static_cast<double>(result.turns));
    return result.winner == static_cast<int>(perspective)
               ? 0.5 + discounted_outcome
               : 0.5 - discounted_outcome;
}

DeckEvolutionSummary evolve_deck(DeckEvolutionConfig config,
                                 std::uint64_t seed,
                                 GameConfig game_config) {
    if (config.generations == 0) {
        throw std::invalid_argument(
            "deck evolution generations must be positive");
    }
    if (config.population < kDeckCount) {
        throw std::invalid_argument(
            "deck evolution population must cover the metagame deck count");
    }
    if (config.repetitions_per_opponent == 0) {
        throw std::invalid_argument(
            "deck evolution repetitions must be positive");
    }
    if ((config.pilot.kind == BotKind::MonteCarlo ||
         config.pilot.kind == BotKind::DeepMonteCarlo) &&
        config.pilot.rollouts_per_action == 0) {
        throw std::invalid_argument(
            "deck evolution Monte Carlo rollouts must be positive");
    }
    game_config.bots = {config.pilot, config.pilot};

    const std::array<std::vector<CardId>, kDeckCount> metagame = {
        deck_cards(DeckId::Green),
        deck_cards(DeckId::Red),
        deck_cards(DeckId::Blue),
        deck_cards(DeckId::White),
        deck_cards(DeckId::RUAggro),
        deck_cards(DeckId::LotusCombo),
        deck_cards(DeckId::Burn),
        deck_cards(DeckId::UWR),
        deck_cards(DeckId::Robots),
        deck_cards(DeckId::WhiteWeenie),
        deck_cards(DeckId::BRMidrange),
    };
    std::vector<CardId> card_pool;
    std::array<bool, kCardCount> seen_cards{};
    for (const auto& deck : metagame) {
        for (const CardId card : deck) {
            const auto index = static_cast<std::size_t>(card);
            if (!seen_cards[index]) {
                seen_cards[index] = true;
                card_pool.push_back(card);
            }
        }
    }

    std::mt19937_64 random(seed);
    std::uniform_int_distribution<std::size_t> choose_card(
        0, card_pool.size() - 1);
    const auto mutate = [&](std::vector<CardId>& deck,
                            std::size_t mutations) {
        std::uniform_int_distribution<std::size_t> choose_slot(
            0, deck.size() - 1);
        for (std::size_t mutation = 0; mutation < mutations;
             ++mutation) {
            deck[choose_slot(random)] = card_pool[choose_card(random)];
        }
    };

    std::vector<std::vector<CardId>> population;
    population.reserve(config.population);
    // Evolved individuals are exactly forty cards; sixty-card
    // metagame decks seed the population as a deterministic
    // forty-card sample (shuffle then trim) so every candidate -
    // including generation-one survivors in the top list - honors
    // the contract.
    std::mt19937_64 seeding(seed ^ 0x5345454453414D50ULL);
    const auto forty = [&](std::vector<CardId> deck) {
        if (deck.size() > 40) {
            std::shuffle(deck.begin(), deck.end(), seeding);
            deck.resize(40);
        }
        // Undersized decks (thirty-card Burn) pad by cycling their
        // own cards so seeds honor the forty-card contract too.
        for (std::size_t source = 0; deck.size() < 40; ++source) {
            deck.push_back(deck[source % deck.size()]);
        }
        return deck;
    };
    for (const auto& deck : metagame) {
        population.push_back(forty(deck));
    }
    while (population.size() < config.population) {
        std::vector<CardId> candidate =
            forty(metagame[population.size() % metagame.size()]);
        mutate(candidate, 1 + population.size() % 8);
        population.push_back(std::move(candidate));
    }

    const std::uint64_t evaluation_seed =
        seed ^ 0x4556414C55415445ULL;
    const auto evaluate_population =
        [&](const std::vector<std::vector<CardId>>& candidates) {
            std::vector<EvolvedDeck> evaluations(candidates.size());
            std::atomic_size_t next_candidate = 0;
            const std::size_t worker_count =
                std::min<std::size_t>(
                    candidates.size(),
                    std::max(
                        1U, std::thread::hardware_concurrency()));
            std::vector<std::thread> workers;
            workers.reserve(worker_count);
            for (std::size_t worker = 0; worker < worker_count;
                 ++worker) {
                workers.emplace_back([&] {
                    while (true) {
                        const std::size_t candidate_index =
                            next_candidate.fetch_add(
                                1, std::memory_order_relaxed);
                        if (candidate_index >= candidates.size()) {
                            return;
                        }
                        auto& evaluation =
                            evaluations[candidate_index];
                        evaluation.cards =
                            candidates[candidate_index];
                        std::mt19937_64 game_seeds(
                            evaluation_seed);
                        for (std::size_t opponent = 0;
                             opponent < metagame.size(); ++opponent) {
                            for (std::size_t repetition = 0;
                                 repetition <
                                 config.repetitions_per_opponent;
                                 ++repetition) {
                                const std::uint64_t game_seed =
                                    game_seeds();
                                for (std::size_t candidate_player = 0;
                                     candidate_player < 2;
                                     ++candidate_player) {
                                    for (std::size_t starting_player = 0;
                                         starting_player < 2;
                                         ++starting_player) {
                                        GameConfig current_config =
                                            game_config;
                                        current_config.starting_player =
                                            starting_player;
                                        const bool candidate_first =
                                            candidate_player == 0;
                                        const std::array<
                                            std::vector<CardId>, 2>
                                            seat_decks = {
                                                candidate_first
                                                    ? candidates
                                                          [candidate_index]
                                                    : metagame[opponent],
                                                candidate_first
                                                    ? metagame[opponent]
                                                    : candidates
                                                          [candidate_index],
                                            };
                                        if (config.controller_pilot) {
                                            current_config
                                                .human_controllers =
                                                config.controller_pilot(
                                                    seat_decks,
                                                    game_seed ^
                                                        (0x9E3779B97F4A7C15ULL *
                                                         (candidate_player *
                                                              2 +
                                                          starting_player +
                                                          1)));
                                        }
                                        Game game(
                                            seat_decks[0], seat_decks[1],
                                            game_seed, current_config);
                                        const GameResult result =
                                            game.run();
                                        record_deck_result(
                                            evaluation.total, result,
                                            candidate_player);
                                        record_deck_result(
                                            evaluation
                                                .by_opponent[opponent],
                                            result, candidate_player);
                                    }
                                }
                            }
                        }
                    }
                });
            }
            for (auto& worker : workers) {
                worker.join();
            }
            return evaluations;
        };

    DeckEvolutionSummary summary;
    for (std::size_t generation = 0;
         generation < config.generations; ++generation) {
        auto evaluations = evaluate_population(population);
        std::vector<std::size_t> order(evaluations.size());
        for (std::size_t index = 0; index < order.size(); ++index) {
            order[index] = index;
        }
        std::sort(
            order.begin(), order.end(),
            [&](std::size_t left, std::size_t right) {
                const double left_rate =
                    evaluations[left].total.win_rate();
                const double right_rate =
                    evaluations[right].total.win_rate();
                if (left_rate != right_rate) {
                    return left_rate > right_rate;
                }
                return evaluations[left].cards <
                       evaluations[right].cards;
            });
        const EvolvedDeck& generation_best =
            evaluations[order.front()];
        summary.generation_best_win_rates.push_back(
            generation_best.total.win_rate());
        if (summary.best.cards.empty() ||
            generation_best.total.win_rate() >
                summary.best.total.win_rate()) {
            summary.best = generation_best;
        }
        if (generation + 1 == config.generations) {
            summary.top.clear();
            for (const std::size_t index : order) {
                const EvolvedDeck& candidate = evaluations[index];
                const bool duplicate = std::any_of(
                    summary.top.begin(), summary.top.end(),
                    [&](const EvolvedDeck& kept) {
                        return kept.cards == candidate.cards;
                    });
                if (!duplicate) {
                    summary.top.push_back(candidate);
                }
                if (summary.top.size() == 5) {
                    break;
                }
            }
        }

        const std::size_t elite_count =
            std::max<std::size_t>(2, config.population / 4);
        std::vector<std::vector<CardId>> next_population;
        next_population.reserve(config.population);
        for (std::size_t elite = 0; elite < elite_count;
             ++elite) {
            next_population.push_back(
                evaluations[order[elite]].cards);
        }
        std::uniform_int_distribution<std::size_t> choose_elite(
            0, elite_count - 1);
        std::uniform_int_distribution<std::size_t> mutation_count(1, 4);
        while (next_population.size() < config.population) {
            auto child =
                next_population[choose_elite(random)];
            mutate(child, mutation_count(random));
            next_population.push_back(std::move(child));
        }
        population = std::move(next_population);
    }
    return summary;
}

} // namespace old_school
