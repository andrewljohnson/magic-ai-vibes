#include "old_school/game.hpp"
#include "old_school/interactive.hpp"
#include "old_school/learned_iteration.hpp"
#include "old_school/probe_runner.hpp"
#include "old_school/probes.hpp"
#include "old_school/replay_weight_audit.hpp"
#include "old_school/target_factorial_audit.hpp"
#include "old_school/terminal_weight_eval.hpp"
#include "old_school/turn_alignment_audit.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::uint64_t random_seed() {
    std::random_device entropy;
    const auto clock_value = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    return (static_cast<std::uint64_t>(entropy()) << 32U) ^
           static_cast<std::uint64_t>(entropy()) ^ clock_value;
}

std::uint64_t parse_number(std::string_view text, std::string_view option) {
    std::uint64_t value = 0;
    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} ||
        result.ptr != text.data() + text.size()) {
        throw std::invalid_argument("invalid value for " +
                                    std::string(option));
    }
    return value;
}

double parse_finite_real(std::string_view text,
                         std::string_view option,
                         std::string_view requirement) {
    const auto is_digit = [](char character) {
        return character >= '0' && character <= '9';
    };
    std::size_t cursor = 0;
    if (cursor < text.size() &&
        (text[cursor] == '+' || text[cursor] == '-')) {
        ++cursor;
    }
    const std::size_t integer_start = cursor;
    while (cursor < text.size() && is_digit(text[cursor])) {
        ++cursor;
    }
    const bool has_integer_digits = cursor != integer_start;
    bool has_fraction_digits = false;
    if (cursor < text.size() && text[cursor] == '.') {
        ++cursor;
        const std::size_t fraction_start = cursor;
        while (cursor < text.size() && is_digit(text[cursor])) {
            ++cursor;
        }
        has_fraction_digits = cursor != fraction_start;
    }
    if (!has_integer_digits && !has_fraction_digits) {
        cursor = text.size() + 1;
    }
    if (cursor < text.size() &&
        (text[cursor] == 'e' || text[cursor] == 'E')) {
        ++cursor;
        if (cursor < text.size() &&
            (text[cursor] == '+' || text[cursor] == '-')) {
            ++cursor;
        }
        const std::size_t exponent_start = cursor;
        while (cursor < text.size() && is_digit(text[cursor])) {
            ++cursor;
        }
        if (cursor == exponent_start) {
            cursor = text.size() + 1;
        }
    }
    const std::string owned(text);
    char* parsed_end = nullptr;
    const double value = cursor == text.size()
                             ? std::strtod(owned.c_str(), &parsed_end)
                             : 0.0;
    if (cursor != text.size() ||
        parsed_end != owned.c_str() + owned.size() ||
        !std::isfinite(value)) {
        throw std::invalid_argument(
            std::string(option) + std::string(requirement));
    }
    return value;
}

double parse_positive_real(std::string_view text,
                           std::string_view option) {
    const double value = parse_finite_real(
        text, option, " must be a positive finite number");
    if (value <= 0.0) {
        throw std::invalid_argument(
            std::string(option) +
            " must be a positive finite number");
    }
    return value;
}

double parse_unit_interval_real(std::string_view text,
                                std::string_view option) {
    constexpr std::string_view requirement =
        " must be a finite number in [0, 1]";
    const double value =
        parse_finite_real(text, option, requirement);
    if (value < 0.0 || value > 1.0) {
        throw std::invalid_argument(
            std::string(option) + std::string(requirement));
    }
    return value;
}

std::string format_real(double value) {
    for (int precision = 1;
         precision <=
         std::numeric_limits<double>::max_digits10;
         ++precision) {
        std::ostringstream output;
        output.imbue(std::locale::classic());
        output << std::setprecision(precision) << value;
        const std::string candidate = output.str();
        char* parsed_end = nullptr;
        const double reparsed =
            std::strtod(candidate.c_str(), &parsed_end);
        if (parsed_end ==
                candidate.c_str() + candidate.size() &&
            reparsed == value) {
            return candidate;
        }
    }
    throw std::runtime_error("failed to format real number");
}

std::string value_continuation_epsilon_suffix(double epsilon) {
    if (epsilon == 0.0) {
        return {};
    }
    return " (continuation epsilon=" + format_real(epsilon) + ")";
}

void print_help(std::string_view executable) {
    std::cout
        << "Usage: " << executable
        << " [--games N] [--seed N] [--bots MODE] [--rollouts N]"
           " [--deep-rollouts N] [--learned-rollouts N]"
           " [--learned-generations N]"
           " [--value-continuation-epsilon X]"
           " [--refresh-value-challenger-cache]"
           " [--train-games N] [--train-seed N]\n"
        << "       " << executable
        << " --interactive [--seed N] [--train-games N]"
           " [--train-seed N] [--learned-generations N]"
           " [--learned-rollouts N]"
           " [--value-continuation-epsilon X]"
           " [--refresh-value-challenger-cache]\n"
        << "       " << executable
        << " --benchmark [--games N] [--challenger BOT]"
           " [--baseline BOT] [--learned-rollouts N]"
           " [--value-continuation-epsilon X]"
           " [--actor-policy-epochs N]"
           " [--actor-policy-rate X]"
           " [--refresh-value-challenger-cache]"
           " [--refresh-value-g8-cache]"
           " [--refresh-value-mix50-cache]\n"
        << "       " << executable
        << " --stability [--stability-runs N] [--games N]"
           " [--value-continuation-epsilon X]"
           " [--refresh-value-challenger-cache]\n\n"
        << "       " << executable
        << " --diagnose-white-plan [--seed N] [--train-games N]\n"
        << "       " << executable
        << " --diagnose-value-context\n"
        << "       " << executable
        << " --diagnose-force-spike-teacher --learned-generations N"
           " [--train-games N] [--train-seed N]\n"
        << "       " << executable
        << " --diagnose-value-pass-dominance --seed 202607260947\n"
        << "       " << executable
        << " --train-p-family N [--seed N] [--train-games 800]"
           " [--train-seed 424242]\n"
        << "       " << executable
        << " --diagnose-p1-fit [--seed N] [--train-games 800]"
           " [--train-seed 424242]\n"
        << "       " << executable
        << " --score-p1r-probes --seed 577215"
           " --train-games 800 --train-seed 424242\n"
        << "       " << executable
        << " --diagnose-terminal-credit --train-games 800"
           " --train-seed 424242\n"
        << "       " << executable
        << " --train-terminal-weight-c17 --train-games 800"
           " --train-seed 424242\n"
        << "       " << executable
        << " --audit-dc1-dominance --train-games 800"
           " --train-seed 424242 --learned-generations 16\n"
        << "       " << executable
        << " --audit-dc1-action-census --train-games 800"
           " --train-seed 424242 --learned-generations 16\n"
        << "       " << executable
        << " --audit-v3-blue-stack-regret --train-games 800"
           " --train-seed 424242 --learned-generations 16\n"
        << "       " << executable
        << " --audit-calendar-turn-targets\n"
        << "       " << executable
        << " --audit-calendar-eight-targets\n"
        << "       " << executable
        << " --audit-replay-weights\n"
        << "       " << executable
        << " --variance-study [--games N] [--train-games N]\n"
        << "       " << executable
        << " --score-probes [--probe-worlds N] [--probe-horizon N]"
           " [--probe-corpus dev-v3|validation-v1]"
           " [--learned-rollouts N]"
           " [--value-continuation-epsilon X]"
           " [--learned-generations N]"
           " [--challenger learned-value-context-cN|"
           "learned-value-dense-masked-cN|"
           "learned-value-dense-context-cN]"
           " [--actor-generation 0|1] [--value-generation 0|8]"
           " [--value-recipe canonical|mix50]"
           " [--probe-cache PATH]"
           " [--refresh-probe-cache]"
           " [--refresh-value-challenger-cache]"
           " [--refresh-value-g8-cache]"
           " [--refresh-value-mix50-cache]"
           " [--actor-policy-epochs N] [--actor-policy-rate X]\n"
        << "       " << executable
        << " --evolve-deck [--generations N] [--population N] "
           "[--games N] [--evolve-pilot BOT]"
           " [--learned-rollouts N] [--train-games N]"
           " [--train-seed N]\n\n"
        << "Simulates an Old School Magic round robin with legal bot play.\n"
        << "  Green: 18 Forest, 9 Grizzly Bears, 8 Ironroot Treefolk, "
           "4 Giant Growth, 1 Tsunami\n"
        << "  Red: 15 Mountain, 9 Lightning Bolt, 7 Ironclaw Orcs, "
           "4 Gray Ogre, 3 Hill Giant, 2 Fire Elemental\n"
        << "  Blue: 15 Island, 1 Mox Sapphire, 1 Sol Ring, "
           "1 Ancestral Recall, 1 Time Walk, 1 Braingeyser, "
           "4 Flying Men, 4 Force Spike, 8 Counterspell, "
           "4 Air Elemental\n"
        << "  White: 22 Plains, 3 Millstone, 15 Moat\n"
        << "  RU Aggro: 13 Mountain, 4 Island, 3 Flying Men, "
           "5 Ironclaw Orcs, 2 Gray Ogre, 8 Hill Giant, "
           "3 Lightning Bolt, 2 Disintegrate\n\n"
        << "Options:\n"
        << "  --games N       Games per matchup (default: 100)\n"
        << "  --seed N        Reproducible random seed (default: random)\n"
        << "  --bots MODE     mixed, random, monte-carlo, "
           "deep-monte-carlo, handcrafted, learned-value, or "
           "learned-actor (default: mixed)\n"
        << "  --rollouts N    Monte Carlo continuations per legal action "
           "(default: 2)\n"
        << "  --deep-rollouts N  Deep Monte Carlo continuations per "
           "legal action (default: 8)\n"
        << "  --learned-rollouts N  Learned search worlds per legal "
           "action, including Value candidate scoring under "
           "--score-probes (default: 2; probe minimum: 2)\n"
        << "  --learned-generations N  Select the separate Value "
           "challenger for interactive, stability, mixed/learned "
           "simulation, or probe scoring; 0 keeps legacy G0 "
           "(default: 0)\n"
        << "  --value-continuation-epsilon X  Research-only epsilon "
           "for Value-mirror continuation priority actions in [0,1]; "
           "the deployed root remains greedy (default: 0)\n"
        << "  --value-pass-dominance  Challenger-only exact "
           "Pass-dominance filter for Learned Value Priority choices\n"
        << "  --diagnose-value-pass-dominance  Exclusive PD0 mechanism "
           "check at seed 202607260947: in-memory G0 T800/S424242/K2 "
           "and load-only exact C16 T800/S424242/K8; accepts no other "
           "options and exits 0/1/2 for pass/reject/infrastructure\n"
        << "  --train-games N  Training games for the selected learned "
           "model "
           "(default: 800)\n"
        << "  --train-seed N   Learned model seed, independent of --seed "
           "(default: 424242)\n"
        << "  --interactive   Play a seeded random five-deck matchup "
           "against frozen Learned Value\n"
        << "  --benchmark     Run the paired bot-strength harness\n"
        << "  --challenger BOT  Benchmark challenger "
           "(default: handcrafted; learned generations: "
           "learned-value-g0..g8, learned-value-cN, "
           "learned-value-context-cN, "
           "learned-value-dense-masked-cN, "
           "learned-value-dense-context-cN, "
           "learned-value-tw50-c17, learned-value-tw75-c17, "
           "learned-value-mix50-g8, "
           "learned-actor-g0/g1); under --score-probes, "
           "the context-ablation tokens add their ordered cells "
           "after matching S0 selected by "
           "--learned-generations N\n"
        << "  --baseline BOT    Benchmark baseline "
           "(default: monte-carlo)\n"
        << "  --stability     Validate Learned against all policies across "
           "seed panels\n"
        << "  --stability-runs N  Number of independent runs "
           "(default: 8)\n"
        << "  --diagnose-white-plan  Compare K=2 and K=64 Learned root "
           "rankings on a held-out White lock state\n"
        << "  --diagnose-value-context  Audit phase/pass context omitted "
           "from the current Value observation; accepts no other options\n"
        << "  --diagnose-force-spike-teacher  Eval-only K=256 audit of "
           "the frozen S0 teacher on Force Spike live/payable and RU "
           "Pass/X=0; requires --learned-generations N and accepts only "
           "training options\n"
        << "  --train-p-family N  Train canonical outcome-tilted "
           "Priority checkpoints P1..PN from exact Value Challenger C16; "
           "N is 1..16, the recipe is fixed at K=8/H=4, root cap 32, "
           "and 500 turns, and only --seed/--train-games/--train-seed "
           "are accepted\n"
        << "  --diagnose-p1-fit  Collect canonical P1 once, then fit "
           "five independent same-parent epoch/rate cells; accepts only "
           "--seed, --train-games, and --train-seed\n"
        << "  --score-p1r-probes  Reconstruct revised P1R "
           "(128 epochs, rate 0.003) and run its immutable dev-v3 and "
           "validation-v1 reject-only gates; accepts only --seed, "
           "--train-games, and --train-seed\n"
        << "  --diagnose-terminal-credit  Eval-only K=1024/H=128 "
           "terminal-outcome audit of exact Value Challenger C16 P0; "
           "uses a Value mirror with zero continuation epsilon and "
           "Priority residual, shallow-prior blend off, and required "
           "terminal results; accepts only --train-games and "
           "--train-seed\n"
        << "  --train-terminal-weight-c17  Train the fixed same-shard "
           "TW50/TW75 C17 family from exact C16 using raw seed "
           "202607260311; accepts only --train-games 800 and "
           "--train-seed 424242 and writes a distinct atomic bundle\n"
        << "  --evaluate-terminal-weight-c17  Exclusive load-only "
           "TW-C17 gate: HOLD1 seed 202607260312, then conditional "
           "same-deck gameplay seed 202607260313; accepts no other "
           "options and exits 0/1/2 for pass/reject/infrastructure\n"
        << "  --audit-dc1-dominance  Evaluation-only Environment-v3 "
           "resource-dominance mining audit of exact C16; fixed "
           "all-five 2x40-game train/heldout blocks and K=8; trains "
           "and deploys nothing\n"
        << "  --audit-dc1-action-census  Load-only DC1-B0 replay of "
           "every Priority legal-action set; fixed all-five 2x40-game "
           "train/heldout blocks, K=8, max_turns=128, and diagnostic "
           "ceiling 512; performs no pair or density evaluation\n"
        << "  --audit-v3-blue-stack-regret  Load-only BSR0 audit of "
           "actual Blue-held opponent-stack choices from 200 balanced "
           "loss-source games; fixed C16, K64+64/H8 Learned-mirror "
           "reference, hidden clone, and 40-root rare-error gate\n"
        << "  --audit-calendar-turn-targets  Exclusive load-only TA4-0 "
           "audit of record-offset-4 versus calendar-turn-4 bootstrap "
           "targets; fixed seed 202607260501 and frozen C16, accepts no "
           "other options, and exits 0/1/2 for "
           "pass/reject/infrastructure\n"
        << "  --audit-calendar-eight-targets  Exclusive load-only CT8-0 "
           "four-arm audit of record/calendar units at four/eight-turn "
           "bootstrap horizons; fixed seed 202607260621 and frozen C16, "
           "accepts no other options, and exits 0/1/2 for "
           "pass/reject/infrastructure\n"
        << "  --audit-replay-weights  Exclusive load-only RB0-0 audit "
           "of unit versus actor-game/exact-calendar-turn replay "
           "weights; fixed seed 202607261047 and frozen C16, accepts no "
           "other options, and exits 0/1/2 for "
           "pass/reject/infrastructure\n"
        << "  --variance-study  Run fixed 3x3 training/evaluation seed "
           "study (default: 5 games)\n"
        << "  --score-probes   Label/score an offline decision-probe "
           "corpus\n"
        << "  --probe-corpus NAME  dev-v3 (20-position development "
           "default) or validation-v1 (harvested RU X=0 regression)\n"
        << "  --probe-worlds N  Common worlds per reference candidate "
           "(default: 128; minimum: 2)\n"
        << "  --probe-horizon N  Actor-mirror reference horizon in turns "
           "(default: 12)\n"
        << "  --actor-generation N  Score frozen Actor G0 or G1 "
           "(default: 0; requires --score-probes)\n"
        << "  --value-generation N  Score frozen Value G0 or G8 "
           "(default: 0; requires --score-probes)\n"
        << "  --value-recipe NAME  Select canonical or mix50 Value G8 "
           "checkpoints (default: canonical; requires --score-probes)\n"
        << "  --actor-policy-epochs N  G1 policy-fit epochs "
           "(default: 2; requires a selected Actor G1)\n"
        << "  --actor-policy-rate X  G1 policy-fit learning rate "
           "(default: 0.001; requires a selected Actor G1)\n"
        << "  --probe-cache PATH  Deterministic label cache "
           "(default depends on --probe-corpus)\n"
        << "  --refresh-probe-cache  Regenerate matching probe labels "
           "atomically\n"
        << "  --refresh-value-challenger-cache  Retrain and atomically "
           "replace every selected state-only, sparse-context, or "
           "dense-context Value C<N> "
           "artifact\n"
        << "  --refresh-value-g8-cache  Retrain and atomically replace "
           "the selected canonical Value G8 artifact\n"
        << "  --refresh-value-mix50-cache  Retrain and atomically replace "
           "the selected Value G8 Late-Mix50 artifact\n"
        << "  --evolve-deck   Evolve a 40-card deck against the current "
           "metagame\n"
        << "  --evolve-pilot BOT  Evolution pilot: handcrafted "
           "(default), random, monte-carlo, deep-monte-carlo, or "
           "learned-value-context-cN\n"
        << "  --generations N  Evolution generations (default: 10)\n"
        << "  --population N   Candidate decks per generation "
           "(default: 16)\n"
        << "  With --evolve-deck, --games is paired repetitions per "
           "metagame deck (default: 4).\n"
        << "  --help          Show this help\n";
}

struct BotSelection {
    enum class ValueFamily : std::uint8_t {
        LegacyG0,
        Challenger,
        ContextChallenger,
        DenseMaskedChallenger,
        DenseContextChallenger,
        TerminalWeight50,
        TerminalWeight75,
        Canonical,
        Mix50,
    };

    old_school::BotKind kind = old_school::BotKind::Random;
    old_school::LearnedVariant learned_variant =
        old_school::LearnedVariant::ValueSearchChampion;
    ValueFamily value_family = ValueFamily::LegacyG0;
    old_school::LearnedValueG8Recipe value_recipe =
        old_school::LearnedValueG8Recipe::CanonicalAllSearchLate;
    std::size_t value_generation = 0;
    std::size_t actor_generation = 0;
};

bool parse_positive_generation_token(
    std::string_view value, std::string_view prefix,
    std::size_t& generation) {
    if (!value.starts_with(prefix)) {
        return false;
    }
    const std::string_view suffix = value.substr(prefix.size());
    std::uint64_t parsed = 0;
    const auto result =
        std::from_chars(suffix.data(),
                        suffix.data() + suffix.size(), parsed);
    if (result.ec != std::errc{} ||
        result.ptr != suffix.data() + suffix.size() ||
        parsed == 0 ||
        parsed > std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    generation = static_cast<std::size_t>(parsed);
    return true;
}

BotSelection parse_bot(std::string_view value) {
    if (value == "random") {
        return {.kind = old_school::BotKind::Random};
    }
    if (value == "monte-carlo" || value == "mc") {
        return {.kind = old_school::BotKind::MonteCarlo};
    }
    if (value == "deep-monte-carlo" || value == "deep-mc") {
        return {.kind = old_school::BotKind::DeepMonteCarlo};
    }
    if (value == "handcrafted" || value == "handcoded" ||
        value == "strategic") {
        return {.kind = old_school::BotKind::Handcrafted};
    }
    if (value == "learned" || value == "learned-value" ||
        value == "learned-value-g0") {
        return {
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::ValueSearchChampion,
            .value_generation = 0,
        };
    }
    if (value == "learned-value-tw50-c17" ||
        value == "learned-value-tw75-c17") {
        return {
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::ValueSearchChampion,
            .value_family =
                value == "learned-value-tw50-c17"
                    ? BotSelection::ValueFamily::
                          TerminalWeight50
                    : BotSelection::ValueFamily::
                          TerminalWeight75,
            .value_generation = 17,
        };
    }
    std::size_t challenger_generation = 0;
    if (parse_positive_generation_token(
            value, "learned-value-dense-masked-c",
            challenger_generation)) {
        return {
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::ValueSearchChampion,
            .value_family =
                BotSelection::ValueFamily::DenseMaskedChallenger,
            .value_generation = challenger_generation,
        };
    }
    if (parse_positive_generation_token(
            value, "learned-value-dense-context-c",
            challenger_generation)) {
        return {
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::ValueSearchChampion,
            .value_family =
                BotSelection::ValueFamily::DenseContextChallenger,
            .value_generation = challenger_generation,
        };
    }
    if (parse_positive_generation_token(
            value, "learned-value-context-c",
            challenger_generation)) {
        return {
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::ValueSearchChampion,
            .value_family =
                BotSelection::ValueFamily::ContextChallenger,
            .value_generation = challenger_generation,
        };
    }
    if (parse_positive_generation_token(
            value, "learned-value-c", challenger_generation)) {
        return {
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::ValueSearchChampion,
            .value_family =
                BotSelection::ValueFamily::Challenger,
            .value_generation = challenger_generation,
        };
    }
    constexpr std::string_view value_generation_prefix =
        "learned-value-g";
    if (value.starts_with(value_generation_prefix) &&
        value.size() == value_generation_prefix.size() + 1 &&
        value.back() >= '0' && value.back() <= '8') {
        return {
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::ValueSearchChampion,
            .value_family =
                BotSelection::ValueFamily::Canonical,
            .value_generation =
                static_cast<std::size_t>(value.back() - '0'),
        };
    }
    if (value == "learned-value-mix50-g8") {
        return {
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::ValueSearchChampion,
            .value_family = BotSelection::ValueFamily::Mix50,
            .value_recipe =
                old_school::LearnedValueG8Recipe::LateMix50,
            .value_generation = 8,
        };
    }
    if (value == "learned-actor" || value == "actor" ||
        value == "learned-actor-g0") {
        return {
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::UnifiedActor,
            .actor_generation = 0,
        };
    }
    if (value == "learned-actor-g1") {
        return {
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::UnifiedActor,
            .actor_generation = 1,
        };
    }
    throw std::invalid_argument("invalid bot name: " +
                                std::string(value));
}

struct BotFieldSelection {
    old_school::BotField field = old_school::BotField::Mixed;
    old_school::LearnedVariant learned_variant =
        old_school::LearnedVariant::ValueSearchChampion;
};

BotFieldSelection parse_bot_field(std::string_view value) {
    if (value == "mixed") {
        return {.field = old_school::BotField::Mixed};
    }
    if (value == "random") {
        return {.field = old_school::BotField::Random};
    }
    if (value == "monte-carlo" || value == "mc") {
        return {.field = old_school::BotField::MonteCarlo};
    }
    if (value == "deep-monte-carlo" || value == "deep-mc") {
        return {.field = old_school::BotField::DeepMonteCarlo};
    }
    if (value == "handcrafted" || value == "handcoded" ||
        value == "strategic") {
        return {.field = old_school::BotField::Handcrafted};
    }
    if (value == "learned" || value == "learned-value") {
        return {
            .field = old_school::BotField::Learned,
            .learned_variant =
                old_school::LearnedVariant::ValueSearchChampion,
        };
    }
    if (value == "learned-actor" || value == "actor") {
        return {
            .field = old_school::BotField::Learned,
            .learned_variant =
                old_school::LearnedVariant::UnifiedActor,
        };
    }
    throw std::invalid_argument(
        "invalid value for --bots (use mixed, random, monte-carlo, "
        "deep-monte-carlo, handcrafted, learned-value, or "
        "learned-actor)");
}

std::string bot_field_name(
    old_school::BotField field,
    old_school::LearnedVariant learned_variant,
    std::size_t learned_generations) {
    switch (field) {
    case old_school::BotField::Random:
        return "random only";
    case old_school::BotField::MonteCarlo:
        return "Monte Carlo only";
    case old_school::BotField::DeepMonteCarlo:
        return "Deep Monte Carlo only";
    case old_school::BotField::Handcrafted:
        return "Handcrafted Policy only";
    case old_school::BotField::Learned:
        if (learned_variant ==
            old_school::LearnedVariant::UnifiedActor) {
            return "Learned Unified Actor only";
        }
        return learned_generations == 0
                   ? "Learned Value G0 only"
                   : "Learned Value Challenger C" +
                         std::to_string(learned_generations) +
                         " only";
    case old_school::BotField::Mixed:
        return "mixed Random, Monte Carlo, Deep Monte Carlo, "
               "Handcrafted Policy, and " +
               (learned_generations == 0
                    ? std::string("Learned Value G0")
                    : "Learned Value Challenger C" +
                          std::to_string(
                              learned_generations));
    }
    return "unknown";
}

void print_deck_stats(std::string_view label,
                      const old_school::DeckSimulationStats& stats) {
    std::cout << label << '\n'
              << "  Record: " << stats.wins << '-' << stats.losses << '-'
              << stats.draws << " (" << stats.win_rate() << "% wins)\n"
              << "  On the play: " << stats.on_play_wins << '/'
              << stats.on_play_games << " wins ("
              << stats.on_play_win_rate() << "%)\n"
              << "  On the draw: " << stats.on_draw_wins << '/'
              << stats.on_draw_games << " wins ("
              << stats.on_draw_win_rate() << "%)\n"
              << "  Per game: " << stats.average_ending_life()
              << " ending life, " << stats.average_cards_drawn()
              << " cards drawn, " << stats.average_lands_played()
              << " lands played, " << stats.average_spells_cast()
              << " spells cast, " << stats.average_spells_countered()
              << " spells countered, "
              << stats.average_damage_to_opponent()
              << " damage to opponent, " << stats.average_cards_milled()
              << " cards milled\n";
}

void print_bot_stats(std::string_view label,
                     const old_school::BotSimulationStats& stats) {
    std::cout << label << '\n'
              << "  Record: " << stats.wins << '-' << stats.losses << '-'
              << stats.draws << " across " << stats.games
              << " seat-games (" << stats.win_rate() << "% wins)\n"
              << "  Per seat-game: " << stats.average_decisions()
              << " nontrivial priority decisions, "
              << stats.average_rollouts()
              << " rollout continuations\n"
              << "  Rollouts per decision: "
              << stats.average_rollouts_per_decision() << '\n';
}

void print_delta(double delta) {
    if (delta >= 0.0) {
        std::cout << '+';
    }
    std::cout << delta << " pp";
}

void print_deck_bot_benefit(const old_school::TournamentSummary& result) {
    struct Benefit {
        old_school::DeckId deck;
        double ranking_delta;
    };

    const auto random_index =
        static_cast<std::size_t>(old_school::BotKind::Random);
    const auto monte_carlo_index =
        static_cast<std::size_t>(old_school::BotKind::MonteCarlo);
    const auto deep_index =
        static_cast<std::size_t>(old_school::BotKind::DeepMonteCarlo);
    const auto handcrafted_index =
        static_cast<std::size_t>(old_school::BotKind::Handcrafted);
    const auto learned_index =
        static_cast<std::size_t>(old_school::BotKind::Learned);
    const bool has_deep_comparison =
        result.bots[random_index].games > 0 &&
        result.bots[deep_index].games > 0;
    const bool has_learned_comparison =
        result.bots[random_index].games > 0 &&
        result.bots[learned_index].games > 0;
    const bool has_monte_carlo_comparison =
        result.bots[random_index].games > 0 &&
        result.bots[monte_carlo_index].games > 0;
    const auto learned_lift =
        old_school::compare_learned_deck_lifts(result);

    std::cout << "\nBot benefit by deck\n";
    if (!has_learned_comparison && !has_deep_comparison &&
        !has_monte_carlo_comparison) {
        std::cout << "  Not available; use --bots mixed to compare "
                     "policies.\n";
        return;
    }

    std::vector<Benefit> benefits;
    for (std::size_t deck = 0; deck < result.deck_bots.size(); ++deck) {
        const auto& random = result.deck_bots[deck][random_index];
        const auto& comparison =
            has_learned_comparison
                ? result.deck_bots[deck][learned_index]
                : (has_deep_comparison
                       ? result.deck_bots[deck][deep_index]
                       : result.deck_bots[deck][monte_carlo_index]);
        if (random.games == 0 || comparison.games == 0) {
            continue;
        }
        benefits.push_back({
            .deck = static_cast<old_school::DeckId>(deck),
            .ranking_delta =
                comparison.win_rate() - random.win_rate(),
        });
    }
    std::sort(benefits.begin(), benefits.end(),
              [](const Benefit& left, const Benefit& right) {
                  return left.ranking_delta > right.ranking_delta;
              });

    std::cout << "  Ranked by "
              << (has_learned_comparison
                      ? "Learned Value"
                      : (has_deep_comparison ? "Deep Monte Carlo"
                                             : "Monte Carlo"))
              << " win-rate lift over Random:\n";
    for (std::size_t rank = 0; rank < benefits.size(); ++rank) {
        const auto deck_index =
            static_cast<std::size_t>(benefits[rank].deck);
        const auto& random = result.deck_bots[deck_index][random_index];
        std::cout << "  " << rank + 1 << ". "
                  << old_school::deck_name(benefits[rank].deck)
                  << ": Random " << random.win_rate() << "% ("
                  << random.games << " games)";

        const auto& monte_carlo =
            result.deck_bots[deck_index][monte_carlo_index];
        if (monte_carlo.games > 0) {
            std::cout << ", Monte Carlo " << monte_carlo.win_rate()
                      << "% (";
            print_delta(monte_carlo.win_rate() - random.win_rate());
            std::cout << ", " << monte_carlo.games << " games)";
        }

        const auto& deep = result.deck_bots[deck_index][deep_index];
        if (deep.games > 0) {
            std::cout << ", Deep " << deep.win_rate() << "% (";
            print_delta(deep.win_rate() - random.win_rate());
            std::cout << ", " << deep.games << " games)";
        }

        const auto& handcrafted =
            result.deck_bots[deck_index][handcrafted_index];
        if (handcrafted.games > 0) {
            std::cout << ", Handcrafted " << handcrafted.win_rate()
                      << "% (";
            print_delta(handcrafted.win_rate() - random.win_rate());
            std::cout << ", " << handcrafted.games << " games)";
        }

        const auto& learned =
            result.deck_bots[deck_index][learned_index];
        if (learned.games > 0) {
            std::cout << ", Learned " << learned.win_rate() << "% (";
            print_delta(learned.win_rate() - random.win_rate());
            std::cout << ", " << learned.games << " games)";
        }
        const auto& lift = learned_lift.decks[deck_index];
        if (lift.available) {
            std::cout << " [Learned lift "
                      << (lift.learned_is_best ? "PASS" : "FAIL")
                      << ']';
        }
        std::cout << '\n';
    }
    if (has_learned_comparison) {
        std::cout << "  Learned per-deck lift gate (all five decks): "
                  << (learned_lift.learned_is_best_on_every_deck()
                          ? "PASS"
                          : "FAIL");
        if (!learned_lift.complete()) {
            std::cout << " (all five policies need samples)";
        }
        std::cout << '\n';
    }
}

old_school::BotConfig bot_config(BotSelection selection,
                            std::size_t rollouts,
                            std::size_t deep_rollouts,
                            std::size_t training_games,
                            std::size_t learned_rollouts,
                            double value_continuation_epsilon = 0.0) {
    old_school::BotConfig config;
    config.kind = selection.kind;
    config.learned_variant = selection.learned_variant;
    config.training_games = training_games;
    switch (selection.kind) {
    case old_school::BotKind::Random:
    case old_school::BotKind::Handcrafted:
        config.rollouts_per_action = 1;
        return config;
    case old_school::BotKind::Learned:
        config.rollouts_per_action = learned_rollouts;
        if (selection.learned_variant ==
            old_school::LearnedVariant::ValueSearchChampion) {
            config.value_continuation_epsilon =
                value_continuation_epsilon;
        }
        return config;
    case old_school::BotKind::MonteCarlo:
        config.rollouts_per_action = rollouts;
        return config;
    case old_school::BotKind::DeepMonteCarlo:
        config.rollouts_per_action = deep_rollouts;
        return config;
    }
    throw std::invalid_argument("unknown bot kind");
}

old_school::BotConfig bot_config(old_school::BotKind kind,
                            std::size_t rollouts,
                            std::size_t deep_rollouts,
                            std::size_t training_games,
                            std::size_t learned_rollouts = 2,
                            double value_continuation_epsilon = 0.0) {
    return bot_config(
        {.kind = kind,
         .learned_variant =
             old_school::LearnedVariant::ValueSearchChampion},
        rollouts, deep_rollouts, training_games,
        learned_rollouts, value_continuation_epsilon);
}

std::shared_ptr<const old_school::LearnedModel>
train_value_challenger_with_progress(
    std::size_t training_games,
    std::uint64_t training_seed,
    std::size_t generations,
    bool refresh_cache);

std::shared_ptr<const old_school::LearnedModel>
train_value_context_challenger_with_progress(
    std::size_t training_games,
    std::uint64_t training_seed,
    std::size_t generations,
    bool refresh_cache);

std::shared_ptr<const old_school::LearnedModel>
train_value_dense_context_challenger_with_progress(
    std::size_t training_games,
    std::uint64_t training_seed,
    std::size_t generations,
    old_school::LearnedValueDenseContextTreatment treatment,
    bool refresh_cache);

std::shared_ptr<const old_school::LearnedModel>
train_frozen_learned_model(old_school::LearnedVariant variant,
                           std::size_t training_games,
                           std::uint64_t training_seed,
                           std::size_t challenger_generations,
                           bool refresh_challenger_cache) {
    if (variant == old_school::LearnedVariant::UnifiedActor) {
        return old_school::train_learned_actor_model(
            training_games, training_seed);
    }
    if (challenger_generations > 0) {
        return train_value_challenger_with_progress(
            training_games, training_seed,
            challenger_generations,
            refresh_challenger_cache);
    }
    return old_school::train_learned_value_champion(
        training_games, training_seed);
}

std::shared_ptr<const old_school::LearnedModel>
train_value_g0_with_progress(std::size_t training_games,
                             std::uint64_t training_seed) {
    std::cout << "Training frozen Value G0 (seed " << training_seed
              << ", " << training_games << " games)..."
              << std::flush;
    const auto started = std::chrono::steady_clock::now();
    auto model = old_school::train_learned_value_champion(
        training_games, training_seed);
    const std::chrono::duration<double> elapsed =
        std::chrono::steady_clock::now() - started;
    std::cout << " done (" << std::fixed << std::setprecision(2)
              << elapsed.count() << "s)\n"
              << "  Value G0 fingerprint: "
              << old_school::learned_model_fingerprint(model) << '\n';
    return model;
}

std::shared_ptr<const old_school::LearnedModel>
train_value_challenger_with_progress(
    std::size_t training_games,
    std::uint64_t training_seed,
    std::size_t generations,
    bool refresh_cache) {
    const std::string cache_path =
        old_school::learned_value_challenger_cache_path(
            training_games, training_seed, generations);
    std::error_code exists_error;
    const bool cache_exists =
        std::filesystem::exists(cache_path, exists_error);
    if (exists_error) {
        throw std::runtime_error(
            "cannot inspect Value Challenger C" +
            std::to_string(generations) + " artifact cache '" +
            cache_path + "': " + exists_error.message());
    }

    const auto started = std::chrono::steady_clock::now();
    std::shared_ptr<const old_school::LearnedModel> model;
    if (cache_exists && !refresh_cache) {
        std::cout
            << "Loading immutable Value Challenger C"
            << generations << " artifact (seed " << training_seed
            << ", " << training_games << " initial games) from "
            << cache_path << "..." << std::flush;
        try {
            model = old_school::
                        load_learned_value_challenger_artifact(
                            cache_path, training_games,
                            training_seed, generations)
                        .model();
        } catch (const std::exception& error) {
            throw std::runtime_error(
                "Value Challenger C" +
                std::to_string(generations) +
                " artifact cache '" + cache_path +
                "' is invalid: " + error.what() +
                "; rerun this challenger route with "
                "--refresh-value-challenger-cache to regenerate it");
        }
    } else {
        std::cout << "Training frozen Value Challenger C"
                  << generations << " (seed " << training_seed
                  << ", " << training_games
                  << " initial games)..." << std::flush;
        const auto artifact =
            old_school::train_learned_value_challenger_artifact(
                training_games, training_seed, generations);
        model = artifact.model();
        old_school::
            write_learned_value_challenger_artifact_atomic(
                cache_path, artifact);
    }
    const std::chrono::duration<double> elapsed =
        std::chrono::steady_clock::now() - started;
    std::cout << " done (" << std::fixed << std::setprecision(2)
              << elapsed.count() << "s)\n"
              << "  Value Challenger C" << generations
              << " fingerprint: "
              << old_school::learned_model_fingerprint(model) << '\n'
              << "  Value Challenger C" << generations
              << " artifact cache: "
              << (cache_exists && !refresh_cache
                      ? "loaded "
                      : "generated ")
              << cache_path << '\n';
    return model;
}

std::shared_ptr<const old_school::LearnedModel>
load_value_challenger_with_progress(
    std::size_t training_games,
    std::uint64_t training_seed,
    std::size_t generations) {
    const std::string cache_path =
        old_school::learned_value_challenger_cache_path(
            training_games, training_seed, generations);
    std::error_code exists_error;
    const bool cache_exists =
        std::filesystem::exists(cache_path, exists_error);
    if (exists_error) {
        throw std::runtime_error(
            "cannot inspect pinned Value Challenger C" +
            std::to_string(generations) + " artifact '" +
            cache_path + "': " + exists_error.message());
    }
    if (!cache_exists) {
        throw std::runtime_error(
            "evaluation-only route requires the existing pinned "
            "Value Challenger C" +
            std::to_string(generations) + " artifact '" +
            cache_path + "'; generate and freeze it in a separate "
            "training run");
    }

    std::cout
        << "Loading pinned Value Challenger C" << generations
        << " artifact (seed " << training_seed << ", "
        << training_games << " initial games) from "
        << cache_path << "..." << std::flush;
    const auto started = std::chrono::steady_clock::now();
    std::shared_ptr<const old_school::LearnedModel> model;
    try {
        model =
            old_school::load_learned_value_challenger_artifact(
                cache_path, training_games, training_seed,
                generations)
                .model();
    } catch (const std::exception& error) {
        throw std::runtime_error(
            "pinned Value Challenger C" +
            std::to_string(generations) + " artifact '" +
            cache_path + "' is invalid: " + error.what());
    }
    const std::chrono::duration<double> elapsed =
        std::chrono::steady_clock::now() - started;
    std::cout
        << " done (" << std::fixed << std::setprecision(2)
        << elapsed.count() << "s)\n"
        << "  Pinned Value Challenger C" << generations
        << " fingerprint: "
        << old_school::learned_model_fingerprint(model) << '\n';
    return model;
}

void require_canonical_terminal_weight_bundle(
    const old_school::LearnedTerminalWeightC17Report& report) {
    if (report.training_games != 800 ||
        report.parent_training_seed != 424242 ||
        report.parent_generations != 16 ||
        report.shard_seed !=
            old_school::kTerminalWeightC17ShardSeed ||
        report.balanced_blocks != 5 ||
        report.scheduled_games != 200 ||
        report.bootstrap_distance != 4 ||
        report.collection_search_worlds != 1 ||
        report.collection_horizon_turns !=
            old_school::kLearnedValueSearchHorizonTurns ||
        report.collection_max_game_turns != 500 ||
        report.collection_exploration_rate != 0.05 ||
        report.control_terminal_weight != 0.50 ||
        report.treatment_terminal_weight != 0.75 ||
        report.fit_epochs != 3 ||
        report.fit_learning_rate != 0.006 ||
        report.parent_fingerprint !=
            old_school::kTerminalWeightC17ParentFingerprint) {
        throw std::runtime_error(
            "terminal-weight C17 artifact is not the canonical "
            "preregistered family");
    }
}

old_school::LearnedTerminalWeightC17Artifact
load_terminal_weight_c17_with_progress(
    std::size_t training_games,
    std::uint64_t training_seed) {
    const std::string path =
        old_school::learned_terminal_weight_c17_cache_path(
            training_games, training_seed);
    std::error_code exists_error;
    const bool exists =
        std::filesystem::exists(path, exists_error);
    if (exists_error) {
        throw std::runtime_error(
            "cannot inspect terminal-weight C17 artifact '" +
            path + "': " + exists_error.message());
    }
    if (!exists) {
        throw std::runtime_error(
            "evaluation-only route requires the existing "
            "terminal-weight C17 artifact '" + path +
            "'; create it with --train-terminal-weight-c17");
    }
    std::cout
        << "Loading pinned terminal-weight C17 artifact from "
        << path << "..." << std::flush;
    const auto started = std::chrono::steady_clock::now();
    auto artifact =
        old_school::load_learned_terminal_weight_c17_artifact(
            path, training_games, training_seed);
    require_canonical_terminal_weight_bundle(
        artifact.report());
    const std::chrono::duration<double> elapsed =
        std::chrono::steady_clock::now() - started;
    std::cout
        << " done (" << std::fixed << std::setprecision(2)
        << elapsed.count() << "s)\n"
        << "  TW50 fingerprint: "
        << artifact.report().control_fingerprint << '\n'
        << "  TW75 fingerprint: "
        << artifact.report().treatment_fingerprint << '\n';
    return artifact;
}

void train_terminal_weight_c17_with_progress(
    std::size_t training_games,
    std::uint64_t training_seed) {
    const std::string path =
        old_school::learned_terminal_weight_c17_cache_path(
            training_games, training_seed);
    std::error_code exists_error;
    const bool exists =
        std::filesystem::exists(path, exists_error);
    if (exists_error) {
        throw std::runtime_error(
            "cannot inspect terminal-weight C17 artifact '" +
            path + "': " + exists_error.message());
    }
    if (exists) {
        throw std::runtime_error(
            "terminal-weight C17 artifact already exists at '" +
            path + "'; refusing to retrain or overwrite the "
                   "one-shot family");
    }
    std::cout
        << "Training canonical same-shard TW50/TW75 C17 "
           "family (parent seed "
        << training_seed << ", raw shard seed "
        << old_school::kTerminalWeightC17ShardSeed
        << ")..." << std::flush;
    const auto started = std::chrono::steady_clock::now();
    old_school::LearnedTerminalWeightC17Config config;
    config.training_games = training_games;
    config.parent_training_seed = training_seed;
    auto artifact =
        old_school::train_learned_terminal_weight_c17_family(
            std::move(config));
    require_canonical_terminal_weight_bundle(
        artifact.report());
    old_school::
        write_learned_terminal_weight_c17_artifact_atomic(
            path, artifact);
    const auto reloaded =
        old_school::load_learned_terminal_weight_c17_artifact(
            path, training_games, training_seed);
    if (reloaded.report() != artifact.report()) {
        throw std::runtime_error(
            "terminal-weight C17 atomic roundtrip changed "
            "provenance");
    }
    const std::chrono::duration<double> elapsed =
        std::chrono::steady_clock::now() - started;
    const auto& report = reloaded.report();
    std::cout
        << " done (" << std::fixed << std::setprecision(2)
        << elapsed.count() << "s)\n"
        << "  Parent/TW50/TW75: "
        << report.parent_fingerprint << " / "
        << report.control_fingerprint << " / "
        << report.treatment_fingerprint << '\n'
        << "  Shared schedule/raw/features/outcomes: "
        << report.schedule_hash << " / "
        << report.raw_shard_hash << " / "
        << report.fit_feature_order_hash << " / "
        << report.outcome_hash << '\n'
        << "  Control/treatment targets: "
        << report.control_target_hash << " / "
        << report.treatment_target_hash << '\n'
        << "  Examples historical/shard/bootstrap/tail: "
        << report.historical_replay_examples << '/'
        << report.shard_examples << '/'
        << report.bootstrapped_examples << '/'
        << report.terminal_tail_examples << '\n'
        << "  Per deck games/examples/bootstrap/tail:\n";
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        const auto& counts = report.decks[deck];
        std::cout
            << "    "
            << old_school::deck_name(
                   static_cast<old_school::DeckId>(deck))
            << ": " << counts.games << '/'
            << counts.examples << '/'
            << counts.bootstrapped_examples << '/'
            << counts.terminal_tail_examples << '\n';
    }
    std::cout << "  Artifact: " << path << '\n';
}

void print_value_root_coverage(
    std::string_view cell,
    bool dense,
    const old_school::LearnedValueContextRootCoverage& root_coverage) {
    std::cout
        << "  " << cell
        << (dense ? " dense" : "")
        << " decision roots: total="
        << root_coverage.total_roots()
        << ", anchor=" << root_coverage.anchor_roots
        << ", self-play=" << root_coverage.self_play_roots
        << "\n  " << cell << " roots by deck:";
    for (std::size_t deck = 0; deck < old_school::kDeckCount;
         ++deck) {
        std::cout
            << ' '
            << old_school::deck_name(
                   static_cast<old_school::DeckId>(deck))
            << '=' << root_coverage.decision_player_decks[deck];
    }
    constexpr std::array<std::string_view, 7> phase_names = {
        "first-main",
        "begin-combat",
        "declare-attackers",
        "declare-blockers",
        "damage-order",
        "end-combat",
        "second-main",
    };
    std::cout << "\n  " << cell << " roots by phase:";
    for (std::size_t phase = 0; phase < phase_names.size();
         ++phase) {
        std::cout << ' ' << phase_names[phase] << '='
                  << root_coverage.phases[phase];
    }
    std::cout
        << "\n  " << cell << " roots by pass: 0="
        << root_coverage.pass_counts[0]
        << " 1=" << root_coverage.pass_counts[1]
        << "\n  " << cell << " roots by stack: empty="
        << root_coverage.stack_status[0]
        << " nonempty=" << root_coverage.stack_status[1] << '\n';
}

std::shared_ptr<const old_school::LearnedModel>
train_value_context_challenger_with_progress(
    std::size_t training_games,
    std::uint64_t training_seed,
    std::size_t generations,
    bool refresh_cache) {
    const std::string cache_path =
        old_school::learned_value_context_challenger_cache_path(
            training_games, training_seed, generations);
    std::error_code exists_error;
    const bool cache_exists =
        std::filesystem::exists(cache_path, exists_error);
    if (exists_error) {
        throw std::runtime_error(
            "cannot inspect Value Context C" +
            std::to_string(generations) + " artifact cache '" +
            cache_path + "': " + exists_error.message());
    }

    const auto started = std::chrono::steady_clock::now();
    std::shared_ptr<const old_school::LearnedModel> model;
    old_school::LearnedValueContextRootCoverage root_coverage;
    if (cache_exists && !refresh_cache) {
        std::cout
            << "Loading immutable Value Context C"
            << generations << " artifact (seed " << training_seed
            << ", " << training_games << " initial games) from "
            << cache_path << "..." << std::flush;
        try {
            const auto artifact =
                old_school::
                    load_learned_value_context_challenger_artifact(
                        cache_path, training_games,
                        training_seed, generations);
            model = artifact.model();
            root_coverage = artifact.root_coverage();
        } catch (const std::exception& error) {
            throw std::runtime_error(
                "Value Context C" +
                std::to_string(generations) +
                " artifact cache '" + cache_path +
                "' is invalid: " + error.what() +
                "; rerun this challenger route with "
                "--refresh-value-challenger-cache to regenerate it");
        }
    } else {
        std::cout << "Training frozen Value Context C"
                  << generations << " (seed " << training_seed
                  << ", " << training_games
                  << " initial games)..." << std::flush;
        const auto artifact =
            old_school::
                train_learned_value_context_challenger_artifact(
                    training_games, training_seed, generations);
        model = artifact.model();
        root_coverage = artifact.root_coverage();
        old_school::
            write_learned_value_context_challenger_artifact_atomic(
                cache_path, artifact);
    }
    const std::chrono::duration<double> elapsed =
        std::chrono::steady_clock::now() - started;
    std::cout << " done (" << std::fixed << std::setprecision(2)
              << elapsed.count() << "s)\n"
              << "  Value Context C" << generations
              << " fingerprint: "
              << old_school::learned_model_fingerprint(model) << '\n'
              << "  Value Context C" << generations
              << " artifact cache: "
              << (cache_exists && !refresh_cache
                      ? "loaded "
                      : "generated ")
              << cache_path << '\n';
    print_value_root_coverage("S1", false, root_coverage);
    return model;
}

std::shared_ptr<const old_school::LearnedModel>
train_value_dense_context_challenger_with_progress(
    std::size_t training_games,
    std::uint64_t training_seed,
    std::size_t generations,
    old_school::LearnedValueDenseContextTreatment treatment,
    bool refresh_cache) {
    const bool context_masked =
        treatment ==
        old_school::LearnedValueDenseContextTreatment::ContextMasked;
    const std::string_view cell = context_masked ? "D0" : "D1";
    const std::string_view name =
        context_masked ? "Value Dense Masked" : "Value Dense Context";
    const std::string cache_path =
        old_school::learned_value_dense_context_challenger_cache_path(
            training_games, training_seed, generations, treatment);
    std::error_code exists_error;
    const bool cache_exists =
        std::filesystem::exists(cache_path, exists_error);
    if (exists_error) {
        throw std::runtime_error(
            "cannot inspect " + std::string(name) + " C" +
            std::to_string(generations) + " artifact cache '" +
            cache_path + "': " + exists_error.message());
    }

    const auto started = std::chrono::steady_clock::now();
    std::shared_ptr<const old_school::LearnedModel> model;
    old_school::LearnedValueContextRootCoverage root_coverage;
    if (cache_exists && !refresh_cache) {
        std::cout
            << "Loading immutable " << name << " C"
            << generations << " artifact (seed " << training_seed
            << ", " << training_games << " initial games) from "
            << cache_path << "..." << std::flush;
        try {
            const auto artifact =
                old_school::
                    load_learned_value_dense_context_challenger_artifact(
                        cache_path, training_games, training_seed,
                        generations, treatment);
            model = artifact.model();
            root_coverage = artifact.root_coverage();
        } catch (const std::exception& error) {
            throw std::runtime_error(
                std::string(name) + " C" +
                std::to_string(generations) +
                " artifact cache '" + cache_path +
                "' is invalid: " + error.what() +
                "; rerun this challenger route with "
                "--refresh-value-challenger-cache to regenerate it");
        }
    } else {
        std::cout << "Training frozen " << name << " C"
                  << generations << " (seed " << training_seed
                  << ", " << training_games
                  << " initial games)..." << std::flush;
        const auto artifact =
            old_school::
                train_learned_value_dense_context_challenger_artifact(
                    training_games, training_seed, generations,
                    treatment);
        model = artifact.model();
        root_coverage = artifact.root_coverage();
        old_school::
            write_learned_value_dense_context_challenger_artifact_atomic(
                cache_path, artifact);
    }
    const std::chrono::duration<double> elapsed =
        std::chrono::steady_clock::now() - started;
    std::cout << " done (" << std::fixed << std::setprecision(2)
              << elapsed.count() << "s)\n"
              << "  " << name << " C" << generations
              << " fingerprint: "
              << old_school::learned_model_fingerprint(model) << '\n'
              << "  " << name << " C" << generations
              << " artifact cache: "
              << (cache_exists && !refresh_cache
                      ? "loaded "
                      : "generated ")
              << cache_path << '\n';
    print_value_root_coverage(cell, true, root_coverage);
    return model;
}

old_school::LearnedValueG8Result
train_value_g8_with_progress(std::size_t training_games,
                             std::uint64_t training_seed,
                             bool refresh_cache) {
    const std::string cache_path =
        old_school::learned_value_g8_cache_path(
            training_games, training_seed);
    std::error_code exists_error;
    const bool cache_exists =
        std::filesystem::exists(cache_path, exists_error);
    if (exists_error) {
        throw std::runtime_error(
            "cannot inspect Value G8 artifact cache '" +
            cache_path + "': " + exists_error.message());
    }

    old_school::LearnedValueG8Result result;
    const auto started = std::chrono::steady_clock::now();
    if (cache_exists && !refresh_cache) {
        std::cout << "Loading immutable Value G8 artifact (seed "
                  << training_seed << ", " << training_games
                  << " initial games) from " << cache_path
                  << "..." << std::flush;
        try {
            result = old_school::load_learned_value_g8_bundle(
                cache_path, training_games, training_seed);
        } catch (const std::exception& error) {
            throw std::runtime_error(
                "Value G8 artifact cache '" + cache_path +
                "' is invalid: " + error.what() +
                "; rerun this G8 route with "
                "--refresh-value-g8-cache to regenerate it");
        }
    } else {
        std::cout << "Training immutable Value G8 (seed "
                  << training_seed << ", " << training_games
                  << " initial games, 8 generations)..."
                  << std::flush;
        result = old_school::train_learned_value_g8(
            training_games, training_seed);
        old_school::write_learned_value_g8_bundle_atomic(
            cache_path, result);
    }
    const std::chrono::duration<double> elapsed =
        std::chrono::steady_clock::now() - started;
    std::cout << " done (" << std::fixed << std::setprecision(2)
              << elapsed.count() << "s)\n"
              << "  Value G8 artifact cache: "
              << (cache_exists && !refresh_cache
                      ? "loaded "
                      : "generated ")
              << cache_path << '\n';
    const auto& report = result.report;
    std::cout << "  G8 base: " << report.base_examples
              << " anchor examples; fingerprint "
              << report.base_fingerprint << '\n';
    for (const auto& generation : report.generations) {
        std::cout
            << "  G8 generation " << generation.generation << ": "
            << generation.self_play_games << " games, "
            << generation.generation_examples << " new + "
            << generation.anchor_examples << " anchor, replay "
            << generation.replay_generations << " shards/"
            << generation.replay_examples << " examples, "
            << (generation.search_enabled ? "search K=" : "raw Value");
        if (generation.search_enabled) {
            std::cout << generation.search_worlds << "/H="
                      << generation.search_horizon_turns;
        }
        std::cout << ", " << generation.rollout_evaluations
                  << " rollout evaluations, exploration "
                  << format_real(generation.exploration_rate)
                  << ", fingerprints "
                  << generation.parent_fingerprint << " -> "
                  << generation.candidate_fingerprint << '\n';
    }
    std::cout << "  Value G8 final fingerprint: "
              << report.final_fingerprint << '\n';
    return result;
}

old_school::LearnedValueG8Result
train_value_g8_mix50_with_progress(std::size_t training_games,
                                   std::uint64_t training_seed,
                                   bool refresh_cache) {
    const std::string cache_path =
        old_school::learned_value_g8_mix50_cache_path(
            training_games, training_seed);
    std::error_code exists_error;
    const bool cache_exists =
        std::filesystem::exists(cache_path, exists_error);
    if (exists_error) {
        throw std::runtime_error(
            "cannot inspect Value G8 Late-Mix50 artifact cache '" +
            cache_path + "': " + exists_error.message());
    }

    old_school::LearnedValueG8Result result;
    const auto started = std::chrono::steady_clock::now();
    if (cache_exists && !refresh_cache) {
        std::cout
            << "Loading immutable Value G8 Late-Mix50 artifact (seed "
            << training_seed << ", " << training_games
            << " initial games) from " << cache_path
            << "..." << std::flush;
        try {
            result = old_school::load_learned_value_g8_mix50_bundle(
                cache_path, training_games, training_seed);
        } catch (const std::exception& error) {
            throw std::runtime_error(
                "Value G8 Late-Mix50 artifact cache '" +
                cache_path + "' is invalid: " + error.what() +
                "; rerun this Mix50 route with "
                "--refresh-value-mix50-cache to regenerate it");
        }
    } else {
        std::cout
            << "Training immutable Value G8 Late-Mix50 (seed "
            << training_seed << ", " << training_games
            << " initial games, 8 generations)..."
            << std::flush;
        result = old_school::train_learned_value_g8_mix50(
            training_games, training_seed);
        old_school::write_learned_value_g8_mix50_bundle_atomic(
            cache_path, result);
    }
    if (result.report.recipe !=
        old_school::LearnedValueG8Recipe::LateMix50) {
        throw std::runtime_error(
            "Value G8 Late-Mix50 route returned a canonical recipe");
    }
    const std::chrono::duration<double> elapsed =
        std::chrono::steady_clock::now() - started;
    std::cout
        << " done (" << std::fixed << std::setprecision(2)
        << elapsed.count() << "s)\n"
        << "  Value G8 Late-Mix50 artifact cache: "
        << (cache_exists && !refresh_cache
                ? "loaded "
                : "generated ")
        << cache_path << '\n';
    const auto& report = result.report;
    std::cout << "  Mix50 G8 base: " << report.base_examples
              << " anchor examples; fingerprint "
              << report.base_fingerprint << '\n';
    for (const auto& generation : report.generations) {
        std::cout
            << "  Mix50 G8 generation "
            << generation.generation << ": "
            << generation.self_play_games << " games, "
            << generation.generation_examples << " new + "
            << generation.anchor_examples << " anchor, replay "
            << generation.replay_generations << " shards/"
            << generation.replay_examples << " examples, collection "
            << generation.raw_collection_games << " raw games/"
            << generation.raw_collection_examples
            << " raw examples + "
            << generation.search_collection_games
            << " search games/"
            << generation.search_collection_examples
            << " search examples, "
            << (generation.search_enabled
                    ? "search present K="
                    : "raw Value only");
        if (generation.search_enabled) {
            std::cout << generation.search_worlds << "/H="
                      << generation.search_horizon_turns;
        }
        std::cout << ", " << generation.rollout_evaluations
                  << " rollout evaluations, exploration "
                  << format_real(generation.exploration_rate)
                  << ", fingerprints "
                  << generation.parent_fingerprint << " -> "
                  << generation.candidate_fingerprint << '\n';
    }
    std::cout << "  Value G8 Late-Mix50 final fingerprint: "
              << report.final_fingerprint << '\n';
    return result;
}

std::shared_ptr<const old_school::LearnedModel>
train_actor_g0_with_progress(std::size_t training_games,
                             std::uint64_t training_seed) {
    std::cout << "Training frozen Actor G0 (seed " << training_seed
              << ", " << training_games << " games)..."
              << std::flush;
    const auto started = std::chrono::steady_clock::now();
    auto model = old_school::train_learned_actor_model(
        training_games, training_seed);
    const std::chrono::duration<double> elapsed =
        std::chrono::steady_clock::now() - started;
    std::cout << " done (" << std::fixed << std::setprecision(2)
              << elapsed.count() << "s)\n"
              << "  G0 fingerprint: "
              << old_school::learned_model_fingerprint(model) << '\n';
    return model;
}

old_school::LearnedActorGenerationResult
train_actor_g1_with_progress(
    std::shared_ptr<const old_school::LearnedModel> actor_g0,
    std::uint64_t training_seed,
    old_school::LearnedActorGenerationConfig config) {
    std::cout << "Training Actor G1 from frozen G0 "
                 "(24 balanced games, K="
              << config.search_worlds << "/H="
              << config.horizon_turns << ", cap="
              << config.max_roots_per_seat_kind
              << "/seat/kind, policy epochs="
              << config.policy_epochs << ", rate="
              << format_real(config.policy_learning_rate) << ")..."
              << std::flush;
    const auto started = std::chrono::steady_clock::now();
    auto result = old_school::train_learned_actor_generation(
        std::move(actor_g0), training_seed, config);
    const std::chrono::duration<double> elapsed =
        std::chrono::steady_clock::now() - started;
    const auto& report = result.report;
    const auto print_policy_fit =
        [](std::string_view label,
           const old_school::LearnedPolicyFitDiagnostics& fit) {
            std::cout
                << "  " << label << " fit: "
                << fit.example_count << " examples, weight "
                << std::setprecision(2) << fit.total_weight
                << ", teacher top-1 "
                << 100.0 *
                       fit.parent_expected_top_one_agreement
                << "% -> "
                << 100.0 *
                       fit.candidate_expected_top_one_agreement
                << "%, entropy "
                << std::setprecision(6)
                << fit.weighted_teacher_entropy
                << ", cross-entropy "
                << std::setprecision(6)
                << fit.parent_weighted_cross_entropy << " -> "
                << fit.candidate_weighted_cross_entropy
                << " (excess "
                << fit.parent_excess_cross_entropy << " -> "
                << fit.candidate_excess_cross_entropy << ')'
                << ", changed argmax "
                << fit.changed_argmax_examples << " (weight "
                << fit.changed_argmax_weight << ", "
                << 100.0 *
                       fit.changed_argmax_weight_fraction
                << "%)\n";
        };
    std::cout << " done (" << std::fixed << std::setprecision(2)
              << elapsed.count() << "s)\n"
              << "  Search roots: Priority " << report.priority_roots
              << ", Attack " << report.attack_roots
              << "; evaluations: Priority "
              << report.priority_rollout_evaluations << ", Attack "
              << report.attack_rollout_evaluations << '\n'
              << "  Training examples: critic "
              << report.critic_examples << ", Priority "
              << report.priority_policy_examples << ", Attack "
              << report.attack_policy_examples << "; replay generations "
              << report.replay_generations << '\n';
    print_policy_fit("Priority", report.fit.priority);
    print_policy_fit("Attack", report.fit.attack);
    std::cout
        << "  Critic TD fit: " << report.fit.critic.example_count
        << " examples, target mean/variance "
        << std::setprecision(6)
        << report.fit.critic.target_mean << '/'
        << report.fit.critic.target_variance
        << ", MSE "
        << report.fit.critic.parent_mean_squared_error << " -> "
        << report.fit.critic.candidate_mean_squared_error
        << ", BCE "
        << report.fit.critic.parent_binary_cross_entropy << " -> "
        << report.fit.critic.candidate_binary_cross_entropy << '\n'
        << "  G1 fingerprint: "
        << report.candidate_fingerprint << '\n';
    return result;
}

std::string_view priority_pass_result_name(
    old_school::PriorityPassResult result) {
    switch (result) {
    case old_school::PriorityPassResult::Passed:
        return "Passed";
    case old_school::PriorityPassResult::StackObjectResolved:
        return "StackObjectResolved";
    case old_school::PriorityPassResult::WindowEnded:
        return "WindowEnded";
    }
    throw std::logic_error("unknown priority pass result");
}

void print_value_context_alias_diagnostic(
    const old_school::ValueContextAliasDiagnostic& result) {
    const auto yes_no = [](bool value) {
        return value ? "yes" : "no";
    };
    std::cout
        << "Value Context Alias Audit\n"
        << "Structural diagnostic only; no model training or policy "
           "changes.\n\n"
        << "Shared lethal-stack root\n"
        << "  Root player / perspective: "
        << result.root_player << " / " << result.perspective << '\n'
        << "  Legal actions in each pass context: "
        << result.stack_action_count << '\n'
        << "  Complete legal action sets identical: "
        << yes_no(result.stack_actions_identical) << '\n'
        << "  Critic state features bit-identical: "
        << yes_no(
               result.stack_critic_features_bit_identical)
        << '\n'
        << "  Neutral policy/action features differ: "
        << yes_no(result.stack_policy_features_different)
        << '\n'
        << "  Pass with prior count 0: "
        << priority_pass_result_name(result.zero_pass_result)
        << "; next player " << result.zero_pass_next_player
        << "; pass count " << result.zero_pass_next_count
        << "; stack " << result.zero_pass_stack_size
        << "; root life " << result.zero_pass_life << '\n'
        << "  Pass with prior count 1: "
        << priority_pass_result_name(result.one_pass_result)
        << "; next player " << result.one_pass_next_player
        << "; pass count " << result.one_pass_next_count
        << "; stack " << result.one_pass_stack_size
        << "; root life " << result.one_pass_life
        << " (lethal)\n\n"
        << "Shared empty-stack main-phase root\n"
        << "  Legal actions in each phase: "
        << result.main_action_count << '\n'
        << "  FirstMain / SecondMain action sets identical: "
        << yes_no(result.main_actions_identical) << '\n'
        << "  Critic state features bit-identical: "
        << yes_no(
               result.main_critic_features_bit_identical)
        << '\n'
        << "  Neutral policy/action features differ: "
        << yes_no(result.main_policy_features_different)
        << '\n'
        << "  Opponent hidden-card substitution bit-identical: "
        << yes_no(result.hidden_information_bit_identical)
        << "\n\nResult: context alias "
        << (result.demonstrated() ? "demonstrated" : "not demonstrated")
        << '\n';
}

std::string white_plan_action_name(
    const old_school::PriorityAction& action) {
    switch (action.kind) {
    case old_school::PriorityActionKind::Pass:
        return "Pass";
    case old_school::PriorityActionKind::PlayLand:
        return "Play " +
               std::string(old_school::card_definition(action.card).name);
    case old_school::PriorityActionKind::CastCreature:
    case old_school::PriorityActionKind::CastSorcery:
    case old_school::PriorityActionKind::CastArtifact:
    case old_school::PriorityActionKind::CastEnchantment:
        return "Cast " +
               std::string(old_school::card_definition(action.card).name);
    case old_school::PriorityActionKind::CastLightningBolt:
        return "Cast Lightning Bolt";
    case old_school::PriorityActionKind::CastDisintegrate:
        return "Cast Disintegrate for X=" +
               std::to_string(action.x_value);
    case old_school::PriorityActionKind::CastGiantGrowth:
        return "Cast Giant Growth";
    case old_school::PriorityActionKind::CastCounterspell:
        return "Cast Counterspell";
    case old_school::PriorityActionKind::CastAncestralRecall:
        return "Cast Ancestral Recall";
    case old_school::PriorityActionKind::CastBraingeyser:
        return "Cast Braingeyser for X=" +
               std::to_string(action.x_value);
    case old_school::PriorityActionKind::CastForceSpike:
        return "Cast Force Spike";
    case old_school::PriorityActionKind::ActivateMillstone:
        if (action.target.has_value() &&
            action.target->player == 1) {
            return "Activate Millstone targeting opponent";
        }
        return "Activate Millstone targeting self";
    }
    throw std::logic_error("unknown diagnostic priority action");
}

void print_white_plan_diagnostic(
    const old_school::WhitePlanTeacherDiagnostic& result,
    std::uint64_t evaluation_seed, std::uint64_t training_seed,
    std::size_t training_games) {
    const auto annotated_name =
        [&](std::size_t index) {
            std::string name =
                white_plan_action_name(result.actions.at(index).action);
            if (result.opponent_millstone_action == index) {
                name += " [opponent-mill plan]";
            }
            if (result.redundant_moat_action == index) {
                name += " [redundant-Moat plan]";
            }
            return name;
        };

    std::cout << std::fixed << std::setprecision(6)
              << "White Lock-Plan Teacher Diagnostic\n"
              << "Evaluation seed: " << evaluation_seed << '\n'
              << "Training seed: " << training_seed << '\n'
              << "Model: "
              << old_school::learned_variant_name(
                     old_school::LearnedVariant::UnifiedActor)
              << '\n'
              << "Learned training games: " << training_games << '\n'
              << "Fixture: White first main, four untapped Plains, "
                 "land played, one Moat, one untapped Millstone, "
                 "redundant Moat in hand\n"
              << "Reference worlds: " << result.reference_worlds
              << "\nTwo-world trials: " << result.two_world_trials
              << "\n\nReference scores\n";
    for (std::size_t index = 0; index < result.actions.size();
         ++index) {
        const auto& action = result.actions[index];
        const double first_place_rate =
            result.two_world_trials == 0
                ? 0.0
                : 100.0 *
                      static_cast<double>(
                          action.two_world_first_place_count) /
                      static_cast<double>(
                          result.two_world_trials);
        std::cout << "  " << annotated_name(index) << ": "
                  << action.reference_score << "; K=2 first "
                  << action.two_world_first_place_count << '/'
                  << result.two_world_trials << " ("
                  << std::setprecision(1) << first_place_rate
                  << "%)\n"
                  << std::setprecision(6);
    }

    std::cout << "\nReference best: "
              << annotated_name(result.reference_best_action)
              << "\nK=2 reference agreement: "
              << result.two_world_reference_agreements << '/'
              << result.two_world_trials << " ("
              << std::setprecision(1)
              << result.two_world_reference_agreement_rate()
              << "%)\n";

    const std::size_t mill =
        result.opponent_millstone_action.value();
    const std::size_t moat = result.redundant_moat_action.value();
    const double mill_score =
        result.actions[mill].reference_score;
    const double moat_score =
        result.actions[moat].reference_score;
    std::cout << std::setprecision(6)
              << "\nPlan comparison\n"
              << "  Opponent Millstone activation: "
              << mill_score << '\n'
              << "  Redundant Moat cast: " << moat_score << '\n'
              << "  K=64 preference: ";
    if (mill_score > moat_score) {
        std::cout << "opponent Millstone activation\n";
    } else if (moat_score > mill_score) {
        std::cout << "redundant Moat\n";
    } else {
        std::cout << "tie\n";
    }
    std::cout << "  K=2 plan rankings: Millstone "
              << result.two_world_millstone_preferences
              << ", Moat " << result.two_world_moat_preferences
              << ", ties " << result.two_world_plan_ties << '\n'
              << "  K=2 plan-order agreement with K=64: "
              << result.two_world_plan_order_agreements << '/'
              << result.two_world_trials << " ("
              << std::setprecision(1)
              << result.two_world_plan_order_agreement_rate()
              << "%)\n"
              << std::setprecision(6);
    std::cout << "  K=2 reference-best agreement below 80%: "
              << (result.two_world_reference_agreement_rate() < 80.0
                      ? "yes"
                      : "no")
              << '\n';
}

void print_benchmark(const old_school::BotBenchmarkSummary& result,
                     std::uint64_t evaluation_seed,
                     std::string challenger_name = {},
                     std::string baseline_name = {}) {
    if (challenger_name.empty()) {
        challenger_name =
            old_school::bot_config_name(result.challenger);
    }
    if (baseline_name.empty()) {
        baseline_name =
            old_school::bot_config_name(result.baseline);
    }
    std::cout << std::fixed << std::setprecision(1)
              << "Old School Magic Bot Benchmark\n"
              << "Evaluation seed: " << evaluation_seed << '\n'
              << "Training seed: "
              << result.learned_training_seed << '\n'
              << "Challenger: "
              << challenger_name << '\n'
              << "Baseline: "
              << baseline_name
              << '\n';
    if (result.challenger.kind == old_school::BotKind::Learned) {
        std::cout << "Challenger frozen model: "
                  << challenger_name
                  << ", seed " << result.learned_training_seed
                  << ", " << result.challenger.training_games
                  << " training games, K="
                  << result.challenger.rollouts_per_action << '\n';
    }
    if (result.baseline.kind == old_school::BotKind::Learned) {
        std::cout << "Baseline frozen model: "
                  << baseline_name
                  << ", seed " << result.learned_training_seed
                  << ", " << result.baseline.training_games
                  << " training games, K="
                  << result.baseline.rollouts_per_action << '\n';
    }
    std::cout
              << "Repetitions per unordered deck pairing: "
              << result.repetitions_per_deck_pairing << '\n'
              << "Total paired games: " << result.total_games
              << "\n\nOverall\n"
              << "  Challenger record: "
              << result.challenger_stats.wins << '-'
              << result.challenger_stats.losses << '-'
              << result.challenger_stats.draws << " ("
              << result.challenger_win_rate() << "% wins)\n"
              << "  Approximate 95% confidence interval: "
              << result.confidence_low_95() << "% to "
              << result.confidence_high_95() << "%\n"
              << "  Verdict: ";
    if (result.challenger_is_better_95()) {
        std::cout
            << "PASS — aggregate lower bound exceeds 50% and "
               "challenger wins on all five decks\n";
    } else if (result.confidence_low_95() > 50.0) {
        std::cout
            << "FAIL — aggregate confidence passes, but challenger "
               "does not win on every deck\n";
    } else if (result.confidence_high_95() < 50.0) {
        std::cout << "FAIL — baseline is better at 95% confidence\n";
    } else {
        std::cout << "INCONCLUSIVE — run more repetitions\n";
    }

    std::cout << "\nEfficiency\n"
              << "  Challenger: "
              << result.challenger_stats.average_decisions()
              << " decisions/game, "
              << result.challenger_stats.average_rollouts()
              << " rollouts/game\n"
              << "  Baseline: "
              << result.baseline_stats.average_decisions()
              << " decisions/game, "
              << result.baseline_stats.average_rollouts()
              << " rollouts/game\n"
              << "\nBy challenger deck\n";
    for (std::size_t deck = 0;
         deck < result.challenger_decks.size(); ++deck) {
        const auto id = static_cast<old_school::DeckId>(deck);
        const auto& challenger = result.challenger_decks[deck];
        const auto& baseline = result.baseline_decks[deck];
        std::cout << "  " << old_school::deck_name(id) << ": challenger "
                  << challenger.win_rate() << "% (" << challenger.wins
                  << '-' << challenger.losses << '-' << challenger.draws
                  << "), baseline " << baseline.win_rate() << "% ("
                  << baseline.wins << '-' << baseline.losses << '-'
                  << baseline.draws << ")\n"
                  << "    Average spells/damage/milled/life: challenger "
                  << challenger.average_spells_cast() << '/'
                  << challenger.average_damage_to_opponent() << '/'
                  << challenger.average_cards_milled() << '/'
                  << challenger.average_ending_life() << ", baseline "
                  << baseline.average_spells_cast() << '/'
                  << baseline.average_damage_to_opponent() << '/'
                  << baseline.average_cards_milled() << '/'
                  << baseline.average_ending_life() << '\n';
    }
    std::cout
        << "\nExact challenger-deck x baseline-deck matrix "
           "(challenger perspective)\n";
    for (std::size_t challenger_deck = 0;
         challenger_deck <
         result.challenger_deck_matchups.size();
         ++challenger_deck) {
        for (std::size_t baseline_deck = 0;
             baseline_deck <
             result.challenger_deck_matchups[challenger_deck].size();
             ++baseline_deck) {
            const auto challenger_id =
                static_cast<old_school::DeckId>(challenger_deck);
            const auto baseline_id =
                static_cast<old_school::DeckId>(baseline_deck);
            const auto& cell =
                result.challenger_deck_matchups[challenger_deck]
                                                [baseline_deck];
            std::cout
                << "  " << old_school::deck_name(challenger_id)
                << " vs " << old_school::deck_name(baseline_id)
                << ": " << cell.wins << '-' << cell.losses << '-'
                << cell.draws << " (" << cell.games << " games)\n";
        }
    }
}

bool run_stability_panel(std::size_t runs,
                         std::size_t repetitions_per_deck_pairing,
                         std::uint64_t base_seed,
                         std::uint64_t training_seed,
                         std::size_t rollouts,
                         std::size_t deep_rollouts,
                         std::size_t training_games,
                         std::size_t learned_rollouts,
                         std::size_t learned_generations,
                         bool refresh_challenger_cache,
                         double value_continuation_epsilon) {
    constexpr std::array<old_school::BotKind, 4> baseline_kinds = {
        old_school::BotKind::Random,
        old_school::BotKind::MonteCarlo,
        old_school::BotKind::DeepMonteCarlo,
        old_school::BotKind::Handcrafted,
    };
    constexpr std::size_t kHandcraftedBaselineIndex = 3;
    old_school::BotConfig learned_config =
        bot_config(old_school::BotKind::Learned, rollouts,
                   deep_rollouts, training_games,
                   learned_rollouts,
                   value_continuation_epsilon);
    std::array<old_school::BotBenchmarkSummary, baseline_kinds.size()>
        pooled;
    std::array<std::size_t, baseline_kinds.size()> seed_wins{};
    for (std::size_t baseline = 0; baseline < pooled.size();
         ++baseline) {
        pooled[baseline].challenger = learned_config;
        pooled[baseline].baseline =
            bot_config(baseline_kinds[baseline], rollouts,
                       deep_rollouts, training_games);
        pooled[baseline].learned_training_seed =
            training_seed;
    }
    const auto merge_bot = [](old_school::BotSimulationStats& destination,
                              const old_school::BotSimulationStats& source) {
        destination.games += source.games;
        destination.wins += source.wins;
        destination.losses += source.losses;
        destination.draws += source.draws;
        destination.total_decisions += source.total_decisions;
        destination.total_rollouts += source.total_rollouts;
    };
    const auto merge_deck = [](old_school::DeckSimulationStats& destination,
                               const old_school::DeckSimulationStats& source) {
        destination.games += source.games;
        destination.wins += source.wins;
        destination.losses += source.losses;
        destination.draws += source.draws;
        destination.on_play_games += source.on_play_games;
        destination.on_play_wins += source.on_play_wins;
        destination.on_draw_games += source.on_draw_games;
        destination.on_draw_wins += source.on_draw_wins;
    };
    constexpr std::size_t mixed_policy_matrix_games =
        old_school::kBotKindCount * old_school::kBotKindCount;
    const std::size_t mixed_target_games =
        20 * repetitions_per_deck_pairing;
    const std::size_t mixed_games_per_matchup =
        ((mixed_target_games + mixed_policy_matrix_games - 1) /
         mixed_policy_matrix_games) *
        mixed_policy_matrix_games;
    old_school::TournamentConfig mixed_config;
    mixed_config.bot_field = old_school::BotField::Mixed;
    mixed_config.monte_carlo_rollouts = rollouts;
    mixed_config.deep_monte_carlo_rollouts = deep_rollouts;
    mixed_config.learned_rollouts = learned_rollouts;
    mixed_config.value_continuation_epsilon =
        value_continuation_epsilon;
    mixed_config.learned_training_games = training_games;
    mixed_config.learned_variant =
        old_school::LearnedVariant::ValueSearchChampion;
    old_school::TournamentSummary pooled_mixed;
    std::size_t mixed_seed_lift_passes = 0;
    std::size_t all_policy_seed_wins = 0;

    std::cout << std::fixed << std::setprecision(1)
              << (learned_generations == 0
                      ? "Learned Value G0 All-Policy Stability Panel\n"
                      : "Learned Value Challenger All-Policy "
                        "Stability Panel\n")
              << "Runs: " << runs << '\n'
              << "Evaluation base seed: " << base_seed << '\n'
              << "Training seed: " << training_seed << '\n'
              << "Repetitions per unordered deck pairing per run: "
              << repetitions_per_deck_pairing
              << '\n'
              << "Mixed-field games per deck pairing per run: "
              << mixed_games_per_matchup << '\n'
              << "Training games for fixed model: "
              << training_games << '\n'
              << "Learned model: "
              << (learned_generations == 0
                      ? "Legacy G0"
                      : "Challenger C" +
                            std::to_string(learned_generations))
              << "\nLearned search worlds per legal action: "
              << learned_rollouts;
    if (value_continuation_epsilon != 0.0) {
        std::cout << "\nValue continuation priority-action epsilon: "
                  << format_real(value_continuation_epsilon);
    }
    std::cout
              << "\nTraining fixed model..." << std::flush;

    old_school::GameConfig shared_config;
    shared_config.learned_training_seed = training_seed;
    shared_config.learned_model =
        train_frozen_learned_model(
            old_school::LearnedVariant::ValueSearchChampion,
            training_games, training_seed,
            learned_generations,
            refresh_challenger_cache);
    learned_config.learned_model = shared_config.learned_model;
    for (auto& result : pooled) {
        result.challenger.learned_model =
            shared_config.learned_model;
    }
    std::cout << " done\n\n";

    for (std::size_t run = 0; run < runs; ++run) {
        const std::uint64_t evaluation_seed =
            base_seed + 101ULL * static_cast<std::uint64_t>(run + 1);
        bool seed_pass = true;
        std::cout << "  Evaluation seed " << evaluation_seed
                  << ":\n";
        for (std::size_t baseline = 0;
             baseline < baseline_kinds.size(); ++baseline) {
            const auto result = old_school::run_bot_benchmark(
                repetitions_per_deck_pairing, evaluation_seed,
                learned_config, pooled[baseline].baseline,
                shared_config);
            const bool learned_won =
                result.challenger_stats.wins >
                result.baseline_stats.wins;
            seed_wins[baseline] += learned_won ? 1 : 0;
            seed_pass = seed_pass && learned_won;
            pooled[baseline].total_games += result.total_games;
            merge_bot(pooled[baseline].challenger_stats,
                      result.challenger_stats);
            merge_bot(pooled[baseline].baseline_stats,
                      result.baseline_stats);
            for (std::size_t deck = 0;
                 deck < result.challenger_decks.size(); ++deck) {
                merge_deck(
                    pooled[baseline].challenger_decks[deck],
                    result.challenger_decks[deck]);
                merge_deck(pooled[baseline].baseline_decks[deck],
                           result.baseline_decks[deck]);
            }
            for (std::size_t challenger_deck = 0;
                 challenger_deck <
                 result.challenger_deck_matchups.size();
                 ++challenger_deck) {
                for (std::size_t baseline_deck = 0;
                     baseline_deck <
                     result
                         .challenger_deck_matchups[challenger_deck]
                         .size();
                     ++baseline_deck) {
                    merge_deck(
                        pooled[baseline]
                            .challenger_deck_matchups[challenger_deck]
                                                     [baseline_deck],
                        result
                            .challenger_deck_matchups[challenger_deck]
                                                     [baseline_deck]);
                }
            }
            std::cout << "    vs "
                      << old_school::bot_name(baseline_kinds[baseline])
                      << ": " << result.challenger_stats.wins << '-'
                      << result.baseline_stats.wins << '-'
                      << result.challenger_stats.draws << " ("
                      << result.challenger_win_rate() << "%)"
                      << (learned_won ? " PASS\n" : " FAIL\n");
        }
        const auto mixed = old_school::run_tournament(
            mixed_games_per_matchup, evaluation_seed,
            shared_config,
            mixed_config);
        const auto seed_lift =
            old_school::compare_learned_deck_lifts(mixed);
        const bool seed_lift_pass =
            seed_lift.learned_is_best_on_every_deck();
        mixed_seed_lift_passes += seed_lift_pass ? 1 : 0;
        std::cout << "    mixed-field lift:";
        for (const auto& deck : seed_lift.decks) {
            std::cout << ' ' << old_school::deck_name(deck.deck) << '='
                      << (deck.available
                              ? (deck.learned_is_best ? "PASS"
                                                     : "FAIL")
                              : "N/A");
        }
        std::cout << " => "
                  << (seed_lift_pass ? "PASS\n" : "FAIL\n");
        for (std::size_t deck = 0;
             deck < mixed.deck_bots.size(); ++deck) {
            for (std::size_t bot = 0;
                 bot < mixed.deck_bots[deck].size(); ++bot) {
                merge_deck(pooled_mixed.deck_bots[deck][bot],
                           mixed.deck_bots[deck][bot]);
            }
        }
        all_policy_seed_wins += seed_pass ? 1 : 0;
    }

    bool every_policy_passed = true;
    std::cout << "\nPooled results\n";
    for (std::size_t baseline = 0;
         baseline < baseline_kinds.size(); ++baseline) {
        const auto& result = pooled[baseline];
        const bool confidence_pass =
            result.challenger_is_better_95();
        bool every_deck_won = true;
        std::cout << "  vs "
                  << old_school::bot_name(baseline_kinds[baseline])
                  << ": " << result.challenger_stats.wins << '-'
                  << result.baseline_stats.wins << '-'
                  << result.challenger_stats.draws << " ("
                  << result.challenger_win_rate()
                  << "%, 95% interval "
                  << result.confidence_low_95() << "% to "
                  << result.confidence_high_95() << "%)\n";
        for (std::size_t deck = 0;
             deck < result.challenger_decks.size(); ++deck) {
            const auto id = static_cast<old_school::DeckId>(deck);
            const auto& learned =
                result.challenger_decks[deck];
            const auto& other = result.baseline_decks[deck];
            const bool deck_won = learned.wins > other.wins;
            every_deck_won = every_deck_won && deck_won;
            std::cout << "    " << old_school::deck_name(id) << ": "
                      << learned.wins << " vs " << other.wins
                      << (deck_won ? " PASS\n" : " FAIL\n");
        }
        const bool policy_pass =
            seed_wins[baseline] == runs && confidence_pass &&
            every_deck_won;
        every_policy_passed =
            every_policy_passed && policy_pass;
        std::cout << "    Seeds " << seed_wins[baseline] << '/'
                  << runs << ", confidence "
                  << (confidence_pass ? "PASS" : "FAIL")
                  << ", decks "
                  << (every_deck_won ? "PASS" : "FAIL")
                  << " => " << (policy_pass ? "PASS" : "FAIL")
                  << '\n';
    }
    const auto& pooled_handcrafted =
        pooled[kHandcraftedBaselineIndex];
    std::cout
        << "\nPooled Handcrafted exact challenger-deck x "
           "baseline-deck matrix (challenger perspective)\n";
    for (std::size_t challenger_deck = 0;
         challenger_deck <
         pooled_handcrafted.challenger_deck_matchups.size();
         ++challenger_deck) {
        for (std::size_t baseline_deck = 0;
             baseline_deck <
             pooled_handcrafted
                 .challenger_deck_matchups[challenger_deck]
                 .size();
             ++baseline_deck) {
            const auto challenger_id =
                static_cast<old_school::DeckId>(challenger_deck);
            const auto baseline_id =
                static_cast<old_school::DeckId>(baseline_deck);
            const auto& cell =
                pooled_handcrafted
                    .challenger_deck_matchups[challenger_deck]
                                             [baseline_deck];
            std::cout
                << "  " << old_school::deck_name(challenger_id)
                << " vs " << old_school::deck_name(baseline_id)
                << ": " << cell.wins << '-' << cell.losses << '-'
                << cell.draws << " (" << cell.games << " games)\n";
        }
    }
    constexpr std::array<old_school::BotKind, 5>
        exact_mixed_policy_order = {
            old_school::BotKind::Random,
            old_school::BotKind::MonteCarlo,
            old_school::BotKind::DeepMonteCarlo,
            old_school::BotKind::Handcrafted,
            old_school::BotKind::Learned,
        };
    std::cout
        << "\nPooled mixed-field exact deck-policy counts\n";
    for (std::size_t deck = 0;
         deck < pooled_mixed.deck_bots.size(); ++deck) {
        const auto deck_id =
            static_cast<old_school::DeckId>(deck);
        for (const old_school::BotKind policy :
             exact_mixed_policy_order) {
            const auto& stats =
                pooled_mixed
                    .deck_bots[deck]
                              [static_cast<std::size_t>(policy)];
            std::cout << "  " << old_school::deck_name(deck_id)
                      << " | " << old_school::bot_name(policy)
                      << ": " << stats.wins << '-'
                      << stats.losses << '-' << stats.draws
                      << " (" << stats.games << " games)\n";
        }
    }

    const auto pooled_lift =
        old_school::compare_learned_deck_lifts(pooled_mixed);
    const bool mixed_lift_pass =
        pooled_lift.learned_is_best_on_every_deck();
    std::cout << "\nPooled mixed-field lift over Random\n";
    for (const auto& deck : pooled_lift.decks) {
        std::cout << "  " << old_school::deck_name(deck.deck) << ": ";
        if (!deck.available) {
            std::cout << "N/A FAIL\n";
            continue;
        }
        std::cout << "Learned ";
        print_delta(deck.learned_lift);
        std::cout << ", best other "
                  << old_school::bot_name(deck.best_other) << ' ';
        print_delta(deck.best_other_lift);
        std::cout << ' '
                  << (deck.learned_is_best ? "PASS\n" : "FAIL\n");
    }
    std::cout << "  Mixed-field seeds: "
              << mixed_seed_lift_passes << '/' << runs
              << "\n  Per-deck pooled lift gate: "
              << (mixed_lift_pass ? "PASS" : "FAIL") << '\n';
    const bool passed =
        all_policy_seed_wins == runs && every_policy_passed &&
        mixed_lift_pass;
    std::cout << "\nAll-policy seed verdict: "
              << all_policy_seed_wins << '/' << runs
              << "\nOverall: " << (passed ? "PASS" : "FAIL")
              << '\n';
    return passed;
}

void run_variance_study(
    std::size_t repetitions_per_deck_pairing,
    std::size_t training_games, std::size_t rollouts,
    std::size_t deep_rollouts) {
    constexpr std::array<std::uint64_t, 3> seeds = {
        424242,
        101,
        707,
    };
    auto learned = bot_config(
        old_school::BotKind::Learned, rollouts,
        deep_rollouts, training_games);
    const auto handcrafted = bot_config(
        old_school::BotKind::Handcrafted, rollouts,
        deep_rollouts, training_games);
    std::array<std::array<double, seeds.size()>, seeds.size()>
        win_rates{};

    std::cout << std::fixed << std::setprecision(1)
              << "Learned Training/Evaluation Seed Variance Study\n"
              << "Challenger: "
              << old_school::bot_config_name(learned) << '\n'
              << "Baseline: "
              << old_school::bot_name(old_school::BotKind::Handcrafted)
              << '\n'
              << "Training seeds: 424242, 101, 707\n"
              << "Evaluation seeds: 424242, 101, 707\n"
              << "Training games per model: " << training_games
              << '\n'
              << "Paired repetitions per cell: "
              << repetitions_per_deck_pairing << "\n\n";

    for (std::size_t training = 0; training < seeds.size();
         ++training) {
        old_school::GameConfig shared_config;
        shared_config.learned_training_seed = seeds[training];
        std::cout << "Training seed " << seeds[training]
                  << "..." << std::flush;
        shared_config.learned_model =
            old_school::train_learned_value_champion(
                training_games, seeds[training]);
        learned.learned_model = shared_config.learned_model;
        std::cout << " done\n";
        for (std::size_t evaluation = 0;
             evaluation < seeds.size(); ++evaluation) {
            const auto result = old_school::run_bot_benchmark(
                repetitions_per_deck_pairing,
                seeds[evaluation], learned, handcrafted,
                shared_config);
            win_rates[training][evaluation] =
                result.challenger_win_rate();
            std::cout << "  Evaluation seed "
                      << seeds[evaluation] << ": "
                      << win_rates[training][evaluation]
                      << "%\n"
                      << std::flush;
        }
    }

    std::cout << "\nWin-rate matrix (%)\n"
              << "  train/eval";
    for (const auto evaluation_seed : seeds) {
        std::cout << std::setw(12) << evaluation_seed;
    }
    std::cout << '\n';
    std::array<double, seeds.size()> row_spans{};
    std::array<double, seeds.size()> column_spans{};
    for (std::size_t training = 0; training < seeds.size();
         ++training) {
        std::cout << "  " << std::setw(12) << seeds[training];
        for (const double rate : win_rates[training]) {
            std::cout << std::setw(12) << rate;
        }
        const auto [minimum, maximum] = std::minmax_element(
            win_rates[training].begin(),
            win_rates[training].end());
        row_spans[training] = *maximum - *minimum;
        std::cout << '\n';
    }

    std::cout << "\nRow ranges (evaluation variance at fixed model)\n";
    for (std::size_t training = 0; training < seeds.size();
         ++training) {
        const auto [minimum, maximum] = std::minmax_element(
            win_rates[training].begin(),
            win_rates[training].end());
        std::cout << "  Training seed " << seeds[training]
                  << ": " << *minimum << "% to " << *maximum
                  << "% (span " << row_spans[training]
                  << " pp)\n";
    }

    std::cout
        << "\nColumn ranges (training variance at fixed evaluation)\n";
    for (std::size_t evaluation = 0;
         evaluation < seeds.size(); ++evaluation) {
        std::array<double, seeds.size()> column{};
        for (std::size_t training = 0;
             training < seeds.size(); ++training) {
            column[training] =
                win_rates[training][evaluation];
        }
        const auto [minimum, maximum] =
            std::minmax_element(column.begin(), column.end());
        column_spans[evaluation] = *maximum - *minimum;
        std::cout << "  Evaluation seed " << seeds[evaluation]
                  << ": " << *minimum << "% to " << *maximum
                  << "% (span " << column_spans[evaluation]
                  << " pp)\n";
    }

    const auto mean =
        [](const std::array<double, 3>& values) {
            double total = 0.0;
            for (const double value : values) {
                total += value;
            }
            return total /
                   static_cast<double>(values.size());
        };
    const double mean_row_span = mean(row_spans);
    const double mean_column_span = mean(column_spans);
    std::cout << "\nMean row span: " << mean_row_span
              << " pp\nMean column span: "
              << mean_column_span
              << " pp\nTraining variance hypothesis: "
              << (mean_column_span > mean_row_span
                      ? "SUPPORTED"
                      : "NOT SUPPORTED")
              << '\n';
}

void print_evolution(const old_school::DeckEvolutionSummary& result,
                     std::uint64_t seed,
                     std::string_view pilot_name) {
    std::cout << std::fixed << std::setprecision(1)
              << "Old School Magic Deck Evolution\n"
              << "Seed: " << seed << '\n'
              << "Pilot: " << pilot_name
              << "\n\nGeneration best fitness\n";
    for (std::size_t generation = 0;
         generation < result.generation_best_win_rates.size();
         ++generation) {
        std::cout << "  " << generation + 1 << ": "
                  << result.generation_best_win_rates[generation]
                  << "%\n";
    }

    std::array<std::size_t, old_school::kCardCount> counts{};
    for (const old_school::CardId card : result.best.cards) {
        ++counts[static_cast<std::size_t>(card)];
    }
    std::cout << "\nBest 40-card deck\n";
    for (std::size_t card = 0; card < counts.size(); ++card) {
        if (counts[card] == 0) {
            continue;
        }
        const auto id = static_cast<old_school::CardId>(card);
        std::cout << "  " << counts[card] << " "
                  << old_school::card_definition(id).name << '\n';
    }
    std::cout << "\nFitness: " << result.best.total.win_rate()
              << "% (" << result.best.total.wins << '-'
              << result.best.total.losses << '-'
              << result.best.total.draws << ")\n"
              << "By metagame opponent\n";
    for (std::size_t opponent = 0;
         opponent < result.best.by_opponent.size(); ++opponent) {
        const auto id = static_cast<old_school::DeckId>(opponent);
        const auto& stats = result.best.by_opponent[opponent];
        std::cout << "  " << old_school::deck_name(id) << ": "
                  << stats.win_rate() << "% (" << stats.wins << '-'
                  << stats.losses << '-' << stats.draws << ")\n";
    }
}

constexpr std::string_view kCanonicalP0Fingerprint =
    "bda1ea4401388bac3f26cf773623bac8848482f68e73d45a968473105a6d8dbc";
constexpr std::string_view kDc1EnvironmentV3P0Fingerprint =
    "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f";
constexpr std::string_view kCanonicalActorG0Fingerprint =
    "7639176465b7b7c240e9d0d0067d352b0cac052a7083b47e6504073206068a84";
constexpr std::string_view kP1RExpectedFingerprint =
    "a17814d6cca71c95ab937d162e8fd183679b8e88c0fd175a7d1f750d4cd06a9b";

bool approximately_equal(double left, double right) {
    constexpr double kTolerance = 1.0e-9;
    return std::isfinite(left) && std::isfinite(right) &&
           std::abs(left - right) <= kTolerance;
}

void print_dc1_mining_split(
    std::string_view name,
    const old_school::probes::Dc1MiningSplitReport& split) {
    std::cout
        << name << ": seed=" << split.seed
        << ", games=" << split.games
        << ", seat-games=" << split.seat_games
        << ", priority-roots=" << split.raw_priority_roots
        << ", multi-roots=" << split.raw_multi_action_roots
        << ", retained-roots=" << split.retained_roots
        << ", pair-groups=" << split.pair_groups
        << ", paired-world-cells=" << split.paired_world_cells
        << ", settlements=" << split.settlement_operations
        << ", caps/sums="
        << (split.accounting_passed ? "PASS" : "FAIL")
        << ", hidden="
        << (split.hidden_repartition_passed ? "PASS" : "FAIL")
        << ", density="
        << (split.density_passed ? "PASS" : "FAIL") << '\n';
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        const auto& summary = split.decks[deck];
        std::cout
            << "  "
            << old_school::deck_name(
                   static_cast<old_school::DeckId>(deck))
            << ": seat-games=" << summary.seat_games
            << ", priority-roots=" << summary.raw_priority_roots
            << ", multi-roots="
            << summary.raw_multi_action_roots
            << ", retained-roots=" << summary.retained_roots
            << ", pair-groups=" << summary.pair_groups
            << ", paired-world-cells="
            << summary.paired_world_cells
            << ", settlements="
            << summary.settlement_operations
            << ", unique +=" << summary.unique_positive_pairs
            << ", unique incomparable="
            << summary.unique_incomparable_pairs
            << ", matched controls="
            << summary.unique_matched_incomparable_controls
            << ", conflicting keys="
            << summary.conflicting_pair_keys
            << ", seat coverage "
            << summary.positive_seat_games << '/'
            << summary.incomparable_control_seat_games
            << " ["
            << (summary.density_passed ? "PASS" : "FAIL")
            << "]\n";
    }
}

void print_dc1_mining_report(
    const old_school::probes::Dc1DominanceAuditReport& report) {
    std::cout
        << "DC1 Immediate Resource-Dominance Mining Audit\n"
        << "Evaluation-only; no fit, filter, model write, or "
           "deployment.\n"
        << "Environment: v3 cleanup-discard\n"
        << "Frozen parent: " << report.model_fingerprint << '\n'
        << "Canonical settlement: K=" << report.config.worlds
        << ", max_legal_actions="
        << report.config.max_legal_actions
        << ", max_turns=" << report.config.max_game_turns
        << ", max roots/seat-game="
        << report.config.max_roots_per_seat_game
        << ", max pairs/root="
        << report.config.max_pairs_per_root << '\n'
        << "Fixture gate: "
        << (report.fixture_gate_passed ? "PASS" : "FAIL")
        << " (X=0 positive; payable/live Spike and productive "
           "actions nontriggers)\n";
    print_dc1_mining_split("Training mining", report.training);
    print_dc1_mining_split("Held-out mining", report.heldout);
    std::cout
        << "Accounting: "
        << (report.accounting_passed ? "PASS" : "FAIL")
        << "\nVerdict: "
        << (report.gate_passed ? "PASS" : "REJECT")
        << (report.gate_passed
                ? " (licenses a separate preregistered treatment)"
                : " (density insufficient; no treatment)")
        << '\n';
}

std::string_view dc1_phase_name(old_school::TurnPhase phase) {
    switch (phase) {
    case old_school::TurnPhase::FirstMain:
        return "first-main";
    case old_school::TurnPhase::BeginCombat:
        return "begin-combat";
    case old_school::TurnPhase::DeclareAttackers:
        return "declare-attackers";
    case old_school::TurnPhase::DeclareBlockers:
        return "declare-blockers";
    case old_school::TurnPhase::DamageOrder:
        return "damage-order";
    case old_school::TurnPhase::EndCombat:
        return "end-combat";
    case old_school::TurnPhase::SecondMain:
        return "second-main";
    }
    throw std::logic_error("unknown DC1 census phase");
}

std::string_view dc1_action_kind_name(
    old_school::PriorityActionKind kind) {
    switch (kind) {
    case old_school::PriorityActionKind::Pass:
        return "pass";
    case old_school::PriorityActionKind::PlayLand:
        return "play-land";
    case old_school::PriorityActionKind::CastCreature:
        return "cast-creature";
    case old_school::PriorityActionKind::CastSorcery:
        return "cast-sorcery";
    case old_school::PriorityActionKind::CastArtifact:
        return "cast-artifact";
    case old_school::PriorityActionKind::CastEnchantment:
        return "cast-enchantment";
    case old_school::PriorityActionKind::CastLightningBolt:
        return "bolt";
    case old_school::PriorityActionKind::CastDisintegrate:
        return "disintegrate";
    case old_school::PriorityActionKind::CastGiantGrowth:
        return "giant-growth";
    case old_school::PriorityActionKind::CastCounterspell:
        return "counterspell";
    case old_school::PriorityActionKind::CastAncestralRecall:
        return "ancestral";
    case old_school::PriorityActionKind::CastBraingeyser:
        return "braingeyser";
    case old_school::PriorityActionKind::CastForceSpike:
        return "force-spike";
    case old_school::PriorityActionKind::ActivateMillstone:
        return "millstone";
    }
    throw std::logic_error("unknown DC1 census action kind");
}

void print_dc1_count_histogram(
    const std::vector<std::size_t>& histogram) {
    std::cout << '{';
    bool first = true;
    for (std::size_t count = 0;
         count < histogram.size(); ++count) {
        if (histogram[count] == 0) {
            continue;
        }
        std::cout << (first ? "" : ", ")
                  << count << ':' << histogram[count];
        first = false;
    }
    std::cout << '}';
}

void print_dc1_action_kind_histogram(
    const std::array<
        std::size_t,
        old_school::probes::kDc1PriorityActionKindCount>&
        histogram) {
    std::cout << '{';
    bool first = true;
    for (std::size_t kind = 0;
         kind < histogram.size(); ++kind) {
        if (histogram[kind] == 0) {
            continue;
        }
        std::cout
            << (first ? "" : ", ")
            << dc1_action_kind_name(
                   static_cast<
                       old_school::PriorityActionKind>(kind))
            << ':' << histogram[kind];
        first = false;
    }
    std::cout << '}';
}

std::string dc1_hex64(std::uint64_t value) {
    std::ostringstream output;
    output << std::hex << std::setfill('0')
           << std::setw(16) << value;
    return output.str();
}

void print_dc1_census_split(
    std::string_view name,
    const old_school::probes::Dc1ActionCensusSplitReport&
        split) {
    std::cout
        << name << ": seed=" << split.seed
        << ", games=" << split.games
        << ", seat-games=" << split.seat_games
        << ", priority-roots=" << split.priority_roots
        << ", over-64-roots=" << split.over_threshold_roots
        << ", max-actions=" << split.maximum_legal_actions
        << ", descriptors="
        << (split.descriptors_distinct ? "distinct" : "DUPLICATE")
        << ", accounting="
        << (split.accounting_passed ? "PASS" : "FAIL")
        << "\n  global legal-count histogram=";
    print_dc1_count_histogram(split.legal_action_histogram);
    std::cout << '\n';
    for (std::size_t deck_index = 0;
         deck_index < old_school::kDeckCount; ++deck_index) {
        const auto& deck = split.decks[deck_index];
        const auto deck_id =
            static_cast<old_school::DeckId>(deck_index);
        std::cout
            << "  " << old_school::deck_name(deck_id)
            << '(' << deck_index << ')'
            << ": seat-games=" << deck.seat_games
            << ", roots=" << deck.priority_roots
            << ", over-64=" << deck.over_threshold_roots
            << ", max=" << deck.maximum_legal_actions
            << ", histogram=";
        print_dc1_count_histogram(
            deck.legal_action_histogram);
        std::cout << '\n';
    }
    std::cout << "  over-64 contexts:";
    if (split.over_threshold_contexts.empty()) {
        std::cout << " none\n";
        return;
    }
    std::cout << '\n';
    for (const auto& context :
         split.over_threshold_contexts) {
        const auto root_id =
            static_cast<std::size_t>(context.root_deck);
        const auto opponent_id =
            static_cast<std::size_t>(
                context.opponent_deck);
        std::cout
            << "    split="
            << (context.training_split
                    ? "training"
                    : "heldout")
            << " block=" << context.block
            << " schedule=" << context.schedule_index
            << " seat=" << context.seat
            << " root="
            << old_school::deck_name(context.root_deck)
            << '(' << root_id << ')'
            << " opponent="
            << old_school::deck_name(context.opponent_deck)
            << '(' << opponent_id << ')'
            << " trace-root=" << context.trace_ordinal
            << " turn=" << context.turn_number
            << " phase=" << dc1_phase_name(context.phase)
            << " passes=" << context.consecutive_passes
            << " stack=" << context.stack_size
            << " actions=" << context.actions.legal_actions
            << " kinds=";
        print_dc1_action_kind_histogram(
            context.actions.action_kinds);
        std::cout
            << " descriptors="
            << (context.actions.descriptors_distinct
                    ? "distinct"
                    : "DUPLICATE")
            << " fnv1a64="
            << dc1_hex64(
                   context.actions
                       .sorted_descriptor_fnv1a64)
            << '\n';
    }
}

void print_dc1_census_report(
    const old_school::probes::Dc1ActionCensusReport&
        report) {
    std::cout
        << "DC1-B0 Legal-Action Census\n"
        << "Evaluation-only; no pair settlement, density "
           "evaluation, fit, filter, model write, or deployment.\n"
        << "Frozen parent: " << report.model_fingerprint << '\n'
        << "Configuration: K=" << report.config.worlds
        << ", blocks/split=" << report.config.blocks_per_split
        << ", max_turns=" << report.config.max_game_turns
        << ", threshold=" << report.config.threshold
        << ", diagnostic-ceiling="
        << report.config.diagnostic_ceiling
        << ", training-exploration="
        << format_real(
               report.config.training_exploration_rate)
        << "\n";
    print_dc1_census_split("Training census", report.training);
    print_dc1_census_split("Held-out census", report.heldout);
    std::cout
        << "Split seeds distinct: "
        << (report.config.training_seed !=
                    report.config.heldout_seed
                ? "PASS"
                : "FAIL")
        << "\nPair comparisons evaluated: "
        << report.pair_comparisons
        << "\nDensity examples evaluated: "
        << report.density_examples
        << "\nAccounting: "
        << (report.accounting_passed ? "PASS" : "FAIL")
        << "\nReproduced >64 root: "
        << (report.reproduced_over_threshold_root
                ? "PASS"
                : "FAIL")
        << "\nMaximum <=512: "
        << (report.ceiling_passed ? "PASS" : "FAIL")
        << "\nVerdict: "
        << (report.gate_passed ? "ACCEPT" : "REJECT")
        << (report.gate_passed
                ? " (licenses only a separately declared density bound)"
                : " (density remains unlicensed)")
        << '\n';
}

void print_bsr_best_actions(
    const std::vector<std::string>& actions) {
    std::cout << '{';
    for (std::size_t index = 0;
         index < actions.size(); ++index) {
        std::cout << (index == 0 ? "" : ",")
                  << actions[index];
    }
    std::cout << '}';
}

void print_bsr_report(
    const old_school::probes::BsrAuditReport& report,
    double elapsed_seconds) {
    std::cout
        << "BSR0 Environment-v3 Blue-Held Stack-Regret Audit\n"
        << "Load-only diagnostic; Handcrafted supplies source-game "
           "opposition only. Every score/continuation is frozen "
           "Learned mirror play.\n"
        << "Environment: "
        << old_school::probes::kBsrEnvironmentRevision
        << '\n'
        << "Frozen model: " << report.model_fingerprint
        << "\nSource: seed=" << report.config.source_seed
        << ", blocks=" << report.config.source_blocks
        << ", games=" << report.source_games
        << ", max-turns=" << report.config.source_max_turns
        << ", production=K"
        << report.config.production_worlds
        << "/H4, roots/loss<="
        << report.config.roots_per_loss
        << ", roots/opponent="
        << report.config.roots_per_opponent
        << "\nReference: seed="
        << report.config.reference_seed
        << ", scout=K"
        << report.config.reference.scout_worlds
        << ", confirmation=K"
        << report.config.reference.confirmation_worlds
        << ", H=" << report.config.reference.horizon_turns
        << ", rollouts/world="
        << report.config.reference.rollouts_per_world
        << ", threads="
        << report.config.reference.evaluation_threads
        << ", Value mirror, epsilon=0, residual=0, "
           "shallow blend=off\n"
        << "Source totals: losses=" << report.tracked_losses
        << ", draws=" << report.draws
        << ", turn-limit-draws="
        << report.turn_limit_draws
        << ", trace-roots=" << report.trace_roots
        << ", Blue-held/opponent-top="
        << report.tracked_held_opponent_stack_roots
        << ", rejected opponent-held/opponent-top="
        << report.opponent_held_opponent_stack_roots
        << ", eligible loss roots="
        << report.eligible_loss_roots
        << ", losses with eligible roots="
        << report.loss_games_with_eligible_roots
        << "\n\nSource cells (opponent, tracked seat, play/draw):\n";
    for (const auto& cell : report.source_cells) {
        std::cout
            << "  "
            << old_school::deck_name(cell.opponent_deck)
            << " seat=" << cell.tracked_seat
            << ' '
            << (cell.tracked_starts ? "play" : "draw")
            << ": games=" << cell.games
            << ", losses=" << cell.tracked_losses
            << ", draws=" << cell.draws
            << ", turn-limit-draws="
            << cell.turn_limit_draws
            << ", roots=" << cell.trace_roots
            << ", Blue-held=" <<
                cell.tracked_held_opponent_stack_roots
            << ", opponent-held="
            << cell.opponent_held_opponent_stack_roots
            << ", eligible-loss=" << cell.eligible_loss_roots
            << ", eligible-loss-games="
            << cell.loss_games_with_eligible_roots
            << ", retained=" << cell.retained_roots << '\n';
    }
    std::cout << "\nOpponent strata:\n";
    for (const auto& deck : report.decks) {
        std::cout
            << "  " << old_school::deck_name(
                   deck.opponent_deck)
            << ": games=" << deck.games
            << ", losses=" << deck.tracked_losses
            << ", draws=" << deck.draws
            << ", turn-limit-draws="
            << deck.turn_limit_draws
            << ", trace-roots=" << deck.trace_roots
            << ", Blue-held=" <<
                deck.tracked_held_opponent_stack_roots
            << ", opponent-held="
            << deck.opponent_held_opponent_stack_roots
            << ", eligible-loss=" << deck.eligible_loss_roots
            << ", eligible-loss-games="
            << deck.loss_games_with_eligible_roots
            << ", retained=" << deck.retained_roots
            << " from " << deck.retained_distinct_losses
            << " losses, diagnostic mistakes="
            << deck.diagnostic_stable_mistakes
            << ", practical mistakes="
            << deck.practical_high_cost_mistakes
            << ", reference="
            << deck.reference_rollout_evaluations
            << " (terminal="
            << deck.reference_terminal_evaluations
            << ", bootstrap="
            << deck.reference_bootstrapped_evaluations
            << ")\n";
    }

    std::size_t deck_games = 0;
    std::size_t deck_losses = 0;
    std::size_t deck_draws = 0;
    std::size_t deck_turn_limit_draws = 0;
    std::size_t deck_trace_roots = 0;
    std::size_t deck_tracked_held = 0;
    std::size_t deck_opponent_held = 0;
    std::size_t deck_eligible = 0;
    std::size_t deck_eligible_games = 0;
    std::size_t deck_retained = 0;
    std::size_t deck_diagnostic = 0;
    std::size_t deck_practical = 0;
    std::size_t deck_reference_rollouts = 0;
    std::size_t deck_reference_terminal = 0;
    std::size_t deck_reference_bootstrapped = 0;
    for (const auto& deck : report.decks) {
        deck_games += deck.games;
        deck_losses += deck.tracked_losses;
        deck_draws += deck.draws;
        deck_turn_limit_draws += deck.turn_limit_draws;
        deck_trace_roots += deck.trace_roots;
        deck_tracked_held +=
            deck.tracked_held_opponent_stack_roots;
        deck_opponent_held +=
            deck.opponent_held_opponent_stack_roots;
        deck_eligible += deck.eligible_loss_roots;
        deck_eligible_games +=
            deck.loss_games_with_eligible_roots;
        deck_retained += deck.retained_roots;
        deck_diagnostic +=
            deck.diagnostic_stable_mistakes;
        deck_practical +=
            deck.practical_high_cost_mistakes;
        deck_reference_rollouts +=
            deck.reference_rollout_evaluations;
        deck_reference_terminal +=
            deck.reference_terminal_evaluations;
        deck_reference_bootstrapped +=
            deck.reference_bootstrapped_evaluations;
    }
    std::size_t root_reference_rollouts = 0;
    std::size_t root_reference_terminal = 0;
    std::size_t root_reference_bootstrapped = 0;
    for (const auto& root : report.roots) {
        root_reference_rollouts +=
            root.score.rollout_evaluations;
        root_reference_terminal +=
            root.score.terminal_evaluations;
        root_reference_bootstrapped +=
            root.score.bootstrapped_evaluations;
    }
    std::cout
        << "\nAccounting cross-sums "
           "(opponent-strata sum/aggregate):\n"
        << "  games=" << deck_games << '/'
        << report.source_games
        << ", losses=" << deck_losses << '/'
        << report.tracked_losses
        << ", draws=" << deck_draws << '/'
        << report.draws
        << ", turn-limit-draws="
        << deck_turn_limit_draws << '/'
        << report.turn_limit_draws
        << ", trace-roots=" << deck_trace_roots << '/'
        << report.trace_roots << '\n'
        << "  Blue-held=" << deck_tracked_held << '/'
        << report.tracked_held_opponent_stack_roots
        << ", opponent-held=" << deck_opponent_held << '/'
        << report.opponent_held_opponent_stack_roots
        << ", eligible-loss=" << deck_eligible << '/'
        << report.eligible_loss_roots
        << ", eligible-loss-games="
        << deck_eligible_games << '/'
        << report.loss_games_with_eligible_roots
        << ", retained=" << deck_retained << '/'
        << report.roots.size() << '\n'
        << "  diagnostic=" << deck_diagnostic << '/'
        << report.diagnostic_stable_mistakes
        << ", practical=" << deck_practical << '/'
        << report.practical_high_cost_mistakes
        << ", reference-rollouts="
        << deck_reference_rollouts << '/'
        << report.reference_rollout_evaluations
        << ", terminal=" << deck_reference_terminal << '/'
        << report.reference_terminal_evaluations
        << ", bootstrap=" << deck_reference_bootstrapped << '/'
        << report.reference_bootstrapped_evaluations << '\n'
        << "  root-reference-rollouts="
        << root_reference_rollouts << '/'
        << report.reference_rollout_evaluations
        << ", terminal=" << root_reference_terminal << '/'
        << report.reference_terminal_evaluations
        << ", bootstrap="
        << root_reference_bootstrapped << '/'
        << report.reference_bootstrapped_evaluations
        << ", retained distinct losses="
        << report.retained_distinct_losses << '\n';

    std::cout << "\nRetained roots and split-sample reference:\n";
    for (std::size_t index = 0;
         index < report.roots.size(); ++index) {
        const auto& root = report.roots[index];
        const auto& score = root.score;
        std::cout
            << "  [" << index + 1 << "] "
            << root.stable_id
            << "\n      opponent="
            << old_school::deck_name(root.opponent_deck)
            << ", stable-root="
            << root.stable_root_fingerprint
            << ", info/actions="
            << root.information_action_fingerprint
            << ", block=" << root.block
            << ", cell=" << root.schedule_index
            << ", seat=" << root.tracked_seat
            << ", " << (root.tracked_starts ? "play" : "draw")
            << ", game-seed=" << root.game_seed
            << ", trace=" << root.trace_ordinal
            << ", turn=" << root.turn_number
            << ", phase=" << dc1_phase_name(root.phase)
            << ", actions=" << root.action_count
            << ", actual[" << score.actual_action_index
            << "]=" << root.actual_action_descriptor
            << "\n      scout seed=" << score.scout_seed
            << ", best=";
        print_bsr_best_actions(score.scout_best_actions);
        std::cout
            << ", Q(actual)="
            << format_real(score.scout_actual_mean)
            << ", Q(best)="
            << format_real(score.scout_best_mean)
            << "\n      confirm seed="
            << score.confirmation_seed << ", best=";
        print_bsr_best_actions(
            score.confirmation_best_actions);
        std::cout
            << ", Q(actual)="
            << format_real(
                   score.confirmation_actual_mean)
            << ", Q(best)="
            << format_real(
                   score.confirmation_best_mean)
            << ", regret="
            << format_real(score.confirmation_regret)
            << ", paired-SE="
            << format_real(score.paired_standard_error)
            << ", lower95="
            << format_real(score.paired_lower_95)
            << "\n      stable-best="
            << (score
                        .scout_confirmation_best_set_stable
                    ? "PASS"
                    : "FAIL")
            << ", actual-outside="
            << (score.actual_outside_best_sets
                    ? "yes"
                    : "no")
            << ", diagnostic>=0.05/lower>0="
            << (score.diagnostic_stable_mistake
                    ? "YES"
                    : "no")
            << ", practical>=0.20/lower>0.10="
            << (score.practical_high_cost_mistake
                    ? "YES"
                    : "no")
            << ", descriptor-order="
            << (score.descriptor_order_invariant
                    ? "PASS"
                    : "FAIL")
            << ", hidden eligibility/scores="
            << (score.hidden_repartition_eligible
                    ? "PASS"
                    : "FAIL")
            << '/'
            << (score.hidden_repartition_bit_identical
                    ? "PASS"
                    : "FAIL")
            << ", evals=" << score.rollout_evaluations
            << " (terminal=" << score.terminal_evaluations
            << ", bootstrap="
            << score.bootstrapped_evaluations
            << "), accounting="
            << (score.accounting_passed ? "PASS" : "FAIL")
            << '\n';
    }

    std::cout
        << "\nAggregate mistakes: diagnostic="
        << report.diagnostic_stable_mistakes
        << ", practical-high-cost="
        << report.practical_high_cost_mistakes
        << ", practical opponent strata="
        << report.mistake_opponent_strata
        << "\nReference evaluations: "
        << report.reference_rollout_evaluations
        << " (terminal="
        << report.reference_terminal_evaluations
        << ", bootstrap="
        << report.reference_bootstrapped_evaluations
        << "; declared max="
        << old_school::probes::
               kBsrMaximumReferenceEvaluations
        << ")\nChecks: source-balance="
        << (report.source_balance_passed ? "PASS" : "FAIL")
        << ", retention="
        << (report.retention_passed ? "PASS" : "FAIL")
        << ", traced-actions="
        << (report.traced_actions_valid ? "PASS" : "FAIL")
        << ", descriptor-order="
        << (report.descriptor_order_invariant ? "PASS" : "FAIL")
        << ", hidden="
        << (report.hidden_repartition_passed ? "PASS" : "FAIL")
        << ", split-seeds="
        << (report.scout_confirmation_seeds_disjoint
                ? "PASS"
                : "FAIL")
        << ", accounting="
        << (report.accounting_passed ? "PASS" : "FAIL")
        << ", bounds="
        << (report.bounds_passed ? "PASS" : "FAIL")
        << "\nAudit validity: "
        << (report.audit_valid ? "PASS" : "INVALID")
        << "\nMinimum diagnostic replication: "
        << (report.diagnostic_replication_found
                ? "FOUND"
                : "NOT FOUND")
        << "\nBSR0 practical high-cost verdict: "
        << (report.gate_passed ? "PASS" : "INCONCLUSIVE")
        << "\nAudit time: " << format_real(elapsed_seconds)
        << " seconds\nIntended process exit: "
        << (report.gate_passed ? 0 : 1) << '\n';
}

void require_p_family_invariant(bool condition,
                                std::string_view description) {
    if (!condition) {
        throw std::logic_error(
            "P-family invariant failed: " +
            std::string(description));
    }
}

void validate_p_family_result(
    const old_school::LearnedValuePolicyFamilyResult& result,
    std::uint64_t root_seed, std::size_t requested_generations,
    old_school::LearnedValuePriorityHeadUpdateConfig
        expected_optimizer = {},
    bool expected_canonical_recipe = true) {
    constexpr std::size_t kCanonicalGames = 40;
    constexpr std::size_t kCanonicalSeatGames = 80;
    constexpr std::size_t kCanonicalSeatGamesPerDeck = 16;
    constexpr std::size_t kCanonicalSeatZeroGamesPerDeck = 8;
    constexpr std::size_t kCanonicalStartingGamesPerDeck = 8;
    constexpr std::size_t kCanonicalWorlds = 8;
    constexpr std::size_t kCanonicalRootCap = 32;
    constexpr std::size_t kCanonicalMaxTurns = 500;
    constexpr std::size_t kCanonicalCollectionThreads = 4;

    require_p_family_invariant(
        result.checkpoints.size() == requested_generations + 1,
        "checkpoint count");
    require_p_family_invariant(
        result.reports.size() == requested_generations,
        "generation-report count");

    for (std::size_t index = 0;
         index < result.reports.size(); ++index) {
        const auto& report = result.reports[index];
        const auto& parent = result.checkpoints[index];
        const auto& candidate = result.checkpoints[index + 1];
        const std::size_t generation = index + 1;

        require_p_family_invariant(
            report.root_seed == root_seed,
            "reported root seed");
        require_p_family_invariant(
            report.generation == generation,
            "generation number");
        require_p_family_invariant(
            report.canonical_recipe == expected_canonical_recipe &&
                report.search_worlds == kCanonicalWorlds &&
                report.rollouts_per_world == 1 &&
                report.search_horizon_turns == 4 &&
                report.max_roots_per_actor_game ==
                    kCanonicalRootCap &&
                report.max_game_turns == kCanonicalMaxTurns &&
                report.collection_threads ==
                    kCanonicalCollectionThreads &&
                report.residual_weight == 0.10 &&
                report.td_lambda == 0.90,
            "canonical search and return recipe");
        auto reported_optimizer = report.optimizer;
        reported_optimizer.seed = 0;
        expected_optimizer.seed = 0;
        require_p_family_invariant(
            reported_optimizer == expected_optimizer,
            "expected optimizer recipe");

        require_p_family_invariant(
            report.parent_fingerprint ==
                old_school::learned_model_fingerprint(parent),
            "parent fingerprint");
        require_p_family_invariant(
            report.candidate_fingerprint ==
                old_school::learned_model_fingerprint(candidate),
            "candidate fingerprint");
        require_p_family_invariant(
            report.parent_components ==
                old_school::learned_model_component_fingerprints(
                    parent),
            "parent component fingerprints");
        require_p_family_invariant(
            report.candidate_components ==
                old_school::learned_model_component_fingerprints(
                    candidate),
            "candidate component fingerprints");
        require_p_family_invariant(
            report.parent_components.critic ==
                    report.candidate_components.critic &&
                report.parent_components.attack ==
                    report.candidate_components.attack &&
                report.parent_components.block ==
                    report.candidate_components.block &&
                report.parent_components.damage_order ==
                    report.candidate_components.damage_order,
            "critic and non-Priority component isolation");

        require_p_family_invariant(
            report.games.size() == kCanonicalGames,
            "40-game schedule");
        std::array<old_school::LearnedValuePolicyDeckReport,
                   old_school::kDeckCount>
            reconstructed_decks;
        std::size_t raw_roots = 0;
        std::size_t raw_options = 0;
        std::size_t retained_roots = 0;
        std::size_t rollout_evaluations = 0;
        double policy_weight = 0.0;
        for (std::size_t game_index = 0;
             game_index < report.games.size(); ++game_index) {
            const auto& game = report.games[game_index];
            require_p_family_invariant(
                game.schedule_index == game_index &&
                    game.pairing_index < 10 &&
                    game.starting_player < 2 &&
                    game.winner >= -1 && game.winner <= 1,
                "game schedule metadata");
            require_p_family_invariant(
                game.seat_decks[0] != game.seat_decks[1],
                "distinct scheduled decks");
            for (std::size_t player = 0; player < 2;
                 ++player) {
                const std::size_t deck =
                    static_cast<std::size_t>(
                        game.seat_decks[player]);
                require_p_family_invariant(
                    deck < old_school::kDeckCount,
                    "scheduled deck id");
                require_p_family_invariant(
                    game.raw_priority_roots[player] > 0 &&
                        game.raw_legal_options[player] >=
                            2 *
                                game.raw_priority_roots[player] &&
                        game.retained_priority_roots[player] > 0 &&
                        game.retained_priority_roots[player] <=
                            kCanonicalRootCap &&
                        game.retained_ordinals[player].size() ==
                            game.retained_priority_roots[player] &&
                        game.rollout_evaluations[player] ==
                            kCanonicalWorlds *
                                game.raw_legal_options[player] &&
                        approximately_equal(
                            game.policy_weight_sums[player],
                            1.0),
                    "per-seat root and rollout accounting");
                require_p_family_invariant(
                    game.retained_ordinals[player] ==
                        old_school::learned_iteration::
                            evenly_spaced_retained_indices(
                                game.raw_priority_roots[player],
                                kCanonicalRootCap),
                    "exact evenly spaced root retention");
                std::size_t prior_ordinal = 0;
                bool first_ordinal = true;
                for (const std::size_t ordinal :
                     game.retained_ordinals[player]) {
                    require_p_family_invariant(
                        ordinal <
                                game.raw_priority_roots[player] &&
                            (first_ordinal ||
                             ordinal > prior_ordinal),
                        "retained-root ordinals");
                    prior_ordinal = ordinal;
                    first_ordinal = false;
                }

                auto& deck_report =
                    reconstructed_decks[deck];
                ++deck_report.seat_games;
                if (player == 0) {
                    ++deck_report.seat_zero_games;
                }
                if (game.starting_player == player) {
                    ++deck_report.starting_games;
                }
                deck_report.raw_priority_roots +=
                    game.raw_priority_roots[player];
                deck_report.raw_legal_options +=
                    game.raw_legal_options[player];
                deck_report.retained_priority_roots +=
                    game.retained_priority_roots[player];
                deck_report.rollout_evaluations +=
                    game.rollout_evaluations[player];
                deck_report.policy_weight +=
                    game.policy_weight_sums[player];

                raw_roots +=
                    game.raw_priority_roots[player];
                raw_options +=
                    game.raw_legal_options[player];
                retained_roots +=
                    game.retained_priority_roots[player];
                rollout_evaluations +=
                    game.rollout_evaluations[player];
                policy_weight +=
                    game.policy_weight_sums[player];
            }
        }

        require_p_family_invariant(
            report.raw_priority_roots == raw_roots &&
                report.raw_legal_options == raw_options &&
                report.retained_priority_roots ==
                    retained_roots &&
                report.rollout_evaluations ==
                    rollout_evaluations &&
                report.rollout_evaluations ==
                    kCanonicalWorlds *
                        report.raw_legal_options &&
                report.rootless_actor_games == 0 &&
                approximately_equal(
                    report.policy_weight, policy_weight) &&
                approximately_equal(
                    report.policy_weight,
                    static_cast<double>(
                        kCanonicalSeatGames)),
            "generation totals");

        double positive_weight = 0.0;
        double negative_weight = 0.0;
        double zero_weight = 0.0;
        double conflict_weight = 0.0;
        for (std::size_t deck = 0;
             deck < report.decks.size(); ++deck) {
            const auto& actual = report.decks[deck];
            const auto& reconstructed =
                reconstructed_decks[deck];
            require_p_family_invariant(
                actual.seat_games ==
                        kCanonicalSeatGamesPerDeck &&
                    actual.seat_zero_games ==
                        kCanonicalSeatZeroGamesPerDeck &&
                    actual.starting_games ==
                        kCanonicalStartingGamesPerDeck &&
                    actual.rootless_actor_games == 0 &&
                    actual.raw_priority_roots ==
                        reconstructed.raw_priority_roots &&
                    actual.raw_legal_options ==
                        reconstructed.raw_legal_options &&
                    actual.retained_priority_roots ==
                        reconstructed.retained_priority_roots &&
                    actual.rollout_evaluations ==
                        reconstructed.rollout_evaluations &&
                    approximately_equal(
                        actual.policy_weight,
                        reconstructed.policy_weight) &&
                    approximately_equal(
                        actual.policy_weight,
                        static_cast<double>(
                            kCanonicalSeatGamesPerDeck)) &&
                    approximately_equal(
                        actual.positive_advantage_weight +
                            actual.negative_advantage_weight +
                            actual.zero_advantage_weight,
                        actual.policy_weight),
                "per-deck balance and weight accounting");
            positive_weight +=
                actual.positive_advantage_weight;
            negative_weight +=
                actual.negative_advantage_weight;
            zero_weight += actual.zero_advantage_weight;
            conflict_weight += actual.conflict_weight;
        }

        const auto& mechanism = report.mechanism;
        require_p_family_invariant(
            mechanism.observation_count ==
                    report.retained_priority_roots &&
                mechanism.residual_option_count >=
                    2 * mechanism.observation_count &&
                mechanism.saturated_residual_count <=
                    mechanism.residual_option_count &&
                approximately_equal(
                    mechanism.total_weight,
                    report.policy_weight) &&
                approximately_equal(
                    mechanism.residual_option_weight,
                    report.policy_weight) &&
                approximately_equal(
                    mechanism.positive_advantage_weight,
                    positive_weight) &&
                approximately_equal(
                    mechanism.negative_advantage_weight,
                    negative_weight) &&
                approximately_equal(
                    mechanism.zero_advantage_weight,
                    zero_weight) &&
                approximately_equal(
                    mechanism.conflict_weight,
                    conflict_weight),
            "mechanism accounting");
        require_p_family_invariant(
            std::isfinite(report.minimum_target_sum) &&
                std::isfinite(report.maximum_target_sum) &&
                approximately_equal(
                    report.minimum_target_sum, 1.0) &&
                approximately_equal(
                    report.maximum_target_sum, 1.0),
            "finite normalized targets");

        const std::size_t replay_begin =
            generation > 3 ? generation - 3 : 0;
        std::size_t expected_replay_examples = 0;
        for (std::size_t replay_index = replay_begin;
             replay_index < generation; ++replay_index) {
            expected_replay_examples +=
                result.reports[replay_index]
                    .retained_priority_roots;
        }
        require_p_family_invariant(
            report.replay_generations ==
                    std::min<std::size_t>(generation, 3) &&
                report.replay_examples ==
                    expected_replay_examples,
            "three-generation replay window");
    }
}

void print_component_comparison(
    std::string_view name, const std::string& parent,
    const std::string& candidate) {
    std::cout << "    " << name << ' '
              << (parent == candidate ? "SAME " : "CHANGED ")
              << parent;
    if (parent != candidate) {
        std::cout << " -> " << candidate;
    }
    std::cout << '\n';
}

bool print_p_family_generation_report(
    const old_school::LearnedValuePolicyGenerationReport& report) {
    const auto& mechanism = report.mechanism;
    bool deck_signal_pass = true;
    for (const auto& deck : report.decks) {
        deck_signal_pass =
            deck_signal_pass &&
            deck.positive_advantage_weight > 0.0 &&
            deck.negative_advantage_weight > 0.0 &&
            deck.conflict_weight > 0.0;
    }
    const bool kl_pass =
        mechanism.kl_reduction_defined &&
        std::isfinite(mechanism.kl_reduction_fraction) &&
        mechanism.kl_reduction_fraction >= 0.30;
    const bool movement_pass =
        std::isfinite(
            mechanism.signed_movement_correct_rate) &&
        mechanism.signed_movement_correct_rate > 0.60;
    const bool argmax_pass =
        mechanism.changed_argmax_weight > 0.0;
    const bool saturation_pass =
        std::isfinite(
            mechanism.residual_saturation_fraction) &&
        mechanism.residual_saturation_fraction < 0.05;
    const bool mechanism_pass =
        deck_signal_pass && kl_pass && movement_pass &&
        argmax_pass && saturation_pass;

    std::cout
        << "\nP" << report.generation
        << " generation\n"
        << "  fingerprints: parent="
        << report.parent_fingerprint
        << " candidate=" << report.candidate_fingerprint
        << "\n  recipe: K=" << report.search_worlds
        << " H=" << report.search_horizon_turns
        << " rollout/world=" << report.rollouts_per_world
        << " collection-threads="
        << report.collection_threads
        << "\n  balance: games=" << report.games.size()
        << "/40 seat-games=80/80 rootless="
        << report.rootless_actor_games
        << " accounting=PASS isolation=PASS\n"
        << "  totals: raw-roots="
        << report.raw_priority_roots
        << " raw-options=" << report.raw_legal_options
        << " retained-roots="
        << report.retained_priority_roots
        << " rollout-evaluations="
        << report.rollout_evaluations
        << " policy-weight="
        << format_real(report.policy_weight) << '\n';
    for (std::size_t deck = 0;
         deck < report.decks.size(); ++deck) {
        const auto& metrics = report.decks[deck];
        std::cout
            << "    "
            << old_school::deck_name(
                   static_cast<old_school::DeckId>(deck))
            << ": seats=" << metrics.seat_games
            << " seat0=" << metrics.seat_zero_games
            << " starts=" << metrics.starting_games
            << " roots=" << metrics.raw_priority_roots
            << '/' << metrics.retained_priority_roots
            << " options/evals=" << metrics.raw_legal_options
            << '/' << metrics.rollout_evaluations
            << " weight=" << format_real(metrics.policy_weight)
            << " +A="
            << format_real(
                   metrics.positive_advantage_weight)
            << " -A="
            << format_real(
                   metrics.negative_advantage_weight)
            << " zero="
            << format_real(metrics.zero_advantage_weight)
            << " conflict="
            << format_real(metrics.conflict_weight)
            << '\n';
    }
    std::cout
        << "  replay: generations="
        << report.replay_generations
        << " examples=" << report.replay_examples
        << "\n  target sums: min="
        << format_real(report.minimum_target_sum)
        << " max=" << format_real(report.maximum_target_sum)
        << "\n  KL(y||mu): parent="
        << format_real(mechanism.parent_kl)
        << " candidate="
        << format_real(mechanism.candidate_kl)
        << " reduction="
        << format_real(mechanism.kl_reduction_fraction)
        << " defined="
        << (mechanism.kl_reduction_defined ? "yes" : "no")
        << " gate=" << (kl_pass ? "PASS" : "REJECT")
        << "\n  signed movement: all="
        << format_real(
               mechanism.signed_movement_correct_rate)
        << " ("
        << format_real(
               mechanism.correct_signed_movement_weight)
        << '/'
        << format_real(
               mechanism.eligible_signed_movement_weight)
        << "), +A="
        << format_real(
               mechanism.positive_movement_correct_rate)
        << " ("
        << format_real(
               mechanism.correct_positive_movement_weight)
        << '/'
        << format_real(
               mechanism.eligible_positive_movement_weight)
        << "), -A="
        << format_real(
               mechanism.negative_movement_correct_rate)
        << " ("
        << format_real(
               mechanism.correct_negative_movement_weight)
        << '/'
        << format_real(
               mechanism.eligible_negative_movement_weight)
        << ") gate="
        << (movement_pass ? "PASS" : "REJECT")
        << "\n  argmax changed: weight="
        << format_real(mechanism.changed_argmax_weight)
        << " fraction="
        << format_real(
               mechanism.changed_argmax_weight_fraction)
        << " gate=" << (argmax_pass ? "PASS" : "REJECT")
        << "\n  residual saturation: count="
        << mechanism.saturated_residual_count << '/'
        << mechanism.residual_option_count
        << " weight="
        << format_real(
               mechanism.saturated_residual_weight)
        << '/'
        << format_real(mechanism.residual_option_weight)
        << " fraction="
        << format_real(
               mechanism.residual_saturation_fraction)
        << " gate="
        << (saturation_pass ? "PASS" : "REJECT")
        << "\n  deck +A/-A/conflict gate: "
        << (deck_signal_pass ? "PASS" : "REJECT")
        << "\n  components (bit-exact candidate vs parent):\n";
    print_component_comparison(
        "critic", report.parent_components.critic,
        report.candidate_components.critic);
    print_component_comparison(
        "priority", report.parent_components.priority,
        report.candidate_components.priority);
    print_component_comparison(
        "attack", report.parent_components.attack,
        report.candidate_components.attack);
    print_component_comparison(
        "block", report.parent_components.block,
        report.candidate_components.block);
    print_component_comparison(
        "damage-order",
        report.parent_components.damage_order,
        report.candidate_components.damage_order);
    std::cout
        << "  Mechanism gate P" << report.generation
        << ": "
        << (mechanism_pass ? "PASS" : "REJECT")
        << (mechanism_pass
                ? "\n"
                : " (scientific rejection; exit status remains 0)\n");
    return mechanism_pass;
}

struct P1FitCell {
    std::string_view label;
    std::size_t epochs;
    double learning_rate;
};

constexpr std::array<P1FitCell, 5> kP1FitCells = {{
    {"E8/R0.001-control", 8, 0.001},
    {"E32/R0.001", 32, 0.001},
    {"E128/R0.001", 128, 0.001},
    {"E512/R0.001", 512, 0.001},
    {"E128/R0.003", 128, 0.003},
}};

std::vector<old_school::LearnedValuePriorityHeadUpdateConfig>
p1_fit_diagnostic_optimizers() {
    std::vector<
        old_school::LearnedValuePriorityHeadUpdateConfig>
        optimizers;
    optimizers.reserve(kP1FitCells.size());
    for (const auto& cell : kP1FitCells) {
        old_school::LearnedValuePriorityHeadUpdateConfig optimizer;
        optimizer.epochs = cell.epochs;
        optimizer.learning_rate = cell.learning_rate;
        optimizers.push_back(optimizer);
    }
    return optimizers;
}

bool p1_fit_mechanism_input_equal(
    const old_school::LearnedValuePolicyMechanismReport& left,
    const old_school::LearnedValuePolicyMechanismReport& right) {
    return left.observation_count == right.observation_count &&
           left.residual_option_count ==
               right.residual_option_count &&
           left.total_weight == right.total_weight &&
           left.parent_kl == right.parent_kl &&
           left.positive_advantage_weight ==
               right.positive_advantage_weight &&
           left.negative_advantage_weight ==
               right.negative_advantage_weight &&
           left.zero_advantage_weight ==
               right.zero_advantage_weight &&
           left.conflict_weight == right.conflict_weight &&
           left.eligible_signed_movement_weight ==
               right.eligible_signed_movement_weight &&
           left.eligible_positive_movement_weight ==
               right.eligible_positive_movement_weight &&
           left.eligible_negative_movement_weight ==
               right.eligible_negative_movement_weight &&
           left.residual_option_weight ==
               right.residual_option_weight;
}

void validate_p1_fit_mechanism(
    const old_school::LearnedValuePolicyMechanismReport& metrics,
    std::string_view scope) {
    require_p_family_invariant(
        metrics.observation_count > 0 &&
            metrics.residual_option_count >=
                2 * metrics.observation_count &&
            metrics.saturated_residual_count <=
                metrics.residual_option_count &&
            metrics.total_weight > 0.0 &&
            std::isfinite(metrics.parent_kl) &&
            std::isfinite(metrics.candidate_kl) &&
            metrics.kl_reduction_defined &&
            std::isfinite(metrics.kl_reduction_fraction) &&
            metrics.positive_advantage_weight > 0.0 &&
            metrics.negative_advantage_weight > 0.0 &&
            metrics.conflict_weight > 0.0 &&
            metrics.correct_signed_movement_weight <=
                metrics.eligible_signed_movement_weight &&
            metrics.correct_positive_movement_weight <=
                metrics.eligible_positive_movement_weight &&
            metrics.correct_negative_movement_weight <=
                metrics.eligible_negative_movement_weight &&
            std::isfinite(
                metrics.signed_movement_correct_rate) &&
            std::isfinite(
                metrics.positive_movement_correct_rate) &&
            std::isfinite(
                metrics.negative_movement_correct_rate) &&
            metrics.changed_argmax_weight <=
                metrics.total_weight &&
            std::isfinite(
                metrics.changed_argmax_weight_fraction) &&
            metrics.saturated_residual_weight <=
                metrics.residual_option_weight &&
            std::isfinite(
                metrics.residual_saturation_fraction),
        std::string("P1 fit mechanism metrics for ") +
            std::string(scope));
}

void validate_p1_fit_diagnostic(
    const old_school::LearnedValuePolicyFamilyResult& family) {
    require_p_family_invariant(
        family.reports.size() == 1 &&
            family.checkpoints.size() == 2,
        "P1 fit diagnostic family shape");
    const auto& report = family.reports.front();
    const auto& diagnostics = report.capacity_diagnostics;
    require_p_family_invariant(
        diagnostics.size() == kP1FitCells.size(),
        "P1 fit diagnostic cell count");
    require_p_family_invariant(
        diagnostics.front().optimizer == report.optimizer &&
            diagnostics.front().parent_fingerprint ==
                report.parent_fingerprint &&
            diagnostics.front().candidate_fingerprint ==
                report.candidate_fingerprint &&
            diagnostics.front().parent_components ==
                report.parent_components &&
            diagnostics.front().candidate_components ==
                report.candidate_components &&
            diagnostics.front().mechanism ==
                report.mechanism,
        "E8/R0.001 control equivalence");
    require_p_family_invariant(
        report.rootwise_oracle.has_value(),
        "P1 rootwise oracle presence");
    const auto& oracle = *report.rootwise_oracle;
    require_p_family_invariant(
        oracle.observation_count ==
                report.mechanism.observation_count &&
            oracle.observation_count ==
                report.retained_priority_roots &&
            oracle.total_weight ==
                report.mechanism.total_weight &&
            oracle.total_weight == report.policy_weight &&
            approximately_equal(
                oracle.parent_kl,
                report.mechanism.parent_kl) &&
            oracle.reduction_defined ==
                report.mechanism.kl_reduction_defined,
        "P1 rootwise oracle shard identity");
    const auto validate_oracle_bound =
        [&](const old_school::
                LearnedValuePolicyRootwiseOracleBoundReport&
                    bound,
            std::string_view name) {
            require_p_family_invariant(
                std::isfinite(bound.numerical_best_kl) &&
                    bound.numerical_best_kl >= 0.0 &&
                    std::isfinite(
                        bound.achievable_reduction_fraction) &&
                    std::isfinite(
                        bound.certified_kl_lower_bound) &&
                    bound.certified_kl_lower_bound >= 0.0 &&
                    std::isfinite(
                        bound.certified_reduction_upper_bound) &&
                    bound.achievable_reduction_fraction <=
                        bound.certified_reduction_upper_bound &&
                    std::isfinite(
                        bound.maximum_abs_squashed_residual) &&
                    bound.maximum_abs_squashed_residual >= 0.0 &&
                    bound.maximum_abs_squashed_residual <= 1.0,
                std::string("P1 rootwise oracle ") +
                    std::string(name));
        };
    validate_oracle_bound(oracle.full_range, "full range");
    validate_oracle_bound(
        oracle.zero_saturation, "zero saturation");

    for (std::size_t cell = 0;
         cell < diagnostics.size(); ++cell) {
        const auto& diagnostic = diagnostics[cell];
        auto expected_optimizer =
            old_school::LearnedValuePriorityHeadUpdateConfig{};
        expected_optimizer.epochs =
            kP1FitCells[cell].epochs;
        expected_optimizer.learning_rate =
            kP1FitCells[cell].learning_rate;
        expected_optimizer.seed = report.optimizer.seed;
        require_p_family_invariant(
            diagnostic.optimizer == expected_optimizer,
            "P1 fit optimizer identity");
        require_p_family_invariant(
            diagnostic.parent_fingerprint ==
                    report.parent_fingerprint &&
                diagnostic.parent_components ==
                    report.parent_components &&
                diagnostic.candidate_components.critic ==
                    report.parent_components.critic &&
                diagnostic.candidate_components.attack ==
                    report.parent_components.attack &&
                diagnostic.candidate_components.block ==
                    report.parent_components.block &&
                diagnostic.candidate_components.damage_order ==
                    report.parent_components.damage_order,
            "P1 fit same-parent component isolation");
        validate_p1_fit_mechanism(
            diagnostic.mechanism, "pooled");
        require_p_family_invariant(
            p1_fit_mechanism_input_equal(
                diagnostic.mechanism,
                diagnostics.front().mechanism),
            "P1 fit pooled same-shard identity");
        for (std::size_t deck = 0;
             deck < old_school::kDeckCount; ++deck) {
            const auto& deck_metrics =
                diagnostic.mechanisms_by_deck[deck];
            validate_p1_fit_mechanism(
                deck_metrics,
                old_school::deck_name(
                    static_cast<old_school::DeckId>(deck)));
            require_p_family_invariant(
                p1_fit_mechanism_input_equal(
                    deck_metrics,
                    diagnostics.front()
                        .mechanisms_by_deck[deck]),
                "P1 fit per-deck same-shard identity");
        }
    }
}

void print_weighted_rate(double numerator, double denominator,
                         double rate) {
    std::cout << format_real(numerator) << '/'
              << format_real(denominator) << '('
              << format_real(rate) << ')';
}

void print_p1_fit_metric_row(
    std::string_view cell, std::string_view scope,
    const old_school::LearnedValuePolicyMechanismReport& metrics) {
    std::cout << "  " << cell << " | " << scope
              << " | " << format_real(metrics.parent_kl)
              << " | " << format_real(metrics.candidate_kl)
              << " | "
              << format_real(metrics.kl_reduction_fraction)
              << " | ";
    print_weighted_rate(
        metrics.correct_signed_movement_weight,
        metrics.eligible_signed_movement_weight,
        metrics.signed_movement_correct_rate);
    std::cout << " | ";
    print_weighted_rate(
        metrics.correct_positive_movement_weight,
        metrics.eligible_positive_movement_weight,
        metrics.positive_movement_correct_rate);
    std::cout << " | ";
    print_weighted_rate(
        metrics.correct_negative_movement_weight,
        metrics.eligible_negative_movement_weight,
        metrics.negative_movement_correct_rate);
    std::cout << " | ";
    print_weighted_rate(
        metrics.changed_argmax_weight,
        metrics.total_weight,
        metrics.changed_argmax_weight_fraction);
    std::cout << " | ";
    print_weighted_rate(
        metrics.saturated_residual_weight,
        metrics.residual_option_weight,
        metrics.residual_saturation_fraction);
    std::cout << '\n';
}

void print_p1_fit_diagnostic(
    const old_school::LearnedValuePolicyFamilyResult& family,
    std::uint64_t root_seed, std::size_t training_games,
    std::uint64_t training_seed, double elapsed_seconds) {
    const auto& report = family.reports.front();
    std::cout
        << "\nP1 Same-Parent Fit Diagnostic\n"
        << "  root-seed=" << root_seed
        << " train-games=" << training_games
        << " train-seed=" << training_seed
        << "\n  collection: games=" << report.games.size()
        << " seat-games=80 K=" << report.search_worlds
        << "/H=" << report.search_horizon_turns
        << " root-cap=" << report.max_roots_per_actor_game
        << " max-turns=" << report.max_game_turns
        << " threads=" << report.collection_threads
        << " retained-roots="
        << report.retained_priority_roots
        << " rollout-evaluations="
        << report.rollout_evaluations
        << "\n  frozen-parent="
        << report.parent_fingerprint
        << "\n  control-candidate="
        << report.candidate_fingerprint
        << "\n  control equivalence: PASS"
        << "\n  all-cell same-parent isolation: PASS"
        << "\n  rootwise-oracle shard identity: PASS"
        << "\n  elapsed-seconds="
        << format_real(elapsed_seconds)
        << "\n\nCandidate fingerprints\n";
    for (std::size_t cell = 0;
         cell < report.capacity_diagnostics.size(); ++cell) {
        const auto& diagnostic =
            report.capacity_diagnostics[cell];
        std::cout
            << "  " << kP1FitCells[cell].label
            << ": model="
            << diagnostic.candidate_fingerprint
            << " priority="
            << diagnostic.candidate_components.priority
            << " fit-seed=" << diagnostic.optimizer.seed
            << " isolation=PASS\n";
    }

    const auto& oracle = *report.rootwise_oracle;
    const auto print_oracle_bound =
        [&](std::string_view name,
            const old_school::
                LearnedValuePolicyRootwiseOracleBoundReport&
                    bound) {
            std::cout
                << "  " << name << " | "
                << format_real(bound.numerical_best_kl)
                << " | "
                << format_real(
                       bound.achievable_reduction_fraction)
                << " | "
                << format_real(
                       bound.certified_kl_lower_bound)
                << " | "
                << format_real(
                       bound.certified_reduction_upper_bound)
                << " | "
                << format_real(
                       bound.maximum_abs_squashed_residual)
                << " | "
                << bound.roots_reaching_iteration_limit
                << '\n';
        };
    std::cout
        << "\nIndependent-root capacity bracket"
        << "\n  observations=" << oracle.observation_count
        << " weight=" << format_real(oracle.total_weight)
        << " parent-KL=" << format_real(oracle.parent_kl)
        << " reduction-defined="
        << (oracle.reduction_defined ? "yes" : "no")
        << "\n  range | numerical-best-KL | achievable-reduction | "
           "certified-KL-lower | certified-reduction-upper | "
           "max-abs-squashed-residual | iteration-limit-roots\n";
    print_oracle_bound("full", oracle.full_range);
    print_oracle_bound(
        "zero-saturation", oracle.zero_saturation);

    std::cout
        << "\nMetrics"
        << "\n  cell | scope | parent-KL | candidate-KL | "
           "KL-reduction | signed | signed+ | signed- | "
           "argmax | saturation\n";
    for (std::size_t cell = 0;
         cell < report.capacity_diagnostics.size(); ++cell) {
        const auto& diagnostic =
            report.capacity_diagnostics[cell];
        print_p1_fit_metric_row(
            kP1FitCells[cell].label, "Pooled",
            diagnostic.mechanism);
        for (std::size_t deck = 0;
             deck < old_school::kDeckCount; ++deck) {
            print_p1_fit_metric_row(
                kP1FitCells[cell].label,
                old_school::deck_name(
                    static_cast<old_school::DeckId>(deck)),
                diagnostic.mechanisms_by_deck[deck]);
        }
    }
}

bool same_real_bits(double left, double right) {
    return std::bit_cast<std::uint64_t>(left) ==
           std::bit_cast<std::uint64_t>(right);
}

bool same_deck_probe_metrics(
    const old_school::probe_eval::DeckProbeMetrics& left,
    const old_school::probe_eval::DeckProbeMetrics& right) {
    return left.root_deck == right.root_deck &&
           left.probe_count == right.probe_count &&
           left.stable_pair_count == right.stable_pair_count &&
           same_real_bits(
               left.top1_expected_agreement,
               right.top1_expected_agreement) &&
           same_real_bits(
               left.stable_pair_agreement,
               right.stable_pair_agreement) &&
           same_real_bits(left.mean_regret, right.mean_regret) &&
           same_real_bits(
               left.critic_brier, right.critic_brier) &&
           same_real_bits(left.critic_mse, right.critic_mse) &&
           same_real_bits(
               left.critic_log_loss, right.critic_log_loss) &&
           same_real_bits(left.critic_bias, right.critic_bias) &&
           same_real_bits(left.critic_ece, right.critic_ece);
}

bool same_probe_metrics(
    const old_school::probe_eval::ProbeMetricSummary& left,
    const old_school::probe_eval::ProbeMetricSummary& right) {
    if (left.probe_count != right.probe_count ||
        left.stable_pair_count != right.stable_pair_count ||
        !same_real_bits(
            left.top1_expected_agreement,
            right.top1_expected_agreement) ||
        !same_real_bits(
            left.stable_pair_agreement,
            right.stable_pair_agreement) ||
        !same_real_bits(left.mean_regret, right.mean_regret) ||
        !same_real_bits(left.critic_brier, right.critic_brier) ||
        !same_real_bits(left.critic_mse, right.critic_mse) ||
        !same_real_bits(
            left.critic_log_loss, right.critic_log_loss) ||
        !same_real_bits(left.critic_bias, right.critic_bias) ||
        !same_real_bits(left.critic_ece, right.critic_ece)) {
        return false;
    }
    for (std::size_t deck = 0; deck < left.by_deck.size();
         ++deck) {
        if (!same_deck_probe_metrics(
                left.by_deck[deck], right.by_deck[deck])) {
            return false;
        }
    }
    return true;
}

bool same_value_probe_decision(
    const old_school::probe_runner::ValueProbeDecisionDetail& left,
    const old_school::probe_runner::ValueProbeDecisionDetail& right) {
    return left.stable_id == right.stable_id &&
           left.root_deck == right.root_deck &&
           left.selected_keys == right.selected_keys &&
           left.deterministic_selection ==
               right.deterministic_selection &&
           left.reference_best_set == right.reference_best_set &&
           same_real_bits(left.regret, right.regret) &&
           same_real_bits(
               left.critic_prediction,
               right.critic_prediction) &&
           same_real_bits(
               left.selected_action_reference_q,
               right.selected_action_reference_q) &&
           same_real_bits(
               left.critic_error, right.critic_error) &&
           left.selection_changed_from_reference ==
               right.selection_changed_from_reference &&
           left.selection_changed_from_previous ==
               right.selection_changed_from_previous;
}

const old_school::probe_runner::ValueCheckpointProbeReport&
checkpoint_named(
    const old_school::probe_runner::ProbeScoreReport& report,
    std::string_view name) {
    const auto found = std::find_if(
        report.value_checkpoints.begin(),
        report.value_checkpoints.end(),
        [name](const auto& checkpoint) {
            return checkpoint.name == name;
        });
    if (found == report.value_checkpoints.end()) {
        throw std::logic_error(
            "P1R probe report is missing checkpoint " +
            std::string(name));
    }
    return *found;
}

const old_school::probe_runner::ValueProbeDecisionDetail&
decision_named(
    const old_school::probe_runner::ValueCheckpointProbeReport&
        checkpoint,
    std::string_view stable_id) {
    const auto found = std::find_if(
        checkpoint.decisions.begin(), checkpoint.decisions.end(),
        [stable_id](const auto& decision) {
            return decision.stable_id == stable_id;
        });
    if (found == checkpoint.decisions.end()) {
        throw std::logic_error(
            "P1R checkpoint is missing decision " +
            std::string(stable_id));
    }
    return *found;
}

const old_school::probe_runner::ForceSpikePolicyControlReport&
force_spike_control_named(
    const old_school::probe_runner::ProbeScoreReport& report,
    std::string_view name) {
    const auto found = std::find_if(
        report.force_spike_controls.begin(),
        report.force_spike_controls.end(),
        [name](const auto& control) {
            return control.policy_name == name;
        });
    if (found == report.force_spike_controls.end()) {
        throw std::logic_error(
            "P1R probe report is missing Force Spike control " +
            std::string(name));
    }
    return *found;
}

const old_school::probe_runner::CandidatePairEstimate&
value_pair_named(
    const old_school::probe_runner::ProbeScoreReport& report,
    std::string_view name) {
    const auto found = std::find_if(
        report.value_candidate_pairs.begin(),
        report.value_candidate_pairs.end(),
        [name](const auto& pair) {
            return pair.name == name;
        });
    if (found == report.value_candidate_pairs.end()) {
        throw std::logic_error(
            "P1R probe report is missing Value pair " +
            std::string(name));
    }
    return *found;
}

bool checkpoint_behavior_bit_identical(
    const old_school::probe_runner::ValueCheckpointProbeReport& left,
    const old_school::probe_runner::ValueCheckpointProbeReport&
        right) {
    if (left.fingerprint != right.fingerprint ||
        !same_probe_metrics(left.metrics, right.metrics) ||
        left.decisions.size() != right.decisions.size()) {
        return false;
    }
    for (const auto& left_decision : left.decisions) {
        const auto& right_decision =
            decision_named(right, left_decision.stable_id);
        if (!same_value_probe_decision(
                left_decision, right_decision)) {
            return false;
        }
    }
    return true;
}

bool selected_inside_reference_best(
    const old_school::probe_runner::ValueProbeDecisionDetail&
        decision) {
    return !decision.selected_keys.empty() &&
           std::all_of(
               decision.selected_keys.begin(),
               decision.selected_keys.end(),
               [&decision](const std::string& selected) {
                   return std::find(
                              decision.reference_best_set.begin(),
                              decision.reference_best_set.end(),
                              selected) !=
                          decision.reference_best_set.end();
               });
}

bool critic_predictions_bit_identical(
    const old_school::probe_runner::ValueCheckpointProbeReport&
        left,
    const old_school::probe_runner::ValueCheckpointProbeReport&
        right) {
    if (left.decisions.size() != right.decisions.size()) {
        return false;
    }
    return std::all_of(
        left.decisions.begin(), left.decisions.end(),
        [&right](const auto& decision) {
            return same_real_bits(
                decision.critic_prediction,
                decision_named(right, decision.stable_id)
                    .critic_prediction);
        });
}

bool evaluate_p1r_offline_gate(
    const old_school::probe_runner::ProbeScoreReport& dev,
    const old_school::probe_runner::ProbeScoreReport& validation) {
    const auto& dev_s0 = checkpoint_named(dev, "P0 residual-off");
    const auto& dev_p0 = checkpoint_named(dev, "P0 residual-on");
    const auto& dev_p1r = checkpoint_named(dev, "P1R");
    const auto& validation_s0 =
        checkpoint_named(validation, "P0 residual-off");
    const auto& validation_p0 =
        checkpoint_named(validation, "P0 residual-on");
    const auto& validation_p1r =
        checkpoint_named(validation, "P1R");
    const auto& p1r_force_spike =
        force_spike_control_named(dev, "P1R");
    const auto& p0_x_zero = value_pair_named(
        validation, "P0 residual-on Q(Pass) - Q(X=0)");
    const auto& p1r_x_zero = value_pair_named(
        validation, "P1R Q(Pass) - Q(X=0)");

    const bool caches_loaded =
        dev.cache_status ==
            old_school::probe_runner::ProbeCacheStatus::Loaded &&
        validation.cache_status ==
            old_school::probe_runner::ProbeCacheStatus::Loaded;
    const bool hidden_invariance =
        dev.hidden_repartition.passed &&
        validation.hidden_repartition.passed;
    const bool p0_identity =
        checkpoint_behavior_bit_identical(dev_s0, dev_p0) &&
        checkpoint_behavior_bit_identical(
            validation_s0, validation_p0);
    const bool critic_identity =
        critic_predictions_bit_identical(dev_p0, dev_p1r) &&
        critic_predictions_bit_identical(
            validation_p0, validation_p1r);
    const bool pooled_regret_improved =
        dev_p1r.metrics.mean_regret <
        dev_p0.metrics.mean_regret;
    bool deck_regret_guard = true;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        deck_regret_guard =
            deck_regret_guard &&
            dev_p1r.metrics.by_deck[deck].mean_regret <=
                dev_p0.metrics.by_deck[deck].mean_regret + 0.01;
    }

    constexpr std::array<std::string_view, 4> kBlueStackProbes = {
        "blue.counter-fire-elemental.v3",
        "blue.counter-lethal-bolt.v3",
        "blue.counter-war.v3",
        "blue.force-spike-tapped-out-gray-ogre.v3",
    };
    bool blue_stack_retained = true;
    for (const std::string_view stable_id : kBlueStackProbes) {
        blue_stack_retained =
            blue_stack_retained &&
            selected_inside_reference_best(
                decision_named(dev_p1r, stable_id));
    }

    const bool force_spike_gate =
        p1r_force_spike.gate_passed();
    const auto& validation_decision = decision_named(
        validation_p1r,
        "validation.ru.disintegrate-hold-x0.v1");
    const bool validation_selects_pass =
        old_school::probe_runner::
            value_decision_uniquely_selects(
                validation_decision, p1r_x_zero.first_key);
    const bool x_zero_q_gate =
        p1r_x_zero.delta_q > p0_x_zero.delta_q &&
        p1r_x_zero.confidence_lower_95 > 0.0;

    const auto print_gate =
        [](std::string_view name, bool passed) {
            std::cout << "  " << name << ": "
                      << (passed ? "PASS" : "REJECT") << '\n';
        };
    std::cout
        << "\nP1R preregistered offline gate\n"
        << "  P0 pooled regret: "
        << format_real(dev_p0.metrics.mean_regret)
        << "\n  P1R pooled regret: "
        << format_real(dev_p1r.metrics.mean_regret)
        << "\n  validation P0 Q(Pass)-Q(X=0): "
        << format_real(p0_x_zero.delta_q)
        << " [" << format_real(p0_x_zero.confidence_lower_95)
        << ", " << format_real(p0_x_zero.confidence_upper_95)
        << "]\n  validation P1R Q(Pass)-Q(X=0): "
        << format_real(p1r_x_zero.delta_q)
        << " [" << format_real(p1r_x_zero.confidence_lower_95)
        << ", " << format_real(p1r_x_zero.confidence_upper_95)
        << "]\n";
    print_gate("immutable caches loaded", caches_loaded);
    print_gate("P0 residual-on identity", p0_identity);
    print_gate("P1R critic prediction identity", critic_identity);
    print_gate("pooled regret strictly improved",
               pooled_regret_improved);
    print_gate("all-five deck regret guard", deck_regret_guard);
    print_gate("Blue stack decisions retained",
               blue_stack_retained);
    print_gate("Force Spike live/payable behavior",
               force_spike_gate);
    print_gate("validation uniquely selects Pass",
               validation_selects_pass);
    print_gate("validation positive Q improvement", x_zero_q_gate);
    print_gate("hidden repartition invariance",
               hidden_invariance);

    const bool passed =
        caches_loaded && p0_identity && critic_identity &&
        pooled_regret_improved && deck_regret_guard &&
        blue_stack_retained && force_spike_gate &&
        validation_selects_pass && x_zero_q_gate &&
        hidden_invariance;
    std::cout << "  Offline verdict: "
              << (passed ? "PASS" : "REJECT")
              << (passed
                      ? " (permits separate P4R declaration)\n"
                      : " (stop: no P4R or gameplay)\n");
    return passed;
}

struct Pd0Fixture {
    old_school::GameState state;
    std::array<std::vector<old_school::CardId>, 2> decks;
    std::size_t player = 0;
    bool sorcery_actions = false;
    old_school::TurnPhase phase =
        old_school::TurnPhase::FirstMain;
    int consecutive_passes = 0;
};

void pd0_remove_one(
    std::vector<old_school::CardId>& cards,
    old_school::CardId card) {
    const auto found = std::find(cards.begin(), cards.end(), card);
    if (found == cards.end()) {
        throw std::logic_error(
            "PD0 fixture exceeds its original deck");
    }
    cards.erase(found);
}

void pd0_complete_libraries(Pd0Fixture& fixture) {
    for (std::size_t player = 0;
         player < fixture.state.players.size(); ++player) {
        std::vector<old_school::CardId> remaining =
            fixture.decks[player];
        const auto remove = [&](old_school::CardId card) {
            pd0_remove_one(remaining, card);
        };
        const auto& state = fixture.state.players[player];
        for (const auto card : state.hand) {
            remove(card);
        }
        for (const auto card : state.graveyard) {
            remove(card);
        }
        for (const auto card : state.exile) {
            remove(card);
        }
        for (const auto& land : state.lands) {
            remove(land.card);
        }
        for (const auto& creature : state.creatures) {
            remove(creature.card);
        }
        for (const auto& artifact : state.artifacts) {
            remove(artifact.card);
        }
        for (const auto card : state.enchantments) {
            remove(card);
        }
        for (const auto& object : fixture.state.stack) {
            if (object.kind ==
                    old_school::StackObjectKind::Spell &&
                object.controller == player) {
                remove(object.card);
            }
        }
        fixture.state.players[player].library =
            std::move(remaining);
    }
}

Pd0Fixture pd0_braingeyser_fixture() {
    Pd0Fixture fixture{
        .decks = {
            old_school::blue_deck(),
            old_school::red_deck(),
        },
        .player = 0,
        .sorcery_actions = true,
        .phase = old_school::TurnPhase::SecondMain,
    };
    fixture.state.active_player = 0;
    fixture.state.starting_player = 0;
    fixture.state.turn_number = 8;
    fixture.state.next_stack_object_id = 20;
    fixture.state.players[0].hand = {
        old_school::CardId::Braingeyser,
    };
    fixture.state.players[0].lands = {
        {.card = old_school::CardId::Island, .tapped = false},
        {.card = old_school::CardId::Island, .tapped = false},
        {.card = old_school::CardId::Island, .tapped = false},
    };
    fixture.state.players[1].hand = {
        old_school::CardId::Mountain,
    };
    pd0_complete_libraries(fixture);
    return fixture;
}

old_school::GameState pd0_hidden_repartition(
    const old_school::GameState& state, std::size_t observer) {
    old_school::GameState changed = state;
    std::reverse(
        changed.players[observer].library.begin(),
        changed.players[observer].library.end());
    const std::size_t opponent = 1 - observer;
    auto& hand = changed.players[opponent].hand;
    auto& library = changed.players[opponent].library;
    std::vector<old_school::CardId> hidden = hand;
    hidden.insert(hidden.end(), library.begin(), library.end());
    if (hidden.size() > 1) {
        std::rotate(hidden.begin(), hidden.begin() + 1,
                    hidden.end());
        std::reverse(hidden.begin(), hidden.end());
    }
    const std::size_t hand_size = hand.size();
    hand.assign(
        hidden.begin(),
        hidden.begin() + static_cast<std::ptrdiff_t>(hand_size));
    library.assign(
        hidden.begin() + static_cast<std::ptrdiff_t>(hand_size),
        hidden.end());
    return changed;
}

std::string pd0_action_name(
    const old_school::PriorityAction& action) {
    if (action.kind == old_school::PriorityActionKind::Pass) {
        return "Pass";
    }
    std::string name(
        old_school::card_definition(action.card).name);
    if (action.kind ==
            old_school::PriorityActionKind::CastDisintegrate ||
        action.kind ==
            old_school::PriorityActionKind::CastBraingeyser) {
        name += " X=" + std::to_string(action.x_value);
    }
    if (action.target.has_value()) {
        name += action.target->creature.has_value()
                    ? " -> creature #" +
                          std::to_string(*action.target->creature)
                    : " -> player " +
                          std::to_string(action.target->player);
    }
    if (action.spell_target.has_value()) {
        name += " -> stack #" +
                std::to_string(*action.spell_target);
    }
    return name;
}

bool pd0_is_dominated(
    const old_school::ValuePassDominanceDiagnostic& diagnostic,
    const std::function<bool(
        const old_school::PriorityAction&)>& predicate) {
    const auto found = std::find_if(
        diagnostic.actions.begin(), diagnostic.actions.end(),
        [&](const auto& action) {
            return predicate(action.action);
        });
    return found != diagnostic.actions.end() &&
           found->comparison_settled &&
           found->strictly_dominated_by_pass;
}

bool pd0_is_retained(
    const old_school::ValuePassDominanceDiagnostic& diagnostic,
    const std::function<bool(
        const old_school::PriorityAction&)>& predicate) {
    const auto found = std::find_if(
        diagnostic.actions.begin(), diagnostic.actions.end(),
        [&](const auto& action) {
            return predicate(action.action);
        });
    return found != diagnostic.actions.end() &&
           found->comparison_settled &&
           !found->strictly_dominated_by_pass;
}

std::uint64_t pd0_diagnostic_hash(
    const old_school::LearnedValuePriorityDiagnostic& diagnostic) {
    constexpr std::uint64_t kOffset = 1469598103934665603ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t hash = kOffset;
    const auto add = [&](std::uint64_t value) {
        for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
            hash ^= static_cast<std::uint8_t>(
                value >> (8U * byte));
            hash *= kPrime;
        }
    };
    const auto add_action =
        [&](const old_school::PriorityAction& action) {
            add(static_cast<std::uint64_t>(action.kind));
            add(static_cast<std::uint64_t>(action.card));
            add(static_cast<std::uint64_t>(action.x_value));
            add(action.target.has_value());
            if (action.target.has_value()) {
                add(action.target->player);
                add(action.target->creature.has_value());
                if (action.target->creature.has_value()) {
                    add(*action.target->creature);
                }
            }
            add(action.spell_target.has_value());
            if (action.spell_target.has_value()) {
                add(*action.spell_target);
            }
            add(action.source_permanent.has_value());
            if (action.source_permanent.has_value()) {
                add(*action.source_permanent);
            }
        };
    add(diagnostic.legal_actions.size());
    for (const auto& action : diagnostic.legal_actions) {
        add_action(action);
    }
    add(diagnostic.actions.size());
    for (const auto& action : diagnostic.actions) {
        add_action(action);
    }
    for (const double score : diagnostic.scores) {
        add(std::bit_cast<std::uint64_t>(score));
    }
    add_action(diagnostic.selected_action);
    return hash;
}

bool run_pd0_exact_controls() {
    bool passed = true;
    const auto print_gate =
        [&](std::string_view name, bool gate) {
            std::cout << "  " << name << ": "
                      << (gate ? "PASS" : "FAIL") << '\n';
            passed = passed && gate;
        };

    const Pd0Fixture braingeyser = pd0_braingeyser_fixture();
    const auto braingeyser_result =
        old_school::diagnose_value_pass_dominance(
            braingeyser.state, braingeyser.player,
            braingeyser.sorcery_actions, braingeyser.phase,
            braingeyser.consecutive_passes);
    const auto braingeyser_hidden =
        old_school::diagnose_value_pass_dominance(
            pd0_hidden_repartition(
                braingeyser.state, braingeyser.player),
            braingeyser.player, braingeyser.sorcery_actions,
            braingeyser.phase, braingeyser.consecutive_passes);
    std::size_t dominated_x_zero = 0;
    bool productive_x_one = false;
    for (const auto& action : braingeyser_result.actions) {
        if (action.action.kind !=
            old_school::PriorityActionKind::CastBraingeyser) {
            continue;
        }
        if (action.action.x_value == 0 &&
            action.strictly_dominated_by_pass) {
            ++dominated_x_zero;
        }
        if (action.action.x_value == 1 &&
            action.action.target.has_value() &&
            action.action.target->player ==
                braingeyser.player &&
            !action.strictly_dominated_by_pass) {
            productive_x_one = true;
        }
    }
    print_gate(
        "Braingeyser both X=0 branches dominated",
        dominated_x_zero == 2);
    print_gate(
        "Braingeyser productive X=1 retained",
        productive_x_one);
    print_gate(
        "Braingeyser hidden repartition exact",
        braingeyser_result == braingeyser_hidden);

    old_school::GameState disintegrate;
    disintegrate.active_player = 0;
    disintegrate.turn_number = 8;
    disintegrate.players[0].hand = {
        old_school::CardId::Disintegrate,
    };
    disintegrate.players[0].lands = {
        {.card = old_school::CardId::Mountain, .tapped = false},
        {.card = old_school::CardId::Mountain, .tapped = false},
    };
    const auto disintegrate_result =
        old_school::diagnose_value_pass_dominance(
            disintegrate, 0, true,
            old_school::TurnPhase::SecondMain, 0);
    print_gate(
        "Disintegrate X=0 dominated",
        pd0_is_dominated(
            disintegrate_result,
            [](const old_school::PriorityAction& action) {
                return action.kind ==
                           old_school::PriorityActionKind::
                               CastDisintegrate &&
                       action.x_value == 0;
            }));
    print_gate(
        "Disintegrate productive X retained",
        pd0_is_retained(
            disintegrate_result,
            [](const old_school::PriorityAction& action) {
                return action.kind ==
                           old_school::PriorityActionKind::
                               CastDisintegrate &&
                       action.x_value == 1;
            }));

    const auto force_spike_state = [](bool payable) {
        old_school::GameState state;
        state.active_player = 1;
        state.turn_number = 6;
        state.next_permanent_id = 2;
        state.next_stack_object_id = 2;
        state.players[0].hand = {
            old_school::CardId::ForceSpike,
        };
        state.players[0].lands = {
            {.card = old_school::CardId::Island,
             .tapped = false},
        };
        if (payable) {
            state.players[1].lands.push_back(
                {.card = old_school::CardId::Mountain,
                 .tapped = false});
        }
        state.stack = {
            {
                .kind = old_school::StackObjectKind::Spell,
                .id = 1,
                .card = old_school::CardId::GrayOgre,
                .controller = 1,
            },
        };
        return state;
    };
    for (const bool payable : {false, true}) {
        const auto result =
            old_school::diagnose_value_pass_dominance(
                force_spike_state(payable), 0, false,
                old_school::TurnPhase::FirstMain, 0);
        print_gate(
            payable ? "payable Force Spike retained"
                    : "live Force Spike retained",
            pd0_is_retained(
                result,
                [](const old_school::PriorityAction& action) {
                    return action.kind ==
                           old_school::PriorityActionKind::
                               CastForceSpike;
                }));
    }

    old_school::GameState own_spell;
    own_spell.active_player = 0;
    own_spell.turn_number = 5;
    own_spell.next_stack_object_id = 2;
    own_spell.players[0].hand = {
        old_school::CardId::Counterspell,
    };
    own_spell.players[0].lands = {
        {.card = old_school::CardId::Island, .tapped = false},
        {.card = old_school::CardId::Island, .tapped = false},
    };
    own_spell.stack = {
        {
            .kind = old_school::StackObjectKind::Spell,
            .id = 1,
            .card = old_school::CardId::FlyingMen,
            .controller = 0,
        },
    };
    const auto own_counter =
        old_school::diagnose_value_pass_dominance(
            own_spell, 0, false,
            old_school::TurnPhase::FirstMain, 0);
    print_gate(
        "own useful-spell Counterspell retained",
        pd0_is_retained(
            own_counter,
            [](const old_school::PriorityAction& action) {
                return action.kind ==
                       old_school::PriorityActionKind::
                           CastCounterspell;
            }));

    old_school::GameState redundant = own_spell;
    redundant.active_player = 1;
    redundant.next_stack_object_id = 4;
    redundant.stack = {
        {
            .kind = old_school::StackObjectKind::Spell,
            .id = 1,
            .card = old_school::CardId::AirElemental,
            .controller = 1,
        },
        {
            .kind = old_school::StackObjectKind::Spell,
            .id = 2,
            .card = old_school::CardId::Counterspell,
            .controller = 0,
            .spell_target = 1,
        },
    };
    const auto redundant_result =
        old_school::diagnose_value_pass_dominance(
            redundant, 0, false,
            old_school::TurnPhase::FirstMain, 0);
    print_gate(
        "redundant same-target Counterspell dominated",
        pd0_is_dominated(
            redundant_result,
            [](const old_school::PriorityAction& action) {
                return action.kind ==
                           old_school::PriorityActionKind::
                               CastCounterspell &&
                       action.spell_target == 1;
            }));
    print_gate(
        "materially distinct Counterspell retained",
        pd0_is_retained(
            redundant_result,
            [](const old_school::PriorityAction& action) {
                return action.kind ==
                           old_school::PriorityActionKind::
                               CastCounterspell &&
                       action.spell_target == 2;
            }));

    old_school::GameState counter_war = redundant;
    counter_war.next_stack_object_id = 5;
    counter_war.stack.push_back({
        .kind = old_school::StackObjectKind::Spell,
        .id = 3,
        .card = old_school::CardId::Counterspell,
        .controller = 1,
        .spell_target = 2,
    });
    const auto counter_war_result =
        old_school::diagnose_value_pass_dominance(
            counter_war, 0, false,
            old_school::TurnPhase::FirstMain, 0);
    print_gate(
        "same-target counter with intervening response retained",
        pd0_is_retained(
            counter_war_result,
            [](const old_school::PriorityAction& action) {
                return action.kind ==
                           old_school::PriorityActionKind::
                               CastCounterspell &&
                       action.spell_target == 1;
            }));
    return passed;
}

bool print_pd0_model_row(
    std::string_view name, const Pd0Fixture& fixture,
    std::shared_ptr<const old_school::LearnedModel> model,
    std::size_t worlds, std::uint64_t seed) {
    const auto control =
        old_school::diagnose_learned_value_priority(
            fixture.state, fixture.decks, fixture.player,
            fixture.sorcery_actions, fixture.phase,
            fixture.consecutive_passes, model, worlds, seed);
    const auto explicit_control =
        old_school::diagnose_learned_value_priority(
            fixture.state, fixture.decks, fixture.player,
            fixture.sorcery_actions, fixture.phase,
            fixture.consecutive_passes, model, worlds, seed,
            0.0, 0.0, false);
    const auto treatment =
        old_school::diagnose_learned_value_priority(
            fixture.state, fixture.decks, fixture.player,
            fixture.sorcery_actions, fixture.phase,
            fixture.consecutive_passes, model, worlds, seed,
            0.0, 0.0, true);
    const auto hidden_treatment =
        old_school::diagnose_learned_value_priority(
            pd0_hidden_repartition(
                fixture.state, fixture.player),
            fixture.decks, fixture.player,
            fixture.sorcery_actions, fixture.phase,
            fixture.consecutive_passes, model, worlds, seed,
            0.0, 0.0, true);

    const bool default_off_identity =
        control == explicit_control;
    const bool legal_actions_unchanged =
        control.legal_actions == treatment.legal_actions;
    const bool exact_filter =
        treatment.pass_dominated_actions.size() == 2 &&
        treatment.actions.size() + 2 ==
            treatment.legal_actions.size();
    const bool selected_retained =
        std::find(
            treatment.pass_dominated_actions.begin(),
            treatment.pass_dominated_actions.end(),
            treatment.selected_action) ==
        treatment.pass_dominated_actions.end();
    const bool hidden_exact =
        treatment == hidden_treatment;

    std::cout << "\n" << name << " K=" << worlds << '\n'
              << "  before:";
    for (const auto& action : control.legal_actions) {
        std::cout << " [" << pd0_action_name(action) << ']';
    }
    std::cout << "\n  filtered:";
    for (const auto& action :
         treatment.pass_dominated_actions) {
        std::cout << " [" << pd0_action_name(action) << ']';
    }
    std::cout << "\n  after:";
    for (const auto& action : treatment.actions) {
        std::cout << " [" << pd0_action_name(action) << ']';
    }
    std::cout << "\n  control selected: "
              << pd0_action_name(control.selected_action)
              << "\n  treatment selected: "
              << pd0_action_name(treatment.selected_action)
              << "\n  control hash: 0x" << std::hex
              << pd0_diagnostic_hash(control)
              << "\n  treatment hash: 0x"
              << pd0_diagnostic_hash(treatment)
              << std::dec
              << "\n  default-off identity: "
              << (default_off_identity ? "PASS" : "FAIL")
              << "\n  legal actions unchanged: "
              << (legal_actions_unchanged ? "PASS" : "FAIL")
              << "\n  exact filter: "
              << (exact_filter ? "PASS" : "FAIL")
              << "\n  selected action retained: "
              << (selected_retained ? "PASS" : "FAIL")
              << "\n  hidden repartition exact: "
              << (hidden_exact ? "PASS" : "FAIL") << '\n';
    return default_off_identity && legal_actions_unchanged &&
           exact_filter && selected_retained && hidden_exact;
}

int run_pd0_diagnostic(std::uint64_t seed) {
    constexpr std::size_t kTrainingGames = 800;
    constexpr std::uint64_t kTrainingSeed = 424242;
    constexpr std::size_t kGenerations = 16;
    constexpr std::string_view kC16Fingerprint =
        "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f";

    std::cout
        << "PD0 exact Pass-dominance diagnostic\n"
        << "Diagnostic seed: " << seed
        << "\nG0: T800/S424242, K=2"
        << "\nC16: load-only T800/S424242/C16, K=8"
        << "\nTreatment: Learned Value roots and K=0 "
           "Value-mirror continuations only\n\n"
        << "Exact comparator controls\n";
    const bool controls = run_pd0_exact_controls();

    const auto g0 =
        train_value_g0_with_progress(
            kTrainingGames, kTrainingSeed);
    const auto c16 =
        load_value_challenger_with_progress(
            kTrainingGames, kTrainingSeed, kGenerations);
    const std::string c16_fingerprint =
        old_school::learned_model_fingerprint(c16);
    const bool c16_identity =
        c16_fingerprint == kC16Fingerprint;
    std::cout << "  Exact frozen C16 fingerprint: "
              << (c16_identity ? "PASS" : "FAIL")
              << " (" << c16_fingerprint << ")\n";
    if (!c16_identity) {
        std::cout
            << "\nPD0 mechanism verdict: INFRASTRUCTURE "
               "(frozen C16 identity mismatch)\n";
        return 2;
    }

    const Pd0Fixture fixture = pd0_braingeyser_fixture();
    const bool g0_gate = print_pd0_model_row(
        "Legacy Value G0", fixture, g0, 2, seed);
    const bool c16_gate = print_pd0_model_row(
        "Frozen Value C16", fixture, c16, 8, seed);
    const bool passed =
        controls && c16_identity && g0_gate && c16_gate;
    std::cout << "\nPD0 mechanism verdict: "
              << (passed ? "PASS" : "REJECT") << '\n';
    return passed ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    try {
        constexpr std::string_view calendar_turn_audit_option =
            "--audit-calendar-turn-targets";
        constexpr std::string_view calendar_eight_audit_option =
            "--audit-calendar-eight-targets";
        constexpr std::string_view replay_weight_audit_option =
            "--audit-replay-weights";
        constexpr std::string_view pd0_diagnostic_option =
            "--diagnose-value-pass-dominance";
        constexpr std::string_view pd0_diagnostic_seed_text =
            "202607260947";
        constexpr std::string_view pd0_smoke_seed_text =
            "202607260948";
        bool calendar_turn_audit_requested = false;
        bool calendar_eight_audit_requested = false;
        bool replay_weight_audit_requested = false;
        bool pd0_diagnostic_requested = false;
        bool pd0_diagnostic_seed_requested = false;
        bool pd0_smoke_seed_requested = false;
        for (int argument = 1; argument < argc; ++argument) {
            const std::string_view raw_argument = argv[argument];
            calendar_turn_audit_requested =
                calendar_turn_audit_requested ||
                raw_argument == calendar_turn_audit_option;
            calendar_eight_audit_requested =
                calendar_eight_audit_requested ||
                raw_argument == calendar_eight_audit_option;
            replay_weight_audit_requested =
                replay_weight_audit_requested ||
                raw_argument == replay_weight_audit_option;
            pd0_diagnostic_requested =
                pd0_diagnostic_requested ||
                raw_argument == pd0_diagnostic_option;
            if ((raw_argument == "--seed" ||
                 raw_argument == "--train-seed") &&
                argument + 1 < argc) {
                const std::string_view raw_seed =
                    argv[argument + 1];
                pd0_diagnostic_seed_requested =
                    pd0_diagnostic_seed_requested ||
                    raw_seed == pd0_diagnostic_seed_text;
                pd0_smoke_seed_requested =
                    pd0_smoke_seed_requested ||
                    raw_seed == pd0_smoke_seed_text;
            }
        }
        if (calendar_turn_audit_requested &&
            (argc != 2 ||
             std::string_view(argv[1]) !=
                 calendar_turn_audit_option)) {
            throw std::invalid_argument(
                "--audit-calendar-turn-targets is exclusive and "
                "accepts no other options");
        }
        if (calendar_eight_audit_requested &&
            (argc != 2 ||
             std::string_view(argv[1]) !=
                 calendar_eight_audit_option)) {
            throw std::invalid_argument(
                "--audit-calendar-eight-targets is exclusive and "
                "accepts no other options");
        }
        if (replay_weight_audit_requested &&
            (argc != 2 ||
             std::string_view(argv[1]) !=
                 replay_weight_audit_option)) {
            throw std::invalid_argument(
                "--audit-replay-weights is exclusive and accepts no "
                "other options");
        }

        std::size_t games = 100;
        std::uint64_t seed = random_seed();
        bool seed_option_used = false;
        old_school::BotField bot_field = old_school::BotField::Mixed;
        old_school::LearnedVariant bot_field_learned_variant =
            old_school::LearnedVariant::ValueSearchChampion;
        std::size_t rollouts = 2;
        std::size_t deep_rollouts = 8;
        std::size_t learned_rollouts = 2;
        std::size_t learned_generations = 0;
        double value_continuation_epsilon = 0.0;
        std::size_t training_games = 800;
        std::uint64_t training_seed =
            old_school::kDefaultLearnedTrainingSeed;
        bool training_seed_option_used = false;
        bool games_were_set = false;
        bool interactive = false;
        bool interactive_unsupported_option_used = false;
        bool benchmark = false;
        bool stability = false;
        bool evolve = false;
        bool diagnose_white_plan = false;
        bool diagnose_value_context = false;
        bool diagnose_force_spike_teacher = false;
        bool teacher_audit_unsupported_option_used = false;
        bool diagnose_value_pass_dominance = false;
        bool pd0_diagnostic_unsupported_option_used = false;
        bool pd0_smoke_unsupported_option_used = false;
        bool value_pass_dominance = false;
        bool train_p_family = false;
        std::size_t p_family_generations = 0;
        bool p_family_unsupported_option_used = false;
        bool diagnose_p1_fit = false;
        bool p1_fit_unsupported_option_used = false;
        bool score_p1r_probes = false;
        bool p1r_probe_unsupported_option_used = false;
        bool diagnose_terminal_credit = false;
        bool terminal_credit_unsupported_option_used = false;
        bool train_terminal_weight_c17 = false;
        bool terminal_weight_unsupported_option_used = false;
        bool evaluate_terminal_weight_c17 = false;
        bool audit_dc1_dominance = false;
        bool dc1_unsupported_option_used = false;
        bool audit_dc1_action_census = false;
        bool dc1_census_unsupported_option_used = false;
        bool audit_v3_blue_stack_regret = false;
        bool bsr0_unsupported_option_used = false;
        bool audit_calendar_turn_targets = false;
        bool audit_calendar_eight_targets = false;
        bool audit_replay_weights = false;
        bool variance_study = false;
        bool score_probes = false;
        bool refresh_probe_cache = false;
        bool refresh_value_challenger_cache = false;
        bool refresh_value_g8_cache = false;
        bool refresh_value_mix50_cache = false;
        bool probe_option_used = false;
        bool actor_policy_option_used = false;
        bool learned_rollouts_option_used = false;
        bool learned_generations_option_used = false;
        bool value_continuation_epsilon_option_used = false;
        bool challenger_option_used = false;
        bool baseline_option_used = false;
        bool evolve_pilot_option_used = false;
        old_school::probe_runner::ProbeCorpusKind probe_corpus =
            old_school::probe_runner::ProbeCorpusKind::DevV3;
        bool probe_cache_was_set = false;
        std::size_t probe_worlds = 128;
        std::size_t probe_horizon = 12;
        std::size_t actor_generation = 0;
        std::size_t value_generation = 0;
        old_school::LearnedValueG8Recipe value_recipe =
            old_school::LearnedValueG8Recipe::CanonicalAllSearchLate;
        old_school::LearnedActorGenerationConfig
            actor_generation_config;
        std::string probe_cache =
            "data/old-school-probe-dev-v3-env-v3.labels.tsv";
        std::size_t stability_runs = 8;
        std::size_t generations = 10;
        std::size_t population = 16;
        BotSelection challenger = {
            .kind = old_school::BotKind::Handcrafted,
        };
        BotSelection baseline = {
            .kind = old_school::BotKind::MonteCarlo,
        };
        BotSelection evolve_pilot = {
            .kind = old_school::BotKind::Handcrafted,
        };

        for (int argument = 1; argument < argc; ++argument) {
            const std::string_view option = argv[argument];
            if (option == "--help" || option == "-h") {
                if (pd0_diagnostic_requested) {
                    throw std::invalid_argument(
                        "--diagnose-value-pass-dominance accepts "
                        "only --seed " +
                        std::string(pd0_diagnostic_seed_text));
                }
                if (pd0_diagnostic_seed_requested) {
                    throw std::invalid_argument(
                        "reserved PD0 diagnostic seed " +
                        std::string(pd0_diagnostic_seed_text) +
                        " may be used only by "
                        "--diagnose-value-pass-dominance");
                }
                if (pd0_smoke_seed_requested) {
                    throw std::invalid_argument(
                        "reserved PD0 smoke seed " +
                        std::string(pd0_smoke_seed_text) +
                        " may be used only by the exact 240-game "
                        "C16/K8-vs-C16/K8 paired control or "
                        "challenger-only treatment");
                }
                print_help(argv[0]);
                return 0;
            }
            if (option != "--train-p-family" &&
                option != "--seed" &&
                option != "--train-games" &&
                option != "--train-seed") {
                p_family_unsupported_option_used = true;
            }
            if (option != "--diagnose-p1-fit" &&
                option != "--seed" &&
                option != "--train-games" &&
                option != "--train-seed") {
                p1_fit_unsupported_option_used = true;
            }
            if (option != "--score-p1r-probes" &&
                option != "--seed" &&
                option != "--train-games" &&
                option != "--train-seed") {
                p1r_probe_unsupported_option_used = true;
            }
            if (option != "--diagnose-terminal-credit" &&
                option != "--train-games" &&
                option != "--train-seed") {
                terminal_credit_unsupported_option_used = true;
            }
            if (option != "--train-terminal-weight-c17" &&
                option != "--train-games" &&
                option != "--train-seed") {
                terminal_weight_unsupported_option_used = true;
            }
            if (option != "--audit-dc1-dominance" &&
                option != "--train-games" &&
                option != "--train-seed" &&
                option != "--learned-generations") {
                dc1_unsupported_option_used = true;
            }
            if (option != "--audit-dc1-action-census" &&
                option != "--train-games" &&
                option != "--train-seed" &&
                option != "--learned-generations") {
                dc1_census_unsupported_option_used = true;
            }
            if (option != "--audit-v3-blue-stack-regret" &&
                option != "--train-games" &&
                option != "--train-seed" &&
                option != "--learned-generations") {
                bsr0_unsupported_option_used = true;
            }
            if (option != "--diagnose-force-spike-teacher" &&
                option != "--train-games" &&
                option != "--train-seed" &&
                option != "--learned-generations") {
                teacher_audit_unsupported_option_used = true;
            }
            if (option != "--diagnose-value-pass-dominance" &&
                option != "--seed") {
                pd0_diagnostic_unsupported_option_used = true;
            }
            if (option != "--benchmark" &&
                option != "--games" &&
                option != "--seed" &&
                option != "--train-seed" &&
                option != "--train-games" &&
                option != "--challenger" &&
                option != "--baseline" &&
                option != "--learned-rollouts" &&
                option != "--value-pass-dominance") {
                pd0_smoke_unsupported_option_used = true;
            }
            if (option == "--benchmark") {
                benchmark = true;
                continue;
            }
            if (option == "--interactive") {
                interactive = true;
                continue;
            }
            if (option == "--stability") {
                stability = true;
                continue;
            }
            if (option == "--evolve-deck") {
                evolve = true;
                continue;
            }
            if (option == "--diagnose-white-plan") {
                diagnose_white_plan = true;
                continue;
            }
            if (option == "--diagnose-value-context") {
                diagnose_value_context = true;
                continue;
            }
            if (option == "--diagnose-force-spike-teacher") {
                diagnose_force_spike_teacher = true;
                continue;
            }
            if (option == "--diagnose-value-pass-dominance") {
                diagnose_value_pass_dominance = true;
                continue;
            }
            if (option == "--diagnose-p1-fit") {
                diagnose_p1_fit = true;
                continue;
            }
            if (option == "--score-p1r-probes") {
                score_p1r_probes = true;
                continue;
            }
            if (option == "--diagnose-terminal-credit") {
                diagnose_terminal_credit = true;
                continue;
            }
            if (option == "--train-terminal-weight-c17") {
                train_terminal_weight_c17 = true;
                continue;
            }
            if (option == "--evaluate-terminal-weight-c17") {
                evaluate_terminal_weight_c17 = true;
                continue;
            }
            if (option == "--audit-dc1-dominance") {
                audit_dc1_dominance = true;
                continue;
            }
            if (option == "--audit-dc1-action-census") {
                audit_dc1_action_census = true;
                continue;
            }
            if (option == "--audit-v3-blue-stack-regret") {
                audit_v3_blue_stack_regret = true;
                continue;
            }
            if (option == "--audit-calendar-turn-targets") {
                audit_calendar_turn_targets = true;
                continue;
            }
            if (option == "--audit-calendar-eight-targets") {
                audit_calendar_eight_targets = true;
                continue;
            }
            if (option == "--audit-replay-weights") {
                audit_replay_weights = true;
                continue;
            }
            if (option == "--variance-study") {
                variance_study = true;
                continue;
            }
            if (option == "--score-probes") {
                score_probes = true;
                continue;
            }
            if (option == "--refresh-probe-cache") {
                refresh_probe_cache = true;
                probe_option_used = true;
                continue;
            }
            if (option ==
                "--refresh-value-challenger-cache") {
                refresh_value_challenger_cache = true;
                continue;
            }
            if (option == "--refresh-value-g8-cache") {
                refresh_value_g8_cache = true;
                continue;
            }
            if (option == "--refresh-value-mix50-cache") {
                refresh_value_mix50_cache = true;
                continue;
            }
            if (option == "--value-pass-dominance") {
                value_pass_dominance = true;
                continue;
            }
            if (option != "--games" && option != "--seed" &&
                option != "--train-seed" &&
                option != "--bots" && option != "--rollouts" &&
                option != "--deep-rollouts" &&
                option != "--learned-rollouts" &&
                option != "--learned-generations" &&
                option != "--value-continuation-epsilon" &&
                option != "--train-games" &&
                option != "--stability-runs" &&
                option != "--generations" &&
                option != "--population" &&
                option != "--evolve-pilot" &&
                option != "--challenger" &&
                option != "--baseline" &&
                option != "--probe-corpus" &&
                option != "--probe-worlds" &&
                option != "--probe-horizon" &&
                option != "--actor-generation" &&
                option != "--value-generation" &&
                option != "--value-recipe" &&
                option != "--actor-policy-epochs" &&
                option != "--actor-policy-rate" &&
                option != "--train-p-family" &&
                option != "--probe-cache") {
                throw std::invalid_argument("unknown option: " +
                                            std::string(option));
            }
            if (++argument >= argc) {
                throw std::invalid_argument("missing value for " +
                                            std::string(option));
            }
            if (option != "--seed" &&
                option != "--train-seed" &&
                option != "--train-games" &&
                option != "--learned-rollouts" &&
                option != "--learned-generations" &&
                option != "--value-continuation-epsilon") {
                interactive_unsupported_option_used = true;
            }
            if (option == "--bots") {
                const auto selection =
                    parse_bot_field(argv[argument]);
                bot_field = selection.field;
                bot_field_learned_variant =
                    selection.learned_variant;
                continue;
            }
            if (option == "--challenger" ||
                option == "--baseline") {
                const auto selection =
                    parse_bot(argv[argument]);
                if (option == "--challenger") {
                    challenger = selection;
                    challenger_option_used = true;
                } else {
                    baseline = selection;
                    baseline_option_used = true;
                }
                continue;
            }
            if (option == "--evolve-pilot") {
                evolve_pilot = parse_bot(argv[argument]);
                evolve_pilot_option_used = true;
                continue;
            }
            if (option == "--probe-cache") {
                probe_cache = argv[argument];
                if (probe_cache.empty()) {
                    throw std::invalid_argument(
                        "--probe-cache must not be empty");
                }
                probe_cache_was_set = true;
                probe_option_used = true;
                continue;
            }
            if (option == "--probe-corpus") {
                const std::string_view corpus = argv[argument];
                if (corpus == "dev-v3") {
                    probe_corpus =
                        old_school::probe_runner::
                            ProbeCorpusKind::DevV3;
                } else if (corpus == "validation-v1") {
                    probe_corpus =
                        old_school::probe_runner::
                            ProbeCorpusKind::ValidationV1;
                } else {
                    throw std::invalid_argument(
                        "--probe-corpus must be dev-v3 or "
                        "validation-v1");
                }
                probe_option_used = true;
                continue;
            }
            if (option == "--value-recipe") {
                const std::string_view recipe = argv[argument];
                if (recipe == "canonical") {
                    value_recipe =
                        old_school::LearnedValueG8Recipe::
                            CanonicalAllSearchLate;
                } else if (recipe == "mix50") {
                    value_recipe =
                        old_school::LearnedValueG8Recipe::LateMix50;
                } else {
                    throw std::invalid_argument(
                        "--value-recipe must be canonical or mix50");
                }
                probe_option_used = true;
                continue;
            }
            if (option == "--actor-policy-rate") {
                actor_generation_config.policy_learning_rate =
                    parse_positive_real(argv[argument], option);
                actor_policy_option_used = true;
                continue;
            }
            if (option == "--value-continuation-epsilon") {
                value_continuation_epsilon =
                    parse_unit_interval_real(
                        argv[argument], option);
                value_continuation_epsilon_option_used = true;
                continue;
            }

            const std::uint64_t value = parse_number(argv[argument], option);
            if (option == "--games") {
                if (value == 0) {
                    throw std::invalid_argument(
                        "--games must be greater than zero");
                }
                games = static_cast<std::size_t>(value);
                games_were_set = true;
            } else if (option == "--seed") {
                seed = value;
                seed_option_used = true;
            } else if (option == "--train-seed") {
                training_seed = value;
                training_seed_option_used = true;
            } else if (option == "--rollouts") {
                if (value == 0) {
                    throw std::invalid_argument(
                        "--rollouts must be greater than zero");
                }
                rollouts = static_cast<std::size_t>(value);
            } else if (option == "--deep-rollouts") {
                if (value == 0) {
                    throw std::invalid_argument(
                        "--deep-rollouts must be greater than zero");
                }
                deep_rollouts = static_cast<std::size_t>(value);
            } else if (option == "--learned-rollouts") {
                if (value == 0) {
                    throw std::invalid_argument(
                        "--learned-rollouts must be greater than zero");
                }
                learned_rollouts =
                    static_cast<std::size_t>(value);
                learned_rollouts_option_used = true;
            } else if (option == "--learned-generations") {
                learned_generations =
                    static_cast<std::size_t>(value);
                learned_generations_option_used = true;
            } else if (option == "--stability-runs") {
                if (value == 0) {
                    throw std::invalid_argument(
                        "--stability-runs must be greater than zero");
                }
                stability_runs = static_cast<std::size_t>(value);
            } else if (option == "--generations") {
                if (value == 0) {
                    throw std::invalid_argument(
                        "--generations must be greater than zero");
                }
                generations = static_cast<std::size_t>(value);
            } else if (option == "--population") {
                if (value < static_cast<long long>(
                                old_school::kDeckCount)) {
                    throw std::invalid_argument(
                        "--population must be at least five");
                }
                population = static_cast<std::size_t>(value);
            } else if (option == "--probe-worlds") {
                if (value < 2) {
                    throw std::invalid_argument(
                        "--probe-worlds must be at least two");
                }
                probe_worlds = static_cast<std::size_t>(value);
                probe_option_used = true;
            } else if (option == "--probe-horizon") {
                probe_horizon = static_cast<std::size_t>(value);
                probe_option_used = true;
            } else if (option == "--actor-generation") {
                if (value > 1) {
                    throw std::invalid_argument(
                        "--actor-generation must be zero or one");
                }
                actor_generation =
                    static_cast<std::size_t>(value);
                probe_option_used = true;
            } else if (option == "--value-generation") {
                if (value != 0 && value != 8) {
                    throw std::invalid_argument(
                        "--value-generation must be zero or eight");
                }
                value_generation =
                    static_cast<std::size_t>(value);
                probe_option_used = true;
            } else if (option == "--actor-policy-epochs") {
                if (value == 0) {
                    throw std::invalid_argument(
                        "--actor-policy-epochs must be greater "
                        "than zero");
                }
                actor_generation_config.policy_epochs =
                    static_cast<std::size_t>(value);
                actor_policy_option_used = true;
            } else if (option == "--train-p-family") {
                if (value == 0 || value > 16) {
                    throw std::invalid_argument(
                        "--train-p-family must be in [1, 16]");
                }
                train_p_family = true;
                p_family_generations =
                    static_cast<std::size_t>(value);
            } else {
                if (value == 0) {
                    throw std::invalid_argument(
                        "--train-games must be greater than zero");
                }
                training_games = static_cast<std::size_t>(value);
            }
        }

        if (!probe_cache_was_set) {
            probe_cache =
                old_school::probe_runner::default_probe_cache_path(
                    probe_corpus)
                    .string();
        }

        if (static_cast<int>(interactive) +
                static_cast<int>(benchmark) +
                static_cast<int>(stability) +
                static_cast<int>(evolve) +
                static_cast<int>(diagnose_white_plan) +
                static_cast<int>(diagnose_value_context) +
                static_cast<int>(diagnose_force_spike_teacher) +
                static_cast<int>(diagnose_value_pass_dominance) +
                static_cast<int>(train_p_family) +
                static_cast<int>(diagnose_p1_fit) +
                static_cast<int>(score_p1r_probes) +
                static_cast<int>(diagnose_terminal_credit) +
                static_cast<int>(train_terminal_weight_c17) +
                static_cast<int>(evaluate_terminal_weight_c17) +
                static_cast<int>(audit_dc1_dominance) +
                static_cast<int>(audit_dc1_action_census) +
                static_cast<int>(audit_v3_blue_stack_regret) +
                static_cast<int>(audit_calendar_turn_targets) +
                static_cast<int>(audit_calendar_eight_targets) +
                static_cast<int>(audit_replay_weights) +
                static_cast<int>(variance_study) +
                static_cast<int>(score_probes) >
            1) {
            throw std::invalid_argument(
                "--interactive, --benchmark, --stability, "
                "--evolve-deck, and "
                "--diagnose-white-plan, --diagnose-value-context, "
                "--diagnose-force-spike-teacher, "
                "--diagnose-value-pass-dominance, --train-p-family, "
                "--diagnose-p1-fit, --score-p1r-probes, "
                "--diagnose-terminal-credit, "
                "--train-terminal-weight-c17, "
                "--evaluate-terminal-weight-c17, "
                "--audit-dc1-dominance, "
                "--audit-dc1-action-census, "
                "--audit-v3-blue-stack-regret, "
                "--audit-calendar-turn-targets, "
                "--audit-calendar-eight-targets, "
                "--audit-replay-weights, "
                "--variance-study, and "
                "--score-probes cannot be "
                "combined");
        }
        if (diagnose_value_context && argc != 2) {
            throw std::invalid_argument(
                "--diagnose-value-context accepts no other options");
        }
        if (diagnose_force_spike_teacher &&
            teacher_audit_unsupported_option_used) {
            throw std::invalid_argument(
                "--diagnose-force-spike-teacher accepts only "
                "--train-games, --train-seed, and "
                "--learned-generations");
        }
        if (diagnose_force_spike_teacher &&
            (!learned_generations_option_used ||
             learned_generations == 0)) {
            throw std::invalid_argument(
                "--diagnose-force-spike-teacher requires a positive "
                "--learned-generations N");
        }
        constexpr std::uint64_t pd0_diagnostic_seed =
            202607260947ULL;
        constexpr std::uint64_t pd0_smoke_seed =
            202607260948ULL;
        if (diagnose_value_pass_dominance &&
            (pd0_diagnostic_unsupported_option_used ||
             !seed_option_used ||
             seed != pd0_diagnostic_seed)) {
            throw std::invalid_argument(
                "--diagnose-value-pass-dominance accepts only "
                "--seed 202607260947");
        }
        if (!diagnose_value_pass_dominance &&
            ((seed_option_used &&
              seed == pd0_diagnostic_seed) ||
             (training_seed_option_used &&
              training_seed == pd0_diagnostic_seed))) {
            throw std::invalid_argument(
                "reserved PD0 diagnostic seed 202607260947 may be "
                "used only by --diagnose-value-pass-dominance");
        }
        if (train_p_family &&
            p_family_unsupported_option_used) {
            throw std::invalid_argument(
                "--train-p-family accepts only --seed, "
                "--train-games, and --train-seed");
        }
        if (diagnose_p1_fit &&
            p1_fit_unsupported_option_used) {
            throw std::invalid_argument(
                "--diagnose-p1-fit accepts only --seed, "
                "--train-games, and --train-seed");
        }
        if (score_p1r_probes &&
            p1r_probe_unsupported_option_used) {
            throw std::invalid_argument(
                "--score-p1r-probes accepts only --seed, "
                "--train-games, and --train-seed");
        }
        if (diagnose_terminal_credit &&
            terminal_credit_unsupported_option_used) {
            throw std::invalid_argument(
                "--diagnose-terminal-credit accepts only "
                "--train-games and --train-seed");
        }
        if (diagnose_terminal_credit &&
            (training_games != 800 ||
             training_seed != 424242)) {
            throw std::invalid_argument(
                "--diagnose-terminal-credit requires exact "
                "--train-games 800 --train-seed 424242");
        }
        if (train_terminal_weight_c17 &&
            terminal_weight_unsupported_option_used) {
            throw std::invalid_argument(
                "--train-terminal-weight-c17 accepts only "
                "--train-games and --train-seed");
        }
        if (train_terminal_weight_c17 &&
            (training_games != 800 ||
             training_seed != 424242)) {
            throw std::invalid_argument(
                "--train-terminal-weight-c17 requires exact "
                "--train-games 800 --train-seed 424242");
        }
        if (evaluate_terminal_weight_c17 && argc != 2) {
            throw std::invalid_argument(
                "--evaluate-terminal-weight-c17 is exclusive and "
                "accepts no other options");
        }
        if (audit_calendar_turn_targets && argc != 2) {
            throw std::invalid_argument(
                "--audit-calendar-turn-targets is exclusive and "
                "accepts no other options");
        }
        if (audit_calendar_eight_targets && argc != 2) {
            throw std::invalid_argument(
                "--audit-calendar-eight-targets is exclusive and "
                "accepts no other options");
        }
        if (audit_replay_weights && argc != 2) {
            throw std::invalid_argument(
                "--audit-replay-weights is exclusive and accepts no "
                "other options");
        }
        constexpr std::uint64_t calendar_turn_audit_seed =
            202607260501ULL;
        if (!audit_calendar_turn_targets &&
            ((seed_option_used &&
              seed == calendar_turn_audit_seed) ||
             (training_seed_option_used &&
              training_seed == calendar_turn_audit_seed))) {
            throw std::invalid_argument(
                "reserved TA4-0 audit seed 202607260501 may be used "
                "only by --audit-calendar-turn-targets");
        }
        constexpr std::uint64_t calendar_eight_audit_seed =
            202607260621ULL;
        if (!audit_calendar_eight_targets &&
            ((seed_option_used &&
              seed == calendar_eight_audit_seed) ||
             (training_seed_option_used &&
              training_seed == calendar_eight_audit_seed))) {
            throw std::invalid_argument(
                "reserved CT8-0 audit seed 202607260621 may be used "
                "only by --audit-calendar-eight-targets");
        }
        if (!audit_replay_weights &&
            ((seed_option_used &&
              seed ==
                  old_school::replay_weight_audit::kAuditSeed) ||
             (training_seed_option_used &&
              training_seed ==
                  old_school::replay_weight_audit::kAuditSeed))) {
            throw std::invalid_argument(
                "reserved RB0-0 audit seed 202607261047 may be used "
                "only by --audit-replay-weights");
        }
        if ((seed_option_used &&
             seed ==
                 old_school::replay_weight_audit::
                     kQuarantinedAuditSeed) ||
            (training_seed_option_used &&
             training_seed ==
                 old_school::replay_weight_audit::
                     kQuarantinedAuditSeed)) {
            throw std::invalid_argument(
                "quarantined RB0-0 audit seed 202607260731 may not "
                "be reused");
        }
        if (audit_dc1_dominance &&
            dc1_unsupported_option_used) {
            throw std::invalid_argument(
                "--audit-dc1-dominance accepts only "
                "--train-games, --train-seed, and "
                "--learned-generations");
        }
        if (audit_dc1_dominance &&
            (training_games != 800 ||
             training_seed != 424242 ||
             !learned_generations_option_used ||
             learned_generations != 16)) {
            throw std::invalid_argument(
                "--audit-dc1-dominance requires exact "
                "--train-games 800 --train-seed 424242 "
                "--learned-generations 16");
        }
        if (audit_dc1_action_census &&
            dc1_census_unsupported_option_used) {
            throw std::invalid_argument(
                "--audit-dc1-action-census accepts only "
                "--train-games, --train-seed, and "
                "--learned-generations");
        }
        if (audit_dc1_action_census &&
            (training_games != 800 ||
             training_seed != 424242 ||
             !learned_generations_option_used ||
             learned_generations != 16)) {
            throw std::invalid_argument(
                "--audit-dc1-action-census requires exact "
                "--train-games 800 --train-seed 424242 "
                "--learned-generations 16");
        }
        if (audit_v3_blue_stack_regret &&
            bsr0_unsupported_option_used) {
            throw std::invalid_argument(
                "--audit-v3-blue-stack-regret accepts only "
                "--train-games, --train-seed, and "
                "--learned-generations");
        }
        if (audit_v3_blue_stack_regret &&
            (training_games != 800 ||
             training_seed != 424242 ||
             !learned_generations_option_used ||
             learned_generations != 16)) {
            throw std::invalid_argument(
                "--audit-v3-blue-stack-regret requires exact "
                "--train-games 800 --train-seed 424242 "
                "--learned-generations 16");
        }
        if (interactive &&
            interactive_unsupported_option_used) {
            throw std::invalid_argument(
                "--interactive only accepts --seed, --train-seed, "
                "--train-games, --learned-generations, and "
                "--learned-rollouts, and "
                "--value-continuation-epsilon");
        }
        if (probe_option_used && !score_probes) {
            throw std::invalid_argument(
                "--probe-corpus, --probe-worlds, --probe-horizon, "
                "--actor-generation, --value-generation, --value-recipe, "
                "--probe-cache, and --refresh-probe-cache require "
                "--score-probes");
        }
        if (challenger_option_used &&
            !benchmark && !score_probes) {
            throw std::invalid_argument(
                "--challenger requires --benchmark or "
                "--score-probes");
        }
        if (baseline_option_used && !benchmark) {
            throw std::invalid_argument(
                "--baseline requires --benchmark");
        }
        if (evolve_pilot_option_used && !evolve) {
            throw std::invalid_argument(
                "--evolve-pilot requires --evolve-deck");
        }
        const auto selects_value_context_challenger =
            [](const BotSelection& selection) {
                return selection.kind ==
                           old_school::BotKind::Learned &&
                       selection.learned_variant ==
                           old_school::LearnedVariant::
                               ValueSearchChampion &&
                       selection.value_family ==
                           BotSelection::ValueFamily::
                               ContextChallenger &&
                       selection.value_generation > 0;
            };
        const auto selects_value_dense_masked_challenger =
            [](const BotSelection& selection) {
                return selection.kind ==
                           old_school::BotKind::Learned &&
                       selection.learned_variant ==
                           old_school::LearnedVariant::
                               ValueSearchChampion &&
                       selection.value_family ==
                           BotSelection::ValueFamily::
                               DenseMaskedChallenger &&
                       selection.value_generation > 0;
            };
        const auto selects_value_dense_context_challenger =
            [](const BotSelection& selection) {
                return selection.kind ==
                           old_school::BotKind::Learned &&
                       selection.learned_variant ==
                           old_school::LearnedVariant::
                               ValueSearchChampion &&
                       selection.value_family ==
                           BotSelection::ValueFamily::
                               DenseContextChallenger &&
                       selection.value_generation > 0;
            };
        const auto selects_terminal_weight_c17 =
            [](const BotSelection& selection) {
                return selection.kind ==
                           old_school::BotKind::Learned &&
                       selection.learned_variant ==
                           old_school::LearnedVariant::
                               ValueSearchChampion &&
                       (selection.value_family ==
                            BotSelection::ValueFamily::
                                TerminalWeight50 ||
                        selection.value_family ==
                            BotSelection::ValueFamily::
                                TerminalWeight75);
            };
        if (benchmark &&
            (selects_terminal_weight_c17(challenger) ||
             selects_terminal_weight_c17(baseline)) &&
            (training_games != 800 ||
             training_seed != 424242)) {
            throw std::invalid_argument(
                "terminal-weight C17 benchmark tokens require "
                "exact --train-games 800 --train-seed 424242");
        }
        if (benchmark &&
            (selects_terminal_weight_c17(challenger) ||
             selects_terminal_weight_c17(baseline)) &&
            (seed == old_school::kTerminalWeightC17ShardSeed ||
             seed == old_school::kTerminalWeightC17HoldoutSeed ||
             seed == old_school::kTerminalWeightC17GameplaySeed)) {
            throw std::invalid_argument(
                "reserved terminal-weight C17 seeds may be used "
                "only by the sealed evaluator");
        }
        if (evolve &&
            evolve_pilot.kind == old_school::BotKind::Learned &&
            !selects_value_context_challenger(evolve_pilot)) {
            throw std::invalid_argument(
                "Learned --evolve-pilot currently requires "
                "learned-value-context-cN");
        }
        const bool probe_sparse_context_challenger_selected =
            score_probes && challenger_option_used &&
            selects_value_context_challenger(challenger);
        const bool probe_dense_masked_challenger_selected =
            score_probes && challenger_option_used &&
            selects_value_dense_masked_challenger(challenger);
        const bool probe_dense_context_challenger_selected =
            score_probes && challenger_option_used &&
            selects_value_dense_context_challenger(challenger);
        const bool probe_context_ablation_selected =
            probe_sparse_context_challenger_selected ||
            probe_dense_masked_challenger_selected ||
            probe_dense_context_challenger_selected;
        if (score_probes && challenger_option_used &&
            !probe_context_ablation_selected) {
            throw std::invalid_argument(
                "--score-probes --challenger requires "
                "learned-value-context-cN, "
                "learned-value-dense-masked-cN, or "
                "learned-value-dense-context-cN");
        }
        if (probe_context_ablation_selected &&
            learned_generations == 0) {
            throw std::invalid_argument(
                "context-ablation probe scoring requires "
                "--learned-generations N for its state-only S0");
        }
        if (probe_context_ablation_selected &&
            challenger.value_generation !=
                learned_generations) {
            throw std::invalid_argument(
                "context-ablation generation must match "
                "--learned-generations N for ordered probe "
                "attribution");
        }
        if (score_probes &&
            value_recipe ==
                old_school::LearnedValueG8Recipe::LateMix50 &&
            value_generation != 8) {
            throw std::invalid_argument(
                "--value-recipe mix50 requires "
                "--value-generation 8");
        }
        const bool benchmark_uses_learned =
            benchmark &&
            (challenger.kind == old_school::BotKind::Learned ||
             baseline.kind == old_school::BotKind::Learned);
        const bool evolution_uses_learned =
            evolve &&
            evolve_pilot.kind == old_school::BotKind::Learned;
        const bool evolution_uses_value =
            evolution_uses_learned &&
            evolve_pilot.learned_variant ==
                old_school::LearnedVariant::ValueSearchChampion;
        const bool benchmark_challenger_uses_value =
            benchmark &&
            challenger.kind == old_school::BotKind::Learned &&
            challenger.learned_variant ==
                old_school::LearnedVariant::
                    ValueSearchChampion;
        const auto is_pd0_c16 =
            [](const BotSelection& selection) {
                return selection.kind ==
                           old_school::BotKind::Learned &&
                       selection.learned_variant ==
                           old_school::LearnedVariant::
                               ValueSearchChampion &&
                       selection.value_family ==
                           BotSelection::ValueFamily::Challenger &&
                       selection.value_generation == 16;
            };
        if (value_pass_dominance &&
            !benchmark_challenger_uses_value) {
            throw std::invalid_argument(
                "--value-pass-dominance requires --benchmark "
                "with a Learned Value challenger");
        }
        const bool pd0_smoke_seed_used =
            (seed_option_used && seed == pd0_smoke_seed) ||
            (training_seed_option_used &&
             training_seed == pd0_smoke_seed);
        const bool exact_pd0_smoke_configuration =
            benchmark && is_pd0_c16(challenger) &&
            is_pd0_c16(baseline) &&
            seed_option_used && seed == pd0_smoke_seed &&
            games_were_set && games == 4 &&
            training_games == 800 &&
            training_seed == 424242 &&
            learned_rollouts == 8 &&
            !pd0_smoke_unsupported_option_used;
        if (pd0_smoke_seed_used &&
            !exact_pd0_smoke_configuration) {
            throw std::invalid_argument(
                "reserved PD0 smoke seed 202607260948 may be used "
                "only by the exact 240-game C16/K8-vs-C16/K8 "
                "paired control or challenger-only treatment");
        }
        const bool allow_pd0_identical_policy_control =
            pd0_smoke_seed_used &&
            exact_pd0_smoke_configuration &&
            !value_pass_dominance;
        const bool tournament_uses_any_learned =
            !interactive && !benchmark && !stability && !evolve &&
            !diagnose_white_plan && !diagnose_value_context &&
            !diagnose_force_spike_teacher &&
            !diagnose_value_pass_dominance &&
            !train_p_family &&
            !diagnose_p1_fit &&
            !score_p1r_probes &&
            !diagnose_terminal_credit &&
            !train_terminal_weight_c17 &&
            !evaluate_terminal_weight_c17 &&
            !audit_dc1_dominance &&
            !audit_dc1_action_census &&
            !audit_v3_blue_stack_regret &&
            !audit_calendar_turn_targets &&
            !audit_calendar_eight_targets &&
            !variance_study && !score_probes &&
            (bot_field == old_school::BotField::Mixed ||
             bot_field == old_school::BotField::Learned);
        const bool tournament_uses_value =
            tournament_uses_any_learned &&
            (bot_field == old_school::BotField::Mixed ||
             bot_field_learned_variant ==
                 old_school::LearnedVariant::
                     ValueSearchChampion);
        if (value_continuation_epsilon_option_used &&
            !(interactive || stability || score_probes ||
              tournament_uses_value ||
              benchmark_challenger_uses_value ||
              evolution_uses_value)) {
            throw std::invalid_argument(
                "--value-continuation-epsilon requires "
                "--interactive, --stability, --score-probes, a "
                "mixed/learned-value simulation, or a benchmark "
                "with a Learned Value challenger, or Learned Value "
                "deck evolution");
        }
        if (learned_generations_option_used &&
            !(interactive || stability ||
              diagnose_force_spike_teacher ||
              audit_dc1_dominance ||
              audit_dc1_action_census ||
              audit_v3_blue_stack_regret || score_probes ||
              tournament_uses_value)) {
            throw std::invalid_argument(
                "--learned-generations requires --interactive, "
                "--stability, --diagnose-force-spike-teacher, "
                "--audit-dc1-dominance, "
                "--audit-dc1-action-census, "
                "--audit-v3-blue-stack-regret, --score-probes, or a "
                "mixed/learned-value simulation; "
                "benchmark challengers use "
                "learned-value-cN");
        }
        if (learned_rollouts_option_used &&
            !(interactive || stability || score_probes ||
              tournament_uses_any_learned ||
              benchmark_uses_learned ||
              evolution_uses_learned)) {
            throw std::invalid_argument(
                "--learned-rollouts requires --interactive, "
                "--stability, --score-probes, a benchmark with a "
                "Learned bot, a mixed/learned simulation, or Learned "
                "deck evolution");
        }
        if (score_probes && learned_rollouts < 2) {
            throw std::invalid_argument(
                "--score-probes requires --learned-rollouts "
                "of at least two");
        }
        const auto selects_actor_g1 =
            [](const BotSelection& selection) {
                return selection.kind == old_school::BotKind::Learned &&
                       selection.learned_variant ==
                           old_school::LearnedVariant::UnifiedActor &&
                       selection.actor_generation == 1;
            };
        const bool actor_g1_will_be_trained =
            (score_probes && actor_generation == 1) ||
            (benchmark &&
             (selects_actor_g1(challenger) ||
              selects_actor_g1(baseline)));
        if (actor_policy_option_used &&
            !actor_g1_will_be_trained) {
            throw std::invalid_argument(
                "--actor-policy-epochs and --actor-policy-rate "
                "require a selected Actor G1");
        }
        const auto selects_value_challenger =
            [](const BotSelection& selection) {
                return selection.kind ==
                           old_school::BotKind::Learned &&
                       selection.learned_variant ==
                           old_school::LearnedVariant::
                               ValueSearchChampion &&
                       selection.value_family ==
                           BotSelection::ValueFamily::Challenger &&
                       selection.value_generation > 0;
            };
        const auto selects_canonical_value_bundle_checkpoint =
            [](const BotSelection& selection) {
                return selection.kind ==
                           old_school::BotKind::Learned &&
                       selection.learned_variant ==
                           old_school::LearnedVariant::
                               ValueSearchChampion &&
                       selection.value_family ==
                           BotSelection::ValueFamily::Canonical &&
                       selection.value_generation > 0;
            };
        const auto selects_mix50_value_bundle =
            [](const BotSelection& selection) {
                return selection.kind ==
                           old_school::BotKind::Learned &&
                       selection.learned_variant ==
                           old_school::LearnedVariant::
                               ValueSearchChampion &&
                       selection.value_family ==
                           BotSelection::ValueFamily::Mix50 &&
                       selection.value_generation == 8;
            };
        const bool value_g8_will_be_used =
            (score_probes && value_generation == 8 &&
             value_recipe ==
                 old_school::LearnedValueG8Recipe::
                     CanonicalAllSearchLate) ||
            (benchmark &&
             (selects_canonical_value_bundle_checkpoint(
                  challenger) ||
              selects_canonical_value_bundle_checkpoint(
                  baseline)));
        const bool value_mix50_will_be_used =
            (score_probes && value_generation == 8 &&
             value_recipe ==
                 old_school::LearnedValueG8Recipe::LateMix50) ||
            (benchmark &&
             (selects_mix50_value_bundle(challenger) ||
              selects_mix50_value_bundle(baseline)));
        const bool value_challenger_will_be_used =
            ((interactive || stability ||
              diagnose_force_spike_teacher ||
              audit_dc1_dominance ||
              audit_dc1_action_census ||
              audit_v3_blue_stack_regret || score_probes ||
              tournament_uses_value) &&
             learned_generations > 0) ||
            (benchmark &&
             (selects_value_challenger(challenger) ||
              selects_value_challenger(baseline) ||
              selects_value_context_challenger(challenger) ||
              selects_value_context_challenger(baseline) ||
              selects_value_dense_masked_challenger(challenger) ||
              selects_value_dense_masked_challenger(baseline) ||
              selects_value_dense_context_challenger(challenger) ||
              selects_value_dense_context_challenger(baseline))) ||
            (evolve &&
             selects_value_context_challenger(evolve_pilot));
        if (refresh_value_challenger_cache &&
            !value_challenger_will_be_used) {
            throw std::invalid_argument(
                "--refresh-value-challenger-cache requires a route "
                "that selects Value Challenger C<N>");
        }
        if (refresh_value_g8_cache &&
            !value_g8_will_be_used) {
            throw std::invalid_argument(
                "--refresh-value-g8-cache requires a benchmark "
                "or probe route that selects canonical Value G8");
        }
        if (refresh_value_mix50_cache &&
            !value_mix50_will_be_used) {
            throw std::invalid_argument(
                "--refresh-value-mix50-cache requires a benchmark "
                "or probe route that selects Value G8 Late-Mix50");
        }
        if (evaluate_terminal_weight_c17) {
            try {
                const auto report =
                    old_school::terminal_weight_eval::
                        run_sealed_terminal_weight_c17_evaluation(
                            std::cout);
                old_school::terminal_weight_eval::
                    write_human_report(report, std::cout);
                old_school::terminal_weight_eval::
                    write_tsv_report(report, std::cout);
                return report.passed ? 0 : 1;
            } catch (const std::exception& error) {
                std::cerr
                    << "TW-C17 infrastructure/incomplete-evidence "
                       "failure: "
                    << error.what() << '\n';
                return 2;
            }
        }
        if (audit_calendar_turn_targets) {
            try {
                const auto report =
                    old_school::turn_alignment_audit::
                        run_canonical_ta4_audit(std::cout);
                old_school::turn_alignment_audit::
                    write_human_report(report, std::cout);
                old_school::turn_alignment_audit::
                    write_tsv_report(report, std::cout);
                return old_school::turn_alignment_audit::
                    audit_exit_code(true, report.passed);
            } catch (const std::exception& error) {
                std::cerr
                    << "TA4-0 infrastructure/incomplete-evidence "
                       "failure: "
                    << error.what() << '\n';
                return old_school::turn_alignment_audit::
                    audit_exit_code(false, false);
            }
        }
        if (audit_calendar_eight_targets) {
            try {
                const auto report =
                    old_school::target_factorial_audit::
                        run_canonical_ct8_audit(std::cout);
                old_school::target_factorial_audit::
                    write_human_report(report, std::cout);
                old_school::target_factorial_audit::
                    write_tsv_report(report, std::cout);
                return old_school::target_factorial_audit::
                    audit_exit_code(true, report.passed);
            } catch (const std::exception& error) {
                std::cerr
                    << "CT8-0 infrastructure/incomplete-evidence "
                       "failure: "
                    << error.what() << '\n';
                return old_school::target_factorial_audit::
                    audit_exit_code(false, false);
            }
        }
        if (audit_replay_weights) {
            try {
                const auto report =
                    old_school::replay_weight_audit::
                        run_canonical_rb0_audit(std::cout);
                old_school::replay_weight_audit::
                    write_human_report(report, std::cout);
                old_school::replay_weight_audit::
                    write_tsv_report(report, std::cout);
                return old_school::replay_weight_audit::
                    audit_exit_code(
                        old_school::replay_weight_audit::
                            infrastructure_complete(report),
                        report.passed);
            } catch (const std::exception& error) {
                std::cerr
                    << "RB0-0 infrastructure/incomplete-evidence "
                       "failure: "
                    << error.what() << '\n';
                return old_school::replay_weight_audit::
                    audit_exit_code(false, false);
            }
        }
        if (diagnose_p1_fit) {
            std::cout
                << "Preparing P1 same-parent fit diagnostic"
                << " (root seed " << seed
                << ", C16 training seed " << training_seed
                << ", " << training_games
                << " initial games)...\n";
            const auto p0 =
                train_value_challenger_with_progress(
                    training_games, training_seed, 16, false);
            const std::string p0_fingerprint =
                old_school::learned_model_fingerprint(p0);
            if (p0_fingerprint != kCanonicalP0Fingerprint) {
                throw std::runtime_error(
                    "Value Challenger C16 P0 fingerprint mismatch: "
                    "expected " +
                    std::string(kCanonicalP0Fingerprint) +
                    ", got " + p0_fingerprint);
            }

            old_school::LearnedValuePolicyFamilyConfig config;
            config.generations = 1;
            config.capacity_diagnostic_optimizers =
                p1_fit_diagnostic_optimizers();
            config.compute_rootwise_oracle = true;
            config.required_p0_fingerprint =
                kCanonicalP0Fingerprint;
            const auto started =
                std::chrono::steady_clock::now();
            const auto family =
                old_school::train_learned_value_policy_family(
                    p0, seed, config);
            const std::chrono::duration<double> elapsed =
                std::chrono::steady_clock::now() - started;
            validate_p_family_result(family, seed, 1);
            validate_p1_fit_diagnostic(family);
            print_p1_fit_diagnostic(
                family, seed, training_games, training_seed,
                elapsed.count());
            return 0;
        }
        if (score_p1r_probes) {
            constexpr std::uint64_t kP1RRootSeed = 577215;
            constexpr std::size_t kP1RTrainingGames = 800;
            constexpr std::uint64_t kP1RTrainingSeed = 424242;
            const std::filesystem::path dev_cache =
                "data/old-school-probe-dev-v3-k8-h0-audit.labels.tsv";
            const std::filesystem::path validation_cache =
                "data/old-school-probe-validation-v1-exact-v2-k128-h0-t800-s424242.labels.tsv";
            if (seed != kP1RRootSeed ||
                training_games != kP1RTrainingGames ||
                training_seed != kP1RTrainingSeed) {
                throw std::invalid_argument(
                    "--score-p1r-probes requires exact --seed "
                    "577215 --train-games 800 --train-seed 424242");
            }
            if (!std::filesystem::exists(dev_cache) ||
                !std::filesystem::exists(validation_cache)) {
                throw std::runtime_error(
                    "--score-p1r-probes requires both immutable "
                    "preregistered probe caches from Environment-v2; "
                    "the exact-v2 validation cache is legacy");
            }
            throw std::runtime_error(
                "--score-p1r-probes requires both immutable "
                "preregistered probe caches, but its Environment-v2 "
                "caches are legacy and cannot run under Environment-v3 "
                "cleanup-discard rules; preregister a fresh v3 route "
                "and labels");

            std::cout
                << "P1R Revised-Optimizer Offline Gate\n"
                << "Root seed: " << seed
                << "\nTraining seed/games: " << training_seed
                << '/' << training_games
                << "\nOptimizer: Adam batch 64, epochs 128, "
                   "rate 0.003, beta1 0.9, beta2 0.999, "
                   "epsilon 1e-8, clip 5"
                << "\nDev scoring: immutable Actor K=8/H=0 "
                   "labels, Value K=8/H=4"
                << "\nValidation scoring: immutable Actor "
                   "K=128/H=0 labels, Value K=256/H=4\n\n";

            const auto p0 =
                train_value_challenger_with_progress(
                    training_games, training_seed, 16, false);
            const std::string p0_fingerprint =
                old_school::learned_model_fingerprint(p0);
            if (p0_fingerprint != kCanonicalP0Fingerprint) {
                throw std::runtime_error(
                    "Value Challenger C16 P0 fingerprint mismatch: "
                    "expected " +
                    std::string(kCanonicalP0Fingerprint) +
                    ", got " + p0_fingerprint);
            }
            const auto actor_g0 =
                train_actor_g0_with_progress(
                    training_games, training_seed);
            const std::string actor_fingerprint =
                old_school::learned_model_fingerprint(actor_g0);
            if (actor_fingerprint !=
                kCanonicalActorG0Fingerprint) {
                throw std::runtime_error(
                    "Actor G0 fingerprint mismatch: expected " +
                    std::string(kCanonicalActorG0Fingerprint) +
                    ", got " + actor_fingerprint);
            }

            old_school::LearnedValuePolicyFamilyConfig family_config;
            family_config.generations = 1;
            family_config.optimizer.epochs = 128;
            family_config.optimizer.learning_rate = 0.003;
            family_config.required_p0_fingerprint =
                kCanonicalP0Fingerprint;
            const auto started =
                std::chrono::steady_clock::now();
            const auto family =
                old_school::train_learned_value_policy_family(
                    p0, seed, family_config);
            const std::chrono::duration<double> elapsed =
                std::chrono::steady_clock::now() - started;
            validate_p_family_result(
                family, seed, 1, family_config.optimizer, false);
            const auto p1r = family.checkpoints.at(1);
            const std::string p1r_fingerprint =
                old_school::learned_model_fingerprint(p1r);
            if (p1r_fingerprint != kP1RExpectedFingerprint) {
                throw std::runtime_error(
                    "P1R fingerprint mismatch: expected " +
                    std::string(kP1RExpectedFingerprint) +
                    ", got " + p1r_fingerprint);
            }
            const bool mechanism_passed =
                print_p_family_generation_report(
                    family.reports.front());
            std::cout
                << "  Revised P1R reconstruction time: "
                << format_real(elapsed.count()) << " seconds\n";
            if (!mechanism_passed) {
                std::cout
                    << "\nP1R offline verdict: REJECT "
                       "(mechanism gate failed; probes skipped)\n";
                return 1;
            }

            const auto make_models =
                [&]() {
                    return old_school::probe_runner::
                        ProbeScoringModels{
                            .reference_actor_model = actor_g0,
                            .scoring_actor_model = actor_g0,
                            .scoring_actor_name = "Actor G0",
                            .reference_value_model = p0,
                            .reference_value_name =
                                "P0 residual-off",
                            .scoring_value_models = {
                                {
                                    .name = "P0 residual-on",
                                    .model = p0,
                                    .transition_family =
                                        "p1r-revised-optimizer",
                                    .value_priority_residual_weight =
                                        0.10,
                                },
                                {
                                    .name = "P1R",
                                    .model = p1r,
                                    .transition_family =
                                        "p1r-revised-optimizer",
                                    .value_priority_residual_weight =
                                        0.10,
                                },
                            },
                        };
                };
            const old_school::probe_runner::ProbeScoreConfig
                dev_config{
                    .training_games = training_games,
                    .training_seed = training_seed,
                    .reference_worlds = 8,
                    .reference_horizon_turns = 0,
                    .reference_rollouts_per_world = 1,
                    .scoring_value_worlds = 8,
                    .scoring_value_continuation_epsilon = 0.0,
                    .cache_path = dev_cache,
                    .refresh_cache = false,
                };
            const auto dev_report =
                old_school::probe_runner::
                    score_probe_corpus_with_candidates(
                        old_school::probe_runner::ProbeCorpusKind::
                            DevV3,
                        dev_config, std::cout, make_models());
            std::cout
                << old_school::probe_runner::
                       format_probe_score_report(dev_report);

            const old_school::probe_runner::ProbeScoreConfig
                validation_config{
                    .training_games = training_games,
                    .training_seed = training_seed,
                    .reference_worlds = 128,
                    .reference_horizon_turns = 0,
                    .reference_rollouts_per_world = 1,
                    .scoring_value_worlds = 256,
                    .scoring_value_continuation_epsilon = 0.0,
                    .cache_path = validation_cache,
                    .refresh_cache = false,
                };
            const auto validation_report =
                old_school::probe_runner::
                    score_probe_corpus_with_candidates(
                        old_school::probe_runner::ProbeCorpusKind::
                            ValidationV1,
                        validation_config, std::cout, make_models());
            std::cout
                << old_school::probe_runner::
                       format_probe_score_report(
                           validation_report);
            return evaluate_p1r_offline_gate(
                       dev_report, validation_report)
                       ? 0
                       : 1;
        }
        if (train_terminal_weight_c17) {
            train_terminal_weight_c17_with_progress(
                training_games, training_seed);
            return 0;
        }
        if (diagnose_terminal_credit) {
            constexpr std::size_t kTerminalTrainingGames = 800;
            constexpr std::uint64_t kTerminalTrainingSeed = 424242;
            constexpr std::size_t kTerminalWorlds = 1024;
            constexpr std::size_t kTerminalHorizon = 128;

            const auto p0 =
                train_value_challenger_with_progress(
                    kTerminalTrainingGames, kTerminalTrainingSeed,
                    16, false);
            const std::string fingerprint =
                old_school::learned_model_fingerprint(p0);
            if (fingerprint != kCanonicalP0Fingerprint) {
                std::cout
                    << "Terminal Credit Audit\n"
                    << "Exact P0 identity: FAIL\n"
                    << "  expected: " << kCanonicalP0Fingerprint
                    << "\n  actual:   " << fingerprint << '\n';
                return 1;
            }

            std::cout
                << "Scoring exact P0 terminal credit "
                   "(Value mirror, K=1024/H=128, one rollout/world, "
                   "epsilon=0, residual=0, shallow blend off, "
                   "terminal required)..."
                << std::flush;
            const auto report =
                old_school::probe_runner::
                    score_teacher_sufficiency_audit(
                        p0, "Exact P0 C16 Value K1024/H128",
                        {
                            .worlds = kTerminalWorlds,
                            .horizon_turns = kTerminalHorizon,
                            .continuation_variant =
                                old_school::LearnedVariant::
                                    ValueSearchChampion,
                            .blend_shallow_prior = false,
                            .require_terminal_results = true,
                        });
            std::cout
                << " done\n\n"
                << old_school::probe_runner::
                       format_terminal_credit_audit_report(report);
            const bool exact_identity =
                report.model_fingerprint ==
                kCanonicalP0Fingerprint;
            return exact_identity &&
                           old_school::probe_runner::
                               terminal_credit_primary_gate_passed(
                                   report)
                       ? 0
                       : 1;
        }
        if (audit_v3_blue_stack_regret) {
            std::cout
                << "Preparing pinned Environment-v3 Value "
                   "Challenger C16 for BSR0...\n";
            const auto parent =
                load_value_challenger_with_progress(
                    training_games, training_seed,
                    learned_generations);
            old_school::probes::BsrAuditConfig config;
            config.required_model_fingerprint =
                std::string(kDc1EnvironmentV3P0Fingerprint);
            const auto started =
                std::chrono::steady_clock::now();
            const auto report =
                old_school::probes::
                    audit_bsr_blue_stack_regret(
                        parent, config);
            const std::chrono::duration<double> elapsed =
                std::chrono::steady_clock::now() - started;
            print_bsr_report(report, elapsed.count());
            return report.gate_passed ? 0 : 1;
        }
        if (audit_dc1_action_census) {
            std::cout
                << "Preparing pinned Environment-v3 Value "
                   "Challenger C16 for the DC1-B0 census...\n";
            const auto parent =
                load_value_challenger_with_progress(
                    training_games, training_seed,
                    learned_generations);
            old_school::probes::Dc1ActionCensusConfig config;
            config.required_model_fingerprint =
                std::string(kDc1EnvironmentV3P0Fingerprint);
            const auto report =
                old_school::probes::
                    audit_dc1_action_census(parent, config);
            print_dc1_census_report(report);
            return report.gate_passed ? 0 : 1;
        }
        if (audit_dc1_dominance) {
            std::cout
                << "Preparing frozen Environment-v3 Value "
                   "Challenger C16 for the DC1 audit...\n";
            const auto parent =
                load_value_challenger_with_progress(
                    training_games, training_seed,
                    learned_generations);
            old_school::probes::Dc1MiningConfig config;
            config.required_model_fingerprint =
                std::string(kDc1EnvironmentV3P0Fingerprint);
            const auto started =
                std::chrono::steady_clock::now();
            const auto report =
                old_school::probes::
                    audit_dc1_dominance_mining(parent, config);
            const std::chrono::duration<double> elapsed =
                std::chrono::steady_clock::now() - started;
            print_dc1_mining_report(report);
            std::cout
                << "Audit time: " << format_real(elapsed.count())
                << " seconds\n";
            return report.gate_passed ? 0 : 1;
        }
        if (train_p_family) {
            std::cout
                << "Canonical Outcome-Tilted Priority P-Family\n"
                << "Root seed: " << seed
                << "\nInitial model: Value Challenger C16"
                << "\nTraining games: " << training_games
                << "\nTraining seed: " << training_seed
                << "\nRequired P0 fingerprint: "
                << kCanonicalP0Fingerprint
                << "\nRecipe: 40 games/generation, 80 seat-games, "
                   "K=8/H=4, one rollout/world, root cap 32, "
                   "max turns 500, 4 collection threads, residual "
                   "0.1, TD(lambda) 0.9"
                << "\nOptimizer: Adam batch 64, epochs 8, "
                   "rate 0.001, beta1 0.9, beta2 0.999, "
                   "epsilon 1e-8, clip 5"
                << "\nRequested checkpoints: P1..P"
                << p_family_generations << "\n\n";
            const auto p0 =
                train_value_challenger_with_progress(
                    training_games, training_seed, 16, false);
            const std::string p0_fingerprint =
                old_school::learned_model_fingerprint(p0);
            if (p0_fingerprint != kCanonicalP0Fingerprint) {
                throw std::runtime_error(
                    "Value Challenger C16 P0 fingerprint mismatch: "
                    "expected " +
                    std::string(kCanonicalP0Fingerprint) +
                    ", got " + p0_fingerprint);
            }
            old_school::LearnedValuePolicyFamilyConfig config;
            config.generations = p_family_generations;
            config.required_p0_fingerprint =
                kCanonicalP0Fingerprint;
            const auto started =
                std::chrono::steady_clock::now();
            const auto family =
                old_school::train_learned_value_policy_family(
                    p0, seed, config);
            const std::chrono::duration<double> elapsed =
                std::chrono::steady_clock::now() - started;
            validate_p_family_result(
                family, seed, p_family_generations);

            bool every_mechanism_passed = true;
            for (const auto& report : family.reports) {
                every_mechanism_passed =
                    print_p_family_generation_report(report) &&
                    every_mechanism_passed;
            }
            std::cout
                << "\nP-family training complete: P0..P"
                << p_family_generations << " in "
                << format_real(elapsed.count())
                << " seconds\nMechanism summary: "
                << (every_mechanism_passed ? "PASS" : "REJECT")
                << (every_mechanism_passed
                        ? "\n"
                        : " (scientific result; process succeeded)\n");
            return 0;
        }
        if (diagnose_value_context) {
            const auto result =
                old_school::diagnose_value_context_aliases();
            print_value_context_alias_diagnostic(result);
            return result.demonstrated() ? 0 : 1;
        }
        if (diagnose_value_pass_dominance) {
            return run_pd0_diagnostic(seed);
        }
        if (diagnose_force_spike_teacher) {
            const auto value_s0 =
                train_value_challenger_with_progress(
                    training_games, training_seed,
                    learned_generations, false);
            const auto actor_g0 =
                train_actor_g0_with_progress(
                    training_games, training_seed);
            const std::string generation =
                std::to_string(learned_generations);
            std::vector<
                old_school::probe_runner::
                    TeacherSufficiencyAuditReport>
                reports;
            reports.reserve(3);
            std::cout
                << "Scoring S0 C" << generation
                << " Value K=256/H=4 unblended teacher..."
                << std::flush;
            reports.push_back(
                old_school::probe_runner::
                    score_teacher_sufficiency_audit(
                        value_s0,
                        "S0 C" + generation +
                            " Value K256/H4",
                        {
                            .worlds = 256,
                            .horizon_turns = 4,
                            .continuation_variant =
                                old_school::LearnedVariant::
                                    ValueSearchChampion,
                            .blend_shallow_prior = false,
                        }));
            std::cout << " done\n"
                      << "Scoring S0 C" << generation
                      << " Value K=256/H=0 unblended control..."
                      << std::flush;
            reports.push_back(
                old_school::probe_runner::
                    score_teacher_sufficiency_audit(
                        value_s0,
                        "S0 C" + generation +
                            " Value K256/H0",
                        {
                            .worlds = 256,
                            .horizon_turns = 0,
                            .continuation_variant =
                                old_school::LearnedVariant::
                                    ValueSearchChampion,
                            .blend_shallow_prior = false,
                        }));
            std::cout << " done\n"
                      << "Scoring Actor G0 K=256/H=0 unblended "
                         "control..."
                      << std::flush;
            reports.push_back(
                old_school::probe_runner::
                    score_teacher_sufficiency_audit(
                        actor_g0, "Actor G0 K256/H0",
                        {
                            .worlds = 256,
                            .horizon_turns = 0,
                            .continuation_variant =
                                old_school::LearnedVariant::
                                    UnifiedActor,
                            .blend_shallow_prior = false,
                        }));
            std::cout
                << " done\n\n"
                << old_school::probe_runner::
                       format_teacher_sufficiency_audit_report(
                           reports);
            return 0;
        }
        if (interactive) {
            const auto matchup =
                old_school::choose_interactive_matchup(seed);
            std::cout
                << "Old School Magic Interactive\n"
                << "Match: Human "
                << old_school::deck_name(matchup.human_deck)
                << " vs Learned Value "
                << (learned_generations == 0
                        ? "G0 "
                        : "Challenger C" +
                              std::to_string(learned_generations) +
                              " ")
                << old_school::deck_name(matchup.learned_deck) << '\n'
                << "Game seed: " << seed << '\n'
                << "Training seed: " << training_seed << '\n'
                << "Learned search worlds per legal action: "
                << learned_rollouts << '\n';
            if (value_continuation_epsilon != 0.0) {
                std::cout
                    << "Value continuation priority-action epsilon: "
                    << format_real(value_continuation_epsilon)
                    << " (root remains greedy)\n";
            }
            std::cout
                << "Type q at any prompt to abandon the game.\n"
                << "Board layout: 120 columns, expanding to 180 for "
                   "the stack rail; opponent hand is shown for "
                   "inspection only.\n"
                << "MVP timing note: there is no priority window after "
                   "attackers or blockers are declared.\n";
            const auto learned_model =
                learned_generations == 0
                    ? train_value_g0_with_progress(
                          training_games, training_seed)
                    : train_value_challenger_with_progress(
                          training_games, training_seed,
                          learned_generations,
                          refresh_value_challenger_cache);
            old_school::run_interactive_match(
                std::cin, std::cout, seed, learned_model,
                matchup, learned_rollouts,
                value_continuation_epsilon);
            return 0;
        }
        if (score_probes) {
            const old_school::probe_runner::ProbeScoreConfig config{
                .training_games = training_games,
                .training_seed = training_seed,
                .reference_worlds = probe_worlds,
                .reference_horizon_turns = probe_horizon,
                .reference_rollouts_per_world = 1,
                .scoring_value_worlds = learned_rollouts,
                .scoring_value_continuation_epsilon =
                    value_continuation_epsilon,
                .cache_path = probe_cache,
                .refresh_cache = refresh_probe_cache,
            };
            const auto actor_g0 =
                train_actor_g0_with_progress(
                    training_games, training_seed);
            auto scoring_actor = actor_g0;
            if (actor_generation == 1) {
                scoring_actor =
                    train_actor_g1_with_progress(
                        actor_g0, training_seed,
                        actor_generation_config)
                        .model;
            }
            const auto value_g0 =
                train_value_g0_with_progress(
                    training_games, training_seed);
            std::vector<
                old_school::probe_runner::NamedValueScoringModel>
                scoring_value_models;
            std::vector<
                old_school::probe_runner::NamedValueScoringModel>
                challenger_scoring_models;
            if (learned_generations > 0) {
                const std::string transition_family =
                    probe_context_ablation_selected
                        ? "value-context-ablation-c" +
                              std::to_string(
                                  learned_generations)
                        : "value-challenger-c" +
                              std::to_string(
                                  learned_generations);
                challenger_scoring_models.push_back(
                    {
                        .name =
                            "Value Challenger C" +
                            std::to_string(
                                learned_generations),
                        .model =
                            train_value_challenger_with_progress(
                                training_games, training_seed,
                                learned_generations,
                                refresh_value_challenger_cache),
                        .transition_family =
                            transition_family,
                    });
                if (probe_context_ablation_selected) {
                    challenger_scoring_models.push_back(
                        {
                            .name =
                                "Value Context C" +
                                std::to_string(
                                    learned_generations),
                            .model =
                                train_value_context_challenger_with_progress(
                                    training_games, training_seed,
                                    learned_generations,
                                    refresh_value_challenger_cache),
                            .transition_family =
                                transition_family,
                        });
                }
                if (probe_dense_masked_challenger_selected ||
                    probe_dense_context_challenger_selected) {
                    challenger_scoring_models.push_back(
                        {
                            .name =
                                "Value Dense Masked C" +
                                std::to_string(
                                    learned_generations),
                            .model =
                                train_value_dense_context_challenger_with_progress(
                                    training_games, training_seed,
                                    learned_generations,
                                    old_school::
                                        LearnedValueDenseContextTreatment::
                                            ContextMasked,
                                    refresh_value_challenger_cache),
                            .transition_family =
                                transition_family,
                        });
                }
                if (probe_dense_context_challenger_selected) {
                    challenger_scoring_models.push_back(
                        {
                            .name =
                                "Value Dense Context C" +
                                std::to_string(
                                    learned_generations),
                            .model =
                                train_value_dense_context_challenger_with_progress(
                                    training_games, training_seed,
                                    learned_generations,
                                    old_school::
                                        LearnedValueDenseContextTreatment::
                                            ContextLive,
                                    refresh_value_challenger_cache),
                            .transition_family =
                                transition_family,
                        });
                }
            }
            if (value_generation == 8) {
                const bool mix50 =
                    value_recipe ==
                    old_school::LearnedValueG8Recipe::LateMix50;
                const auto value_g8 =
                    mix50
                        ? train_value_g8_mix50_with_progress(
                              training_games, training_seed,
                              refresh_value_mix50_cache)
                        : train_value_g8_with_progress(
                              training_games, training_seed,
                              refresh_value_g8_cache);
                if (value_g8.checkpoints.size() !=
                    old_school::kLearnedValueG8Generations + 1) {
                    throw std::runtime_error(
                        std::string(
                            mix50 ? "Value G8 Late-Mix50"
                                  : "Value G8") +
                        " trainer did not retain base-G8");
                }
                scoring_value_models.reserve(
                    scoring_value_models.size() + 9);
                scoring_value_models.push_back(
                    {
                        .name =
                            mix50 ? "Value Mix50 base"
                                  : "Value G8 base",
                        .model = value_g8.checkpoints.front(),
                        .transition_family =
                            mix50 ? "value-mix50-g8"
                                  : "value-canonical-g8",
                    });
                for (std::size_t generation = 1;
                     generation < value_g8.checkpoints.size();
                     ++generation) {
                    scoring_value_models.push_back(
                        {
                            .name =
                                std::string(
                                    mix50 ? "Value Mix50 G"
                                          : "Value G") +
                                std::to_string(generation),
                            .model =
                                value_g8.checkpoints[generation],
                            .transition_family =
                                mix50 ? "value-mix50-g8"
                                      : "value-canonical-g8",
                        });
                }
            }
            scoring_value_models.insert(
                scoring_value_models.end(),
                std::make_move_iterator(
                    challenger_scoring_models.begin()),
                std::make_move_iterator(
                    challenger_scoring_models.end()));
            const auto report =
                old_school::probe_runner::
                    score_probe_corpus_with_candidates(
                        probe_corpus, config, std::cout,
                        {
                            .reference_actor_model = actor_g0,
                            .scoring_actor_model = scoring_actor,
                            .scoring_actor_name =
                                actor_generation == 0
                                    ? "Actor G0"
                                    : "Actor G1",
                            .reference_value_model = value_g0,
                            .reference_value_name = "Value G0",
                            .scoring_value_models =
                                std::move(scoring_value_models),
                        });
            std::cout
                << old_school::probe_runner::format_probe_score_report(
                       report);
            return 0;
        }
        if (diagnose_white_plan) {
            const auto model =
                old_school::train_learned_actor_model(
                    training_games, training_seed);
            const auto result =
                old_school::diagnose_white_lock_plan_teacher(model, seed);
            print_white_plan_diagnostic(
                result, seed, training_seed, training_games);
            return 0;
        }
        if (benchmark) {
            auto challenger_config =
                bot_config(challenger, rollouts, deep_rollouts,
                           training_games, learned_rollouts,
                           value_continuation_epsilon);
            challenger_config.value_pass_dominance =
                value_pass_dominance;
            auto baseline_config =
                bot_config(baseline, rollouts, deep_rollouts,
                           training_games, learned_rollouts);
            old_school::GameConfig shared_config;
            shared_config.learned_training_seed = training_seed;
            std::shared_ptr<const old_school::LearnedModel>
                frozen_value_g0;
            std::map<
                std::size_t,
                std::shared_ptr<const old_school::LearnedModel>>
                frozen_value_challengers;
            std::map<
                std::size_t,
                std::shared_ptr<const old_school::LearnedModel>>
                frozen_value_context_challengers;
            std::map<
                std::size_t,
                std::shared_ptr<const old_school::LearnedModel>>
                frozen_value_dense_masked_challengers;
            std::map<
                std::size_t,
                std::shared_ptr<const old_school::LearnedModel>>
                frozen_value_dense_context_challengers;
            old_school::LearnedValueG8Result frozen_value_bundle;
            old_school::LearnedValueG8Result
                frozen_value_mix50_bundle;
            std::optional<
                old_school::LearnedTerminalWeightC17Artifact>
                frozen_terminal_weight_bundle;
            std::shared_ptr<const old_school::LearnedModel>
                frozen_actor_g0;
            std::shared_ptr<const old_school::LearnedModel>
                frozen_actor_g1;
            const auto resolve_frozen_model =
                [&](const BotSelection& selection)
                -> std::shared_ptr<const old_school::LearnedModel> {
                if (selection.learned_variant ==
                    old_school::LearnedVariant::
                        ValueSearchChampion) {
                    if (selection.value_family ==
                            BotSelection::ValueFamily::
                                TerminalWeight50 ||
                        selection.value_family ==
                            BotSelection::ValueFamily::
                                TerminalWeight75) {
                        if (!frozen_terminal_weight_bundle) {
                            frozen_terminal_weight_bundle =
                                load_terminal_weight_c17_with_progress(
                                    training_games,
                                    training_seed);
                        }
                        return selection.value_family ==
                                       BotSelection::ValueFamily::
                                           TerminalWeight50
                                   ? frozen_terminal_weight_bundle
                                         ->control_model()
                                   : frozen_terminal_weight_bundle
                                         ->treatment_model();
                    }
                    if (selection.value_family ==
                        BotSelection::ValueFamily::Challenger) {
                        const auto found =
                            frozen_value_challengers.find(
                                selection.value_generation);
                        if (found !=
                            frozen_value_challengers.end()) {
                            return found->second;
                        }
                        auto model =
                            train_value_challenger_with_progress(
                                training_games, training_seed,
                                selection.value_generation,
                                refresh_value_challenger_cache);
                        frozen_value_challengers.emplace(
                            selection.value_generation, model);
                        return model;
                    }
                    if (selection.value_family ==
                        BotSelection::ValueFamily::
                            ContextChallenger) {
                        const auto found =
                            frozen_value_context_challengers.find(
                                selection.value_generation);
                        if (found !=
                            frozen_value_context_challengers.end()) {
                            return found->second;
                        }
                        auto model =
                            train_value_context_challenger_with_progress(
                                training_games, training_seed,
                                selection.value_generation,
                                refresh_value_challenger_cache);
                        frozen_value_context_challengers.emplace(
                            selection.value_generation, model);
                        return model;
                    }
                    if (selection.value_family ==
                        BotSelection::ValueFamily::
                            DenseMaskedChallenger) {
                        const auto found =
                            frozen_value_dense_masked_challengers.find(
                                selection.value_generation);
                        if (found !=
                            frozen_value_dense_masked_challengers
                                .end()) {
                            return found->second;
                        }
                        auto model =
                            train_value_dense_context_challenger_with_progress(
                                training_games, training_seed,
                                selection.value_generation,
                                old_school::
                                    LearnedValueDenseContextTreatment::
                                        ContextMasked,
                                refresh_value_challenger_cache);
                        frozen_value_dense_masked_challengers.emplace(
                            selection.value_generation, model);
                        return model;
                    }
                    if (selection.value_family ==
                        BotSelection::ValueFamily::
                            DenseContextChallenger) {
                        const auto found =
                            frozen_value_dense_context_challengers.find(
                                selection.value_generation);
                        if (found !=
                            frozen_value_dense_context_challengers
                                .end()) {
                            return found->second;
                        }
                        auto model =
                            train_value_dense_context_challenger_with_progress(
                                training_games, training_seed,
                                selection.value_generation,
                                old_school::
                                    LearnedValueDenseContextTreatment::
                                        ContextLive,
                                refresh_value_challenger_cache);
                        frozen_value_dense_context_challengers.emplace(
                            selection.value_generation, model);
                        return model;
                    }
                    if (selection.value_family ==
                        BotSelection::ValueFamily::Mix50) {
                        if (selection.value_generation != 8) {
                            throw std::logic_error(
                                "Value G8 Late-Mix50 supports only "
                                "generation eight");
                        }
                        if (frozen_value_mix50_bundle
                                .checkpoints.empty()) {
                            frozen_value_mix50_bundle =
                                train_value_g8_mix50_with_progress(
                                    training_games,
                                    training_seed,
                                    refresh_value_mix50_cache);
                        }
                        const auto checkpoint =
                            old_school::
                                learned_value_g8_generation_checkpoint(
                                    frozen_value_mix50_bundle,
                                    selection.value_generation);
                        std::cout
                            << "  Selected Value Mix50 G8 "
                               "fingerprint: "
                            << old_school::learned_model_fingerprint(
                                   checkpoint)
                            << '\n';
                        return checkpoint;
                    }
                    if (selection.value_family ==
                        BotSelection::ValueFamily::LegacyG0) {
                        if (!frozen_value_g0) {
                            frozen_value_g0 =
                                train_value_g0_with_progress(
                                    training_games, training_seed);
                        }
                        return frozen_value_g0;
                    }
                    if (selection.value_family !=
                            BotSelection::ValueFamily::Canonical ||
                        selection.value_generation >
                        old_school::kLearnedValueG8Generations) {
                        throw std::logic_error(
                            "unsupported Value generation");
                    }
                    if (frozen_value_bundle.checkpoints.empty()) {
                        frozen_value_bundle =
                            train_value_g8_with_progress(
                                training_games, training_seed,
                                refresh_value_g8_cache);
                    }
                    const auto checkpoint =
                        old_school::
                            learned_value_g8_generation_checkpoint(
                                frozen_value_bundle,
                                selection.value_generation);
                    std::cout
                        << "  Selected Value G"
                        << selection.value_generation
                        << " fingerprint: "
                        << old_school::learned_model_fingerprint(
                               checkpoint)
                        << '\n';
                    return checkpoint;
                }
                if (!frozen_actor_g0) {
                    frozen_actor_g0 =
                        train_actor_g0_with_progress(
                            training_games, training_seed);
                }
                if (selection.actor_generation == 0) {
                    return frozen_actor_g0;
                }
                if (!frozen_actor_g1) {
                    frozen_actor_g1 =
                        train_actor_g1_with_progress(
                            frozen_actor_g0, training_seed,
                            actor_generation_config)
                            .model;
                }
                return frozen_actor_g1;
            };
            const auto selections_share_model =
                [](const BotSelection& left,
                   const BotSelection& right) {
                    if (left.learned_variant !=
                        right.learned_variant) {
                        return false;
                    }
                    if (left.learned_variant ==
                        old_school::LearnedVariant::UnifiedActor) {
                        return left.actor_generation ==
                               right.actor_generation;
                    }
                    if (left.value_family !=
                        right.value_family) {
                        return false;
                    }
                    switch (left.value_family) {
                    case BotSelection::ValueFamily::LegacyG0:
                    case BotSelection::ValueFamily::Mix50:
                    case BotSelection::ValueFamily::
                        TerminalWeight50:
                    case BotSelection::ValueFamily::
                        TerminalWeight75:
                        return true;
                    case BotSelection::ValueFamily::Challenger:
                    case BotSelection::ValueFamily::
                        ContextChallenger:
                    case BotSelection::ValueFamily::
                        DenseMaskedChallenger:
                    case BotSelection::ValueFamily::
                        DenseContextChallenger:
                    case BotSelection::ValueFamily::Canonical:
                        return left.value_generation ==
                               right.value_generation;
                    }
                    return false;
                };
            if (challenger_config.kind ==
                old_school::BotKind::Learned) {
                challenger_config.learned_model =
                    resolve_frozen_model(challenger);
                shared_config.learned_model =
                    challenger_config.learned_model;
            }
            if (baseline_config.kind ==
                old_school::BotKind::Learned) {
                if (challenger_config.kind ==
                        old_school::BotKind::Learned &&
                    selections_share_model(challenger, baseline)) {
                    baseline_config.learned_model =
                        challenger_config.learned_model;
                } else {
                    baseline_config.learned_model =
                        resolve_frozen_model(baseline);
                }
                if (!shared_config.learned_model) {
                    shared_config.learned_model =
                        baseline_config.learned_model;
                }
            }
            const auto result = old_school::run_bot_benchmark(
                games, seed, challenger_config,
                baseline_config, shared_config,
                allow_pd0_identical_policy_control);
            const auto benchmark_name =
                [](const BotSelection& selection,
                   const old_school::BotConfig& config) {
                    if (selection.kind ==
                            old_school::BotKind::Learned) {
                        if (selection.learned_variant ==
                            old_school::LearnedVariant::
                                UnifiedActor) {
                            return std::string(
                                       "Learned Actor G") +
                                   std::to_string(
                                       selection.actor_generation);
                        }
                        if (selection.value_family ==
                            BotSelection::ValueFamily::Challenger) {
                            return std::string(
                                       "Learned Value Challenger C") +
                                   std::to_string(
                                       selection.value_generation) +
                                   value_continuation_epsilon_suffix(
                                       config
                                           .value_continuation_epsilon);
                        }
                        if (selection.value_family ==
                            BotSelection::ValueFamily::
                                ContextChallenger) {
                            return std::string(
                                       "Learned Value Context C") +
                                   std::to_string(
                                       selection.value_generation) +
                                   value_continuation_epsilon_suffix(
                                       config
                                           .value_continuation_epsilon);
                        }
                        if (selection.value_family ==
                            BotSelection::ValueFamily::
                                DenseMaskedChallenger) {
                            return std::string(
                                       "Learned Value Dense Masked C") +
                                   std::to_string(
                                       selection.value_generation) +
                                   value_continuation_epsilon_suffix(
                                       config
                                           .value_continuation_epsilon);
                        }
                        if (selection.value_family ==
                            BotSelection::ValueFamily::
                                DenseContextChallenger) {
                            return std::string(
                                       "Learned Value Dense Context C") +
                                   std::to_string(
                                       selection.value_generation) +
                                   value_continuation_epsilon_suffix(
                                       config
                                           .value_continuation_epsilon);
                        }
                        if (selection.value_family ==
                            BotSelection::ValueFamily::
                                TerminalWeight50) {
                            return std::string(
                                       "Learned Value TW50 C17") +
                                   value_continuation_epsilon_suffix(
                                       config
                                           .value_continuation_epsilon);
                        }
                        if (selection.value_family ==
                            BotSelection::ValueFamily::
                                TerminalWeight75) {
                            return std::string(
                                       "Learned Value TW75 C17") +
                                   value_continuation_epsilon_suffix(
                                       config
                                           .value_continuation_epsilon);
                        }
                        if (selection.value_family ==
                            BotSelection::ValueFamily::Mix50) {
                            return std::string(
                                       "Learned Value Mix50 G8") +
                                   value_continuation_epsilon_suffix(
                                       config
                                           .value_continuation_epsilon);
                        }
                        return std::string("Learned Value G") +
                               std::to_string(
                                   selection.value_generation) +
                               value_continuation_epsilon_suffix(
                                   config
                                       .value_continuation_epsilon);
                    }
                    return old_school::bot_config_name(config);
                };
            std::string challenger_name =
                benchmark_name(challenger, challenger_config);
            if (challenger_config.value_pass_dominance) {
                challenger_name += " + exact Pass dominance";
            }
            print_benchmark(
                result, seed, challenger_name,
                benchmark_name(baseline, baseline_config));
            return result.challenger_is_better_95() ? 0 : 1;
        }
        if (stability) {
            return run_stability_panel(
                       stability_runs, games, seed,
                       training_seed, rollouts, deep_rollouts,
                       training_games, learned_rollouts,
                       learned_generations,
                       refresh_value_challenger_cache,
                       value_continuation_epsilon)
                       ? 0
                       : 1;
        }
        if (variance_study) {
            run_variance_study(
                games_were_set ? games : 5, training_games,
                rollouts, deep_rollouts);
            return 0;
        }
        if (evolve) {
            const std::size_t repetitions =
                games_were_set ? games : 4;
            auto evolution_pilot_config =
                bot_config(
                    evolve_pilot, rollouts, deep_rollouts,
                    training_games, learned_rollouts,
                    value_continuation_epsilon);
            old_school::GameConfig evolution_game_config;
            evolution_game_config.learned_training_seed =
                training_seed;
            std::string evolution_pilot_name =
                old_school::bot_config_name(
                    evolution_pilot_config);
            if (evolution_uses_learned) {
                const auto model =
                    train_value_context_challenger_with_progress(
                        training_games, training_seed,
                        evolve_pilot.value_generation,
                        refresh_value_challenger_cache);
                evolution_pilot_config.learned_model = model;
                evolution_game_config.learned_model = model;
                evolution_pilot_name =
                    "Learned Value Context C" +
                    std::to_string(
                        evolve_pilot.value_generation) +
                    " (K=" +
                    std::to_string(learned_rollouts) +
                    ", training seed " +
                    std::to_string(training_seed) + ", " +
                    std::to_string(training_games) +
                    " initial games)";
            }
            const auto result = old_school::evolve_deck(
                {
                    .generations = generations,
                    .population = population,
                    .repetitions_per_opponent = repetitions,
                    .pilot = evolution_pilot_config,
                },
                seed, evolution_game_config);
            print_evolution(result, seed, evolution_pilot_name);
            return 0;
        }

        old_school::GameConfig tournament_game_config;
        tournament_game_config.learned_training_seed =
            training_seed;
        if (bot_field == old_school::BotField::Mixed ||
            bot_field == old_school::BotField::Learned) {
            const auto model_variant =
                bot_field == old_school::BotField::Mixed
                    ? old_school::LearnedVariant::
                          ValueSearchChampion
                    : bot_field_learned_variant;
            tournament_game_config.learned_model =
                train_frozen_learned_model(
                    model_variant, training_games,
                    training_seed,
                    learned_generations,
                    refresh_value_challenger_cache);
        }
        old_school::TournamentConfig tournament_config;
        tournament_config.bot_field = bot_field;
        tournament_config.monte_carlo_rollouts = rollouts;
        tournament_config.deep_monte_carlo_rollouts =
            deep_rollouts;
        tournament_config.learned_rollouts = learned_rollouts;
        tournament_config.value_continuation_epsilon =
            value_continuation_epsilon;
        tournament_config.learned_training_games =
            training_games;
        tournament_config.learned_variant =
            bot_field == old_school::BotField::Mixed
                ? old_school::LearnedVariant::ValueSearchChampion
                : bot_field_learned_variant;
        const old_school::TournamentSummary result =
            old_school::run_tournament(
                games, seed, tournament_game_config,
                tournament_config);

        std::cout << std::fixed << std::setprecision(1)
                  << "Old School Magic Bot Simulator\n"
                  << "Evaluation seed: " << seed << '\n'
                  << "Training seed: " << training_seed << '\n'
                  << "Bot field: "
                  << bot_field_name(
                         bot_field,
                         bot_field_learned_variant,
                         learned_generations)
                  << '\n';
        if (bot_field == old_school::BotField::Mixed ||
            bot_field == old_school::BotField::MonteCarlo) {
            std::cout << "Monte Carlo rollouts per legal action: "
                      << rollouts << '\n';
        }
        if (bot_field == old_school::BotField::Mixed ||
            bot_field == old_school::BotField::DeepMonteCarlo) {
            std::cout
                << "Deep Monte Carlo rollouts per legal action: "
                << deep_rollouts << '\n';
        }
        if (bot_field == old_school::BotField::Mixed ||
            bot_field == old_school::BotField::Learned) {
            const auto model_variant =
                bot_field == old_school::BotField::Mixed
                    ? old_school::LearnedVariant::
                          ValueSearchChampion
                    : bot_field_learned_variant;
            std::cout << "Frozen learned model: ";
            if (model_variant ==
                    old_school::LearnedVariant::
                        ValueSearchChampion &&
                learned_generations > 0) {
                std::cout << "Learned Value Challenger C"
                          << learned_generations;
            } else if (
                model_variant ==
                old_school::LearnedVariant::
                    ValueSearchChampion) {
                std::cout << "Learned Value G0";
            } else {
                std::cout << old_school::learned_variant_name(
                                 model_variant);
            }
            std::cout
                      << ", seed " << training_seed << ", "
                      << training_games << " training games, K="
                      << learned_rollouts << '\n';
            if (value_continuation_epsilon != 0.0) {
                std::cout
                    << "Value continuation priority-action epsilon: "
                    << format_real(value_continuation_epsilon)
                    << " (root remains greedy)\n";
            }
        }
        std::cout
                  << "Games per matchup: " << result.games_per_matchup
                  << '\n'
                  << "Total games: " << result.total_games
                  << "\n\nMatchups\n";

        for (const auto& matchup : result.matchups) {
            const auto& first = matchup.result.decks[0];
            const auto& second = matchup.result.decks[1];
            std::cout << "  " << old_school::deck_name(matchup.first_deck)
                      << " vs "
                      << old_school::deck_name(matchup.second_deck) << ": "
                      << first.wins << '-' << second.wins << '-'
                      << matchup.result.draws << " ("
                      << first.win_rate() << "% / "
                      << second.win_rate() << "%)\n";
        }

        std::cout << "\nDeck statistics\n";
        for (std::size_t deck = 0; deck < result.decks.size(); ++deck) {
            const auto id = static_cast<old_school::DeckId>(deck);
            const std::string label =
                std::string(old_school::deck_name(id)) + " — " +
                std::string(old_school::deck_list(id));
            print_deck_stats(label, result.decks[deck]);
            if (deck + 1 != result.decks.size()) {
                std::cout << '\n';
            }
        }

        print_deck_bot_benefit(result);

        std::cout << "\nBot statistics\n";
        bool printed_bot = false;
        for (std::size_t bot = 0; bot < result.bots.size(); ++bot) {
            if (result.bots[bot].games == 0) {
                continue;
            }
            if (printed_bot) {
                std::cout << '\n';
            }
            const auto kind = static_cast<old_school::BotKind>(bot);
            if (kind == old_school::BotKind::Learned) {
                const auto learned_display = bot_config(
                    {.kind = old_school::BotKind::Learned,
                     .learned_variant =
                         bot_field == old_school::BotField::Mixed
                             ? old_school::LearnedVariant::
                                   ValueSearchChampion
                             : bot_field_learned_variant},
                    rollouts, deep_rollouts, training_games,
                    learned_rollouts);
                const std::string learned_label =
                    learned_display.learned_variant ==
                            old_school::LearnedVariant::
                                UnifiedActor
                        ? old_school::bot_config_name(
                              learned_display)
                        : learned_generations == 0
                              ? "Learned Value G0"
                              : "Learned Value Challenger C" +
                                    std::to_string(
                                        learned_generations);
                print_bot_stats(
                    learned_label,
                    result.bots[bot]);
            } else {
                print_bot_stats(old_school::bot_name(kind),
                                result.bots[bot]);
            }
            printed_bot = true;
        }
        bool printed_matchup = false;
        for (const auto& matchup : result.bot_matchups) {
            if (matchup.games == 0) {
                continue;
            }
            if (!printed_matchup) {
                std::cout << "\nDirect bot matchups\n";
                printed_matchup = true;
            }
            std::cout << "  " << old_school::bot_name(matchup.first_bot)
                      << " vs " << old_school::bot_name(matchup.second_bot)
                      << ": " << matchup.first_wins << '-'
                      << matchup.second_wins << '-' << matchup.draws
                      << " across " << matchup.games << " games ("
                      << matchup.first_win_rate() << "% / "
                      << matchup.second_win_rate() << "%)\n";
        }

        std::cout << "\nOverall\n"
                  << "  Draws: " << result.draws << '\n'
                  << "  Average individual turns: "
                  << result.average_turns() << '\n'
                  << "  Finishes by life total: "
                  << result.life_total_finishes << '\n'
                  << "  Finishes by empty library: "
                  << result.empty_library_finishes << '\n'
                  << "  Turn-limit draws: " << result.turn_limit_draws
                  << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}
