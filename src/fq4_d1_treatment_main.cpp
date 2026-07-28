#include "old_school/artifact_integrity.hpp"
#include "old_school/fq4_d1_treatment.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

namespace field = old_school::fq4_d1_field_gate;
namespace treatment = old_school::fq4_d1_treatment;
namespace integrity = old_school::artifact_integrity;

constexpr std::array<std::string_view, 4> kClassLabels{
    "Safe",
    "Class1",
    "Class2",
    "Class3",
};

std::string_view verdict_name(
    treatment::ExitClassification classification) {
    switch (classification) {
    case treatment::ExitClassification::Pass:
        return "PASS";
    case treatment::ExitClassification::ScientificReject:
        return "SCIENTIFIC_REJECT";
    case treatment::ExitClassification::InfrastructureFailure:
        return "INFRASTRUCTURE_FAILURE";
    }
    return "INFRASTRUCTURE_FAILURE";
}

void print_components(
    std::string_view label,
    const old_school::LearnedModelComponentFingerprints&
        components) {
    std::cout
        << "components model=" << label
        << " critic=" << components.critic
        << " priority=" << components.priority
        << " attack=" << components.attack
        << " block=" << components.block
        << " damage_order=" << components.damage_order
        << '\n';
}

void print_aggregate(
    std::string_view label,
    const treatment::AggregateResult& aggregate) {
    std::cout
        << "aggregate scope=" << label
        << " parent_safe=" << aggregate.parent.safe()
        << " parent_class1=" << aggregate.parent.class1()
        << " parent_class2=" << aggregate.parent.class2()
        << " parent_class3=" << aggregate.parent.class3()
        << " candidate_safe=" << aggregate.candidate.safe()
        << " candidate_class1=" << aggregate.candidate.class1()
        << " candidate_class2=" << aggregate.candidate.class2()
        << " candidate_class3=" << aggregate.candidate.class3()
        << " candidate_high_confidence="
        << aggregate.candidate.high_confidence()
        << " candidate_unsafe="
        << aggregate.candidate.unsafe()
        << '\n';
    for (std::size_t parent = 0;
         parent < kClassLabels.size(); ++parent) {
        for (std::size_t candidate = 0;
             candidate < kClassLabels.size(); ++candidate) {
            std::cout
                << "transition scope=" << label
                << " parent=" << kClassLabels[parent]
                << " candidate=" << kClassLabels[candidate]
                << " count="
                << aggregate.transitions[parent][candidate]
                << '\n';
        }
    }
}

void print_control(
    std::string_view checkpoint,
    const old_school::fq4_priority_fit::ControlReport&
        control) {
    std::cout
        << "d0b_control checkpoint=" << checkpoint
        << " name=" << std::quoted(control.name)
        << " stable_id=" << control.stable_id
        << " passed=" << (control.passed ? 1 : 0)
        << '\n';
}

int worker() {
    try {
        const std::string artifact_path(
            field::kParentArtifactPath);
        const integrity::RegularFileSnapshot before =
            integrity::snapshot_regular_file(artifact_path);
        if (before.sha256 !=
            treatment::kRequiredParentArtifactSha256) {
            throw std::runtime_error(
                "immutable C16 artifact SHA-256 drifted");
        }
        const auto parent =
            old_school::
                load_learned_value_challenger_artifact(
                    artifact_path,
                    field::kParentTrainingGames,
                    field::kParentTrainingSeed,
                    field::kParentGenerations)
                .model();
        treatment::TreatmentReport report =
            treatment::run_production(
                parent, before.sha256);
        const integrity::RegularFileSnapshot after =
            integrity::snapshot_regular_file(artifact_path);
        if (after != before) {
            report.infrastructure_failures.push_back(
                "immutable C16 artifact changed during D1");
        }

        const treatment::ExitClassification classification =
            treatment::classify_exit(report);
        if (classification ==
            treatment::ExitClassification::
                InfrastructureFailure) {
            if (report.infrastructure_failures.empty()) {
                std::cerr
                    << "FQ4-D1-T0 infrastructure failure: "
                       "report failed without a named reason\n";
            } else {
                for (const std::string& failure :
                     report.infrastructure_failures) {
                    std::cerr
                        << "FQ4-D1-T0 infrastructure failure: "
                        << failure << '\n';
                }
            }
            std::cerr.flush();
            return static_cast<int>(classification);
        }

        std::cout
            << std::setprecision(
                   std::numeric_limits<double>::
                       max_digits10)
            << "FQ4-D1-T0 held-out Priority treatment\n"
            << "parent_artifact_sha256="
            << report.parent_reconstruction.artifact_sha256
            << '\n'
            << "parent_fingerprint="
            << report.parent_fingerprint
            << " candidate_fingerprint="
            << report.candidate_fingerprint << '\n'
            << "treatment_input_sha256="
            << report.treatment_input_sha256
            << " evidence_sha256="
            << report.evidence_sha256 << '\n'
            << "schedule_sha256="
            << report.parent_reconstruction.schedule_sha256
            << " trajectory_sha256="
            << report.parent_reconstruction
                   .trajectory_sha256
            << '\n'
            << "retained_corpus_sha256="
            << report.parent_reconstruction
                   .retained_corpus_sha256
            << " dominance_corpus_sha256="
            << report.parent_reconstruction
                   .dominance_corpus_sha256
            << " scored_corpus_sha256="
            << report.parent_reconstruction
                   .scored_corpus_sha256
            << " audit_scores_sha256="
            << report.parent_reconstruction
                   .audit_scores_sha256
            << '\n';
        print_components(
            "parent", report.parent_components);
        print_components(
            "candidate", report.candidate_components);

        std::cout
            << "parent_census physical_games="
            << report.parent_reconstruction.physical_games
            << " owner_perspectives="
            << report.parent_reconstruction
                   .owner_perspectives
            << " retained_roots="
            << report.parent_reconstruction.retained_roots
            << " scored_roots="
            << report.parent_reconstruction.scored_roots
            << " class2_sigma_mass="
            << report.parent_reconstruction
                   .class2_sigma_mass
            << " high_confidence_games="
            << report.parent_reconstruction
                   .high_confidence_games
            << " high_confidence_decks="
            << report.parent_reconstruction
                   .high_confidence_decks
            << " exact="
            << (report.parent_reconstruction.exact ? 1 : 0)
            << '\n'
            << "parent_accounting calls="
            << report.parent_reconstruction
                   .accounting.score_calls
            << " actions="
            << report.parent_reconstruction
                   .accounting.scored_actions
            << " worlds="
            << report.parent_reconstruction
                   .accounting.sampled_worlds
            << " evaluations="
            << report.parent_reconstruction
                   .accounting.rollout_evaluations
            << " terminal="
            << report.parent_reconstruction
                   .accounting.terminal_evaluations
            << " bootstrap="
            << report.parent_reconstruction
                   .accounting.bootstrapped_evaluations
            << " dominance_transitions="
            << report.parent_reconstruction
                   .accounting.dominance_transitions
            << '\n'
            << "parent_control_accounting primary_calls="
            << report.parent_reconstruction
                   .primary_accounting.score_calls
            << " hidden_calls="
            << report.parent_reconstruction
                   .hidden_control_accounting.score_calls
            << " reverse_calls="
            << report.parent_reconstruction
                   .reverse_control_accounting.score_calls
            << " repeat_calls="
            << report.parent_reconstruction
                   .repeat_accounting.score_calls
            << " census="
            << (report.parent_reconstruction.census_passed ? 1 : 0)
            << " hidden_replay="
            << (report.parent_reconstruction.hidden_replay_exact ? 1 : 0)
            << " hidden_features="
            << (report.parent_reconstruction
                        .hidden_feature_bits_identical
                    ? 1
                    : 0)
            << " reverse_scores="
            << (report.parent_reconstruction
                        .reverse_score_bits_identical
                    ? 1
                    : 0)
            << " recipe_accounting="
            << (report.parent_reconstruction
                        .recipe_and_accounting_exact
                    ? 1
                    : 0)
            << " cross_sums="
            << (report.parent_reconstruction
                        .count_cross_sums_exact
                    ? 1
                    : 0)
            << " repeat="
            << (report.parent_reconstruction
                        .repeated_construction_bit_identical
                    ? 1
                    : 0)
            << '\n'
            << "d0b anchor_epochs="
            << report.d0b.anchor_epochs
            << " treatment_epochs="
            << report.d0b.treatment_epochs
            << " constraints="
            << report.d0b.discovered_constraints
            << " candidate_margins_at_gate="
            << report.d0b.candidate_margins_at_gate
            << " anchor_kl="
            << report.d0b.anchor_pooled_kl
            << " treatment_kl="
            << report.d0b.treatment_pooled_kl
            << " exact=" << (report.d0b.exact ? 1 : 0)
            << " training_input_sha256="
            << report.d0b.training_input_sha256
            << " anchor_fingerprint="
            << report.d0b.anchor_candidate_fingerprint
            << '\n';
        std::cout
            << "d0b_qualification report_passed="
            << (report.d0b.report_passed ? 1 : 0)
            << " parent_contract="
            << (report.d0b.parent_contract_qualified ? 1 : 0)
            << " anchor_contract="
            << (report.d0b.anchor_contract_qualified ? 1 : 0)
            << " optimizer_only_epochs="
            << (report.d0b.optimizer_only_epochs_differ ? 1 : 0)
            << " checkpoint_inputs="
            << (report.d0b.checkpoint_inputs_bit_identical ? 1 : 0)
            << " kl_improved="
            << (report.d0b.target_kl_strictly_improved ? 1 : 0)
            << " fingerprint="
            << (report.d0b.candidate_fingerprint_exact ? 1 : 0)
            << " priority_only="
            << (report.d0b.only_priority_component_changed ? 1 : 0)
            << " repeat="
            << (report.d0b.repeated_fit_bit_identical ? 1 : 0)
            << " hidden="
            << (report.d0b.hidden_repartition_bit_identical ? 1 : 0)
            << " order="
            << (report.d0b.action_order_bit_identical ? 1 : 0)
            << " immutable_base_accounting="
            << (report.d0b.immutable_base_and_accounting ? 1 : 0)
            << " controls="
            << (report.d0b.every_control_passed ? 1 : 0)
            << '\n';
        for (std::size_t index = 0;
             index < report.d0b.candidate_margins.size();
             ++index) {
            std::cout
                << "d0b_margin index=" << index
                << " parent="
                << report.d0b.parent_margins[index]
                << " candidate="
                << report.d0b.candidate_margins[index]
                << '\n';
        }
        for (const auto& control :
             report.d0b.anchor_controls) {
            print_control("anchor", control);
        }
        for (const auto& control :
             report.d0b.treatment_controls) {
            print_control("treatment", control);
        }

        for (std::size_t index = 0;
             index < report.roots.size(); ++index) {
            const auto& root = report.roots[index];
            std::cout
                << "root index=" << index
                << " stable_id=" << root.stable_id
                << " physical_game_id="
                << root.physical_game_id
                << " deck="
                << old_school::deck_name(root.owner_deck)
                << " parent_class="
                << field::parent_class_name(
                       root.parent_class.classification)
                << " candidate_class="
                << field::parent_class_name(
                       root.candidate_class.classification)
                << " transition="
                << field::parent_class_name(
                       root.parent_class.classification)
                << "->"
                << field::parent_class_name(
                       root.candidate_class.classification)
                << " parent_d="
                << std::quoted(
                       root.parent_dominated_descriptor)
                << " parent_n="
                << std::quoted(
                       root.parent_nondominated_descriptor)
                << " candidate_d="
                << std::quoted(
                       root.candidate_dominated_descriptor)
                << " candidate_n="
                << std::quoted(
                       root.candidate_nondominated_descriptor)
                << " parent_margin="
                << root.parent_class.margin
                << " parent_se="
                << root.parent_class.paired_standard_error
                << " parent_sigma="
                << root.parent_class.sigma
                << " candidate_margin="
                << root.candidate_class.margin
                << " candidate_se="
                << root.candidate_class
                       .paired_standard_error
                << " candidate_sigma="
                << root.candidate_class.sigma
                << " full_repair="
                << (root.full_repair ? 1 : 0)
                << " severity_regression="
                << (root.severity_regression ? 1 : 0)
                << " hidden="
                << (root.hidden_bit_identical ? 1 : 0)
                << " reverse="
                << (root.reverse_order_bit_identical ? 1 : 0)
                << '\n';
            for (std::size_t action = 0;
                 action < root.parent_logits.size();
                 ++action) {
                std::cout
                    << "parent_logit root_index=" << index
                    << " action_index=" << action
                    << " value=" << root.parent_logits[action]
                    << '\n';
            }
            std::cout
                << "parent_exact_support root_index="
                << index
                << " count="
                << root.parent_exact_support.size();
            for (const std::string& descriptor :
                 root.parent_exact_support) {
                std::cout
                    << " descriptor="
                    << std::quoted(descriptor);
            }
            std::cout << '\n';
        }

        for (std::size_t deck = 0;
             deck < old_school::kDeckCount; ++deck) {
            print_aggregate(
                old_school::deck_name(
                    static_cast<old_school::DeckId>(deck)),
                report.decks[deck]);
        }
        print_aggregate("Pooled", report.pooled);

        std::cout
            << "repairs roots=" << report.full_repairs
            << " games=" << report.distinct_repair_games
            << " decks=" << report.distinct_repair_decks
            << " severity_regressions="
            << report.severity_regressions
            << " candidate_class2_sigma_mass="
            << report.candidate_class2_sigma_mass
            << '\n'
            << "gates repair_roots="
            << (report.gates.repair_root_floor ? 1 : 0)
            << " repair_games="
            << (report.gates.repair_game_floor ? 1 : 0)
            << " repair_decks="
            << (report.gates.repair_deck_floor ? 1 : 0)
            << " zero_regressions="
            << (report.gates.zero_severity_regressions
                    ? 1
                    : 0)
            << " per_deck="
            << (report.gates.per_deck_nonregression ? 1 : 0)
            << " red_protected="
            << (report.gates.red_protected ? 1 : 0)
            << " pooled_high_confidence="
            << (report.gates.pooled_high_confidence_bound
                    ? 1
                    : 0)
            << " pooled_unsafe="
            << (report.gates.pooled_unsafe_bound ? 1 : 0)
            << " pooled_class1="
            << (report.gates.pooled_class1_bound ? 1 : 0)
            << " sigma="
            << (report.gates.class2_sigma_nonregression
                    ? 1
                    : 0)
            << " strict="
            << (report.gates.strict_registered_improvement
                    ? 1
                    : 0)
            << '\n'
            << "controls parent_reproduced="
            << (report.parent_reproduced ? 1 : 0)
            << " only_priority="
            << (report.only_priority_component_changed ? 1 : 0)
            << " hidden="
            << (report.hidden_bit_identical ? 1 : 0)
            << " reverse="
            << (report.reverse_order_bit_identical ? 1 : 0)
            << " repeat="
            << (report.repeated_evaluation_bit_identical
                    ? 1
                    : 0)
            << " zero_treatment_rollouts="
            << (report.zero_treatment_rollout_accounting
                    ? 1
                    : 0)
            << '\n'
            << "treatment_accounting search_calls="
            << report.treatment_accounting.search_calls
            << " worlds="
            << report.treatment_accounting.sampled_worlds
            << " evaluations="
            << report.treatment_accounting
                   .rollout_evaluations
            << " terminal="
            << report.treatment_accounting.terminal_leaves
            << " bootstrap="
            << report.treatment_accounting.bootstrap_leaves
            << " dominance_transitions="
            << report.treatment_accounting
                   .dominance_transitions
            << '\n'
            << "timings parent_reconstruction_seconds="
            << report.timings.parent_reconstruction_seconds
            << " d0b_fit_seconds="
            << report.timings.d0b_fit_seconds
            << " tensor_evaluation_seconds="
            << report.timings.tensor_evaluation_seconds
            << " total_seconds="
            << report.timings.total_seconds
            << '\n';

        for (const std::string& failure :
             report.scientific_failures) {
            std::cout
                << "scientific_failure=" << failure << '\n';
        }
        std::cout
            << "verdict=" << verdict_name(classification)
            << '\n';
        std::cout.flush();
        return static_cast<int>(classification);
    } catch (const std::exception& error) {
        std::cerr
            << "FQ4-D1-T0 infrastructure failure: "
            << error.what() << '\n';
        std::cerr.flush();
        return static_cast<int>(
            treatment::ExitClassification::
                InfrastructureFailure);
    }
}

void close_descriptor(int& descriptor) {
    if (descriptor >= 0) {
        static_cast<void>(::close(descriptor));
        descriptor = -1;
    }
}

bool make_nonblocking(int descriptor) {
    const int flags = ::fcntl(descriptor, F_GETFL, 0);
    return flags >= 0 &&
           ::fcntl(
               descriptor, F_SETFL,
               flags | O_NONBLOCK) == 0;
}

bool drain_descriptor(
    int descriptor, std::string& output, bool& eof) {
    std::array<char, 8192> buffer{};
    while (!eof) {
        const ssize_t count =
            ::read(
                descriptor, buffer.data(),
                buffer.size());
        if (count > 0) {
            output.append(
                buffer.data(),
                static_cast<std::size_t>(count));
            continue;
        }
        if (count == 0) {
            eof = true;
            return true;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN ||
            errno == EWOULDBLOCK) {
            return true;
        }
        return false;
    }
    return true;
}

bool drain_after_exit(
    int descriptor, std::string& output, bool& eof) {
    for (std::size_t attempt = 0;
         attempt < 100 && !eof; ++attempt) {
        if (!drain_descriptor(
                descriptor, output, eof)) {
            return false;
        }
        if (!eof) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1));
        }
    }
    return eof;
}

int publish_captured_worker(
    int status,
    const std::string& captured_stdout,
    const std::string& captured_stderr) {
    if (!WIFEXITED(status)) {
        if (!captured_stderr.empty()) {
            std::cerr << captured_stderr;
        }
        std::cerr
            << "FQ4-D1-T0 infrastructure failure: "
               "worker terminated by signal\n";
        return static_cast<int>(
            treatment::ExitClassification::
                InfrastructureFailure);
    }

    const int exit_status = WEXITSTATUS(status);
    if (exit_status ==
        static_cast<int>(
            treatment::ExitClassification::
                InfrastructureFailure)) {
        if (!captured_stderr.empty()) {
            std::cerr << captured_stderr;
        } else {
            std::cerr
                << "FQ4-D1-T0 infrastructure failure: "
                   "worker failed without a named reason\n";
        }
        return exit_status;
    }

    const bool pass =
        exit_status ==
        static_cast<int>(
            treatment::ExitClassification::Pass);
    const bool reject =
        exit_status ==
        static_cast<int>(
            treatment::ExitClassification::
                ScientificReject);
    const std::string expected_suffix =
        pass
            ? "verdict=PASS\n"
            : "verdict=SCIENTIFIC_REJECT\n";
    if ((!pass && !reject) ||
        !captured_stderr.empty() ||
        !captured_stdout.ends_with(expected_suffix)) {
        if (!captured_stderr.empty()) {
            std::cerr << captured_stderr;
        }
        std::cerr
            << "FQ4-D1-T0 infrastructure failure: "
               "worker did not produce one complete buffered report\n";
        return static_cast<int>(
            treatment::ExitClassification::
                InfrastructureFailure);
    }

    std::cout << captured_stdout;
    std::cout.flush();
    if (!std::cout.good()) {
        std::cerr
            << "FQ4-D1-T0 infrastructure failure: "
               "could not publish the complete buffered report\n";
        return static_cast<int>(
            treatment::ExitClassification::
                InfrastructureFailure);
    }
    return exit_status;
}

int supervise_worker() {
    std::array<int, 2> stdout_pipe{{-1, -1}};
    std::array<int, 2> stderr_pipe{{-1, -1}};
    if (::pipe(stdout_pipe.data()) != 0 ||
        ::pipe(stderr_pipe.data()) != 0) {
        close_descriptor(stdout_pipe[0]);
        close_descriptor(stdout_pipe[1]);
        close_descriptor(stderr_pipe[0]);
        close_descriptor(stderr_pipe[1]);
        std::cerr
            << "FQ4-D1-T0 infrastructure failure: "
               "could not create buffered-output pipes\n";
        return static_cast<int>(
            treatment::ExitClassification::
                InfrastructureFailure);
    }

    const pid_t child = ::fork();
    if (child < 0) {
        close_descriptor(stdout_pipe[0]);
        close_descriptor(stdout_pipe[1]);
        close_descriptor(stderr_pipe[0]);
        close_descriptor(stderr_pipe[1]);
        std::cerr
            << "FQ4-D1-T0 infrastructure failure: "
               "could not create watchdog worker\n";
        return static_cast<int>(
            treatment::ExitClassification::
                InfrastructureFailure);
    }
    if (child == 0) {
        close_descriptor(stdout_pipe[0]);
        close_descriptor(stderr_pipe[0]);
        const bool redirected =
            ::dup2(
                stderr_pipe[1],
                STDERR_FILENO) >= 0 &&
            ::dup2(
                stdout_pipe[1],
                STDOUT_FILENO) >= 0;
        close_descriptor(stdout_pipe[1]);
        close_descriptor(stderr_pipe[1]);
        if (!redirected) {
            ::_exit(static_cast<int>(
                treatment::ExitClassification::
                    InfrastructureFailure));
        }
        const int status = worker();
        std::cout.flush();
        std::cerr.flush();
        ::_exit(status);
    }

    close_descriptor(stdout_pipe[1]);
    close_descriptor(stderr_pipe[1]);
    if (!make_nonblocking(stdout_pipe[0]) ||
        !make_nonblocking(stderr_pipe[0])) {
        static_cast<void>(::kill(child, SIGKILL));
        static_cast<void>(::waitpid(child, nullptr, 0));
        close_descriptor(stdout_pipe[0]);
        close_descriptor(stderr_pipe[0]);
        std::cerr
            << "FQ4-D1-T0 infrastructure failure: "
               "could not configure buffered-output pipes\n";
        return static_cast<int>(
            treatment::ExitClassification::
                InfrastructureFailure);
    }

    std::string captured_stdout;
    std::string captured_stderr;
    bool stdout_eof = false;
    bool stderr_eof = false;
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(
            treatment::kWatchdogSeconds);
    while (true) {
        if (!drain_descriptor(
                stdout_pipe[0], captured_stdout,
                stdout_eof) ||
            !drain_descriptor(
                stderr_pipe[0], captured_stderr,
                stderr_eof)) {
            static_cast<void>(::kill(child, SIGKILL));
            static_cast<void>(
                ::waitpid(child, nullptr, 0));
            close_descriptor(stdout_pipe[0]);
            close_descriptor(stderr_pipe[0]);
            std::cerr
                << "FQ4-D1-T0 infrastructure failure: "
                   "could not buffer worker output\n";
            return static_cast<int>(
                treatment::ExitClassification::
                    InfrastructureFailure);
        }

        int status = 0;
        const pid_t waited =
            ::waitpid(child, &status, WNOHANG);
        if (waited == child) {
            const bool drained =
                drain_after_exit(
                    stdout_pipe[0],
                    captured_stdout,
                    stdout_eof) &&
                drain_after_exit(
                    stderr_pipe[0],
                    captured_stderr,
                    stderr_eof);
            close_descriptor(stdout_pipe[0]);
            close_descriptor(stderr_pipe[0]);
            if (!drained) {
                std::cerr
                    << "FQ4-D1-T0 infrastructure failure: "
                       "buffered worker output did not close\n";
                return static_cast<int>(
                    treatment::ExitClassification::
                        InfrastructureFailure);
            }
            return publish_captured_worker(
                status, captured_stdout,
                captured_stderr);
        }
        if (waited < 0) {
            static_cast<void>(::kill(child, SIGKILL));
            static_cast<void>(
                ::waitpid(child, nullptr, 0));
            close_descriptor(stdout_pipe[0]);
            close_descriptor(stderr_pipe[0]);
            std::cerr
                << "FQ4-D1-T0 infrastructure failure: "
                   "watchdog wait failed\n";
            return static_cast<int>(
                treatment::ExitClassification::
                    InfrastructureFailure);
        }
        if (std::chrono::steady_clock::now() >=
            deadline) {
            static_cast<void>(::kill(child, SIGKILL));
            static_cast<void>(
                ::waitpid(child, nullptr, 0));
            close_descriptor(stdout_pipe[0]);
            close_descriptor(stderr_pipe[0]);
            if (!captured_stderr.empty()) {
                std::cerr << captured_stderr;
            }
            std::cerr
                << "FQ4-D1-T0 infrastructure failure: "
                   "hard 240-second watchdog expired\n";
            return static_cast<int>(
                treatment::ExitClassification::
                    InfrastructureFailure);
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(20));
    }
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 1) {
        const std::string program =
            argc > 0 && argv[0] != nullptr
                ? argv[0]
                : "old-school-fq4-priority-fit-d1";
        std::cerr << "Usage: " << program << '\n';
        return static_cast<int>(
            treatment::ExitClassification::
                InfrastructureFailure);
    }
    return supervise_worker();
}
