#include "old_school/ac1_teacher_audit.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/oc1_action_regression.hpp"
#include "old_school/probe_runner.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <unistd.h>

namespace {

namespace ac1 = old_school::ac1_teacher_audit;
namespace ar1 = old_school::oc1_action_regression;
namespace probes = old_school::probes;
namespace scoring = old_school::oc1_action_scoring;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Exception = std::exception,
          typename Function>
void expect_throws(
    Function&& function, std::string_view message) {
    try {
        std::forward<Function>(function)();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

bool same_bits(double first, double second) {
    return std::bit_cast<std::uint64_t>(first) ==
           std::bit_cast<std::uint64_t>(second);
}

std::shared_ptr<const old_school::LearnedModel> test_model() {
    static const auto model =
        old_school::train_learned_value_champion(
            1, 0x41433154455354ULL);
    return model;
}

scoring::SearchRecipe small_recipe() {
    return {
        .seed_tag = "old-school-ac1-t0-unit-v1",
        .seed_base = 0xAC100001ULL,
        .worlds = 2,
        .horizon_turns = 0,
        .rollouts_per_world = 1,
        .blend_shallow_prior = false,
        .evaluation_threads = 2,
    };
}

const ac1::ManifestRoot& manifest_root(
    const ac1::Manifest& manifest,
    std::string_view stable_id) {
    const auto found = std::find_if(
        manifest.roots.begin(), manifest.roots.end(),
        [&](const ac1::ManifestRoot& root) {
            return root.probe.stable_id == stable_id;
        });
    if (found == manifest.roots.end()) {
        throw std::runtime_error("manifest root is missing");
    }
    return *found;
}

std::size_t count_occurrences(
    std::string_view text, std::string_view needle) {
    std::size_t count = 0;
    std::size_t position = 0;
    while ((position = text.find(needle, position)) !=
           std::string_view::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        path_ =
            std::filesystem::temp_directory_path() /
            ("old-school-ac1-tests-" +
             std::to_string(
                 static_cast<unsigned long long>(::getpid())) +
             "-" + std::to_string(next_++));
        std::filesystem::create_directory(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

  private:
    inline static std::size_t next_ = 0;
    std::filesystem::path path_;
};

class ThrowingStreamBuffer final : public std::streambuf {
  protected:
    std::streamsize xsputn(
        const char*, std::streamsize) override {
        throw std::runtime_error("injected output failure");
    }
};

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}

void test_manifest_is_exact_and_frozen() {
    const ac1::Manifest first = ac1::build_manifest();
    const ac1::Manifest second = ac1::build_manifest();
    expect(first == second,
           "independent manifest construction drifted");
    expect(first.exact, "manifest is not exact");
    expect(first.roots.size() ==
               ac1::kPhysicalPriorityRoots,
           "physical Priority root count drifted");
    expect(first.logical_priority_ids ==
               ac1::kLogicalPriorityIds,
           "logical Priority ID count drifted");
    expect(first.physical_roots_by_deck ==
               std::array<std::size_t,
                          old_school::kDeckCount>{
                   6, 4, 8, 4, 4},
           "physical deck census drifted");
    expect(first.logical_dev_roots_by_deck ==
               std::array<std::size_t,
                          old_school::kDeckCount>{
                   4, 4, 4, 4, 4},
           "logical dev deck census drifted");
    expect(first.dev_force_spike_alias ==
               ac1::kDevForceSpikeAlias &&
               first.canonical_live_force_spike ==
                   ac1::kCanonicalLiveForceSpike,
           "Force Spike alias mapping drifted");
    expect(first.attack_stable_id ==
               ac1::kDevAttackStableId &&
               first.attack_information_action_fingerprint ==
                   ac1::kDevAttackFingerprint,
           "Attack census identity drifted");
    expect(
        std::none_of(
            first.roots.begin(), first.roots.end(),
            [](const ac1::ManifestRoot& root) {
                return root.probe.stable_id ==
                       ac1::kDevForceSpikeAlias;
            }),
        "logical Force Spike alias was scored physically");
    expect(
        manifest_root(first, ac1::kCanonicalLiveForceSpike)
                .information_action_fingerprint ==
            "b792d7434096d2cc",
        "canonical live Force Spike IA drifted");
    for (std::size_t index = 0;
         index < first.roots.size(); ++index) {
        const auto& root = first.roots[index];
        expect(
            root.factory_contract_fingerprint.size() == 64,
            "root contract is not SHA-256");
        if (index != 0) {
            expect(
                first.roots[index - 1].probe.stable_id <
                    root.probe.stable_id,
                "manifest report order is not stable");
        }
    }
}

void test_manifest_contract_covers_order_and_typed_actions() {
    probes::DecisionProbe probe =
        probes::make_force_spike_policy_controls_v1().front();
    const std::string original =
        ac1::testing::manifest_root_contract_fingerprint(
            probe);
    expect(
        original ==
            ac1::testing::manifest_root_contract_fingerprint(
                probe),
        "manifest contract is nondeterministic");

    std::reverse(
        probe.candidates.begin(), probe.candidates.end());
    expect(
        original !=
            ac1::testing::manifest_root_contract_fingerprint(
                probe),
        "factory descriptor order is not in the contract");
    std::reverse(
        probe.candidates.begin(), probe.candidates.end());
    auto& action =
        std::get<old_school::PriorityAction>(
            probe.candidates.front().action);
    action.x_value += 1;
    expect(
        original !=
            ac1::testing::manifest_root_contract_fingerprint(
                probe),
        "typed action is not in the manifest contract");
}

void test_scorer_exercises_descriptor_order_and_hidden_invariance() {
    probes::DecisionProbe probe =
        probes::make_force_spike_policy_controls_v1().front();
    const auto original =
        ac1::testing::score_priority_root(
            probe, test_model(), small_recipe());
    expect(
        original.decision.actions.size() ==
            probe.candidates.size(),
        "AC1 scorer omitted an action");
    expect(
        original.h0_boundaries.size() ==
                probe.candidates.size() &&
            original.h0_public_consequence_hashes.size() ==
                probe.candidates.size(),
        "AC1 scorer omitted H0 boundary evidence");
    for (std::size_t action = 0;
         action < original.decision.actions.size();
         ++action) {
        expect(
            original.decision.actions[action]
                    .raw_samples.size() == 2 &&
                original.h0_boundaries[action].size() == 2 &&
                original.h0_public_consequence_hashes[action]
                        .size() == 2,
            "small AC1 score row width is wrong");
        if (action != 0) {
            expect(
                original.decision.actions[action - 1]
                        .descriptor <
                    original.decision.actions[action]
                        .descriptor,
                "AC1 output rows are not descriptor-canonical");
        }
    }

    std::reverse(
        probe.candidates.begin(), probe.candidates.end());
    const auto reversed =
        ac1::testing::score_priority_root(
            probe, test_model(), small_recipe());
    expect(
        ac1::testing::captured_score_bit_identical(
            original, reversed),
        "caller-order execution changed canonical score evidence");

    probe.state =
        old_school::probe_runner::hidden_repartition_clone(
            probe);
    const auto hidden =
        ac1::testing::score_priority_root(
            probe, test_model(), small_recipe());
    expect(
        ac1::testing::captured_score_bit_identical(
            original, hidden),
        "opponent hidden repartition changed AC1 evidence");
}

scoring::DescriptorScore constant_score(
    std::string descriptor, double value) {
    return {
        .descriptor = std::move(descriptor),
        .raw_samples =
            std::vector<double>(
                ac1::kSamplesPerAction, value),
        .raw_score = value,
    };
}

void test_paired_contrast_exact_statistics_and_fail_closed() {
    const auto positive = constant_score("positive", 0.75);
    const auto negative = constant_score("negative", 0.25);
    const auto passed =
        ac1::paired_contrast(
            "constant", positive, negative);
    expect(
        same_bits(passed.mean, 0.5) &&
            same_bits(passed.standard_error, 0.0) &&
            same_bits(passed.lower_95, 0.5) &&
            passed.positive_blocks == ac1::kBlocks &&
            passed.complete && passed.passed,
        "zero-variance paired contrast is wrong");

    auto five_blocks = constant_score("five", -0.1);
    for (std::size_t index = 0;
         index < 5 * ac1::kSamplesPerBlock; ++index) {
        five_blocks.raw_samples[index] = 1.0;
    }
    const auto block_failure =
        ac1::paired_contrast(
            "five-blocks", five_blocks,
            constant_score("zero", 0.0));
    expect(
        block_failure.mean > 0.0 &&
            block_failure.lower_95 > 0.0 &&
            block_failure.positive_blocks == 5 &&
            !block_failure.passed,
        "six-of-eight block predicate was not enforced");

    const auto tie =
        ac1::paired_contrast(
            "tie", constant_score("a", 0.25),
            constant_score("b", 0.25));
    expect(
        same_bits(tie.mean, 0.0) &&
            same_bits(tie.lower_95, 0.0) &&
            !tie.passed,
        "exact tie did not fail scientifically");

    auto incomplete = positive;
    incomplete.raw_samples.pop_back();
    expect_throws<std::invalid_argument>(
        [&] {
            static_cast<void>(ac1::paired_contrast(
                "incomplete", incomplete, negative));
        },
        "incomplete contrast was accepted");
    auto nonfinite = positive;
    nonfinite.raw_samples.front() =
        std::numeric_limits<double>::quiet_NaN();
    expect_throws<std::runtime_error>(
        [&] {
            static_cast<void>(ac1::paired_contrast(
                "nonfinite", nonfinite, negative));
        },
        "non-finite contrast was accepted");
}

void test_support_uses_ieee_bit_exact_ties() {
    std::vector<scoring::DescriptorScore> actions = {
        {
            .descriptor = "a-negative-zero",
            .raw_score = -0.0,
        },
        {
            .descriptor = "b-positive-zero",
            .raw_score = 0.0,
        },
    };
    expect(
        ac1::testing::bit_exact_max_support(actions) ==
            std::vector<std::string>{"a-negative-zero"},
        "AC1 support collapsed signed-zero score bits");
    actions[0].raw_score = 0.0;
    expect(
        ac1::testing::bit_exact_max_support(actions) ==
            std::vector<std::string>{
                "a-negative-zero", "b-positive-zero"},
        "bit-exact equal maxima did not share support");

    std::reverse(actions.begin(), actions.end());
    expect_throws<std::invalid_argument>(
        [&] {
            static_cast<void>(
                ac1::testing::bit_exact_max_support(
                    actions));
        },
        "noncanonical descriptors were accepted");
    std::reverse(actions.begin(), actions.end());
    actions[1].raw_score =
        std::numeric_limits<double>::infinity();
    expect_throws<std::invalid_argument>(
        [&] {
            static_cast<void>(
                ac1::testing::bit_exact_max_support(
                    actions));
        },
        "non-finite maximum was accepted");
}

void test_public_consequence_hash_redacts_only_opponent_hidden() {
    old_school::LearnedPriorityH0Boundary boundary;
    boundary.context = {
        .valid = true,
        .phase = old_school::TurnPhase::FirstMain,
        .decision_player = 1,
        .consecutive_passes = 0,
        .sorcery_actions = true,
    };
    boundary.state.active_player = 1;
    boundary.state.starting_player = 0;
    boundary.state.turn_number = 4;
    boundary.state.players[0].hand = {
        old_school::CardId::Forest};
    boundary.state.players[0].library = {
        old_school::CardId::GrizzlyBears,
        old_school::CardId::GiantGrowth};
    boundary.state.players[1].hand = {
        old_school::CardId::Island,
        old_school::CardId::FlyingMen};
    boundary.state.players[1].library = {
        old_school::CardId::Counterspell,
        old_school::CardId::ForceSpike};
    const std::string original =
        ac1::testing::h0_public_consequence_hash(
            boundary, 0);

    auto opponent_repartition = boundary;
    std::swap(
        opponent_repartition.state.players[1].hand[0],
        opponent_repartition.state.players[1].library[0]);
    expect(
        original ==
            ac1::testing::h0_public_consequence_hash(
                opponent_repartition, 0),
        "opponent hidden identities leaked into H0 hash");

    auto own_library = boundary;
    std::reverse(
        own_library.state.players[0].library.begin(),
        own_library.state.players[0].library.end());
    expect(
        original !=
            ac1::testing::h0_public_consequence_hash(
                own_library, 0),
        "owner library identity/order is absent from H0 hash");

    auto public_change = boundary;
    --public_change.state.players[1].life;
    expect(
        original !=
            ac1::testing::h0_public_consequence_hash(
                public_change, 0),
        "public life is absent from H0 hash");

    auto reporting_only = boundary;
    ++reporting_only.state.stats[0].decisions;
    expect(
        original ==
            ac1::testing::h0_public_consequence_hash(
                reporting_only, 0),
        "reporting-only stats contaminated H0 hash");
}

void test_support_x_zero_control_is_typed() {
    const probes::DecisionProbe probe =
        probes::make_braingeyser_x_zero_control_v1().front();
    scoring::DecisionScore score;
    score.stable_id = probe.stable_id;
    score.selected_support = {"pass"};
    expect(
        ac1::testing::support_excludes_x_zero(
            probe, score),
        "Pass was mistaken for an X=0 spell");

    const auto productive = std::find_if(
        probe.candidates.begin(), probe.candidates.end(),
        [](const probes::Candidate& candidate) {
            const auto* action =
                std::get_if<old_school::PriorityAction>(
                    &candidate.action);
            return action != nullptr &&
                   action->kind ==
                       old_school::PriorityActionKind::
                           CastBraingeyser &&
                   action->x_value > 0;
        });
    const auto waste = std::find_if(
        probe.candidates.begin(), probe.candidates.end(),
        [](const probes::Candidate& candidate) {
            const auto* action =
                std::get_if<old_school::PriorityAction>(
                    &candidate.action);
            return action != nullptr &&
                   action->kind ==
                       old_school::PriorityActionKind::
                           CastBraingeyser &&
                   action->x_value == 0;
        });
    expect(
        productive != probe.candidates.end() &&
            waste != probe.candidates.end(),
        "Braingeyser typed controls are missing");
    score.selected_support = {productive->descriptor};
    expect(
        ac1::testing::support_excludes_x_zero(
            probe, score),
        "productive Braingeyser was rejected");
    score.selected_support = {waste->descriptor};
    expect(
        !ac1::testing::support_excludes_x_zero(
            probe, score),
        "Braingeyser X=0 was accepted");
}

void test_gate_exit_semantics() {
    ac1::ScientificEvidence scientific;
    scientific.complete = true;
    scientific.passed = true;
    scientific.passed_primary_contrasts = 3;
    scientific.support_controls_passed = true;
    ac1::IntegrityReport integrity;
    integrity.passed = true;
    ac1::GateReport gate =
        ac1::evaluate_gate(scientific, integrity);
    expect(
        gate.passed && ac1::exit_code(gate) == 0,
        "complete passing evidence did not exit 0");

    scientific.passed = false;
    scientific.passed_primary_contrasts = 2;
    gate = ac1::evaluate_gate(scientific, integrity);
    expect(
        !gate.infrastructure_failure &&
            ac1::exit_code(gate) == 1,
        "complete scientific failure did not exit 1");

    scientific.complete = false;
    gate = ac1::evaluate_gate(scientific, integrity);
    expect(
        gate.infrastructure_failure &&
            ac1::exit_code(gate) == 2,
        "incomplete evidence did not exit 2");

    scientific.complete = true;
    integrity.passed = false;
    gate = ac1::evaluate_gate(scientific, integrity);
    expect(
        gate.infrastructure_failure &&
            ac1::exit_code(gate) == 2,
        "integrity failure did not exit 2");
}

ac1::ModelRootEvidence fake_model_evidence(
    const ac1::ManifestRoot& manifest_root_value,
    double offset) {
    std::vector<probes::Candidate> candidates =
        manifest_root_value.probe.candidates;
    std::sort(
        candidates.begin(), candidates.end(),
        [](const probes::Candidate& left,
           const probes::Candidate& right) {
            return left.descriptor < right.descriptor;
        });
    ac1::ModelRootEvidence model;
    model.stable_id =
        manifest_root_value.probe.stable_id;
    model.information_action_fingerprint =
        manifest_root_value.information_action_fingerprint;
    model.root_deck =
        manifest_root_value.probe.root_deck;
    model.descriptor_order_invariant = true;
    model.complete = true;
    auto& decision = model.score.decision;
    decision.stable_id = model.stable_id;
    decision.decision_kind =
        probes::DecisionKind::Priority;
    decision.score_mode =
        scoring::ScoreMode::ReferenceSearch;
    decision.recipe = {
        .seed_source = scoring::SeedSource::Derived,
        .seed_tag = std::string(ac1::kSeedTag),
        .seed_base = ac1::kSeedBase,
        .resolved_seed =
            old_school::probe_runner::
                reference_seed_for_probe(
                    ac1::kSeedTag,
                    manifest_root_value.probe.stable_id,
                    ac1::kSeedBase),
        .worlds = ac1::kWorlds,
        .horizon_turns = 0,
        .rollouts_per_world = 1,
        .blend_shallow_prior = false,
        .evaluation_threads = ac1::kEvaluationThreads,
        .value_mirror = true,
        .value_continuation_epsilon = 0.0,
        .value_priority_residual_weight = 0.0,
        .value_pass_dominance = false,
        .value_continuation_controller =
            old_school::LearnedContinuationController::Legacy,
    };
    decision.accounting.sampled_worlds = ac1::kWorlds;
    decision.accounting.rollout_evaluations =
        candidates.size() * ac1::kSamplesPerAction;
    decision.accounting.bootstrapped_evaluations =
        decision.accounting.rollout_evaluations;
    for (std::size_t index = 0;
         index < candidates.size(); ++index) {
        double value =
            offset + static_cast<double>(index) / 100.0;
        if (index == 0 && offset == 0.0) {
            value = -0.0;
        }
        decision.actions.push_back({
            .descriptor = candidates[index].descriptor,
            .raw_samples =
                std::vector<double>(
                    ac1::kSamplesPerAction, value),
            .raw_score = value,
        });
        model.score.h0_public_consequence_hashes.push_back(
            std::vector<std::string>(
                ac1::kSamplesPerAction,
                std::string(64, static_cast<char>(
                    "0123456789abcdef"[index % 16]))));
    }
    decision.selected_support = {
        decision.actions.back().descriptor};
    return model;
}

ac1::RunReport fake_complete_report() {
    ac1::RunReport report;
    report.scientific.parent_model_fingerprint =
        std::string(ar1::kParentModelFingerprint);
    report.scientific.candidate_model_fingerprint =
        std::string(ar1::kCandidateModelFingerprint);
    report.scientific.manifest = ac1::build_manifest();
    for (const auto& manifest_root_value :
         report.scientific.manifest.roots) {
        ac1::RootEvidence root{
            .stable_id =
                manifest_root_value.probe.stable_id,
            .information_action_fingerprint =
                manifest_root_value
                    .information_action_fingerprint,
            .root_deck =
                manifest_root_value.probe.root_deck,
            .from_dev_v3 =
                manifest_root_value.from_dev_v3,
            .hidden_identity_changed = true,
            .c16 = fake_model_evidence(
                manifest_root_value, 0.0),
            .oc1 = fake_model_evidence(
                manifest_root_value, 0.1),
            .hidden_scores_bit_identical = true,
            .hidden_consequence_hashes_bit_identical =
                true,
            .hidden_bit_identical = true,
            .complete = true,
        };
        report.scientific.roots.push_back(
            std::move(root));
    }
    const auto positive =
        constant_score("positive", 0.75);
    const auto negative =
        constant_score("negative", 0.25);
    for (std::size_t index = 0; index < 3; ++index) {
        report.scientific.c16_primary_contrasts[index] =
            ac1::paired_contrast(
                "c16-" + std::to_string(index),
                positive, negative);
        report.scientific.oc1_primary_contrasts[index] =
            ac1::paired_contrast(
                "oc1-" + std::to_string(index),
                positive, negative);
    }
    report.scientific.passed_primary_contrasts = 3;
    report.scientific.passed_support_controls = 8;
    report.scientific.required_support_controls = 8;
    report.scientific.support_controls_passed = true;
    report.scientific.complete = true;
    report.scientific.passed = true;

    const auto snapshot =
        [](std::string path, std::uintmax_t bytes,
           char digest_character) {
            old_school::artifact_integrity::
                RegularFileSnapshot value;
            value.path = std::move(path);
            value.physical_path = value.path;
            value.byte_size = bytes;
            value.sha256 =
                std::string(64, digest_character);
            return value;
        };
    report.integrity.parent_before =
        snapshot(
            "/tree-a/parent.bin",
            ar1::kParentArtifactBytes, '3');
    report.integrity.parent_before.sha256 =
        std::string(ar1::kParentArtifactSha256);
    report.integrity.parent_after =
        report.integrity.parent_before;
    report.integrity.candidate_before =
        snapshot(
            "/tree-a/candidate.bin",
            ar1::kCandidateArtifactBytes, '4');
    report.integrity.candidate_before.sha256 =
        std::string(ar1::kCandidateArtifactSha256);
    report.integrity.candidate_after =
        report.integrity.candidate_before;
    report.integrity.hidden.attempted =
        ac1::kPhysicalPriorityRoots;
    report.integrity.hidden.changed =
        ac1::kPhysicalPriorityRoots;
    report.integrity.hidden.changed_roots_by_deck =
        {6, 4, 8, 4, 4};
    report.integrity.hidden.nonvacuous_all_decks = true;
    report.integrity.hidden.scores_bit_identical = true;
    report.integrity.hidden
        .consequence_hashes_bit_identical = true;
    report.integrity.hidden.passed = true;
    report.integrity.artifact_requirements_match = true;
    report.integrity.model_identities_match = true;
    report.integrity.artifacts_unchanged = true;
    report.integrity.independent_manifest_bit_identical =
        true;
    report.integrity.repeated_construction_bit_identical =
        true;
    report.integrity.passed = true;
    report.gate =
        ac1::evaluate_gate(
            report.scientific, report.integrity);
    return report;
}

void test_scientific_bit_comparison_uses_ieee_bits() {
    ac1::RunReport first = fake_complete_report();
    ac1::ScientificEvidence second = first.scientific;
    expect(
        ac1::testing::scientific_evidence_bit_identical(
            first.scientific, second),
        "identical scientific evidence did not compare equal");
    double& sample =
        second.roots.front()
            .c16.score.decision.actions.front()
            .raw_samples.front();
    expect(same_bits(sample, -0.0),
           "fixture did not retain negative zero");
    sample = 0.0;
    expect(
        !ac1::testing::scientific_evidence_bit_identical(
            first.scientific, second),
        "scientific comparison collapsed signed zero");
}

void test_evidence_bundle_is_canonical_and_complete() {
    ac1::RunReport report = fake_complete_report();
    const ac1::EvidenceBundle first =
        ac1::testing::serialize_evidence_bundle(report);
    const ac1::EvidenceBundle repeated =
        ac1::testing::serialize_evidence_bundle(report);
    expect(first == repeated,
           "evidence serialization is nondeterministic");
    expect(
        first.section_names ==
                std::vector<std::string>{
                    "metadata", "manifest", "scores",
                    "contrasts", "integrity"} &&
            first.section_sha256.size() ==
                first.section_names.size(),
        "evidence section census drifted");
    expect(
        first.payload_sha256.size() == 64 &&
            first.bytes.find(
                "payload_sha256\t" +
                first.payload_sha256) !=
                std::string::npos,
        "evidence payload hash is missing");
    expect(
        first.bytes.find("8000000000000000") !=
            std::string::npos,
        "raw doubles were not serialized by exact bits");

    std::size_t action_count = 0;
    for (const auto& root : report.scientific.roots) {
        action_count +=
            root.c16.score.decision.actions.size();
        action_count +=
            root.oc1.score.decision.actions.size();
    }
    expect(
        count_occurrences(first.bytes, "\nsample\t") ==
            action_count * ac1::kSamplesPerAction,
        "evidence did not retain every raw64 row");
    expect(
        count_occurrences(first.bytes, "\naction\t") ==
            action_count,
        "evidence action census is wrong");
    expect(
        count_occurrences(
            first.bytes, "\nlogical_alias_reuse\t") == 1,
        "logical alias was not projected exactly once");

    ac1::RunReport other_tree = report;
    other_tree.integrity.parent_before.path =
        "/tree-b/parent.bin";
    other_tree.integrity.parent_after.path =
        "/tree-b/parent.bin";
    other_tree.integrity.candidate_before.path =
        "/tree-b/candidate.bin";
    other_tree.integrity.candidate_after.path =
        "/tree-b/candidate.bin";
    expect(
        first ==
            ac1::testing::serialize_evidence_bundle(
                other_tree),
        "absolute worktree paths contaminated evidence");

    report.scientific.roots.front()
        .oc1.score.decision.actions.front()
        .raw_samples.front() += 0.125;
    const auto changed =
        ac1::testing::serialize_evidence_bundle(report);
    expect(
        first.payload_sha256 != changed.payload_sha256,
        "raw sample change did not alter evidence hash");

    ac1::RunReport incomplete = fake_complete_report();
    incomplete.scientific.complete = false;
    incomplete.gate =
        ac1::evaluate_gate(
            incomplete.scientific,
            incomplete.integrity);
    expect_throws<std::runtime_error>(
        [&] {
            static_cast<void>(
                ac1::testing::serialize_evidence_bundle(
                    incomplete));
        },
        "exit-2 evidence was serializable");

    ac1::RunReport inconsistent =
        fake_complete_report();
    inconsistent.gate.passed = false;
    expect_throws<std::runtime_error>(
        [&] {
            static_cast<void>(
                ac1::testing::serialize_evidence_bundle(
                    inconsistent));
        },
        "inconsistent synthetic verdict was serialized");
}

void test_atomic_evidence_writer_is_no_replace() {
    TemporaryDirectory temporary;
    const auto target =
        temporary.path() / "evidence.tsv";
    ac1::testing::write_evidence_atomic_no_replace(
        target.string(), "sealed evidence\n");
    expect(
        read_file(target) == "sealed evidence\n",
        "atomic evidence writer changed bytes");
    expect(
        !std::filesystem::exists(
            target.string() + ".tmp"),
        "atomic evidence writer left its temporary");

    expect_throws<std::runtime_error>(
        [&] {
            ac1::testing::write_evidence_atomic_no_replace(
                target.string(), "replacement\n");
        },
        "atomic evidence writer replaced its target");
    expect(
        read_file(target) == "sealed evidence\n",
        "no-replace failure mutated the target");
    std::ostringstream rejected_summary;
    expect_throws<std::runtime_error>(
        [&] {
            ac1::testing::
                publish_evidence_and_emit_noexcept(
                    target.string(), "replacement\n",
                    "must-not-emit\n", rejected_summary);
        },
        "publication helper accepted an existing target");
    expect(
        rejected_summary.str().empty(),
        "summary leaked before failed publication");

    const auto occupied_temp =
        temporary.path() / "occupied.tsv";
    {
        std::ofstream output(
            occupied_temp.string() + ".tmp",
            std::ios::binary);
        output << "sentinel";
    }
    expect_throws<std::runtime_error>(
        [&] {
            ac1::testing::write_evidence_atomic_no_replace(
                occupied_temp.string(), "new\n");
        },
        "existing evidence temporary was accepted");
    expect(
        !std::filesystem::exists(occupied_temp) &&
            read_file(occupied_temp.string() + ".tmp") ==
                "sentinel",
        "temporary rejection mutated filesystem state");

    const auto symlink_target =
        temporary.path() / "symlink-target";
    {
        std::ofstream output(
            symlink_target, std::ios::binary);
        output << "private";
    }
    const auto symlink =
        temporary.path() / "evidence-link.tsv";
    std::filesystem::create_symlink(
        symlink_target, symlink);
    expect_throws<std::runtime_error>(
        [&] {
            ac1::testing::write_evidence_atomic_no_replace(
                symlink.string(), "replacement\n");
        },
        "evidence writer followed a target symlink");
    expect(
        read_file(symlink_target) == "private",
        "symlink rejection mutated its referent");

    const auto committed =
        temporary.path() / "committed.tsv";
    ThrowingStreamBuffer throwing_buffer;
    std::ostream throwing_output(&throwing_buffer);
    throwing_output.exceptions(
        std::ios::badbit | std::ios::failbit);
    ac1::testing::publish_evidence_and_emit_noexcept(
        committed.string(), "committed evidence\n",
        "post-commit summary\n", throwing_output);
    expect(
        read_file(committed) == "committed evidence\n",
        "post-commit output failure undid publication");
}

} // namespace

int main() {
    const std::vector<
        std::pair<std::string, std::function<void()>>>
        tests = {
            {"exact frozen manifest",
             test_manifest_is_exact_and_frozen},
            {"manifest contract",
             test_manifest_contract_covers_order_and_typed_actions},
            {"scorer order and hidden invariance",
             test_scorer_exercises_descriptor_order_and_hidden_invariance},
            {"paired contrast",
             test_paired_contrast_exact_statistics_and_fail_closed},
            {"bit-exact support",
             test_support_uses_ieee_bit_exact_ties},
            {"public consequence hash",
             test_public_consequence_hash_redacts_only_opponent_hidden},
            {"typed X=0 support",
             test_support_x_zero_control_is_typed},
            {"gate exit semantics",
             test_gate_exit_semantics},
            {"bitwise scientific comparison",
             test_scientific_bit_comparison_uses_ieee_bits},
            {"canonical evidence bundle",
             test_evidence_bundle_is_canonical_and_complete},
            {"atomic no-replace evidence",
             test_atomic_evidence_writer_is_no_replace},
        };

    std::size_t passed = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& failure) {
            std::cerr << "[FAIL] " << name << ": "
                      << failure.what() << '\n';
        }
    }
    std::cout << passed << "/" << tests.size()
              << " tests passed\n";
    return passed == tests.size() ? 0 : 1;
}
