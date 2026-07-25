#include "old_school/interactive.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <istream>
#include <numeric>
#include <optional>
#include <ostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace old_school {
namespace {

constexpr std::size_t kTerminalWidth = 120;
constexpr std::size_t kBoxContentWidth = kTerminalWidth - 4;
constexpr std::size_t kStackColumnWidth = 58;
constexpr std::size_t kWideTerminalWidth =
    kTerminalWidth + 2 + kStackColumnWidth;

struct BoxStyle {
    char corner = '+';
    char side = '|';
    char horizontal = '-';
};

constexpr BoxStyle kNormalBox;
constexpr BoxStyle kBoldBox{
    .corner = '#',
    .side = '#',
    .horizontal = '=',
};

class InteractiveQuit : public std::exception {
  public:
    explicit InteractiveQuit(bool input_closed)
        : input_closed_(input_closed) {}

    bool input_closed() const { return input_closed_; }

    const char* what() const noexcept override {
        return input_closed_ ? "interactive input closed"
                             : "interactive game abandoned";
    }

  private:
    bool input_closed_ = false;
};

std::string_view trim(std::string_view text) {
    const auto whitespace = [](unsigned char character) {
        return std::isspace(character) != 0;
    };
    while (!text.empty() &&
           whitespace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() &&
           whitespace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

std::string lowercase(std::string_view text) {
    std::string result(text);
    std::transform(
        result.begin(), result.end(), result.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return result;
}

std::string uppercase(std::string_view text) {
    std::string result(text);
    std::transform(
        result.begin(), result.end(), result.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::toupper(character));
        });
    return result;
}

std::string_view phase_name(TurnPhase phase) {
    switch (phase) {
    case TurnPhase::FirstMain:
        return "first main";
    case TurnPhase::BeginCombat:
        return "beginning of combat";
    case TurnPhase::DeclareAttackers:
        return "declare attackers";
    case TurnPhase::DeclareBlockers:
        return "declare blockers";
    case TurnPhase::DamageOrder:
        return "combat damage order";
    case TurnPhase::EndCombat:
        return "end of combat";
    case TurnPhase::SecondMain:
        return "second main";
    }
    throw std::logic_error("unknown turn phase");
}

std::string_view end_reason_name(EndReason reason) {
    switch (reason) {
    case EndReason::LifeTotal:
        return "life total";
    case EndReason::EmptyLibrary:
        return "empty library";
    case EndReason::TurnLimit:
        return "turn limit";
    }
    throw std::logic_error("unknown game end reason");
}

std::string player_name(const PlayerObservation& observation,
                        std::size_t player) {
    return player == observation.observer ? "You" : "Learned";
}

struct LocatedCreature {
    std::size_t controller = 0;
    const CreaturePermanent* creature = nullptr;
};

std::optional<LocatedCreature>
find_creature(const PlayerObservation& observation,
              PermanentId permanent) {
    for (std::size_t player = 0;
         player < observation.players.size(); ++player) {
        const auto& creatures =
            observation.players[player].creatures;
        const auto found = std::find_if(
            creatures.begin(), creatures.end(),
            [permanent](const CreaturePermanent& creature) {
                return creature.id == permanent;
            });
        if (found != creatures.end()) {
            return LocatedCreature{
                .controller = player,
                .creature = &*found,
            };
        }
    }
    return std::nullopt;
}

std::string creature_name(const PlayerObservation& observation,
                          PermanentId permanent,
                          bool include_controller = true) {
    const auto located = find_creature(observation, permanent);
    if (!located.has_value()) {
        return "creature #" + std::to_string(permanent);
    }
    std::string result;
    if (include_controller) {
        result += located->controller == observation.observer
                      ? "your "
                      : "Learned's ";
    }
    result += card_definition(located->creature->card).name;
    result += " #" + std::to_string(permanent);
    return result;
}

std::string target_name(const PlayerObservation& observation,
                        const Target& target) {
    if (target.creature.has_value()) {
        return creature_name(
            observation, *target.creature);
    }
    return target.player == observation.observer
               ? "you"
               : "Learned";
}

std::string stack_object_name(
    const PlayerObservation& observation,
    const StackObject& object) {
    std::string result =
        player_name(observation, object.controller) + "'s " +
        std::string(card_definition(object.card).name);
    if (object.card == CardId::Disintegrate) {
        result += " (X=" + std::to_string(object.x_value) + ")";
    }
    if (object.target.has_value()) {
        result += " -> " +
                  target_name(observation, *object.target);
    }
    if (object.spell_target.has_value()) {
        result += " -> spell #" +
                  std::to_string(*object.spell_target);
    }
    if (object.kind == StackObjectKind::ActivatedAbility) {
        result += " ability";
    }
    result += " [stack #" + std::to_string(object.id) + "]";
    return result;
}

std::string action_name(const PlayerObservation& observation,
                        const PriorityAction& action) {
    const std::string card(card_definition(action.card).name);
    switch (action.kind) {
    case PriorityActionKind::Pass:
        return "Pass priority";
    case PriorityActionKind::PlayLand:
        return "Play " + card;
    case PriorityActionKind::CastCreature:
    case PriorityActionKind::CastSorcery:
    case PriorityActionKind::CastArtifact:
    case PriorityActionKind::CastEnchantment:
        return "Cast " + card;
    case PriorityActionKind::CastLightningBolt:
    case PriorityActionKind::CastGiantGrowth:
        return "Cast " + card + " -> " +
               target_name(observation, *action.target);
    case PriorityActionKind::CastDisintegrate:
        return "Cast " + card + " (X=" +
               std::to_string(action.x_value) + ") -> " +
               target_name(observation, *action.target);
    case PriorityActionKind::CastCounterspell:
        return "Cast " + card + " -> spell #" +
               std::to_string(*action.spell_target);
    case PriorityActionKind::ActivateMillstone:
        return "Activate " + card + " #" +
               std::to_string(*action.source_permanent) + " -> " +
               target_name(observation, *action.target);
    }
    throw std::logic_error("unknown priority action");
}

void print_card_list(std::ostream& output,
                     const std::vector<CardId>& cards,
                     std::string_view empty_label = "(none)") {
    if (cards.empty()) {
        output << empty_label;
        return;
    }
    std::array<std::size_t, kCardCount> counts{};
    for (const CardId card : cards) {
        ++counts[static_cast<std::size_t>(card)];
    }
    bool first = true;
    for (std::size_t card = 0; card < counts.size(); ++card) {
        if (counts[card] == 0) {
            continue;
        }
        if (!first) {
            output << ", ";
        }
        output << card_definition(static_cast<CardId>(card)).name;
        if (counts[card] > 1) {
            output << " x" << counts[card];
        }
        first = false;
    }
}

template <typename Renderer>
std::string render_text(Renderer renderer) {
    std::ostringstream rendered;
    renderer(rendered);
    return rendered.str();
}

std::vector<std::string> wrap_text(std::string text,
                                   std::size_t width) {
    std::vector<std::string> lines;
    std::string_view remaining = trim(text);
    if (remaining.empty()) {
        return {""};
    }
    while (remaining.size() > width) {
        std::size_t split = remaining.rfind(' ', width);
        if (split == std::string_view::npos || split == 0) {
            split = width;
        }
        lines.emplace_back(trim(remaining.substr(0, split)));
        remaining.remove_prefix(split);
        remaining = trim(remaining);
    }
    lines.emplace_back(remaining);
    return lines;
}

void print_log_line(std::ostream& output,
                    std::string message) {
    const auto lines =
        wrap_text(std::move(message), kTerminalWidth - 2);
    for (std::size_t line = 0; line < lines.size(); ++line) {
        output << (line == 0 ? "" : "  ")
               << lines[line] << '\n';
    }
}

void print_box_border(std::ostream& output,
                      std::string_view title = {},
                      BoxStyle style = kNormalBox) {
    std::string inside(
        kTerminalWidth - 2, style.horizontal);
    if (!title.empty()) {
        const std::string label = " " + std::string(title) + " ";
        if (label.size() > inside.size() - 2) {
            throw std::logic_error("interactive box title is too long");
        }
        inside.replace(2, label.size(), label);
    }
    output << style.corner << inside << style.corner
           << '\n';
}

void print_box_row(std::ostream& output, std::string_view label,
                   const std::string& value,
                   std::size_t label_width = 22,
                   BoxStyle style = kNormalBox) {
    std::string prefix;
    if (!label.empty()) {
        prefix = std::string(label) + ": ";
    }
    if (prefix.size() > label_width) {
        throw std::logic_error("interactive row label is too long");
    }
    prefix.resize(label_width, ' ');
    const std::size_t value_width =
        kBoxContentWidth - label_width;
    const auto lines = wrap_text(value, value_width);
    for (std::size_t index = 0; index < lines.size(); ++index) {
        std::string content =
            (index == 0 ? prefix
                        : std::string(label_width, ' ')) +
            lines[index];
        content.resize(kBoxContentWidth, ' ');
        output << style.side << ' ' << content << ' '
               << style.side << '\n';
    }
}

struct CardTile {
    std::size_t width = 0;
    std::vector<std::string> lines;
};

std::string fit_tile_text(std::string text,
                          std::size_t width) {
    if (text.size() > width) {
        if (width == 0) {
            return {};
        }
        text.resize(width - 1);
        text += '~';
    }
    text.resize(width, ' ');
    return text;
}

std::string tile_border(std::size_t width) {
    return "+" + std::string(width - 2, '-') + "+";
}

std::array<std::string, 2>
two_tile_lines(std::string text, std::size_t width) {
    const auto wrapped = wrap_text(std::move(text), width);
    if (wrapped.size() > 2) {
        throw std::logic_error(
            "interactive card field needs more than two lines");
    }
    return {
        wrapped.empty() ? "" : wrapped[0],
        wrapped.size() > 1 ? wrapped[1] : "",
    };
}

CardTile make_hand_tile(CardId card, std::size_t copies) {
    constexpr std::size_t width = 17;
    constexpr std::size_t inside = width - 2;
    const auto name = two_tile_lines(
        std::string(card_definition(card).name), inside);
    const std::string count =
        copies == 1 ? ""
                    : "x" + std::to_string(copies);
    return {
        .width = width,
        .lines = {
            "/" + std::string(inside, '-') + "\\",
            "|" + fit_tile_text(name[0], inside) + "|",
            "|" + fit_tile_text(name[1], inside) + "|",
            "|" + fit_tile_text(count, inside) + "|",
            "\\" + std::string(inside, '_') + "/",
        },
    };
}

CardTile make_upright_card(std::string name,
                           std::string detail,
                           std::string status) {
    constexpr std::size_t width = 17;
    constexpr std::size_t inside = width - 2;
    const auto name_lines =
        two_tile_lines(std::move(name), inside);
    const auto status_lines =
        two_tile_lines(std::move(status), inside);
    return {
        .width = width,
        .lines = {
            tile_border(width),
            "|" + fit_tile_text(name_lines[0], inside) + "|",
            "|" + fit_tile_text(name_lines[1], inside) + "|",
            "|" + fit_tile_text(std::move(detail), inside) + "|",
            "|" + fit_tile_text(status_lines[0], inside) + "|",
            "|" + fit_tile_text(status_lines[1], inside) + "|",
            tile_border(width),
        },
    };
}

CardTile make_tapped_card(std::string name,
                          std::string detail,
                          std::string status) {
    constexpr std::size_t width = 25;
    constexpr std::size_t inside = width - 2;
    std::string tap_line = "<<< TAPPED >>>";
    if (!status.empty() && status != "READY") {
        tap_line += " " + status;
    }
    return {
        .width = width,
        .lines = {
            tile_border(width),
            "|" + fit_tile_text(std::move(name), inside) + "|",
            "|" + fit_tile_text(std::move(detail), inside) + "|",
            "|" + fit_tile_text(std::move(tap_line), inside) + "|",
            "|" + fit_tile_text(std::move(status), inside) + "|",
            tile_border(width),
        },
    };
}

CardTile make_zone_tile(std::string name, std::size_t count,
                        std::string detail) {
    constexpr std::size_t width = 27;
    constexpr std::size_t inside = width - 2;
    return {
        .width = width,
        .lines = {
            tile_border(width),
            "|" + fit_tile_text(std::move(name), inside) + "|",
            "|" + fit_tile_text(
                      std::to_string(count) +
                          (count == 1 ? " CARD" : " CARDS"),
                      inside) +
                "|",
            "|" + fit_tile_text(std::move(detail), inside) + "|",
            tile_border(width),
        },
    };
}

CardTile make_stack_tile(
    const PlayerObservation& observation,
    const StackObject& object, std::size_t position) {
    constexpr std::size_t width = 53;
    constexpr std::size_t inside = width - 2;
    const auto description =
        wrap_text(stack_object_name(observation, object), inside);
    return {
        .width = width,
        .lines = {
            tile_border(width),
            "|" + fit_tile_text(
                      "STACK #" + std::to_string(position) +
                          (position == 0 ? " | TOP" : ""),
                      inside) +
                "|",
            "|" + fit_tile_text(description.front(), inside) + "|",
            "|" + fit_tile_text(
                      description.size() > 1 ? description[1] : "",
                      inside) +
                "|",
            tile_border(width),
        },
    };
}

void print_box_content(std::ostream& output,
                       std::string content,
                       BoxStyle style = kNormalBox) {
    if (content.size() > kBoxContentWidth) {
        throw std::logic_error(
            "interactive content exceeds terminal width");
    }
    content.resize(kBoxContentWidth, ' ');
    output << style.side << ' ' << content << ' '
           << style.side << '\n';
}

void print_section_title(std::ostream& output,
                         std::string_view title,
                         BoxStyle style = kNormalBox) {
    const std::string label = "[ " + std::string(title) + " ]";
    const std::size_t left =
        (kBoxContentWidth - label.size()) / 2;
    print_box_content(
        output, std::string(left, ' ') + label, style);
}

void print_tiles(std::ostream& output,
                 const std::vector<CardTile>& tiles,
                 BoxStyle style = kNormalBox) {
    if (tiles.empty()) {
        return;
    }

    std::vector<std::vector<CardTile>> rows(1);
    std::size_t row_width = 0;
    for (const auto& tile : tiles) {
        const std::size_t required =
            tile.width + (rows.back().empty() ? 0 : 1);
        if (!rows.back().empty() &&
            row_width + required > kBoxContentWidth) {
            rows.emplace_back();
            row_width = 0;
        }
        if (tile.width > kBoxContentWidth) {
            throw std::logic_error(
                "interactive tile exceeds terminal width");
        }
        row_width += tile.width +
                     (rows.back().empty() ? 0 : 1);
        rows.back().push_back(tile);
    }

    for (const auto& row : rows) {
        const std::size_t width =
            std::accumulate(
                row.begin(), row.end(), std::size_t{0},
                [](std::size_t total, const CardTile& tile) {
                    return total + tile.width;
                }) +
            row.size() - 1;
        const std::size_t height =
            std::max_element(
                row.begin(), row.end(),
                [](const CardTile& left, const CardTile& right) {
                    return left.lines.size() <
                           right.lines.size();
                })
                ->lines.size();
        const std::size_t left =
            (kBoxContentWidth - width) / 2;
        for (std::size_t line = 0; line < height; ++line) {
            std::string content(left, ' ');
            for (std::size_t tile_index = 0;
                 tile_index < row.size(); ++tile_index) {
                if (tile_index != 0) {
                    content += ' ';
                }
                const auto& tile = row[tile_index];
                const std::size_t top =
                    (height - tile.lines.size()) / 2;
                if (line < top ||
                    line >= top + tile.lines.size()) {
                    content += std::string(tile.width, ' ');
                } else {
                    content += tile.lines[line - top];
                }
            }
            print_box_content(
                output, std::move(content), style);
        }
    }
}

std::vector<CardTile>
hand_tiles(const std::vector<CardId>& hand) {
    std::array<std::size_t, kCardCount> counts{};
    for (const CardId card : hand) {
        ++counts[static_cast<std::size_t>(card)];
    }
    std::vector<CardTile> tiles;
    for (std::size_t card = 0; card < counts.size(); ++card) {
        if (counts[card] != 0) {
            tiles.push_back(make_hand_tile(
                static_cast<CardId>(card), counts[card]));
        }
    }
    return tiles;
}

void append_status(std::string& status,
                   const std::string& item) {
    if (!status.empty()) {
        status += ' ';
    }
    status += item;
}

std::vector<CardTile> creature_tiles(
    const std::vector<CreaturePermanent>& creatures) {
    std::vector<CardTile> tiles;
    tiles.reserve(creatures.size());
    for (const auto& creature : creatures) {
        const auto& definition =
            card_definition(creature.card);
        const std::string detail =
            "#" + std::to_string(creature.id) + " " +
            std::to_string(
                definition.power +
                creature.temporary_power_bonus) +
            "/" +
            std::to_string(
                definition.toughness +
                creature.temporary_toughness_bonus);
        std::string status;
        if (creature.summoning_sick) {
            append_status(status, "SICK");
        }
        if (creature.damage != 0) {
            append_status(
                status,
                "DMG " + std::to_string(creature.damage));
        }
        if (creature.temporary_power_bonus != 0 ||
            creature.temporary_toughness_bonus != 0) {
            append_status(
                status,
                "TEMP " +
                    std::to_string(
                        creature.temporary_power_bonus) +
                    "/" +
                    std::to_string(
                        creature.temporary_toughness_bonus));
        }
        if (status.empty()) {
            status = "READY";
        }
        tiles.push_back(
            creature.tapped
                ? make_tapped_card(
                      std::string(definition.name), detail,
                      status)
                : make_upright_card(
                      std::string(definition.name), detail,
                      status));
    }
    return tiles;
}

std::vector<CardTile> land_tiles(
    const std::vector<LandPermanent>& lands) {
    std::vector<CardTile> tiles;
    tiles.reserve(lands.size());
    for (const auto& land : lands) {
        const std::string name(
            card_definition(land.card).name);
        tiles.push_back(
            land.tapped
                ? make_tapped_card(name, "LAND", "")
                : make_upright_card(
                      name, "LAND", ""));
    }
    return tiles;
}

std::vector<CardTile> other_permanent_tiles(
    const PublicPlayerState& state) {
    std::vector<CardTile> tiles;
    tiles.reserve(
        state.artifacts.size() +
        state.enchantments.size());
    for (const auto& artifact : state.artifacts) {
        const std::string name(
            card_definition(artifact.card).name);
        const std::string detail =
            "#" + std::to_string(artifact.id) + " ARTIFACT";
        tiles.push_back(
            artifact.tapped
                ? make_tapped_card(name, detail, "READY")
                : make_upright_card(
                      name, detail, "READY"));
    }
    for (const CardId enchantment : state.enchantments) {
        tiles.push_back(make_upright_card(
            std::string(card_definition(enchantment).name),
            "ENCHANTMENT", "IN PLAY"));
    }
    return tiles;
}

void print_hand(std::ostream& output,
                const PlayerObservation& observation,
                std::size_t player, BoxStyle style) {
    const bool own_hand = player == observation.observer;
    const bool revealed =
        observation.revealed_opponent_hand.has_value();
    const std::size_t hand_size =
        observation.players[player].hand_size;
    print_section_title(
        output,
        (own_hand
             ? "YOUR HAND"
             : (revealed ? "HAND (DEBUG REVEAL)"
                         : "HIDDEN HAND")) +
            std::string(" | ") +
            std::to_string(hand_size) +
            (hand_size == 1 ? " CARD" : " CARDS"),
        style);
    if (own_hand) {
        print_tiles(
            output, hand_tiles(observation.hand), style);
    } else if (revealed) {
        print_tiles(
            output,
            hand_tiles(
                *observation.revealed_opponent_hand),
            style);
    } else {
        print_tiles(
            output,
            {make_zone_tile(
                "HIDDEN HAND",
                observation.players[player].hand_size,
                "IDENTITIES HIDDEN")},
            style);
    }
}

void print_zones(std::ostream& output,
                 const PublicPlayerState& state,
                 BoxStyle style) {
    const auto top_card = [](const std::vector<CardId>& cards) {
        return cards.empty()
                   ? std::string("EMPTY")
                   : "TOP: " +
                         std::string(
                             card_definition(cards.back()).name);
    };
    print_section_title(output, "ZONES", style);
    print_tiles(
        output,
        {
            make_zone_tile(
                "LIBRARY", state.library_size,
                "ORDER HIDDEN"),
            make_zone_tile(
                "GRAVEYARD", state.graveyard.size(),
                state.graveyard.empty()
                    ? ""
                    : top_card(state.graveyard)),
            make_zone_tile(
                "EXILE", state.exile.size(),
                state.exile.empty()
                    ? ""
                    : top_card(state.exile)),
        },
        style);
    if (!state.graveyard.empty()) {
        print_box_row(
            output, "GRAVEYARD CARDS",
            render_text([&](std::ostream& stream) {
                print_card_list(stream, state.graveyard);
            }),
            22, style);
    }
    if (!state.exile.empty()) {
        print_box_row(
            output, "EXILE CARDS",
            render_text([&](std::ostream& stream) {
                print_card_list(stream, state.exile);
            }),
            22, style);
    }
}

void print_battlefield_section(
    std::ostream& output, std::string_view title,
    const std::vector<CardTile>& tiles, BoxStyle style) {
    if (tiles.empty()) {
        return;
    }
    print_section_title(output, title, style);
    print_tiles(output, tiles, style);
}

void print_player(std::ostream& output,
                  const PlayerObservation& observation,
                  std::size_t player) {
    const auto& state = observation.players[player];
    std::ostringstream title;
    title << (player == observation.observer ? "YOU" : "LEARNED")
          << " | LIFE " << state.life;
    if (player == observation.active_player) {
        title << " | ACTIVE";
    }
    const BoxStyle style =
        player == observation.observer ? kBoldBox
                                       : kNormalBox;
    print_box_border(output, title.str(), style);

    const auto creatures = creature_tiles(state.creatures);
    const auto lands = land_tiles(state.lands);
    auto permanents = creatures;
    const auto other = other_permanent_tiles(state);
    permanents.insert(
        permanents.end(), other.begin(), other.end());
    if (player != observation.observer) {
        print_hand(output, observation, player, style);
        print_zones(output, state, style);
        print_battlefield_section(
            output, "LANDS", lands, style);
        print_battlefield_section(
            output, "PERMANENTS", permanents, style);
    } else {
        print_battlefield_section(
            output, "PERMANENTS", permanents, style);
        print_battlefield_section(
            output, "LANDS", lands, style);
        print_zones(output, state, style);
        print_hand(
            output, observation, player, style);
    }
    print_box_border(output, {}, style);
}

std::vector<std::string>
rendered_lines(const std::string& rendered) {
    std::vector<std::string> lines;
    std::istringstream input(rendered);
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(std::move(line));
    }
    return lines;
}

std::string stack_column_border(std::string_view title = {}) {
    std::string inside(kStackColumnWidth - 2, '-');
    if (!title.empty()) {
        const std::string label = " " + std::string(title) + " ";
        inside.replace(2, label.size(), label);
    }
    return "+" + inside + "+";
}

std::vector<std::string> stack_column_lines(
    const PlayerObservation& observation) {
    std::vector<std::string> lines = {
        stack_column_border("STACK | TOP FIRST"),
    };
    std::size_t position = 0;
    for (auto object = observation.stack.rbegin();
         object != observation.stack.rend(); ++object) {
        const CardTile tile = make_stack_tile(
            observation, *object, position++);
        const std::size_t content_width =
            kStackColumnWidth - 4;
        const std::size_t left =
            (content_width - tile.width) / 2;
        for (const auto& tile_line : tile.lines) {
            std::string content(left, ' ');
            content += tile_line;
            content.resize(content_width, ' ');
            lines.push_back("| " + content + " |");
        }
    }
    lines.push_back(stack_column_border());
    return lines;
}

void print_state(std::ostream& output,
                 const PlayerObservation& observation) {
    std::ostringstream board;
    std::ostringstream title;
    title << "BOARD | TURN " << observation.turn_number;
    print_box_border(board, title.str());
    print_player(
        board, observation, 1 - observation.observer);
    print_player(board, observation, observation.observer);

    output << '\n';
    if (observation.stack.empty()) {
        output << board.str();
        return;
    }

    auto board_lines = rendered_lines(board.str());
    auto stack_lines = stack_column_lines(observation);
    const std::size_t height =
        std::max(board_lines.size(), stack_lines.size());
    for (std::size_t line = 0; line < height; ++line) {
        std::string left =
            line < board_lines.size() ? board_lines[line] : "";
        if (left.size() > kTerminalWidth) {
            throw std::logic_error(
                "interactive board exceeds base width");
        }
        left.resize(kTerminalWidth, ' ');
        std::string right =
            line < stack_lines.size() ? stack_lines[line] : "";
        if (right.size() > kStackColumnWidth) {
            throw std::logic_error(
                "interactive stack exceeds column width");
        }
        right.resize(kStackColumnWidth, ' ');
        const std::string wide =
            left + "  " + right;
        if (wide.size() != kWideTerminalWidth) {
            throw std::logic_error(
                "interactive wide board has invalid width");
        }
        output << wide << '\n';
    }
}

void print_choice_box(
    std::ostream& output, std::string_view title,
    const std::vector<std::string>& choices) {
    print_box_border(output, title);
    for (std::size_t index = 0; index < choices.size(); ++index) {
        print_box_row(
            output, "[" + std::to_string(index) + "]",
            choices[index], 7);
    }
    print_box_border(output);
}

class TerminalSession {
  public:
    TerminalSession(std::istream& input, std::ostream& output)
        : input_(input), output_(output) {}

    HumanController controller() {
        return {
            .choose_priority_action =
                [this](const PlayerObservation& observation,
                       TurnPhase phase,
                       const std::vector<PriorityAction>& actions) {
                    return choose_priority(
                        observation, phase, actions);
                },
            .choose_attackers =
                [this](const PlayerObservation& observation,
                       const std::vector<PermanentId>& attackers) {
                    return choose_attackers(
                        observation, attackers);
                },
            .choose_blockers =
                [this](
                    const PlayerObservation& observation,
                    const std::vector<PermanentId>& attackers,
                    const std::vector<LegalBlockerChoice>& blockers) {
                    return choose_blockers(
                        observation, attackers, blockers);
                },
            .choose_damage_order =
                [this](const PlayerObservation& observation,
                       PermanentId attacker,
                       const std::vector<PermanentId>& blockers) {
                    return choose_damage_order(
                        observation, attacker, blockers);
                },
            .observe =
                [this](const PlayerObservation& observation,
                       const GameEvent& event) {
                    observe(observation, event);
                },
            .reveal_opponent_hand = true,
        };
    }

    void print_final(const PlayerObservation& observation,
                     const GameResult& result) {
        output_ << "\n=== Game over ===\n";
        if (result.winner < 0) {
            output_ << "Result: Draw\n";
        } else if (result.winner ==
                   static_cast<int>(observation.observer)) {
            output_ << "Result: You win\n";
        } else {
            output_ << "Result: Learned wins\n";
        }
        output_ << "Reason: " << end_reason_name(result.reason)
                << "\nIndividual turns: " << result.turns << '\n';
        print_state(output_, observation);
    }

  private:
    std::size_t read_index(std::string_view prompt,
                           std::size_t maximum) {
        while (true) {
            output_ << prompt << std::flush;
            std::string line;
            if (!std::getline(input_, line)) {
                throw InteractiveQuit(true);
            }
            const std::string_view value = trim(line);
            const std::string lowered = lowercase(value);
            if (lowered == "q" || lowered == "quit") {
                throw InteractiveQuit(false);
            }

            std::size_t chosen = 0;
            const auto parsed = std::from_chars(
                value.data(), value.data() + value.size(),
                chosen);
            if (parsed.ec == std::errc{} &&
                parsed.ptr == value.data() + value.size() &&
                chosen <= maximum) {
                return chosen;
            }
            output_ << "Please enter a number from 0 to "
                    << maximum << ", or q to quit.\n";
        }
    }

    void render_decision_state(
        const PlayerObservation& observation) {
        if (!last_rendered_.has_value() ||
            *last_rendered_ != observation) {
            print_state(output_, observation);
            last_rendered_ = observation;
        }
    }

    std::size_t choose_priority(
        const PlayerObservation& observation, TurnPhase phase,
        const std::vector<PriorityAction>& actions) {
        render_decision_state(observation);
        std::vector<std::string> choices;
        choices.reserve(actions.size());
        for (const auto& action : actions) {
            choices.push_back(action_name(observation, action));
        }
        print_choice_box(
            output_,
            uppercase(phase_name(phase)) + " | YOUR PRIORITY",
            choices);
        return read_index("choice> ", actions.size() - 1);
    }

    std::vector<PermanentId> choose_attackers(
        const PlayerObservation& observation,
        const std::vector<PermanentId>& legal_attackers) {
        render_decision_state(observation);
        if (legal_attackers.empty()) {
            output_ << "[ATTACK] You have no legal attackers.\n";
            return {};
        }
        std::vector<PermanentId> selected;
        for (const PermanentId attacker : legal_attackers) {
            print_choice_box(
                output_,
                "DECLARE ATTACKER | " +
                    creature_name(observation, attacker, false),
                {"Stay home", "Attack"});
            if (read_index("choice> ", 1) == 1) {
                selected.push_back(attacker);
            }
        }
        return selected;
    }

    std::vector<std::pair<PermanentId, PermanentId>>
    choose_blockers(
        const PlayerObservation& observation,
        const std::vector<PermanentId>& attackers,
        const std::vector<LegalBlockerChoice>& blockers) {
        render_decision_state(observation);
        std::ostringstream incoming;
        incoming << "[BLOCK] Incoming attackers: ";
        for (std::size_t index = 0; index < attackers.size(); ++index) {
            if (index != 0) {
                incoming << ", ";
            }
            incoming << creature_name(
                observation, attackers[index], false);
        }
        print_log_line(output_, incoming.str());
        if (blockers.empty()) {
            output_ << "[BLOCK] You have no legal blockers.\n";
            return {};
        }

        std::vector<std::pair<PermanentId, PermanentId>> selected;
        for (const auto& blocker : blockers) {
            std::vector<std::string> choices = {"Do not block"};
            for (std::size_t index = 0;
                 index < blocker.legal_attackers.size(); ++index) {
                choices.push_back(
                    "Block " +
                    creature_name(
                        observation,
                        blocker.legal_attackers[index], false));
            }
            print_choice_box(
                output_,
                "ASSIGN BLOCKER | " +
                    creature_name(
                        observation, blocker.blocker, false),
                choices);
            const std::size_t chosen = read_index(
                "choice> ", blocker.legal_attackers.size());
            if (chosen != 0) {
                selected.emplace_back(
                    blocker.legal_attackers[chosen - 1],
                    blocker.blocker);
            }
        }
        return selected;
    }

    std::vector<PermanentId> choose_damage_order(
        const PlayerObservation& observation, PermanentId attacker,
        const std::vector<PermanentId>& blockers) {
        render_decision_state(observation);
        output_ << "[ORDER] Choose first damage recipient for "
                << creature_name(observation, attacker, false)
                << ".\n";
        std::vector<PermanentId> remaining = blockers;
        std::vector<PermanentId> ordered;
        ordered.reserve(blockers.size());
        while (!remaining.empty()) {
            std::vector<std::string> choices;
            choices.reserve(remaining.size());
            for (std::size_t index = 0;
                 index < remaining.size(); ++index) {
                choices.push_back(creature_name(
                    observation, remaining[index], false));
            }
            print_choice_box(
                output_, "COMBAT DAMAGE ORDER | FIRST TO LAST",
                choices);
            const std::size_t chosen =
                read_index("choice> ", remaining.size() - 1);
            ordered.push_back(remaining[chosen]);
            remaining.erase(
                remaining.begin() +
                static_cast<std::ptrdiff_t>(chosen));
        }
        return ordered;
    }

    void observe(const PlayerObservation& observation,
                 const GameEvent& event) {
        switch (event.kind) {
        case GameEventKind::TurnStarted:
            output_ << '\n';
            print_box_border(
                output_,
                "TURN " + std::to_string(observation.turn_number) +
                    (event.player == observation.observer
                         ? " | YOUR TURN"
                         : " | LEARNED'S TURN"));
            print_state(output_, observation);
            last_rendered_ = observation;
            return;
        case GameEventKind::PriorityActionSelected:
            if (event.priority_action.has_value() &&
                event.priority_action->kind !=
                    PriorityActionKind::Pass) {
                print_log_line(
                    output_,
                    "[ACTION] " +
                        player_name(observation, event.player) +
                        ": " +
                        action_name(
                            observation,
                            *event.priority_action));
                if (!observation.stack.empty()) {
                    print_state(output_, observation);
                    last_rendered_ = observation;
                }
            }
            return;
        case GameEventKind::StackObjectResolved:
            print_log_line(
                output_,
                "[RESOLVE] " +
                    stack_object_name(
                        observation, *event.stack_object));
            return;
        case GameEventKind::AttackersDeclared: {
            if (event.attackers.empty()) {
                if (event.player != observation.observer) {
                    output_
                        << "[ATTACK] Learned declares no attackers.\n";
                }
                return;
            }
            std::ostringstream attack_log;
            attack_log
                << "[ATTACK] "
                << player_name(observation, event.player)
                << (event.player == observation.observer
                        ? " declare attackers: "
                        : " declares attackers: ");
            for (std::size_t index = 0;
                 index < event.attackers.size(); ++index) {
                if (index != 0) {
                    attack_log << ", ";
                }
                attack_log << creature_name(
                    observation, event.attackers[index], false);
            }
            print_log_line(output_, attack_log.str());
            return;
        }
        case GameEventKind::BlockersDeclared: {
            if (event.blocks.empty()) {
                output_ << "[BLOCK] "
                        << player_name(observation, event.player)
                        << (event.player == observation.observer
                                ? " declare no blockers.\n"
                                : " declares no blockers.\n");
                return;
            }
            std::ostringstream block_log;
            block_log
                << "[BLOCK] "
                << player_name(observation, event.player)
                << (event.player == observation.observer
                        ? " declare blockers: "
                        : " declares blockers: ");
            for (std::size_t index = 0;
                 index < event.blocks.size(); ++index) {
                if (index != 0) {
                    block_log << ", ";
                }
                block_log
                    << creature_name(
                           observation,
                           event.blocks[index].second, false)
                    << " blocks "
                    << creature_name(
                           observation,
                           event.blocks[index].first, false);
            }
            print_log_line(output_, block_log.str());
            return;
        }
        case GameEventKind::DamageOrderChosen: {
            for (std::size_t index = 0;
                 index < event.blocks.size();) {
                const PermanentId attacker =
                    event.blocks[index].first;
                std::size_t end = index + 1;
                while (end < event.blocks.size() &&
                       event.blocks[end].first == attacker) {
                    ++end;
                }
                if (end - index > 1) {
                    std::ostringstream order_log;
                    order_log
                        << "[ORDER] Damage order for "
                        << creature_name(
                               observation, attacker, false)
                        << ": ";
                    for (std::size_t blocker = index;
                         blocker < end; ++blocker) {
                        if (blocker != index) {
                            order_log << ", ";
                        }
                        order_log << creature_name(
                            observation,
                            event.blocks[blocker].second, false);
                    }
                    print_log_line(output_, order_log.str());
                }
                index = end;
            }
            return;
        }
        case GameEventKind::CombatResolved:
            output_ << "[COMBAT] Combat resolves.\n";
            return;
        }
        throw std::logic_error("unknown interactive game event");
    }

    std::istream& input_;
    std::ostream& output_;
    std::optional<PlayerObservation> last_rendered_;
};

std::vector<CardId> interactive_deck(DeckId deck) {
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
    throw std::out_of_range("unknown interactive deck");
}

} // namespace

InteractiveMatchup choose_interactive_matchup(std::uint64_t seed) {
    std::mt19937_64 random(
        seed ^ 0xA17E4AC7D3C5B921ULL);
    const std::size_t human = random() % kDeckCount;
    const std::size_t learned =
        (human + 1 + random() % (kDeckCount - 1)) %
        kDeckCount;
    return {
        .human_deck = static_cast<DeckId>(human),
        .learned_deck = static_cast<DeckId>(learned),
    };
}

InteractiveMatchResult run_interactive_match(
    std::istream& input, std::ostream& output, std::uint64_t seed,
    std::shared_ptr<const LearnedModel> learned_model,
    InteractiveMatchup matchup, std::size_t learned_rollouts) {
    if (!learned_model) {
        throw std::invalid_argument(
            "interactive match requires a frozen Learned model");
    }
    if (learned_rollouts == 0) {
        throw std::invalid_argument(
            "interactive match requires positive Learned rollouts");
    }
    TerminalSession terminal(input, output);
    GameConfig config;
    config.bots[0] = {
        .kind = BotKind::Random,
        .learned_variant = LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = 1,
    };
    config.bots[1] = {
        .kind = BotKind::Learned,
        .learned_variant = LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = learned_rollouts,
        .learned_model = learned_model,
    };
    config.learned_model = learned_model;
    config.human_controllers[0] = terminal.controller();

    Game game(
        interactive_deck(matchup.human_deck),
        interactive_deck(matchup.learned_deck), seed, config);
    try {
        const GameResult result = game.run();
        auto observation = observe_game_state(game.state(), 0);
        observation.revealed_opponent_hand =
            game.state().players[1].hand;
        terminal.print_final(observation, result);
        return {
            .abandoned = false,
            .game = result,
        };
    } catch (const InteractiveQuit& quit) {
        output << (quit.input_closed() ? "\nInput closed; game abandoned.\n"
                                      : "Game abandoned.\n");
        return {
            .abandoned = true,
            .game = std::nullopt,
        };
    }
}

} // namespace old_school
