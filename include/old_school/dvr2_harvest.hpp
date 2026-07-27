#pragma once

#include "old_school/learned_iteration.hpp"
#include "old_school/probes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::dvr2_harvest {

inline constexpr std::string_view kSchema =
    "old-school-dvr2-harvest-v1";
inline constexpr std::string_view kBundleSchema =
    "old-school-dvr2-bundle-v1";
inline constexpr std::string_view kExpectedModelFingerprint =
    "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f";
inline constexpr std::array<std::uint64_t, 2> kSourceSeedBases = {
    4242,
    7801,
};
inline constexpr std::uint64_t kGenerationNamespace =
    0x44565232ULL;
inline constexpr std::string_view kExpectedSourceScheduleSha256 =
    "876ac6ce9c89fa3a33b52c1650d46653eb0db9d06c98251b00aaa9733872fd13";
inline constexpr std::size_t kSourceTurnCap = 128;
inline constexpr std::size_t kProductionWorlds = 8;
inline constexpr std::size_t kMaximumLegalActions = 32;
inline constexpr std::size_t kMaximumRootsPerOwnerGame = 2;
inline constexpr std::size_t kMaximumPrimaryRoots = 64;
inline constexpr std::size_t kMaximumReferenceEvaluations =
    131072;
inline constexpr std::size_t kWatchdogMinutes = 15;
inline constexpr std::size_t kExpectedPhysicalGames =
    kSourceSeedBases.size() *
    learned_iteration::kBalancedScheduleGames;
inline constexpr std::size_t kExpectedOwnerPerspectives =
    2 * kExpectedPhysicalGames;

enum class Stratum : std::uint8_t {
    BlueOpponentTop,
    BlueOwnTop,
    NonBlueOpponentTop,
    NonBlueOwnTop,
};

inline constexpr std::size_t kStratumCount = 4;

struct SourceLocator {
    std::uint64_t seed_base = 0;
    std::size_t seed_base_index = 0;
    std::size_t schedule_index = 0;
    std::size_t pairing_index = 0;
    std::uint64_t game_seed = 0;
    std::size_t owner_seat = 0;
    bool owner_on_play = false;
    std::size_t starting_player = 0;
    std::size_t trace_ordinal = 0;

    bool operator==(const SourceLocator&) const = default;
};

struct ScheduledGame {
    SourceLocator source;
    std::array<DeckId, 2> seat_decks = {
        DeckId::Green,
        DeckId::Red,
    };

    bool operator==(const ScheduledGame&) const = default;
};

// The exact 80-game production schedule. There are no seed, block, or
// generation arguments: the scientific source distribution is not a CLI dial.
std::vector<ScheduledGame> source_schedule();
std::string source_schedule_sha256();

struct SelectionCandidate {
    std::size_t source_index = 0;
    SourceLocator source;
    DeckId owner_deck = DeckId::Green;
    DeckId opponent_deck = DeckId::Red;
    bool top_controlled_by_owner = false;
    std::string information_action_fingerprint;
    std::size_t action_count = 0;

    bool operator==(const SelectionCandidate&) const = default;
};

struct CoverageCell {
    // `considered` includes action-cap failures. `eligible` excludes
    // them and cross-sums exactly across the five terminal dispositions.
    std::size_t considered = 0;
    std::size_t eligible = 0;
    std::size_t selected = 0;
    std::size_t duplicate_skipped = 0;
    std::size_t per_owner_game_skipped = 0;
    std::size_t action_cap_skipped = 0;
    std::size_t quota_skipped = 0;
    std::size_t budget_skipped = 0;

    bool cross_sum_valid() const;
    bool operator==(const CoverageCell&) const = default;
};

struct SelectionReport {
    std::vector<std::size_t> selected_indices;
    std::array<std::array<CoverageCell, kDeckCount>,
               kStratumCount>
        coverage{};
    std::size_t below_action_floor = 0;
    bool quotas_met = false;
    bool cross_sums_valid = false;

    bool operator==(const SelectionReport&) const = default;
};

// Outcome-blind deterministic selection. `selected_indices` index the caller's
// vector. Reference budget skips are applied later, after exact per-root costs
// are known.
SelectionReport select_roots(
    const std::vector<SelectionCandidate>& candidates);

enum class ReferenceClassification : std::uint8_t {
    StableDisagreement,
    StableAgreement,
    UnstableBestSet,
    InvalidInvariance,
};

ReferenceClassification classify_reference(
    const probes::BsrRootScore& score);

std::size_t reference_score_cost(std::size_t action_count);
std::size_t worst_case_reference_score_calls(
    bool owner_control_already_retained) noexcept;
bool can_reserve_worst_case_reference_path(
    std::size_t evaluations_already_charged,
    std::size_t action_count,
    bool owner_control_already_retained,
    std::size_t evaluation_cap =
        kMaximumReferenceEvaluations);

struct ReferenceBudgetLedger {
    std::size_t evaluation_cap =
        kMaximumReferenceEvaluations;
    std::size_t score_calls = 0;
    std::size_t evaluations = 0;
    std::size_t terminal_evaluations = 0;
    std::size_t bootstrapped_evaluations = 0;

    bool can_reserve(
        std::size_t action_count,
        std::size_t maximum_score_calls) const;
    void charge(const probes::BsrRootScore& score);
    bool accounting_valid() const noexcept;

    bool operator==(
        const ReferenceBudgetLedger&) const = default;
};

struct HarvestReport;

// A reversed-candidate control is scored with one worker rather than the
// canonical four. This comparison normalizes only that expected metadata
// difference; every semantic score, seed, best set, and invariant must match.
bool semantic_control_score_equal(
    const probes::BsrRootScore& canonical_four_thread,
    const probes::BsrRootScore& reversed_one_thread);

// Independently reconstructs actual BSR call accounting and all derived
// classification/RS1 counters from retained root evidence and control links.
bool reference_accounting_matches_evidence(
    const HarvestReport& report);

struct RootEvidence {
    SourceLocator source;
    DeckId owner_deck = DeckId::Green;
    DeckId opponent_deck = DeckId::Red;
    Stratum stratum = Stratum::NonBlueOpponentTop;
    std::string information_action_fingerprint;
    std::string production_action_descriptor;
    ReferenceClassification classification =
        ReferenceClassification::InvalidInvariance;
    probes::BsrRootScore score;
    std::string dvr1_record;
    std::string dvr1_record_fingerprint;
    bool decoded_replay_exact = false;
    bool reversed_single_thread_repeat_exact = false;
    bool regenerated_canonical_repeat_exact = false;
    bool repeat_reference_agreement = false;

    bool operator==(const RootEvidence&) const = default;
};

struct AgreementControl {
    bool retained = false;
    bool budget_skipped = false;
    SourceLocator source;
    std::string information_action_fingerprint;
    std::size_t action_count = 0;

    bool operator==(const AgreementControl&) const = default;
};

struct HarvestReport {
    std::string schema = std::string(kSchema);
    std::string model_artifact_path;
    std::string model_artifact_sha256;
    std::string model_fingerprint;
    SelectionReport selection;
    std::array<AgreementControl, kDeckCount> controls{};
    std::vector<RootEvidence> roots;
    std::array<std::array<std::array<std::size_t, 2>, 2>,
               kDeckCount>
        owner_deck_seat_play_draw{};
    std::array<std::array<std::size_t, kDeckCount>,
               kDeckCount>
        ordered_matchups{};
    std::array<std::size_t, kSourceSeedBases.size()>
        physical_games_by_seed_base{};
    std::array<
        std::array<
            std::array<
                std::array<std::array<std::size_t, 2>, 2>,
                kDeckCount>,
            kDeckCount>,
        kSourceSeedBases.size()>
        source_balance_cells{};
    std::size_t physical_games = 0;
    std::size_t owner_game_perspectives = 0;
    std::size_t traced_priority_roots = 0;
    std::size_t selected_roots = 0;
    std::size_t stable_disagreements = 0;
    std::size_t stable_agreements = 0;
    std::size_t unstable_best_sets = 0;
    std::size_t invalid_invariance = 0;
    std::size_t blue_opponent_top_disagreements = 0;
    std::size_t blue_opponent_top_high_cost = 0;
    std::size_t reference_score_calls = 0;
    std::size_t reference_evaluations = 0;
    std::size_t reference_terminal_evaluations = 0;
    std::size_t reference_bootstrapped_evaluations = 0;
    bool exact_source_schedule = false;
    bool source_balance_passed = false;
    bool source_policy_exact = false;
    bool model_identity_passed = false;
    bool manifest_cross_sums_passed = false;
    bool coverage_gate_passed = false;
    bool reference_accounting_passed = false;
    bool invariance_passed = false;
    bool replay_passed = false;
    bool controls_passed = false;
    bool controls_underpowered_only_by_budget = false;
    bool watchdog_passed = false;
    bool valid = false;
    bool rs1_licensed = false;
    std::vector<std::string> errors;

    bool operator==(const HarvestReport&) const = default;
};

struct RunResult {
    HarvestReport report;
    std::string payload;
    std::string payload_sha256;
    std::string bundle;
    bool published = false;

    int exit_code() const noexcept;
};

// Canonical evidence bytes. These are public test seams so determinism,
// checksums, corruption rejection, and no-overwrite publication can be tested
// without executing the reserved source schedule.
std::string serialize_payload(const HarvestReport& report);
std::string make_checksummed_bundle(std::string_view payload);
bool verify_checksummed_bundle(std::string_view bundle);
void write_bundle_atomic_no_replace(
    const std::filesystem::path& path,
    std::string_view bundle);

namespace testing {

enum class PublicationFault : std::uint8_t {
    None,
    WatchdogExpiredBeforeLink,
    AfterLinkBeforeDirectorySync,
};

// Fault-injected publication seam. Production always calls the non-testing
// entry point above, which is equivalent to PublicationFault::None.
void write_bundle_atomic_no_replace(
    const std::filesystem::path& path,
    std::string_view bundle,
    PublicationFault fault);

} // namespace testing

// The only production entry point. It loads exact frozen C16, runs the fixed
// protocol above, and publishes only a valid exit-0/exit-1 evidence bundle.
// The output path is the sole caller-controlled value.
RunResult run(const std::filesystem::path& output_path,
              std::ostream& progress);

void write_summary(const HarvestReport& report,
                   std::ostream& output);

} // namespace old_school::dvr2_harvest
