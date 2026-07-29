#include "old_school/decision_density_sparse_cross.hpp"

#include "old_school/artifact_integrity.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <ostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace old_school::decision_density_sparse_cross {
namespace {

constexpr double kOofPairBceImprovement = 0.000025;
constexpr double kOofRegretImprovement = 0.0005;

std::size_t deck_index(DeckId deck) {
    const std::size_t result =
        static_cast<std::size_t>(deck);
    if (result >= kDeckCount) {
        throw std::invalid_argument(
            "AQ20 sparse-cross deck is invalid");
    }
    return result;
}

std::size_t width_index(
    aq19::priority::WidthStratum width) {
    const std::size_t result =
        static_cast<std::size_t>(width);
    if (result >= aq19::priority::kWidthStrata) {
        throw std::invalid_argument(
            "AQ20 sparse-cross width is invalid");
    }
    return result;
}

std::size_t cell_index(
    DeckId deck,
    aq19::priority::WidthStratum width) {
    return deck_index(deck) *
               aq19::priority::kWidthStrata +
           width_index(width);
}

double positive_zero(double value) {
    return value == 0.0 ? 0.0 : value;
}

bool bit_equal(double left, double right) {
    return std::bit_cast<std::uint64_t>(left) ==
           std::bit_cast<std::uint64_t>(right);
}

double sigmoid(double value) {
    if (value >= 0.0) {
        const double exponential = std::exp(-value);
        return 1.0 / (1.0 + exponential);
    }
    const double exponential = std::exp(value);
    return exponential / (1.0 + exponential);
}

double binary_cross_entropy_from_logit(
    double target, double logit) {
    const double softplus =
        logit > 0.0
            ? logit +
                  std::log1p(std::exp(-logit))
            : std::log1p(std::exp(logit));
    return softplus - target * logit;
}

void append_u64(
    std::vector<std::uint8_t>& bytes,
    std::uint64_t value) {
    for (std::size_t byte = 0; byte < 8; ++byte) {
        bytes.push_back(
            static_cast<std::uint8_t>(
                (value >> (8 * byte)) &
                UINT64_C(0xff)));
    }
}

void append_double(
    std::vector<std::uint8_t>& bytes,
    double value) {
    append_u64(
        bytes,
        std::bit_cast<std::uint64_t>(
            positive_zero(value)));
}

std::string bytes_string(
    std::span<const std::uint8_t> bytes) {
    return std::string(
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size());
}

struct UnsignedBytesLess {
    bool operator()(
        const std::vector<std::uint8_t>& left,
        const std::vector<std::uint8_t>& right) const {
        return std::lexicographical_compare(
            left.begin(), left.end(),
            right.begin(), right.end());
    }
};

std::vector<std::vector<double>>
positive_zero_residuals(const aq19::Dataset& dataset) {
    std::vector<std::vector<double>> result;
    result.reserve(dataset.roots.size());
    for (const aq19::Root& root : dataset.roots) {
        result.emplace_back(root.options.size(), 0.0);
    }
    return result;
}

std::size_t fold_for_group(
    const aq19::FoldAssignment& assignment,
    std::string_view group) {
    const auto found = std::lower_bound(
        assignment.group_folds.begin(),
        assignment.group_folds.end(), group,
        [](const auto& item, std::string_view key) {
            return item.first < key;
        });
    if (found == assignment.group_folds.end() ||
        found->first != group ||
        found->second >= aq19::kFoldCount) {
        throw std::invalid_argument(
            "AQ20 sparse-cross group fold is invalid");
    }
    return found->second;
}

struct Column {
    testing::ColumnSpec spec;
    std::vector<std::vector<double>> values;
    std::vector<std::uint8_t> sign_identity;
};

struct PreparedColumns {
    std::size_t eligible_coordinates = 0;
    std::vector<Column> representatives;
    std::string canonical_sha256;
};

bool stage_candidate_better(
    const testing::StageCandidate& candidate,
    const testing::StageCandidate& incumbent) {
    return
        candidate.derivative.actual_improvement >
            incumbent.derivative.actual_improvement ||
        (candidate.derivative.actual_improvement ==
             incumbent.derivative.actual_improvement &&
         std::pair{
             candidate.state_feature,
             candidate.action_feature} <
             std::pair{
                 incumbent.state_feature,
                 incumbent.action_feature});
}

std::vector<std::vector<double>> column_values(
    const aq19::Dataset& dataset,
    const testing::ColumnSpec& spec) {
    if (spec.state_feature >=
            aq19::kStateFeatureCount ||
        spec.action_feature >=
            aq19::kActionFeatureCount ||
        !std::isfinite(spec.sigma) ||
        spec.sigma <= 0.0 ||
        spec.root_support == 0 ||
        spec.group_support == 0 ||
        !std::isfinite(
            spec.maximum_group_leverage) ||
        spec.maximum_group_leverage < 0.0) {
        throw std::invalid_argument(
            "AQ20 sparse-cross column spec is invalid");
    }
    std::vector<std::vector<double>> result;
    result.reserve(dataset.roots.size());
    for (const aq19::Root& root : dataset.roots) {
        const auto order =
            aq19::canonical_option_order(root);
        double action_mean = 0.0;
        for (const std::size_t row : order) {
            action_mean +=
                root.options[row]
                    .action_features[
                        spec.action_feature];
        }
        action_mean /=
            static_cast<double>(order.size());
        std::vector<double> root_values(
            root.options.size(), 0.0);
        for (std::size_t row = 0;
             row < root.options.size(); ++row) {
            const double value =
                root.state[spec.state_feature] *
                (root.options[row]
                         .action_features[
                             spec.action_feature] -
                 action_mean) /
                spec.sigma;
            if (!std::isfinite(value)) {
                throw std::runtime_error(
                    "AQ20 sparse-cross standardized "
                    "column is nonfinite");
            }
            root_values[row] = positive_zero(value);
        }
        result.push_back(std::move(root_values));
    }
    return result;
}

std::vector<std::uint8_t> signed_column_bytes(
    const aq19::Dataset& dataset,
    const std::vector<std::vector<double>>& values,
    double sign) {
    if (values.size() != dataset.roots.size() ||
        (sign != 1.0 && sign != -1.0)) {
        throw std::invalid_argument(
            "AQ20 sparse-cross column byte shape drifted");
    }
    std::vector<std::uint8_t> bytes;
    for (std::size_t root_index = 0;
         root_index < dataset.roots.size();
         ++root_index) {
        const aq19::Root& root =
            dataset.roots[root_index];
        if (values[root_index].size() !=
            root.options.size()) {
            throw std::invalid_argument(
                "AQ20 sparse-cross column action shape "
                "drifted");
        }
        const auto order =
            aq19::canonical_option_order(root);
        for (const std::size_t row : order) {
            append_double(
                bytes,
                sign * values[root_index][row]);
        }
    }
    return bytes;
}

PreparedColumns prepare_columns(
    const aq19::Dataset& dataset,
    std::vector<testing::ColumnSpec> specs) {
    support::validate_label_blind_dataset(dataset);
    std::sort(
        specs.begin(), specs.end(),
        [](const auto& left, const auto& right) {
            return std::tie(
                       left.state_feature,
                       left.action_feature) <
                   std::tie(
                       right.state_feature,
                       right.action_feature);
        });
    if (std::adjacent_find(
            specs.begin(), specs.end(),
            [](const auto& left, const auto& right) {
                return left.state_feature ==
                           right.state_feature &&
                       left.action_feature ==
                           right.action_feature;
            }) != specs.end()) {
        throw std::invalid_argument(
            "AQ20 sparse-cross coordinate is duplicated");
    }

    PreparedColumns result{
        .eligible_coordinates = specs.size(),
    };
    std::map<
        std::vector<std::uint8_t>,
        std::size_t,
        UnsignedBytesLess>
        equivalence_classes;
    for (const testing::ColumnSpec& spec : specs) {
        Column column{
            .spec = spec,
            .values = column_values(dataset, spec),
        };
        auto positive =
            signed_column_bytes(
                dataset, column.values, 1.0);
        auto negative =
            signed_column_bytes(
                dataset, column.values, -1.0);
        column.sign_identity =
            UnsignedBytesLess{}(negative, positive)
                ? std::move(negative)
                : std::move(positive);
        const auto [position, inserted] =
            equivalence_classes.emplace(
                column.sign_identity,
                result.representatives.size());
        if (!inserted) {
            const Column& retained =
                result.representatives[
                    position->second];
            if (retained.sign_identity !=
                column.sign_identity) {
                throw std::runtime_error(
                    "AQ20 sparse-cross equivalence "
                    "identity collision");
            }
            continue;
        }
        result.representatives.push_back(
            std::move(column));
    }
    std::vector<std::uint8_t> canonical;
    for (const Column& column :
         result.representatives) {
        append_u64(
            canonical, column.spec.state_feature);
        append_u64(
            canonical, column.spec.action_feature);
        append_double(canonical, column.spec.sigma);
        append_u64(
            canonical, column.sign_identity.size());
        canonical.insert(
            canonical.end(),
            column.sign_identity.begin(),
            column.sign_identity.end());
    }
    result.canonical_sha256 =
        artifact_integrity::sha256_string(
            bytes_string(canonical));
    return result;
}

PreparedColumns prepare_columns(
    const aq19::Dataset& dataset,
    const support::PartitionReport& report) {
    if (report.coordinates.size() !=
            support::kCoordinateCount ||
        report.roots != dataset.roots.size() ||
        report.canonical_table_sha256 !=
            artifact_integrity::sha256_string(
                support::canonical_partition_table(
                    report))) {
        throw std::invalid_argument(
            "AQ20 sparse-cross support partition "
            "identity drifted");
    }
    std::vector<testing::ColumnSpec> specs;
    specs.reserve(report.eligible_coordinates);
    for (std::size_t coordinate = 0;
         coordinate < report.coordinates.size();
         ++coordinate) {
        const support::CoordinateRow& row =
            report.coordinates[coordinate];
        if (!row.eligible) {
            continue;
        }
        if (!row.finite || !row.active ||
            !std::isfinite(row.weighted_energy) ||
            row.weighted_energy <= 0.0 ||
            row.root_support <
                support::kMinimumRootSupport ||
            row.group_support <
                support::kMinimumGroupSupport ||
            row.maximum_group_leverage >
                support::kMaximumGroupLeverage) {
            throw std::invalid_argument(
                "AQ20 sparse-cross eligible support "
                "row is invalid");
        }
        specs.push_back({
            .state_feature =
                coordinate /
                aq19::kActionFeatureCount,
            .action_feature =
                coordinate %
                aq19::kActionFeatureCount,
            .sigma = std::sqrt(row.weighted_energy),
            .root_support = row.root_support,
            .group_support = row.group_support,
            .maximum_group_leverage =
                row.maximum_group_leverage,
        });
    }
    if (specs.size() !=
        report.eligible_coordinates) {
        throw std::invalid_argument(
            "AQ20 sparse-cross eligible support count "
            "drifted");
    }
    return prepare_columns(dataset, std::move(specs));
}

struct ForwardRows {
    std::vector<std::vector<double>> centered_logits;
    std::vector<std::vector<double>> residuals;
    double maximum_absolute_centered_logit = 0.0;
    std::size_t saturated_roots = 0;
};

const Column* find_column(
    const PreparedColumns& columns,
    const Term& term) {
    const auto found = std::lower_bound(
        columns.representatives.begin(),
        columns.representatives.end(),
        std::pair{
            term.state_feature,
            term.action_feature},
        [](const Column& column, const auto& key) {
            return std::pair{
                       column.spec.state_feature,
                       column.spec.action_feature} <
                   key;
        });
    if (found == columns.representatives.end() ||
        found->spec.state_feature !=
            term.state_feature ||
        found->spec.action_feature !=
            term.action_feature ||
        !bit_equal(found->spec.sigma, term.sigma)) {
        throw std::invalid_argument(
            "AQ20 sparse-cross term is not a prepared "
            "column");
    }
    return &*found;
}

ForwardRows forward_rows(
    const aq19::Dataset& dataset,
    const PreparedColumns& columns,
    std::span<const Term> terms) {
    ForwardRows result;
    result.centered_logits.reserve(
        dataset.roots.size());
    result.residuals.reserve(dataset.roots.size());
    std::vector<const Column*> selected;
    selected.reserve(terms.size());
    for (const Term& term : terms) {
        if (!std::isfinite(term.beta) ||
            term.beta < -kMaximumAbsoluteBeta ||
            term.beta > kMaximumAbsoluteBeta) {
            throw std::invalid_argument(
                "AQ20 sparse-cross beta is invalid");
        }
        selected.push_back(
            find_column(columns, term));
    }
    for (std::size_t root_index = 0;
         root_index < dataset.roots.size();
         ++root_index) {
        const aq19::Root& root =
            dataset.roots[root_index];
        const auto order =
            aq19::canonical_option_order(root);
        std::vector<double> logits(
            root.options.size(), 0.0);
        for (std::size_t term_index = 0;
             term_index < terms.size();
             ++term_index) {
            for (std::size_t row = 0;
                 row < root.options.size(); ++row) {
                const double contribution =
                    terms[term_index].beta *
                    selected[term_index]
                        ->values[root_index][row];
                if (!std::isfinite(contribution)) {
                    throw std::runtime_error(
                        "AQ20 sparse-cross contribution "
                        "is nonfinite");
                }
                logits[row] += contribution;
                if (!std::isfinite(logits[row])) {
                    throw std::runtime_error(
                        "AQ20 sparse-cross logit is "
                        "nonfinite");
                }
            }
        }
        double mean = 0.0;
        for (const std::size_t row : order) {
            mean += logits[row];
            if (!std::isfinite(mean)) {
                throw std::runtime_error(
                    "AQ20 sparse-cross centered mean is "
                    "nonfinite");
            }
        }
        mean /= static_cast<double>(order.size());
        std::vector<double> centered(
            root.options.size(), 0.0);
        std::vector<double> residual(
            root.options.size(), 0.0);
        bool saturated = false;
        for (std::size_t row = 0;
             row < root.options.size(); ++row) {
            centered[row] =
                positive_zero(logits[row] - mean);
            const double absolute =
                std::abs(centered[row]);
            result.maximum_absolute_centered_logit =
                std::max(
                    result
                        .maximum_absolute_centered_logit,
                    absolute);
            saturated = saturated || absolute >= 1.0;
            residual[row] =
                positive_zero(
                    kResidualWeight *
                    std::tanh(centered[row]));
            if (!std::isfinite(residual[row]) ||
                residual[row] < -kResidualWeight ||
                residual[row] > kResidualWeight) {
                throw std::runtime_error(
                    "AQ20 sparse-cross residual escaped "
                    "its bound");
            }
        }
        result.saturated_roots +=
            saturated ? 1U : 0U;
        result.centered_logits.push_back(
            std::move(centered));
        result.residuals.push_back(
            std::move(residual));
    }
    return result;
}

std::vector<std::vector<double>> direct_residuals(
    const aq19::Dataset& dataset,
    std::span<const Term> terms) {
    aq19::validate_dataset(dataset);
    std::set<std::pair<std::size_t, std::size_t>>
        coordinates;
    for (const Term& term : terms) {
        if (term.state_feature >=
                aq19::kStateFeatureCount ||
            term.action_feature >=
                aq19::kActionFeatureCount ||
            !std::isfinite(term.sigma) ||
            term.sigma <= 0.0 ||
            !std::isfinite(term.beta) ||
            term.beta < -kMaximumAbsoluteBeta ||
            term.beta > kMaximumAbsoluteBeta ||
            !coordinates
                 .emplace(
                     term.state_feature,
                     term.action_feature)
                 .second) {
            throw std::invalid_argument(
                "AQ20 sparse-cross direct term is "
                "invalid");
        }
    }
    std::vector<std::vector<double>> result;
    result.reserve(dataset.roots.size());
    for (const aq19::Root& root : dataset.roots) {
        const auto order =
            aq19::canonical_option_order(root);
        std::vector<double> logits(
            root.options.size(), 0.0);
        for (const Term& term : terms) {
            double action_mean = 0.0;
            for (const std::size_t row : order) {
                action_mean +=
                    root.options[row]
                        .action_features[
                            term.action_feature];
            }
            action_mean /=
                static_cast<double>(order.size());
            for (std::size_t row = 0;
                 row < root.options.size(); ++row) {
                const double phi =
                    root.state[term.state_feature] *
                    (root.options[row]
                             .action_features[
                                 term.action_feature] -
                     action_mean) /
                    term.sigma;
                const double contribution =
                    term.beta * phi;
                if (!std::isfinite(phi) ||
                    !std::isfinite(contribution)) {
                    throw std::runtime_error(
                        "AQ20 sparse-cross direct "
                        "contribution is nonfinite");
                }
                logits[row] += contribution;
                if (!std::isfinite(logits[row])) {
                    throw std::runtime_error(
                        "AQ20 sparse-cross direct logit "
                        "is nonfinite");
                }
            }
        }
        double mean = 0.0;
        for (const std::size_t row : order) {
            mean += logits[row];
            if (!std::isfinite(mean)) {
                throw std::runtime_error(
                    "AQ20 sparse-cross direct mean is "
                    "nonfinite");
            }
        }
        mean /= static_cast<double>(order.size());
        std::vector<double> residual(
            root.options.size(), 0.0);
        for (std::size_t row = 0;
             row < root.options.size(); ++row) {
            residual[row] =
                positive_zero(
                    kResidualWeight *
                    std::tanh(logits[row] - mean));
        }
        result.push_back(std::move(residual));
    }
    return result;
}

std::vector<std::vector<double>> runtime_residuals(
    const aq19::Dataset& dataset,
    const Terms& terms) {
    if (terms.size() != kTermCount) {
        throw std::invalid_argument(
            "AQ20 sparse-cross deployed scoring "
            "requires sixteen terms");
    }
    const LearnedPrioritySparseCross runtime(terms);
    std::vector<std::vector<double>> result;
    result.reserve(dataset.roots.size());
    for (const aq19::Root& root : dataset.roots) {
        std::vector<
            LearnedPrioritySparseCrossAction>
            actions;
        actions.reserve(root.options.size());
        for (const aq19::Option& option :
             root.options) {
            actions.push_back(option.action_features);
        }
        result.push_back(
            runtime.residuals(
                root.state, actions,
                aq19::canonical_option_order(root)));
    }
    return result;
}

bool double_rows_bit_identical(
    const std::vector<std::vector<double>>& first,
    const std::vector<std::vector<double>>& second) {
    if (first.size() != second.size()) {
        return false;
    }
    for (std::size_t root = 0;
         root < first.size(); ++root) {
        if (first[root].size() !=
            second[root].size()) {
            return false;
        }
        for (std::size_t action = 0;
             action < first[root].size(); ++action) {
            if (!bit_equal(
                    first[root][action],
                    second[root][action])) {
                return false;
            }
        }
    }
    return true;
}

bool fit_bit_identical(
    const FitReport& first,
    const FitReport& second) {
    if (first.completed != second.completed ||
        first.completed_stages !=
            second.completed_stages ||
        first.failure != second.failure ||
        first.term_sha256 != second.term_sha256 ||
        first.eligible_coordinates !=
            second.eligible_coordinates ||
        first.representative_coordinates !=
            second.representative_coordinates ||
        first.terms.size() != second.terms.size() ||
        !double_rows_bit_identical(
            first.residuals, second.residuals) ||
        !bit_equal(
            first.maximum_absolute_centered_logit,
            second.maximum_absolute_centered_logit) ||
        first.saturated_roots !=
            second.saturated_roots ||
        !bit_equal(
            first.saturated_root_fraction,
            second.saturated_root_fraction)) {
        return false;
    }
    for (std::size_t term = 0;
         term < first.terms.size(); ++term) {
        if (first.terms[term].state_feature !=
                second.terms[term].state_feature ||
            first.terms[term].action_feature !=
                second.terms[term].action_feature ||
            !bit_equal(
                first.terms[term].sigma,
                second.terms[term].sigma) ||
            !bit_equal(
                first.terms[term].beta,
                second.terms[term].beta)) {
            return false;
        }
    }
    if ((first.terms.empty() ||
         first.terms.size() == kTermCount) &&
        learned_priority_sparse_cross_canonical_bytes(
            first.terms) !=
            learned_priority_sparse_cross_canonical_bytes(
                second.terms)) {
        return false;
    }
    for (std::size_t stage = 0;
         stage < kTermCount; ++stage) {
        const SelectedTerm& left =
            first.selected[stage];
        const SelectedTerm& right =
            second.selected[stage];
        if (left.term.state_feature !=
                right.term.state_feature ||
            left.term.action_feature !=
                right.term.action_feature ||
            !bit_equal(
                left.term.sigma,
                right.term.sigma) ||
            !bit_equal(
                left.term.beta,
                right.term.beta) ||
            left.root_support != right.root_support ||
            left.group_support !=
                right.group_support ||
            !bit_equal(
                left.maximum_group_leverage,
                right.maximum_group_leverage) ||
            !bit_equal(
                left.derivative.gradient,
                right.derivative.gradient) ||
            !bit_equal(
                left.derivative.diagonal,
                right.derivative.diagonal) ||
            !bit_equal(
                left.derivative.beta,
                right.derivative.beta) ||
            !bit_equal(
                left.derivative.actual_improvement,
                right.derivative.actual_improvement) ||
            left.derivative.valid !=
                right.derivative.valid ||
            left.derivative.clipped !=
                right.derivative.clipped) {
            return false;
        }
    }
    return true;
}

double pair_objective(
    const aq19::Dataset& dataset,
    const ForwardRows& forward) {
    if (forward.residuals.size() !=
        dataset.roots.size()) {
        throw std::invalid_argument(
            "AQ20 sparse-cross forward root shape "
            "drifted");
    }
    double objective = 0.0;
    for (std::size_t root_index = 0;
         root_index < dataset.roots.size();
         ++root_index) {
        const aq19::Root& root =
            dataset.roots[root_index];
        const auto order =
            aq19::canonical_option_order(root);
        double total_cost = 0.0;
        for (std::size_t left = 0;
             left < order.size(); ++left) {
            for (std::size_t right = left + 1;
                 right < order.size(); ++right) {
                total_cost += std::abs(
                    root.options[order[left]]
                        .teacher_aggregate_score -
                    root.options[order[right]]
                        .teacher_aggregate_score);
            }
        }
        if (total_cost == 0.0) {
            continue;
        }
        const double root_weight =
            1.0 /
            (static_cast<double>(aq19::kCellCount) *
             static_cast<double>(
                 dataset.roots_by_cell[
                     cell_index(
                         root.deck, root.width)]));
        for (std::size_t left = 0;
             left < order.size(); ++left) {
            const std::size_t i = order[left];
            for (std::size_t right = left + 1;
                 right < order.size(); ++right) {
                const std::size_t j = order[right];
                const double gap =
                    root.options[i]
                        .teacher_aggregate_score -
                    root.options[j]
                        .teacher_aggregate_score;
                if (gap == 0.0) {
                    continue;
                }
                const double target =
                    sigmoid(gap / kPairTemperature);
                const double candidate_logit =
                    (root.options[i]
                             .base_aggregate_score +
                         forward.residuals[root_index][i] -
                     root.options[j]
                             .base_aggregate_score -
                         forward.residuals[root_index][j]) /
                    kPairTemperature;
                objective +=
                    root_weight *
                    std::abs(gap) / total_cost *
                    binary_cross_entropy_from_logit(
                        target, candidate_logit);
            }
        }
    }
    if (!std::isfinite(objective)) {
        throw std::runtime_error(
            "AQ20 sparse-cross objective is nonfinite");
    }
    return objective;
}

StageDerivative derivative_for_column(
    const aq19::Dataset& dataset,
    const ForwardRows& forward,
    const Column& candidate) {
    double gradient = 0.0;
    double diagonal = 0.0;
    for (std::size_t root_index = 0;
         root_index < dataset.roots.size();
         ++root_index) {
        const aq19::Root& root =
            dataset.roots[root_index];
        const auto order =
            aq19::canonical_option_order(root);
        double total_cost = 0.0;
        for (std::size_t left = 0;
             left < order.size(); ++left) {
            for (std::size_t right = left + 1;
                 right < order.size(); ++right) {
                total_cost += std::abs(
                    root.options[order[left]]
                        .teacher_aggregate_score -
                    root.options[order[right]]
                        .teacher_aggregate_score);
            }
        }
        if (total_cost == 0.0) {
            continue;
        }
        double phi_mean = 0.0;
        for (const std::size_t row : order) {
            phi_mean +=
                candidate.values[root_index][row];
        }
        phi_mean /=
            static_cast<double>(order.size());
        std::vector<double> score_derivative(
            root.options.size(), 0.0);
        for (std::size_t row = 0;
             row < root.options.size(); ++row) {
            const double tanh_centered =
                std::tanh(
                    forward
                        .centered_logits[root_index][row]);
            score_derivative[row] =
                kResidualWeight *
                (1.0 -
                 tanh_centered * tanh_centered) *
                (candidate.values[root_index][row] -
                 phi_mean);
        }
        const double root_weight =
            1.0 /
            (static_cast<double>(aq19::kCellCount) *
             static_cast<double>(
                 dataset.roots_by_cell[
                     cell_index(
                         root.deck, root.width)]));
        for (std::size_t left = 0;
             left < order.size(); ++left) {
            const std::size_t i = order[left];
            for (std::size_t right = left + 1;
                 right < order.size(); ++right) {
                const std::size_t j = order[right];
                const double gap =
                    root.options[i]
                        .teacher_aggregate_score -
                    root.options[j]
                        .teacher_aggregate_score;
                if (gap == 0.0) {
                    continue;
                }
                const double weight =
                    root_weight *
                    std::abs(gap) / total_cost;
                const double target =
                    sigmoid(gap / kPairTemperature);
                const double candidate_logit =
                    (root.options[i]
                             .base_aggregate_score +
                         forward.residuals[root_index][i] -
                     root.options[j]
                             .base_aggregate_score -
                         forward.residuals[root_index][j]) /
                    kPairTemperature;
                const double probability =
                    sigmoid(candidate_logit);
                const double u =
                    (score_derivative[i] -
                     score_derivative[j]) /
                    kPairTemperature;
                gradient +=
                    weight *
                    (probability - target) * u;
                diagonal +=
                    weight * probability *
                    (1.0 - probability) * u * u;
            }
        }
    }
    StageDerivative result =
        coordinate_step(gradient, diagonal);
    result.gradient = positive_zero(gradient);
    result.diagonal = positive_zero(diagonal);
    return result;
}

FitReport fit_prepared(
    const aq19::Dataset& dataset,
    const PreparedColumns& prepared) {
    FitReport report{
        .eligible_coordinates =
            prepared.eligible_coordinates,
        .representative_coordinates =
            prepared.representatives.size(),
        .c16_metrics = aq19::evaluate(
            dataset,
            positive_zero_residuals(dataset)),
    };
    if (prepared.representatives.size() <
        kTermCount) {
        report.failure =
            "fewer than sixteen sign-deduplicated "
            "representatives";
        return report;
    }

    std::vector<bool> selected(
        prepared.representatives.size(), false);
    Terms terms;
    terms.reserve(kTermCount);
    for (std::size_t stage = 0;
         stage < kTermCount; ++stage) {
        const ForwardRows current =
            forward_rows(dataset, prepared, terms);
        std::optional<std::size_t> best;
        StageDerivative best_derivative;
        testing::StageCandidate best_candidate;
        for (std::size_t coordinate = 0;
             coordinate <
                 prepared.representatives.size();
             ++coordinate) {
            if (selected[coordinate]) {
                continue;
            }
            const StageDerivative derivative =
                derivative_for_column(
                    dataset, current,
                    prepared
                        .representatives[coordinate]);
            if (!derivative.valid) {
                continue;
            }
            const auto& spec =
                prepared
                    .representatives[coordinate]
                    .spec;
            const testing::StageCandidate candidate{
                .state_feature = spec.state_feature,
                .action_feature = spec.action_feature,
                .derivative = derivative,
            };
            if (!best ||
                stage_candidate_better(
                    candidate, best_candidate)) {
                best = coordinate;
                best_derivative = derivative;
                best_candidate = candidate;
            }
        }
        if (!best) {
            report.failure =
                "no positive finite stage gain";
            report.terms = std::move(terms);
            report.completed_stages = stage;
            return report;
        }
        selected[*best] = true;
        const testing::ColumnSpec& spec =
            prepared.representatives[*best].spec;
        Term term{
            .state_feature = spec.state_feature,
            .action_feature = spec.action_feature,
            .sigma = positive_zero(spec.sigma),
            .beta =
                positive_zero(best_derivative.beta),
        };
        terms.push_back(term);
        report.selected[stage] = {
            .term = term,
            .root_support = spec.root_support,
            .group_support = spec.group_support,
            .maximum_group_leverage =
                spec.maximum_group_leverage,
            .derivative = best_derivative,
        };
        report.completed_stages = stage + 1;
    }
    report.terms = terms;
    report.completed = true;
    const ForwardRows final =
        forward_rows(dataset, prepared, report.terms);
    report.residuals = final.residuals;
    report.candidate_metrics =
        aq19::evaluate(dataset, report.residuals);
    report.maximum_absolute_centered_logit =
        final.maximum_absolute_centered_logit;
    report.saturated_roots = final.saturated_roots;
    report.saturated_root_fraction =
        static_cast<double>(final.saturated_roots) /
        static_cast<double>(dataset.roots.size());
    const LearnedPrioritySparseCross runtime(
        report.terms);
    report.term_sha256 =
        artifact_integrity::sha256_string(
            learned_priority_sparse_cross_canonical_bytes(
                report.terms));

    // The final analytic fit must replay bit-for-bit through the immutable
    // runtime object that deployment uses.
    for (std::size_t root_index = 0;
         root_index < dataset.roots.size();
         ++root_index) {
        const aq19::Root& root =
            dataset.roots[root_index];
        std::vector<
            LearnedPrioritySparseCrossAction>
            action_features;
        action_features.reserve(root.options.size());
        for (const aq19::Option& option :
             root.options) {
            action_features.push_back(
                option.action_features);
        }
        const auto replay = runtime.residuals(
            root.state, action_features,
            aq19::canonical_option_order(root));
        if (replay.size() !=
            report.residuals[root_index].size()) {
            throw std::runtime_error(
                "AQ20 sparse-cross runtime replay "
                "shape drifted");
        }
        for (std::size_t action = 0;
             action < replay.size(); ++action) {
            if (!bit_equal(
                    replay[action],
                    report.residuals[root_index][action])) {
                throw std::runtime_error(
                    "AQ20 sparse-cross runtime replay "
                    "drifted at root " +
                    std::to_string(root_index) +
                    " action " +
                    std::to_string(action) +
                    " analytic=" +
                    std::to_string(
                        report
                            .residuals[root_index][action]) +
                    " analytic_bits=" +
                    std::to_string(
                        std::bit_cast<std::uint64_t>(
                            report.residuals[
                                root_index][action])) +
                    " runtime=" +
                    std::to_string(replay[action]) +
                    " runtime_bits=" +
                    std::to_string(
                        std::bit_cast<std::uint64_t>(
                            replay[action])));
            }
        }
    }
    return report;
}

std::size_t exact_max_row(
    const aq19::Root& root,
    std::span<const double> residuals) {
    if (residuals.size() != root.options.size()) {
        throw std::invalid_argument(
            "AQ20 sparse-cross exact-max shape drifted");
    }
    const auto order =
        aq19::canonical_option_order(root);
    std::size_t selected = order.front();
    double maximum =
        root.options[selected].base_aggregate_score +
        residuals[selected];
    for (const std::size_t row : order) {
        const double score =
            root.options[row].base_aggregate_score +
            residuals[row];
        if (score > maximum) {
            maximum = score;
            selected = row;
        }
    }
    return selected;
}

std::size_t changed_exact_max(
    const aq19::Dataset& dataset,
    const std::vector<std::vector<double>>& first,
    const std::vector<std::vector<double>>& second) {
    if (first.size() != dataset.roots.size() ||
        second.size() != dataset.roots.size()) {
        throw std::invalid_argument(
            "AQ20 sparse-cross exact-max root shape "
            "drifted");
    }
    std::size_t changed = 0;
    for (std::size_t root = 0;
         root < dataset.roots.size(); ++root) {
        changed +=
            exact_max_row(
                dataset.roots[root],
                first[root]) !=
                    exact_max_row(
                        dataset.roots[root],
                        second[root])
                ? 1U
                : 0U;
    }
    return changed;
}

double pair_bce(const Metrics& metrics) {
    return metrics.pairs.equal_deck_pair_bce;
}

double listwise(const Metrics& metrics) {
    return metrics.ranking
        .equal_deck_listwise_cross_entropy;
}

double regret(const Metrics& metrics) {
    return metrics.ranking.equal_deck_mean_regret;
}

double top_one(const Metrics& metrics) {
    return metrics.ranking
        .equal_deck_top_one_expected_agreement;
}

double stable_pair(const Metrics& metrics) {
    return metrics.ranking
        .equal_deck_stable_pair_agreement;
}

bool aggregate_metrics_bit_equal(
    const Metrics& metrics,
    double expected_pair_bce,
    double expected_listwise,
    double expected_regret,
    double expected_top_one,
    double expected_stable_pair) {
    return
        bit_equal(
            pair_bce(metrics),
            expected_pair_bce) &&
        bit_equal(
            listwise(metrics),
            expected_listwise) &&
        bit_equal(
            regret(metrics),
            expected_regret) &&
        bit_equal(
            top_one(metrics),
            expected_top_one) &&
        bit_equal(
            stable_pair(metrics),
            expected_stable_pair);
}

bool oof_deck_regrets_bit_equal(
    const Metrics& c16,
    const Metrics& aq19_metrics) {
    constexpr std::array<double, kDeckCount>
        c16_expected{
            0.013165204668703483,
            0.026226565361018767,
            0.013099042185603772,
            0.0071829827867596731,
            0.0325095172592249,
        };
    constexpr std::array<double, kDeckCount>
        aq19_expected{
            0.013140131126654879,
            0.024711634806538169,
            0.013099042185603772,
            0.0071829827867596731,
            0.0325095172592249,
        };
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        if (!bit_equal(
                c16.ranking.decks[deck].mean_regret,
                c16_expected[deck]) ||
            !bit_equal(
                aq19_metrics.ranking.decks[deck]
                    .mean_regret,
                aq19_expected[deck])) {
            return false;
        }
    }
    return true;
}

void require_gate(
    bool condition, std::string message,
    Gate& gate) {
    if (!condition) {
        gate.failures.push_back(std::move(message));
    }
}

std::string stage_name(EvidenceStage stage) {
    switch (stage) {
    case EvidenceStage::OofRejected:
        return "oof";
    case EvidenceStage::DevRejected:
        return "dev";
    case EvidenceStage::CounterPending:
        return "counter-pending";
    case EvidenceStage::CounterRejected:
        return "counter";
    case EvidenceStage::SelectorsAuthorized:
        return "selectors-authorized";
    }
    throw std::logic_error(
        "AQ20 sparse-cross evidence stage is invalid");
}

void print_metrics(
    std::ostream& output, std::string_view label,
    const Metrics& c16, const Metrics& aq19_metrics,
    const Metrics& candidate) {
    output
        << label
        << " pair_bce C16=" << pair_bce(c16)
        << " AQ19=" << pair_bce(aq19_metrics)
        << " AQ20=" << pair_bce(candidate)
        << " listwise C16=" << listwise(c16)
        << " AQ19=" << listwise(aq19_metrics)
        << " AQ20=" << listwise(candidate)
        << " regret C16=" << regret(c16)
        << " AQ19=" << regret(aq19_metrics)
        << " AQ20=" << regret(candidate)
        << " top1 C16=" << top_one(c16)
        << " AQ19=" << top_one(aq19_metrics)
        << " AQ20=" << top_one(candidate)
        << " stable C16=" << stable_pair(c16)
        << " AQ19=" << stable_pair(aq19_metrics)
        << " AQ20=" << stable_pair(candidate)
        << '\n';
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const auto& c16_deck =
            c16.ranking.decks[deck];
        const auto& aq19_deck =
            aq19_metrics.ranking.decks[deck];
        const auto& candidate_deck =
            candidate.ranking.decks[deck];
        output
            << "  "
            << deck_name(static_cast<DeckId>(deck))
            << " regret C16="
            << c16_deck.mean_regret
            << " AQ19=" << aq19_deck.mean_regret
            << " AQ20="
            << candidate_deck.mean_regret
            << " pair_bce C16="
            << c16.pairs.decks[deck].pair_bce
            << " AQ19="
            << aq19_metrics.pairs.decks[deck].pair_bce
            << " AQ20="
            << candidate.pairs.decks[deck].pair_bce
            << '\n';
    }
}

} // namespace

bool parse_command(
    std::span<const std::string_view> arguments) {
    return arguments.size() == 1 &&
           arguments.front() == "--offline-report";
}

void print_usage(std::ostream& output) {
    output
        << "Usage: old-school-decision-density-sparse-cross "
           "--offline-report\n";
}

bool exact_support_identity(
    const support::CensusReport& report) {
    if (report.fold_manifest !=
            aq19::kRequiredFoldManifest ||
        report.cross_partition_digest !=
            kRequiredSupportDigest ||
        !report.every_partition_has_16_eligible ||
        report.teacher_fields_read != 0 ||
        report.candidate_scores != 0 ||
        report.selected_terms != 0 ||
        report.optimizer_steps != 0 ||
        report.model_created != 0 ||
        report.tactical_seed_opened ||
        report.selector_seed_opened ||
        report.gameplay_games != 0) {
        return false;
    }
    for (std::size_t partition = 0;
         partition < support::kPartitionCount;
         ++partition) {
        const auto& value =
            report.partitions[partition];
        if (value.active_coordinates !=
                kRequiredActiveCoordinates[partition] ||
            value.eligible_coordinates !=
                kRequiredEligibleCoordinates[partition] ||
            value.canonical_table_sha256 !=
                kRequiredSupportTableSha256[partition]) {
            return false;
        }
    }
    return true;
}

StageDerivative coordinate_step(
    double gradient, double diagonal) {
    StageDerivative result{
        .gradient = positive_zero(gradient),
        .diagonal = positive_zero(diagonal),
    };
    if (!std::isfinite(gradient) ||
        !std::isfinite(diagonal) ||
        diagonal <= 0.0) {
        return result;
    }
    const double proposal =
        -kStepFraction * gradient / diagonal;
    if (!std::isfinite(proposal)) {
        return result;
    }
    const double beta = std::clamp(
        proposal,
        -kMaximumAbsoluteBeta,
        kMaximumAbsoluteBeta);
    const double improvement =
        -(gradient * beta +
          0.5 * diagonal * beta * beta);
    result.beta = positive_zero(beta);
    result.actual_improvement =
        positive_zero(improvement);
    result.clipped = beta != proposal;
    result.valid =
        std::isfinite(improvement) &&
        improvement > 0.0;
    return result;
}

FitReport fit(
    const aq19::Dataset& dataset,
    const support::PartitionReport& support_report) {
    // Every full/fold fit, including each repeat, rederives its own
    // label-blind eligibility/energy table before any objective or teacher
    // field is read.
    const support::PartitionReport recomputed =
        support::census_partition(
            support_report.name, dataset);
    if (recomputed != support_report) {
        throw std::invalid_argument(
            "AQ20 sparse-cross fold-local S0 table "
            "failed independent reproduction");
    }
    return fit_prepared(
        dataset,
        prepare_columns(dataset, recomputed));
}

GroupedOofReport evaluate_grouped_oof(
    const aq19::Dataset& train,
    const aq19::FoldAssignment& assignment,
    const support::CensusReport& support_report,
    const aq19::FoldReport& aq19_oof) {
    aq19::validate_dataset(train);
    if (assignment.manifest !=
            aq19::kRequiredFoldManifest ||
        support_report.fold_manifest !=
            assignment.manifest ||
        support_report.partitions.size() !=
            support::kPartitionCount ||
        aq19_oof.candidate_residuals.size() !=
            train.roots.size()) {
        throw std::invalid_argument(
            "AQ20 sparse-cross OOF identity drifted");
    }
    GroupedOofReport report{
        .candidate_residuals =
            std::vector<std::vector<double>>(
                train.roots.size()),
        .c16 = aq19_oof.parent,
        .aq19 = aq19_oof.candidate,
    };
    std::vector<std::size_t> predictions(
        train.roots.size(), 0);
    report.physical_groups_disjoint = true;
    std::map<std::string, std::size_t> groups;
    for (const aq19::Root& root : train.roots) {
        const std::size_t fold =
            fold_for_group(
                assignment,
                root.physical_game_group);
        const auto [position, inserted] =
            groups.emplace(
                root.physical_game_group, fold);
        if (!inserted && position->second != fold) {
            report.physical_groups_disjoint = false;
        }
    }

    bool repeat_fits = true;
    bool repeat_scores = true;
    for (std::size_t fold = 0;
         fold < aq19::kFoldCount; ++fold) {
        const aq19::Dataset fit_dataset =
            aq19::fold_training_dataset(
                train, assignment, fold);
        const aq19::Dataset holdout =
            aq19::fold_holdout_dataset(
                train, assignment, fold);
        report.fits[fold] = fit(
            fit_dataset,
            support_report.partitions[fold + 1]);
        report.repeated_fits[fold] = fit(
            fit_dataset,
            support_report.partitions[fold + 1]);
        repeat_fits =
            repeat_fits &&
            fit_bit_identical(
                report.fits[fold],
                report.repeated_fits[fold]);
        if (!report.fits[fold].completed ||
            !report.repeated_fits[fold].completed) {
            continue;
        }
        const auto candidate =
            runtime_residuals(
                holdout,
                report.fits[fold].terms);
        const auto repeated =
            runtime_residuals(
                holdout,
                report.repeated_fits[fold].terms);
        if (candidate.size() != repeated.size()) {
            repeat_scores = false;
        }
        for (std::size_t root = 0;
             root < std::min(
                 candidate.size(),
                 repeated.size());
             ++root) {
            if (candidate[root].size() !=
                repeated[root].size()) {
                repeat_scores = false;
                continue;
            }
            for (std::size_t action = 0;
                 action < candidate[root].size();
                 ++action) {
                repeat_scores =
                    repeat_scores &&
                    bit_equal(
                        candidate[root][action],
                        repeated[root][action]);
            }
        }
        report.c16_holdout[fold] =
            aq19::evaluate(
                holdout,
                positive_zero_residuals(holdout));
        report.aq19_holdout[fold] =
            aq19::evaluate(
                holdout,
                aq19_oof.fits[fold].parameters);
        report.candidate_holdout[fold] =
            aq19::evaluate(holdout, candidate);

        std::size_t local_root = 0;
        for (std::size_t root = 0;
             root < train.roots.size(); ++root) {
            if (fold_for_group(
                    assignment,
                    train.roots[root]
                        .physical_game_group) != fold) {
                continue;
            }
            if (local_root >= candidate.size()) {
                throw std::runtime_error(
                    "AQ20 sparse-cross holdout order "
                    "drifted");
            }
            report.candidate_residuals[root] =
                candidate[local_root++];
            ++predictions[root];
        }
        if (local_root != candidate.size()) {
            throw std::runtime_error(
                "AQ20 sparse-cross holdout root count "
                "drifted");
        }
    }
    report.every_root_predicted_once =
        std::all_of(
            predictions.begin(), predictions.end(),
            [](std::size_t count) {
                return count == 1;
            });
    report.repeated_fits_bit_identical =
        repeat_fits;
    report.repeated_scores_bit_identical =
        repeat_scores;
    if (report.every_root_predicted_once) {
        report.candidate = aq19::evaluate(
            train, report.candidate_residuals);
    }
    return report;
}

Gate evaluate_oof_gate(const OofGateInputs& inputs) {
    Gate gate;
    require_gate(
        pair_bce(inputs.candidate_train) <
            pair_bce(inputs.aq19_train),
        "TRAIN pair BCE did not improve on AQ19",
        gate);
    require_gate(
        regret(inputs.candidate_train) <
            regret(inputs.aq19_train),
        "TRAIN regret did not improve on AQ19",
        gate);
    require_gate(
        listwise(inputs.candidate_train) <=
            listwise(inputs.aq19_train),
        "TRAIN listwise CE increased from AQ19",
        gate);
    require_gate(
        top_one(inputs.candidate_train) >=
            top_one(inputs.aq19_train),
        "TRAIN top-one decreased from AQ19",
        gate);
    require_gate(
        stable_pair(inputs.candidate_train) >=
            stable_pair(inputs.aq19_train),
        "TRAIN stable-pair decreased from AQ19",
        gate);

    require_gate(
        pair_bce(inputs.aq19_oof) -
                pair_bce(inputs.candidate_oof) >=
            kOofPairBceImprovement,
        "OOF pair BCE improvement is below 0.000025",
        gate);
    require_gate(
        regret(inputs.aq19_oof) -
                regret(inputs.candidate_oof) >=
            kOofRegretImprovement,
        "OOF regret improvement is below 0.0005",
        gate);
    require_gate(
        listwise(inputs.candidate_oof) <=
            std::min(
                listwise(inputs.c16_oof),
                listwise(inputs.aq19_oof)),
        "OOF listwise CE exceeds a comparator",
        gate);
    require_gate(
        top_one(inputs.candidate_oof) >=
            std::max(
                top_one(inputs.c16_oof),
                top_one(inputs.aq19_oof)),
        "OOF top-one is below a comparator",
        gate);
    require_gate(
        stable_pair(inputs.candidate_oof) >=
            std::max(
                stable_pair(inputs.c16_oof),
                stable_pair(inputs.aq19_oof)),
        "OOF stable-pair is below a comparator",
        gate);

    std::size_t strict_folds = 0;
    for (std::size_t fold = 0;
         fold < aq19::kFoldCount; ++fold) {
        const double aq19_regret =
            regret(inputs.aq19_folds[fold]);
        const double candidate_regret =
            regret(inputs.candidate_folds[fold]);
        require_gate(
            candidate_regret <= aq19_regret,
            "OOF held fold " +
                std::to_string(fold) +
                " regret increased from AQ19",
            gate);
        strict_folds +=
            candidate_regret < aq19_regret ? 1U : 0U;
    }
    require_gate(
        strict_folds >= 3,
        "OOF regret did not strictly improve in three "
        "folds",
        gate);

    std::size_t strict_decks = 0;
    std::size_t strict_focus_decks = 0;
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const double comparator =
            std::min(
                inputs.c16_oof.ranking.decks[deck]
                    .mean_regret,
                inputs.aq19_oof.ranking.decks[deck]
                    .mean_regret);
        const double candidate =
            inputs.candidate_oof.ranking.decks[deck]
                .mean_regret;
        require_gate(
            candidate <= comparator,
            std::string("OOF ") +
                std::string(
                    deck_name(
                        static_cast<DeckId>(deck))) +
                " regret exceeds its better comparator",
            gate);
        if (candidate < comparator) {
            ++strict_decks;
            if (deck ==
                    static_cast<std::size_t>(
                        DeckId::Blue) ||
                deck ==
                    static_cast<std::size_t>(
                        DeckId::White) ||
                deck ==
                    static_cast<std::size_t>(
                        DeckId::RUAggro)) {
                ++strict_focus_decks;
            }
        }
    }
    require_gate(
        strict_decks >= 4,
        "OOF regret did not strictly improve on four "
        "decks",
        gate);
    require_gate(
        strict_focus_decks >= 2,
        "OOF regret did not strictly improve on two of "
        "Blue/White/RU",
        gate);
    require_gate(
        inputs.changed_exact_max_roots >= 6,
        "AQ20 changed fewer than six exact-max roots "
        "from AQ19",
        gate);
    require_gate(
        inputs.frozen_inputs_exact,
        "frozen cache/corpus/fold inputs drifted",
        gate);
    require_gate(
        inputs.support_identity_exact,
        "S0 support identity drifted",
        gate);
    require_gate(
        inputs.comparator_reproduced,
        "AQ19 comparator did not reproduce",
        gate);
    require_gate(
        inputs.full_fit_complete,
        "full TRAIN fit did not complete exactly "
        "sixteen stages",
        gate);
    require_gate(
        inputs.fold_local_preparation,
        "fold-local column preparation failed",
        gate);
    require_gate(
        inputs.grouped_oof_exact,
        "grouped OOF coverage/isolation failed",
        gate);
    require_gate(
        inputs.repeated_fits_bit_identical,
        "repeated full/fold fits were not bit-identical",
        gate);
    gate.passed = gate.failures.empty();
    return gate;
}

Gate evaluate_dev_gate(const DevGateInputs& inputs) {
    Gate gate;
    require_gate(
        pair_bce(inputs.candidate) <=
            std::min(
                pair_bce(inputs.c16),
                pair_bce(inputs.aq19)),
        "DEV pair BCE exceeds its better comparator",
        gate);
    require_gate(
        listwise(inputs.candidate) <=
            std::min(
                listwise(inputs.c16),
                listwise(inputs.aq19)),
        "DEV listwise CE exceeds its better comparator",
        gate);
    require_gate(
        regret(inputs.candidate) <=
            std::min(
                regret(inputs.c16),
                regret(inputs.aq19)),
        "DEV regret exceeds its better comparator",
        gate);
    require_gate(
        top_one(inputs.candidate) >=
            std::max(
                top_one(inputs.c16),
                top_one(inputs.aq19)),
        "DEV top-one is below its better comparator",
        gate);
    require_gate(
        stable_pair(inputs.candidate) >=
            std::max(
                stable_pair(inputs.c16),
                stable_pair(inputs.aq19)),
        "DEV stable-pair is below its better comparator",
        gate);
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        require_gate(
            inputs.candidate.ranking.decks[deck]
                    .mean_regret <=
                std::min(
                    inputs.c16.ranking.decks[deck]
                        .mean_regret,
                    inputs.aq19.ranking.decks[deck]
                        .mean_regret),
            std::string("DEV ") +
                std::string(
                    deck_name(
                        static_cast<DeckId>(deck))) +
                " regret exceeds its better comparator",
            gate);
    }
    gate.passed = gate.failures.empty();
    return gate;
}

ConditionalPath authorize_path(
    const Gate& oof_gate,
    const std::optional<Gate>& dev_gate,
    std::optional<bool> counter_gate_passed) {
    ConditionalPath result;
    if (!oof_gate.passed) {
        result.stage = EvidenceStage::OofRejected;
        return result;
    }
    if (!dev_gate || !dev_gate->passed) {
        result.stage = EvidenceStage::DevRejected;
        result.dev_candidate_opened =
            dev_gate.has_value();
        return result;
    }
    result.dev_candidate_opened = true;
    if (!counter_gate_passed) {
        result.stage = EvidenceStage::CounterPending;
        return result;
    }
    result.counter_gate_opened = true;
    if (!*counter_gate_passed) {
        result.stage = EvidenceStage::CounterRejected;
        return result;
    }
    result.stage = EvidenceStage::SelectorsAuthorized;
    // Authorization deliberately does not open either gameplay seed. A
    // separate engine-integrated runner must do that sequentially.
    return result;
}

RunReport run_offline() {
    RunReport report{
        .cache_path =
            std::filesystem::path(
                aq19::labels::kProductionCachePath),
    };
    const auto cache_before =
        artifact_integrity::snapshot_regular_file(
            report.cache_path);
    const auto parent_before =
        artifact_integrity::snapshot_regular_file(
            std::filesystem::path(
                aq19::labels::kParentArtifactPath));
    report.cache_bytes = cache_before.byte_size;
    report.cache_sha256 = cache_before.sha256;
    report.cache_identity_exact =
        cache_before.byte_size ==
            aq19::kRequiredCacheBytes &&
        cache_before.sha256 ==
            aq19::kRequiredCacheSha256;
    if (!report.cache_identity_exact) {
        throw std::runtime_error(
            "AQ20 sparse-cross cache identity drifted");
    }
    if (parent_before.byte_size !=
            aq19::labels::kParentArtifactBytes ||
        parent_before.sha256 !=
            aq19::labels::kParentArtifactSha256) {
        throw std::runtime_error(
            "AQ20 sparse-cross parent artifact "
            "identity drifted");
    }

    const aq19::labels::Corpus source =
        aq19::labels::load_cache(report.cache_path);
    const aq19::Corpus corpus =
        aq19::project_corpus(source);
    report.corpus_digest = corpus.source_digest;
    report.parent_fingerprint =
        corpus.parent_fingerprint;
    report.corpus_identity_exact =
        corpus.source_digest ==
            aq19::kRequiredCorpusDigest &&
        corpus.parent_fingerprint ==
            aq19::kRequiredParentFingerprint &&
        corpus.train.roots.size() == 300 &&
        corpus.dev.roots.size() == 150 &&
        std::all_of(
            corpus.train.roots_by_cell.begin(),
            corpus.train.roots_by_cell.end(),
            [](std::size_t count) {
                return count == 20;
            }) &&
        std::all_of(
            corpus.dev.roots_by_cell.begin(),
            corpus.dev.roots_by_cell.end(),
            [](std::size_t count) {
                return count == 10;
            });
    if (!report.corpus_identity_exact) {
        throw std::runtime_error(
            "AQ20 sparse-cross corpus identity drifted");
    }

    const aq19::FoldAssignment folds =
        aq19::assign_grouped_folds(
            corpus.train, corpus.source_digest);
    report.fold_manifest = folds.manifest;
    report.fold_manifest_exact =
        folds.manifest ==
        aq19::kRequiredFoldManifest;
    if (!report.fold_manifest_exact) {
        throw std::runtime_error(
            "AQ20 sparse-cross fold manifest drifted");
    }
    const support::CensusReport support_report =
        support::census(corpus.train, folds);
    report.support_digest =
        support_report.cross_partition_digest;
    report.support_identity_exact =
        exact_support_identity(support_report);
    if (!report.support_identity_exact) {
        throw std::runtime_error(
            "AQ20 sparse-cross S0 report drifted");
    }

    const aq19::FoldReport aq19_oof =
        aq19::evaluate_grouped_oof(
            corpus.train, folds);
    const aq19::OptimizerReport aq19_full =
        aq19::optimize(corpus.train);
    report.comparator_reproduced =
        aq19_full.parameter_sha256 ==
            kRequiredAq19ParameterSha256 &&
        aq19_oof.physical_groups_disjoint &&
        aq19_oof.every_root_predicted_once &&
        aq19_oof.repeated_fits_bit_identical &&
        aq19_oof.repeated_scores_bit_identical;

    const auto c16_train_residuals =
        positive_zero_residuals(corpus.train);
    report.c16_train =
        aq19::evaluate(
            corpus.train, c16_train_residuals);
    report.aq19_train =
        aq19::evaluate(
            corpus.train, aq19_full.parameters);
    report.c16_oof = aq19_oof.parent;
    report.aq19_oof = aq19_oof.candidate;
    report.comparator_reproduced =
        report.comparator_reproduced &&
        aq19_full.before == report.c16_train &&
        aq19_full.after == report.aq19_train &&
        aq19_oof.parent == report.c16_oof &&
        aq19_oof.candidate == report.aq19_oof &&
        aggregate_metrics_bit_equal(
            report.c16_train,
            0.4774595980542512,
            1.1659151286438101,
            0.018436662452262116,
            0.67333333333333334,
            0.88637241175128145) &&
        aggregate_metrics_bit_equal(
            report.aq19_train,
            0.47741713626667204,
            1.1658761235415642,
            0.018108270406560753,
            0.68000000000000005,
            0.88726526889413859) &&
        aggregate_metrics_bit_equal(
            report.c16_oof,
            0.4774595980542512,
            1.1659151286438101,
            0.018436662452262116,
            0.67333333333333334,
            0.88637241175128145) &&
        aggregate_metrics_bit_equal(
            report.aq19_oof,
            0.47745340221887206,
            1.1658959361331662,
            0.018128661632956275,
            0.68000000000000005,
            0.88943918193761673) &&
        oof_deck_regrets_bit_equal(
            report.c16_oof,
            report.aq19_oof);

    report.full_fit = fit(
        corpus.train, support_report.partitions[0]);
    report.repeated_full_fit = fit(
        corpus.train, support_report.partitions[0]);
    report.oof = evaluate_grouped_oof(
        corpus.train, folds, support_report,
        aq19_oof);
    if (report.oof.every_root_predicted_once) {
        report.changed_exact_max_roots =
            changed_exact_max(
                corpus.train,
                aq19_oof.candidate_residuals,
                report.oof.candidate_residuals);
    }

    bool fold_local_preparation = true;
    for (std::size_t fold = 0;
         fold < aq19::kFoldCount; ++fold) {
        fold_local_preparation =
            fold_local_preparation &&
            report.oof.fits[fold]
                    .eligible_coordinates ==
                kRequiredEligibleCoordinates[
                    fold + 1] &&
            report.oof.fits[fold].completed;
    }
    const bool repeated =
        fit_bit_identical(
            report.full_fit,
            report.repeated_full_fit) &&
        report.oof.repeated_fits_bit_identical &&
        report.oof.repeated_scores_bit_identical;
    const auto cache_after =
        artifact_integrity::snapshot_regular_file(
            report.cache_path);
    const auto parent_after =
        artifact_integrity::snapshot_regular_file(
            std::filesystem::path(
                aq19::labels::kParentArtifactPath));
    const bool frozen_inputs_exact =
        report.cache_identity_exact &&
        report.corpus_identity_exact &&
        report.fold_manifest_exact &&
        cache_before == cache_after &&
        parent_before == parent_after;
    report.oof_gate = evaluate_oof_gate({
        .c16_train = report.c16_train,
        .aq19_train = report.aq19_train,
        .candidate_train =
            report.full_fit.candidate_metrics,
        .c16_oof = report.c16_oof,
        .aq19_oof = report.aq19_oof,
        .candidate_oof = report.oof.candidate,
        .aq19_folds = report.oof.aq19_holdout,
        .candidate_folds =
            report.oof.candidate_holdout,
        .changed_exact_max_roots =
            report.changed_exact_max_roots,
        .frozen_inputs_exact =
            frozen_inputs_exact,
        .support_identity_exact =
            report.support_identity_exact,
        .comparator_reproduced =
            report.comparator_reproduced,
        .full_fit_complete =
            report.full_fit.completed &&
            report.full_fit.completed_stages ==
                kTermCount &&
            report.full_fit.terms.size() ==
                kTermCount,
        .fold_local_preparation =
            fold_local_preparation,
        .grouped_oof_exact =
            report.oof.physical_groups_disjoint &&
            report.oof.every_root_predicted_once,
        .repeated_fits_bit_identical = repeated,
    });

    // Comparator DEV fields are descriptive frozen baselines. Candidate DEV
    // values remain unopened unless the complete primary OOF gate passes.
    report.c16_dev =
        aq19::evaluate(
            corpus.dev,
            positive_zero_residuals(corpus.dev));
    report.aq19_dev =
        aq19::evaluate(
            corpus.dev, aq19_full.parameters);
    if (report.oof_gate.passed) {
        report.candidate_dev =
            aq19::evaluate(
                corpus.dev,
                runtime_residuals(
                    corpus.dev,
                    report.full_fit.terms));
        report.dev_gate = evaluate_dev_gate({
            .c16 = *report.c16_dev,
            .aq19 = *report.aq19_dev,
            .candidate =
                *report.candidate_dev,
        });
    }
    report.path = authorize_path(
        report.oof_gate, report.dev_gate,
        std::nullopt);
    // The research-only offline runner has no authority to open the frozen
    // counter fixtures or either sequential gameplay seed.
    report.tactical_seed_opened = false;
    report.selector_seed_opened = false;
    report.gameplay_games = 0;
    return report;
}

void print_report(
    std::ostream& output, const RunReport& report) {
    const auto flag =
        [](bool value) {
            return value ? "pass" : "fail";
        };
    output << std::setprecision(17)
           << "identifier=" << kIdentifier << '\n'
           << "schema=" << kSchema << '\n'
           << "fit_tag=" << kFitTag << '\n'
           << "cache_bytes=" << report.cache_bytes
           << " cache_sha256="
           << report.cache_sha256 << '\n'
           << "corpus_digest="
           << report.corpus_digest
           << " parent_fingerprint="
           << report.parent_fingerprint << '\n'
           << "fold_manifest="
           << report.fold_manifest
           << " support_digest="
           << report.support_digest << '\n'
           << "identities cache="
           << flag(report.cache_identity_exact)
           << " corpus="
           << flag(report.corpus_identity_exact)
           << " folds="
           << flag(report.fold_manifest_exact)
           << " support="
           << flag(report.support_identity_exact)
           << " aq19_comparator="
           << flag(report.comparator_reproduced)
           << '\n';
    if (report.full_fit.completed) {
        output
            << "full_fit_sha256="
            << report.full_fit.term_sha256
            << " eligible="
            << report.full_fit.eligible_coordinates
            << " representatives="
            << report.full_fit
                   .representative_coordinates
            << " maximum_abs_c="
            << report.full_fit
                   .maximum_absolute_centered_logit
            << " saturated_roots="
            << report.full_fit.saturated_roots
            << "/" << report.full_fit.residuals.size()
            << " saturated_fraction="
            << report.full_fit
                   .saturated_root_fraction
            << '\n';
        for (std::size_t stage = 0;
             stage < kTermCount; ++stage) {
            const SelectedTerm& selected =
                report.full_fit.selected[stage];
            output
                << "term[" << stage << "] p="
                << selected.term.state_feature
                << " q="
                << selected.term.action_feature
                << " sigma=" << selected.term.sigma
                << " beta=" << selected.term.beta
                << " roots="
                << selected.root_support
                << " groups="
                << selected.group_support
                << " max_group_leverage="
                << selected.maximum_group_leverage
                << " g="
                << selected.derivative.gradient
                << " h="
                << selected.derivative.diagonal
                << " actual_I="
                << selected.derivative
                       .actual_improvement
                << " clipped="
                << (selected.derivative.clipped
                        ? "yes"
                        : "no")
                << '\n';
        }
    } else {
        output
            << "full_fit_incomplete stages="
            << report.full_fit.completed_stages
            << " reason="
            << report.full_fit.failure << '\n';
    }
    print_metrics(
        output, "TRAIN",
        report.c16_train, report.aq19_train,
        report.full_fit.candidate_metrics);
    print_metrics(
        output, "OOF",
        report.c16_oof, report.aq19_oof,
        report.oof.candidate);
    for (std::size_t fold = 0;
         fold < aq19::kFoldCount; ++fold) {
        const FitReport& fit = report.oof.fits[fold];
        output
            << "OOF fold[" << fold << "]"
            << " fit_sha256="
            << (fit.term_sha256.empty()
                    ? "unavailable"
                    : fit.term_sha256)
            << " regret C16="
            << regret(report.oof.c16_holdout[fold])
            << " AQ19="
            << regret(report.oof.aq19_holdout[fold])
            << " AQ20="
            << regret(
                   report.oof.candidate_holdout[fold])
            << " terms=";
        for (std::size_t term = 0;
             term < fit.terms.size(); ++term) {
            if (term != 0) {
                output << ',';
            }
            output
                << fit.terms[term].state_feature
                << ':'
                << fit.terms[term].action_feature;
        }
        output << '\n';
    }
    output
        << "OOF invariants groups="
        << flag(report.oof.physical_groups_disjoint)
        << " coverage="
        << flag(report.oof.every_root_predicted_once)
        << " repeat_fits="
        << flag(report.oof.repeated_fits_bit_identical)
        << " repeat_scores="
        << flag(
               report.oof
                   .repeated_scores_bit_identical)
        << '\n';
    output
        << "changed_exact_max_roots="
        << report.changed_exact_max_roots
        << "/300\n"
        << (report.oof_gate.passed
                ? "PASS stage=oof"
                : "REJECT stage=oof")
        << '\n';
    for (const std::string& failure :
         report.oof_gate.failures) {
        output << "  - " << failure << '\n';
    }
    if (report.candidate_dev && report.c16_dev &&
        report.aq19_dev && report.dev_gate) {
        print_metrics(
            output, "DEV",
            *report.c16_dev, *report.aq19_dev,
            *report.candidate_dev);
        output
            << (report.dev_gate->passed
                    ? "PASS stage=dev"
                    : "REJECT stage=dev")
            << '\n';
        for (const std::string& failure :
             report.dev_gate->failures) {
            output << "  - " << failure << '\n';
        }
    } else {
        output
            << "DEV candidate=unopened"
            << " comparators="
            << (report.c16_dev && report.aq19_dev
                    ? "reproduced"
                    : "missing")
            << '\n';
    }
    output
        << "evidence_stage="
        << stage_name(report.path.stage)
        << " dev_candidate_opened="
        << (report.path.dev_candidate_opened
                ? "yes"
                : "no")
        << " counter_gate_opened="
        << (report.path.counter_gate_opened
                ? "yes"
                : "no")
        << " tactical_seed_opened="
        << (report.tactical_seed_opened
                ? "yes"
                : "no")
        << " selector_seed_opened="
        << (report.selector_seed_opened
                ? "yes"
                : "no")
        << " gameplay_games="
        << report.gameplay_games
        << '\n';
}

namespace testing {

PreparedColumnsProbe prepare_columns_probe(
    const aq19::Dataset& dataset,
    std::span<const ColumnSpec> columns) {
    const PreparedColumns prepared =
        prepare_columns(
            dataset,
            std::vector<ColumnSpec>(
                columns.begin(), columns.end()));
    PreparedColumnsProbe result{
        .input_coordinates =
            prepared.eligible_coordinates,
        .representatives =
            prepared.representatives.size(),
        .canonical_sha256 =
            prepared.canonical_sha256,
    };
    result.coordinates.reserve(
        prepared.representatives.size());
    for (const Column& column :
         prepared.representatives) {
        result.coordinates.emplace_back(
            column.spec.state_feature,
            column.spec.action_feature);
    }
    return result;
}

FitReport fit_with_columns(
    const aq19::Dataset& dataset,
    std::span<const ColumnSpec> columns) {
    return fit_prepared(
        dataset,
        prepare_columns(
            dataset,
            std::vector<ColumnSpec>(
                columns.begin(), columns.end())));
}

DerivativeProbe derivative_probe(
    const aq19::Dataset& dataset,
    std::span<const Term> current_terms,
    const ColumnSpec& candidate) {
    std::vector<ColumnSpec> specs;
    specs.reserve(current_terms.size() + 1);
    for (const Term& term : current_terms) {
        specs.push_back({
            .state_feature = term.state_feature,
            .action_feature = term.action_feature,
            .sigma = term.sigma,
        });
    }
    specs.push_back(candidate);
    const PreparedColumns prepared =
        prepare_columns(dataset, std::move(specs));
    const auto found = std::find_if(
        prepared.representatives.begin(),
        prepared.representatives.end(),
        [&candidate](const Column& column) {
            return column.spec.state_feature ==
                       candidate.state_feature &&
                   column.spec.action_feature ==
                       candidate.action_feature;
        });
    if (found == prepared.representatives.end()) {
        throw std::invalid_argument(
            "AQ20 sparse-cross candidate deduplicated "
            "against an existing term");
    }
    const ForwardRows forward =
        forward_rows(
            dataset, prepared, current_terms);
    const StageDerivative derivative =
        derivative_for_column(
            dataset, forward, *found);
    return {
        .objective =
            pair_objective(dataset, forward),
        .gradient = derivative.gradient,
        .diagonal = derivative.diagonal,
    };
}

double objective(
    const aq19::Dataset& dataset,
    std::span<const Term> terms) {
    std::vector<ColumnSpec> specs;
    specs.reserve(terms.size());
    for (const Term& term : terms) {
        specs.push_back({
            .state_feature = term.state_feature,
            .action_feature = term.action_feature,
            .sigma = term.sigma,
        });
    }
    if (specs.empty()) {
        // One unused finite feature column supplies the forward workspace;
        // the empty term span still produces the exact parent objective.
        specs.push_back({
            .state_feature = 0,
            .action_feature = 0,
            .sigma = 1.0,
        });
    }
    const PreparedColumns prepared =
        prepare_columns(dataset, std::move(specs));
    return pair_objective(
        dataset,
        forward_rows(dataset, prepared, terms));
}

std::optional<std::size_t> select_stage_candidate(
    std::span<const StageCandidate> candidates) {
    std::optional<std::size_t> best;
    for (std::size_t index = 0;
         index < candidates.size(); ++index) {
        if (!candidates[index].derivative.valid) {
            continue;
        }
        if (!best ||
            stage_candidate_better(
                candidates[index],
                candidates[*best])) {
            best = index;
        }
    }
    return best;
}

std::vector<std::vector<double>> residuals(
    const aq19::Dataset& dataset,
    std::span<const Term> terms) {
    return direct_residuals(dataset, terms);
}

} // namespace testing

} // namespace old_school::decision_density_sparse_cross
