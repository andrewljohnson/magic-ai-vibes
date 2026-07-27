#include "old_school/dvr2_harvest.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/dvr1_replay.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <tuple>
#include <unistd.h>
#include <utility>

namespace old_school::dvr2_harvest {
namespace {

constexpr std::size_t kPlayers = 2;
constexpr std::size_t kTrainingGames = 800;
constexpr std::uint64_t kTrainingSeed = 424242;
constexpr std::size_t kSelfPlayGenerations = 16;
constexpr std::size_t kReferenceCostPerAction =
    2 * (probes::kBsrScoutWorlds +
         probes::kBsrConfirmationWorlds);

static_assert(kReferenceCostPerAction == 256);
static_assert(kExpectedPhysicalGames == 80);
static_assert(kExpectedOwnerPerspectives == 160);
static_assert(kProductionWorlds == 8);
static_assert(kLearnedValueSearchHorizonTurns == 4);
static_assert(probes::kBsrScoutWorlds == 64);
static_assert(probes::kBsrConfirmationWorlds == 64);
static_assert(probes::kBsrReferenceHorizon == 8);
static_assert(probes::kBsrReferenceEvaluationThreads == 4);

std::size_t deck_index(DeckId deck) {
    const std::size_t index = static_cast<std::size_t>(deck);
    if (index >= kDeckCount) {
        throw std::invalid_argument("DVR2 deck is out of range");
    }
    return index;
}

std::size_t stratum_index(Stratum stratum) {
    const std::size_t index =
        static_cast<std::size_t>(stratum);
    if (index >= kStratumCount) {
        throw std::invalid_argument(
            "DVR2 stratum is out of range");
    }
    return index;
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
    throw std::invalid_argument("unknown DVR2 deck");
}

Stratum candidate_stratum(const SelectionCandidate& candidate) {
    if (candidate.owner_deck == DeckId::Blue) {
        return candidate.top_controlled_by_owner
                   ? Stratum::BlueOwnTop
                   : Stratum::BlueOpponentTop;
    }
    return candidate.top_controlled_by_owner
               ? Stratum::NonBlueOwnTop
               : Stratum::NonBlueOpponentTop;
}

std::size_t quota_for(const SelectionCandidate& candidate) {
    switch (candidate_stratum(candidate)) {
    case Stratum::BlueOpponentTop:
        return 8;
    case Stratum::BlueOwnTop:
        return 2;
    case Stratum::NonBlueOpponentTop:
        return 4;
    case Stratum::NonBlueOwnTop:
        return 2;
    }
    throw std::invalid_argument("unknown DVR2 quota stratum");
}

using QuotaKey =
    std::tuple<std::size_t, std::size_t>;

QuotaKey quota_key(const SelectionCandidate& candidate) {
    const Stratum stratum = candidate_stratum(candidate);
    switch (stratum) {
    case Stratum::BlueOpponentTop:
    case Stratum::BlueOwnTop:
        return {
            stratum_index(stratum),
            deck_index(candidate.opponent_deck),
        };
    case Stratum::NonBlueOpponentTop:
    case Stratum::NonBlueOwnTop:
        return {
            stratum_index(stratum),
            deck_index(candidate.owner_deck),
        };
    }
    throw std::invalid_argument("unknown DVR2 quota key");
}

using OwnerGameKey =
    std::tuple<std::size_t, std::size_t, std::size_t>;

OwnerGameKey owner_game_key(
    const SelectionCandidate& candidate) {
    return {
        candidate.source.seed_base_index,
        candidate.source.schedule_index,
        candidate.source.owner_seat,
    };
}

auto provenance_key(const SelectionCandidate& candidate) {
    return std::tie(
        candidate.source.seed_base_index,
        candidate.source.schedule_index,
        candidate.source.owner_seat,
        candidate.source.trace_ordinal,
        candidate.information_action_fingerprint,
        candidate.source_index);
}

CoverageCell& coverage_for(
    SelectionReport& report,
    const SelectionCandidate& candidate) {
    return report.coverage.at(
        stratum_index(candidate_stratum(candidate)))
        .at(deck_index(candidate.owner_deck));
}

const CoverageCell& coverage_for(
    const SelectionReport& report, Stratum stratum,
    DeckId owner_deck) {
    return report.coverage.at(stratum_index(stratum))
        .at(deck_index(owner_deck));
}

bool quota_cells_met(const SelectionReport& report) {
    if (coverage_for(
            report, Stratum::BlueOpponentTop,
            DeckId::Blue)
            .selected != 32 ||
        coverage_for(
            report, Stratum::BlueOwnTop,
            DeckId::Blue)
            .selected != 8) {
        return false;
    }
    for (std::size_t owner = 0; owner < kDeckCount; ++owner) {
        const DeckId deck = static_cast<DeckId>(owner);
        if (deck == DeckId::Blue) {
            continue;
        }
        if (coverage_for(
                report, Stratum::NonBlueOpponentTop,
                deck)
                .selected != 4 ||
            coverage_for(
                report, Stratum::NonBlueOwnTop,
                deck)
                .selected != 2) {
            return false;
        }
    }
    return report.selected_indices.size() ==
           kMaximumPrimaryRoots;
}

std::string stratum_name(Stratum stratum) {
    switch (stratum) {
    case Stratum::BlueOpponentTop:
        return "blue-opponent-top";
    case Stratum::BlueOwnTop:
        return "blue-own-top";
    case Stratum::NonBlueOpponentTop:
        return "nonblue-opponent-top";
    case Stratum::NonBlueOwnTop:
        return "nonblue-own-top";
    }
    throw std::invalid_argument("unknown DVR2 stratum");
}

std::string classification_name(
    ReferenceClassification classification) {
    switch (classification) {
    case ReferenceClassification::StableDisagreement:
        return "stable-disagreement";
    case ReferenceClassification::StableAgreement:
        return "stable-agreement";
    case ReferenceClassification::UnstableBestSet:
        return "unstable-best-set";
    case ReferenceClassification::InvalidInvariance:
        return "invalid-invariance";
    }
    throw std::invalid_argument(
        "unknown DVR2 reference classification");
}

void append_text(std::string& output, std::string_view name,
                 std::string_view value) {
    output += name;
    output.push_back('\t');
    output += std::to_string(value.size());
    output.push_back(':');
    output.append(value);
    output.push_back('\n');
}

template <typename Value>
void append_scalar(std::string& output, std::string_view name,
                   Value value) {
    output += name;
    output.push_back('\t');
    if constexpr (std::is_same_v<Value, bool>) {
        output += value ? "1" : "0";
    } else {
        output += std::to_string(value);
    }
    output.push_back('\n');
}

void append_real(std::string& output, std::string_view name,
                 double value) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(
            "DVR2 bundle cannot contain a non-finite real");
    }
    std::ostringstream formatted;
    formatted.imbue(std::locale::classic());
    formatted << std::setprecision(
                     std::numeric_limits<double>::max_digits10)
              << value;
    append_text(output, name, formatted.str());
}

std::string indexed(std::string_view prefix,
                    std::size_t index) {
    return std::string(prefix) + "." + std::to_string(index);
}

void append_locator(std::string& output, std::string_view prefix,
                    const SourceLocator& source) {
    const std::string base(prefix);
    append_scalar(output, base + ".seed_base",
                  source.seed_base);
    append_scalar(output, base + ".seed_base_index",
                  source.seed_base_index);
    append_scalar(output, base + ".schedule_index",
                  source.schedule_index);
    append_scalar(output, base + ".pairing_index",
                  source.pairing_index);
    append_scalar(output, base + ".game_seed",
                  source.game_seed);
    append_scalar(output, base + ".owner_seat",
                  source.owner_seat);
    append_scalar(output, base + ".owner_on_play",
                  source.owner_on_play);
    append_scalar(output, base + ".starting_player",
                  source.starting_player);
    append_scalar(output, base + ".trace_ordinal",
                  source.trace_ordinal);
}

void append_string_vector(
    std::string& output, std::string_view prefix,
    const std::vector<std::string>& values) {
    append_scalar(
        output, std::string(prefix) + ".count", values.size());
    for (std::size_t index = 0; index < values.size();
         ++index) {
        append_text(
            output, indexed(prefix, index), values[index]);
    }
}

void append_score(std::string& output, std::string_view prefix,
                  const probes::BsrRootScore& score) {
    const std::string base(prefix);
    append_text(output, base + ".stable_id", score.stable_id);
    append_text(
        output, base + ".information_action_fingerprint",
        score.information_action_fingerprint);
    append_scalar(
        output, base + ".action_count", score.action_count);
    append_scalar(
        output, base + ".actual_action_index",
        score.actual_action_index);
    append_text(
        output, base + ".actual_action_descriptor",
        score.actual_action_descriptor);
    append_text(
        output, base + ".reference_model_fingerprint",
        score.reference_model_fingerprint);
    append_scalar(
        output, base + ".reference_seed_base",
        score.reference_seed_base);
    append_scalar(
        output, base + ".scout_seed", score.scout_seed);
    append_scalar(
        output, base + ".confirmation_seed",
        score.confirmation_seed);
    append_scalar(
        output, base + ".scout_worlds",
        score.scout_worlds);
    append_scalar(
        output, base + ".confirmation_worlds",
        score.confirmation_worlds);
    append_scalar(
        output, base + ".horizon_turns",
        score.horizon_turns);
    append_scalar(
        output, base + ".rollouts_per_world",
        score.rollouts_per_world);
    append_scalar(
        output, base + ".evaluation_threads",
        score.evaluation_threads);
    append_string_vector(
        output, base + ".scout_best_actions",
        score.scout_best_actions);
    append_string_vector(
        output, base + ".confirmation_best_actions",
        score.confirmation_best_actions);
    append_scalar(
        output, base + ".action_means.count",
        score.action_means.size());
    for (std::size_t index = 0;
         index < score.action_means.size(); ++index) {
        const std::string row =
            base + ".action_means." + std::to_string(index);
        append_text(
            output, row + ".descriptor",
            score.action_means[index].descriptor);
        append_real(
            output, row + ".scout_mean",
            score.action_means[index].scout_mean);
        append_real(
            output, row + ".confirmation_mean",
            score.action_means[index].confirmation_mean);
    }
    append_real(
        output, base + ".scout_actual_mean",
        score.scout_actual_mean);
    append_real(
        output, base + ".scout_best_mean",
        score.scout_best_mean);
    append_real(
        output, base + ".confirmation_actual_mean",
        score.confirmation_actual_mean);
    append_real(
        output, base + ".confirmation_best_mean",
        score.confirmation_best_mean);
    append_real(
        output, base + ".confirmation_regret",
        score.confirmation_regret);
    append_real(
        output, base + ".paired_standard_error",
        score.paired_standard_error);
    append_real(
        output, base + ".paired_lower_95",
        score.paired_lower_95);
    append_scalar(
        output, base + ".sampled_worlds",
        score.sampled_worlds);
    append_scalar(
        output, base + ".rollout_evaluations",
        score.rollout_evaluations);
    append_scalar(
        output, base + ".terminal_evaluations",
        score.terminal_evaluations);
    append_scalar(
        output, base + ".bootstrapped_evaluations",
        score.bootstrapped_evaluations);
    append_scalar(
        output, base + ".stable_best_set",
        score.scout_confirmation_best_set_stable);
    append_scalar(
        output, base + ".actual_outside_best_sets",
        score.actual_outside_best_sets);
    append_scalar(
        output, base + ".descriptor_order_invariant",
        score.descriptor_order_invariant);
    append_scalar(
        output, base + ".hidden_repartition_eligible",
        score.hidden_repartition_eligible);
    append_scalar(
        output, base + ".hidden_repartition_bit_identical",
        score.hidden_repartition_bit_identical);
    append_scalar(
        output, base + ".accounting_passed",
        score.accounting_passed);
}

std::optional<std::pair<std::string, std::string>>
parse_bundle_envelope(std::string_view bundle) {
    const auto read_text =
        [&](std::string_view name, std::size_t& cursor)
        -> std::optional<std::string> {
        if (cursor + name.size() + 1 > bundle.size() ||
            bundle.substr(cursor, name.size()) != name ||
            bundle[cursor + name.size()] != '\t') {
            return std::nullopt;
        }
        cursor += name.size() + 1;
        const std::size_t colon = bundle.find(':', cursor);
        if (colon == std::string_view::npos) {
            return std::nullopt;
        }
        std::size_t length = 0;
        const std::string token(
            bundle.substr(cursor, colon - cursor));
        if (token.empty() ||
            !std::all_of(
                token.begin(), token.end(),
                [](char character) {
                    return character >= '0' &&
                           character <= '9';
                }) ||
            (token.size() > 1 && token.front() == '0')) {
            return std::nullopt;
        }
        try {
            std::size_t consumed = 0;
            length = std::stoull(token, &consumed);
            if (consumed != token.size() ||
                std::to_string(length) != token) {
                return std::nullopt;
            }
        } catch (const std::exception&) {
            return std::nullopt;
        }
        cursor = colon + 1;
        if (length > bundle.size() - cursor) {
            return std::nullopt;
        }
        std::string result(bundle.substr(cursor, length));
        cursor += length;
        if (cursor >= bundle.size() || bundle[cursor] != '\n') {
            return std::nullopt;
        }
        ++cursor;
        return result;
    };

    std::size_t cursor = 0;
    const auto schema = read_text("schema", cursor);
    const auto digest = read_text("payload_sha256", cursor);
    const auto payload = read_text("payload", cursor);
    if (!schema || !digest || !payload ||
        cursor != bundle.size() ||
        *schema != kBundleSchema ||
        digest->size() != 64) {
        return std::nullopt;
    }
    return std::pair{*digest, *payload};
}

} // namespace

std::vector<ScheduledGame> source_schedule() {
    std::vector<ScheduledGame> result;
    result.reserve(kExpectedPhysicalGames);
    for (std::size_t seed_index = 0;
         seed_index < kSourceSeedBases.size(); ++seed_index) {
        const std::uint64_t seed_base =
            kSourceSeedBases[seed_index];
        const auto schedule =
            learned_iteration::balanced_schedule(
                seed_base, kGenerationNamespace, 0);
        for (const auto& scheduled : schedule) {
            result.push_back({
                .source = {
                    .seed_base = seed_base,
                    .seed_base_index = seed_index,
                    .schedule_index =
                        scheduled.schedule_index,
                    .pairing_index =
                        scheduled.pairing_index,
                    .game_seed = scheduled.seed,
                    .owner_seat = 0,
                    .owner_on_play =
                        scheduled.starting_player == 0,
                    .starting_player =
                        scheduled.starting_player,
                    .trace_ordinal = 0,
                },
                .seat_decks = scheduled.seat_decks,
            });
        }
    }
    if (result.size() != kExpectedPhysicalGames) {
        throw std::logic_error(
            "DVR2 source schedule size changed");
    }
    return result;
}

std::string source_schedule_sha256() {
    const auto schedule = source_schedule();
    std::string canonical;
    canonical.reserve(schedule.size() * 96);
    for (const ScheduledGame& game : schedule) {
        canonical += std::to_string(game.source.seed_base);
        canonical.push_back('\t');
        canonical +=
            std::to_string(game.source.seed_base_index);
        canonical.push_back('\t');
        canonical +=
            std::to_string(game.source.schedule_index);
        canonical.push_back('\t');
        canonical +=
            std::to_string(game.source.pairing_index);
        canonical.push_back('\t');
        canonical +=
            std::to_string(game.source.game_seed);
        canonical.push_back('\t');
        canonical +=
            std::to_string(game.source.starting_player);
        canonical.push_back('\t');
        canonical += std::to_string(
            static_cast<std::size_t>(
                game.seat_decks[0]));
        canonical.push_back('\t');
        canonical += std::to_string(
            static_cast<std::size_t>(
                game.seat_decks[1]));
        canonical.push_back('\n');
    }
    return artifact_integrity::sha256_string(canonical);
}

bool CoverageCell::cross_sum_valid() const {
    if (considered != eligible + action_cap_skipped) {
        return false;
    }
    if (selected >
            std::numeric_limits<std::size_t>::max() -
                duplicate_skipped ||
        selected + duplicate_skipped >
            std::numeric_limits<std::size_t>::max() -
                per_owner_game_skipped ||
        selected + duplicate_skipped +
                per_owner_game_skipped >
            std::numeric_limits<std::size_t>::max() -
                quota_skipped ||
        selected + duplicate_skipped +
                per_owner_game_skipped + quota_skipped >
            std::numeric_limits<std::size_t>::max() -
                budget_skipped) {
        return false;
    }
    return eligible ==
           selected + duplicate_skipped +
               per_owner_game_skipped + quota_skipped +
               budget_skipped;
}

SelectionReport select_roots(
    const std::vector<SelectionCandidate>& candidates) {
    SelectionReport result;
    std::vector<std::size_t> ordered(candidates.size());
    for (std::size_t index = 0; index < candidates.size();
         ++index) {
        const SelectionCandidate& candidate = candidates[index];
        deck_index(candidate.owner_deck);
        deck_index(candidate.opponent_deck);
        const auto exact_schedule =
            learned_iteration::balanced_schedule(
                candidate.source.seed_base,
                kGenerationNamespace, 0);
        const auto& scheduled = exact_schedule.at(
            candidate.source.schedule_index);
        if (candidate.source_index >= candidates.size() ||
            candidate.source.seed_base_index >=
                kSourceSeedBases.size() ||
            candidate.source.seed_base !=
                kSourceSeedBases[
                    candidate.source.seed_base_index] ||
            candidate.source.schedule_index >=
                learned_iteration::kBalancedScheduleGames ||
            candidate.source.owner_seat >= kPlayers ||
            candidate.source.starting_player >= kPlayers ||
            candidate.source.owner_on_play !=
                (candidate.source.owner_seat ==
                 candidate.source.starting_player) ||
            candidate.owner_deck ==
                candidate.opponent_deck ||
            candidate.source.pairing_index !=
                scheduled.pairing_index ||
            candidate.source.game_seed != scheduled.seed ||
            candidate.source.starting_player !=
                scheduled.starting_player ||
            candidate.owner_deck !=
                scheduled.seat_decks[
                    candidate.source.owner_seat] ||
            candidate.opponent_deck !=
                scheduled.seat_decks[
                    1 - candidate.source.owner_seat] ||
            candidate.information_action_fingerprint.empty()) {
            throw std::invalid_argument(
                "invalid DVR2 selection candidate");
        }
        ordered[index] = index;
    }
    std::sort(
        ordered.begin(), ordered.end(),
        [&](std::size_t left_index,
            std::size_t right_index) {
            const auto& left = candidates[left_index];
            const auto& right = candidates[right_index];
            return std::tuple{
                       stratum_index(
                           candidate_stratum(left)),
                       quota_key(left),
                       provenance_key(left)} <
                   std::tuple{
                       stratum_index(
                           candidate_stratum(right)),
                       quota_key(right),
                       provenance_key(right)};
        });

    // Frozen precedence: action cap -> global information/action
    // deduplication -> owner-game cap -> quota -> reference budget.
    std::set<std::string> seen_fingerprints;
    std::map<OwnerGameKey, std::size_t> owner_game_counts;
    std::map<QuotaKey, std::vector<std::size_t>>
        quota_candidates;
    for (const std::size_t index : ordered) {
        const SelectionCandidate& candidate =
            candidates[index];
        CoverageCell& coverage =
            coverage_for(result, candidate);
        if (candidate.action_count < 2) {
            ++result.below_action_floor;
            continue;
        }
        ++coverage.considered;
        if (candidate.action_count >
            kMaximumLegalActions) {
            ++coverage.action_cap_skipped;
            continue;
        }
        ++coverage.eligible;
        if (!seen_fingerprints
                 .insert(
                     candidate
                         .information_action_fingerprint)
                 .second) {
            ++coverage.duplicate_skipped;
            continue;
        }
        std::size_t& owner_count =
            owner_game_counts[owner_game_key(candidate)];
        if (owner_count >=
            kMaximumRootsPerOwnerGame) {
            ++coverage.per_owner_game_skipped;
            continue;
        }
        ++owner_count;
        quota_candidates[quota_key(candidate)]
            .push_back(index);
    }

    for (auto& [key, indices] : quota_candidates) {
        static_cast<void>(key);
        if (indices.empty()) {
            continue;
        }
        const SelectionCandidate& exemplar =
            candidates[indices.front()];
        const std::size_t quota = quota_for(exemplar);
        std::array<std::size_t, kDeckCount>
            opponent_uses{};
        std::array<std::size_t, kSourceSeedBases.size()>
            seed_uses{};
        std::array<std::size_t, kPlayers> seat_uses{};
        std::array<std::size_t, 2> play_uses{};
        std::vector<bool> retained(
            indices.size(), false);
        std::size_t selected = 0;
        while (selected < quota) {
            std::optional<std::size_t> best_position;
            for (std::size_t position = 0;
                 position < indices.size(); ++position) {
                if (retained[position]) {
                    continue;
                }
                if (!best_position.has_value()) {
                    best_position = position;
                    continue;
                }
                const SelectionCandidate& candidate =
                    candidates[indices[position]];
                const SelectionCandidate& best =
                    candidates[indices[*best_position]];
                const auto rank =
                    [&](const SelectionCandidate& item) {
                        const std::size_t opponent =
                            deck_index(
                                item.opponent_deck);
                        const std::size_t seed =
                            item.source.seed_base_index;
                        const std::size_t seat =
                            item.source.owner_seat;
                        const std::size_t play =
                            item.source.owner_on_play
                                ? 0U
                                : 1U;
                        return std::tuple{
                            opponent_uses[opponent],
                            seed_uses[seed],
                            seat_uses[seat],
                            play_uses[play],
                            provenance_key(item),
                        };
                    };
                if (rank(candidate) < rank(best)) {
                    best_position = position;
                }
            }
            if (!best_position.has_value()) {
                break;
            }
            retained[*best_position] = true;
            const std::size_t index =
                indices[*best_position];
            const SelectionCandidate& chosen =
                candidates[index];
            ++opponent_uses[
                deck_index(chosen.opponent_deck)];
            ++seed_uses[
                chosen.source.seed_base_index];
            ++seat_uses[
                chosen.source.owner_seat];
            ++play_uses[
                chosen.source.owner_on_play ? 0U : 1U];
            result.selected_indices.push_back(index);
            ++coverage_for(result, chosen).selected;
            ++selected;
        }
        for (std::size_t position = 0;
             position < indices.size(); ++position) {
            if (!retained[position]) {
                ++coverage_for(
                      result,
                      candidates[indices[position]])
                      .quota_skipped;
            }
        }
    }

    if (result.selected_indices.size() >
        kMaximumPrimaryRoots) {
        throw std::logic_error(
            "DVR2 selector exceeded the primary-root cap");
    }
    result.quotas_met = quota_cells_met(result);
    result.cross_sums_valid = true;
    for (const auto& stratum : result.coverage) {
        for (const CoverageCell& coverage : stratum) {
            result.cross_sums_valid =
                result.cross_sums_valid &&
                coverage.cross_sum_valid();
        }
    }
    return result;
}

ReferenceClassification classify_reference(
    const probes::BsrRootScore& score) {
    const bool invariant =
        score.action_count >= 2 &&
        score.action_count <= kMaximumLegalActions &&
        score.scout_worlds == probes::kBsrScoutWorlds &&
        score.confirmation_worlds ==
            probes::kBsrConfirmationWorlds &&
        score.horizon_turns ==
            probes::kBsrReferenceHorizon &&
        score.rollouts_per_world == 1 &&
        score.evaluation_threads ==
            probes::kBsrReferenceEvaluationThreads &&
        score.descriptor_order_invariant &&
        score.hidden_repartition_eligible &&
        score.hidden_repartition_bit_identical &&
        score.accounting_passed &&
        score.rollout_evaluations ==
            kReferenceCostPerAction * score.action_count &&
        score.terminal_evaluations <=
            score.rollout_evaluations &&
        score.bootstrapped_evaluations ==
            score.rollout_evaluations -
                score.terminal_evaluations &&
        score.scout_seed != score.confirmation_seed;
    if (!invariant) {
        return ReferenceClassification::InvalidInvariance;
    }
    if (!score.scout_confirmation_best_set_stable) {
        return ReferenceClassification::UnstableBestSet;
    }
    return score.actual_outside_best_sets
               ? ReferenceClassification::StableDisagreement
               : ReferenceClassification::StableAgreement;
}

std::string serialize_payload(const HarvestReport& report) {
    if (!report.valid || report.schema != kSchema ||
        report.model_artifact_path.empty() ||
        std::filesystem::path(
            report.model_artifact_path)
            .is_absolute() ||
        report.model_fingerprint !=
            kExpectedModelFingerprint ||
        report.reference_evaluations >
            kMaximumReferenceEvaluations ||
        !report.manifest_cross_sums_passed ||
        !report.reference_accounting_passed ||
        !reference_accounting_matches_evidence(
            report) ||
        !report.invariance_passed ||
        !report.replay_passed ||
        !report.watchdog_passed) {
        throw std::invalid_argument(
            "DVR2 payload requires a valid bounded report");
    }

    std::string output;
    output.reserve(64 * 1024);
    append_text(output, "schema", report.schema);
    append_text(
        output, "environment_revision",
        probes::kBsrEnvironmentRevision);
    append_text(
        output, "model.artifact_path",
        report.model_artifact_path);
    append_text(
        output, "model.artifact_sha256",
        report.model_artifact_sha256);
    append_text(
        output, "model.fingerprint",
        report.model_fingerprint);
    append_scalar(
        output, "protocol.training_games",
        kTrainingGames);
    append_scalar(
        output, "protocol.training_seed",
        kTrainingSeed);
    append_scalar(
        output, "protocol.self_play_generations",
        kSelfPlayGenerations);
    append_scalar(
        output, "protocol.generation_namespace",
        kGenerationNamespace);
    append_scalar(
        output, "protocol.source_turn_cap",
        kSourceTurnCap);
    append_scalar(
        output, "protocol.production_worlds",
        kProductionWorlds);
    append_scalar(
        output, "protocol.production_horizon",
        kLearnedValueSearchHorizonTurns);
    append_real(
        output, "protocol.production_epsilon", 0.0);
    append_real(
        output, "protocol.production_residual", 0.0);
    append_scalar(
        output, "protocol.production_pass_dominance",
        false);
    append_text(
        output, "protocol.production_controller",
        "Legacy");
    append_scalar(
        output, "protocol.reference_seed",
        probes::kBsrReferenceSeed);
    append_scalar(
        output, "protocol.reference_scout_worlds",
        probes::kBsrScoutWorlds);
    append_scalar(
        output, "protocol.reference_confirmation_worlds",
        probes::kBsrConfirmationWorlds);
    append_scalar(
        output, "protocol.reference_horizon",
        probes::kBsrReferenceHorizon);
    append_scalar(
        output, "protocol.reference_rollouts_per_world",
        1);
    append_scalar(
        output, "protocol.reference_threads",
        probes::kBsrReferenceEvaluationThreads);
    append_scalar(
        output, "protocol.reference_evaluation_cap",
        kMaximumReferenceEvaluations);
    append_scalar(
        output, "protocol.source_seed_bases.count",
        kSourceSeedBases.size());
    for (std::size_t index = 0;
         index < kSourceSeedBases.size(); ++index) {
        append_scalar(
            output,
            "protocol.source_seed_bases." +
                std::to_string(index),
            kSourceSeedBases[index]);
    }
    append_text(
        output, "protocol.source_schedule_sha256",
        source_schedule_sha256());

    append_scalar(
        output, "counts.physical_games",
        report.physical_games);
    append_scalar(
        output, "counts.owner_game_perspectives",
        report.owner_game_perspectives);
    append_scalar(
        output, "counts.traced_priority_roots",
        report.traced_priority_roots);
    append_scalar(
        output, "counts.selected_roots",
        report.selected_roots);
    append_scalar(
        output, "counts.stable_disagreements",
        report.stable_disagreements);
    append_scalar(
        output, "counts.stable_agreements",
        report.stable_agreements);
    append_scalar(
        output, "counts.unstable_best_sets",
        report.unstable_best_sets);
    append_scalar(
        output, "counts.invalid_invariance",
        report.invalid_invariance);
    append_scalar(
        output,
        "counts.blue_opponent_top_disagreements",
        report.blue_opponent_top_disagreements);
    append_scalar(
        output,
        "counts.blue_opponent_top_high_cost",
        report.blue_opponent_top_high_cost);
    append_scalar(
        output, "counts.reference_score_calls",
        report.reference_score_calls);
    append_scalar(
        output, "counts.reference_evaluations",
        report.reference_evaluations);
    append_scalar(
        output,
        "counts.reference_terminal_evaluations",
        report.reference_terminal_evaluations);
    append_scalar(
        output,
        "counts.reference_bootstrapped_evaluations",
        report.reference_bootstrapped_evaluations);
    append_scalar(
        output, "selection.below_action_floor",
        report.selection.below_action_floor);

    for (std::size_t stratum = 0;
         stratum < kStratumCount; ++stratum) {
        for (std::size_t deck = 0;
             deck < kDeckCount; ++deck) {
            const CoverageCell& cell =
                report.selection.coverage[stratum][deck];
            const std::string prefix =
                "coverage." + std::to_string(stratum) +
                "." + std::to_string(deck);
            append_text(
                output, prefix + ".stratum",
                stratum_name(
                    static_cast<Stratum>(stratum)));
            append_text(
                output, prefix + ".owner_deck",
                deck_name(static_cast<DeckId>(deck)));
            append_scalar(
                output, prefix + ".considered",
                cell.considered);
            append_scalar(
                output, prefix + ".eligible",
                cell.eligible);
            append_scalar(
                output, prefix + ".selected",
                cell.selected);
            append_scalar(
                output, prefix + ".duplicate_skipped",
                cell.duplicate_skipped);
            append_scalar(
                output,
                prefix + ".per_owner_game_skipped",
                cell.per_owner_game_skipped);
            append_scalar(
                output, prefix + ".action_cap_skipped",
                cell.action_cap_skipped);
            append_scalar(
                output, prefix + ".quota_skipped",
                cell.quota_skipped);
            append_scalar(
                output, prefix + ".budget_skipped",
                cell.budget_skipped);
        }
    }

    for (std::size_t deck = 0; deck < kDeckCount;
         ++deck) {
        const AgreementControl& control =
            report.controls[deck];
        const std::string prefix =
            "control." + std::to_string(deck);
        append_text(
            output, prefix + ".owner_deck",
            deck_name(static_cast<DeckId>(deck)));
        append_scalar(
            output, prefix + ".retained",
            control.retained);
        append_scalar(
            output, prefix + ".budget_skipped",
            control.budget_skipped);
        append_scalar(
            output, prefix + ".action_count",
            control.action_count);
        if (control.retained || control.budget_skipped) {
            append_locator(
                output, prefix + ".source",
                control.source);
            append_text(
                output,
                prefix +
                    ".information_action_fingerprint",
                control
                    .information_action_fingerprint);
        }
    }

    for (std::size_t owner = 0; owner < kDeckCount;
         ++owner) {
        for (std::size_t seat = 0; seat < kPlayers;
             ++seat) {
            for (std::size_t play = 0; play < 2; ++play) {
                append_scalar(
                    output,
                    "source_balance.owner." +
                        std::to_string(owner) +
                        ".seat." + std::to_string(seat) +
                        ".play." + std::to_string(play),
                    report.owner_deck_seat_play_draw
                        [owner][seat][play]);
            }
        }
        for (std::size_t opponent = 0;
             opponent < kDeckCount; ++opponent) {
            append_scalar(
                output,
                "source_balance.matchup." +
                    std::to_string(owner) + "." +
                    std::to_string(opponent),
                report.ordered_matchups[owner][opponent]);
        }
    }
    for (std::size_t seed_index = 0;
         seed_index < kSourceSeedBases.size();
         ++seed_index) {
        append_scalar(
            output,
            "source_balance.seed." +
                std::to_string(seed_index) +
                ".physical_games",
            report.physical_games_by_seed_base[seed_index]);
        for (std::size_t owner = 0;
             owner < kDeckCount; ++owner) {
            for (std::size_t opponent = 0;
                 opponent < kDeckCount; ++opponent) {
                for (std::size_t seat = 0;
                     seat < kPlayers; ++seat) {
                    for (std::size_t play = 0;
                         play < 2; ++play) {
                        append_scalar(
                            output,
                            "source_balance.seed." +
                                std::to_string(
                                    seed_index) +
                                ".owner." +
                                std::to_string(owner) +
                                ".opponent." +
                                std::to_string(
                                    opponent) +
                                ".seat." +
                                std::to_string(seat) +
                                ".play." +
                                std::to_string(play),
                            report.source_balance_cells
                                [seed_index][owner]
                                [opponent][seat][play]);
                    }
                }
            }
        }
    }

    append_scalar(
        output, "roots.count", report.roots.size());
    for (std::size_t index = 0;
         index < report.roots.size(); ++index) {
        const RootEvidence& root = report.roots[index];
        const std::string prefix =
            "root." + std::to_string(index);
        append_locator(
            output, prefix + ".source", root.source);
        append_text(
            output, prefix + ".owner_deck",
            deck_name(root.owner_deck));
        append_text(
            output, prefix + ".opponent_deck",
            deck_name(root.opponent_deck));
        append_text(
            output, prefix + ".stratum",
            stratum_name(root.stratum));
        append_text(
            output,
            prefix +
                ".information_action_fingerprint",
            root.information_action_fingerprint);
        append_text(
            output,
            prefix +
                ".production_action_descriptor",
            root.production_action_descriptor);
        append_text(
            output, prefix + ".classification",
            classification_name(root.classification));
        append_score(
            output, prefix + ".reference", root.score);
        append_text(
            output, prefix + ".dvr1_record",
            root.dvr1_record);
        append_text(
            output,
            prefix + ".dvr1_record_fingerprint",
            root.dvr1_record_fingerprint);
        append_scalar(
            output, prefix + ".decoded_replay_exact",
            root.decoded_replay_exact);
        append_scalar(
            output,
            prefix +
                ".reversed_single_thread_repeat_exact",
            root.reversed_single_thread_repeat_exact);
        append_scalar(
            output,
            prefix +
                ".regenerated_canonical_repeat_exact",
            root.regenerated_canonical_repeat_exact);
        append_scalar(
            output,
            prefix + ".repeat_reference_agreement",
            root.repeat_reference_agreement);
    }

    append_scalar(
        output, "gate.exact_source_schedule",
        report.exact_source_schedule);
    append_scalar(
        output, "gate.source_balance",
        report.source_balance_passed);
    append_scalar(
        output, "gate.source_policy_exact",
        report.source_policy_exact);
    append_scalar(
        output, "gate.model_identity",
        report.model_identity_passed);
    append_scalar(
        output, "gate.manifest_cross_sums",
        report.manifest_cross_sums_passed);
    append_scalar(
        output, "gate.coverage",
        report.coverage_gate_passed);
    append_scalar(
        output, "gate.reference_accounting",
        report.reference_accounting_passed);
    append_scalar(
        output, "gate.invariance",
        report.invariance_passed);
    append_scalar(
        output, "gate.replay", report.replay_passed);
    append_scalar(
        output, "gate.controls", report.controls_passed);
    append_scalar(
        output,
        "gate.controls_underpowered_only_by_budget",
        report.controls_underpowered_only_by_budget);
    append_scalar(
        output, "gate.watchdog",
        report.watchdog_passed);
    append_scalar(output, "gate.valid", report.valid);
    append_scalar(
        output, "gate.rs1_licensed",
        report.rs1_licensed);
    append_scalar(
        output, "errors.count", report.errors.size());
    for (std::size_t index = 0;
         index < report.errors.size(); ++index) {
        append_text(
            output, "errors." + std::to_string(index),
            report.errors[index]);
    }
    return output;
}

std::string make_checksummed_bundle(
    std::string_view payload) {
    if (payload.empty()) {
        throw std::invalid_argument(
            "DVR2 payload must not be empty");
    }
    const std::string digest =
        artifact_integrity::sha256_string(payload);
    std::string bundle;
    bundle.reserve(
        payload.size() + digest.size() + 128);
    append_text(bundle, "schema", kBundleSchema);
    append_text(bundle, "payload_sha256", digest);
    append_text(bundle, "payload", payload);
    return bundle;
}

bool verify_checksummed_bundle(std::string_view bundle) {
    const auto parsed = parse_bundle_envelope(bundle);
    if (!parsed.has_value()) {
        return false;
    }
    return parsed->first ==
           artifact_integrity::sha256_string(
               parsed->second);
}

namespace {

void write_bundle_atomic_no_replace_impl(
    const std::filesystem::path& path,
    std::string_view bundle,
    testing::PublicationFault fault) {
    if (path.empty() || path.filename().empty() ||
        path.string().find('\0') != std::string::npos ||
        !verify_checksummed_bundle(bundle) ||
        (fault != testing::PublicationFault::None &&
         fault != testing::PublicationFault::
                      AfterLinkBeforeDirectorySync)) {
        throw std::invalid_argument(
            "invalid DVR2 bundle publication request");
    }
    std::error_code target_error;
    const auto target_status =
        std::filesystem::symlink_status(path, target_error);
    if (target_error &&
        target_error !=
            std::errc::no_such_file_or_directory) {
        throw std::system_error(
            target_error,
            "cannot preflight DVR2 publication destination");
    }
    if (!target_error &&
        target_status.type() !=
            std::filesystem::file_type::not_found) {
        throw std::runtime_error(
            "DVR2 bundle destination already exists: '" +
            path.string() + "'");
    }
    const std::filesystem::path directory =
        path.has_parent_path()
            ? path.parent_path()
            : std::filesystem::path(".");
    std::error_code directory_error;
    std::filesystem::create_directories(
        directory, directory_error);
    if (directory_error) {
        throw std::runtime_error(
            "cannot create DVR2 bundle directory: " +
            directory_error.message());
    }

    static std::atomic<std::uint64_t> counter{0};
    std::filesystem::path temporary;
    int descriptor = -1;
    for (std::size_t attempt = 0; attempt < 128; ++attempt) {
        temporary =
            directory /
            (path.filename().string() + ".tmp." +
             std::to_string(
                 static_cast<unsigned long long>(::getpid())) +
             "." +
             std::to_string(
                 counter.fetch_add(
                     1, std::memory_order_relaxed)));
        descriptor = ::open(
            temporary.c_str(),
            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
            0644);
        if (descriptor >= 0) {
            break;
        }
        if (errno != EEXIST) {
            throw std::runtime_error(
                "cannot create temporary DVR2 bundle: " +
                std::string(std::strerror(errno)));
        }
    }
    if (descriptor < 0) {
        throw std::runtime_error(
            "could not reserve a temporary DVR2 bundle");
    }
    const auto cleanup = [&] {
        if (descriptor >= 0) {
            static_cast<void>(::close(descriptor));
            descriptor = -1;
        }
        static_cast<void>(::unlink(temporary.c_str()));
    };

    std::size_t cursor = 0;
    while (cursor < bundle.size()) {
        const ssize_t written = ::write(
            descriptor, bundle.data() + cursor,
            bundle.size() - cursor);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            const std::string detail = std::strerror(errno);
            cleanup();
            throw std::runtime_error(
                "cannot write temporary DVR2 bundle: " +
                detail);
        }
        if (written == 0) {
            cleanup();
            throw std::runtime_error(
                "temporary DVR2 write made no progress");
        }
        cursor += static_cast<std::size_t>(written);
    }
    if (::fsync(descriptor) != 0) {
        const std::string detail = std::strerror(errno);
        cleanup();
        throw std::runtime_error(
            "cannot sync temporary DVR2 bundle: " +
            detail);
    }
    if (::close(descriptor) != 0) {
        const std::string detail = std::strerror(errno);
        descriptor = -1;
        static_cast<void>(::unlink(temporary.c_str()));
        throw std::runtime_error(
            "cannot close temporary DVR2 bundle: " +
            detail);
    }
    descriptor = -1;

    const int directory_descriptor = ::open(
        directory.c_str(), O_RDONLY | O_CLOEXEC);
    if (directory_descriptor < 0) {
        const std::string detail = std::strerror(errno);
        static_cast<void>(::unlink(temporary.c_str()));
        throw std::runtime_error(
            "cannot open DVR2 publication directory: " +
            detail);
    }
    if (::link(temporary.c_str(), path.c_str()) != 0) {
        const int link_error = errno;
        static_cast<void>(::close(directory_descriptor));
        static_cast<void>(::unlink(temporary.c_str()));
        if (link_error == EEXIST) {
            throw std::runtime_error(
                "DVR2 bundle destination already exists: '" +
                path.string() + "'");
        }
        throw std::runtime_error(
            "cannot atomically publish DVR2 bundle: " +
            std::string(std::strerror(link_error)));
    }
    const auto rollback_target = [&] {
        if (::unlink(path.c_str()) != 0) {
            return false;
        }
        static_cast<void>(
            ::fsync(directory_descriptor));
        return true;
    };
    if (fault == testing::PublicationFault::
                     AfterLinkBeforeDirectorySync) {
        const bool rolled_back = rollback_target();
        static_cast<void>(::unlink(temporary.c_str()));
        static_cast<void>(::close(directory_descriptor));
        if (rolled_back) {
            throw std::runtime_error(
                "injected DVR2 failure after link");
        }
        // If rollback itself failed, the target exists and publication is
        // irrevocably successful. Never report exit 2 with evidence present.
        return;
    }
    if (::unlink(temporary.c_str()) != 0) {
        const std::string detail = std::strerror(errno);
        const bool rolled_back = rollback_target();
        static_cast<void>(::close(directory_descriptor));
        if (rolled_back) {
            throw std::runtime_error(
                "cannot remove linked DVR2 temporary: " +
                detail);
        }
        return;
    }
    if (::fsync(directory_descriptor) != 0) {
        const std::string detail = std::strerror(errno);
        const bool rolled_back = rollback_target();
        static_cast<void>(::close(directory_descriptor));
        if (rolled_back) {
            throw std::runtime_error(
                "cannot sync DVR2 publication directory: " +
                detail);
        }
        return;
    }
    // The target is durable after the successful directory fsync. A close-only
    // anomaly cannot turn this into an exit-2 result with evidence present.
    static_cast<void>(::close(directory_descriptor));
}

} // namespace

void write_bundle_atomic_no_replace(
    const std::filesystem::path& path,
    std::string_view bundle) {
    write_bundle_atomic_no_replace_impl(
        path, bundle,
        testing::PublicationFault::None);
}

namespace testing {

void write_bundle_atomic_no_replace(
    const std::filesystem::path& path,
    std::string_view bundle,
    PublicationFault fault) {
    write_bundle_atomic_no_replace_impl(
        path, bundle, fault);
}

} // namespace testing

int RunResult::exit_code() const noexcept {
    if (!report.valid || !published) {
        return 2;
    }
    return report.rs1_licensed ? 0 : 1;
}

namespace {

struct PreparedRoot {
    probes::DecisionProbe probe;
    SourceLocator source;
    DeckId owner_deck = DeckId::Green;
    DeckId opponent_deck = DeckId::Red;
    bool top_controlled_by_owner = false;
    std::string information_action_fingerprint;
    std::string production_action_descriptor;
    probes::BsrRootKeyContext provenance;
};

class RunGuard {
  public:
    RunGuard() {
        if (active_.exchange(true)) {
            throw std::logic_error(
                "recursive DVR2 harvest invocation");
        }
    }

    ~RunGuard() {
        active_.store(false);
    }

    RunGuard(const RunGuard&) = delete;
    RunGuard& operator=(const RunGuard&) = delete;

  private:
    static std::atomic<bool> active_;
};

std::atomic<bool> RunGuard::active_{false};

GameConfig source_game_config(
    const std::shared_ptr<const LearnedModel>& model,
    std::size_t starting_player) {
    const BotConfig c16{
        .kind = BotKind::Learned,
        .learned_variant =
            LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = kProductionWorlds,
        .exploration_rate = 0.0,
        .value_continuation_epsilon = 0.0,
        .value_priority_residual_weight = 0.0,
        .value_pass_dominance = false,
        .value_continuation_controller =
            LearnedContinuationController::Legacy,
        .training_games = kTrainingGames,
        .learned_model = model,
    };
    return {
        .max_turns = kSourceTurnCap,
        .starting_player = starting_player,
        .bots = {c16, c16},
        .learned_training_seed = kTrainingSeed,
        .learned_model = model,
        .learned_search_depth = 1,
    };
}

bool source_policy_exact(const GameConfig& config,
                         const std::shared_ptr<const LearnedModel>& model) {
    if (config.max_turns != kSourceTurnCap ||
        !config.starting_player.has_value() ||
        config.learned_training_seed != kTrainingSeed ||
        config.learned_model != model ||
        config.learned_search_depth != 1) {
        return false;
    }
    for (const BotConfig& bot : config.bots) {
        if (bot.kind != BotKind::Learned ||
            bot.learned_variant !=
                LearnedVariant::ValueSearchChampion ||
            bot.rollouts_per_action != kProductionWorlds ||
            bot.exploration_rate != 0.0 ||
            bot.value_continuation_epsilon != 0.0 ||
            bot.value_priority_residual_weight != 0.0 ||
            bot.value_pass_dominance ||
            bot.value_continuation_controller !=
                LearnedContinuationController::Legacy ||
            bot.training_games != kTrainingGames ||
            bot.learned_model != model) {
            return false;
        }
    }
    return true;
}

std::string source_root_id(
    const SourceLocator& source,
    std::string_view information_action_fingerprint) {
    return "dvr2.s" +
           std::to_string(source.seed_base_index) +
           ".g" + std::to_string(source.schedule_index) +
           ".p" + std::to_string(source.owner_seat) +
           ".r" + std::to_string(source.trace_ordinal) +
           ".k" + std::string(information_action_fingerprint);
}

PreparedRoot prepare_root(
    const ScheduledGame& scheduled,
    std::size_t owner,
    std::size_t trace_ordinal,
    const LearnedDecisionTracePoint& point,
    const std::array<std::vector<CardId>, 2>& decks) {
    if (!point.context.valid ||
        point.context.decision_player != owner ||
        owner >= kPlayers || point.state.stack.empty() ||
        point.state.stack.back().controller >= kPlayers) {
        throw std::invalid_argument(
            "DVR2 cannot prepare an invalid stack root");
    }
    const std::vector<PriorityAction> actions =
        legal_priority_actions(
            point.state, owner,
            point.context.sorcery_actions);
    if (!point.selected_priority_action.has_value() ||
        std::count(
            actions.begin(), actions.end(),
            *point.selected_priority_action) != 1) {
        throw std::logic_error(
            "DVR2 source trace lost its exact selected action");
    }

    SourceLocator source = scheduled.source;
    source.owner_seat = owner;
    source.owner_on_play =
        owner == source.starting_player;
    source.trace_ordinal = trace_ordinal;
    const DeckId owner_deck =
        scheduled.seat_decks[owner];
    const DeckId opponent_deck =
        scheduled.seat_decks[1 - owner];

    probes::DecisionProbe probe;
    probe.category = probes::Category::BlueCounterWar;
    probe.decision_kind = probes::DecisionKind::Priority;
    probe.root_deck = owner_deck;
    probe.opponent_deck = opponent_deck;
    probe.root_player = owner;
    probe.phase = point.context.phase;
    probe.consecutive_passes =
        point.context.consecutive_passes;
    probe.state = point.state;
    probe.original_decks = decks;
    probe.candidates.reserve(actions.size());
    std::set<std::string> descriptors;
    std::string production_descriptor;
    for (const PriorityAction& action : actions) {
        const std::string descriptor =
            probes::stable_priority_action_descriptor(action);
        if (!descriptors.insert(descriptor).second) {
            throw std::logic_error(
                "DVR2 source has duplicate action descriptors");
        }
        probe.candidates.push_back({
            .descriptor = descriptor,
            .action = action,
        });
        if (action == *point.selected_priority_action) {
            production_descriptor = descriptor;
        }
    }
    if (production_descriptor.empty()) {
        throw std::logic_error(
            "DVR2 source selected descriptor is empty");
    }
    probe.harvest = probes::HarvestProvenance{
        .collector =
            "Game::run_with_priority_root_trace",
        .trajectory_script =
            "dvr2-frozen-c16-learned-mirror-v1",
        .game_seed = source.game_seed,
        .starting_player = source.starting_player,
        .priority_decision_ordinal = trace_ordinal,
        .turn_number = point.state.turn_number,
        .phase = point.context.phase,
    };
    const std::string fingerprint =
        probes::bsr_information_action_fingerprint(probe);
    probe.stable_id =
        source_root_id(source, fingerprint);

    if (actions.size() >= 2 &&
        actions.size() <= kMaximumLegalActions) {
        const probes::Validation validation =
            probes::validate_probe(probe);
        if (!validation.ok()) {
            std::ostringstream detail;
            detail << "DVR2 source probe validation failed";
            for (const std::string& error :
                 validation.errors) {
                detail << "; " << error;
            }
            throw std::logic_error(detail.str());
        }
    }

    return {
        .probe = std::move(probe),
        .source = source,
        .owner_deck = owner_deck,
        .opponent_deck = opponent_deck,
        .top_controlled_by_owner =
            point.state.stack.back().controller == owner,
        .information_action_fingerprint = fingerprint,
        .production_action_descriptor =
            production_descriptor,
        .provenance = {
            .game_seed = source.game_seed,
            .block = 0,
            .schedule_index = source.schedule_index,
            .tracked_seat = owner,
            .tracked_starts = source.owner_on_play,
            .trace_ordinal = trace_ordinal,
        },
    };
}

std::vector<PreparedRoot> run_source_game(
    const ScheduledGame& scheduled,
    const std::shared_ptr<const LearnedModel>& model,
    bool* policy_exact) {
    const std::array<std::vector<CardId>, 2> decks = {
        cards_for_deck(scheduled.seat_decks[0]),
        cards_for_deck(scheduled.seat_decks[1]),
    };
    const GameConfig config = source_game_config(
        model, scheduled.source.starting_player);
    *policy_exact =
        *policy_exact && source_policy_exact(config, model);
    Game game(
        decks[0], decks[1], scheduled.source.game_seed,
        config);
    std::vector<LearnedDecisionTracePoint> trace;
    const GameResult result =
        game.run_with_priority_root_trace(trace);
    if (result.starting_player !=
        scheduled.source.starting_player) {
        throw std::logic_error(
            "DVR2 source starter changed");
    }

    std::vector<PreparedRoot> roots;
    for (std::size_t ordinal = 0;
         ordinal < trace.size(); ++ordinal) {
        const auto& point = trace[ordinal];
        if (!point.context.valid ||
            point.context.decision_player >= kPlayers ||
            point.state.stack.empty()) {
            continue;
        }
        roots.push_back(prepare_root(
            scheduled, point.context.decision_player,
            ordinal, point, decks));
    }
    return roots;
}

const ScheduledGame& scheduled_for(
    const std::vector<ScheduledGame>& schedule,
    const SourceLocator& source) {
    const auto found = std::find_if(
        schedule.begin(), schedule.end(),
        [&](const ScheduledGame& candidate) {
            return candidate.source.seed_base_index ==
                       source.seed_base_index &&
                   candidate.source.schedule_index ==
                       source.schedule_index &&
                   candidate.source.game_seed ==
                       source.game_seed;
        });
    if (found == schedule.end()) {
        throw std::logic_error(
            "DVR2 source locator is absent from schedule");
    }
    return *found;
}

PreparedRoot regenerate_root(
    const std::vector<ScheduledGame>& schedule,
    const PreparedRoot& original,
    const std::shared_ptr<const LearnedModel>& model,
    bool* policy_exact) {
    const ScheduledGame& scheduled =
        scheduled_for(schedule, original.source);
    const auto roots =
        run_source_game(scheduled, model, policy_exact);
    const auto found = std::find_if(
        roots.begin(), roots.end(),
        [&](const PreparedRoot& candidate) {
            return candidate.source.owner_seat ==
                       original.source.owner_seat &&
                   candidate.source.trace_ordinal ==
                       original.source.trace_ordinal;
        });
    if (found == roots.end()) {
        throw std::logic_error(
            "DVR2 control root did not regenerate");
    }
    if (found->source != original.source ||
        found->owner_deck != original.owner_deck ||
        found->opponent_deck !=
            original.opponent_deck ||
        found->top_controlled_by_owner !=
            original.top_controlled_by_owner ||
        found->information_action_fingerprint !=
            original.information_action_fingerprint ||
        found->production_action_descriptor !=
            original.production_action_descriptor ||
        found->probe.candidates !=
            original.probe.candidates) {
        throw std::logic_error(
            "DVR2 control regeneration changed the root");
    }
    return *found;
}

probes::BsrReferenceConfig canonical_reference_config() {
    return {
        .seed = probes::kBsrReferenceSeed,
        .scout_worlds = probes::kBsrScoutWorlds,
        .confirmation_worlds =
            probes::kBsrConfirmationWorlds,
        .horizon_turns =
            probes::kBsrReferenceHorizon,
        .rollouts_per_world = 1,
        .evaluation_threads =
            probes::kBsrReferenceEvaluationThreads,
    };
}

} // namespace

std::size_t reference_score_cost(std::size_t actions) {
    if (actions < 2 || actions > kMaximumLegalActions ||
        actions >
            std::numeric_limits<std::size_t>::max() /
                kReferenceCostPerAction) {
        throw std::invalid_argument(
            "DVR2 score action count is invalid");
    }
    return kReferenceCostPerAction * actions;
}

std::size_t worst_case_reference_score_calls(
    bool owner_control_already_retained) noexcept {
    return owner_control_already_retained ? 2U : 3U;
}

bool can_reserve_worst_case_reference_path(
    std::size_t evaluations_already_charged,
    std::size_t action_count,
    bool owner_control_already_retained,
    std::size_t evaluation_cap) {
    const std::size_t score_cost =
        reference_score_cost(action_count);
    const std::size_t calls =
        worst_case_reference_score_calls(
            owner_control_already_retained);
    return score_cost <=
               std::numeric_limits<std::size_t>::max() /
                   calls &&
           calls * score_cost <= evaluation_cap &&
           evaluations_already_charged <=
               evaluation_cap - calls * score_cost;
}

bool ReferenceBudgetLedger::can_reserve(
    std::size_t action_count,
    std::size_t maximum_score_calls) const {
    if (maximum_score_calls == 0) {
        throw std::invalid_argument(
            "DVR2 reservation needs at least one score call");
    }
    const std::size_t cost =
        reference_score_cost(action_count);
    return cost <=
               std::numeric_limits<std::size_t>::max() /
                   maximum_score_calls &&
           maximum_score_calls * cost <= evaluation_cap &&
           evaluations <=
               evaluation_cap -
                   maximum_score_calls * cost;
}

void ReferenceBudgetLedger::charge(
    const probes::BsrRootScore& score) {
    const std::size_t expected_cost =
        reference_score_cost(score.action_count);
    if (score.rollout_evaluations != expected_cost ||
        score.terminal_evaluations >
            score.rollout_evaluations ||
        score.bootstrapped_evaluations !=
            score.rollout_evaluations -
                score.terminal_evaluations ||
        score.rollout_evaluations > evaluation_cap ||
        evaluations >
            evaluation_cap -
                score.rollout_evaluations ||
        score_calls ==
            std::numeric_limits<std::size_t>::max()) {
        throw std::logic_error(
            "DVR2 reference accounting or cap failed");
    }
    ++score_calls;
    evaluations += score.rollout_evaluations;
    terminal_evaluations +=
        score.terminal_evaluations;
    bootstrapped_evaluations +=
        score.bootstrapped_evaluations;
}

bool ReferenceBudgetLedger::accounting_valid() const noexcept {
    return evaluations <= evaluation_cap &&
           terminal_evaluations <= evaluations &&
           bootstrapped_evaluations ==
               evaluations - terminal_evaluations;
}

bool semantic_control_score_equal(
    const probes::BsrRootScore& canonical_four_thread,
    const probes::BsrRootScore& reversed_one_thread) {
    if (canonical_four_thread.evaluation_threads !=
            probes::kBsrReferenceEvaluationThreads ||
        reversed_one_thread.evaluation_threads != 1) {
        return false;
    }
    probes::BsrRootScore normalized =
        reversed_one_thread;
    normalized.evaluation_threads =
        canonical_four_thread.evaluation_threads;
    return canonical_four_thread == normalized;
}

bool reference_accounting_matches_evidence(
    const HarvestReport& report) {
    if (report.roots.size() != report.selected_roots ||
        report.selection.selected_indices.size() !=
            report.selected_roots) {
        return false;
    }

    std::size_t expected_calls = 0;
    std::size_t expected_evaluations = 0;
    std::size_t expected_terminal = 0;
    std::size_t expected_bootstrapped = 0;
    std::size_t stable_disagreements = 0;
    std::size_t stable_agreements = 0;
    std::size_t unstable_best_sets = 0;
    std::size_t invalid_invariance = 0;
    std::size_t blue_opponent_disagreements = 0;
    std::size_t blue_opponent_high_cost = 0;
    std::array<std::size_t, kDeckCount>
        control_matches{};

    const auto add_repeated =
        [](std::size_t value, std::size_t repeats,
           std::size_t& total) {
            if (repeats == 0 ||
                value >
                    std::numeric_limits<std::size_t>::max() /
                        repeats ||
                total >
                    std::numeric_limits<std::size_t>::max() -
                        value * repeats) {
                return false;
            }
            total += value * repeats;
            return true;
        };

    for (const RootEvidence& root : report.roots) {
        if (root.score.action_count < 2 ||
            root.score.action_count >
                kMaximumLegalActions ||
            root.score.actual_action_index >=
                root.score.action_count ||
            root.score.action_means.size() !=
                root.score.action_count ||
            root.score.information_action_fingerprint !=
                root.information_action_fingerprint ||
            root.score.actual_action_descriptor !=
                root.production_action_descriptor ||
            root.score.reference_model_fingerprint !=
                report.model_fingerprint ||
            report.model_fingerprint !=
                kExpectedModelFingerprint ||
            root.score.reference_seed_base !=
                probes::kBsrReferenceSeed ||
            classify_reference(root.score) !=
                root.classification ||
            root.score.rollout_evaluations !=
                reference_score_cost(
                    root.score.action_count)) {
            return false;
        }
        std::size_t calls = 1;
        switch (root.classification) {
        case ReferenceClassification::StableDisagreement:
            ++stable_disagreements;
            calls = 2;
            if (!root.decoded_replay_exact ||
                root.dvr1_record.empty() ||
                root.dvr1_record_fingerprint.empty()) {
                return false;
            }
            if (root.stratum ==
                Stratum::BlueOpponentTop) {
                ++blue_opponent_disagreements;
                if (root.score.confirmation_regret >=
                        0.05 &&
                    root.score.paired_lower_95 > 0.0) {
                    ++blue_opponent_high_cost;
                }
            }
            break;
        case ReferenceClassification::StableAgreement: {
            ++stable_agreements;
            const std::size_t owner =
                deck_index(root.owner_deck);
            const AgreementControl& control =
                report.controls[owner];
            const bool retained_match =
                control.retained &&
                control.source == root.source &&
                control.information_action_fingerprint ==
                    root.information_action_fingerprint;
            if (retained_match) {
                ++control_matches[owner];
                calls = 3;
                if (control.budget_skipped ||
                    control.action_count !=
                        root.score.action_count ||
                    !root
                         .reversed_single_thread_repeat_exact ||
                    !root
                         .regenerated_canonical_repeat_exact ||
                    !root.repeat_reference_agreement) {
                    return false;
                }
            }
            break;
        }
        case ReferenceClassification::UnstableBestSet:
            ++unstable_best_sets;
            break;
        case ReferenceClassification::InvalidInvariance:
            ++invalid_invariance;
            return false;
        }
        if (expected_calls >
            std::numeric_limits<std::size_t>::max() -
                calls) {
            return false;
        }
        expected_calls += calls;
        if (!add_repeated(
                root.score.rollout_evaluations, calls,
                expected_evaluations) ||
            !add_repeated(
                root.score.terminal_evaluations, calls,
                expected_terminal) ||
            !add_repeated(
                root.score.bootstrapped_evaluations,
                calls, expected_bootstrapped)) {
            return false;
        }
    }

    for (std::size_t deck = 0; deck < kDeckCount;
         ++deck) {
        const AgreementControl& control =
            report.controls[deck];
        if (control.retained) {
            if (control_matches[deck] != 1 ||
                control.budget_skipped) {
                return false;
            }
        } else if (control_matches[deck] != 0) {
            return false;
        }
    }
    return expected_calls ==
               report.reference_score_calls &&
           expected_evaluations ==
               report.reference_evaluations &&
           expected_terminal ==
               report.reference_terminal_evaluations &&
           expected_bootstrapped ==
               report.reference_bootstrapped_evaluations &&
           expected_evaluations <=
               kMaximumReferenceEvaluations &&
           stable_disagreements ==
               report.stable_disagreements &&
           stable_agreements ==
               report.stable_agreements &&
           unstable_best_sets ==
               report.unstable_best_sets &&
           invalid_invariance ==
               report.invalid_invariance &&
           blue_opponent_disagreements ==
               report.blue_opponent_top_disagreements &&
           blue_opponent_high_cost ==
               report.blue_opponent_top_high_cost;
}

namespace {

void charge_score(HarvestReport& report,
                  ReferenceBudgetLedger& budget,
                  const probes::BsrRootScore& score) {
    budget.charge(score);
    report.reference_score_calls =
        budget.score_calls;
    report.reference_evaluations =
        budget.evaluations;
    report.reference_terminal_evaluations =
        budget.terminal_evaluations;
    report.reference_bootstrapped_evaluations =
        budget.bootstrapped_evaluations;
}

bool deadline_passed(
    std::chrono::steady_clock::time_point deadline) {
    return std::chrono::steady_clock::now() >= deadline;
}

void add_error(HarvestReport& report,
               std::string message) {
    report.errors.push_back(std::move(message));
}

bool source_balance_exact(const HarvestReport& report) {
    if (report.physical_games != kExpectedPhysicalGames ||
        report.owner_game_perspectives !=
            kExpectedOwnerPerspectives) {
        return false;
    }
    for (std::size_t seed_index = 0;
         seed_index < kSourceSeedBases.size();
         ++seed_index) {
        if (report.physical_games_by_seed_base[seed_index] !=
            learned_iteration::kBalancedScheduleGames) {
            return false;
        }
        for (std::size_t owner = 0;
             owner < kDeckCount; ++owner) {
            for (std::size_t opponent = 0;
                 opponent < kDeckCount; ++opponent) {
                for (std::size_t seat = 0;
                     seat < kPlayers; ++seat) {
                    for (std::size_t play = 0;
                         play < 2; ++play) {
                        const std::size_t expected =
                            owner == opponent ? 0 : 1;
                        if (report.source_balance_cells
                                [seed_index][owner]
                                [opponent][seat][play] !=
                            expected) {
                            return false;
                        }
                    }
                }
            }
        }
    }
    for (std::size_t owner = 0; owner < kDeckCount;
         ++owner) {
        for (std::size_t seat = 0; seat < kPlayers;
             ++seat) {
            for (std::size_t play = 0; play < 2; ++play) {
                if (report.owner_deck_seat_play_draw
                        [owner][seat][play] != 8) {
                    return false;
                }
            }
        }
        for (std::size_t opponent = 0;
             opponent < kDeckCount; ++opponent) {
            const std::size_t expected =
                owner == opponent ? 0 : 8;
            if (report.ordered_matchups[owner][opponent] !=
                expected) {
                return false;
            }
        }
    }
    return true;
}

bool all_controls_retained(const HarvestReport& report) {
    return std::all_of(
        report.controls.begin(), report.controls.end(),
        [](const AgreementControl& control) {
            return control.retained;
        });
}

void apply_budget_skip(
    HarvestReport& report,
    const SelectionCandidate& candidate) {
    CoverageCell& cell =
        coverage_for(report.selection, candidate);
    if (cell.selected == 0) {
        throw std::logic_error(
            "DVR2 budget skip has no selected root");
    }
    --cell.selected;
    ++cell.budget_skipped;
    AgreementControl& control =
        report.controls.at(
            deck_index(candidate.owner_deck));
    if (!control.retained &&
        !control.budget_skipped) {
        control = {
            .retained = false,
            .budget_skipped = true,
            .source = candidate.source,
            .information_action_fingerprint =
                candidate
                    .information_action_fingerprint,
            .action_count = candidate.action_count,
        };
    }
}

} // namespace

RunResult run(const std::filesystem::path& output_path,
              std::ostream& progress) {
    if (output_path.empty() ||
        output_path.filename().empty()) {
        throw std::invalid_argument(
            "DVR2 output path must name a file");
    }
    std::error_code target_error;
    const auto target_status =
        std::filesystem::symlink_status(
            output_path, target_error);
    if (target_error &&
        target_error !=
            std::errc::no_such_file_or_directory) {
        throw std::system_error(
            target_error,
            "cannot preflight DVR2 destination");
    }
    if (!target_error &&
        target_status.type() !=
            std::filesystem::file_type::not_found) {
        throw std::runtime_error(
            "DVR2 bundle destination already exists: '" +
            output_path.string() + "'");
    }

    RunGuard guard;
    const auto started =
        std::chrono::steady_clock::now();
    const auto deadline =
        started + std::chrono::minutes(kWatchdogMinutes);
    RunResult result;
    HarvestReport& report = result.report;
    report.model_artifact_path =
        learned_value_challenger_cache_path(
            kTrainingGames, kTrainingSeed,
            kSelfPlayGenerations);

    progress
        << "DVR2: loading exact frozen Environment-v3 C16...\n";
    const auto artifact_before =
        artifact_integrity::snapshot_regular_file(
            report.model_artifact_path);
    const auto artifact =
        load_learned_value_challenger_artifact(
            report.model_artifact_path,
            kTrainingGames, kTrainingSeed,
            kSelfPlayGenerations);
    const auto model = artifact.model();
    report.model_artifact_sha256 =
        artifact_before.sha256;
    report.model_fingerprint =
        learned_model_fingerprint(model);
    report.model_identity_passed =
        report.model_fingerprint ==
            kExpectedModelFingerprint;
    if (!report.model_identity_passed) {
        add_error(
            report,
            "frozen C16 fingerprint mismatch");
        return result;
    }

    const std::vector<ScheduledGame> schedule =
        source_schedule();
    std::set<std::uint64_t> unique_source_seeds;
    for (const ScheduledGame& scheduled : schedule) {
        unique_source_seeds.insert(
            scheduled.source.game_seed);
    }
    report.exact_source_schedule =
        schedule.size() == kExpectedPhysicalGames &&
        unique_source_seeds.size() ==
            kExpectedPhysicalGames &&
        source_schedule_sha256() ==
            kExpectedSourceScheduleSha256;
    report.source_policy_exact = true;
    std::vector<PreparedRoot> prepared;
    for (std::size_t game_index = 0;
         game_index < schedule.size(); ++game_index) {
        if (deadline_passed(deadline)) {
            add_error(
                report,
                "15-minute watchdog expired during source games");
            report.watchdog_passed = false;
            return result;
        }
        const ScheduledGame& scheduled =
            schedule[game_index];
        const auto roots = run_source_game(
            scheduled, model,
            &report.source_policy_exact);
        ++report.physical_games;
        ++report.physical_games_by_seed_base.at(
            scheduled.source.seed_base_index);
        report.traced_priority_roots += roots.size();
        prepared.insert(
            prepared.end(), roots.begin(), roots.end());
        for (std::size_t owner = 0; owner < kPlayers;
             ++owner) {
            const DeckId owner_deck =
                scheduled.seat_decks[owner];
            const DeckId opponent_deck =
                scheduled.seat_decks[1 - owner];
            const std::size_t play =
                owner ==
                        scheduled.source.starting_player
                    ? 0
                    : 1;
            ++report.owner_game_perspectives;
            ++report.owner_deck_seat_play_draw
                  [deck_index(owner_deck)][owner][play];
            ++report.ordered_matchups
                  [deck_index(owner_deck)]
                  [deck_index(opponent_deck)];
            ++report.source_balance_cells
                  [scheduled.source.seed_base_index]
                  [deck_index(owner_deck)]
                  [deck_index(opponent_deck)]
                  [owner][play];
        }
        if ((game_index + 1) %
                learned_iteration::kBalancedScheduleGames ==
            0) {
            progress << "DVR2: completed source seed base "
                     << scheduled.source.seed_base << " ("
                     << game_index + 1 << "/"
                     << schedule.size() << " games)\n";
        }
    }
    report.source_balance_passed =
        source_balance_exact(report);
    if (!report.exact_source_schedule ||
        !report.source_balance_passed ||
        !report.source_policy_exact) {
        add_error(
            report,
            "source schedule, balance, or policy gate failed");
        return result;
    }

    std::vector<SelectionCandidate> candidates;
    candidates.reserve(prepared.size());
    for (std::size_t index = 0;
         index < prepared.size(); ++index) {
        const PreparedRoot& root = prepared[index];
        candidates.push_back({
            .source_index = index,
            .source = root.source,
            .owner_deck = root.owner_deck,
            .opponent_deck = root.opponent_deck,
            .top_controlled_by_owner =
                root.top_controlled_by_owner,
            .information_action_fingerprint =
                root.information_action_fingerprint,
            .action_count =
                root.probe.candidates.size(),
        });
    }
    report.selection = select_roots(candidates);

    const probes::BsrReferenceConfig canonical =
        canonical_reference_config();
    ReferenceBudgetLedger reference_budget;
    std::vector<std::size_t> scored_indices;
    scored_indices.reserve(
        report.selection.selected_indices.size());
    for (const std::size_t candidate_index :
         report.selection.selected_indices) {
        if (deadline_passed(deadline)) {
            add_error(
                report,
                "15-minute watchdog expired during reference scoring");
            report.watchdog_passed = false;
            return result;
        }
        const SelectionCandidate& candidate =
            candidates.at(candidate_index);
        PreparedRoot& root =
            prepared.at(candidate.source_index);
        AgreementControl& owner_control =
            report.controls.at(
                deck_index(root.owner_deck));
        const bool control_already_retained =
            owner_control.retained;
        const std::size_t maximum_score_calls =
            worst_case_reference_score_calls(
                control_already_retained);
        if (!can_reserve_worst_case_reference_path(
                reference_budget.evaluations,
                candidate.action_count,
                control_already_retained) ||
            !reference_budget.can_reserve(
                candidate.action_count,
                maximum_score_calls)) {
            apply_budget_skip(report, candidate);
            continue;
        }

        probes::BsrRootScore score =
            probes::score_bsr_priority_probe(
                root.probe,
                root.production_action_descriptor,
                model, canonical);
        charge_score(
            report, reference_budget, score);
        const ReferenceClassification classification =
            classify_reference(score);
        RootEvidence evidence{
            .source = root.source,
            .owner_deck = root.owner_deck,
            .opponent_deck = root.opponent_deck,
            .stratum =
                candidate_stratum(candidate),
            .information_action_fingerprint =
                root.information_action_fingerprint,
            .production_action_descriptor =
                root.production_action_descriptor,
            .classification = classification,
            .score = score,
        };

        switch (classification) {
        case ReferenceClassification::StableDisagreement: {
            ++report.stable_disagreements;
            const auto capture =
                probes::
                    capture_dvr1_owner_visible_divergence(
                        root.probe,
                        root.production_action_descriptor,
                        report.model_fingerprint,
                        root.provenance, score);
            if (!capture.captured()) {
                throw std::logic_error(
                    "DVR2 stable disagreement did not produce DVR1");
            }
            const auto decoded =
                probes::
                    deserialize_dvr1_owner_visible_record(
                        capture.serialized_record);
            const probes::DecisionProbe replay =
                probes::rehydrate_dvr1_decision_probe(
                    decoded);
            probes::BsrRootScore replay_score =
                probes::score_bsr_priority_probe(
                    replay,
                    root.production_action_descriptor,
                    model, canonical);
            charge_score(
                report, reference_budget,
                replay_score);
            const auto replay_capture =
                probes::
                    capture_dvr1_owner_visible_divergence(
                        replay,
                        root.production_action_descriptor,
                        report.model_fingerprint,
                        root.provenance, replay_score);
            evidence.decoded_replay_exact =
                replay_score == score &&
                probes::
                        serialize_dvr1_owner_visible_record(
                            decoded) ==
                    capture.serialized_record &&
                probes::
                        dvr1_owner_visible_record_fingerprint(
                            decoded) ==
                    capture.record_fingerprint &&
                replay_capture.captured() &&
                replay_capture.serialized_record ==
                    capture.serialized_record &&
                replay_capture.record_fingerprint ==
                    capture.record_fingerprint;
            evidence.dvr1_record =
                capture.serialized_record;
            evidence.dvr1_record_fingerprint =
                capture.record_fingerprint;
            if (!evidence.decoded_replay_exact) {
                throw std::logic_error(
                    "DVR2 decoded disagreement did not re-score exactly");
            }
            if (evidence.stratum ==
                Stratum::BlueOpponentTop) {
                ++report
                      .blue_opponent_top_disagreements;
                if (score.confirmation_regret >= 0.05 &&
                    score.paired_lower_95 > 0.0) {
                    ++report
                          .blue_opponent_top_high_cost;
                }
            }
            break;
        }
        case ReferenceClassification::StableAgreement: {
            ++report.stable_agreements;
            const auto original_agreement =
                probes::
                    capture_dvr1_owner_visible_divergence(
                        root.probe,
                        root.production_action_descriptor,
                        report.model_fingerprint,
                        root.provenance, score);
            if (original_agreement.disposition !=
                    probes::Dvr1CaptureDisposition::
                        ReferenceAgreement ||
                original_agreement.record.has_value() ||
                !original_agreement.serialized_record.empty()) {
                throw std::logic_error(
                    "DVR2 agreement failed the DVR1 negative control");
            }
            AgreementControl& control = owner_control;
            if (!control.retained) {
                probes::DecisionProbe reversed =
                    root.probe;
                std::reverse(
                    reversed.candidates.begin(),
                    reversed.candidates.end());
                probes::BsrReferenceConfig single =
                    canonical;
                single.evaluation_threads = 1;
                const probes::BsrRootScore
                    reversed_single =
                        probes::score_bsr_priority_probe(
                            reversed,
                            root.production_action_descriptor,
                            model, single);
                charge_score(
                    report, reference_budget,
                    reversed_single);
                evidence
                    .reversed_single_thread_repeat_exact =
                    semantic_control_score_equal(
                        score, reversed_single);

                PreparedRoot regenerated =
                    regenerate_root(
                        schedule, root, model,
                        &report.source_policy_exact);
                const probes::BsrRootScore regenerated_score =
                    probes::score_bsr_priority_probe(
                        regenerated.probe,
                        regenerated
                            .production_action_descriptor,
                        model, canonical);
                charge_score(
                    report, reference_budget,
                    regenerated_score);
                evidence
                    .regenerated_canonical_repeat_exact =
                    regenerated_score == score;
                const auto regenerated_agreement =
                    probes::
                        capture_dvr1_owner_visible_divergence(
                            regenerated.probe,
                            regenerated
                                .production_action_descriptor,
                            report.model_fingerprint,
                            regenerated.provenance,
                            regenerated_score);
                evidence.repeat_reference_agreement =
                    regenerated_agreement.disposition ==
                        probes::Dvr1CaptureDisposition::
                            ReferenceAgreement &&
                    !regenerated_agreement.record.has_value() &&
                    regenerated_agreement
                        .serialized_record.empty();
                if (!evidence
                         .reversed_single_thread_repeat_exact ||
                    !evidence
                         .regenerated_canonical_repeat_exact ||
                    !evidence.repeat_reference_agreement) {
                    throw std::logic_error(
                        "DVR2 agreement control repeat failed");
                }
                control = {
                    .retained = true,
                    .budget_skipped = false,
                    .source = root.source,
                    .information_action_fingerprint =
                        root
                            .information_action_fingerprint,
                    .action_count =
                        root.probe.candidates.size(),
                };
            }
            break;
        }
        case ReferenceClassification::UnstableBestSet:
            ++report.unstable_best_sets;
            break;
        case ReferenceClassification::InvalidInvariance:
            ++report.invalid_invariance;
            throw std::logic_error(
                "DVR2 selected root failed invariance");
        }
        scored_indices.push_back(candidate_index);
        report.roots.push_back(std::move(evidence));
    }
    report.selection.selected_indices =
        std::move(scored_indices);
    report.selected_roots =
        report.selection.selected_indices.size();
    report.selection.quotas_met =
        quota_cells_met(report.selection);
    report.selection.cross_sums_valid = true;
    for (const auto& stratum :
         report.selection.coverage) {
        for (const CoverageCell& cell : stratum) {
            report.selection.cross_sums_valid =
                report.selection.cross_sums_valid &&
                cell.cross_sum_valid();
        }
    }

    for (std::size_t deck = 0; deck < kDeckCount;
         ++deck) {
        const AgreementControl& control =
            report.controls[deck];
        if (!control.retained &&
            control.budget_skipped &&
            (control.information_action_fingerprint.empty() ||
             control.action_count < 2 ||
             control.action_count >
                 kMaximumLegalActions ||
             control.source.seed_base_index >=
                 kSourceSeedBases.size())) {
            throw std::logic_error(
                "DVR2 missing-control budget witness is incomplete");
        }
    }

    const auto artifact_after =
        artifact_integrity::snapshot_regular_file(
            report.model_artifact_path);
    report.model_identity_passed =
        report.model_identity_passed &&
        artifact_after == artifact_before &&
        learned_model_fingerprint(model) ==
            report.model_fingerprint;
    report.manifest_cross_sums_passed =
        report.selection.cross_sums_valid;
    report.coverage_gate_passed =
        report.selection.quotas_met;
    report.reference_accounting_passed =
        reference_budget.accounting_valid() &&
        report.reference_score_calls ==
            reference_budget.score_calls &&
        report.reference_evaluations ==
            reference_budget.evaluations &&
        report.reference_terminal_evaluations ==
            reference_budget.terminal_evaluations &&
        report.reference_bootstrapped_evaluations ==
            reference_budget.bootstrapped_evaluations &&
        reference_accounting_matches_evidence(report);
    report.invariance_passed =
        report.invalid_invariance == 0 &&
        std::all_of(
            report.roots.begin(), report.roots.end(),
            [](const RootEvidence& root) {
                return root.classification !=
                       ReferenceClassification::
                           InvalidInvariance;
            });
    report.replay_passed =
        std::all_of(
            report.roots.begin(), report.roots.end(),
            [](const RootEvidence& root) {
                return root.classification !=
                           ReferenceClassification::
                               StableDisagreement ||
                       (!root.dvr1_record.empty() &&
                        !root
                             .dvr1_record_fingerprint
                             .empty() &&
                        root.decoded_replay_exact);
            });
    report.controls_passed =
        all_controls_retained(report);
    report.controls_underpowered_only_by_budget =
        !report.controls_passed &&
        std::all_of(
            report.controls.begin(),
            report.controls.end(),
            [](const AgreementControl& control) {
                return control.retained ||
                       control.budget_skipped;
            });
    report.watchdog_passed =
        !deadline_passed(deadline);
    if (!report.controls_passed &&
        !report.controls_underpowered_only_by_budget) {
        add_error(
            report,
            "missing all-five agreement controls without a budget skip");
    }
    if (!report.model_identity_passed) {
        add_error(
            report,
            "frozen model artifact changed during DVR2");
    }
    if (!report.manifest_cross_sums_passed) {
        add_error(
            report,
            "coverage manifest cross-sum failed");
    }
    if (!report.reference_accounting_passed) {
        add_error(
            report,
            "reference evaluation accounting failed");
    }
    if (!report.invariance_passed ||
        !report.replay_passed) {
        add_error(
            report,
            "reference invariance or replay failed");
    }
    if (!report.watchdog_passed) {
        add_error(
            report,
            "15-minute watchdog expired");
    }
    report.valid =
        report.exact_source_schedule &&
        report.source_balance_passed &&
        report.source_policy_exact &&
        report.model_identity_passed &&
        report.manifest_cross_sums_passed &&
        report.reference_accounting_passed &&
        report.invariance_passed &&
        report.replay_passed &&
        (report.controls_passed ||
         report.controls_underpowered_only_by_budget) &&
        report.watchdog_passed;
    report.rs1_licensed =
        report.valid &&
        report.coverage_gate_passed &&
        report.controls_passed &&
        report.blue_opponent_top_disagreements >= 4 &&
        report.blue_opponent_top_high_cost >= 1;
    if (!report.valid) {
        write_summary(report, progress);
        return result;
    }

    result.payload = serialize_payload(report);
    result.payload_sha256 =
        artifact_integrity::sha256_string(
            result.payload);
    result.bundle =
        make_checksummed_bundle(result.payload);
    if (!verify_checksummed_bundle(result.bundle)) {
        throw std::logic_error(
            "DVR2 bundle failed its own checksum");
    }
    if (deadline_passed(deadline)) {
        report.watchdog_passed = false;
        report.valid = false;
        report.rs1_licensed = false;
        add_error(
            report,
            "15-minute watchdog expired before publication");
        result.payload.clear();
        result.payload_sha256.clear();
        result.bundle.clear();
        write_summary(report, progress);
        return result;
    }
    write_bundle_atomic_no_replace(
        output_path, result.bundle);
    result.published = true;
    write_summary(report, progress);
    progress << "DVR2 bundle: " << output_path.string()
             << "\npayload SHA-256: "
             << result.payload_sha256 << '\n';
    return result;
}

void write_summary(const HarvestReport& report,
                   std::ostream& output) {
    output
        << "DVR2 frozen-C16 Learned-mirror harvest\n"
        << "model fingerprint: "
        << report.model_fingerprint << '\n'
        << "source games / owner perspectives / priority roots: "
        << report.physical_games << " / "
        << report.owner_game_perspectives << " / "
        << report.traced_priority_roots << '\n'
        << "selected / disagreements / agreements / unstable: "
        << report.selected_roots << " / "
        << report.stable_disagreements << " / "
        << report.stable_agreements << " / "
        << report.unstable_best_sets << '\n'
        << "Blue opponent-top divergences / high-cost: "
        << report.blue_opponent_top_disagreements
        << " / "
        << report.blue_opponent_top_high_cost << '\n'
        << "reference evaluations: "
        << report.reference_evaluations << " / "
        << kMaximumReferenceEvaluations << '\n'
        << "coverage / controls / valid / RS1: "
        << (report.coverage_gate_passed ? "PASS" : "MISS")
        << " / "
        << (report.controls_passed ? "PASS" : "MISS")
        << " / "
        << (report.valid ? "PASS" : "FAIL")
        << " / "
        << (report.rs1_licensed ? "LICENSED" : "NOT-LICENSED")
        << '\n';
    for (const std::string& error : report.errors) {
        output << "error: " << error << '\n';
    }
}

} // namespace old_school::dvr2_harvest
