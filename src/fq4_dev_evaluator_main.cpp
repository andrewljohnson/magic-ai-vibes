#include "old_school/fq4_dev_evaluator.hpp"

#include "old_school/artifact_integrity.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

namespace bundle = old_school::fq4_dev_bundle;
namespace evaluator = old_school::fq4_dev_evaluator;
namespace integrity = old_school::artifact_integrity;

bool projection_work_is_practical(
    const evaluator::ConstraintCensus& census) {
    constexpr std::uint64_t kMaximumSubsetsPerRoot = 63;
    return census.maximum_constraints > 0 &&
           census.maximum_constraints < 64 &&
           ((std::uint64_t{1}
             << census.maximum_constraints) -
            1U) <= kMaximumSubsetsPerRoot;
}

std::string run(std::string_view mode) {
    const integrity::RegularFileSnapshot artifact_before =
        integrity::snapshot_regular_file(
            std::string(bundle::kArtifactPath));
    const bundle::Bundle artifact =
        bundle::load_published();
    const evaluator::ConstraintCensus census =
        evaluator::constraint_census(artifact);
    if (mode == "--constraint-census") {
        const std::string output =
            evaluator::format_constraint_census(
                census);
        if (integrity::snapshot_regular_file(
                std::string(bundle::kArtifactPath)) !=
            artifact_before) {
            throw std::runtime_error(
                "fixed bundle changed during census");
        }
        return output;
    }
    if (!projection_work_is_practical(census)) {
        throw std::runtime_error(
            "frozen projection work exceeds the prefit bound");
    }

    const evaluator::PreparedCorpus corpus =
        evaluator::prepare(artifact);
    const integrity::RegularFileSnapshot parent_before =
        integrity::snapshot_regular_file(
            std::string(
                evaluator::kParentArtifactPath));
    const auto parent =
        evaluator::load_fixed_parent();
    std::string output;
    if (mode == "--evaluate-parent") {
        const evaluator::ModelEvaluationReport report =
            evaluator::evaluate_models(
                corpus, parent, parent);
        output = evaluator::format_evaluation_report(
            "evaluate-parent", report,
            evaluator::FitAccounting{
                .optimizer = evaluator::kOptimizer,
            });
    } else if (mode == "--fit") {
        const evaluator::CandidateFit fit =
            evaluator::fit_candidate(
                corpus, parent);
        const evaluator::ModelEvaluationReport report =
            evaluator::evaluate_models(
                corpus, parent, fit.model);
        output =
            evaluator::format_constraint_census(
                census) +
            evaluator::format_evaluation_report(
                "fit", report,
                fit.accounting);
    } else {
        throw std::invalid_argument(
            "unsupported fixed mode");
    }
    if (integrity::snapshot_regular_file(
            std::string(bundle::kArtifactPath)) !=
            artifact_before ||
        integrity::snapshot_regular_file(
            std::string(
                evaluator::kParentArtifactPath)) !=
            parent_before) {
        throw std::runtime_error(
            "fixed artifact changed during evaluation");
    }
    return output;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2 ||
        (std::string_view(argv[1]) !=
             "--constraint-census" &&
         std::string_view(argv[1]) !=
             "--evaluate-parent" &&
         std::string_view(argv[1]) != "--fit")) {
        std::cerr
            << "Usage: old-school-fq4-priority-dev-evaluate "
               "--constraint-census|--evaluate-parent|--fit\n";
        return 2;
    }
    try {
        const std::string output = run(argv[1]);
        std::cout << output;
        std::cout.flush();
        return std::cout.good() ? 0 : 2;
    } catch (const std::exception&) {
        std::cerr
            << "result=ERROR"
               " reason=fixed_artifact_evaluation_failed\n";
        return 2;
    }
}
