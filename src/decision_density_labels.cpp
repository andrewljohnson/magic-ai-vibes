#include "old_school/decision_density_labels.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/learned_iteration.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <ostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unistd.h>
#include <utility>

namespace old_school::decision_density_labels {
namespace {

constexpr std::string_view kCorpusDigestSchema =
    "old-school-aq18-dbc6-label-corpus-v1";
constexpr std::string_view kProjectedPhysicalSchema =
    "old-school-aq18-dbc6-opaque-physical-game-v1";
constexpr std::string_view kProjectedActorSchema =
    "old-school-aq18-dbc6-opaque-actor-game-v1";
constexpr std::array<char, 8> kCacheMagic{
    'O', 'S', 'A', 'Q', '1', '8', 'L', '1',
};
constexpr std::size_t kMaximumTextBytes = 4096;
constexpr std::size_t kMaximumRoots = 4096;
constexpr std::size_t kMaximumActions = 1024;
constexpr std::size_t kMaximumFeatures =
    density::kPolicyFeatureCount;
constexpr std::size_t kMaximumAliases = 4096;
constexpr std::size_t kMaximumMatrixCells =
    kMaximumActions * kWorlds;
// Frozen AQ17 S0 cells in priority::cell_index order: TRAIN then DEV,
// DeckId order within each split, B2/B3/B4+ within each deck.
constexpr std::array<std::size_t, kExpectedCells>
    kExpectedCellOptions{
        40, 60, 98,
        40, 60, 133,
        40, 60, 108,
        40, 60, 102,
        40, 60, 147,
        20, 30, 55,
        20, 30, 55,
        20, 30, 42,
        20, 30, 55,
        20, 30, 56,
    };
constexpr std::array<std::size_t, kExpectedCells>
    kExpectedCellAliasPairs{
        0, 0, 13,
        0, 0, 24,
        0, 0, 2,
        0, 0, 12,
        0, 0, 5,
        0, 0, 19,
        0, 0, 3,
        0, 0, 2,
        0, 0, 10,
        0, 0, 1,
    };
constexpr std::size_t kFrozenActorGameCap = 6;
static_assert([] {
    std::size_t total = 0;
    for (std::size_t cell = 0;
         cell < kDeckCount * priority::kWidthStrata;
         ++cell) {
        total += kExpectedCellOptions[cell];
    }
    return total;
}() == kExpectedTrainOptions);
static_assert([] {
    std::size_t total = 0;
    for (std::size_t cell =
             kDeckCount * priority::kWidthStrata;
         cell < kExpectedCells; ++cell) {
        total += kExpectedCellOptions[cell];
    }
    return total;
}() == kExpectedDevOptions);
static_assert([] {
    std::size_t total = 0;
    for (const std::size_t pairs :
         kExpectedCellAliasPairs) {
        total += pairs;
    }
    return total;
}() == kExpectedAliasPairs);

bool canonical_sha256(std::string_view value) {
    return value.size() == 64 &&
           std::all_of(
               value.begin(), value.end(),
               [](char character) {
                   return
                       (character >= '0' &&
                        character <= '9') ||
                       (character >= 'a' &&
                        character <= 'f');
               });
}

bool finite_probability(double value) {
    return std::isfinite(value) &&
           value >= 0.0 && value <= 1.0;
}

bool bit_equal(double left, double right) {
    return
        std::bit_cast<std::uint64_t>(left) ==
        std::bit_cast<std::uint64_t>(right);
}

bool rows_bit_identical(
    std::span<const double> left,
    std::span<const double> right) {
    return
        left.size() == right.size() &&
        std::equal(
            left.begin(), left.end(), right.begin(),
            bit_equal);
}

void checked_add(
    std::size_t& destination, std::size_t value,
    std::string_view field) {
    if (value >
        std::numeric_limits<std::size_t>::max() -
            destination) {
        throw std::overflow_error(
            "AQ18 " + std::string(field) +
            " overflow");
    }
    destination += value;
}

std::size_t deck_index(DeckId deck) {
    const std::size_t result =
        static_cast<std::size_t>(deck);
    if (result >= kDeckCount) {
        throw std::out_of_range(
            "AQ18 deck is invalid");
    }
    return result;
}

std::size_t stratum_index(
    priority::WidthStratum stratum) {
    const std::size_t result =
        static_cast<std::size_t>(stratum);
    if (result >= priority::kWidthStrata) {
        throw std::out_of_range(
            "AQ18 width stratum is invalid");
    }
    return result;
}

std::string_view split_name(density::Split split) {
    switch (split) {
    case density::Split::Train:
        return "TRAIN";
    case density::Split::Dev:
        return "DEV";
    }
    throw std::out_of_range("AQ18 split is invalid");
}

std::string_view stratum_name(
    priority::WidthStratum stratum) {
    switch (stratum) {
    case priority::WidthStratum::B2:
        return "B2";
    case priority::WidthStratum::B3:
        return "B3";
    case priority::WidthStratum::B4Plus:
        return "B4+";
    }
    throw std::out_of_range(
        "AQ18 width stratum is invalid");
}

class Writer {
  public:
    explicit Writer(
        std::size_t maximum = kMaximumCacheBytes)
        : maximum_(maximum) {}

    void u8(std::uint8_t value) {
        ensure(1);
        bytes_.push_back(static_cast<char>(value));
    }

    void boolean(bool value) {
        u8(value ? 1U : 0U);
    }

    void u64(std::uint64_t value) {
        ensure(8);
        for (std::size_t byte = 0; byte < 8; ++byte) {
            bytes_.push_back(static_cast<char>(
                static_cast<std::uint8_t>(
                    value >> (byte * 8U))));
        }
    }

    void size(std::size_t value) {
        u64(static_cast<std::uint64_t>(value));
    }

    void real(double value) {
        u64(std::bit_cast<std::uint64_t>(value));
    }

    void raw(std::string_view value) {
        ensure(value.size());
        bytes_.append(value.data(), value.size());
    }

    void text(std::string_view value) {
        if (value.size() > kMaximumTextBytes) {
            throw std::length_error(
                "AQ18 cache text exceeds its bound");
        }
        size(value.size());
        raw(value);
    }

    const std::string& bytes() const {
        return bytes_;
    }

    std::string take() {
        return std::move(bytes_);
    }

  private:
    void ensure(std::size_t count) const {
        if (count > maximum_ - bytes_.size()) {
            throw std::length_error(
                "AQ18 cache exceeds its byte bound");
        }
    }

    std::size_t maximum_;
    std::string bytes_;
};

class Reader {
  public:
    explicit Reader(std::string_view bytes)
        : bytes_(bytes) {}

    std::uint8_t u8(std::string_view field) {
        require(1, field);
        return static_cast<std::uint8_t>(
            static_cast<unsigned char>(
                bytes_[position_++]));
    }

    bool boolean(std::string_view field) {
        const std::uint8_t value = u8(field);
        if (value > 1) {
            fail(field, "is not a canonical boolean");
        }
        return value != 0;
    }

    std::uint64_t u64(std::string_view field) {
        require(8, field);
        std::uint64_t result = 0;
        for (std::size_t byte = 0; byte < 8; ++byte) {
            result |=
                static_cast<std::uint64_t>(
                    static_cast<unsigned char>(
                        bytes_[position_ + byte]))
                << (byte * 8U);
        }
        position_ += 8;
        return result;
    }

    std::size_t size(
        std::size_t maximum,
        std::string_view field) {
        const std::uint64_t value = u64(field);
        if (value > maximum ||
            value >
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::size_t>::max())) {
            fail(field, "exceeds its bound");
        }
        return static_cast<std::size_t>(value);
    }

    double real(std::string_view field) {
        return std::bit_cast<double>(u64(field));
    }

    std::string_view raw(
        std::size_t count,
        std::string_view field) {
        require(count, field);
        const std::string_view result =
            bytes_.substr(position_, count);
        position_ += count;
        return result;
    }

    std::string text(
        std::string_view field,
        std::size_t maximum = kMaximumTextBytes) {
        const std::size_t count = size(maximum, field);
        return std::string(raw(count, field));
    }

    void finish() const {
        if (position_ != bytes_.size()) {
            throw std::runtime_error(
                "AQ18 cache has trailing bytes");
        }
    }

  private:
    [[noreturn]] static void fail(
        std::string_view field,
        std::string_view detail) {
        throw std::runtime_error(
            "AQ18 cache " + std::string(field) +
            " " + std::string(detail));
    }

    void require(
        std::size_t count,
        std::string_view field) const {
        if (position_ > bytes_.size() ||
            count > bytes_.size() - position_) {
            fail(field, "is truncated");
        }
    }

    std::string_view bytes_;
    std::size_t position_ = 0;
};

void append_optional_id(
    Writer& output,
    const std::optional<std::uint64_t>& value) {
    output.boolean(value.has_value());
    if (value.has_value()) {
        output.u64(*value);
    }
}

std::optional<std::uint64_t> read_optional_id(
    Reader& input, std::string_view field) {
    if (!input.boolean(
            std::string(field) + " present")) {
        return std::nullopt;
    }
    return input.u64(field);
}

void append_action(
    Writer& output, const PriorityAction& action) {
    output.u64(
        static_cast<std::uint64_t>(action.kind));
    output.u64(
        static_cast<std::uint64_t>(action.card));
    output.boolean(action.target.has_value());
    if (action.target.has_value()) {
        output.size(action.target->player);
        append_optional_id(
            output, action.target->creature);
    }
    append_optional_id(output, action.spell_target);
    append_optional_id(
        output, action.source_permanent);
    output.u64(std::bit_cast<std::uint64_t>(
        static_cast<std::int64_t>(action.x_value)));
}

PriorityAction read_action(Reader& input) {
    const std::uint64_t raw_kind =
        input.u64("action kind");
    const std::uint64_t raw_card =
        input.u64("action card");
    if (raw_kind >
            static_cast<std::uint64_t>(
                PriorityActionKind::ActivateMillstone) ||
        raw_card >= kCardCount) {
        throw std::runtime_error(
            "AQ18 cache action enum is invalid");
    }
    PriorityAction result;
    result.kind =
        static_cast<PriorityActionKind>(raw_kind);
    result.card = static_cast<CardId>(raw_card);
    if (input.boolean("action target present")) {
        const std::size_t player =
            input.size(1, "target player");
        const auto creature =
            read_optional_id(
                input, "target creature");
        result.target = Target{
            .player = player,
            .creature = creature,
        };
    }
    result.spell_target =
        read_optional_id(input, "spell target");
    result.source_permanent =
        read_optional_id(
            input, "source permanent");
    const std::int64_t x =
        std::bit_cast<std::int64_t>(
            input.u64("action X"));
    if (x < std::numeric_limits<int>::min() ||
        x > std::numeric_limits<int>::max()) {
        throw std::runtime_error(
            "AQ18 cache action X is out of range");
    }
    result.x_value = static_cast<int>(x);
    return result;
}

void append_components(
    Writer& output,
    const LearnedModelComponentFingerprints& components) {
    output.text(components.critic);
    output.text(components.priority);
    output.text(components.attack);
    output.text(components.block);
    output.text(components.damage_order);
}

LearnedModelComponentFingerprints read_components(
    Reader& input) {
    return {
        .critic = input.text("critic fingerprint", 64),
        .priority = input.text(
            "priority fingerprint", 64),
        .attack = input.text("attack fingerprint", 64),
        .block = input.text("block fingerprint", 64),
        .damage_order = input.text(
            "damage-order fingerprint", 64),
    };
}

void append_search_config(
    Writer& output,
    const LearnedSearchConfig& config) {
    output.u64(config.seed);
    output.size(config.worlds);
    output.size(config.rollouts_per_world);
    output.size(config.horizon_turns);
    output.u64(static_cast<std::uint64_t>(
        config.continuation_variant));
    output.real(config.value_continuation_epsilon);
    output.boolean(config.blend_shallow_prior);
    output.real(
        config.value_resolved_shallow_prior_weight);
    output.real(
        config.value_priority_residual_weight);
    output.boolean(config.value_pass_dominance);
    output.u64(static_cast<std::uint64_t>(
        config.value_continuation_controller));
    output.size(config.evaluation_threads);
    output.boolean(
        config.capture_priority_h0_boundaries);
    output.size(
        config.value_continuation_search_worlds);
    output.u64(static_cast<std::uint64_t>(
        config.value_continuation_search_scope));
    output.boolean(
        config.capture_settled_boundary_samples);
    output.boolean(config.use_exact_combat_subgame);
    output.u64(static_cast<std::uint64_t>(
        config.terminal_utility_mode));
}

bool search_configs_bit_identical(
    const LearnedSearchConfig& left,
    const LearnedSearchConfig& right) {
    Writer left_bytes(4096);
    Writer right_bytes(4096);
    append_search_config(left_bytes, left);
    append_search_config(right_bytes, right);
    return left_bytes.bytes() == right_bytes.bytes();
}

LearnedSearchConfig read_search_config(Reader& input) {
    LearnedSearchConfig result;
    result.seed = input.u64("search seed");
    result.worlds =
        input.size(kWorlds, "search worlds");
    result.rollouts_per_world =
        input.size(16, "search rollouts");
    result.horizon_turns =
        input.size(128, "search horizon");
    const auto variant =
        input.u64("search continuation variant");
    if (variant >
        static_cast<std::uint64_t>(
            LearnedVariant::UnifiedActor)) {
        throw std::runtime_error(
            "AQ18 cache search variant is invalid");
    }
    result.continuation_variant =
        static_cast<LearnedVariant>(variant);
    result.value_continuation_epsilon =
        input.real("search epsilon");
    result.blend_shallow_prior =
        input.boolean("search shallow blend");
    result.value_resolved_shallow_prior_weight =
        input.real("search resolved prior");
    result.value_priority_residual_weight =
        input.real("search residual");
    result.value_pass_dominance =
        input.boolean("search pass dominance");
    const auto controller =
        input.u64("search controller");
    if (controller >
        static_cast<std::uint64_t>(
            LearnedContinuationController::
                PublicStackPassV1)) {
        throw std::runtime_error(
            "AQ18 cache search controller is invalid");
    }
    result.value_continuation_controller =
        static_cast<LearnedContinuationController>(
            controller);
    result.evaluation_threads =
        input.size(64, "search threads");
    result.capture_priority_h0_boundaries =
        input.boolean("search H0 capture");
    result.value_continuation_search_worlds =
        input.size(64, "search inner worlds");
    const auto scope =
        input.u64("search scope");
    if (scope >
        static_cast<std::uint64_t>(
            LearnedContinuationSearchScope::
                AllDecisions)) {
        throw std::runtime_error(
            "AQ18 cache search scope is invalid");
    }
    result.value_continuation_search_scope =
        static_cast<LearnedContinuationSearchScope>(
            scope);
    result.capture_settled_boundary_samples =
        input.boolean("search settled capture");
    result.use_exact_combat_subgame =
        input.boolean("search exact combat");
    const auto utility =
        input.u64("search terminal utility");
    if (utility >
        static_cast<std::uint64_t>(
            LearnedTerminalUtilityMode::
                C16DiscountedAbsoluteTurn)) {
        throw std::runtime_error(
            "AQ18 cache terminal utility is invalid");
    }
    result.terminal_utility_mode =
        static_cast<LearnedTerminalUtilityMode>(
            utility);
    return result;
}

void append_projected_root(
    Writer& output, const ProjectedRoot& root) {
    output.u64(
        static_cast<std::uint64_t>(root.split));
    output.u64(
        static_cast<std::uint64_t>(
            root.owner_deck));
    output.u64(
        static_cast<std::uint64_t>(
            root.width_stratum));
    output.text(root.stable_root_id);
    output.text(root.selection_key);
    output.text(root.information_action_fingerprint);
    output.text(root.physical_game_group);
    output.text(root.actor_game_group);
    output.u64(root.label_seed);
}

ProjectedRoot read_projected_root(Reader& input) {
    const auto split = input.u64("root split");
    const auto deck = input.u64("root deck");
    const auto stratum = input.u64("root width");
    if (split > 1 || deck >= kDeckCount ||
        stratum >= priority::kWidthStrata) {
        throw std::runtime_error(
            "AQ18 cache root enum is invalid");
    }
    return {
        .split = static_cast<density::Split>(split),
        .owner_deck = static_cast<DeckId>(deck),
        .width_stratum =
            static_cast<priority::WidthStratum>(
                stratum),
        .stable_root_id =
            input.text("stable root", 64),
        .selection_key =
            input.text("selection key", 64),
        .information_action_fingerprint =
            input.text("information fingerprint", 64),
        .physical_game_group =
            input.text("physical group", 64),
        .actor_game_group =
            input.text("actor group", 64),
        .label_seed =
            input.u64("root label seed"),
    };
}

void append_size_matrix(
    Writer& output,
    const std::vector<std::vector<std::size_t>>& matrix) {
    output.size(matrix.size());
    for (const auto& row : matrix) {
        output.size(row.size());
        for (const std::size_t value : row) {
            output.size(value);
        }
    }
}

std::vector<std::vector<std::size_t>>
read_size_matrix(
    Reader& input, std::string_view field) {
    const std::size_t rows =
        input.size(kMaximumActions, field);
    std::vector<std::vector<std::size_t>> result;
    result.reserve(rows);
    std::size_t cells = 0;
    for (std::size_t row = 0; row < rows; ++row) {
        const std::size_t columns =
            input.size(kWorlds, field);
        checked_add(cells, columns, "matrix cell");
        if (cells > kMaximumMatrixCells) {
            throw std::runtime_error(
                "AQ18 cache matrix exceeds its bound");
        }
        std::vector<std::size_t> values;
        values.reserve(columns);
        for (std::size_t column = 0;
             column < columns; ++column) {
            values.push_back(
                input.size(
                    std::numeric_limits<std::size_t>::
                        max(),
                    field));
        }
        result.push_back(std::move(values));
    }
    return result;
}

void append_accounting(
    Writer& output,
    const SearchAccounting& accounting) {
    output.size(accounting.sampled_worlds);
    output.size(accounting.rollout_evaluations);
    output.size(accounting.terminal_evaluations);
    output.size(accounting.bootstrapped_evaluations);
    output.size(
        accounting.inner_rollout_evaluations);
    output.size(
        accounting.inner_search_invocations);
    output.size(accounting.inner_search_max_depth);
    append_size_matrix(
        output,
        accounting
            .inner_rollout_evaluations_by_cell);
    append_size_matrix(
        output,
        accounting
            .inner_search_invocations_by_cell);
    append_size_matrix(
        output,
        accounting
            .inner_search_max_depth_by_cell);
}

SearchAccounting read_accounting(Reader& input) {
    return {
        .sampled_worlds =
            input.size(kWorlds, "sampled worlds"),
        .rollout_evaluations =
            input.size(
                kMaximumMatrixCells,
                "rollout evaluations"),
        .terminal_evaluations =
            input.size(
                kMaximumMatrixCells,
                "terminal evaluations"),
        .bootstrapped_evaluations =
            input.size(
                kMaximumMatrixCells,
                "bootstrapped evaluations"),
        .inner_rollout_evaluations =
            input.size(
                std::numeric_limits<std::size_t>::max(),
                "inner rollout evaluations"),
        .inner_search_invocations =
            input.size(
                std::numeric_limits<std::size_t>::max(),
                "inner invocations"),
        .inner_search_max_depth =
            input.size(16, "inner depth"),
        .inner_rollout_evaluations_by_cell =
            read_size_matrix(
                input, "inner rollout matrix"),
        .inner_search_invocations_by_cell =
            read_size_matrix(
                input, "inner invocation matrix"),
        .inner_search_max_depth_by_cell =
            read_size_matrix(
                input, "inner depth matrix"),
    };
}

void append_real_rows(
    Writer& output,
    const std::vector<std::vector<double>>& rows) {
    output.size(rows.size());
    for (const auto& row : rows) {
        output.size(row.size());
        for (const double value : row) {
            output.real(value);
        }
    }
}

std::vector<std::vector<double>> read_real_rows(
    Reader& input, std::size_t maximum_rows,
    std::size_t maximum_columns,
    std::string_view field) {
    const std::size_t rows =
        input.size(maximum_rows, field);
    std::vector<std::vector<double>> result;
    result.reserve(rows);
    for (std::size_t row = 0; row < rows; ++row) {
        const std::size_t columns =
            input.size(maximum_columns, field);
        std::vector<double> values;
        values.reserve(columns);
        for (std::size_t column = 0;
             column < columns; ++column) {
            values.push_back(input.real(field));
        }
        result.push_back(std::move(values));
    }
    return result;
}

void append_flag_rows(
    Writer& output,
    const std::vector<std::vector<std::uint8_t>>& rows) {
    output.size(rows.size());
    for (const auto& row : rows) {
        output.size(row.size());
        for (const std::uint8_t value : row) {
            output.u8(value);
        }
    }
}

std::vector<std::vector<std::uint8_t>>
read_flag_rows(
    Reader& input, std::string_view field) {
    const std::size_t rows =
        input.size(kMaximumActions, field);
    std::vector<std::vector<std::uint8_t>> result;
    result.reserve(rows);
    for (std::size_t row = 0; row < rows; ++row) {
        const std::size_t columns =
            input.size(kWorlds, field);
        std::vector<std::uint8_t> values;
        values.reserve(columns);
        for (std::size_t column = 0;
             column < columns; ++column) {
            const std::uint8_t value =
                input.u8(field);
            if (value > 1) {
                throw std::runtime_error(
                    "AQ18 cache terminal flag is invalid");
            }
            values.push_back(value);
        }
        result.push_back(std::move(values));
    }
    return result;
}

void append_real_vector(
    Writer& output,
    std::span<const double> values) {
    output.size(values.size());
    for (const double value : values) {
        output.real(value);
    }
}

std::vector<double> read_real_vector(
    Reader& input, std::size_t maximum,
    std::string_view field) {
    const std::size_t count =
        input.size(maximum, field);
    std::vector<double> result;
    result.reserve(count);
    for (std::size_t index = 0;
         index < count; ++index) {
        result.push_back(input.real(field));
    }
    return result;
}

void append_root_label(
    Writer& output, const RootLabel& root) {
    append_projected_root(output, root.identity);
    output.size(root.actions.size());
    for (const PriorityAction& action : root.actions) {
        append_action(output, action);
    }
    output.size(root.action_descriptors.size());
    for (const std::string& descriptor :
         root.action_descriptors) {
        output.text(descriptor);
    }
    append_real_rows(output, root.option_rows);
    append_real_rows(output, root.base_q_samples);
    append_flag_rows(output, root.base_terminal_flags);
    append_real_rows(
        output, root.base_shallow_prior_samples);
    append_real_rows(
        output, root.base_continuation_samples);
    append_real_vector(
        output, root.base_aggregate_scores);
    append_accounting(output, root.base_accounting);
    append_real_rows(output, root.teacher_q_samples);
    append_flag_rows(
        output, root.teacher_terminal_flags);
    append_real_rows(
        output, root.teacher_shallow_prior_samples);
    append_real_rows(
        output, root.teacher_continuation_samples);
    append_real_vector(
        output, root.teacher_aggregate_scores);
    append_accounting(output, root.teacher_accounting);
    append_real_vector(
        output, root.target_probabilities);
    output.real(root.weight);
}

bool root_labels_bit_identical(
    const RootLabel& left, const RootLabel& right) {
    Writer left_bytes;
    Writer right_bytes;
    append_root_label(left_bytes, left);
    append_root_label(right_bytes, right);
    return left_bytes.bytes() == right_bytes.bytes();
}

RootLabel read_root_label(Reader& input) {
    RootLabel result;
    result.identity = read_projected_root(input);
    const std::size_t actions =
        input.size(kMaximumActions, "root actions");
    result.actions.reserve(actions);
    for (std::size_t action = 0;
         action < actions; ++action) {
        result.actions.push_back(read_action(input));
    }
    const std::size_t descriptors =
        input.size(
            kMaximumActions, "root descriptors");
    result.action_descriptors.reserve(descriptors);
    for (std::size_t descriptor = 0;
         descriptor < descriptors; ++descriptor) {
        result.action_descriptors.push_back(
            input.text("action descriptor"));
    }
    result.option_rows =
        read_real_rows(
            input, kMaximumActions,
            kMaximumFeatures, "option rows");
    result.base_q_samples =
        read_real_rows(
            input, kMaximumActions, kWorlds,
            "base Q samples");
    result.base_terminal_flags =
        read_flag_rows(input, "base terminal flags");
    result.base_shallow_prior_samples =
        read_real_rows(
            input, kMaximumActions, kWorlds,
            "base shallow samples");
    result.base_continuation_samples =
        read_real_rows(
            input, kMaximumActions, kWorlds,
            "base continuation samples");
    result.base_aggregate_scores =
        read_real_vector(
            input, kMaximumActions,
            "base aggregates");
    result.base_accounting = read_accounting(input);
    result.teacher_q_samples =
        read_real_rows(
            input, kMaximumActions, kWorlds,
            "teacher Q samples");
    result.teacher_terminal_flags =
        read_flag_rows(
            input, "teacher terminal flags");
    result.teacher_shallow_prior_samples =
        read_real_rows(
            input, kMaximumActions, kWorlds,
            "teacher shallow samples");
    result.teacher_continuation_samples =
        read_real_rows(
            input, kMaximumActions, kWorlds,
            "teacher continuation samples");
    result.teacher_aggregate_scores =
        read_real_vector(
            input, kMaximumActions,
            "teacher aggregates");
    result.teacher_accounting =
        read_accounting(input);
    result.target_probabilities =
        read_real_vector(
            input, kMaximumActions,
            "target probabilities");
    result.weight = input.real("root weight");
    return result;
}

void append_cell_metrics(
    Writer& output, const CellMetrics& metrics) {
    output.u64(
        static_cast<std::uint64_t>(metrics.split));
    output.u64(
        static_cast<std::uint64_t>(
            metrics.owner_deck));
    output.u64(
        static_cast<std::uint64_t>(
            metrics.width_stratum));
    output.size(metrics.roots);
    output.size(metrics.options);
    output.size(metrics.stable_pairs);
    output.size(metrics.differing_roots);
    output.size(metrics.alias_pairs);
    output.size(metrics.material_alias_conflicts);
    output.real(metrics.weight_mass);
    output.real(metrics.exact_max_agreement);
    output.real(metrics.stable_pair_agreement);
    output.real(metrics.listwise_cross_entropy);
    output.real(metrics.teacher_regret);
    output.real(
        metrics.maximum_absolute_alias_correction_gap);
}

CellMetrics read_cell_metrics(Reader& input) {
    const auto split = input.u64("cell split");
    const auto deck = input.u64("cell deck");
    const auto stratum = input.u64("cell width");
    if (split > 1 || deck >= kDeckCount ||
        stratum >= priority::kWidthStrata) {
        throw std::runtime_error(
            "AQ18 cache cell enum is invalid");
    }
    return {
        .split = static_cast<density::Split>(split),
        .owner_deck = static_cast<DeckId>(deck),
        .width_stratum =
            static_cast<priority::WidthStratum>(
                stratum),
        .roots = input.size(
            kMaximumRoots, "cell roots"),
        .options = input.size(
            kMaximumMatrixCells, "cell options"),
        .stable_pairs = input.size(
            kMaximumMatrixCells, "cell stable pairs"),
        .differing_roots = input.size(
            kMaximumRoots, "cell differing roots"),
        .alias_pairs = input.size(
            kMaximumAliases, "cell alias pairs"),
        .material_alias_conflicts = input.size(
            kMaximumAliases,
            "cell material alias conflicts"),
        .weight_mass = input.real("cell weight mass"),
        .exact_max_agreement =
            input.real("cell exact-max agreement"),
        .stable_pair_agreement =
            input.real("cell stable-pair agreement"),
        .listwise_cross_entropy =
            input.real("cell cross entropy"),
        .teacher_regret =
            input.real("cell teacher regret"),
        .maximum_absolute_alias_correction_gap =
            input.real(
                "cell maximum alias correction gap"),
    };
}

void append_alias(
    Writer& output, const AliasDiagnostic& alias) {
    output.u64(
        static_cast<std::uint64_t>(alias.split));
    output.u64(
        static_cast<std::uint64_t>(
            alias.owner_deck));
    output.u64(
        static_cast<std::uint64_t>(
            alias.width_stratum));
    output.text(alias.stable_root_id);
    output.size(alias.left_action);
    output.size(alias.right_action);
    for (const double value :
         alias.paired_corrections) {
        output.real(value);
    }
    output.real(alias.paired_mean);
    output.real(alias.paired_standard_error);
    output.real(alias.correction_gap);
    output.real(alias.target_probability_gap);
    output.boolean(alias.material_conflict);
}

bool aliases_bit_identical(
    const AliasDiagnostic& left,
    const AliasDiagnostic& right) {
    Writer left_bytes(8192);
    Writer right_bytes(8192);
    append_alias(left_bytes, left);
    append_alias(right_bytes, right);
    return left_bytes.bytes() == right_bytes.bytes();
}

AliasDiagnostic read_alias(Reader& input) {
    const auto split = input.u64("alias split");
    const auto deck = input.u64("alias deck");
    const auto stratum = input.u64("alias width");
    if (split > 1 || deck >= kDeckCount ||
        stratum >= priority::kWidthStrata) {
        throw std::runtime_error(
            "AQ18 cache alias enum is invalid");
    }
    AliasDiagnostic result{
        .split = static_cast<density::Split>(split),
        .owner_deck = static_cast<DeckId>(deck),
        .width_stratum =
            static_cast<priority::WidthStratum>(
                stratum),
        .stable_root_id =
            input.text("alias root", 64),
        .left_action =
            input.size(kMaximumActions, "alias left"),
        .right_action =
            input.size(kMaximumActions, "alias right"),
    };
    for (double& value : result.paired_corrections) {
        value = input.real("alias paired correction");
    }
    result.paired_mean = input.real("alias mean");
    result.paired_standard_error =
        input.real("alias standard error");
    result.correction_gap =
        input.real("alias correction gap");
    result.target_probability_gap =
        input.real("alias target gap");
    result.material_conflict =
        input.boolean("alias material conflict");
    return result;
}

void append_diagnostics(
    Writer& output, const Diagnostics& diagnostics) {
    output.size(diagnostics.cells.size());
    for (const CellMetrics& cell :
         diagnostics.cells) {
        append_cell_metrics(output, cell);
    }
    output.size(diagnostics.aliases.size());
    for (const AliasDiagnostic& alias :
         diagnostics.aliases) {
        append_alias(output, alias);
    }
    output.size(diagnostics.deck_signal.size());
    for (const bool signal :
         diagnostics.deck_signal) {
        output.boolean(signal);
    }
    output.size(diagnostics.material_alias_conflicts);
    output.real(
        diagnostics
            .maximum_absolute_alias_correction_gap);
    output.real(
        diagnostics
            .equal_deck_train_exact_max_agreement);
    output.real(
        diagnostics
            .equal_deck_train_stable_pair_agreement);
    output.real(
        diagnostics
            .equal_deck_train_listwise_cross_entropy);
    output.real(
        diagnostics
            .equal_deck_train_teacher_regret);
    output.real(
        diagnostics
            .equal_deck_dev_exact_max_agreement);
    output.real(
        diagnostics
            .equal_deck_dev_stable_pair_agreement);
    output.real(
        diagnostics
            .equal_deck_dev_listwise_cross_entropy);
    output.real(
        diagnostics
            .equal_deck_dev_teacher_regret);
}

bool diagnostics_bit_identical(
    const Diagnostics& left,
    const Diagnostics& right) {
    Writer left_bytes;
    Writer right_bytes;
    append_diagnostics(left_bytes, left);
    append_diagnostics(right_bytes, right);
    return left_bytes.bytes() == right_bytes.bytes();
}

Diagnostics read_diagnostics(Reader& input) {
    Diagnostics result;
    const std::size_t cells =
        input.size(kExpectedCells, "diagnostic cells");
    if (cells != result.cells.size()) {
        throw std::runtime_error(
            "AQ18 cache diagnostic cell count drifted");
    }
    for (CellMetrics& cell : result.cells) {
        cell = read_cell_metrics(input);
    }
    const std::size_t aliases =
        input.size(kMaximumAliases, "diagnostic aliases");
    result.aliases.reserve(aliases);
    for (std::size_t alias = 0;
         alias < aliases; ++alias) {
        result.aliases.push_back(read_alias(input));
    }
    const std::size_t signals =
        input.size(
            result.deck_signal.size(),
            "diagnostic signal count");
    if (signals != result.deck_signal.size()) {
        throw std::runtime_error(
            "AQ18 cache signal count drifted");
    }
    for (std::size_t index = 0;
         index < result.deck_signal.size(); ++index) {
        result.deck_signal[index] =
            input.boolean("diagnostic signal");
    }
    result.material_alias_conflicts =
        input.size(
            kMaximumAliases,
            "material alias conflicts");
    result.maximum_absolute_alias_correction_gap =
        input.real("maximum alias correction gap");
    result.equal_deck_train_exact_max_agreement =
        input.real("TRAIN exact-max agreement");
    result.equal_deck_train_stable_pair_agreement =
        input.real("TRAIN stable-pair agreement");
    result.equal_deck_train_listwise_cross_entropy =
        input.real("TRAIN cross entropy");
    result.equal_deck_train_teacher_regret =
        input.real("TRAIN teacher regret");
    result.equal_deck_dev_exact_max_agreement =
        input.real("DEV exact-max agreement");
    result.equal_deck_dev_stable_pair_agreement =
        input.real("DEV stable-pair agreement");
    result.equal_deck_dev_listwise_cross_entropy =
        input.real("DEV cross entropy");
    result.equal_deck_dev_teacher_regret =
        input.real("DEV teacher regret");
    return result;
}

void append_corpus_without_digest(
    Writer& output, const Corpus& corpus) {
    output.text(kCorpusDigestSchema);
    output.text(kIdentifier);
    output.text(kCacheSchema);
    output.u64(kCacheVersion);
    output.u64(kLabelSeed);
    output.size(kWorlds);
    output.size(kExpectedActionWorldCellsPerArm);
    output.real(kTrainRootWeight);
    output.real(kDevRootWeight);
    output.real(kStablePairMinimumDelta);
    output.real(kNormal95CriticalValue);
    output.real(kMaterialCorrectionGap);
    output.text(corpus.parent_artifact_sha256);
    output.text(corpus.parent_fingerprint);
    append_components(output, corpus.parent_components);
    output.text(corpus.source_manifest_hash);
    output.text(corpus.selection_manifest_hash);
    output.text(corpus.preflight_digest);
    append_search_config(
        output, corpus.base_search_template);
    append_search_config(
        output, corpus.teacher_search_template);
    output.size(corpus.train.size());
    for (const RootLabel& root : corpus.train) {
        append_root_label(output, root);
    }
    output.size(corpus.dev.size());
    for (const RootLabel& root : corpus.dev) {
        append_root_label(output, root);
    }
    append_diagnostics(output, corpus.diagnostics);
}

bool corpora_bit_identical(
    const Corpus& left, const Corpus& right) {
    Writer left_bytes;
    Writer right_bytes;
    append_corpus_without_digest(left_bytes, left);
    left_bytes.text(left.digest);
    append_corpus_without_digest(right_bytes, right);
    right_bytes.text(right.digest);
    return left_bytes.bytes() == right_bytes.bytes();
}

Corpus read_corpus_without_digest(Reader& input) {
    if (input.text("corpus schema") !=
            kCorpusDigestSchema ||
        input.text("identifier") != kIdentifier ||
        input.text("cache schema") != kCacheSchema ||
        input.u64("cache version") != kCacheVersion ||
        input.u64("label seed") != kLabelSeed ||
        input.size(kWorlds, "worlds") != kWorlds ||
        input.size(
            kExpectedActionWorldCellsPerArm,
            "action-world cells") !=
            kExpectedActionWorldCellsPerArm ||
        !bit_equal(
            input.real("TRAIN root weight"),
            kTrainRootWeight) ||
        !bit_equal(
            input.real("DEV root weight"),
            kDevRootWeight) ||
        !bit_equal(
            input.real("stable-pair delta"),
            kStablePairMinimumDelta) ||
        !bit_equal(
            input.real("normal critical value"),
            kNormal95CriticalValue) ||
        !bit_equal(
            input.real("material gap"),
            kMaterialCorrectionGap)) {
        throw std::runtime_error(
            "AQ18 cache recipe is invalid");
    }
    Corpus result;
    result.parent_artifact_sha256 =
        input.text("parent artifact SHA", 64);
    result.parent_fingerprint =
        input.text("parent fingerprint", 64);
    result.parent_components = read_components(input);
    result.source_manifest_hash =
        input.text("source manifest", 64);
    result.selection_manifest_hash =
        input.text("selection manifest", 64);
    result.preflight_digest =
        input.text("preflight digest", 64);
    result.base_search_template =
        read_search_config(input);
    result.teacher_search_template =
        read_search_config(input);
    const std::size_t train =
        input.size(kMaximumRoots, "TRAIN roots");
    result.train.reserve(train);
    for (std::size_t root = 0;
         root < train; ++root) {
        result.train.push_back(read_root_label(input));
    }
    const std::size_t dev =
        input.size(kMaximumRoots, "DEV roots");
    result.dev.reserve(dev);
    for (std::size_t root = 0;
         root < dev; ++root) {
        result.dev.push_back(read_root_label(input));
    }
    result.diagnostics = read_diagnostics(input);
    return result;
}

std::string digest_for_corpus(const Corpus& corpus) {
    Writer payload;
    append_corpus_without_digest(payload, corpus);
    return artifact_integrity::sha256_string(
        payload.bytes());
}

double sample_standard_error(
    std::span<const double> samples) {
    if (samples.size() < 2) {
        throw std::invalid_argument(
            "AQ18 paired statistic needs two samples");
    }
    const double mean =
        std::accumulate(
            samples.begin(), samples.end(), 0.0) /
        static_cast<double>(samples.size());
    double sum = 0.0;
    for (const double sample : samples) {
        const double centered = sample - mean;
        sum += centered * centered;
    }
    return std::sqrt(
        sum /
        static_cast<double>(
            samples.size() *
            (samples.size() - 1)));
}

std::vector<std::size_t> exact_max_support(
    std::span<const double> values) {
    if (values.empty()) {
        throw std::invalid_argument(
            "AQ18 exact maximum needs values");
    }
    const double maximum =
        *std::max_element(values.begin(), values.end());
    std::vector<std::size_t> result;
    for (std::size_t index = 0;
         index < values.size(); ++index) {
        if (values[index] == maximum) {
            result.push_back(index);
        }
    }
    return result;
}

struct MetricAccumulator {
    std::size_t roots = 0;
    std::size_t options = 0;
    std::size_t stable_pairs = 0;
    std::size_t differing_roots = 0;
    double weight_mass = 0.0;
    double exact_max_sum = 0.0;
    double stable_pair_sum = 0.0;
    double cross_entropy_sum = 0.0;
    double regret_sum = 0.0;
};

void add_root_metrics(
    MetricAccumulator& metrics,
    const RootLabel& root) {
    ++metrics.roots;
    metrics.options += root.actions.size();
    metrics.weight_mass += root.weight;
    const auto base_support =
        exact_max_support(root.base_aggregate_scores);
    const auto teacher_support =
        exact_max_support(
            root.teacher_aggregate_scores);
    std::size_t overlap = 0;
    double selected_teacher = 0.0;
    for (const std::size_t action : base_support) {
        selected_teacher +=
            root.teacher_aggregate_scores[action];
        overlap +=
            std::find(
                teacher_support.begin(),
                teacher_support.end(), action) !=
                    teacher_support.end()
                ? 1U
                : 0U;
    }
    selected_teacher /=
        static_cast<double>(base_support.size());
    metrics.exact_max_sum +=
        static_cast<double>(overlap) /
        static_cast<double>(base_support.size());
    metrics.regret_sum +=
        root.teacher_aggregate_scores[
            teacher_support.front()] -
        selected_teacher;

    const auto base_distribution =
        learned_soft_priority_target(
            root.base_aggregate_scores);
    if (base_distribution !=
        root.target_probabilities) {
        ++metrics.differing_roots;
    }
    for (std::size_t action = 0;
         action < root.actions.size(); ++action) {
        metrics.cross_entropy_sum -=
            root.target_probabilities[action] *
            std::log(base_distribution[action]);
    }

    for (std::size_t left = 0;
         left < root.actions.size(); ++left) {
        for (std::size_t right = left + 1;
             right < root.actions.size(); ++right) {
            std::array<double, kWorlds> differences{};
            for (std::size_t world = 0;
                 world < kWorlds; ++world) {
                differences[world] =
                    root.teacher_q_samples[left][world] -
                    root.teacher_q_samples[right][world];
            }
            const double teacher_delta =
                root.teacher_aggregate_scores[left] -
                root.teacher_aggregate_scores[right];
            const double uncertainty =
                kNormal95CriticalValue *
                sample_standard_error(differences);
            if (std::abs(teacher_delta) <
                    kStablePairMinimumDelta ||
                std::abs(teacher_delta) <=
                    uncertainty) {
                continue;
            }
            ++metrics.stable_pairs;
            const double base_delta =
                root.base_aggregate_scores[left] -
                root.base_aggregate_scores[right];
            if (base_delta == 0.0) {
                metrics.stable_pair_sum += 0.5;
            } else if (
                (base_delta > 0.0) ==
                (teacher_delta > 0.0)) {
                metrics.stable_pair_sum += 1.0;
            }
        }
    }
}

CellMetrics finish_metrics(
    const MetricAccumulator& source,
    density::Split split, DeckId deck,
    priority::WidthStratum stratum) {
    if (source.roots == 0) {
        throw std::invalid_argument(
            "AQ18 metric cell is empty");
    }
    const double roots =
        static_cast<double>(source.roots);
    return {
        .split = split,
        .owner_deck = deck,
        .width_stratum = stratum,
        .roots = source.roots,
        .options = source.options,
        .stable_pairs = source.stable_pairs,
        .differing_roots = source.differing_roots,
        .weight_mass = source.weight_mass,
        .exact_max_agreement =
            source.exact_max_sum / roots,
        .stable_pair_agreement =
            source.stable_pairs == 0
                ? 0.0
                : source.stable_pair_sum /
                      static_cast<double>(
                          source.stable_pairs),
        .listwise_cross_entropy =
            source.cross_entropy_sum / roots,
        .teacher_regret =
            source.regret_sum / roots,
    };
}

double exact_priority_aggregate(
    std::span<const double> shallow,
    std::span<const double> continuation,
    bool blend_shallow) {
    if (shallow.size() != continuation.size() ||
        continuation.empty()) {
        throw std::invalid_argument(
            "AQ18 aggregate sample shape is invalid");
    }
    double score = 0.0;
    if (blend_shallow) {
        for (const double value : shallow) {
            score += value;
        }
        score /= static_cast<double>(shallow.size());
    }
    for (const double value : continuation) {
        score += value;
    }
    score /=
        static_cast<double>(
            continuation.size() +
            (blend_shallow ? 1U : 0U));
    return score;
}

SearchAccounting accounting_from_samples(
    const LearnedActionSamples& samples,
    bool include_inner_matrices) {
    return {
        .sampled_worlds = samples.sampled_worlds,
        .rollout_evaluations =
            samples.rollout_evaluations,
        .terminal_evaluations =
            samples.terminal_evaluations,
        .bootstrapped_evaluations =
            samples.bootstrapped_evaluations,
        .inner_rollout_evaluations =
            samples.inner_rollout_evaluations,
        .inner_search_invocations =
            samples.inner_search_invocations,
        .inner_search_max_depth =
            samples.inner_search_max_depth,
        .inner_rollout_evaluations_by_cell =
            include_inner_matrices
                ? samples
                      .priority_inner_rollout_evaluations
                : std::vector<
                      std::vector<std::size_t>>{},
        .inner_search_invocations_by_cell =
            include_inner_matrices
                ? samples
                      .priority_inner_search_invocations
                : std::vector<
                      std::vector<std::size_t>>{},
        .inner_search_max_depth_by_cell =
            include_inner_matrices
                ? samples
                      .priority_inner_search_max_depth
                : std::vector<
                      std::vector<std::size_t>>{},
    };
}

void require_priority_sample_contract(
    const LearnedActionSamples& samples,
    bool teacher) {
    if (!samples.settled_boundary_samples.empty() ||
        !samples.exact_combat_pure_chump_flags.empty() ||
        !samples.exact_combat_bound_fallback_flags.empty() ||
        !samples.combat_inner_rollout_evaluations.empty() ||
        !samples.combat_inner_search_invocations.empty() ||
        !samples.combat_inner_search_max_depth.empty() ||
        !samples.priority_h0_boundaries.empty() ||
        (!teacher &&
         (!samples
               .priority_inner_rollout_evaluations.empty() ||
          !samples
               .priority_inner_search_invocations.empty() ||
          !samples
               .priority_inner_search_max_depth.empty()))) {
        throw std::invalid_argument(
            "AQ18 sampler returned an unlicensed component");
    }
}

void validate_accounting(
    const SearchAccounting& accounting,
    std::size_t actions, bool teacher) {
    if (accounting.sampled_worlds != kWorlds ||
        accounting.rollout_evaluations !=
            actions * kWorlds ||
        accounting.terminal_evaluations +
                accounting.bootstrapped_evaluations !=
            accounting.rollout_evaluations) {
        throw std::invalid_argument(
            "AQ18 outer accounting drifted");
    }
    if (!teacher) {
        if (accounting.inner_rollout_evaluations != 0 ||
            accounting.inner_search_invocations != 0 ||
            accounting.inner_search_max_depth != 0 ||
            !accounting
                 .inner_rollout_evaluations_by_cell
                 .empty() ||
            !accounting
                 .inner_search_invocations_by_cell
                 .empty() ||
            !accounting
                 .inner_search_max_depth_by_cell
                 .empty()) {
            throw std::invalid_argument(
                "AQ18 base arm performed nested search");
        }
        return;
    }
    const auto& rollouts =
        accounting.inner_rollout_evaluations_by_cell;
    const auto& invocations =
        accounting.inner_search_invocations_by_cell;
    const auto& depths =
        accounting.inner_search_max_depth_by_cell;
    if (rollouts.size() != actions ||
        invocations.size() != actions ||
        depths.size() != actions ||
        accounting.inner_search_max_depth > 1) {
        throw std::invalid_argument(
            "AQ18 teacher inner matrix shape drifted");
    }
    std::size_t rollout_total = 0;
    std::size_t invocation_total = 0;
    std::size_t maximum_depth = 0;
    for (std::size_t action = 0;
         action < actions; ++action) {
        if (rollouts[action].size() != kWorlds ||
            invocations[action].size() != kWorlds ||
            depths[action].size() != kWorlds) {
            throw std::invalid_argument(
                "AQ18 teacher inner row shape drifted");
        }
        for (std::size_t world = 0;
             world < kWorlds; ++world) {
            const bool inactive =
                rollouts[action][world] == 0 &&
                invocations[action][world] == 0 &&
                depths[action][world] == 0;
            const bool active =
                rollouts[action][world] != 0 &&
                invocations[action][world] != 0 &&
                rollouts[action][world] %
                        aq4::kInnerWorlds ==
                    0 &&
                depths[action][world] == 1;
            if (!inactive && !active) {
                throw std::invalid_argument(
                    "AQ18 teacher inner cell drifted");
            }
            checked_add(
                rollout_total,
                rollouts[action][world],
                "teacher inner rollout");
            checked_add(
                invocation_total,
                invocations[action][world],
                "teacher inner invocation");
            maximum_depth =
                std::max(
                    maximum_depth,
                    depths[action][world]);
        }
    }
    if (rollout_total !=
            accounting.inner_rollout_evaluations ||
        invocation_total !=
            accounting.inner_search_invocations ||
        maximum_depth !=
            accounting.inner_search_max_depth) {
        throw std::invalid_argument(
            "AQ18 teacher inner accounting does not cross-sum");
    }
}

void validate_sample_arm(
    const RootLabel& root,
    bool teacher) {
    const auto& q =
        teacher
            ? root.teacher_q_samples
            : root.base_q_samples;
    const auto& flags =
        teacher
            ? root.teacher_terminal_flags
            : root.base_terminal_flags;
    const auto& shallow =
        teacher
            ? root.teacher_shallow_prior_samples
            : root.base_shallow_prior_samples;
    const auto& continuation =
        teacher
            ? root.teacher_continuation_samples
            : root.base_continuation_samples;
    const auto& aggregate =
        teacher
            ? root.teacher_aggregate_scores
            : root.base_aggregate_scores;
    const auto& accounting =
        teacher
            ? root.teacher_accounting
            : root.base_accounting;
    const std::size_t actions = root.actions.size();
    if (q.size() != actions ||
        flags.size() != actions ||
        shallow.size() != actions ||
        continuation.size() != actions ||
        aggregate.size() != actions) {
        throw std::invalid_argument(
            "AQ18 sample arm action shape drifted");
    }
    std::size_t terminal_count = 0;
    for (std::size_t action = 0;
         action < actions; ++action) {
        if (q[action].size() != kWorlds ||
            flags[action].size() != kWorlds ||
            shallow[action].size() != kWorlds ||
            continuation[action].size() != kWorlds ||
            !std::all_of(
                q[action].begin(), q[action].end(),
                finite_probability) ||
            !std::all_of(
                shallow[action].begin(),
                shallow[action].end(),
                finite_probability) ||
            !std::all_of(
                continuation[action].begin(),
                continuation[action].end(),
                finite_probability) ||
            !finite_probability(aggregate[action])) {
            throw std::invalid_argument(
                "AQ18 sample arm value shape drifted");
        }
        for (std::size_t world = 0;
             world < kWorlds; ++world) {
            if (flags[action][world] > 1) {
                throw std::invalid_argument(
                    "AQ18 terminal flag drifted");
            }
            terminal_count += flags[action][world];
            const double expected =
                teacher
                    ? continuation[action][world]
                    : (shallow[action][world] +
                       static_cast<double>(kWorlds) *
                           continuation[action][world]) /
                          static_cast<double>(
                              kWorlds + 1);
            if (!bit_equal(
                    q[action][world], expected)) {
                throw std::invalid_argument(
                    "AQ18 Q component identity drifted");
            }
        }
        const double expected_aggregate =
            exact_priority_aggregate(
                shallow[action],
                continuation[action], !teacher);
        if (!bit_equal(
                aggregate[action],
                expected_aggregate)) {
            throw std::invalid_argument(
                "AQ18 aggregate division order drifted");
        }
    }
    if (terminal_count !=
        accounting.terminal_evaluations) {
        throw std::invalid_argument(
            "AQ18 terminal flags do not cross-sum");
    }
    validate_accounting(accounting, actions, teacher);
}

std::string opaque_physical_group(
    const density::RootCoordinate& coordinate) {
    Writer payload(4096);
    payload.text(kProjectedPhysicalSchema);
    payload.text(kRequiredSourceManifest);
    payload.u64(
        static_cast<std::uint64_t>(coordinate.split));
    payload.size(coordinate.block_index);
    payload.size(coordinate.schedule_index);
    payload.size(coordinate.pairing_index);
    payload.u64(coordinate.game_seed);
    payload.size(coordinate.starting_player);
    for (const DeckId deck : coordinate.seat_decks) {
        payload.u64(
            static_cast<std::uint64_t>(deck));
    }
    return artifact_integrity::sha256_string(
        payload.bytes());
}

std::string opaque_actor_group(
    std::string_view physical,
    const density::RootCoordinate& coordinate) {
    Writer payload(1024);
    payload.text(kProjectedActorSchema);
    payload.text(physical);
    payload.size(coordinate.actor);
    return artifact_integrity::sha256_string(
        payload.bytes());
}

void validate_projected_root(
    const ProjectedRoot& root) {
    static_cast<void>(
        density::split_index(root.split));
    static_cast<void>(deck_index(root.owner_deck));
    static_cast<void>(
        stratum_index(root.width_stratum));
    if (!canonical_sha256(root.stable_root_id) ||
        root.selection_key !=
            priority::selection_key(
                kRequiredSourceManifest,
                root.stable_root_id) ||
        !canonical_sha256(
            root.information_action_fingerprint) ||
        !canonical_sha256(root.physical_game_group) ||
        !canonical_sha256(root.actor_game_group) ||
        root.label_seed == 0) {
        throw std::invalid_argument(
            "AQ18 projected root identity drifted");
    }
}

std::vector<const RootLabel*> all_roots(
    std::span<const RootLabel> train,
    std::span<const RootLabel> dev) {
    std::vector<const RootLabel*> result;
    result.reserve(train.size() + dev.size());
    for (const RootLabel& root : train) {
        result.push_back(&root);
    }
    for (const RootLabel& root : dev) {
        result.push_back(&root);
    }
    return result;
}

std::vector<AliasDiagnostic> make_alias_diagnostics(
    std::span<const RootLabel> train,
    std::span<const RootLabel> dev,
    std::span<const priority::AliasGroup> aliases) {
    std::map<std::string, const RootLabel*> by_id;
    for (const RootLabel* root :
         all_roots(train, dev)) {
        if (!by_id.emplace(
                 root->identity.stable_root_id,
                 root)
                 .second) {
            throw std::invalid_argument(
                "AQ18 duplicate labeled root");
        }
    }
    std::vector<AliasDiagnostic> result;
    for (const priority::AliasGroup& group :
         aliases) {
        const auto found =
            by_id.find(group.stable_root_id);
        if (found == by_id.end()) {
            throw std::invalid_argument(
                "AQ18 alias root is unlabeled");
        }
        const RootLabel& root = *found->second;
        if (root.identity.split != group.split ||
            root.identity.owner_deck !=
                group.owner_deck ||
            root.identity.width_stratum !=
                group.width_stratum) {
            throw std::invalid_argument(
                "AQ18 alias root identity drifted");
        }
        std::vector<std::size_t> indices;
        indices.reserve(
            group.action_descriptors.size());
        for (const std::string& descriptor :
             group.action_descriptors) {
            const auto action =
                std::find(
                    root.action_descriptors.begin(),
                    root.action_descriptors.end(),
                    descriptor);
            if (action ==
                root.action_descriptors.end()) {
                throw std::invalid_argument(
                    "AQ18 alias descriptor is missing");
            }
            indices.push_back(
                static_cast<std::size_t>(
                    action -
                    root.action_descriptors.begin()));
        }
        for (std::size_t left = 0;
             left < indices.size(); ++left) {
            for (std::size_t right = left + 1;
                 right < indices.size(); ++right) {
                const std::size_t left_action =
                    indices[left];
                const std::size_t right_action =
                    indices[right];
                if (!rows_bit_identical(
                        root.option_rows[left_action],
                        root.option_rows[right_action])) {
                    throw std::invalid_argument(
                        "AQ18 alias feature rows differ");
                }
                AliasDiagnostic diagnostic{
                    .split = group.split,
                    .owner_deck = group.owner_deck,
                    .width_stratum =
                        group.width_stratum,
                    .stable_root_id =
                        group.stable_root_id,
                    .left_action = left_action,
                    .right_action = right_action,
                };
                for (std::size_t world = 0;
                     world < kWorlds; ++world) {
                    const double left_correction =
                        root.teacher_q_samples
                                [left_action][world] -
                        root.base_q_samples
                                [left_action][world];
                    const double right_correction =
                        root.teacher_q_samples
                                [right_action][world] -
                        root.base_q_samples
                                [right_action][world];
                    diagnostic.paired_corrections[world] =
                        left_correction -
                        right_correction;
                }
                diagnostic.paired_mean =
                    std::accumulate(
                        diagnostic
                            .paired_corrections.begin(),
                        diagnostic
                            .paired_corrections.end(),
                        0.0) /
                    static_cast<double>(kWorlds);
                diagnostic.paired_standard_error =
                    sample_standard_error(
                        diagnostic
                            .paired_corrections);
                diagnostic.correction_gap =
                    (root.teacher_aggregate_scores
                         [left_action] -
                     root.base_aggregate_scores
                         [left_action]) -
                    (root.teacher_aggregate_scores
                         [right_action] -
                     root.base_aggregate_scores
                         [right_action]);
                diagnostic.target_probability_gap =
                    root.target_probabilities
                            [left_action] -
                    root.target_probabilities
                            [right_action];
                const double interval =
                    kNormal95CriticalValue *
                    diagnostic
                        .paired_standard_error;
                diagnostic.material_conflict =
                    std::abs(
                        diagnostic.correction_gap) >=
                        kMaterialCorrectionGap &&
                    (diagnostic.paired_mean - interval >
                         0.0 ||
                     diagnostic.paired_mean + interval <
                         0.0);
                result.push_back(
                    std::move(diagnostic));
            }
        }
    }
    return result;
}

void apply_alias_summaries(Diagnostics& diagnostics) {
    diagnostics.material_alias_conflicts = 0;
    diagnostics.maximum_absolute_alias_correction_gap =
        0.0;
    for (CellMetrics& cell : diagnostics.cells) {
        cell.alias_pairs = 0;
        cell.material_alias_conflicts = 0;
        cell.maximum_absolute_alias_correction_gap =
            0.0;
    }
    for (const AliasDiagnostic& alias :
         diagnostics.aliases) {
        CellMetrics& cell =
            diagnostics.cells[priority::cell_index(
                alias.split, alias.owner_deck,
                alias.width_stratum)];
        ++cell.alias_pairs;
        if (alias.material_conflict) {
            ++diagnostics.material_alias_conflicts;
            ++cell.material_alias_conflicts;
        }
        const double absolute_gap =
            std::abs(alias.correction_gap);
        cell.maximum_absolute_alias_correction_gap =
            std::max(
                cell.maximum_absolute_alias_correction_gap,
                absolute_gap);
        diagnostics
            .maximum_absolute_alias_correction_gap =
            std::max(
                diagnostics
                    .maximum_absolute_alias_correction_gap,
                absolute_gap);
    }
}

Diagnostics evaluate_diagnostics_impl(
    std::span<const RootLabel> train,
    std::span<const RootLabel> dev,
    std::span<const priority::AliasGroup> aliases) {
    std::array<MetricAccumulator, kExpectedCells>
        cells;
    std::array<
        std::array<MetricAccumulator, kDeckCount>, 2>
        decks;
    const auto consume =
        [&](std::span<const RootLabel> roots,
            density::Split expected_split) {
            for (const RootLabel& root : roots) {
                if (root.identity.split != expected_split) {
                    throw std::invalid_argument(
                        "AQ18 metric split drifted");
                }
                const std::size_t cell =
                    priority::cell_index(
                        root.identity.split,
                        root.identity.owner_deck,
                        root.identity.width_stratum);
                add_root_metrics(cells[cell], root);
                add_root_metrics(
                    decks[density::split_index(
                              root.identity.split)]
                         [deck_index(
                             root.identity.owner_deck)],
                    root);
            }
        };
    consume(train, density::Split::Train);
    consume(dev, density::Split::Dev);

    Diagnostics result;
    for (std::size_t split = 0; split < 2; ++split) {
        const density::Split split_value =
            static_cast<density::Split>(split);
        for (std::size_t deck = 0;
             deck < kDeckCount; ++deck) {
            const DeckId deck_value =
                static_cast<DeckId>(deck);
            const MetricAccumulator& deck_source =
                decks[split][deck];
            if (deck_source.roots == 0) {
                throw std::invalid_argument(
                    "AQ18 deck metric cell is empty: split=" +
                    std::string(split_name(split_value)) +
                    " deck=" +
                    std::string(deck_name(deck_value)));
            }
            result.deck_signal[
                split * kDeckCount + deck] =
                deck_source.regret_sum > 0.0 &&
                deck_source.differing_roots > 0;
            const CellMetrics deck_metrics =
                finish_metrics(
                    deck_source, split_value,
                    deck_value,
                    priority::WidthStratum::B2);
            double* exact = split == 0
                ? &result
                       .equal_deck_train_exact_max_agreement
                : &result
                       .equal_deck_dev_exact_max_agreement;
            double* stable = split == 0
                ? &result
                       .equal_deck_train_stable_pair_agreement
                : &result
                       .equal_deck_dev_stable_pair_agreement;
            double* cross_entropy = split == 0
                ? &result
                       .equal_deck_train_listwise_cross_entropy
                : &result
                       .equal_deck_dev_listwise_cross_entropy;
            double* regret = split == 0
                ? &result
                       .equal_deck_train_teacher_regret
                : &result
                       .equal_deck_dev_teacher_regret;
            *exact +=
                deck_metrics.exact_max_agreement /
                static_cast<double>(kDeckCount);
            *stable +=
                deck_metrics.stable_pair_agreement /
                static_cast<double>(kDeckCount);
            *cross_entropy +=
                deck_metrics.listwise_cross_entropy /
                static_cast<double>(kDeckCount);
            *regret +=
                deck_metrics.teacher_regret /
                static_cast<double>(kDeckCount);

            for (std::size_t stratum = 0;
                 stratum < priority::kWidthStrata;
                 ++stratum) {
                const auto stratum_value =
                    static_cast<
                        priority::WidthStratum>(
                        stratum);
                const std::size_t cell =
                    priority::cell_index(
                        split_value, deck_value,
                        stratum_value);
                result.cells[cell] =
                    finish_metrics(
                        cells[cell], split_value,
                        deck_value, stratum_value);
            }
        }
    }
    result.aliases =
        make_alias_diagnostics(
            train, dev, aliases);
    apply_alias_summaries(result);
    return result;
}

void validate_alias_rows(
    std::span<const RootLabel> train,
    std::span<const RootLabel> dev,
    const Diagnostics& diagnostics) {
    std::map<std::string, const RootLabel*> by_id;
    for (const RootLabel* root :
         all_roots(train, dev)) {
        by_id.emplace(
            root->identity.stable_root_id, root);
    }
    std::set<
        std::tuple<std::string, std::size_t, std::size_t>>
        identities;
    for (const AliasDiagnostic& alias :
         diagnostics.aliases) {
        const auto found =
            by_id.find(alias.stable_root_id);
        if (found == by_id.end() ||
            alias.left_action >=
                found->second->actions.size() ||
            alias.right_action >=
                found->second->actions.size() ||
            alias.left_action >= alias.right_action ||
            !identities.emplace(
                 alias.stable_root_id,
                 alias.left_action,
                 alias.right_action)
                 .second) {
            throw std::invalid_argument(
                "AQ18 cached alias identity drifted");
        }
        const RootLabel& root = *found->second;
        const priority::AliasGroup group{
            .split = alias.split,
            .owner_deck = alias.owner_deck,
            .width_stratum = alias.width_stratum,
            .stable_root_id = alias.stable_root_id,
            .action_descriptors = {
                root.action_descriptors[
                    alias.left_action],
                root.action_descriptors[
                    alias.right_action],
            },
        };
        const auto recomputed =
            make_alias_diagnostics(
                train, dev,
                std::span<const priority::AliasGroup>(
                    &group, 1));
        if (recomputed.size() != 1 ||
            !aliases_bit_identical(
                recomputed.front(), alias)) {
            throw std::invalid_argument(
                "AQ18 cached alias statistic drifted");
        }
    }
    Diagnostics summaries = diagnostics;
    apply_alias_summaries(summaries);
    if (summaries.material_alias_conflicts !=
            diagnostics.material_alias_conflicts ||
        !bit_equal(
            summaries
                .maximum_absolute_alias_correction_gap,
            diagnostics
                .maximum_absolute_alias_correction_gap)) {
        throw std::invalid_argument(
            "AQ18 alias summary drifted");
    }
    for (std::size_t index = 0;
         index < diagnostics.cells.size(); ++index) {
        const CellMetrics& expected =
            summaries.cells[index];
        const CellMetrics& actual =
            diagnostics.cells[index];
        if (expected.alias_pairs !=
                actual.alias_pairs ||
            expected.material_alias_conflicts !=
                actual.material_alias_conflicts ||
            !bit_equal(
                expected
                    .maximum_absolute_alias_correction_gap,
                actual
                    .maximum_absolute_alias_correction_gap)) {
            throw std::invalid_argument(
                "AQ18 alias cell summary drifted");
        }
    }
}

void validate_corpus_impl(
    const Corpus& corpus, bool frozen) {
    if (!canonical_sha256(
            corpus.parent_artifact_sha256) ||
        !canonical_sha256(corpus.parent_fingerprint) ||
        !canonical_sha256(
            corpus.parent_components.critic) ||
        !canonical_sha256(
            corpus.parent_components.priority) ||
        !canonical_sha256(
            corpus.parent_components.attack) ||
        !canonical_sha256(
            corpus.parent_components.block) ||
        !canonical_sha256(
            corpus.parent_components.damage_order) ||
        corpus.source_manifest_hash !=
            kRequiredSourceManifest ||
        corpus.selection_manifest_hash !=
            kRequiredSelectionManifest ||
        corpus.preflight_digest !=
            kRequiredPreflightDigest ||
        !search_configs_bit_identical(
            corpus.base_search_template,
            base_search_config(0)) ||
        !search_configs_bit_identical(
            corpus.teacher_search_template,
            teacher_search_config(0)) ||
        !canonical_sha256(corpus.digest) ||
        corpus.digest != digest_for_corpus(corpus)) {
        throw std::invalid_argument(
            "AQ18 corpus identity drifted");
    }
    if (frozen &&
        (corpus.parent_artifact_sha256 !=
             kParentArtifactSha256 ||
         corpus.parent_fingerprint !=
             kRequiredParentFingerprint ||
         corpus.train.size() !=
             kExpectedTrainRoots ||
         corpus.dev.size() !=
             kExpectedDevRoots ||
         corpus.diagnostics.aliases.size() !=
             kExpectedAliasPairs)) {
        throw std::invalid_argument(
            "AQ18 frozen corpus census drifted");
    }

    std::set<std::string> roots;
    std::set<std::string> selection_keys;
    std::set<std::uint64_t> label_seeds;
    std::set<std::string> physical_groups;
    std::array<std::size_t, kExpectedCells>
        cell_roots{};
    std::array<std::size_t, kExpectedCells>
        cell_options{};
    std::array<std::set<std::string>, kExpectedCells>
        cell_actor_groups;
    std::map<std::string, std::size_t>
        actor_group_counts;
    std::size_t train_options = 0;
    std::size_t dev_options = 0;
    const auto consume =
        [&](std::span<const RootLabel> examples,
            density::Split split,
            std::size_t& options) {
            for (const RootLabel& root : examples) {
                validate_root_label(root);
                if (root.identity.split != split ||
                    !roots.insert(
                         root.identity.stable_root_id)
                         .second ||
                    !selection_keys.insert(
                         root.identity.selection_key)
                         .second ||
                    !label_seeds.insert(
                         root.identity.label_seed)
                         .second) {
                    throw std::invalid_argument(
                        "AQ18 corpus root order/identity drifted");
                }
                physical_groups.insert(
                    root.identity.physical_game_group);
                const std::size_t cell =
                    priority::cell_index(
                        root.identity.split,
                        root.identity.owner_deck,
                        root.identity.width_stratum);
                ++cell_roots[cell];
                checked_add(
                    cell_options[cell],
                    root.actions.size(),
                    "cell option count");
                cell_actor_groups[cell].insert(
                    root.identity.actor_game_group);
                ++actor_group_counts[
                    root.identity.actor_game_group];
                checked_add(
                    options, root.actions.size(),
                    "corpus option count");
            }
        };
    consume(
        corpus.train, density::Split::Train,
        train_options);
    consume(
        corpus.dev, density::Split::Dev,
        dev_options);
    std::size_t maximum_actor_group = 0;
    for (const auto& [unused, count] :
         actor_group_counts) {
        static_cast<void>(unused);
        maximum_actor_group =
            std::max(maximum_actor_group, count);
    }
    if (frozen &&
        (train_options != kExpectedTrainOptions ||
         dev_options != kExpectedDevOptions ||
         physical_groups.size() != 120 ||
         label_seeds.size() !=
             kExpectedTrainRoots +
                 kExpectedDevRoots ||
         maximum_actor_group == 0 ||
         maximum_actor_group >
             kFrozenActorGameCap)) {
        throw std::invalid_argument(
            "AQ18 frozen option/group census drifted");
    }
    if (frozen) {
        for (std::size_t cell = 0;
             cell < kExpectedCells; ++cell) {
            const std::size_t expected_roots =
                cell <
                        kDeckCount *
                            priority::kWidthStrata
                    ? priority::kTrainRootsPerCell
                    : priority::kDevRootsPerCell;
            double expected_weight = 0.0;
            const double root_weight =
                cell <
                        kDeckCount *
                            priority::kWidthStrata
                    ? kTrainRootWeight
                    : kDevRootWeight;
            for (std::size_t root = 0;
                 root < expected_roots; ++root) {
                expected_weight += root_weight;
            }
            const CellMetrics& metrics =
                corpus.diagnostics.cells[cell];
            if (cell_roots[cell] != expected_roots ||
                cell_options[cell] !=
                    kExpectedCellOptions[cell] ||
                cell_actor_groups[cell].size() !=
                    expected_roots ||
                metrics.roots != expected_roots ||
                metrics.options !=
                    kExpectedCellOptions[cell] ||
                metrics.alias_pairs !=
                    kExpectedCellAliasPairs[cell] ||
                !bit_equal(
                    metrics.weight_mass,
                    expected_weight)) {
                throw std::invalid_argument(
                    "AQ18 frozen cell census drifted");
            }
        }
    }

    Diagnostics recomputed =
        evaluate_diagnostics_impl(
            corpus.train, corpus.dev, {});
    recomputed.aliases = corpus.diagnostics.aliases;
    apply_alias_summaries(recomputed);
    if (!diagnostics_bit_identical(
            recomputed, corpus.diagnostics)) {
        throw std::invalid_argument(
            "AQ18 cached cell diagnostics drifted");
    }
    validate_alias_rows(
        corpus.train, corpus.dev,
        corpus.diagnostics);
    if (frozen &&
        !std::all_of(
            corpus.diagnostics.deck_signal.begin(),
            corpus.diagnostics.deck_signal.end(),
            [](bool signal) { return signal; })) {
        throw std::runtime_error(
            "AQ18 deep teacher signal hypothesis failed");
    }
}

std::string encode_cache_impl(
    const Corpus& corpus, bool frozen) {
    validate_corpus_impl(corpus, frozen);
    Writer payload;
    append_corpus_without_digest(payload, corpus);
    payload.text(corpus.digest);
    const std::string payload_bytes = payload.take();
    Writer envelope(
        kMaximumCacheBytes + 4096U);
    envelope.raw(std::string_view(
        kCacheMagic.data(), kCacheMagic.size()));
    envelope.u64(kCacheVersion);
    envelope.size(payload_bytes.size());
    envelope.text(
        artifact_integrity::sha256_string(
            payload_bytes));
    envelope.raw(payload_bytes);
    return envelope.take();
}

Corpus decode_cache_impl(
    std::string_view bytes, bool frozen) {
    if (bytes.size() >
        kMaximumCacheBytes + 4096U) {
        throw std::length_error(
            "AQ18 cache file exceeds its bound");
    }
    Reader envelope(bytes);
    if (envelope.raw(
            kCacheMagic.size(), "cache magic") !=
            std::string_view(
                kCacheMagic.data(),
                kCacheMagic.size()) ||
        envelope.u64("envelope version") !=
            kCacheVersion) {
        throw std::runtime_error(
            "AQ18 cache magic/version is invalid");
    }
    const std::size_t payload_size =
        envelope.size(
            kMaximumCacheBytes, "payload size");
    const std::string payload_hash =
        envelope.text("payload digest", 64);
    const std::string_view payload =
        envelope.raw(payload_size, "payload");
    envelope.finish();
    if (!canonical_sha256(payload_hash) ||
        payload_hash !=
            artifact_integrity::sha256_string(
                payload)) {
        throw std::runtime_error(
            "AQ18 cache payload digest drifted");
    }
    Reader input(payload);
    Corpus corpus = read_corpus_without_digest(input);
    corpus.digest = input.text("corpus digest", 64);
    input.finish();
    validate_corpus_impl(corpus, frozen);
    return corpus;
}

std::string read_file(
    const std::filesystem::path& path) {
    if (path.empty() || path.filename().empty()) {
        throw std::invalid_argument(
            "AQ18 cache path must name a file");
    }
    const auto snapshot =
        artifact_integrity::snapshot_regular_file(path);
    if (snapshot.byte_size >
        kMaximumCacheBytes + 4096U) {
        throw std::length_error(
            "AQ18 cache file exceeds its bound");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(
            "cannot open AQ18 cache");
    }
    std::string result(
        static_cast<std::size_t>(
            snapshot.byte_size),
        '\0');
    if (!result.empty()) {
        input.read(
            result.data(),
            static_cast<std::streamsize>(
                result.size()));
    }
    if (input.gcount() !=
            static_cast<std::streamsize>(
                result.size()) ||
        input.peek() !=
            std::char_traits<char>::eof()) {
        throw std::runtime_error(
            "AQ18 cache changed while reading");
    }
    const auto after =
        artifact_integrity::snapshot_regular_file(path);
    if (after != snapshot) {
        throw std::runtime_error(
            "AQ18 cache changed while reading");
    }
    return result;
}

void write_bytes_atomic_no_replace_impl(
    const std::filesystem::path& path,
    std::string_view bytes) {
    if (path.empty() || path.filename().empty()) {
        throw std::invalid_argument(
            "AQ18 cache path must name a file");
    }
    const std::filesystem::path directory =
        path.has_parent_path()
            ? path.parent_path()
            : std::filesystem::path(".");
    std::error_code error;
    std::filesystem::create_directories(
        directory, error);
    if (error) {
        throw std::runtime_error(
            "cannot create AQ18 cache directory: " +
            error.message());
    }

    static std::atomic<std::uint64_t> counter{0};
    std::filesystem::path temporary;
    int descriptor = -1;
    for (std::size_t attempt = 0;
         attempt < 128; ++attempt) {
        temporary =
            directory /
            (path.filename().string() + ".tmp." +
             std::to_string(
                 static_cast<unsigned long long>(
                     ::getpid())) +
             "." +
             std::to_string(
                 counter.fetch_add(
                     1,
                     std::memory_order_relaxed)));
        descriptor = ::open(
            temporary.c_str(),
            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
            0644);
        if (descriptor >= 0) {
            break;
        }
        if (errno != EEXIST) {
            throw std::runtime_error(
                "cannot create AQ18 cache temporary: " +
                std::string(std::strerror(errno)));
        }
    }
    if (descriptor < 0) {
        throw std::runtime_error(
            "cannot reserve AQ18 cache temporary");
    }
    const auto cleanup = [&] {
        if (descriptor >= 0) {
            static_cast<void>(::close(descriptor));
            descriptor = -1;
        }
        static_cast<void>(::unlink(temporary.c_str()));
    };
    std::size_t position = 0;
    while (position < bytes.size()) {
        const ssize_t written =
            ::write(
                descriptor,
                bytes.data() + position,
                bytes.size() - position);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            const std::string detail =
                std::strerror(errno);
            cleanup();
            throw std::runtime_error(
                "cannot write AQ18 cache temporary: " +
                detail);
        }
        if (written == 0) {
            cleanup();
            throw std::runtime_error(
                "AQ18 cache write made no progress");
        }
        position +=
            static_cast<std::size_t>(written);
    }
    if (::fsync(descriptor) != 0) {
        const std::string detail =
            std::strerror(errno);
        cleanup();
        throw std::runtime_error(
            "cannot sync AQ18 cache temporary: " +
            detail);
    }
    if (::close(descriptor) != 0) {
        const std::string detail =
            std::strerror(errno);
        descriptor = -1;
        static_cast<void>(
            ::unlink(temporary.c_str()));
        throw std::runtime_error(
            "cannot close AQ18 cache temporary: " +
            detail);
    }
    descriptor = -1;
    const int directory_descriptor =
        ::open(
            directory.c_str(),
            O_RDONLY | O_CLOEXEC);
    if (directory_descriptor < 0) {
        const std::string detail =
            std::strerror(errno);
        static_cast<void>(
            ::unlink(temporary.c_str()));
        throw std::runtime_error(
            "cannot open AQ18 cache directory: " +
            detail);
    }
    if (::link(
            temporary.c_str(), path.c_str()) != 0) {
        const std::string detail =
            std::strerror(errno);
        static_cast<void>(
            ::close(directory_descriptor));
        static_cast<void>(
            ::unlink(temporary.c_str()));
        throw std::runtime_error(
            "cannot atomically publish AQ18 cache: " +
            detail);
    }
    if (::unlink(temporary.c_str()) != 0) {
        const std::string detail =
            std::strerror(errno);
        static_cast<void>(
            ::close(directory_descriptor));
        throw std::runtime_error(
            "cannot retire AQ18 cache temporary: " +
            detail);
    }
    if (::fsync(directory_descriptor) != 0) {
        const std::string detail =
            std::strerror(errno);
        static_cast<void>(
            ::close(directory_descriptor));
        throw std::runtime_error(
            "cannot sync AQ18 cache directory: " +
            detail);
    }
    if (::close(directory_descriptor) != 0) {
        throw std::runtime_error(
            "cannot close AQ18 cache directory");
    }
}

} // namespace

std::optional<Command> parse_command(
    std::span<const std::string_view> arguments) {
    if (arguments.size() == 1 &&
        arguments.front() == "--publish") {
        return Command::Publish;
    }
    return std::nullopt;
}

void print_usage(std::ostream& output) {
    output
        << "Usage: old-school-decision-density-labels "
           "--publish\n";
}

LearnedSearchConfig base_search_config(
    std::uint64_t seed) {
    LearnedSearchConfig result =
        aq4::base_search_config(seed);
    if (result.worlds != kWorlds ||
        result.rollouts_per_world != 1 ||
        result.horizon_turns != 4 ||
        result.continuation_variant !=
            LearnedVariant::ValueSearchChampion ||
        result.value_continuation_epsilon != 0.0 ||
        !result.blend_shallow_prior ||
        result.value_resolved_shallow_prior_weight !=
            0.0 ||
        result.value_priority_residual_weight != 0.0 ||
        result.value_pass_dominance ||
        result.value_continuation_controller !=
            LearnedContinuationController::Legacy ||
        result.evaluation_threads != 4 ||
        result.capture_priority_h0_boundaries ||
        result.value_continuation_search_worlds != 0 ||
        result.value_continuation_search_scope !=
            LearnedContinuationSearchScope::PriorityOnly ||
        result.capture_settled_boundary_samples ||
        result.use_exact_combat_subgame ||
        result.terminal_utility_mode !=
            LearnedTerminalUtilityMode::ExactOutcome) {
        throw std::logic_error(
            "AQ18 exact base recipe drifted");
    }
    return result;
}

LearnedSearchConfig teacher_search_config(
    std::uint64_t seed) {
    LearnedSearchConfig result =
        aq4::teacher_search_config(seed);
    if (result.worlds != kWorlds ||
        result.rollouts_per_world != 1 ||
        result.horizon_turns != 8 ||
        result.continuation_variant !=
            LearnedVariant::ValueSearchChampion ||
        result.value_continuation_epsilon != 0.0 ||
        result.blend_shallow_prior ||
        result.value_resolved_shallow_prior_weight !=
            0.0 ||
        result.value_priority_residual_weight != 0.0 ||
        result.value_pass_dominance ||
        result.value_continuation_controller !=
            LearnedContinuationController::Legacy ||
        result.evaluation_threads != 4 ||
        result.capture_priority_h0_boundaries ||
        result.value_continuation_search_worlds !=
            aq4::kInnerWorlds ||
        result.value_continuation_search_scope !=
            LearnedContinuationSearchScope::PriorityOnly ||
        result.capture_settled_boundary_samples ||
        result.use_exact_combat_subgame ||
        result.terminal_utility_mode !=
            LearnedTerminalUtilityMode::ExactOutcome) {
        throw std::logic_error(
            "AQ18 exact teacher recipe drifted");
    }
    return result;
}

std::uint64_t root_label_seed(
    const density::RootCoordinate& coordinate) {
    if (coordinate.block_index >
            std::numeric_limits<std::uint32_t>::max() ||
        coordinate.schedule_index >
            std::numeric_limits<std::uint32_t>::max() ||
        coordinate.actor >
            std::numeric_limits<std::uint32_t>::max() ||
        coordinate.nontrivial_ordinal >
            std::numeric_limits<std::uint32_t>::max()) {
        throw std::out_of_range(
            "AQ18 label seed coordinate exceeds 32 bits");
    }
    const std::uint64_t game =
        (static_cast<std::uint64_t>(
             coordinate.block_index)
         << 32) |
        static_cast<std::uint64_t>(
            coordinate.schedule_index);
    const std::uint64_t actor_root =
        (static_cast<std::uint64_t>(
             coordinate.actor)
         << 32) |
        static_cast<std::uint64_t>(
            coordinate.nontrivial_ordinal);
    return learned_iteration::derive_seed(
        kLabelSeed,
        learned_iteration::SeedDomain::PrioritySearch,
        density::split_index(coordinate.split),
        game, actor_root);
}

std::string canonical_corpus_digest(
    const Corpus& corpus) {
    return digest_for_corpus(corpus);
}

void validate_root_label(const RootLabel& root) {
    validate_projected_root(root.identity);
    const std::size_t actions = root.actions.size();
    if (actions < 2 ||
        root.action_descriptors.size() != actions ||
        root.option_rows.size() != actions ||
        priority::width_stratum(actions) !=
            root.identity.width_stratum ||
        root.identity.information_action_fingerprint !=
            density::
                canonical_information_action_fingerprint(
                    root.actions,
                    root.action_descriptors,
                    root.option_rows)) {
        throw std::invalid_argument(
            "AQ18 action/feature identity drifted");
    }
    validate_sample_arm(root, false);
    validate_sample_arm(root, true);
    const std::vector<double> expected_targets =
        learned_soft_priority_target(
            root.teacher_aggregate_scores);
    if (!rows_bit_identical(
            root.target_probabilities,
            expected_targets) ||
        !std::all_of(
            root.target_probabilities.begin(),
            root.target_probabilities.end(),
            finite_probability) ||
        !bit_equal(
            root.weight,
            root.identity.split ==
                    density::Split::Train
                ? kTrainRootWeight
                : kDevRootWeight)) {
        throw std::invalid_argument(
            "AQ18 target or weight drifted");
    }
}

void validate_corpus(const Corpus& corpus) {
    validate_corpus_impl(corpus, true);
}

Diagnostics evaluate_diagnostics(
    std::span<const RootLabel> train,
    std::span<const RootLabel> dev,
    std::span<const priority::AliasGroup> aliases) {
    return evaluate_diagnostics_impl(
        train, dev, aliases);
}

std::string encode_cache(const Corpus& corpus) {
    return encode_cache_impl(corpus, true);
}

Corpus decode_cache(std::string_view bytes) {
    return decode_cache_impl(bytes, true);
}

Corpus roundtrip_cache(const Corpus& corpus) {
    const Corpus result =
        decode_cache(encode_cache(corpus));
    if (!corpora_bit_identical(result, corpus)) {
        throw std::runtime_error(
            "AQ18 cache roundtrip drifted");
    }
    return result;
}

void write_cache_atomic_no_replace(
    const std::filesystem::path& path,
    const Corpus& corpus) {
    write_bytes_atomic_no_replace_impl(
        path, encode_cache(corpus));
}

Corpus load_cache(
    const std::filesystem::path& path) {
    return decode_cache(read_file(path));
}

namespace {

RootLabel label_live_root(
    const priority::SelectedRoot& selected,
    std::span<const PriorityAction> actions,
    std::span<const std::vector<double>> option_rows,
    const GameState& state,
    const LearnedDecisionContext& context,
    const std::array<std::vector<CardId>, 2>& decks,
    const std::shared_ptr<const LearnedModel>& parent) {
    const auto& coordinate =
        selected.source_root.coordinate;
    if (!context.valid ||
        context.decision_player != coordinate.actor ||
        selected.source_root.action_descriptors.size() !=
            actions.size() ||
        option_rows.size() != actions.size()) {
        throw std::logic_error(
            "AQ18 live label context drifted");
    }
    const std::uint64_t seed =
        root_label_seed(coordinate);
    const std::vector<PriorityAction> action_vector(
        actions.begin(), actions.end());
    const LearnedActionSamples base =
        learned_priority_action_samples(
            state, decks, coordinate.actor,
            context.sorcery_actions, context.phase,
            context.consecutive_passes, action_vector,
            parent, base_search_config(seed));
    const LearnedActionSamples teacher =
        learned_priority_action_samples(
            state, decks, coordinate.actor,
            context.sorcery_actions, context.phase,
            context.consecutive_passes, action_vector,
            parent, teacher_search_config(seed));
    return testing::make_root_label(
        testing::project_root(selected),
        action_vector,
        selected.source_root.action_descriptors,
        std::vector<std::vector<double>>(
            option_rows.begin(), option_rows.end()),
        base, teacher);
}

bool option_rows_bit_identical(
    std::span<const std::vector<double>> left,
    std::span<const std::vector<double>> right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < left.size(); ++index) {
        if (!rows_bit_identical(
                left[index], right[index])) {
            return false;
        }
    }
    return true;
}

void require_parent_snapshot(
    const artifact_integrity::RegularFileSnapshot&
        expected) {
    const auto actual =
        artifact_integrity::snapshot_regular_file(
            std::string(kParentArtifactPath));
    if (actual != expected ||
        actual.byte_size != kParentArtifactBytes ||
        actual.sha256 != kParentArtifactSha256) {
        throw std::runtime_error(
            "AQ18 parent artifact changed");
    }
}

} // namespace

RunReport run_and_publish(
    std::shared_ptr<const LearnedModel> parent,
    const std::filesystem::path& path) {
    if (!parent ||
        learned_model_fingerprint(parent) !=
            kRequiredParentFingerprint ||
        learned_critic_schema(parent) !=
            LearnedCriticSchema::LegacyStateOnly ||
        path != std::filesystem::path(
                    kProductionCachePath)) {
        throw std::invalid_argument(
            "AQ18 requires exact C16 and fixed cache path");
    }
    std::error_code exists_error;
    const bool destination_exists =
        std::filesystem::exists(path, exists_error);
    if (exists_error || destination_exists) {
        throw std::runtime_error(
            "AQ18 fixed cache destination is not absent");
    }
    const auto parent_before =
        artifact_integrity::snapshot_regular_file(
            std::string(kParentArtifactPath));
    require_parent_snapshot(parent_before);

    const priority::FrozenSelectionReplay frozen =
        priority::reconstruct_frozen_selection(parent);
    if (frozen.source_census.manifest_hash !=
            kRequiredSourceManifest ||
        frozen.manifest.manifest_hash !=
            kRequiredSelectionManifest ||
        frozen.selected_population.size() !=
            kExpectedTrainRoots +
                kExpectedDevRoots ||
        frozen.manifest.selected_roots.size() !=
            frozen.selected_population.size()) {
        throw std::runtime_error(
            "AQ18 frozen source/selection drifted");
    }
    require_parent_snapshot(parent_before);

    const auto preflight =
        action_q_nested_actor_diagnostic::
            run_preflight(
                parent, aq4::preflight_recipe());
    if (!aq4::preflight_exact(preflight) ||
        aq4::canonical_preflight_digest(
            preflight) !=
            kRequiredPreflightDigest) {
        throw std::runtime_error(
            "AQ18 AQ4 control preflight drifted");
    }
    require_parent_snapshot(parent_before);

    std::map<std::string, std::size_t>
        selected_positions;
    std::set<std::uint64_t> seeds;
    for (std::size_t index = 0;
         index < frozen.manifest.selected_roots.size();
         ++index) {
        const auto& selected =
            frozen.manifest.selected_roots[index];
        if (selected.source_root !=
                frozen.selected_population[index]
                    .source_root ||
            !selected_positions.emplace(
                 selected.source_root.stable_root_id,
                 index)
                 .second ||
            !seeds.insert(
                 root_label_seed(
                     selected.source_root.coordinate))
                 .second) {
            throw std::runtime_error(
                "AQ18 selected seed/order drifted");
        }
    }
    if (seeds.size() !=
        kExpectedTrainRoots + kExpectedDevRoots) {
        throw std::runtime_error(
            "AQ18 root label seeds are not distinct");
    }

    Corpus corpus{
        .parent_artifact_sha256 =
            std::string(kParentArtifactSha256),
        .parent_fingerprint =
            learned_model_fingerprint(parent),
        .parent_components =
            learned_model_component_fingerprints(parent),
        .source_manifest_hash =
            frozen.source_census.manifest_hash,
        .selection_manifest_hash =
            frozen.manifest.manifest_hash,
        .preflight_digest =
            aq4::canonical_preflight_digest(
                preflight),
        .base_search_template =
            base_search_config(0),
        .teacher_search_template =
            teacher_search_config(0),
    };
    std::array<bool, kDeckCount> repeated_decks{};
    std::size_t sentinel_relabels = 0;
    bool hidden_repartition_bit_identical = false;
    std::size_t selected_position = 0;

    const density::AuthenticatedReplayRootVisitor visitor =
        [&](const density::AuthenticatedReplayRootView&
                view) {
            const auto selected_found =
                selected_positions.find(
                    view.manifest.stable_root_id);
            if (selected_found ==
                selected_positions.end()) {
                return;
            }
            if (selected_found->second !=
                    selected_position ||
                selected_position >=
                    frozen.selected_population.size()) {
                throw std::runtime_error(
                    "AQ18 selected replay order drifted");
            }
            const auto& selected =
                frozen.manifest.selected_roots[
                    selected_position];
            const auto& population =
                frozen.selected_population[
                    selected_position];
            if (view.manifest !=
                    selected.source_root ||
                population.source_root !=
                    selected.source_root ||
                !std::equal(
                    view.actions.begin(),
                    view.actions.end(),
                    population.actions.begin(),
                    population.actions.end()) ||
                view.actions.size() !=
                    population.actions.size() ||
                !option_rows_bit_identical(
                    view.option_rows,
                    population.option_rows)) {
                throw std::runtime_error(
                    "AQ18 authenticated live root drifted");
            }

            RootLabel label =
                label_live_root(
                    selected, view.actions,
                    view.option_rows,
                    view.trace_point.state,
                    view.trace_point.context,
                    view.original_decks, parent);
            const std::size_t deck =
                deck_index(
                    label.identity.owner_deck);
            if (label.identity.split ==
                    density::Split::Train &&
                !repeated_decks[deck]) {
                const RootLabel repeated =
                    label_live_root(
                        selected, view.actions,
                        view.option_rows,
                        view.trace_point.state,
                        view.trace_point.context,
                        view.original_decks, parent);
                if (!root_labels_bit_identical(
                        repeated, label)) {
                    throw std::runtime_error(
                        "AQ18 deck sentinel relabel drifted");
                }
                repeated_decks[deck] = true;
                ++sentinel_relabels;
            }

            if (!hidden_repartition_bit_identical &&
                view.hidden_repartition_witness) {
                const auto hidden =
                    density::
                        make_actor_local_hidden_repartition(
                            view.trace_point.state,
                            selected.source_root
                                .coordinate.actor);
                if (!hidden.has_value() ||
                    *hidden ==
                        view.trace_point.state ||
                    observe_game_state(
                        *hidden,
                        selected.source_root
                            .coordinate.actor) !=
                        observe_game_state(
                            view.trace_point.state,
                            selected.source_root
                                .coordinate.actor)) {
                    throw std::runtime_error(
                        "AQ18 hidden sentinel was vacuous");
                }
                const auto hidden_observation =
                    learned_observation(
                        *hidden,
                        selected.source_root
                            .coordinate.actor);
                const auto hidden_actions =
                    legal_priority_actions(
                        *hidden,
                        selected.source_root
                            .coordinate.actor,
                        view.trace_point.context
                            .sorcery_actions);
                std::vector<std::vector<double>>
                    hidden_rows;
                hidden_rows.reserve(
                    hidden_actions.size());
                for (const PriorityAction& action :
                     hidden_actions) {
                    hidden_rows.push_back(
                        learned_priority_policy_features(
                            *hidden,
                            selected.source_root
                                .coordinate.actor,
                            action,
                            view.trace_point.context
                                .sorcery_actions,
                            view.trace_point.context.phase,
                            view.trace_point.context
                                .consecutive_passes));
                }
                if (!rows_bit_identical(
                        view.observation,
                        hidden_observation) ||
                    !std::equal(
                        view.actions.begin(),
                        view.actions.end(),
                        hidden_actions.begin(),
                        hidden_actions.end()) ||
                    view.actions.size() !=
                        hidden_actions.size() ||
                    !option_rows_bit_identical(
                        view.option_rows,
                        hidden_rows)) {
                    throw std::runtime_error(
                        "AQ18 hidden sentinel changed actor input");
                }
                const RootLabel hidden_label =
                    label_live_root(
                        selected, hidden_actions,
                        hidden_rows, *hidden,
                        view.trace_point.context,
                        view.original_decks, parent);
                if (!root_labels_bit_identical(
                        hidden_label, label)) {
                    throw std::runtime_error(
                        "AQ18 hidden sentinel changed labels");
                }
                hidden_repartition_bit_identical = true;
            }

            if (label.identity.split ==
                density::Split::Train) {
                corpus.train.push_back(
                    std::move(label));
            } else {
                corpus.dev.push_back(
                    std::move(label));
            }
            ++selected_position;
        };
    const density::Collection replayed =
        density::replay_frozen_census(
            parent, frozen.source_census, visitor);
    if (replayed.census != frozen.source_census ||
        selected_position !=
            frozen.selected_population.size() ||
        sentinel_relabels != kDeckCount ||
        !std::all_of(
            repeated_decks.begin(),
            repeated_decks.end(),
            [](bool value) { return value; }) ||
        !hidden_repartition_bit_identical) {
        throw std::runtime_error(
            "AQ18 authenticated label replay did not cross-sum");
    }
    require_parent_snapshot(parent_before);

    corpus.diagnostics =
        evaluate_diagnostics_impl(
            corpus.train, corpus.dev,
            frozen.manifest.alias_groups);
    corpus.digest = digest_for_corpus(corpus);
    validate_corpus(corpus);
    const Corpus roundtrip = roundtrip_cache(corpus);
    if (!corpora_bit_identical(
            roundtrip, corpus)) {
        throw std::runtime_error(
            "AQ18 prepublication roundtrip drifted");
    }

    write_cache_atomic_no_replace(path, roundtrip);
    const Corpus loaded = load_cache(path);
    if (!corpora_bit_identical(
            loaded, roundtrip)) {
        throw std::runtime_error(
            "AQ18 strict reload drifted");
    }
    const auto cache_snapshot =
        artifact_integrity::snapshot_regular_file(path);
    require_parent_snapshot(parent_before);
    return {
        .corpus = std::move(corpus),
        .cache_path = path,
        .cache_bytes = cache_snapshot.byte_size,
        .cache_sha256 = cache_snapshot.sha256,
        .sentinel_labels_bit_identical = true,
        .sentinel_relabels = sentinel_relabels,
        .hidden_repartition_bit_identical = true,
        .roundtrip_bit_identical = true,
        .strict_reload_bit_identical = true,
        .parent_immutable = true,
        .artifact_published = true,
    };
}

void print_report(
    std::ostream& output, const RunReport& report) {
    validate_corpus(report.corpus);
    if (!report.sentinel_labels_bit_identical ||
        report.sentinel_relabels != kDeckCount ||
        !report.hidden_repartition_bit_identical ||
        !report.roundtrip_bit_identical ||
        !report.strict_reload_bit_identical ||
        !report.parent_immutable ||
        !report.artifact_published ||
        report.cache_path !=
            std::filesystem::path(
                kProductionCachePath) ||
        report.cache_bytes == 0 ||
        !canonical_sha256(report.cache_sha256)) {
        throw std::invalid_argument(
            "AQ18 report gates failed");
    }
    output
        << std::setprecision(17)
        << "result=PASS"
        << " identifier=" << kIdentifier
        << " parent="
        << report.corpus.parent_fingerprint
        << " source_manifest="
        << report.corpus.source_manifest_hash
        << " selection_manifest="
        << report.corpus.selection_manifest_hash
        << " corpus_digest="
        << report.corpus.digest
        << " cache_sha256="
        << report.cache_sha256
        << " cache_bytes=" << report.cache_bytes
        << " train_roots="
        << report.corpus.train.size()
        << " dev_roots="
        << report.corpus.dev.size()
        << " base_cells="
        << kExpectedActionWorldCellsPerArm
        << " teacher_cells="
        << kExpectedActionWorldCellsPerArm
        << " sentinel_relabels="
        << report.sentinel_relabels
        << " sentinel_bit_identical=1"
        << " hidden_repartition_bit_identical=1"
        << " roundtrip_bit_identical=1"
        << " strict_reload_bit_identical=1"
        << " material_alias_conflicts="
        << report.corpus.diagnostics
               .material_alias_conflicts
        << " train_equal_deck_regret="
        << report.corpus.diagnostics
               .equal_deck_train_teacher_regret
        << " dev_equal_deck_regret="
        << report.corpus.diagnostics
               .equal_deck_dev_teacher_regret
        << " candidate_scores=0"
        << " model_created=0"
        << " optimizer_steps=0"
        << " gameplay_games=0"
        << " artifact_published=1\n";
    for (const CellMetrics& cell :
         report.corpus.diagnostics.cells) {
        output
            << "cell split=" << split_name(cell.split)
            << " deck=" << deck_name(cell.owner_deck)
            << " width="
            << stratum_name(cell.width_stratum)
            << " roots=" << cell.roots
            << " options=" << cell.options
            << " weight_mass=" << cell.weight_mass
            << " exact_max_agreement="
            << cell.exact_max_agreement
            << " stable_pairs=" << cell.stable_pairs
            << " stable_pair_agreement="
            << cell.stable_pair_agreement
            << " listwise_cross_entropy="
            << cell.listwise_cross_entropy
            << " teacher_regret="
            << cell.teacher_regret
            << " differing_roots="
            << cell.differing_roots
            << " alias_pairs="
            << cell.alias_pairs
            << " material_alias_conflicts="
            << cell.material_alias_conflicts
            << " maximum_absolute_correction_gap="
            << cell
                   .maximum_absolute_alias_correction_gap
            << '\n';
    }
    output
        << "alias_summary pairs="
        << report.corpus.diagnostics.aliases.size()
        << " material_conflicts="
        << report.corpus.diagnostics
               .material_alias_conflicts
        << " maximum_absolute_correction_gap="
        << report.corpus.diagnostics
               .maximum_absolute_alias_correction_gap
        << " threshold=" << kMaterialCorrectionGap
        << " normal_critical="
        << kNormal95CriticalValue << '\n';
}

namespace testing {

ProjectedRoot project_root(
    const priority::SelectedRoot& selected) {
    const auto& source = selected.source_root;
    const std::string physical =
        opaque_physical_group(source.coordinate);
    return {
        .split = source.coordinate.split,
        .owner_deck =
            source.coordinate.owner_deck(),
        .width_stratum =
            selected.width_stratum,
        .stable_root_id = source.stable_root_id,
        .selection_key = selected.selection_key,
        .information_action_fingerprint =
            source.information_action_fingerprint,
        .physical_game_group = physical,
        .actor_game_group =
            opaque_actor_group(
                physical, source.coordinate),
        .label_seed =
            root_label_seed(source.coordinate),
    };
}

RootLabel make_root_label(
    ProjectedRoot identity,
    std::vector<PriorityAction> actions,
    std::vector<std::string> descriptors,
    std::vector<std::vector<double>> option_rows,
    const LearnedActionSamples& base,
    const LearnedActionSamples& teacher) {
    require_priority_sample_contract(base, false);
    require_priority_sample_contract(teacher, true);
    const density::Split split = identity.split;
    RootLabel result{
        .identity = std::move(identity),
        .actions = std::move(actions),
        .action_descriptors =
            std::move(descriptors),
        .option_rows = std::move(option_rows),
        .base_q_samples = base.q_samples,
        .base_terminal_flags =
            base.terminal_evaluation_flags,
        .base_shallow_prior_samples =
            base.priority_shallow_prior_samples,
        .base_continuation_samples =
            base.priority_continuation_samples,
        .base_aggregate_scores =
            base.exact_priority_aggregate_scores,
        .base_accounting =
            accounting_from_samples(base, false),
        .teacher_q_samples = teacher.q_samples,
        .teacher_terminal_flags =
            teacher.terminal_evaluation_flags,
        .teacher_shallow_prior_samples =
            teacher.priority_shallow_prior_samples,
        .teacher_continuation_samples =
            teacher.priority_continuation_samples,
        .teacher_aggregate_scores =
            teacher.exact_priority_aggregate_scores,
        .teacher_accounting =
            accounting_from_samples(teacher, true),
        .target_probabilities =
            learned_soft_priority_target(
                teacher.exact_priority_aggregate_scores),
        .weight =
            split == density::Split::Train
                ? kTrainRootWeight
                : kDevRootWeight,
    };
    validate_root_label(result);
    return result;
}

Corpus make_unfrozen_corpus(
    std::vector<RootLabel> train,
    std::vector<RootLabel> dev,
    std::vector<priority::AliasGroup> aliases) {
    Corpus result{
        .parent_artifact_sha256 =
            std::string(64, 'a'),
        .parent_fingerprint =
            std::string(64, 'b'),
        .parent_components = {
            .critic = std::string(64, 'c'),
            .priority = std::string(64, 'd'),
            .attack = std::string(64, 'e'),
            .block = std::string(64, 'f'),
            .damage_order = std::string(64, '1'),
        },
        .source_manifest_hash =
            std::string(kRequiredSourceManifest),
        .selection_manifest_hash =
            std::string(kRequiredSelectionManifest),
        .preflight_digest =
            std::string(kRequiredPreflightDigest),
        .base_search_template =
            base_search_config(0),
        .teacher_search_template =
            teacher_search_config(0),
        .train = std::move(train),
        .dev = std::move(dev),
    };
    result.diagnostics =
        evaluate_diagnostics_impl(
            result.train, result.dev, aliases);
    result.digest = digest_for_corpus(result);
    validate_corpus_impl(result, false);
    return result;
}

std::string encode_unfrozen_cache(
    const Corpus& corpus) {
    return encode_cache_impl(corpus, false);
}

Corpus decode_unfrozen_cache(
    std::string_view bytes) {
    return decode_cache_impl(bytes, false);
}

void write_bytes_atomic_no_replace(
    const std::filesystem::path& path,
    std::string_view bytes) {
    write_bytes_atomic_no_replace_impl(path, bytes);
}

} // namespace testing

} // namespace old_school::decision_density_labels
