#pragma once

#include "old_school/artifact_integrity.hpp"
#include "old_school/oc1_action_scoring.hpp"
#include "old_school/probes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::ac1_teacher_audit {

inline constexpr std::string_view kSeedTag =
    "old-school-ac1-t0-next-turn-boundary-v1";
inline constexpr std::uint64_t kSeedBase = 202607262205ULL;
inline constexpr std::size_t kWorlds = 64;
inline constexpr std::size_t kRolloutsPerWorld = 1;
inline constexpr std::size_t kHorizonTurns = 0;
inline constexpr std::size_t kEvaluationThreads = 4;
inline constexpr std::size_t kPhysicalPriorityRoots = 26;
inline constexpr std::size_t kLogicalPriorityIds = 27;
inline constexpr std::size_t kDevPriorityRoots = 19;
inline constexpr std::size_t kDevAttackRoots = 1;
inline constexpr std::size_t kSamplesPerAction =
    kWorlds * kRolloutsPerWorld;
inline constexpr std::size_t kBlocks = 8;
inline constexpr std::size_t kSamplesPerBlock = 8;
inline constexpr std::size_t kMinimumPositiveBlocks = 6;
inline constexpr double kNormal95CriticalValue = 1.96;
inline constexpr std::string_view kEvidencePath =
    "build/experiments/"
    "old-school-ac1-t0-next-turn-v1.evidence.tsv";
inline constexpr std::string_view kEvidenceSchema =
    "old-school-ac1-t0-next-turn-evidence-v1";

inline constexpr std::string_view kDevForceSpikeAlias =
    "blue.force-spike-tapped-out-gray-ogre.v3";
inline constexpr std::string_view kCanonicalLiveForceSpike =
    "control.blue.force-spike-live-gray-ogre.v1";
inline constexpr std::string_view kDevAttackStableId =
    "ru.flying-men-moat-attack.v3";
inline constexpr std::string_view kDevAttackFingerprint =
    "41172fde564f86ea";

inline constexpr oc1_action_scoring::SearchRecipe kSearchRecipe{
    .seed_tag = kSeedTag,
    .seed_base = kSeedBase,
    .worlds = kWorlds,
    .horizon_turns = kHorizonTurns,
    .rollouts_per_world = kRolloutsPerWorld,
    .blend_shallow_prior = false,
    .evaluation_threads = kEvaluationThreads,
};

struct ManifestRoot {
    probes::DecisionProbe probe;
    std::string information_action_fingerprint;
    std::string factory_contract_fingerprint;
    bool from_dev_v3 = false;

    bool operator==(const ManifestRoot& other) const {
        return probe.stable_id == other.probe.stable_id &&
               probe.category == other.probe.category &&
               probe.decision_kind == other.probe.decision_kind &&
               probe.root_deck == other.probe.root_deck &&
               probe.opponent_deck == other.probe.opponent_deck &&
               probe.root_player == other.probe.root_player &&
               probe.phase == other.probe.phase &&
               probe.consecutive_passes ==
                   other.probe.consecutive_passes &&
               probe.state == other.probe.state &&
               probe.original_decks == other.probe.original_decks &&
               probe.candidates == other.probe.candidates &&
               probe.harvest == other.probe.harvest &&
               information_action_fingerprint ==
                   other.information_action_fingerprint &&
               factory_contract_fingerprint ==
                   other.factory_contract_fingerprint &&
               from_dev_v3 == other.from_dev_v3;
    }
};

struct Manifest {
    std::vector<ManifestRoot> roots;
    std::string dev_force_spike_alias;
    std::string canonical_live_force_spike;
    std::string attack_stable_id;
    std::string attack_information_action_fingerprint;
    std::array<std::size_t, kDeckCount> physical_roots_by_deck{};
    std::array<std::size_t, kDeckCount> logical_dev_roots_by_deck{};
    std::size_t logical_priority_ids = 0;
    bool exact = false;

    bool operator==(const Manifest&) const = default;
};

struct CapturedScore {
    oc1_action_scoring::DecisionScore decision;
    // Descriptor-canonical rows, matching decision.actions. Every inner row
    // is world-major then rollout-major and hashes the canonical redacted H0
    // boundary projection: public state plus the observer-owned hidden zones.
    // This is stricter than a literal player observation because it includes
    // the sampled owner-library identity/order while redacting the opponent's
    // hand/library identities to counts.
    std::vector<std::vector<std::string>>
        h0_public_consequence_hashes;
    // Retained only by the focused testing seam. Scientific reports discard
    // these states and retain the redacted hashes above.
    std::vector<std::vector<LearnedPriorityH0Boundary>>
        h0_boundaries;

};

struct PairedContrast {
    std::string name;
    std::string positive_descriptor;
    std::string negative_descriptor;
    double mean = 0.0;
    double standard_error = 0.0;
    double lower_95 = 0.0;
    std::size_t positive_blocks = 0;
    std::size_t blocks = 0;
    std::size_t samples = 0;
    bool complete = false;
    bool passed = false;

};

struct ModelRootEvidence {
    std::string stable_id;
    std::string information_action_fingerprint;
    DeckId root_deck = DeckId::Green;
    CapturedScore score;
    bool descriptor_order_invariant = false;
    bool complete = false;

};

struct RootEvidence {
    std::string stable_id;
    std::string information_action_fingerprint;
    DeckId root_deck = DeckId::Green;
    bool from_dev_v3 = false;
    bool hidden_identity_changed = false;
    ModelRootEvidence c16;
    ModelRootEvidence oc1;
    bool hidden_scores_bit_identical = false;
    bool hidden_consequence_hashes_bit_identical = false;
    bool hidden_bit_identical = false;
    bool complete = false;

};

struct ScientificEvidence {
    std::string parent_model_fingerprint;
    std::string candidate_model_fingerprint;
    Manifest manifest;
    std::vector<RootEvidence> roots;
    // C16 is a localization control only; these predicates never gate the
    // OC1 teacher.
    std::array<PairedContrast, 3> c16_primary_contrasts;
    std::array<PairedContrast, 3> oc1_primary_contrasts;
    std::size_t passed_primary_contrasts = 0;
    std::size_t passed_support_controls = 0;
    std::size_t required_support_controls = 0;
    bool support_controls_passed = false;
    bool complete = false;
    bool passed = false;

};

struct HiddenAudit {
    std::array<std::size_t, kDeckCount> changed_roots_by_deck{};
    std::size_t attempted = 0;
    std::size_t changed = 0;
    bool nonvacuous_all_decks = false;
    bool scores_bit_identical = false;
    bool consequence_hashes_bit_identical = false;
    bool passed = false;

    bool operator==(const HiddenAudit&) const = default;
};

struct IntegrityReport {
    artifact_integrity::RegularFileSnapshot parent_before;
    artifact_integrity::RegularFileSnapshot candidate_before;
    artifact_integrity::RegularFileSnapshot parent_after;
    artifact_integrity::RegularFileSnapshot candidate_after;
    HiddenAudit hidden;
    bool artifact_requirements_match = false;
    bool model_identities_match = false;
    bool artifacts_unchanged = false;
    bool independent_manifest_bit_identical = false;
    bool repeated_construction_bit_identical = false;
    bool passed = false;

    bool operator==(const IntegrityReport&) const = default;
};

struct GateReport {
    bool integrity_passed = false;
    bool complete_evidence = false;
    bool scientific_passed = false;
    bool infrastructure_failure = false;
    bool passed = false;
    std::vector<std::string> failures;

    bool operator==(const GateReport&) const = default;
};

struct EvidenceBundle {
    std::string bytes;
    std::string payload_sha256;
    std::vector<std::string> section_names;
    std::vector<std::string> section_sha256;

    bool operator==(const EvidenceBundle&) const = default;
};

struct EvidencePublication {
    std::string path;
    std::uintmax_t byte_size = 0;
    std::string sha256;
    std::string payload_sha256;
    bool atomic_no_replace = false;
    bool published = false;

    bool operator==(const EvidencePublication&) const = default;
};

struct RunReport {
    ScientificEvidence scientific;
    IntegrityReport integrity;
    GateReport gate;
    EvidencePublication publication;

};

Manifest build_manifest();

PairedContrast paired_contrast(
    std::string name,
    const oc1_action_scoring::DescriptorScore& positive,
    const oc1_action_scoring::DescriptorScore& negative);

GateReport evaluate_gate(
    const ScientificEvidence& scientific,
    const IntegrityReport& integrity);
int exit_code(const GateReport& gate);

RunReport run(std::ostream& progress);
int run_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error);

namespace testing {

CapturedScore score_priority_root(
    const probes::DecisionProbe& probe,
    std::shared_ptr<const LearnedModel> model,
    const oc1_action_scoring::SearchRecipe& recipe =
        kSearchRecipe);

std::string h0_public_consequence_hash(
    const LearnedPriorityH0Boundary& boundary,
    std::size_t observer);

std::string manifest_root_contract_fingerprint(
    const probes::DecisionProbe& probe);

bool captured_score_bit_identical(
    const CapturedScore& first,
    const CapturedScore& second);

bool scientific_evidence_bit_identical(
    const ScientificEvidence& first,
    const ScientificEvidence& second);

EvidenceBundle serialize_evidence_bundle(
    const RunReport& report);

void write_evidence_atomic_no_replace(
    std::string_view path, std::string_view bytes);

// Writes evidence first. Summary bytes are emitted only after the atomic
// publication commit and output failures are swallowed thereafter.
void publish_evidence_and_emit_noexcept(
    std::string_view path, std::string_view bytes,
    std::string_view summary, std::ostream& output);

bool support_excludes_x_zero(
    const probes::DecisionProbe& probe,
    const oc1_action_scoring::DecisionScore& score);

std::vector<std::string> bit_exact_max_support(
    const std::vector<
        oc1_action_scoring::DescriptorScore>& actions);

} // namespace testing

} // namespace old_school::ac1_teacher_audit
