#pragma once

#include "old_school/fq4_d1_field_gate.hpp"
#include "old_school/fq4_priority_fit.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::fq4_d1_treatment {

inline constexpr std::string_view kTreatmentInputSchema =
    "old-school-fq4-d1-treatment-input-v1";
inline constexpr std::string_view kTreatmentEvidenceSchema =
    "old-school-fq4-d1-treatment-evidence-v1";
inline constexpr std::string_view kRequiredParentArtifactSha256 =
    "53aeb904bd87311b37201859317f05ab066bdfe134c72460cf94bff6d1f944ca";
inline constexpr std::string_view kExpectedTrajectorySha256 =
    "dee75b047182fd6463a7751e6344ccaba67dca1e76661c64aab58f22f4edbacd";
inline constexpr std::string_view kExpectedRetainedCorpusSha256 =
    "f3ae55a1b972c1d1adad101f5a54af210d3e8b5edd646738538aa839fa3f9883";
inline constexpr std::string_view kExpectedDominanceCorpusSha256 =
    "100296df1e0cba1023320b06f925437af35ef15a561c6861a993be80e8110605";
inline constexpr std::string_view kExpectedScoredCorpusSha256 =
    "5e063d1e577a1809f40e2ef2af992e79bba0339989054f7a94b31495ab047627";
inline constexpr std::string_view kExpectedAuditScoresSha256 =
    "47095c7722d65b8a349f77effe960892f1c9fc0061f12d3ae873771ebb6ca991";

inline constexpr std::size_t kExpectedPhysicalGames = 80;
inline constexpr std::size_t kExpectedOwnerPerspectives = 160;
inline constexpr std::size_t kExpectedRetainedRoots = 2408;
inline constexpr std::size_t kExpectedScoredRoots = 114;
inline constexpr std::size_t kExpectedDominanceTransitions = 59048;
inline constexpr std::size_t kExpectedScoreCalls = 129;
inline constexpr std::size_t kExpectedScoredActions = 907;
inline constexpr std::size_t kExpectedSampledWorlds = 1032;
inline constexpr std::size_t kExpectedRolloutEvaluations = 7256;
inline constexpr std::size_t kExpectedTerminalEvaluations = 1910;
inline constexpr std::size_t kExpectedBootstrapEvaluations = 5346;
inline constexpr std::size_t kExpectedHighConfidenceGames = 24;
inline constexpr std::size_t kExpectedHighConfidenceDecks = 4;
inline constexpr std::size_t kRequiredFullRepairs = 5;
inline constexpr std::size_t kRequiredRepairGames = 5;
inline constexpr std::size_t kRequiredRepairDecks = 2;
inline constexpr std::size_t kWatchdogSeconds = 240;
inline constexpr std::size_t kMaximumCandidateHighConfidence = 22;
inline constexpr std::size_t kMaximumCandidateUnsafe = 28;
inline constexpr std::size_t kMaximumCandidateClass1 = 10;
inline constexpr std::uint64_t kParentClass2SigmaMassBits =
    0x4328484d0ee397f4ULL;
inline constexpr double kParentClass2SigmaMass =
    3417447620267002.0;

// Class order is fixed as Safe, Class1, Class2, Class3.
inline constexpr std::array<std::array<std::size_t, 4>, kDeckCount>
    kExpectedParentClassesByDeck{{
        {{6, 1, 4, 2}},
        {{8, 0, 0, 0}},
        {{32, 1, 8, 3}},
        {{0, 6, 0, 0}},
        {{35, 2, 5, 1}},
    }};
inline constexpr std::array<std::size_t, 4>
    kExpectedPooledParentClasses{{81, 10, 17, 6}};

struct LabeledOption {
    std::string descriptor;
    std::vector<double> visible_tensor;
    std::vector<double> hidden_tensor;

    bool operator==(const LabeledOption&) const = default;
};

// This is the complete scientific-evaluator input. It deliberately contains
// no GameState, replay probe, source seed, source coordinate, or rollout
// configuration.
struct TreatmentRow {
    std::string stable_id;
    std::string physical_game_id;
    DeckId owner_deck = DeckId::Green;
    std::vector<std::string> canonical_descriptors;
    std::vector<LabeledOption> options;
    std::vector<bool> robustly_pass_dominated;
    std::vector<double> base_scores;
    std::vector<std::vector<double>> base_samples;
    std::vector<double> parent_residuals;
    std::vector<double> parent_combined_scores;
    fq4_d1_field_gate::ParentClassResult parent_class;

    bool operator==(const TreatmentRow&) const = default;
};

struct ClassCounts {
    std::array<std::size_t, 4> classes{};

    std::size_t safe() const;
    std::size_t class1() const;
    std::size_t class2() const;
    std::size_t class3() const;
    std::size_t high_confidence() const;
    std::size_t unsafe() const;
    std::size_t total() const;

    bool operator==(const ClassCounts&) const = default;
};

using TransitionMatrix =
    std::array<std::array<std::size_t, 4>, 4>;

struct AggregateResult {
    ClassCounts parent;
    ClassCounts candidate;
    TransitionMatrix transitions{};

    bool operator==(const AggregateResult&) const = default;
};

struct GateInput {
    std::array<AggregateResult, kDeckCount> decks;
    AggregateResult pooled;
    std::size_t full_repairs = 0;
    std::size_t distinct_repair_games = 0;
    std::size_t distinct_repair_decks = 0;
    std::size_t severity_regressions = 0;
    double candidate_class2_sigma_mass = 0.0;

    bool operator==(const GateInput&) const = default;
};

struct ScientificGates {
    bool repair_root_floor = false;
    bool repair_game_floor = false;
    bool repair_deck_floor = false;
    bool zero_severity_regressions = false;
    bool per_deck_nonregression = false;
    bool red_protected = false;
    bool pooled_high_confidence_bound = false;
    bool pooled_unsafe_bound = false;
    bool pooled_class1_bound = false;
    bool class2_sigma_nonregression = false;
    bool strict_registered_improvement = false;

    bool passed() const;
    bool operator==(const ScientificGates&) const = default;
};

struct TreatmentRootResult {
    std::string stable_id;
    std::string physical_game_id;
    DeckId owner_deck = DeckId::Green;
    fq4_d1_field_gate::ParentClassResult parent_class;
    fq4_d1_field_gate::ParentClassResult candidate_class;
    std::string parent_dominated_descriptor;
    std::string parent_nondominated_descriptor;
    std::string candidate_dominated_descriptor;
    std::string candidate_nondominated_descriptor;
    std::vector<double> parent_logits;
    std::vector<double> parent_residuals;
    std::vector<double> parent_combined_scores;
    std::vector<std::string> parent_exact_support;
    std::vector<double> candidate_logits;
    std::vector<double> candidate_residuals;
    std::vector<double> candidate_combined_scores;
    std::vector<std::string> candidate_exact_support;
    std::size_t parent_severity = 0;
    std::size_t candidate_severity = 0;
    bool full_repair = false;
    bool severity_regression = false;
    bool hidden_bit_identical = false;
    bool reverse_order_bit_identical = false;

    bool operator==(const TreatmentRootResult&) const = default;
};

struct TreatmentRolloutAccounting {
    std::size_t search_calls = 0;
    std::size_t sampled_worlds = 0;
    std::size_t rollout_evaluations = 0;
    std::size_t terminal_leaves = 0;
    std::size_t bootstrap_leaves = 0;
    std::size_t dominance_transitions = 0;

    bool zero() const;
    bool operator==(
        const TreatmentRolloutAccounting&) const = default;
};

struct ParentReconstructionSummary {
    std::string artifact_sha256;
    std::string model_fingerprint;
    LearnedModelComponentFingerprints model_components;
    std::string schedule_sha256;
    std::string trajectory_sha256;
    std::string retained_corpus_sha256;
    std::string dominance_corpus_sha256;
    std::string scored_corpus_sha256;
    std::string audit_scores_sha256;
    std::array<ClassCounts, kDeckCount> decks;
    ClassCounts pooled;
    fq4_d1_field_gate::ProductionAccounting primary_accounting;
    fq4_d1_field_gate::ProductionAccounting hidden_control_accounting;
    fq4_d1_field_gate::ProductionAccounting reverse_control_accounting;
    fq4_d1_field_gate::ProductionAccounting accounting;
    fq4_d1_field_gate::ProductionAccounting repeat_accounting;
    std::size_t physical_games = 0;
    std::size_t owner_perspectives = 0;
    std::size_t retained_roots = 0;
    std::size_t scored_roots = 0;
    double class2_sigma_mass = 0.0;
    std::size_t high_confidence_games = 0;
    std::size_t high_confidence_decks = 0;
    bool census_passed = false;
    bool hidden_replay_exact = false;
    bool hidden_feature_bits_identical = false;
    bool reverse_score_bits_identical = false;
    bool recipe_and_accounting_exact = false;
    bool count_cross_sums_exact = false;
    bool repeated_construction_bit_identical = false;
    bool exact = false;

    bool operator==(
        const ParentReconstructionSummary&) const = default;
};

struct D0bQualificationSummary {
    std::string parent_fingerprint;
    std::string anchor_candidate_fingerprint;
    std::string candidate_fingerprint;
    std::string training_input_sha256;
    LearnedModelComponentFingerprints parent_components;
    LearnedModelComponentFingerprints candidate_components;
    std::vector<double> parent_margins;
    std::vector<double> candidate_margins;
    std::vector<fq4_priority_fit::ControlReport> anchor_controls;
    std::vector<fq4_priority_fit::ControlReport> treatment_controls;
    std::size_t anchor_epochs = 0;
    std::size_t treatment_epochs = 0;
    std::size_t discovered_constraints = 0;
    std::size_t candidate_margins_at_gate = 0;
    double anchor_pooled_kl = 0.0;
    double treatment_pooled_kl = 0.0;
    bool report_passed = false;
    bool parent_contract_qualified = false;
    bool anchor_contract_qualified = false;
    bool optimizer_only_epochs_differ = false;
    bool checkpoint_inputs_bit_identical = false;
    bool target_kl_strictly_improved = false;
    bool candidate_fingerprint_exact = false;
    bool only_priority_component_changed = false;
    bool repeated_fit_bit_identical = false;
    bool hidden_repartition_bit_identical = false;
    bool action_order_bit_identical = false;
    bool immutable_base_and_accounting = false;
    bool every_control_passed = false;
    bool exact = false;

    bool operator==(
        const D0bQualificationSummary&) const = default;
};

struct PhaseTimings {
    double parent_reconstruction_seconds = 0.0;
    double d0b_fit_seconds = 0.0;
    double tensor_evaluation_seconds = 0.0;
    double total_seconds = 0.0;

    bool operator==(const PhaseTimings&) const = default;
};

struct TreatmentReport {
    bool production_contracts_required = false;
    ParentReconstructionSummary parent_reconstruction;
    D0bQualificationSummary d0b;
    std::string parent_fingerprint;
    std::string candidate_fingerprint;
    LearnedModelComponentFingerprints parent_components;
    LearnedModelComponentFingerprints candidate_components;
    std::string treatment_input_sha256;
    std::string evidence_sha256;
    std::vector<TreatmentRootResult> roots;
    std::array<AggregateResult, kDeckCount> decks;
    AggregateResult pooled;
    std::size_t full_repairs = 0;
    std::size_t distinct_repair_games = 0;
    std::size_t distinct_repair_decks = 0;
    std::size_t severity_regressions = 0;
    double candidate_class2_sigma_mass = 0.0;
    ScientificGates gates;
    TreatmentRolloutAccounting treatment_accounting;
    PhaseTimings timings;
    bool row_shape_valid = false;
    bool parent_reproduced = false;
    bool only_priority_component_changed = false;
    bool hidden_bit_identical = false;
    bool reverse_order_bit_identical = false;
    bool repeated_evaluation_bit_identical = false;
    bool zero_treatment_rollout_accounting = false;
    std::vector<std::string> infrastructure_failures;
    std::vector<std::string> scientific_failures;

    bool infrastructure_valid() const;
    bool passed() const;
};

enum class ExitClassification : int {
    Pass = 0,
    ScientificReject = 1,
    InfrastructureFailure = 2,
};

ExitClassification classify_exit(const TreatmentReport& report);

namespace production_detail {

// Narrow bridge from the source-owning production adapter into the
// tensor-only evaluator. The evaluator never receives census rows, replay
// locators, seeds, GameState, or rollout/search configuration.
TreatmentReport evaluate_stripped_rows(
    const std::vector<TreatmentRow>& rows,
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate,
    const ParentReconstructionSummary& parent_reconstruction,
    const D0bQualificationSummary& d0b);

} // namespace production_detail

// The caller supplies the already-snapshotted artifact digest and immutable
// model. This function performs the complete P0 census and exact comparison
// before its one call to fit_d0b_production().
TreatmentReport run_production(
    std::shared_ptr<const LearnedModel> frozen_c16,
    std::string_view parent_artifact_sha256);

namespace testing {

struct RowAdaptation {
    std::vector<TreatmentRow> rows;
    std::vector<std::string> infrastructure_failures;

    bool valid() const;
};

// These portable seams never open the P0 source schedule or construct D0b.
RowAdaptation strip_scored_roots(
    const std::vector<fq4_d1_field_gate::ScoredRoot>& roots);
TreatmentReport evaluate(
    const std::vector<TreatmentRow>& rows,
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate);
ScientificGates evaluate_scientific_gates(
    const GateInput& input);

// Exposed only to bind the exact canonical serializers with golden and
// mutation tests. Production uses these same implementations.
std::string treatment_input_sha256(
    const ParentReconstructionSummary& parent,
    const std::vector<TreatmentRow>& rows,
    std::shared_ptr<const LearnedModel> parent_model);
std::string treatment_evidence_sha256(
    const TreatmentReport& report);

} // namespace testing

} // namespace old_school::fq4_d1_treatment
