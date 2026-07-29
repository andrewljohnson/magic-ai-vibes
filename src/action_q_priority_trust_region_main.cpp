#include "old_school/action_q_priority_trust_region.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/fq4_dev_candidate_artifact.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace trust =
    old_school::action_q_priority_trust_region;
namespace op1 =
    old_school::action_q_on_policy_successor;
namespace g4b =
    old_school::action_q_nested_actor_broad_distill;
namespace g1 =
    old_school::action_q_nested_actor_distill;
namespace diagnostic =
    old_school::action_q_nested_actor_diagnostic;
namespace artifact =
    old_school::fq4_dev_candidate_artifact;
namespace integrity = old_school::artifact_integrity;

constexpr std::string_view kC16ArtifactPath =
    "build/model-cache/"
    "old-school-value-challenger-v3-c16-t800-s424242.bin";
constexpr std::uintmax_t kC16ArtifactBytes = 3'111'437;
constexpr std::string_view kC16ArtifactSha256 =
    "53aeb904bd87311b37201859317f05ab0"
    "66bdfe134c72460cf94bff6d1f944ca";
constexpr std::string_view kCandidateArtifactPath =
    "build/model-cache/"
    "old-school-aq4-op2-manual-v1.fq4candidate";
constexpr std::string_view kCompositeCorpusSchema =
    "old-school-aq4-op2-canonical-runtime-corpus-envelope-v1";
constexpr std::string_view kFitInputSchema =
    "old-school-aq4-op2-canonical-selected-fit-input-v1";
constexpr std::string_view kFamily = "AQ4-OP2-MANUAL-V1";
constexpr std::string_view kEnvironment =
    "old-school-environment-v3-cleanup-discard";
constexpr std::size_t kTrainingGames = 800;
constexpr std::uint64_t kTrainingSeed = 424242;
constexpr std::size_t kParentGenerations = 16;
constexpr std::size_t kWorldsPerAction = 8;
constexpr std::size_t kHorizonTurns = 4;
constexpr std::size_t kRolloutsPerWorld = 1;
constexpr std::size_t kRootSearchDepth = 1;
constexpr std::size_t kMaximumTurns = 500;
constexpr double kResidualWeight = 0.10;
constexpr std::uint64_t kSequentialOptimizerCalls = 2;

using ArtifactSnapshot = integrity::RegularFileSnapshot;

class CanonicalBytes {
  public:
    void u64(std::uint64_t value) {
        for (std::size_t byte = 0; byte < 8; ++byte) {
            bytes_.push_back(static_cast<char>(
                (value >> (8U * byte)) & 0xffU));
        }
    }

    void size(std::size_t value) {
        if (value >
            std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error(
                "AQ4-OP2 canonical size exceeds uint64");
        }
        u64(static_cast<std::uint64_t>(value));
    }

    void real(double value) {
        u64(std::bit_cast<std::uint64_t>(value));
    }

    void text(std::string_view value) {
        size(value.size());
        bytes_.append(value);
    }

    const std::string& bytes() const {
        return bytes_;
    }

  private:
    std::string bytes_;
};

void append_optimizer(
    CanonicalBytes& output,
    const old_school::LearnedValuePriorityHeadUpdateConfig&
        optimizer) {
    output.size(optimizer.batch_size);
    output.size(optimizer.epochs);
    output.real(optimizer.learning_rate);
    output.real(optimizer.beta1);
    output.real(optimizer.beta2);
    output.real(optimizer.epsilon);
    output.real(optimizer.global_gradient_norm_clip);
    output.u64(optimizer.seed);
    output.real(optimizer.residual_weight);
    output.real(optimizer.policy_temperature);
}

std::string optimizer_identity(
    const old_school::LearnedValuePriorityHeadUpdateConfig&
        optimizer) {
    CanonicalBytes output;
    output.text("old-school-priority-optimizer-identity-v1");
    append_optimizer(output, optimizer);
    return output.bytes();
}

std::size_t checked_add(
    std::size_t left, std::size_t right,
    std::string_view description) {
    if (left >
        std::numeric_limits<std::size_t>::max() - right) {
        throw std::overflow_error(
            "AQ4-OP2 " + std::string(description) +
            " count overflow");
    }
    return left + right;
}

std::size_t option_count(
    std::span<const g4b::RootExample> examples) {
    std::size_t result = 0;
    for (const auto& example : examples) {
        result = checked_add(
            result, example.manifest.actions.size(),
            "option");
    }
    return result;
}

void append_corpus_provenance(
    CanonicalBytes& output, std::string_view name,
    const g4b::Corpus& corpus) {
    output.text(name);
    output.text(corpus.digest);
    output.u64(corpus.census.root_seed);
    output.text(corpus.census.parent_fingerprint);
    output.text(corpus.census.manifest_hash);
    output.size(corpus.census.games());
    output.size(corpus.train.size());
    output.size(option_count(corpus.train));
    output.size(corpus.dev.size());
    output.size(option_count(corpus.dev));
    for (const auto& split : corpus.census.splits) {
        output.u64(
            static_cast<std::uint64_t>(split.split));
        output.size(split.games);
        output.size(split.actor_games.size());
        output.size(split.retained_roots());
        output.size(split.retained_options());
        for (const auto& deck : split.decks) {
            output.size(deck.actor_games);
            output.size(deck.nontrivial_roots);
            output.size(deck.retained_roots);
            output.size(deck.retained_options);
        }
    }
}

ArtifactSnapshot c16_artifact_snapshot() {
    const auto snapshot =
        integrity::snapshot_regular_file(
            std::string(kC16ArtifactPath));
    if (snapshot.byte_size != kC16ArtifactBytes ||
        snapshot.sha256 != kC16ArtifactSha256) {
        throw std::runtime_error(
            "AQ4-OP2 C16 artifact identity drifted");
    }
    return snapshot;
}

void require_c16_artifact_unchanged(
    const ArtifactSnapshot& expected) {
    if (c16_artifact_snapshot() != expected) {
        throw std::runtime_error(
            "AQ4-OP2 C16 artifact changed during the command");
    }
}

bool path_absent(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::file_status status =
        std::filesystem::symlink_status(path, error);
    if (error ==
        std::make_error_code(
            std::errc::no_such_file_or_directory)) {
        return true;
    }
    if (error) {
        throw std::system_error(
            error,
            "AQ4-OP2 publication-coordinate inspection failed");
    }
    return !std::filesystem::exists(status);
}

void require_new_publication_coordinate() {
    const std::filesystem::path destination(
        kCandidateArtifactPath);
    const std::filesystem::path temporary =
        artifact::temporary_path_for(destination);
    if (!path_absent(destination) ||
        !path_absent(temporary)) {
        throw std::runtime_error(
            "AQ4-OP2 publication coordinate is not new");
    }
}

std::shared_ptr<const old_school::LearnedModel>
load_c16(const ArtifactSnapshot& before) {
    const auto loaded =
        old_school::load_learned_value_challenger_artifact(
            std::string(kC16ArtifactPath),
            kTrainingGames, kTrainingSeed,
            kParentGenerations);
    const auto model = loaded.model();
    require_c16_artifact_unchanged(before);
    if (old_school::learned_model_fingerprint(model) !=
        g4b::kRequiredParentFingerprint) {
        throw std::runtime_error(
            "AQ4-OP2 loaded C16 fingerprint drifted");
    }
    return model;
}

struct WarmReconstruction {
    std::shared_ptr<const old_school::LearnedModel> model;
    diagnostic::PreflightReport frozen_preflight;
    g4b::Corpus corpus;
    g4b::FitReport fit;
};

WarmReconstruction reconstruct_warm_parent(
    const std::shared_ptr<const old_school::LearnedModel>& c16,
    const ArtifactSnapshot& artifact_snapshot) {
    diagnostic::PreflightReport preflight =
        diagnostic::run_preflight(
            c16, g4b::preflight_recipe());
    require_c16_artifact_unchanged(artifact_snapshot);
    if (!g4b::preflight_exact(preflight)) {
        throw std::runtime_error(
            "AQ4-OP2 frozen G4B preflight replay drifted");
    }

    const g4b::Census census =
        g4b::collect_census(c16);
    g4b::require_frozen_census(census);
    require_c16_artifact_unchanged(artifact_snapshot);
    g4b::Corpus corpus =
        g4b::collect_corpus(c16, census, preflight);
    require_c16_artifact_unchanged(artifact_snapshot);
    g4b::FitReport fit = g4b::fit(corpus, c16);
    require_c16_artifact_unchanged(artifact_snapshot);
    if (!fit.candidate ||
        old_school::learned_model_fingerprint(
            fit.candidate) !=
            op1::kRequiredWarmParentFingerprint ||
        !fit.parent_immutable ||
        !fit.repeated_fit_bit_identical ||
        !fit.only_priority_component_changed) {
        throw std::runtime_error(
            "AQ4-OP2 G4B warm-parent reconstruction drifted");
    }
    return {
        .model = fit.candidate,
        .frozen_preflight = std::move(preflight),
        .corpus = std::move(corpus),
        .fit = std::move(fit),
    };
}

struct FullChildReconstruction {
    op1::Corpus corpus;
    op1::FitReport fit;
};

FullChildReconstruction reconstruct_full_child(
    const WarmReconstruction& warm,
    const ArtifactSnapshot& artifact_snapshot) {
    const op1::Census census =
        op1::collect_census(warm.model);
    op1::require_frozen_census(census);
    require_c16_artifact_unchanged(artifact_snapshot);
    op1::Corpus corpus =
        op1::collect_corpus(
            warm.model, census,
            warm.frozen_preflight);
    require_c16_artifact_unchanged(artifact_snapshot);
    if (corpus.digest != trust::kRequiredCorpusDigest) {
        throw std::runtime_error(
            "AQ4-OP2 OP1 corpus digest drifted");
    }
    op1::FitReport fit = op1::fit(corpus, warm.model);
    require_c16_artifact_unchanged(artifact_snapshot);
    if (!fit.candidate ||
        old_school::learned_model_fingerprint(
            fit.candidate) !=
            trust::kRequiredFullChildFingerprint ||
        !fit.parent_immutable ||
        !fit.repeated_fit_bit_identical ||
        !fit.only_priority_component_changed) {
        throw std::runtime_error(
            "AQ4-OP2 full OP1 child reconstruction drifted");
    }
    return {
        .corpus = std::move(corpus),
        .fit = std::move(fit),
    };
}

void print_metrics(
    double alpha, std::string_view policy,
    std::string_view split,
    const op1::Metrics& metrics) {
    std::cout
        << std::setprecision(17)
        << "metrics alpha=" << alpha
        << " policy=" << policy
        << " split=" << split
        << " roots=" << metrics.roots
        << " options=" << metrics.options
        << " agreement="
        << metrics.equal_deck_top_one_expected_agreement
        << " regret="
        << metrics.equal_deck_mean_regret << '\n';
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        const auto& row = metrics.decks[deck];
        std::cout
            << "metrics_deck alpha=" << alpha
            << " policy=" << policy
            << " split=" << split
            << " deck="
            << old_school::deck_name(
                   static_cast<old_school::DeckId>(deck))
            << " roots=" << row.roots
            << " options=" << row.options
            << " weight_mass=" << row.weight_mass
            << " agreement="
            << row.top_one_expected_agreement
            << " regret=" << row.mean_regret
            << '\n';
    }
}

void print_safety(
    double alpha,
    const old_school::action_q_offline_gate::
        ModelGateReport& report) {
    const auto& frozen = report.frozen_dev;
    const auto& ancestral = report.ancestral;
    const auto& order = report.descriptor_order;
    const auto& behavior = report.behavior;
    const auto& force = behavior.force_spike;
    std::cout
        << "safety alpha=" << alpha
        << " result=" << report.gate_passed()
        << " frozen_dev=" << frozen.gate_passed()
        << " ancestral=" << ancestral.gate_passed()
        << " descriptor_order=" << order.gate_passed()
        << " behavior=" << behavior.gate_passed()
        << '\n'
        << "safety_frozen_dev alpha=" << alpha
        << " labels=" << frozen.labels
        << " stable_parent_agreements="
        << frozen.stable_parent_agreements
        << " lost_stable_parent_agreements="
        << frozen.lost_stable_parent_agreements
        << " pooled_regret_no_worse="
        << frozen.pooled_regret_no_worse
        << " pair_hidden="
        << frozen.pair_hidden_repartition.passed
        << " explicit_hidden="
        << frozen.explicit_hidden_repartition.passed
        << " cache_unchanged="
        << (frozen.cache_before == frozen.cache_after)
        << '\n'
        << "safety_ancestral alpha=" << alpha
        << " legal_exact="
        << ancestral.complete_legal_actions_exact
        << " fingerprint_exact="
        << ancestral.information_action_fingerprint_exact
        << " hidden_identity="
        << ancestral.hidden_repartition_bit_identical
        << " self_above_opponent="
        << ancestral.self_strictly_above_opponent
        << " opponent_absent="
        << ancestral.opponent_absent_from_support
        << " self_score=" << ancestral.self_score
        << " opponent_score=" << ancestral.opponent_score
        << '\n'
        << "safety_descriptor alpha=" << alpha
        << " model_count=" << order.model_count
        << " probe_count=" << order.probe_count
        << " scores_identity="
        << order.action_keyed_scores_bit_identical
        << " support_identity="
        << order.selected_supports_identical
        << " hidden_owner_equivalent="
        << order.hidden_repartitions_distinct_owner_equivalent
        << " hidden_scores_identity="
        << order.hidden_action_keyed_scores_bit_identical
        << " hidden_support_identity="
        << order.hidden_selected_supports_identical
        << '\n'
        << "safety_behavior alpha=" << alpha
        << " force_hidden="
        << force.hidden_repartition_passed
        << " force_live="
        << behavior.live_force_spike_preserved
        << " force_one_open_payable_pass="
        << behavior.one_open_payable_selects_pass
        << " force_five_open_pass="
        << behavior.five_open_force_spike_selects_pass
        << " redundant_counter_pass="
        << behavior.redundant_counter_selects_pass
        << " intervening_counter_correct="
        << behavior
               .intervening_counter_selects_opposing_counter
        << " sick_growth_pass="
        << behavior.sick_bear_growth_selects_pass
        << " opponent_growth_excluded="
        << behavior.opponent_growth_excluded
        << " braingeyser_x0_excluded="
        << behavior.braingeyser_x_zero_excluded
        << " live_pass_score=" << force.live.pass_score
        << " live_force_score="
        << force.live.force_spike_score
        << " payable_pass_score="
        << force.payable.pass_score
        << " payable_force_score="
        << force.payable.force_spike_score
        << '\n';
    for (const std::string& failure : report.failures()) {
        std::cout
            << "safety_failure alpha=" << alpha
            << " message=" << std::quoted(failure) << '\n';
    }
}

void print_arm(
    std::string_view role,
    const trust::ArmEvaluation& arm) {
    std::cout
        << std::setprecision(17)
        << "arm role=" << role
        << " alpha=" << arm.alpha
        << " fingerprint=" << arm.fingerprint
        << " repeated_fingerprint="
        << arm.repeated_fingerprint
        << " repeated="
        << arm.repeated_construction_bit_identical
        << " priority_only="
        << arm.only_priority_component_changed
        << " train_regret="
        << arm.train_regret_strictly_improved
        << " dev_regret="
        << arm.dev_regret_strictly_improved
        << " result=" << arm.gate_passed()
        << '\n';
    print_metrics(
        arm.alpha, role, "TRAIN",
        arm.candidate_train);
    print_metrics(
        arm.alpha, role, "DEV",
        arm.candidate_dev);
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        std::cout
            << "dev_guard alpha=" << arm.alpha
            << " deck="
            << old_school::deck_name(
                   static_cast<old_school::DeckId>(deck))
            << " regret_delta="
            << arm.candidate_dev.decks[deck].mean_regret -
                   arm.parent_dev.decks[deck].mean_regret
            << " guard="
            << arm.dev_deck_regret_guard[deck]
            << '\n';
    }
    print_safety(arm.alpha, arm.model_gates);
    for (const std::string& failure : arm.failures) {
        std::cout
            << "arm_failure alpha=" << arm.alpha
            << " message=" << std::quoted(failure) << '\n';
    }
}

void print_full_control(
    const trust::FullControlReport& control) {
    std::cout
        << "full_control alpha=1"
        << " endpoint_pointer="
        << control.endpoint_pointer_exact
        << " corpus_digest="
        << control.corpus_digest_exact
        << " parent_train="
        << control.parent_train_metrics_exact
        << " candidate_train="
        << control.candidate_train_metrics_exact
        << " parent_dev="
        << control.parent_dev_metrics_exact
        << " candidate_dev="
        << control.candidate_dev_metrics_exact
        << " safety_signature="
        << control.expected_safety_signature_exact
        << " result=" << control.control_exact()
        << '\n';
    for (const std::string& failure : control.failures) {
        std::cout
            << "full_control_failure message="
            << std::quoted(failure) << '\n';
    }
}

struct PriorityLayout {
    std::uint32_t hidden = 0;
    std::uint32_t features = 0;
    std::uint64_t parameters = 0;
};

PriorityLayout priority_layout(
    const std::shared_ptr<const old_school::LearnedModel>&
        model) {
    const auto parameters =
        old_school::learned_priority_head_parameters(model);
    if (parameters.input_hidden.empty() ||
        parameters.direct.empty() ||
        parameters.input_hidden.size() >
            std::numeric_limits<std::uint32_t>::max() ||
        parameters.direct.size() >
            std::numeric_limits<std::uint32_t>::max() ||
        parameters.hidden_bias.size() !=
            parameters.input_hidden.size() ||
        parameters.hidden_output.size() !=
            parameters.input_hidden.size()) {
        throw std::runtime_error(
            "AQ4-OP2 Priority tensor layout is invalid");
    }
    for (const auto& row : parameters.input_hidden) {
        if (row.size() != parameters.direct.size()) {
            throw std::runtime_error(
                "AQ4-OP2 Priority tensor layout is ragged");
        }
    }
    const std::uint64_t hidden =
        parameters.input_hidden.size();
    const std::uint64_t features =
        parameters.direct.size();
    if (hidden >
        (std::numeric_limits<std::uint64_t>::max() -
         features - 1U) /
            (features + 2U)) {
        throw std::overflow_error(
            "AQ4-OP2 Priority parameter count overflow");
    }
    return {
        .hidden = static_cast<std::uint32_t>(hidden),
        .features =
            static_cast<std::uint32_t>(features),
        .parameters =
            hidden * features + hidden + hidden +
            features + 1U,
    };
}

struct RuntimeContract {
    artifact::Contract contract;
    std::string corpus_envelope_sha256;
    std::size_t corpus_envelope_bytes = 0;
    std::string fit_input_sha256;
    std::string g4b_corpus_digest;
    std::string op1_corpus_digest;
    std::string warm_endpoint;
    std::string full_child_endpoint;
    std::size_t g4b_train_roots = 0;
    std::size_t g4b_train_options = 0;
    std::size_t g4b_dev_roots = 0;
    std::size_t g4b_dev_options = 0;
    std::size_t op1_train_roots = 0;
    std::size_t op1_train_options = 0;
    std::size_t op1_dev_roots = 0;
    std::size_t op1_dev_options = 0;
};

RuntimeContract make_runtime_contract(
    const ArtifactSnapshot& c16_snapshot,
    const std::shared_ptr<const old_school::LearnedModel>& c16,
    const WarmReconstruction& warm,
    const FullChildReconstruction& full,
    const trust::ArmEvaluation& selected) {
    if (!selected.model || !selected.gate_passed() ||
        optimizer_identity(warm.fit.optimizer) !=
            optimizer_identity(full.fit.optimizer)) {
        throw std::runtime_error(
            "AQ4-OP2 publication requires two identical "
            "sequential optimizer recipes");
    }

    CanonicalBytes corpus_envelope;
    corpus_envelope.text(kCompositeCorpusSchema);
    // This is an exact canonical provenance envelope over both frozen
    // corpora. It is deliberately not represented as a serialized root
    // corpus; stdout reports that distinction explicitly.
    append_corpus_provenance(
        corpus_envelope, "G4B", warm.corpus);
    append_corpus_provenance(
        corpus_envelope, "OP1", full.corpus);
    const std::string corpus_sha =
        integrity::sha256_string(
            corpus_envelope.bytes());

    CanonicalBytes fit_input;
    fit_input.text(kFitInputSchema);
    fit_input.text(corpus_sha);
    fit_input.size(corpus_envelope.bytes().size());
    fit_input.text(
        old_school::learned_model_fingerprint(c16));
    fit_input.text(
        old_school::learned_model_fingerprint(
            warm.model));
    fit_input.text(
        old_school::learned_model_fingerprint(
            full.fit.candidate));
    fit_input.text(selected.fingerprint);
    fit_input.real(selected.alpha);
    fit_input.text(
        "priority-tensor-std-lerp-warm-to-full-child");
    fit_input.u64(kSequentialOptimizerCalls);
    append_optimizer(fit_input, warm.fit.optimizer);
    const std::string fit_sha =
        integrity::sha256_string(fit_input.bytes());

    const std::size_t g4b_train_options =
        option_count(warm.corpus.train);
    const std::size_t g4b_dev_options =
        option_count(warm.corpus.dev);
    const std::size_t op1_train_options =
        option_count(full.corpus.train);
    const std::size_t op1_dev_options =
        option_count(full.corpus.dev);
    if (g4b_train_options != warm.fit.fit_options ||
        op1_train_options != full.fit.fit_options) {
        throw std::runtime_error(
            "AQ4-OP2 fit option accounting drifted");
    }
    const std::size_t fit_examples =
        checked_add(
            warm.fit.fit_examples,
            full.fit.fit_examples,
            "sequential fit example");
    const std::size_t fit_options =
        checked_add(
            warm.fit.fit_options,
            full.fit.fit_options,
            "sequential fit option");
    const std::size_t check_examples =
        checked_add(
            warm.corpus.dev.size(),
            full.corpus.dev.size(),
            "sequential DEV example");
    const PriorityLayout layout =
        priority_layout(selected.model);
    const auto parent_components =
        old_school::learned_model_component_fingerprints(
            c16);

    artifact::Contract contract{
        .family = std::string(kFamily),
        .environment = std::string(kEnvironment),
        .parent = {
            .artifact_bytes = c16_snapshot.byte_size,
            .artifact_sha256 = c16_snapshot.sha256,
            .model_fingerprint =
                old_school::learned_model_fingerprint(c16),
            .components = parent_components,
            .training_games = kTrainingGames,
            .training_seed = kTrainingSeed,
            .generation = kParentGenerations,
        },
        .corpus = {
            .artifact_bytes =
                corpus_envelope.bytes().size(),
            .artifact_sha256 = corpus_sha,
        },
        .fit = {
            .input_sha256 = fit_sha,
            .examples = fit_examples,
            .options = fit_options,
            .check_examples = check_examples,
            .background_only_examples = 0,
            .optimizer_calls =
                kSequentialOptimizerCalls,
            .optimizer = warm.fit.optimizer,
        },
        .candidate_model_fingerprint =
            selected.fingerprint,
        .priority_hidden_count = layout.hidden,
        .priority_feature_count = layout.features,
        .priority_parameter_count =
            layout.parameters,
        .deployment = {
            .variant =
                old_school::LearnedVariant::
                    ValueSearchChampion,
            .training_games = kTrainingGames,
            .worlds_per_action = kWorldsPerAction,
            .horizon_turns = kHorizonTurns,
            .rollouts_per_world = kRolloutsPerWorld,
            .root_search_depth = kRootSearchDepth,
            .shallow_prior = true,
            .root_exploration = 0.0,
            .continuation_epsilon = 0.0,
            .priority_residual_weight =
                kResidualWeight,
            .pass_dominance = false,
            .continuation_controller =
                old_school::
                    LearnedContinuationController::Legacy,
            .max_turns = kMaximumTurns,
        },
    };
    return {
        .contract = std::move(contract),
        .corpus_envelope_sha256 = corpus_sha,
        .corpus_envelope_bytes =
            corpus_envelope.bytes().size(),
        .fit_input_sha256 = fit_sha,
        .g4b_corpus_digest = warm.corpus.digest,
        .op1_corpus_digest = full.corpus.digest,
        .warm_endpoint =
            old_school::learned_model_fingerprint(
                warm.model),
        .full_child_endpoint =
            old_school::learned_model_fingerprint(
                full.fit.candidate),
        .g4b_train_roots = warm.corpus.train.size(),
        .g4b_train_options = g4b_train_options,
        .g4b_dev_roots = warm.corpus.dev.size(),
        .g4b_dev_options = g4b_dev_options,
        .op1_train_roots = full.corpus.train.size(),
        .op1_train_options = op1_train_options,
        .op1_dev_roots = full.corpus.dev.size(),
        .op1_dev_options = op1_dev_options,
    };
}

artifact::Report publish_candidate(
    const RuntimeContract& runtime,
    const std::shared_ptr<const old_school::LearnedModel>& c16,
    const trust::ArmEvaluation& selected,
    const ArtifactSnapshot& c16_snapshot) {
    require_c16_artifact_unchanged(c16_snapshot);
    require_new_publication_coordinate();
    std::cout
        << std::setprecision(17)
        << "artifact_contract"
        << " corpus_provenance_kind="
           "canonical_runtime_envelope_not_serialized_root_corpus"
        << " corpus_envelope_bytes="
        << runtime.corpus_envelope_bytes
        << " corpus_envelope_sha256="
        << runtime.corpus_envelope_sha256
        << " fit_input_sha256="
        << runtime.fit_input_sha256
        << " sequential_optimizer_calls="
        << runtime.contract.fit.optimizer_calls
        << " alpha=" << selected.alpha
        << " c16_parent="
        << runtime.contract.parent.model_fingerprint
        << " warm_endpoint="
        << runtime.warm_endpoint
        << " full_child_endpoint="
        << runtime.full_child_endpoint
        << " selected_endpoint="
        << selected.fingerprint
        << '\n';
    // Print every constituent hidden behind the Contract's two canonical
    // hashes so a future load-only consumer can pin the runtime contract
    // without pretending the envelope is a persisted root-corpus file.
    std::cout
        << "artifact_corpus name=G4B"
        << " digest=" << runtime.g4b_corpus_digest
        << " train_roots=" << runtime.g4b_train_roots
        << " train_options=" << runtime.g4b_train_options
        << " dev_roots=" << runtime.g4b_dev_roots
        << " dev_options=" << runtime.g4b_dev_options
        << '\n'
        << "artifact_corpus name=OP1"
        << " digest=" << runtime.op1_corpus_digest
        << " train_roots=" << runtime.op1_train_roots
        << " train_options=" << runtime.op1_train_options
        << " dev_roots=" << runtime.op1_dev_roots
        << " dev_options=" << runtime.op1_dev_options
        << '\n';

    const std::filesystem::path path(
        kCandidateArtifactPath);
    const artifact::Report report =
        artifact::publish_atomic_no_replace(
            path, c16, selected.model,
            runtime.contract);
    const auto snapshot =
        integrity::snapshot_regular_file(path);
    if (snapshot.byte_size != report.artifact.bytes ||
        snapshot.sha256 != report.artifact.sha256 ||
        report.manifest.contract != runtime.contract) {
        throw std::runtime_error(
            "AQ4-OP2 published artifact identity drifted");
    }
    const artifact::LoadedCandidate reloaded =
        artifact::load(
            path, c16, runtime.contract,
            report.artifact);
    if (!reloaded.model() ||
        reloaded.report() != report ||
        old_school::learned_model_fingerprint(
            reloaded.model()) != selected.fingerprint ||
        old_school::learned_priority_head_parameters(
            reloaded.model()) !=
            old_school::learned_priority_head_parameters(
                selected.model) ||
        !path_absent(
            artifact::temporary_path_for(path))) {
        throw std::runtime_error(
            "AQ4-OP2 published artifact failed exact reload");
    }
    require_c16_artifact_unchanged(c16_snapshot);
    std::cout
        << "artifact result=PUBLISHED"
        << " path=" << path.string()
        << " bytes=" << report.artifact.bytes
        << " sha256=" << report.artifact.sha256
        << " candidate="
        << runtime.contract.candidate_model_fingerprint
        << " alpha=" << selected.alpha
        << " priority_parameters="
        << runtime.contract.priority_parameter_count
        << '\n';
    return report;
}

old_school::BotBenchmarkSummary run_selector(
    const std::shared_ptr<const old_school::LearnedModel>& c16,
    const trust::ArmEvaluation& selected) {
    if (!selected.model || !selected.gate_passed()) {
        throw std::invalid_argument(
            "AQ4-OP2 selector requires the first passing arm");
    }
    const auto summary =
        old_school::run_bot_benchmark(
            g1::kSelectorRepetitions,
            trust::kSelectorSeed,
            g1::selector_bot_config(
                selected.model, kResidualWeight),
            g1::selector_bot_config(c16, 0.0),
            old_school::GameConfig{
                .max_turns = kMaximumTurns,
            },
            false);
    g1::validate_selector_summary(
        summary, c16, selected.model,
        trust::kSelectorSeed);
    return summary;
}

int run_experiment() {
    require_new_publication_coordinate();
    const ArtifactSnapshot c16_snapshot =
        c16_artifact_snapshot();
    const auto c16 = load_c16(c16_snapshot);
    const WarmReconstruction warm =
        reconstruct_warm_parent(c16, c16_snapshot);
    const FullChildReconstruction full =
        reconstruct_full_child(warm, c16_snapshot);

    std::cout
        << "schema=old-school-action-q-aq4-op2-run-v1"
        << " mode=run"
        << " c16="
        << old_school::learned_model_fingerprint(c16)
        << " warm="
        << old_school::learned_model_fingerprint(warm.model)
        << " full_child="
        << old_school::learned_model_fingerprint(
               full.fit.candidate)
        << " g4b_corpus=" << warm.corpus.digest
        << " op1_corpus=" << full.corpus.digest
        << " selector_seed=" << trust::kSelectorSeed
        << '\n';

    const trust::FullControlReport control =
        trust::evaluate_full_control(
            full.corpus, warm.model,
            full.fit.candidate);
    print_metrics(
        0.0, "warm_control", "TRAIN",
        control.arm.parent_train);
    print_metrics(
        0.0, "warm_control", "DEV",
        control.arm.parent_dev);
    print_arm("full_child_control", control.arm);
    print_full_control(control);
    require_c16_artifact_unchanged(c16_snapshot);
    if (!control.control_exact()) {
        throw std::runtime_error(
            "AQ4-OP2 alpha-one full control did not reproduce");
    }

    std::vector<trust::ArmEvaluation> attempted;
    attempted.reserve(trust::kCandidateAlphas.size());
    for (const double alpha : trust::kCandidateAlphas) {
        attempted.push_back(
            trust::evaluate_arm(
                full.corpus, warm.model,
                full.fit.candidate, alpha));
        print_arm("candidate", attempted.back());
        require_c16_artifact_unchanged(c16_snapshot);
        if (attempted.back().gate_passed()) {
            break;
        }
    }

    const trust::SweepDisposition sweep =
        trust::classify_sweep(attempted);
    const std::optional<std::size_t> selected_index =
        trust::first_passing_arm_index(attempted);
    const std::optional<double> selected_alpha =
        trust::first_passing_alpha(attempted);
    if (selected_index.has_value() !=
            selected_alpha.has_value() ||
        (selected_index.has_value() &&
         (sweep != trust::SweepDisposition::Selected ||
          *selected_index + 1U != attempted.size() ||
          attempted[*selected_index].alpha !=
              *selected_alpha)) ||
        (!selected_index.has_value() &&
         sweep != trust::SweepDisposition::Reject)) {
        throw std::runtime_error(
            "AQ4-OP2 sweep classification drifted");
    }
    if (!selected_index.has_value()) {
        std::cout
            << "result=REJECT disposition=OFFLINE_REJECT"
            << " attempted_arms=" << attempted.size()
            << " selector_opened=0 artifact_published=0\n";
        return 1;
    }

    const trust::ArmEvaluation& selected =
        attempted[*selected_index];
    std::cout
        << std::setprecision(17)
        << "selection disposition=FIRST_OFFLINE_PASS"
        << " alpha=" << selected.alpha
        << " fingerprint=" << selected.fingerprint
        << " attempted_arms=" << attempted.size()
        << '\n';
    const auto selector = run_selector(c16, selected);
    require_c16_artifact_unchanged(c16_snapshot);
    const trust::SelectorDisposition disposition =
        trust::classify_selector(selector);
    g1::print_selector(
        std::cout, selector,
        disposition ==
                trust::SelectorDisposition::ManualPilot
            ? g1::SelectorDisposition::ManualOnly
            : g1::SelectorDisposition::Reject);
    if (disposition !=
        trust::SelectorDisposition::ManualPilot) {
        std::cout
            << "result=REJECT disposition=SELECTOR_REJECT"
            << " alpha=" << selected.alpha
            << " selector_opened=1 artifact_published=0\n";
        return 1;
    }

    const RuntimeContract runtime =
        make_runtime_contract(
            c16_snapshot, c16, warm, full, selected);
    static_cast<void>(
        publish_candidate(
            runtime, c16, selected, c16_snapshot));
    std::cout
        << "result=PASS disposition=MANUAL_PILOT"
        << " alpha=" << selected.alpha
        << " selector_opened=1 artifact_published=1\n";
    return 0;
}

void print_usage(std::ostream& output) {
    output
        << "Usage: old-school-action-q-priority-trust-region --run\n";
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2 || argv == nullptr ||
        argv[0] == nullptr || argv[1] == nullptr ||
        std::string_view(argv[1]) != "--run") {
        print_usage(std::cerr);
        return 2;
    }
    try {
        return run_experiment();
    } catch (const std::exception& error) {
        std::cerr
            << "result=ERROR"
            << " reason=action_q_priority_trust_region_failed"
            << " message=" << error.what() << '\n';
        return 1;
    }
}
