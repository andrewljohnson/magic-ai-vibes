#pragma once

#include "old_school/game.hpp"
#include "old_school/probes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace old_school::action_q_recursive_policy_improvement {

inline constexpr std::uint64_t kPreflightSeed =
    202607290401ULL;
inline constexpr std::size_t kFixtureCount = 8;
inline constexpr std::size_t kFixtureRootCount = 12;
inline constexpr std::size_t kMaximumActiveNesting = 1;
inline constexpr std::size_t kBridgeTimeoutSeconds = 120;
inline constexpr std::string_view kRequiredParentFingerprint =
    "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f";
inline constexpr std::string_view kRequiredParentPath =
    "build/model-cache/"
    "old-school-value-challenger-v3-c16-t800-s424242.bin";
inline constexpr std::uintmax_t kRequiredParentBytes = 3'111'437;
inline constexpr std::string_view kRequiredParentSha256 =
    "53aeb904bd87311b37201859317f05ab066bdfe134c72460cf9"
    "4bff6d1f944ca";
inline constexpr std::string_view kPilotLabel =
    "Learned C16 · Recursive Foresight (AQ5)";
inline constexpr std::string_view kPilotQualification =
    "manual diagnostic / not promoted";
inline constexpr std::string_view kNewBlueBlockFixtureId =
    "diagnostic.blue.life20-flying-men-first-block-air-with-air.v1";

enum class DecisionFamily : std::uint8_t {
    Priority,
    Attack,
    Block,
};

enum class DirectionKind : std::uint8_t {
    CounterComposition,
    BraingeyserTargetAndX,
    AncestralTarget,
    GiantGrowthTimingAndTarget,
    ForceSpikeTax,
    LifeSensitiveBlock,
    AvoidBadAttack,
    MultiChoiceBlock,
};

struct SearchBudget {
    std::size_t worlds = 1;
    std::size_t rollouts_per_world = 1;
    std::size_t horizon_turns = 1;
    std::size_t evaluation_threads = 1;
    std::size_t continuation_worlds = 0;
    bool blend_shallow_prior = false;

    bool operator==(const SearchBudget&) const = default;
};

struct SealedRecipe {
    std::uint64_t root_seed = kPreflightSeed;
    SearchBudget priority_outer;
    SearchBudget combat_outer;
    SearchBudget priority_inner;
    SearchBudget combat_inner;
    std::size_t maximum_active_nesting =
        kMaximumActiveNesting;
    bool treatment_default_off = true;
    bool actor_local_redeterminization = true;
    bool candidate_identity_absent_from_seeds = true;
    bool inner_priority_blends_shallow_prior = true;
    bool inner_combat_unblended = true;
    bool damage_order_uses_c16 = true;
    bool cleanup_uses_c16 = true;

    bool operator==(const SealedRecipe&) const = default;
};

struct FixtureSpec {
    std::size_t ordinal = 0;
    DecisionFamily family = DecisionFamily::Priority;
    DirectionKind direction = DirectionKind::CounterComposition;
    std::array<std::string_view, 2> stable_ids{};
    std::size_t root_count = 0;
    std::uint64_t expected_search_seed = 0;
    std::uint64_t expected_tie_seed = 0;

    bool operator==(const FixtureSpec&) const = default;
};

struct PreparedRoot {
    std::size_t fixture_ordinal = 0;
    std::string stable_id;
    DecisionFamily family = DecisionFamily::Priority;
    GameState state;
    std::array<std::vector<CardId>, 2> original_decks;
    std::size_t actor = 0;
    TurnPhase phase = TurnPhase::FirstMain;
    int consecutive_passes = 0;
    bool sorcery_actions = false;
    std::vector<std::string> candidate_keys;
    std::vector<probes::CandidateAction> candidates;

    // Attack context. Candidates are canonical Skip/Include decisions for
    // `subject`, with the prefix and suffix held explicitly.
    std::vector<PermanentId> selected_attackers;
    PermanentId subject = 0;
    std::vector<PermanentId> remaining_attackers;

    // Block context. Candidates are No Block followed by assignments of
    // `subject_blocker`; the remaining suffix is completed before scoring.
    std::vector<PermanentId> attackers;
    std::vector<std::pair<PermanentId, PermanentId>>
        selected_blocks;
    PermanentId subject_blocker = 0;
    std::vector<PermanentId> remaining_blockers;

    bool operator==(const PreparedRoot&) const = default;
};

struct SearchAccounting {
    std::size_t sampled_worlds = 0;
    std::size_t rollout_evaluations = 0;
    std::size_t terminal_evaluations = 0;
    std::size_t bootstrapped_evaluations = 0;
    std::size_t inner_rollout_evaluations = 0;
    std::size_t inner_search_invocations = 0;
    std::size_t inner_search_max_depth = 0;

    bool operator==(const SearchAccounting&) const = default;
};

struct CandidateScore {
    std::string key;
    std::vector<double> samples;
    std::vector<std::uint8_t> terminal_evaluation_flags;
    // Optional AQ6 evaluation-only view of the engine-settled immediate
    // consequence. It is absent in the sealed AQ5-RPI0 run and never changes
    // the AQ5 exact-max selection.
    std::vector<double> settled_boundary_samples;
    std::optional<double> settled_boundary_mean;
    // Optional AQ6 exact-combat witnesses. They retain the engine sampler's
    // candidate/world/rollout shape after canonical action-key remapping.
    // Priority and the default legacy combat path leave both empty.
    std::vector<std::uint8_t>
        exact_combat_pure_chump_flags;
    std::vector<std::uint8_t>
        exact_combat_bound_fallback_flags;
    double mean = 0.0;
    bool exact_max = false;

    bool operator==(const CandidateScore&) const = default;
};

// The adapter returns action-keyed rows. `legal_choice_count` is the complete
// authoritative width at this microdecision, before any caller reordering.
struct SamplerOutput {
    std::vector<CandidateScore> candidates;
    SearchAccounting accounting;
    std::size_t legal_choice_count = 0;
    bool rules_settled = false;
    bool accounting_consistent = false;

    bool operator==(const SamplerOutput&) const = default;
};

struct UntreatedC16RootReport {
    std::string stable_id;
    DecisionFamily family = DecisionFamily::Priority;
    std::vector<CandidateScore> candidates;
    std::string selected_key;
    double selected_margin = 0.0;
    bool complete_legal_choice_coverage = false;
    bool finite_scores = false;

    bool operator==(const UntreatedC16RootReport&) const =
        default;
};

struct UntreatedC16Report {
    std::string parent_fingerprint;
    std::vector<UntreatedC16RootReport> roots;
    bool captured_before_rpi = false;

    bool gate_passed() const;
};

struct RootReport {
    std::string stable_id;
    DecisionFamily family = DecisionFamily::Priority;
    std::vector<CandidateScore> candidates;
    std::string selected_key;
    SearchAccounting accounting;
    bool complete_legal_choice_coverage = false;
    bool rules_settled = false;
    bool finite_scores = false;
    bool accounting_consistent = false;
    bool reversed_input_action_keyed_bit_identical = false;
    bool hidden_repartition_nonvacuous = false;
    bool hidden_observation_bit_identical = false;
    bool hidden_scores_bit_identical = false;
    bool hidden_choice_bit_identical = false;
    bool hidden_accounting_bit_identical = false;

    bool gate_passed() const;
};

struct FixtureReport {
    FixtureSpec spec;
    std::vector<RootReport> roots;
    bool direction_passed = false;

    bool gate_passed() const;
};

struct FamilyInvariantReport {
    DecisionFamily family = DecisionFamily::Priority;
    std::size_t roots = 0;
    bool complete_legal_choice_coverage = false;
    bool rules_settled = false;
    bool finite_scores = false;
    bool accounting_consistent = false;
    bool reversed_input_action_keyed_bit_identical = false;
    bool hidden_repartition_nonvacuous = false;
    bool hidden_observation_bit_identical = false;
    bool hidden_scores_bit_identical = false;
    bool hidden_choice_bit_identical = false;
    bool hidden_accounting_bit_identical = false;
    bool one_level_nesting_bounded = false;
    std::size_t maximum_active_nesting = 0;

    bool gate_passed() const;
};

struct IsolationReport {
    bool exact_parent = false;
    bool treatment_default_off = false;
    bool exact_configuration = false;
    bool all_other_treatments_off = false;
    bool treatment_off_fixed_seed_game_bit_identical = false;

    bool gate_passed() const;
};

struct PreflightReport {
    SealedRecipe recipe;
    std::string parent_fingerprint;
    UntreatedC16Report untreated;
    std::array<FixtureReport, kFixtureCount> fixtures;
    std::array<FamilyInvariantReport, 3> families;
    IsolationReport isolation;
    bool hypothesis_passed = false;

    bool gate_passed() const;
};

struct BridgeSmokeReport {
    DecisionFamily family = DecisionFamily::Priority;
    std::string fixture_id;
    std::size_t elapsed_seconds = 0;
    bool authoritative_decision_completed = false;

    bool gate_passed() const;
};

struct BridgeReport {
    std::array<BridgeSmokeReport, 3> smokes;
    bool web_bridge_tests_passed = false;
    bool web_ui_tests_passed = false;

    bool gate_passed() const;
};

using RpiSampler = std::function<SamplerOutput(
    const PreparedRoot&,
    std::shared_ptr<const LearnedModel>,
    LearnedSearchConfig)>;
using UntreatedC16Capture = std::function<
    UntreatedC16RootReport(
        const PreparedRoot&,
        std::shared_ptr<const LearnedModel>,
        std::uint64_t search_seed,
        std::uint64_t tie_seed)>;
using TreatmentOffIdentityCheck = std::function<bool(
    std::shared_ptr<const LearnedModel>)>;

// The default adapter binds the three engine sampler APIs, exact untreated
// C16 descriptive capture, and the fixed-seed treatment-off identity check.
struct SamplerApi {
    RpiSampler score_rpi;
    UntreatedC16Capture capture_untreated_c16;
    TreatmentOffIdentityCheck
        treatment_off_fixed_seed_game_bit_identical;
};

SealedRecipe sealed_recipe();
std::array<FixtureSpec, kFixtureCount> fixture_manifest();
std::uint64_t search_seed(
    std::size_t fixture_ordinal, DecisionFamily family);
std::uint64_t tie_seed(
    std::size_t fixture_ordinal, DecisionFamily family);
LearnedSearchConfig outer_search_config(
    DecisionFamily family, std::uint64_t seed);
BotConfig treatment_bot_config(
    std::shared_ptr<const LearnedModel> parent);

PreparedRoot make_blue_multi_choice_block_fixture();
std::vector<PreparedRoot> build_fixture_roots();
PreparedRoot make_hidden_repartition_clone(
    const PreparedRoot& root);
PreparedRoot reverse_candidate_order(
    const PreparedRoot& root);

bool fixture_direction_passed(
    const FixtureSpec& spec,
    const std::vector<RootReport>& roots);
bool public_option_licensed(
    const PreflightReport& preflight,
    const BridgeReport& bridge);

SamplerApi engine_sampler_api();
PreflightReport run_preflight(
    std::shared_ptr<const LearnedModel> parent,
    const SamplerApi& samplers);
PreflightReport run_preflight(
    std::shared_ptr<const LearnedModel> parent);

} // namespace old_school::action_q_recursive_policy_improvement
