#include "old_school/fq4_priority_collection.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace old_school::fq4_priority_collection {

std::string_view parent_class_name(
    ParentClass classification) {
    switch (classification) {
    case ParentClass::Safe:
        return "Safe";
    case ParentClass::Class1:
        return "Class 1";
    case ParentClass::Class2:
        return "Class 2";
    case ParentClass::Class3:
        return "Class 3";
    case ParentClass::Invalid:
        return "Invalid";
    }
    return "Invalid";
}

bool ParentClassResult::high_confidence_unsafe() const {
    return valid &&
           (classification == ParentClass::Class1 ||
            classification == ParentClass::Class2);
}

ParentClassResult classify_parent(
    const ParentClassInput& input,
    std::size_t expected_worlds) {
    ParentClassResult result;
    const std::size_t count =
        input.canonical_descriptors.size();
    if (expected_worlds < 2 ||
        count < 2 ||
        input.base_scores.size() != count ||
        input.combined_scores.size() != count ||
        input.base_samples.size() != count ||
        input.robustly_pass_dominated.size() != count ||
        !std::is_sorted(
            input.canonical_descriptors.begin(),
            input.canonical_descriptors.end()) ||
        std::adjacent_find(
            input.canonical_descriptors.begin(),
            input.canonical_descriptors.end()) !=
            input.canonical_descriptors.end()) {
        return result;
    }
    std::optional<std::size_t> best_dominated;
    std::optional<std::size_t> best_nondominated;
    for (std::size_t index = 0; index < count; ++index) {
        if (!std::isfinite(input.base_scores[index]) ||
            !std::isfinite(input.combined_scores[index]) ||
            input.base_samples[index].size() !=
                expected_worlds ||
            !std::all_of(
                input.base_samples[index].begin(),
                input.base_samples[index].end(),
                [](double value) {
                    return std::isfinite(value);
                })) {
            return result;
        }
        std::optional<std::size_t>& best =
            input.robustly_pass_dominated[index]
                ? best_dominated
                : best_nondominated;
        if (!best.has_value() ||
            input.combined_scores[index] >
                input.combined_scores[*best]) {
            best = index;
        }
    }
    if (!best_dominated.has_value() ||
        !best_nondominated.has_value()) {
        return result;
    }
    result.best_dominated_index = *best_dominated;
    result.best_nondominated_index = *best_nondominated;
    result.margin =
        input.combined_scores[*best_dominated] -
        input.combined_scores[*best_nondominated];
    if (!std::isfinite(result.margin)) {
        return ParentClassResult{};
    }
    const double residual_difference =
        (input.combined_scores[*best_dominated] -
         input.base_scores[*best_dominated]) -
        (input.combined_scores[*best_nondominated] -
         input.base_scores[*best_nondominated]);
    std::vector<double> differences(expected_worlds);
    double mean = 0.0;
    for (std::size_t world = 0;
         world < expected_worlds; ++world) {
        differences[world] =
            input.base_samples[*best_dominated][world] -
            input.base_samples[*best_nondominated][world] +
            residual_difference;
        if (!std::isfinite(differences[world])) {
            return ParentClassResult{};
        }
        mean += differences[world];
    }
    mean /= static_cast<double>(expected_worlds);
    double squared = 0.0;
    for (const double difference : differences) {
        const double centered = difference - mean;
        squared += centered * centered;
    }
    result.paired_standard_error =
        std::sqrt(
            squared /
            static_cast<double>(
                expected_worlds *
                (expected_worlds - 1)));
    if (!std::isfinite(
            result.paired_standard_error)) {
        return ParentClassResult{};
    }
    if (result.margin < 0.0) {
        result.classification = ParentClass::Safe;
        result.sigma =
            result.paired_standard_error == 0.0
                ? 0.0
                : result.margin /
                      result.paired_standard_error;
    } else if (
        result.margin > 0.0 &&
        result.paired_standard_error == 0.0) {
        result.classification = ParentClass::Class1;
        result.sigma = 0.0;
    } else if (
        result.margin > 0.0 &&
        result.paired_standard_error > 0.0 &&
        result.margin /
                result.paired_standard_error >=
            3.0) {
        result.classification = ParentClass::Class2;
        result.sigma =
            result.margin /
            result.paired_standard_error;
    } else {
        result.classification = ParentClass::Class3;
        result.sigma =
            result.paired_standard_error == 0.0
                ? 0.0
                : result.margin /
                      result.paired_standard_error;
    }
    if (!std::isfinite(result.sigma)) {
        return ParentClassResult{};
    }
    result.valid = true;
    return result;
}

} // namespace old_school::fq4_priority_collection
