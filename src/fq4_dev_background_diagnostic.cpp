#include "old_school/fq4_dev_background_diagnostic.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/fq4_dev_bundle.hpp"
#include "old_school/fq4_priority_math.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace old_school::fq4_dev_background_diagnostic {
namespace {

namespace bundle = fq4_dev_bundle;
namespace evaluator = fq4_dev_evaluator;
namespace integrity = artifact_integrity;
namespace math = fq4_priority_math;

constexpr std::array<std::string_view, bundle::kDeckCount>
    kDeckNames{
        "Green",
        "Red",
        "Blue",
        "White",
        "RU_Aggro",
    };

[[noreturn]] void fail(std::string_view message) {
    throw std::runtime_error(
        "FQ4 DEV2 background diagnostic: " +
        std::string(message));
}

constexpr StackDeckCensus stack_deck(
    std::size_t empty_roots,
    std::size_t empty_options,
    std::size_t active_roots,
    std::size_t active_options) {
    return {
        .empty = {
            .roots = empty_roots,
            .options = empty_options,
        },
        .active = {
            .roots = active_roots,
            .options = active_options,
        },
    };
}

constexpr StackRoleCensus background_role() {
    return {
        .decks = {
            stack_deck(1, 2, 0, 0),
            stack_deck(1, 2, 0, 0),
            stack_deck(1, 2, 0, 0),
            stack_deck(1, 2, 0, 0),
            stack_deck(1, 3, 0, 0),
        },
    };
}

constexpr StackCensus expected_stack_census() {
    return {
        .fit = {
            .positive = {
                .decks = {
                    stack_deck(11, 35, 0, 0),
                    stack_deck(4, 29, 0, 0),
                    stack_deck(20, 103, 11, 39),
                    stack_deck(6, 26, 7, 21),
                    stack_deck(29, 295, 0, 0),
                },
            },
            .background = background_role(),
        },
        .check = {
            .positive = {
                .decks = {
                    stack_deck(20, 54, 0, 0),
                    stack_deck(5, 33, 0, 0),
                    stack_deck(20, 107, 11, 33),
                    stack_deck(1, 4, 6, 18),
                    stack_deck(31, 322, 0, 0),
                },
            },
            .background = background_role(),
        },
        .selected_rows = kStackCensusSelectedRows,
        .selected_options = kStackCensusSelectedOptions,
        .action_invariant_rows = kStackCensusSelectedRows,
        .exact_stack_encoding_rows = kStackCensusSelectedRows,
        .role_overlap_rows = 0,
    };
}

inline constexpr StackCensus kExpectedStackCensus =
    expected_stack_census();

struct StackFeature {
    std::uint64_t value_bits = std::bit_cast<std::uint64_t>(0.0);
    std::size_t stack_size = 0;
};

StackFeature stack_feature(
    const bundle::ActionRow& action) {
    StackFeature result;
    bool found = false;
    for (const bundle::SparseFeature& feature :
         action.features) {
        if (feature.index != kStackSizeFeatureIndex) {
            continue;
        }
        if (found) {
            fail("stack-size feature is duplicated");
        }
        found = true;
        result.value_bits = feature.value_bits;
    }
    if (found && result.value_bits == 0) {
        fail(
            "positive-zero stack feature must be sparse-absent");
    }
    const double value =
        std::bit_cast<double>(result.value_bits);
    const double scaled =
        value *
        static_cast<double>(
            kStackSizeEncodingDenominator);
    double integer = 0.0;
    if (!std::isfinite(value) ||
        std::signbit(value) ||
        !std::isfinite(scaled) ||
        std::modf(scaled, &integer) != 0.0 ||
        integer >=
            static_cast<double>(
                std::numeric_limits<std::size_t>::max())) {
        fail("stack-size feature is not an exact nonnegative integer");
    }
    result.stack_size =
        static_cast<std::size_t>(integer);
    return result;
}

StackSplitCensus& split_census(
    StackCensus& census, bundle::Split split) {
    switch (split) {
    case bundle::Split::Fit:
        return census.fit;
    case bundle::Split::Check:
        return census.check;
    }
    fail("selected row has an invalid split");
}

void measure_stack_rows(
    StackCensus& result,
    const std::vector<bundle::SelectedRow>& rows,
    bundle::Split expected_split) {
    constexpr std::uint8_t kPositive =
        static_cast<std::uint8_t>(
            bundle::Role::DominancePositive);
    constexpr std::uint8_t kBackground =
        static_cast<std::uint8_t>(
            bundle::Role::BackgroundControl);
    constexpr std::uint8_t kKnownRoles =
        kPositive | kBackground;
    for (const bundle::SelectedRow& row : rows) {
        if (row.split != expected_split) {
            fail("selected row is in the wrong split");
        }
        if (row.census.owner_deck >=
            bundle::kDeckCount) {
            fail("selected row owner deck is out of range");
        }
        const bool positive =
            (row.roles & kPositive) != 0;
        const bool background =
            (row.roles & kBackground) != 0;
        if ((row.roles & ~kKnownRoles) != 0 ||
            positive == background) {
            fail(
                positive && background
                    ? "selected row has overlapping roles"
                    : "selected row has an invalid role");
        }
        if (row.actions.empty()) {
            fail("selected row has no actions");
        }

        const StackFeature first =
            stack_feature(row.actions.front());
        for (std::size_t action = 1;
             action < row.actions.size(); ++action) {
            const StackFeature current =
                stack_feature(row.actions[action]);
            if (current.value_bits !=
                    first.value_bits ||
                current.stack_size !=
                    first.stack_size) {
                fail(
                    "stack-size feature is not action invariant");
            }
        }

        StackSplitCensus& split =
            split_census(result, expected_split);
        StackRoleCensus& role =
            positive
                ? split.positive
                : split.background;
        StackDeckCensus& deck =
            role.decks[row.census.owner_deck];
        StackContextCount& context =
            first.stack_size == 0
                ? deck.empty
                : deck.active;
        ++context.roots;
        context.options += row.actions.size();
        ++result.selected_rows;
        result.selected_options +=
            row.actions.size();
        ++result.action_invariant_rows;
        ++result.exact_stack_encoding_rows;
    }
}

std::pair<std::size_t, std::size_t> role_totals(
    const StackRoleCensus& role) {
    std::size_t roots = 0;
    std::size_t options = 0;
    for (const StackDeckCensus& deck : role.decks) {
        roots +=
            deck.empty.roots +
            deck.active.roots;
        options +=
            deck.empty.options +
            deck.active.options;
    }
    return {roots, options};
}

bool self_consistent_stack_census(
    const StackCensus& census) {
    std::size_t rows = 0;
    std::size_t options = 0;
    for (const StackSplitCensus* split :
         {&census.fit, &census.check}) {
        for (const StackRoleCensus* role :
             {&split->positive, &split->background}) {
            const auto [role_rows, role_options] =
                role_totals(*role);
            rows += role_rows;
            options += role_options;
        }
    }
    return
        rows == census.selected_rows &&
        options == census.selected_options &&
        census.action_invariant_rows ==
            census.selected_rows &&
        census.exact_stack_encoding_rows ==
            census.selected_rows &&
        census.role_overlap_rows == 0;
}

bool background_control(const evaluator::PreparedRow& row) {
    return
        (row.roles &
         static_cast<std::uint8_t>(
             bundle::Role::BackgroundControl)) != 0;
}

std::vector<double> base_scores(
    const evaluator::PreparedRow& row) {
    std::vector<double> result;
    result.reserve(row.actions.size());
    for (const auto& action : row.actions) {
        result.push_back(action.base_score);
    }
    return result;
}

std::vector<double> behavior(
    const std::vector<double>& combined_scores) {
    return math::behavior_mixture(
        math::stable_softmax(
            combined_scores,
            evaluator::kPolicyTemperature),
        evaluator::kBehaviorPrimaryWeight);
}

double forward_kl(
    const std::vector<double>& parent,
    const std::vector<double>& candidate) {
    if (parent.empty() ||
        parent.size() != candidate.size()) {
        fail("KL distributions have inconsistent shapes");
    }
    long double parent_total = 0.0L;
    long double candidate_total = 0.0L;
    long double divergence = 0.0L;
    for (std::size_t index = 0;
         index < parent.size(); ++index) {
        if (!std::isfinite(parent[index]) ||
            !std::isfinite(candidate[index]) ||
            parent[index] <= 0.0 ||
            candidate[index] <= 0.0) {
            fail("KL distribution is not positive and finite");
        }
        parent_total += parent[index];
        candidate_total += candidate[index];
        divergence +=
            static_cast<long double>(parent[index]) *
            std::log(
                static_cast<long double>(parent[index]) /
                static_cast<long double>(candidate[index]));
    }
    if (std::abs(parent_total - 1.0L) > 1.0e-12L ||
        std::abs(candidate_total - 1.0L) > 1.0e-12L) {
        fail("KL distribution is not normalized");
    }
    const double result =
        static_cast<double>(divergence);
    if (!std::isfinite(result) ||
        result < -1.0e-15) {
        fail("KL result is invalid");
    }
    return result < 0.0 ? 0.0 : result;
}

double total_variation(
    const std::vector<double>& parent,
    const std::vector<double>& candidate) {
    if (parent.empty() ||
        parent.size() != candidate.size()) {
        fail("total-variation distributions have inconsistent shapes");
    }
    long double distance = 0.0L;
    for (std::size_t index = 0;
         index < parent.size(); ++index) {
        if (!std::isfinite(parent[index]) ||
            !std::isfinite(candidate[index]) ||
            parent[index] < 0.0 ||
            candidate[index] < 0.0) {
            fail("total-variation distribution is invalid");
        }
        distance += std::abs(
            static_cast<long double>(parent[index]) -
            static_cast<long double>(candidate[index]));
    }
    const double result =
        static_cast<double>(0.5L * distance);
    if (!std::isfinite(result) ||
        result < 0.0 || result > 1.0 + 1.0e-12) {
        fail("total-variation result is invalid");
    }
    return result > 1.0 ? 1.0 : result;
}

std::vector<std::size_t> exact_support(
    const std::vector<double>& scores) {
    if (scores.empty() ||
        !std::all_of(
            scores.begin(), scores.end(),
            [](double value) {
                return std::isfinite(value);
            })) {
        fail("exact support requires finite scores");
    }
    const double maximum =
        *std::max_element(
            scores.begin(), scores.end());
    std::vector<std::size_t> result;
    for (std::size_t index = 0;
         index < scores.size(); ++index) {
        if (scores[index] == maximum) {
            result.push_back(index);
        }
    }
    return result;
}

void add_row(
    DeckMetrics& metrics,
    const evaluator::PreparedRow& row,
    const std::vector<double>& parent_logits,
    const std::vector<double>& candidate_logits) {
    if (row.owner_deck >= bundle::kDeckCount ||
        parent_logits.size() != row.actions.size() ||
        candidate_logits.size() != row.actions.size()) {
        fail("background row or logits have an invalid shape");
    }
    const std::vector<double> base =
        base_scores(row);
    const auto parent_scores =
        math::centered_tanh_scores(
            base, parent_logits,
            evaluator::kResidualWeight);
    const auto candidate_scores =
        math::centered_tanh_scores(
            base, candidate_logits,
            evaluator::kResidualWeight);
    for (std::size_t action = 0;
         action < row.actions.size(); ++action) {
        if (std::bit_cast<std::uint64_t>(
                parent_scores.residuals[action]) !=
            std::bit_cast<std::uint64_t>(
                row.actions[action].parent_residual)) {
            fail("background parent residual anchor drifted");
        }
    }
    const std::vector<double> parent_behavior =
        behavior(parent_scores.combined_scores);
    const std::vector<double> candidate_behavior =
        behavior(candidate_scores.combined_scores);

    ++metrics.roots;
    metrics.options += row.actions.size();
    metrics.parent_to_candidate_kl +=
        forward_kl(
            parent_behavior,
            candidate_behavior);
    metrics.total_variation +=
        total_variation(
            parent_behavior,
            candidate_behavior);
    metrics.exact_support_changes +=
        exact_support(parent_scores.combined_scores) !=
                exact_support(candidate_scores.combined_scores)
            ? 1U
            : 0U;
    for (std::size_t action = 0;
         action < row.actions.size(); ++action) {
        metrics.maximum_combined_score_delta =
            std::max(
                metrics.maximum_combined_score_delta,
                std::abs(
                    candidate_scores.combined_scores[action] -
                    parent_scores.combined_scores[action]));
    }
}

SplitMetrics measure_split(
    const std::vector<evaluator::PreparedRow>& rows,
    const std::vector<std::vector<double>>& parent_logits,
    const std::vector<std::vector<double>>& candidate_logits) {
    if (rows.size() != parent_logits.size() ||
        rows.size() != candidate_logits.size()) {
        fail("background corpus/logit split shape drifted");
    }
    SplitMetrics result;
    for (std::size_t index = 0;
         index < rows.size(); ++index) {
        const auto& row = rows[index];
        if (!background_control(row)) {
            continue;
        }
        if (row.owner_deck >= bundle::kDeckCount) {
            fail("background row owner deck is out of range");
        }
        add_row(
            result.decks[row.owner_deck],
            row, parent_logits[index],
            candidate_logits[index]);
    }
    for (DeckMetrics& deck : result.decks) {
        if (deck.roots != 1 || deck.options < 2) {
            fail("background split is not one row per deck");
        }
        const double inverse_roots =
            1.0 /
            static_cast<double>(deck.roots);
        deck.parent_to_candidate_kl *=
            inverse_roots;
        deck.total_variation *= inverse_roots;
        result.roots += deck.roots;
        result.options += deck.options;
        result.deck_balanced_parent_to_candidate_kl +=
            deck.parent_to_candidate_kl;
        result.deck_balanced_total_variation +=
            deck.total_variation;
        result.exact_support_changes +=
            deck.exact_support_changes;
        result.maximum_combined_score_delta =
            std::max(
                result.maximum_combined_score_delta,
                deck.maximum_combined_score_delta);
    }
    constexpr double kInverseDecks =
        1.0 /
        static_cast<double>(bundle::kDeckCount);
    result.deck_balanced_parent_to_candidate_kl *=
        kInverseDecks;
    result.deck_balanced_total_variation *=
        kInverseDecks;
    return result;
}

bool finite_metrics(const DeckMetrics& metrics) {
    return
        std::isfinite(metrics.parent_to_candidate_kl) &&
        metrics.parent_to_candidate_kl >= 0.0 &&
        std::isfinite(metrics.total_variation) &&
        metrics.total_variation >= 0.0 &&
        metrics.total_variation <= 1.0 &&
        std::isfinite(
            metrics.maximum_combined_score_delta) &&
        metrics.maximum_combined_score_delta >= 0.0;
}

bool valid_split(const SplitMetrics& split) {
    std::size_t roots = 0;
    std::size_t options = 0;
    std::size_t support_changes = 0;
    double maximum_delta = 0.0;
    double deck_balanced_kl = 0.0;
    double deck_balanced_tv = 0.0;
    for (std::size_t deck_index = 0;
         deck_index < split.decks.size();
         ++deck_index) {
        const DeckMetrics& deck =
            split.decks[deck_index];
        if (deck.roots != 1 ||
            deck.options !=
                kBackgroundOptionsPerDeck[deck_index] ||
            deck.exact_support_changes > deck.roots ||
            !finite_metrics(deck)) {
            return false;
        }
        roots += deck.roots;
        options += deck.options;
        support_changes +=
            deck.exact_support_changes;
        deck_balanced_kl +=
            deck.parent_to_candidate_kl;
        deck_balanced_tv +=
            deck.total_variation;
        maximum_delta =
            std::max(
                maximum_delta,
                deck.maximum_combined_score_delta);
    }
    constexpr double kInverseDecks =
        1.0 /
        static_cast<double>(bundle::kDeckCount);
    deck_balanced_kl *= kInverseDecks;
    deck_balanced_tv *= kInverseDecks;
    return
        split.roots == bundle::kDeckCount &&
        roots == split.roots &&
        options == split.options &&
        support_changes ==
            split.exact_support_changes &&
        maximum_delta ==
            split.maximum_combined_score_delta &&
        deck_balanced_kl ==
            split.deck_balanced_parent_to_candidate_kl &&
        deck_balanced_tv ==
            split.deck_balanced_total_variation &&
        std::isfinite(
            split.deck_balanced_parent_to_candidate_kl) &&
        split.deck_balanced_parent_to_candidate_kl >= 0.0 &&
        std::isfinite(
            split.deck_balanced_total_variation) &&
        split.deck_balanced_total_variation >= 0.0 &&
            split.deck_balanced_total_variation <= 1.0;
}

double two_deck_two_split_mean(
    const Measurements& measurements,
    std::size_t first, std::size_t second) {
    return
        (measurements.fit.decks[first]
             .parent_to_candidate_kl +
         measurements.check.decks[first]
             .parent_to_candidate_kl +
         measurements.fit.decks[second]
             .parent_to_candidate_kl +
         measurements.check.decks[second]
             .parent_to_candidate_kl) /
        4.0;
}

bool derived_summary_valid(
    const Measurements& measurements) {
    constexpr std::size_t kGreen =
        static_cast<std::size_t>(DeckId::Green);
    constexpr std::size_t kBlue =
        static_cast<std::size_t>(DeckId::Blue);
    constexpr std::size_t kWhite =
        static_cast<std::size_t>(DeckId::White);
    constexpr std::size_t kRu =
        static_cast<std::size_t>(DeckId::RUAggro);
    const double green_white =
        two_deck_two_split_mean(
            measurements, kGreen, kWhite);
    const double blue_ru =
        two_deck_two_split_mean(
            measurements, kBlue, kRu);
    bool material_green_or_white = false;
    for (const SplitMetrics* split :
         {&measurements.fit, &measurements.check}) {
        for (const std::size_t deck :
             {kGreen, kWhite}) {
            const DeckMetrics& metrics =
                split->decks[deck];
            material_green_or_white =
                material_green_or_white ||
                metrics.exact_support_changes != 0 ||
                metrics.parent_to_candidate_kl >=
                    kMaterialKl;
        }
    }
    const bool exceeds = green_white > blue_ru;
    return
        valid_split(measurements.fit) &&
        valid_split(measurements.check) &&
        measurements.green_white_mean_kl ==
            green_white &&
        measurements.blue_ru_mean_kl ==
            blue_ru &&
        measurements.material_green_or_white ==
            material_green_or_white &&
        measurements.green_white_exceeds_blue_ru ==
            exceeds &&
        measurements.hypothesis_supported ==
            (material_green_or_white && exceeds);
}

bool exact_zero_control(
    const Measurements& measurements) {
    if (!derived_summary_valid(measurements) ||
        measurements.material_green_or_white ||
        measurements.green_white_exceeds_blue_ru ||
        measurements.hypothesis_supported) {
        return false;
    }
    for (const SplitMetrics* split :
         {&measurements.fit, &measurements.check}) {
        if (split->deck_balanced_parent_to_candidate_kl !=
                0.0 ||
            split->deck_balanced_total_variation !=
                0.0 ||
            split->exact_support_changes != 0 ||
            split->maximum_combined_score_delta !=
                0.0) {
            return false;
        }
        for (const DeckMetrics& deck : split->decks) {
            if (deck.parent_to_candidate_kl != 0.0 ||
                deck.total_variation != 0.0 ||
                deck.exact_support_changes != 0 ||
                deck.maximum_combined_score_delta !=
                    0.0) {
                return false;
            }
        }
    }
    return true;
}

void write_split(
    std::ostringstream& output,
    std::string_view name,
    const SplitMetrics& metrics) {
    for (std::size_t deck = 0;
         deck < metrics.decks.size(); ++deck) {
        const DeckMetrics& value =
            metrics.decks[deck];
        output
            << "background split=" << name
            << " deck=" << kDeckNames[deck]
            << " roots=" << value.roots
            << " options=" << value.options
            << " kl_parent_to_candidate="
            << value.parent_to_candidate_kl
            << " total_variation="
            << value.total_variation
            << " exact_support_changes="
            << value.exact_support_changes
            << " maximum_combined_score_delta="
            << value.maximum_combined_score_delta
            << '\n';
    }
    output
        << "background split=" << name
        << " aggregate=deck_balanced"
        << " roots=" << metrics.roots
        << " options=" << metrics.options
        << " kl_parent_to_candidate="
        << metrics
               .deck_balanced_parent_to_candidate_kl
        << " total_variation="
        << metrics.deck_balanced_total_variation
        << " exact_support_changes="
        << metrics.exact_support_changes
        << " maximum_combined_score_delta="
        << metrics.maximum_combined_score_delta
        << '\n';
}

void write_stack_role(
    std::ostringstream& output,
    std::string_view split_name,
    std::string_view role_name,
    const StackRoleCensus& role) {
    for (std::size_t deck_index = 0;
         deck_index < role.decks.size();
         ++deck_index) {
        const StackDeckCensus& deck =
            role.decks[deck_index];
        output
            << "stack_census split=" << split_name
            << " role=" << role_name
            << " deck=" << kDeckNames[deck_index]
            << " empty_roots=" << deck.empty.roots
            << " empty_options=" << deck.empty.options
            << " active_roots=" << deck.active.roots
            << " active_options=" << deck.active.options
            << '\n';
    }
    std::size_t empty_roots = 0;
    std::size_t empty_options = 0;
    std::size_t active_roots = 0;
    std::size_t active_options = 0;
    for (const StackDeckCensus& deck : role.decks) {
        empty_roots += deck.empty.roots;
        empty_options += deck.empty.options;
        active_roots += deck.active.roots;
        active_options += deck.active.options;
    }
    output
        << "stack_census split=" << split_name
        << " role=" << role_name
        << " aggregate=pooled"
        << " empty_roots=" << empty_roots
        << " empty_options=" << empty_options
        << " active_roots=" << active_roots
        << " active_options=" << active_options
        << '\n';
}

} // namespace

Measurements measure(
    const evaluator::PreparedCorpus& corpus,
    const evaluator::CorpusLogits& parent,
    const evaluator::CorpusLogits& candidate) {
    Measurements result{
        .fit = measure_split(
            corpus.fit,
            parent.fit, candidate.fit),
        .check = measure_split(
            corpus.check,
            parent.check, candidate.check),
    };
    constexpr std::size_t kGreen =
        static_cast<std::size_t>(DeckId::Green);
    constexpr std::size_t kBlue =
        static_cast<std::size_t>(DeckId::Blue);
    constexpr std::size_t kWhite =
        static_cast<std::size_t>(DeckId::White);
    constexpr std::size_t kRu =
        static_cast<std::size_t>(DeckId::RUAggro);
    result.green_white_mean_kl =
        two_deck_two_split_mean(
            result, kGreen, kWhite);
    result.blue_ru_mean_kl =
        two_deck_two_split_mean(
            result, kBlue, kRu);
    for (const SplitMetrics* split :
         {&result.fit, &result.check}) {
        for (const std::size_t deck :
             {kGreen, kWhite}) {
            const DeckMetrics& metrics =
                split->decks[deck];
            result.material_green_or_white =
                result.material_green_or_white ||
                metrics.exact_support_changes != 0 ||
                metrics.parent_to_candidate_kl >=
                    kMaterialKl;
        }
    }
    result.green_white_exceeds_blue_ru =
        result.green_white_mean_kl >
        result.blue_ru_mean_kl;
    result.hypothesis_supported =
        result.material_green_or_white &&
        result.green_white_exceeds_blue_ru;
    return result;
}

StackCensus measure_stack_census(
    const std::vector<bundle::SelectedRow>& fit,
    const std::vector<bundle::SelectedRow>& check) {
    StackCensus result;
    measure_stack_rows(
        result, fit, bundle::Split::Fit);
    measure_stack_rows(
        result, check, bundle::Split::Check);
    if (!self_consistent_stack_census(result)) {
        fail("stack census accounting is inconsistent");
    }
    return result;
}

StackCensusReport run_stack_census() {
    const integrity::RegularFileSnapshot before =
        integrity::snapshot_regular_file(
            std::string(bundle::kArtifactPath));
    const bundle::Bundle artifact =
        bundle::load_published();
    const StackCensus census =
        measure_stack_census(
            artifact.fit_rows,
            artifact.check_rows);
    return {
        .bundle_schema =
            std::string(bundle::kBundleSchema),
        .bundle_bytes =
            bundle::kPublishedArtifactBytes,
        .bundle_sha256 =
            std::string(
                bundle::kPublishedArtifactSha256),
        .feature_schema =
            artifact.manifest.feature_schema,
        .feature_count =
            artifact.manifest.feature_count,
        .feature_contract_sha256 =
            bundle::format_sha256(
                artifact.manifest
                    .feature_contract_sha256),
        .stack_size_feature_index =
            kStackSizeFeatureIndex,
        .stack_size_encoding_denominator =
            kStackSizeEncodingDenominator,
        .census = census,
        .bundle_immutable =
            integrity::snapshot_regular_file(
                std::string(bundle::kArtifactPath)) ==
            before,
    };
}

std::string format_stack_census_report(
    const StackCensusReport& report) {
    if (report.bundle_schema !=
            bundle::kBundleSchema ||
        report.bundle_bytes !=
            bundle::kPublishedArtifactBytes ||
        report.bundle_sha256 !=
            bundle::kPublishedArtifactSha256 ||
        report.feature_schema !=
            bundle::kFeatureSchema ||
        report.feature_count !=
            bundle::kFeatureCount ||
        report.feature_contract_sha256 !=
            bundle::kFeatureContractSha256 ||
        report.stack_size_feature_index !=
            kStackSizeFeatureIndex ||
        report.stack_size_encoding_denominator !=
            kStackSizeEncodingDenominator ||
        !report.bundle_immutable ||
        !self_consistent_stack_census(
            report.census) ||
        report.census != kExpectedStackCensus) {
        throw std::invalid_argument(
            "invalid FQ4 DEV3 stack-census report");
    }
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output
        << "schema=" << kStackCensusSchema
        << " bundle_schema="
        << report.bundle_schema
        << " bundle_bytes="
        << report.bundle_bytes
        << " bundle_sha256="
        << report.bundle_sha256
        << '\n'
        << "feature_contract schema="
        << report.feature_schema
        << " feature_count="
        << report.feature_count
        << " feature_contract_sha256="
        << report.feature_contract_sha256
        << " stack_size_feature_index="
        << report.stack_size_feature_index
        << " encoding=stack_size_over_"
        << report.stack_size_encoding_denominator
        << " bundle_immutable=1\n";
    write_stack_role(
        output, "fit", "positive",
        report.census.fit.positive);
    write_stack_role(
        output, "check", "positive",
        report.census.check.positive);
    write_stack_role(
        output, "fit", "background",
        report.census.fit.background);
    write_stack_role(
        output, "check", "background",
        report.census.check.background);
    output
        << "accounting selected_rows="
        << report.census.selected_rows
        << " selected_options="
        << report.census.selected_options
        << " action_invariant_rows="
        << report.census.action_invariant_rows
        << " exact_stack_encoding_rows="
        << report.census.exact_stack_encoding_rows
        << " role_overlap_rows="
        << report.census.role_overlap_rows
        << " models_loaded=0 fits=0 games=0\n"
        << "result=PASS\n";
    return output.str();
}

Report run_fixed() {
    const integrity::RegularFileSnapshot bundle_before =
        integrity::snapshot_regular_file(
            std::string(bundle::kArtifactPath));
    const integrity::RegularFileSnapshot parent_before =
        integrity::snapshot_regular_file(
            std::string(evaluator::kParentArtifactPath));
    const bundle::Bundle artifact =
        bundle::load_published();
    const evaluator::PreparedCorpus corpus =
        evaluator::prepare(artifact);
    const auto parent =
        evaluator::load_fixed_parent();
    const evaluator::CandidateFit fit =
        evaluator::fit_candidate(corpus, parent);
    const std::string parent_fingerprint =
        learned_model_fingerprint(parent);
    const std::string candidate_fingerprint =
        learned_model_fingerprint(fit.model);
    const auto parent_components =
        learned_model_component_fingerprints(parent);
    const auto candidate_components =
        learned_model_component_fingerprints(
            fit.model);
    const evaluator::CorpusLogits parent_logits =
        evaluator::score_logits(corpus, parent);
    const evaluator::CorpusLogits candidate_logits =
        evaluator::score_logits(
            corpus, fit.model);
    const Measurements parent_control =
        measure(
            corpus, parent_logits,
            parent_logits);
    const Measurements measurements =
        measure(
            corpus, parent_logits,
            candidate_logits);
    const auto positive_report =
        evaluator::evaluate_models(
            corpus, parent, fit.model);
    if (positive_report.candidate_fingerprint !=
            candidate_fingerprint ||
        !positive_report.metrics.parent_anchors_exact ||
        !positive_report.metrics.accounting.zero()) {
        fail("positive-root evaluator cross-check failed");
    }
    const bool parent_immutable =
        learned_model_fingerprint(parent) ==
            parent_fingerprint &&
        integrity::snapshot_regular_file(
            std::string(bundle::kArtifactPath)) ==
            bundle_before &&
        integrity::snapshot_regular_file(
            std::string(
                evaluator::kParentArtifactPath)) ==
            parent_before;
    return {
        .parent_fingerprint =
            parent_fingerprint,
        .candidate_fingerprint =
            candidate_fingerprint,
        .parent_components =
            parent_components,
        .candidate_components =
            candidate_components,
        .fit_accounting = fit.accounting,
        .parent_control = parent_control,
        .measurements = measurements,
        .parent_immutable = parent_immutable,
        .candidate_exact =
            candidate_fingerprint ==
            kRejectedCandidateFingerprint,
        .nonpriority_components_identical =
            parent_components.critic ==
                candidate_components.critic &&
            parent_components.attack ==
                candidate_components.attack &&
            parent_components.block ==
                candidate_components.block &&
            parent_components.damage_order ==
                candidate_components.damage_order &&
            parent_components.priority !=
                candidate_components.priority,
    };
}

ParentControlReport run_parent_control() {
    const integrity::RegularFileSnapshot bundle_before =
        integrity::snapshot_regular_file(
            std::string(bundle::kArtifactPath));
    const integrity::RegularFileSnapshot parent_before =
        integrity::snapshot_regular_file(
            std::string(evaluator::kParentArtifactPath));
    const bundle::Bundle artifact =
        bundle::load_published();
    const evaluator::PreparedCorpus corpus =
        evaluator::prepare(artifact);
    const auto parent =
        evaluator::load_fixed_parent();
    const std::string parent_fingerprint =
        learned_model_fingerprint(parent);
    const auto parent_components =
        learned_model_component_fingerprints(parent);
    const evaluator::CorpusLogits parent_logits =
        evaluator::score_logits(corpus, parent);
    const Measurements measurements =
        measure(
            corpus, parent_logits,
            parent_logits);
    return {
        .parent_fingerprint =
            parent_fingerprint,
        .parent_components =
            parent_components,
        .measurements = measurements,
        .parent_immutable =
            learned_model_fingerprint(parent) ==
                parent_fingerprint &&
            integrity::snapshot_regular_file(
                std::string(bundle::kArtifactPath)) ==
                bundle_before &&
            integrity::snapshot_regular_file(
                std::string(
                    evaluator::kParentArtifactPath)) ==
                parent_before,
    };
}

std::string format_parent_control_report(
    const ParentControlReport& report) {
    if (report.parent_fingerprint !=
            bundle::kParentModelFingerprint ||
        !report.parent_immutable ||
        report.parent_components.critic !=
            bundle::kParentCriticFingerprint ||
        report.parent_components.priority !=
            bundle::kParentPriorityFingerprint ||
        report.parent_components.attack !=
            bundle::kParentAttackFingerprint ||
        report.parent_components.block !=
            bundle::kParentBlockFingerprint ||
        report.parent_components.damage_order !=
            bundle::kParentDamageOrderFingerprint ||
        !exact_zero_control(report.measurements)) {
        throw std::invalid_argument(
            "invalid FQ4 DEV2 parent-control report");
    }
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(17);
    output
        << "schema=" << kSchema
        << " mode=parent-control"
        << " bundle_bytes="
        << bundle::kPublishedArtifactBytes
        << " bundle_sha256="
        << bundle::kPublishedArtifactSha256
        << '\n'
        << "identity parent_fingerprint="
        << report.parent_fingerprint
        << " parent_immutable=1"
           " parent_control_exact_zero=1\n";
    write_split(
        output, "fit",
        report.measurements.fit);
    write_split(
        output, "check",
        report.measurements.check);
    output
        << "accounting games=0 determinizations=0"
           " search_calls=0 sampled_worlds=0"
           " rollout_evaluations=0 terminal_leaves=0"
           " bootstrap_leaves=0 dominance_transitions=0\n"
        << "result=PASS\n";
    return output.str();
}

std::string format_report(const Report& report) {
    const auto& fit = report.fit_accounting;
    const auto& measurements =
        report.measurements;
    if (report.parent_fingerprint !=
            bundle::kParentModelFingerprint ||
        report.candidate_fingerprint !=
            kRejectedCandidateFingerprint ||
        !report.parent_immutable ||
        !report.candidate_exact ||
        !report.nonpriority_components_identical ||
        report.parent_components.critic !=
            bundle::kParentCriticFingerprint ||
        report.parent_components.priority !=
            bundle::kParentPriorityFingerprint ||
        report.parent_components.attack !=
            bundle::kParentAttackFingerprint ||
        report.parent_components.block !=
            bundle::kParentBlockFingerprint ||
        report.parent_components.damage_order !=
            bundle::kParentDamageOrderFingerprint ||
        report.candidate_components.critic !=
            report.parent_components.critic ||
        report.candidate_components.attack !=
            report.parent_components.attack ||
        report.candidate_components.block !=
            report.parent_components.block ||
        report.candidate_components.damage_order !=
            report.parent_components.damage_order ||
        report.candidate_components.priority ==
            report.parent_components.priority ||
        fit.fit_examples != kFitExamples ||
        fit.fit_options != kFitOptions ||
        fit.check_examples != 0 ||
        fit.background_only_examples != 0 ||
        fit.optimizer_calls != 1 ||
        fit.training_input_sha256 !=
            kFitInputSha256 ||
        fit.optimizer != evaluator::kOptimizer ||
        !exact_zero_control(
            report.parent_control) ||
        !derived_summary_valid(measurements) ||
        measurements.fit.options +
                measurements.check.options !=
            kBackgroundOptions) {
        throw std::invalid_argument(
            "invalid FQ4 DEV2 background report");
    }
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(17);
    output
        << "schema=" << kSchema
        << " bundle_bytes="
        << bundle::kPublishedArtifactBytes
        << " bundle_sha256="
        << bundle::kPublishedArtifactSha256
        << '\n'
        << "identity parent_fingerprint="
        << report.parent_fingerprint
        << " candidate_fingerprint="
        << report.candidate_fingerprint
        << " parent_immutable=1"
           " candidate_exact=1"
           " nonpriority_components_identical=1"
           " parent_control_exact_zero=1\n"
        << "training fit_examples="
        << fit.fit_examples
        << " fit_options=" << fit.fit_options
        << " check_examples=0"
           " background_only_examples=0"
           " optimizer_calls=1"
           " training_input_sha256="
        << fit.training_input_sha256
        << '\n';
    write_split(
        output, "fit",
        measurements.fit);
    write_split(
        output, "check",
        measurements.check);
    output
        << "hypothesis material_green_or_white="
        << (measurements.material_green_or_white
                ? 1
                : 0)
        << " green_white_exceeds_blue_ru="
        << (measurements.green_white_exceeds_blue_ru
                ? 1
                : 0)
        << " green_white_mean_kl="
        << measurements.green_white_mean_kl
        << " blue_ru_mean_kl="
        << measurements.blue_ru_mean_kl
        << " supported="
        << (measurements.hypothesis_supported
                ? 1
                : 0)
        << '\n'
        << "accounting games=0 determinizations=0"
           " search_calls=0 sampled_worlds=0"
           " rollout_evaluations=0 terminal_leaves=0"
           " bootstrap_leaves=0 dominance_transitions=0\n"
        << "result="
        << (measurements.hypothesis_supported
                ? "SUPPORTED"
                : "NOT_SUPPORTED")
        << '\n';
    return output.str();
}

} // namespace old_school::fq4_dev_background_diagnostic
