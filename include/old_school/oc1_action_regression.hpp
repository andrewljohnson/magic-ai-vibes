#pragma once

#include "old_school/artifact_integrity.hpp"
#include "old_school/dvr2_replay_bundle.hpp"
#include "old_school/oc1_action_eval.hpp"
#include "old_school/oc1_action_scoring.hpp"
#include "old_school/probe_runner.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::oc1_action_regression {

inline constexpr std::string_view kParentArtifactPath =
    "build/model-cache/"
    "old-school-value-challenger-v3-c16-t800-s424242.bin";
inline constexpr std::uintmax_t kParentArtifactBytes = 3111437;
inline constexpr std::string_view kParentArtifactSha256 =
    "53aeb904bd87311b37201859317f05ab066bdfe134c72460cf94bff6d1f944ca";
inline constexpr std::string_view kParentModelFingerprint =
    "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f";

inline constexpr std::string_view kCandidateArtifactPath =
    "build/model-cache/"
    "old-school-value-output-calibration-v1-c16-t800-p424242-"
    "f202607261927.bin";
inline constexpr std::uintmax_t kCandidateArtifactBytes = 4756;
inline constexpr std::string_view kCandidateArtifactSha256 =
    "77d02729e4effd4762daefdcbe28ef4f1a081c87f707739f3d243123f4c17e3c";
inline constexpr std::string_view kCandidateModelFingerprint =
    "64851236ddb8907057ec16d8fe0db9ab1b1966dfff55a3d3a62bde933e94ce0d";

inline constexpr std::string_view kActorCachePath =
    "data/old-school-probe-dev-v3-k64-h8-c17-j1.labels.tsv";
inline constexpr std::uintmax_t kActorCacheBytes = 276387;
inline constexpr std::string_view kActorCacheSha256 =
    "949ea2fda448fa76b31a61927721629cfba9e6addee2da383cfbb68450b04770";
inline constexpr std::string_view kActorModelFingerprint =
    "dd58d3814f46d6661d40690f6ad7ac73226c2160137b2e42bfadf3e6ac7a1b72";
inline constexpr std::string_view kActorInformationFingerprint =
    "cf4729a535378a12";

inline constexpr std::string_view kDvrArtifactPath =
    dvr2_replay_bundle::kArtifactPath;
inline constexpr std::uintmax_t kDvrArtifactBytes =
    dvr2_replay_bundle::kArtifactBytes;
inline constexpr std::string_view kDvrArtifactSha256 =
    dvr2_replay_bundle::kArtifactSha256;

inline constexpr std::size_t kParentTrainingGames = 800;
inline constexpr std::uint64_t kParentTrainingSeed = 424242;
inline constexpr std::size_t kParentGenerations = 16;
inline constexpr std::size_t kBalancedRootCount = 20;
inline constexpr std::size_t kBalancedRootsPerDeck = 4;
inline constexpr std::size_t kActorRowsPerAction = 64;
inline constexpr std::size_t kActorCandidateCount = 61;
inline constexpr std::size_t kActorRawSampleCount = 3904;
inline constexpr std::size_t kFocusedFamilyCount = 7;
inline constexpr std::size_t kDvrRootCount = 4;

struct FileRequirement {
    std::string path;
    std::uintmax_t byte_size = 0;
    std::string sha256;

    bool operator==(const FileRequirement&) const = default;
};

struct HiddenCounts {
    std::size_t attempted = 0;
    std::size_t changed = 0;
    std::size_t unchanged = 0;

    bool operator==(const HiddenCounts&) const = default;
};

struct HiddenAudit {
    HiddenCounts pooled;
    std::array<HiddenCounts, kDeckCount> balanced_by_deck{};
    // Force Spike, counter composition, Braingeyser, Disintegrate,
    // field regressions, attack regression, DVR2 replay.
    std::array<HiddenCounts, kFocusedFamilyCount> focused_by_family{};
    bool owner_observations_identical = false;
    bool typed_actions_identical = false;
    bool information_fingerprints_identical = false;
    bool raw_scores_identical = false;
    bool supports_identical = false;
    bool accounting_identical = false;
    bool nonvacuous = false;
    bool passed = false;

    bool operator==(const HiddenAudit&) const = default;
};

struct BalancedDecisionEvidence {
    std::string stable_id;
    DeckId root_deck = DeckId::Green;
    std::string information_action_fingerprint;
    oc1_action_eval::ReferenceRoot actor_reference;
    oc1_action_eval::ActionSupport actor_best_set;
    oc1_action_eval::ReferenceRoot c16_reference;
    oc1_action_eval::ActionSupport c16_best_set;
    oc1_action_scoring::DecisionScore c16_deployment;
    oc1_action_scoring::DecisionScore candidate_deployment;
    oc1_action_eval::RootPolicyMetrics actor_c16_metrics;
    oc1_action_eval::RootPolicyMetrics actor_candidate_metrics;
    oc1_action_eval::RootPolicyMetrics c16_c16_metrics;
    oc1_action_eval::RootPolicyMetrics c16_candidate_metrics;
    oc1_action_eval::ActionSupport joint_robust_best_set;
    oc1_action_eval::DualReferenceMaterialRegression
        material_regression;
    bool joint_robust_stable = false;
    bool c16_agrees_with_joint_set = false;
    bool candidate_preserves_joint_agreement = false;
    bool reference_sign_reversal = false;
    bool descriptor_order_invariant = false;
};

struct BalancedReport {
    std::vector<BalancedDecisionEvidence> decisions;
    oc1_action_eval::EqualRootComparison actor_metrics;
    std::array<std::size_t, kDeckCount>
        joint_stable_roots_by_deck{};
    std::array<std::size_t, kDeckCount> roots_by_deck{};
    std::size_t c16_stable_agreements = 0;
    std::size_t lost_stable_agreements = 0;
    std::size_t changed_support_roots = 0;
    std::size_t material_regressions = 0;
    std::size_t reference_sign_reversals = 0;
    std::size_t growth_timing_roots = 0;
    std::size_t lost_growth_stable_agreements = 0;
    bool exact_root_census = false;
    bool all_decks_have_joint_stable_root = false;
    bool stable_agreements_preserved = false;
    bool growth_stable_agreements_preserved = false;
    bool no_material_regression = false;
    bool passed = false;
};

struct FocusedDecision {
    std::string family;
    std::string stable_id;
    DeckId root_deck = DeckId::Green;
    std::string information_action_fingerprint;
    oc1_action_scoring::DecisionScore scout_reference;
    oc1_action_scoring::DecisionScore confirmation_reference;
    oc1_action_scoring::DecisionScore c16_deployment;
    oc1_action_scoring::DecisionScore candidate_deployment;
    oc1_action_eval::ActionSupport scout_best_set;
    oc1_action_eval::ActionSupport confirmation_best_set;
    bool reference_stable = false;
    bool reference_required = false;
    bool parent_reference_agreement_preserved = false;
    bool behavior_contract_passed = false;
    bool descriptor_order_invariant = false;
    std::string disposition;
};

struct FocusedReport {
    std::vector<FocusedDecision> decisions;
    std::size_t stable_references = 0;
    std::size_t behavior_contracts = 0;
    std::size_t failed_contracts = 0;
    std::size_t descriptive_parent_reference_losses = 0;
    std::size_t lost_parent_reference_agreements = 0;
    std::size_t inconclusive_required_references = 0;
    bool exact_family_census = false;
    bool passed = false;
};

struct DvrDecision {
    std::string stable_id;
    std::string dvr1_record_fingerprint;
    std::string information_action_fingerprint;
    probes::BsrRootScore stored_reference;
    probes::BsrRootScore reproduced_reference;
    oc1_action_scoring::DecisionScore confirmation_reference;
    oc1_action_scoring::DecisionScore c16_deployment;
    oc1_action_scoring::DecisionScore candidate_deployment;
    oc1_action_eval::RootPolicyMetrics c16_confirmation_metrics;
    oc1_action_eval::RootPolicyMetrics
        candidate_confirmation_metrics;
    oc1_action_eval::PairedEstimate c16_minus_candidate;
    bool bit_exact_reproduction = false;
    bool material_regression = false;
};

struct DvrReport {
    std::vector<DvrDecision> decisions;
    double c16_total_confirmation_regret = 0.0;
    double candidate_total_confirmation_regret = 0.0;
    std::size_t bit_exact_reproductions = 0;
    std::size_t material_regressions = 0;
    bool exact_root_census = false;
    bool total_regret_no_worse = false;
    bool passed = false;
};

struct MechanicalConsequenceRoot {
    std::string stable_id;
    std::vector<std::string> descriptors;
    std::vector<std::string> public_consequence_hashes;
    std::vector<std::string>
        hidden_public_consequence_hashes;
    bool hidden_identity_changed = false;
    bool passed = false;
};

// The scientific projection intentionally omits filesystem provenance and
// hidden-change counters. It retains immutable identities, all recipes,
// raw common-world rows, deployment scores/supports, derived metrics, and
// every scientific gate so original/repartitioned constructions can be
// compared without leaking opponent identities.
struct ScientificEvidence {
    std::string parent_model_fingerprint;
    std::string candidate_model_fingerprint;
    probe_runner::ProbeCacheMetadata actor_cache_metadata;
    std::string actor_cache_contents_hash;
    std::string dvr_bundle_contents_hash;
    std::vector<MechanicalConsequenceRoot>
        mechanical_consequences;
    bool mechanical_consequences_passed = false;
    BalancedReport balanced;
    FocusedReport focused;
    DvrReport dvr;
};

struct ArtifactSnapshotSet {
    artifact_integrity::RegularFileSnapshot parent;
    artifact_integrity::RegularFileSnapshot candidate;
    artifact_integrity::RegularFileSnapshot actor_cache;
    artifact_integrity::RegularFileSnapshot dvr;

    bool operator==(const ArtifactSnapshotSet&) const = default;
};

struct ConstructionBundleReport {
    ArtifactSnapshotSet before;
    ArtifactSnapshotSet after;
    ScientificEvidence original;
    ScientificEvidence hidden;
    HiddenAudit hidden_audit;
    std::string original_scientific_hash;
    std::string hidden_scientific_hash;
    bool original_hidden_scientific_bit_identical = false;
    bool artifacts_unchanged = false;
    bool passed = false;
};

struct IntegrityReport {
    ArtifactSnapshotSet preflight;
    ArtifactSnapshotSet first_before;
    ArtifactSnapshotSet first_after;
    ArtifactSnapshotSet repeated_before;
    ArtifactSnapshotSet repeated_after;
    HiddenAudit hidden;
    HiddenAudit repeated_hidden;
    std::string first_scientific_hash;
    std::string repeated_scientific_hash;
    std::string hidden_scientific_hash;
    std::string repeated_hidden_scientific_hash;
    std::string first_full_construction_hash;
    std::string repeated_full_construction_hash;
    std::string full_report_hash;
    bool model_identities_verified = false;
    bool output_calibration_isolated = false;
    bool raw_actor_double_load_bit_identical = false;
    bool dvr_double_load_bit_identical = false;
    bool artifacts_unchanged = false;
    bool repeated_construction_bit_identical = false;
    bool repeated_full_construction_bit_identical = false;
    bool hidden_scientific_bit_identical = false;
    bool repeated_hidden_scientific_bit_identical = false;
    bool hidden_repeat_bit_identical = false;
    bool hidden_audits_bit_identical = false;
    bool passed = false;
};

struct GateReport {
    bool infrastructure_failure = false;
    bool integrity_passed = false;
    bool balanced_passed = false;
    bool focused_passed = false;
    bool dvr_passed = false;
    bool passed = false;
    std::vector<std::string> failures;

    bool operator==(const GateReport&) const = default;
};

struct RunReport {
    ScientificEvidence scientific;
    IntegrityReport integrity;
    GateReport gate;
};

GateReport evaluate_gate(
    const BalancedReport& balanced,
    const FocusedReport& focused,
    const DvrReport& dvr,
    const IntegrityReport& integrity);

RunReport run(std::ostream& progress);
int exit_code(const GateReport& gate);
int run_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error);

namespace testing {

bool snapshot_matches_requirement(
    const artifact_integrity::RegularFileSnapshot& snapshot,
    const FileRequirement& requirement);

bool focused_authored_census_is_exact(
    const std::array<std::size_t, kFocusedFamilyCount>&
        family_counts,
    std::size_t authored_root_count);

std::string scientific_projection_hash(
    const ScientificEvidence& evidence);

std::string canonical_construction_report_hash(
    const RunReport& report);

std::string canonical_construction_bundle_hash(
    const ConstructionBundleReport& bundle);

std::string canonical_full_report_hash(
    const RunReport& report);

} // namespace testing

} // namespace old_school::oc1_action_regression
