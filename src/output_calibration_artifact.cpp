#include "old_school/output_calibration_artifact.hpp"

#include "old_school/artifact_integrity.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace old_school::output_calibration {
namespace {

constexpr std::array<std::uint8_t, 8> kMagic = {
    'O', 'S', 'M', 'V', 'O', 'C', '1', '1',
};
constexpr std::uint32_t kSchema = 1;
constexpr std::size_t kMaximumArtifactBytes =
    4U * 1024U * 1024U;
constexpr std::size_t kMaximumStringBytes = 512;
constexpr std::size_t kSha256Length = 64;

std::span<const std::byte> byte_span(
    std::span<const std::uint8_t> bytes) {
    return std::as_bytes(bytes);
}

bool is_lower_hex_sha256(std::string_view value) {
    return value.size() == kSha256Length &&
           std::all_of(
               value.begin(), value.end(),
               [](char character) {
                   return (character >= '0' &&
                           character <= '9') ||
                          (character >= 'a' &&
                           character <= 'f');
               });
}

void require_sha256(
    std::string_view value, std::string_view field) {
    if (!is_lower_hex_sha256(value)) {
        throw std::invalid_argument(
            "OC1 artifact " + std::string(field) +
            " must be a lower-case SHA-256 digest");
    }
}

void require_finite(double value, std::string_view field) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(
            "OC1 artifact " + std::string(field) +
            " must be finite");
    }
}

bool nearly_equal(double left, double right) {
    const double scale = std::max(
        {1.0, std::abs(left), std::abs(right)});
    return std::abs(left - right) <=
           1e-12 * scale;
}

bool same_collection_recipe(
    const CollectionConfig& left,
    const CollectionConfig& right) {
    return left.seed == right.seed &&
           left.generation == right.generation &&
           left.balanced_blocks == right.balanced_blocks &&
           left.max_game_turns == right.max_game_turns &&
           left.pilot_training_games ==
               right.pilot_training_games;
}

class BinaryWriter {
  public:
    void byte(std::uint8_t value) {
        bytes_.push_back(value);
    }

    void boolean(bool value) {
        byte(value ? 1U : 0U);
    }

    void unsigned32(std::uint32_t value) {
        for (std::size_t byte_index = 0;
             byte_index < 4; ++byte_index) {
            byte(static_cast<std::uint8_t>(
                (value >> (byte_index * 8U)) & 0xffU));
        }
    }

    void unsigned64(std::uint64_t value) {
        for (std::size_t byte_index = 0;
             byte_index < 8; ++byte_index) {
            byte(static_cast<std::uint8_t>(
                (value >> (byte_index * 8U)) & 0xffU));
        }
    }

    void size(std::size_t value) {
        static_assert(
            sizeof(std::size_t) <= sizeof(std::uint64_t));
        unsigned64(static_cast<std::uint64_t>(value));
    }

    void uintmax(std::uintmax_t value) {
        if constexpr (
            sizeof(std::uintmax_t) >
            sizeof(std::uint64_t)) {
            if (value >
                static_cast<std::uintmax_t>(
                    std::numeric_limits<std::uint64_t>::max())) {
                throw std::length_error(
                    "OC1 artifact file size exceeds uint64");
            }
        }
        unsigned64(static_cast<std::uint64_t>(value));
    }

    void real(double value) {
        require_finite(value, "real field");
        unsigned64(std::bit_cast<std::uint64_t>(value));
    }

    void text(std::string_view value) {
        if (value.size() > kMaximumStringBytes) {
            throw std::length_error(
                "OC1 artifact string exceeds its bound");
        }
        unsigned32(static_cast<std::uint32_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    void raw(std::span<const std::uint8_t> bytes) {
        if (bytes.size() >
            kMaximumArtifactBytes - bytes_.size()) {
            throw std::length_error(
                "OC1 artifact exceeds its size bound");
        }
        bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
    }

    const std::vector<std::uint8_t>& data() const {
        return bytes_;
    }

  private:
    std::vector<std::uint8_t> bytes_;
};

class BinaryReader {
  public:
    explicit BinaryReader(
        std::span<const std::uint8_t> bytes)
        : bytes_(bytes) {}

    std::uint8_t byte(std::string_view field) {
        require(1, field);
        return bytes_[offset_++];
    }

    bool boolean(std::string_view field) {
        const std::uint8_t value = byte(field);
        if (value > 1U) {
            throw std::runtime_error(
                "OC1 artifact field '" +
                std::string(field) +
                "' is not a canonical boolean");
        }
        return value != 0U;
    }

    std::uint32_t unsigned32(std::string_view field) {
        require(4, field);
        std::uint32_t value = 0;
        for (std::size_t byte_index = 0;
             byte_index < 4; ++byte_index) {
            value |=
                static_cast<std::uint32_t>(
                    bytes_[offset_++])
                << (byte_index * 8U);
        }
        return value;
    }

    std::uint64_t unsigned64(std::string_view field) {
        require(8, field);
        std::uint64_t value = 0;
        for (std::size_t byte_index = 0;
             byte_index < 8; ++byte_index) {
            value |=
                static_cast<std::uint64_t>(
                    bytes_[offset_++])
                << (byte_index * 8U);
        }
        return value;
    }

    std::size_t size(std::string_view field) {
        const std::uint64_t value = unsigned64(field);
        if (value >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max())) {
            throw std::runtime_error(
                "OC1 artifact field '" +
                std::string(field) +
                "' exceeds size_t");
        }
        return static_cast<std::size_t>(value);
    }

    std::uintmax_t uintmax(std::string_view field) {
        const std::uint64_t value = unsigned64(field);
        if constexpr (
            sizeof(std::uintmax_t) <
            sizeof(std::uint64_t)) {
            if (value >
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::uintmax_t>::max())) {
                throw std::runtime_error(
                    "OC1 artifact field '" +
                    std::string(field) +
                    "' exceeds uintmax_t");
            }
        }
        return static_cast<std::uintmax_t>(value);
    }

    double real(std::string_view field) {
        const double value = std::bit_cast<double>(
            unsigned64(field));
        if (!std::isfinite(value)) {
            throw std::runtime_error(
                "OC1 artifact field '" +
                std::string(field) +
                "' contains a nonfinite real");
        }
        return value;
    }

    std::string text(std::string_view field) {
        const std::uint32_t length = unsigned32(field);
        if (length > kMaximumStringBytes) {
            throw std::runtime_error(
                "OC1 artifact field '" +
                std::string(field) +
                "' exceeds its string bound");
        }
        require(length, field);
        std::string value(
            reinterpret_cast<const char*>(
                bytes_.data() + offset_),
            length);
        offset_ += length;
        return value;
    }

    std::span<const std::uint8_t> take(
        std::size_t count, std::string_view field) {
        require(count, field);
        const auto result = bytes_.subspan(offset_, count);
        offset_ += count;
        return result;
    }

    std::size_t remaining() const {
        return bytes_.size() - offset_;
    }

    bool at_end() const {
        return offset_ == bytes_.size();
    }

  private:
    void require(
        std::size_t count, std::string_view field) const {
        if (count > remaining()) {
            throw std::runtime_error(
                "OC1 artifact is truncated while reading '" +
                std::string(field) + "'");
        }
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;
};

void write_parent_identity(
    BinaryWriter& writer,
    const ParentArtifactIdentity& identity) {
    writer.uintmax(identity.byte_size);
    writer.text(identity.sha256);
    writer.text(identity.model_fingerprint);
    writer.size(identity.training_games);
    writer.unsigned64(identity.training_seed);
    writer.size(identity.generations);
}

ParentArtifactIdentity read_parent_identity(
    BinaryReader& reader) {
    return {
        .byte_size = reader.uintmax("parent byte size"),
        .sha256 = reader.text("parent artifact SHA-256"),
        .model_fingerprint =
            reader.text("parent model fingerprint"),
        .training_games =
            reader.size("parent training games"),
        .training_seed =
            reader.unsigned64("parent training seed"),
        .generations =
            reader.size("parent generation count"),
    };
}

void write_collection_config(
    BinaryWriter& writer,
    const CollectionConfig& config) {
    writer.unsigned64(config.seed);
    writer.size(config.generation);
    writer.size(config.balanced_blocks);
    writer.size(config.max_game_turns);
    writer.size(config.pilot_training_games);
    writer.size(config.worker_count);
}

CollectionConfig read_collection_config(
    BinaryReader& reader) {
    return {
        .seed = reader.unsigned64("fit seed"),
        .generation = reader.size("fit generation"),
        .balanced_blocks =
            reader.size("fit balanced blocks"),
        .max_game_turns =
            reader.size("fit maximum turns"),
        .pilot_training_games =
            reader.size("fit pilot training games"),
        .worker_count =
            reader.size("fit worker count"),
    };
}

void write_hidden_counts(
    BinaryWriter& writer,
    const HiddenDeckCounts& counts) {
    writer.size(counts.attempted);
    writer.size(counts.changed);
    writer.size(counts.unchanged);
}

HiddenDeckCounts read_hidden_counts(
    BinaryReader& reader, std::string_view scope) {
    return {
        .attempted = reader.size(
            std::string(scope) + " hidden attempts"),
        .changed = reader.size(
            std::string(scope) + " hidden changes"),
        .unchanged = reader.size(
            std::string(scope) + " hidden unchanged"),
    };
}

void write_hidden_report(
    BinaryWriter& writer,
    const HiddenRepartitionReport& report) {
    write_hidden_counts(writer, report.pooled);
    for (const auto& deck : report.by_deck) {
        write_hidden_counts(writer, deck);
    }
    writer.boolean(
        report.owner_visible_rows_bit_identical);
    writer.boolean(
        report.encoded_features_bit_identical);
    writer.boolean(
        report.parent_leaf_predictions_bit_identical);
    writer.boolean(report.parent_predictions_bit_identical);
    writer.boolean(
        report.candidate_leaf_predictions_bit_identical);
    writer.boolean(
        report.candidate_predictions_bit_identical);
    writer.text(report.original_owner_visible_rows_hash);
    writer.text(
        report.repartitioned_owner_visible_rows_hash);
    writer.text(report.original_encoded_rows_hash);
    writer.text(report.repartitioned_encoded_rows_hash);
    writer.text(report.original_parent_leaf_hash);
    writer.text(report.repartitioned_parent_leaf_hash);
    writer.text(report.original_parent_prediction_hash);
    writer.text(report.repartitioned_parent_prediction_hash);
    writer.text(report.original_candidate_leaf_hash);
    writer.text(report.repartitioned_candidate_leaf_hash);
    writer.text(report.original_candidate_prediction_hash);
    writer.text(
        report.repartitioned_candidate_prediction_hash);
}

HiddenRepartitionReport read_hidden_report(
    BinaryReader& reader) {
    HiddenRepartitionReport report;
    report.pooled = read_hidden_counts(reader, "pooled");
    for (std::size_t deck = 0;
         deck < report.by_deck.size(); ++deck) {
        report.by_deck[deck] =
            read_hidden_counts(
                reader, "deck " + std::to_string(deck));
    }
    report.owner_visible_rows_bit_identical =
        reader.boolean("hidden owner-visible identity");
    report.encoded_features_bit_identical =
        reader.boolean("hidden encoded-feature identity");
    report.parent_leaf_predictions_bit_identical =
        reader.boolean("hidden parent-leaf identity");
    report.parent_predictions_bit_identical =
        reader.boolean("hidden parent identity");
    report.candidate_leaf_predictions_bit_identical =
        reader.boolean("hidden candidate-leaf identity");
    report.candidate_predictions_bit_identical =
        reader.boolean("hidden candidate identity");
    report.original_owner_visible_rows_hash =
        reader.text("original owner-visible hash");
    report.repartitioned_owner_visible_rows_hash =
        reader.text("repartitioned owner-visible hash");
    report.original_encoded_rows_hash =
        reader.text("original hidden encoded hash");
    report.repartitioned_encoded_rows_hash =
        reader.text("repartitioned hidden encoded hash");
    report.original_parent_leaf_hash =
        reader.text("original hidden parent-leaf hash");
    report.repartitioned_parent_leaf_hash =
        reader.text("repartitioned hidden parent-leaf hash");
    report.original_parent_prediction_hash =
        reader.text("original hidden parent hash");
    report.repartitioned_parent_prediction_hash =
        reader.text("repartitioned hidden parent hash");
    report.original_candidate_leaf_hash =
        reader.text("original hidden candidate-leaf hash");
    report.repartitioned_candidate_leaf_hash =
        reader.text("repartitioned hidden candidate-leaf hash");
    report.original_candidate_prediction_hash =
        reader.text("original hidden candidate hash");
    report.repartitioned_candidate_prediction_hash =
        reader.text("repartitioned hidden candidate hash");
    return report;
}

void write_schedule_accounting(
    BinaryWriter& writer,
    const ScheduleAccounting& accounting) {
    writer.size(accounting.physical_games);
    for (const std::size_t value :
         accounting.perspectives_by_deck) {
        writer.size(value);
    }
    for (const auto& deck : accounting.deck_seat_start) {
        for (const auto& seat : deck) {
            for (const std::size_t value : seat) {
                writer.size(value);
            }
        }
    }
    writer.boolean(accounting.tasks_well_formed);
    writer.boolean(accounting.exact_balanced_blocks);
}

ScheduleAccounting read_schedule_accounting(
    BinaryReader& reader) {
    ScheduleAccounting accounting;
    accounting.physical_games =
        reader.size("physical games");
    for (std::size_t& value :
         accounting.perspectives_by_deck) {
        value = reader.size("deck perspectives");
    }
    for (auto& deck : accounting.deck_seat_start) {
        for (auto& seat : deck) {
            for (std::size_t& value : seat) {
                value =
                    reader.size("deck/seat/start count");
            }
        }
    }
    accounting.tasks_well_formed =
        reader.boolean("tasks well formed");
    accounting.exact_balanced_blocks =
        reader.boolean("exact balanced blocks");
    return accounting;
}

void write_collection_accounting(
    BinaryWriter& writer,
    const CollectionAccounting& accounting) {
    write_collection_config(writer, accounting.config);
    write_schedule_accounting(writer, accounting.schedule);
    writer.size(accounting.actor_perspectives);
    writer.size(accounting.records);
    writer.real(accounting.total_weight);
    for (const auto& deck : accounting.by_deck) {
        writer.size(deck.perspectives);
        writer.size(deck.records);
        writer.real(deck.total_weight);
    }
    write_hidden_report(writer, accounting.hidden);
}

CollectionAccounting read_collection_accounting(
    BinaryReader& reader) {
    CollectionAccounting accounting;
    accounting.config = read_collection_config(reader);
    accounting.schedule =
        read_schedule_accounting(reader);
    accounting.actor_perspectives =
        reader.size("actor perspectives");
    accounting.records = reader.size("record count");
    accounting.total_weight =
        reader.real("total record weight");
    for (auto& deck : accounting.by_deck) {
        deck.perspectives =
            reader.size("deck perspective count");
        deck.records = reader.size("deck record count");
        deck.total_weight =
            reader.real("deck total weight");
    }
    accounting.hidden = read_hidden_report(reader);
    return accounting;
}

void write_corpus_hashes(
    BinaryWriter& writer, const CorpusHashes& hashes) {
    writer.text(hashes.schedule);
    writer.text(hashes.outcomes);
    writer.text(hashes.record_counts);
    writer.text(hashes.features);
    writer.text(hashes.targets);
    writer.text(hashes.weights);
    writer.text(hashes.optimizer_input);
    writer.text(hashes.parent_leaf_predictions);
    writer.text(hashes.parent_predictions);
    writer.text(hashes.candidate_leaf_predictions);
    writer.text(hashes.candidate_predictions);
}

CorpusHashes read_corpus_hashes(BinaryReader& reader) {
    return {
        .schedule = reader.text("schedule hash"),
        .outcomes = reader.text("outcome hash"),
        .record_counts =
            reader.text("record-count hash"),
        .features = reader.text("feature hash"),
        .targets = reader.text("target hash"),
        .weights = reader.text("weight hash"),
        .optimizer_input =
            reader.text("optimizer-input hash"),
        .parent_leaf_predictions =
            reader.text("parent-leaf hash"),
        .parent_predictions =
            reader.text("parent-prediction hash"),
        .candidate_leaf_predictions =
            reader.text("candidate-leaf hash"),
        .candidate_predictions =
            reader.text("candidate-prediction hash"),
    };
}

void write_optimizer(
    BinaryWriter& writer,
    const LearnedOutputCalibrationConfig& config) {
    writer.size(config.max_iterations);
    writer.real(config.l2_tether);
    writer.real(config.gradient_tolerance);
}

LearnedOutputCalibrationConfig read_optimizer(
    BinaryReader& reader) {
    return {
        .max_iterations =
            reader.size("optimizer iteration cap"),
        .l2_tether = reader.real("optimizer L2 tether"),
        .gradient_tolerance =
            reader.real("optimizer gradient tolerance"),
    };
}

void write_diagnostics(
    BinaryWriter& writer,
    const LearnedOutputCalibrationDiagnostics& diagnostics) {
    writer.size(diagnostics.example_count);
    writer.size(diagnostics.leaf_count);
    writer.size(diagnostics.iterations);
    writer.boolean(diagnostics.converged);
    writer.real(diagnostics.total_weight);
    writer.real(diagnostics.before_weighted_bce);
    writer.real(diagnostics.after_weighted_bce);
    writer.real(diagnostics.max_parameter_delta);
}

LearnedOutputCalibrationDiagnostics read_diagnostics(
    BinaryReader& reader) {
    return {
        .example_count =
            reader.size("optimizer example count"),
        .leaf_count =
            reader.size("optimizer leaf count"),
        .iterations =
            reader.size("optimizer accepted iterations"),
        .converged =
            reader.boolean("optimizer convergence"),
        .total_weight =
            reader.real("optimizer total weight"),
        .before_weighted_bce =
            reader.real("optimizer before loss"),
        .after_weighted_bce =
            reader.real("optimizer after loss"),
        .max_parameter_delta =
            reader.real("optimizer maximum delta"),
    };
}

void write_components(
    BinaryWriter& writer,
    const LearnedModelComponentFingerprints& components) {
    writer.text(components.critic);
    writer.text(components.priority);
    writer.text(components.attack);
    writer.text(components.block);
    writer.text(components.damage_order);
}

LearnedModelComponentFingerprints read_components(
    BinaryReader& reader, std::string_view scope) {
    const std::string prefix(scope);
    return {
        .critic = reader.text(prefix + " critic fingerprint"),
        .priority =
            reader.text(prefix + " priority fingerprint"),
        .attack =
            reader.text(prefix + " attack fingerprint"),
        .block =
            reader.text(prefix + " block fingerprint"),
        .damage_order =
            reader.text(prefix + " damage-order fingerprint"),
    };
}

void write_tensors(
    BinaryWriter& writer,
    const LearnedCriticTensorFingerprints& tensors) {
    writer.text(tensors.input_hidden);
    writer.text(tensors.output_layer);
    writer.text(tensors.direct_paths);
}

LearnedCriticTensorFingerprints read_tensors(
    BinaryReader& reader, std::string_view scope) {
    const std::string prefix(scope);
    return {
        .input_hidden =
            reader.text(prefix + " input-hidden fingerprint"),
        .output_layer =
            reader.text(prefix + " output-layer fingerprint"),
        .direct_paths =
            reader.text(prefix + " direct-path fingerprint"),
    };
}

void write_parameters(
    BinaryWriter& writer,
    const LearnedOutputCalibrationParameters& parameters) {
    for (const auto& leaf : parameters.leaves) {
        for (const double value : leaf) {
            writer.real(value);
        }
    }
}

LearnedOutputCalibrationParameters read_parameters(
    BinaryReader& reader, std::string_view scope) {
    LearnedOutputCalibrationParameters parameters;
    for (auto& leaf : parameters.leaves) {
        for (double& value : leaf) {
            value = reader.real(
                std::string(scope) + " output parameter");
        }
    }
    return parameters;
}

void write_parameter_ledger(
    BinaryWriter& writer,
    const OutputParameterLedger& ledger) {
    write_parameters(writer, ledger.before);
    write_parameters(writer, ledger.after);
    writer.size(ledger.changed_parameters);
    writer.real(ledger.maximum_absolute_delta);
}

std::string parameter_sha256(
    const LearnedOutputCalibrationParameters& parameters) {
    BinaryWriter writer;
    writer.text(
        "old-school-oc1-output-parameters-v1");
    write_parameters(writer, parameters);
    return artifact_integrity::sha256_bytes(
        byte_span(writer.data()));
}

void write_hidden_refit(
    BinaryWriter& writer,
    const HiddenRefitEvidence& evidence) {
    writer.text(evidence.original_parameters_sha256);
    writer.text(evidence.repartitioned_parameters_sha256);
    writer.text(evidence.original_candidate_fingerprint);
    writer.text(
        evidence.repartitioned_candidate_fingerprint);
    write_diagnostics(
        writer,
        evidence.repartitioned_optimizer_diagnostics);
}

HiddenRefitEvidence read_hidden_refit(
    BinaryReader& reader) {
    return {
        .original_parameters_sha256 =
            reader.text("original refit parameter hash"),
        .repartitioned_parameters_sha256 =
            reader.text("repartitioned refit parameter hash"),
        .original_candidate_fingerprint =
            reader.text("original refit fingerprint"),
        .repartitioned_candidate_fingerprint =
            reader.text("repartitioned refit fingerprint"),
        .repartitioned_optimizer_diagnostics =
            read_diagnostics(reader),
    };
}

OutputParameterLedger read_parameter_ledger(
    BinaryReader& reader) {
    return {
        .before = read_parameters(reader, "parent"),
        .after = read_parameters(reader, "candidate"),
        .changed_parameters =
            reader.size("changed output parameter count"),
        .maximum_absolute_delta =
            reader.real("maximum output parameter delta"),
    };
}

void write_report(
    BinaryWriter& writer,
    const OutputCalibrationArtifactReport& report) {
    writer.text(report.family);
    writer.text(report.environment_schema);
    writer.text(report.optimizer_recipe);
    writer.text(report.weighting_recipe);
    write_parent_identity(writer, report.parent);
    write_collection_config(writer, report.fit_config);
    write_collection_accounting(
        writer, report.fit_accounting);
    write_corpus_hashes(writer, report.fit_hashes);
    write_optimizer(writer, report.optimizer);
    write_diagnostics(
        writer, report.optimizer_diagnostics);
    write_components(writer, report.parent_components);
    write_components(writer, report.candidate_components);
    write_tensors(writer, report.parent_tensors);
    write_tensors(writer, report.candidate_tensors);
    write_parameter_ledger(
        writer, report.output_parameters);
    write_hidden_refit(writer, report.hidden_refit);
    writer.text(report.candidate_fingerprint);
}

OutputCalibrationArtifactReport read_report(
    BinaryReader& reader) {
    OutputCalibrationArtifactReport report;
    report.family = reader.text("family");
    report.environment_schema =
        reader.text("environment schema");
    report.optimizer_recipe =
        reader.text("optimizer recipe");
    report.weighting_recipe =
        reader.text("weighting recipe");
    report.parent = read_parent_identity(reader);
    report.fit_config = read_collection_config(reader);
    report.fit_accounting =
        read_collection_accounting(reader);
    report.fit_hashes = read_corpus_hashes(reader);
    report.optimizer = read_optimizer(reader);
    report.optimizer_diagnostics =
        read_diagnostics(reader);
    report.parent_components =
        read_components(reader, "parent");
    report.candidate_components =
        read_components(reader, "candidate");
    report.parent_tensors =
        read_tensors(reader, "parent");
    report.candidate_tensors =
        read_tensors(reader, "candidate");
    report.output_parameters =
        read_parameter_ledger(reader);
    report.hidden_refit = read_hidden_refit(reader);
    report.candidate_fingerprint =
        reader.text("candidate fingerprint");
    return report;
}

std::pair<std::size_t, double> parameter_delta(
    const LearnedOutputCalibrationParameters& before,
    const LearnedOutputCalibrationParameters& after) {
    std::size_t changed = 0;
    double maximum = 0.0;
    for (std::size_t leaf = 0;
         leaf < before.leaves.size(); ++leaf) {
        for (std::size_t parameter = 0;
             parameter < before.leaves[leaf].size();
             ++parameter) {
            const double left =
                before.leaves[leaf][parameter];
            const double right =
                after.leaves[leaf][parameter];
            require_finite(left, "parent output parameter");
            require_finite(right, "candidate output parameter");
            if (std::bit_cast<std::uint64_t>(left) !=
                std::bit_cast<std::uint64_t>(right)) {
                ++changed;
            }
            maximum = std::max(
                maximum, std::abs(right - left));
        }
    }
    return {changed, maximum};
}

void validate_fingerprint_group(
    const LearnedModelComponentFingerprints& components,
    std::string_view scope) {
    require_sha256(
        components.critic,
        std::string(scope) + " critic fingerprint");
    require_sha256(
        components.priority,
        std::string(scope) + " priority fingerprint");
    require_sha256(
        components.attack,
        std::string(scope) + " attack fingerprint");
    require_sha256(
        components.block,
        std::string(scope) + " block fingerprint");
    require_sha256(
        components.damage_order,
        std::string(scope) + " damage-order fingerprint");
}

void validate_tensor_group(
    const LearnedCriticTensorFingerprints& tensors,
    std::string_view scope) {
    require_sha256(
        tensors.input_hidden,
        std::string(scope) + " input-hidden fingerprint");
    require_sha256(
        tensors.output_layer,
        std::string(scope) + " output-layer fingerprint");
    require_sha256(
        tensors.direct_paths,
        std::string(scope) + " direct-path fingerprint");
}

void validate_hashes(const CorpusHashes& hashes) {
    require_sha256(hashes.schedule, "schedule hash");
    require_sha256(hashes.outcomes, "outcome hash");
    require_sha256(
        hashes.record_counts, "record-count hash");
    require_sha256(hashes.features, "feature hash");
    require_sha256(hashes.targets, "target hash");
    require_sha256(hashes.weights, "weight hash");
    require_sha256(
        hashes.optimizer_input, "optimizer-input hash");
    require_sha256(
        hashes.parent_leaf_predictions,
        "parent-leaf hash");
    require_sha256(
        hashes.parent_predictions,
        "parent-prediction hash");
    require_sha256(
        hashes.candidate_leaf_predictions,
        "candidate-leaf hash");
    require_sha256(
        hashes.candidate_predictions,
        "candidate-prediction hash");
}

void validate_hidden(
    const HiddenRepartitionReport& hidden) {
    std::size_t attempted = 0;
    std::size_t changed = 0;
    std::size_t unchanged = 0;
    for (const HiddenDeckCounts& deck : hidden.by_deck) {
        if (deck.changed + deck.unchanged !=
                deck.attempted ||
            deck.attempted == 0 || deck.changed == 0) {
            throw std::invalid_argument(
                "OC1 artifact hidden audit is vacuous or "
                "internally inconsistent");
        }
        attempted += deck.attempted;
        changed += deck.changed;
        unchanged += deck.unchanged;
    }
    if (hidden.pooled.attempted != attempted ||
        hidden.pooled.changed != changed ||
        hidden.pooled.unchanged != unchanged ||
        !hidden.bit_identical() ||
        !hidden.nonvacuous_all_decks()) {
        throw std::invalid_argument(
            "OC1 artifact hidden audit failed");
    }
    const std::array<std::string_view, 12> digests = {
        hidden.original_owner_visible_rows_hash,
        hidden.repartitioned_owner_visible_rows_hash,
        hidden.original_encoded_rows_hash,
        hidden.repartitioned_encoded_rows_hash,
        hidden.original_parent_leaf_hash,
        hidden.repartitioned_parent_leaf_hash,
        hidden.original_parent_prediction_hash,
        hidden.repartitioned_parent_prediction_hash,
        hidden.original_candidate_leaf_hash,
        hidden.repartitioned_candidate_leaf_hash,
        hidden.original_candidate_prediction_hash,
        hidden.repartitioned_candidate_prediction_hash,
    };
    for (const std::string_view digest : digests) {
        require_sha256(digest, "hidden-audit hash");
    }
}

void validate_accounting(
    const CollectionConfig& config,
    const CollectionAccounting& accounting) {
    if (config.balanced_blocks == 0 ||
        config.max_game_turns == 0 ||
        config.pilot_training_games == 0 ||
        !same_collection_recipe(
            accounting.config, config) ||
        !accounting.schedule.tasks_well_formed ||
        !accounting.schedule.exact_balanced_blocks) {
        throw std::invalid_argument(
            "OC1 artifact fit configuration/accounting mismatch");
    }
    if (config.balanced_blocks >
        std::numeric_limits<std::size_t>::max() /
            learned_iteration::kBalancedScheduleGames) {
        throw std::overflow_error(
            "OC1 artifact balanced-block count overflows");
    }
    const std::size_t games =
        config.balanced_blocks *
        learned_iteration::kBalancedScheduleGames;
    if (games >
        std::numeric_limits<std::size_t>::max() / 2U) {
        throw std::overflow_error(
            "OC1 artifact actor-perspective count overflows");
    }
    const std::size_t perspectives = games * 2U;
    if (accounting.schedule.physical_games != games ||
        accounting.actor_perspectives != perspectives ||
        accounting.records == 0 ||
        !nearly_equal(
            accounting.total_weight,
            static_cast<double>(perspectives))) {
        throw std::invalid_argument(
            "OC1 artifact pooled fit accounting is invalid");
    }

    std::size_t summed_perspectives = 0;
    std::size_t summed_records = 0;
    double summed_weight = 0.0;
    const std::size_t deck_perspectives =
        config.balanced_blocks * 16U;
    const std::size_t quadrant_games =
        config.balanced_blocks * 4U;
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        if (accounting.schedule
                .perspectives_by_deck[deck] !=
                deck_perspectives ||
            accounting.by_deck[deck].perspectives !=
                deck_perspectives ||
            accounting.by_deck[deck].records == 0 ||
            accounting.hidden.by_deck[deck].attempted !=
                accounting.by_deck[deck].records ||
            !nearly_equal(
                accounting.by_deck[deck].total_weight,
                static_cast<double>(deck_perspectives))) {
            throw std::invalid_argument(
                "OC1 artifact per-deck fit accounting is invalid");
        }
        for (const auto& seat :
             accounting.schedule.deck_seat_start[deck]) {
            for (const std::size_t count : seat) {
                if (count != quadrant_games) {
                    throw std::invalid_argument(
                        "OC1 artifact deck/seat/start schedule "
                        "is imbalanced");
                }
            }
        }
        summed_perspectives +=
            accounting.by_deck[deck].perspectives;
        summed_records += accounting.by_deck[deck].records;
        summed_weight +=
            accounting.by_deck[deck].total_weight;
    }
    if (summed_perspectives != perspectives ||
        summed_records != accounting.records ||
        accounting.hidden.pooled.attempted !=
            accounting.records ||
        !nearly_equal(
            summed_weight, accounting.total_weight)) {
        throw std::invalid_argument(
            "OC1 artifact per-deck sums do not reconcile");
    }
    validate_hidden(accounting.hidden);
}

void validate_fit_corpus(
    const TrainingCorpus& corpus) {
    static_cast<void>(training_examples(corpus));
    const std::vector<CollectionTask> expected_tasks =
        collection_schedule(corpus.accounting.config);
    if (corpus.tasks != expected_tasks ||
        corpus.tasks.size() !=
            corpus.accounting.schedule.physical_games ||
        corpus.outcomes.size() != corpus.tasks.size() ||
        corpus.records.size() !=
            corpus.accounting.records) {
        throw std::invalid_argument(
            "OC1 artifact fit corpus structure/schedule is "
            "inconsistent");
    }

    struct PerspectiveLedger {
        bool initialized = false;
        DeckId deck = DeckId::Green;
        std::size_t trace_size = 0;
        std::vector<bool> indices;
    };
    std::vector<std::array<PerspectiveLedger, 2>> ledgers(
        corpus.tasks.size());
    std::array<std::size_t, kDeckCount> deck_records{};
    std::array<long double, kDeckCount> deck_weights{};
    for (std::size_t game = 0;
         game < corpus.tasks.size(); ++game) {
        if (corpus.tasks[game].physical_game != game ||
            corpus.outcomes[game].starting_player !=
                corpus.tasks[game].scheduled.starting_player) {
            throw std::invalid_argument(
                "OC1 artifact outcome/schedule identity "
                "mismatch");
        }
    }
    for (const TrainingRecord& record : corpus.records) {
        if (record.physical_game >= corpus.tasks.size() ||
            record.perspective >= 2) {
            throw std::invalid_argument(
                "OC1 artifact training record key is invalid");
        }
        const CollectionTask& task =
            corpus.tasks[record.physical_game];
        if (record.deck !=
            task.scheduled.seat_decks[record.perspective]) {
            throw std::invalid_argument(
                "OC1 artifact training record deck mismatch");
        }
        const double expected_target =
            learned_discounted_terminal_target(
                corpus.outcomes[record.physical_game],
                record.perspective);
        if (std::bit_cast<std::uint64_t>(record.target) !=
            std::bit_cast<std::uint64_t>(
                expected_target)) {
            throw std::invalid_argument(
                "OC1 artifact training target is not the "
                "declared terminal target");
        }
        PerspectiveLedger& ledger =
            ledgers[record.physical_game]
                   [record.perspective];
        if (!ledger.initialized) {
            ledger.initialized = true;
            ledger.deck = record.deck;
            ledger.trace_size = record.trace_size;
            ledger.indices.assign(
                record.trace_size, false);
        }
        if (ledger.deck != record.deck ||
            ledger.trace_size != record.trace_size ||
            record.trace_index >= ledger.indices.size() ||
            ledger.indices[record.trace_index]) {
            throw std::invalid_argument(
                "OC1 artifact training trace ledger is "
                "inconsistent");
        }
        ledger.indices[record.trace_index] = true;
        const std::size_t deck =
            static_cast<std::size_t>(record.deck);
        if (deck >= kDeckCount) {
            throw std::invalid_argument(
                "OC1 artifact training record deck is invalid");
        }
        ++deck_records[deck];
        deck_weights[deck] += record.weight;
    }
    for (const auto& game : ledgers) {
        for (const PerspectiveLedger& ledger : game) {
            if (!ledger.initialized ||
                !std::all_of(
                    ledger.indices.begin(),
                    ledger.indices.end(),
                    [](bool present) { return present; })) {
                throw std::invalid_argument(
                    "OC1 artifact training trace is incomplete");
            }
        }
    }
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        if (deck_records[deck] !=
                corpus.accounting.by_deck[deck].records ||
            !nearly_equal(
                static_cast<double>(deck_weights[deck]),
                corpus.accounting.by_deck[deck]
                    .total_weight)) {
            throw std::invalid_argument(
                "OC1 artifact training deck accounting does "
                "not match its rows");
        }
    }
}

void validate_optimizer(
    const LearnedOutputCalibrationConfig& optimizer,
    const LearnedOutputCalibrationDiagnostics& diagnostics,
    const CollectionAccounting& accounting) {
    if (optimizer.max_iterations == 0 ||
        optimizer.max_iterations > 32 ||
        optimizer.l2_tether != 0.01 ||
        optimizer.gradient_tolerance != 1e-10) {
        throw std::invalid_argument(
            "OC1 artifact optimizer recipe is invalid");
    }
    if (diagnostics.example_count != accounting.records ||
        diagnostics.leaf_count !=
            kLearnedOutputCalibrationLeafCount ||
        diagnostics.iterations > optimizer.max_iterations ||
        !diagnostics.converged ||
        !nearly_equal(
            diagnostics.total_weight,
            accounting.total_weight)) {
        throw std::invalid_argument(
            "OC1 artifact optimizer diagnostics are invalid");
    }
    require_finite(
        diagnostics.before_weighted_bce,
        "optimizer before loss");
    require_finite(
        diagnostics.after_weighted_bce,
        "optimizer after loss");
    require_finite(
        diagnostics.max_parameter_delta,
        "optimizer maximum delta");
    if (diagnostics.before_weighted_bce < 0.0 ||
        diagnostics.after_weighted_bce < 0.0 ||
        diagnostics.max_parameter_delta < 0.0 ||
        diagnostics.after_weighted_bce >
            diagnostics.before_weighted_bce) {
        throw std::invalid_argument(
            "OC1 artifact optimizer loss/delta is invalid");
    }
}

void validate_parent_identity(
    const ParentArtifactIdentity& identity) {
    if (identity.byte_size == 0 ||
        identity.training_games == 0 ||
        identity.generations == 0) {
        throw std::invalid_argument(
            "OC1 artifact parent identity is incomplete");
    }
    require_sha256(identity.sha256, "parent artifact SHA-256");
    require_sha256(
        identity.model_fingerprint,
        "parent model fingerprint");
}

void validate_report_and_models(
    const OutputCalibrationArtifactReport& report,
    const std::shared_ptr<const LearnedModel>& parent,
    const std::shared_ptr<const LearnedModel>& candidate) {
    if (report.family != kArtifactFamily ||
        report.environment_schema !=
            kArtifactEnvironmentSchema ||
        report.optimizer_recipe !=
            kArtifactOptimizerRecipe ||
        report.weighting_recipe !=
            kArtifactWeightingRecipe) {
        throw std::invalid_argument(
            "OC1 artifact family/schema/recipe mismatch");
    }
    if (!parent || !candidate) {
        throw std::invalid_argument(
            "OC1 artifact models must not be null");
    }
    validate_parent_identity(report.parent);
    validate_accounting(
        report.fit_config, report.fit_accounting);
    validate_hashes(report.fit_hashes);
    validate_optimizer(
        report.optimizer,
        report.optimizer_diagnostics,
        report.fit_accounting);
    validate_fingerprint_group(
        report.parent_components, "parent");
    validate_fingerprint_group(
        report.candidate_components, "candidate");
    validate_tensor_group(report.parent_tensors, "parent");
    validate_tensor_group(
        report.candidate_tensors, "candidate");
    require_sha256(
        report.candidate_fingerprint,
        "candidate fingerprint");

    const std::string parent_fingerprint =
        learned_model_fingerprint(parent);
    const std::string candidate_fingerprint =
        learned_model_fingerprint(candidate);
    if (report.parent.model_fingerprint !=
            parent_fingerprint ||
        report.candidate_fingerprint !=
            candidate_fingerprint ||
        report.parent_components !=
            learned_model_component_fingerprints(parent) ||
        report.candidate_components !=
            learned_model_component_fingerprints(candidate) ||
        report.parent_tensors !=
            learned_critic_tensor_fingerprints(parent) ||
        report.candidate_tensors !=
            learned_critic_tensor_fingerprints(candidate)) {
        throw std::invalid_argument(
            "OC1 artifact recorded model fingerprint mismatch");
    }
    if (report.parent_components.priority !=
            report.candidate_components.priority ||
        report.parent_components.attack !=
            report.candidate_components.attack ||
        report.parent_components.block !=
            report.candidate_components.block ||
        report.parent_components.damage_order !=
            report.candidate_components.damage_order ||
        report.parent_tensors.input_hidden !=
            report.candidate_tensors.input_hidden ||
        report.parent_tensors.direct_paths !=
            report.candidate_tensors.direct_paths) {
        throw std::invalid_argument(
            "OC1 artifact contains an impermissible tensor "
            "change");
    }

    const auto parent_parameters =
        learned_output_calibration_parameters(parent);
    const auto candidate_parameters =
        learned_output_calibration_parameters(candidate);
    if (report.output_parameters.before !=
            parent_parameters ||
        report.output_parameters.after !=
            candidate_parameters) {
        throw std::invalid_argument(
            "OC1 artifact output parameter ledger mismatch");
    }
    const auto [changed, maximum] = parameter_delta(
        parent_parameters, candidate_parameters);
    if (report.output_parameters.changed_parameters !=
            changed ||
        std::bit_cast<std::uint64_t>(
            report.output_parameters
                .maximum_absolute_delta) !=
            std::bit_cast<std::uint64_t>(maximum) ||
        !nearly_equal(
            report.optimizer_diagnostics
                .max_parameter_delta,
            maximum)) {
        throw std::invalid_argument(
            "OC1 artifact output delta accounting mismatch");
    }
    const std::string stored_parameter_hash =
        parameter_sha256(candidate_parameters);
    require_sha256(
        report.hidden_refit.original_parameters_sha256,
        "original hidden-refit parameter hash");
    require_sha256(
        report.hidden_refit
            .repartitioned_parameters_sha256,
        "repartitioned hidden-refit parameter hash");
    require_sha256(
        report.hidden_refit.original_candidate_fingerprint,
        "original hidden-refit candidate fingerprint");
    require_sha256(
        report.hidden_refit
            .repartitioned_candidate_fingerprint,
        "repartitioned hidden-refit candidate fingerprint");
    if (report.hidden_refit.original_parameters_sha256 !=
            stored_parameter_hash ||
        report.hidden_refit
                .repartitioned_parameters_sha256 !=
            stored_parameter_hash ||
        report.hidden_refit.original_candidate_fingerprint !=
            candidate_fingerprint ||
        report.hidden_refit
                .repartitioned_candidate_fingerprint !=
            candidate_fingerprint ||
        report.hidden_refit
                .repartitioned_optimizer_diagnostics !=
            report.optimizer_diagnostics) {
        throw std::invalid_argument(
            "OC1 artifact hidden-refit evidence is not "
            "bit-identical");
    }

    const auto rebuilt =
        with_learned_output_calibration_parameters(
            parent, report.output_parameters.after);
    if (learned_model_fingerprint(rebuilt) !=
        candidate_fingerprint) {
        throw std::invalid_argument(
            "OC1 artifact candidate cannot be reconstructed "
            "bit-exactly from its parent and output parameters");
    }
}

std::vector<std::uint8_t> serialize_artifact(
    const OutputCalibrationArtifactReport& report) {
    BinaryWriter payload;
    write_report(payload, report);
    const std::string checksum =
        artifact_integrity::sha256_bytes(
            byte_span(payload.data()));

    BinaryWriter file;
    file.raw(kMagic);
    file.unsigned32(kSchema);
    file.unsigned64(
        static_cast<std::uint64_t>(
            payload.data().size()));
    for (const char character : checksum) {
        file.byte(static_cast<std::uint8_t>(character));
    }
    file.raw(payload.data());
    return file.data();
}

class FileDescriptor {
  public:
    explicit FileDescriptor(int descriptor = -1)
        : descriptor_(descriptor) {}
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    FileDescriptor(FileDescriptor&& other) noexcept
        : descriptor_(other.release()) {}
    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        if (this != &other) {
            if (descriptor_ >= 0) {
                static_cast<void>(::close(descriptor_));
            }
            descriptor_ = other.release();
        }
        return *this;
    }
    ~FileDescriptor() {
        if (descriptor_ >= 0) {
            static_cast<void>(::close(descriptor_));
        }
    }
    int get() const {
        return descriptor_;
    }
    int release() {
        const int result = descriptor_;
        descriptor_ = -1;
        return result;
    }

  private:
    int descriptor_;
};

[[noreturn]] void throw_system(
    std::string_view operation,
    const std::filesystem::path& path, int error) {
    throw std::system_error(
        error, std::generic_category(),
        std::string(operation) + " '" + path.string() + "'");
}

int open_read_only_no_follow(
    const std::filesystem::path& path) {
    int flags = O_RDONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    while (true) {
        const int descriptor = ::open(path.c_str(), flags);
        if (descriptor >= 0) {
            return descriptor;
        }
        const int error = errno;
        if (error == EINTR) {
            continue;
        }
        throw_system("cannot open OC1 artifact", path, error);
    }
}

std::vector<std::uint8_t> read_bounded_file(
    const std::filesystem::path& path) {
    if (path.empty()) {
        throw std::invalid_argument(
            "OC1 artifact path must not be empty");
    }
    FileDescriptor descriptor(
        open_read_only_no_follow(path));
    struct stat status {};
    while (::fstat(descriptor.get(), &status) != 0) {
        const int error = errno;
        if (error != EINTR) {
            throw_system(
                "cannot inspect OC1 artifact", path, error);
        }
    }
    if (!S_ISREG(status.st_mode) || status.st_nlink == 0 ||
        status.st_size < 0 ||
        static_cast<std::uintmax_t>(status.st_size) >
            kMaximumArtifactBytes) {
        throw std::runtime_error(
            "OC1 artifact must be a bounded regular file");
    }
    const std::size_t size =
        static_cast<std::size_t>(status.st_size);
    std::vector<std::uint8_t> bytes(size);
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count = ::read(
            descriptor.get(), bytes.data() + offset,
            bytes.size() - offset);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count == 0) {
            throw std::runtime_error(
                "OC1 artifact changed or truncated while read");
        }
        throw_system("cannot read OC1 artifact", path, errno);
    }
    struct stat after {};
    while (::fstat(descriptor.get(), &after) != 0) {
        const int error = errno;
        if (error != EINTR) {
            throw_system(
                "cannot re-inspect OC1 artifact", path, error);
        }
    }
    if (after.st_dev != status.st_dev ||
        after.st_ino != status.st_ino ||
        after.st_size != status.st_size ||
        after.st_mtime != status.st_mtime ||
        after.st_ctime != status.st_ctime) {
        throw std::runtime_error(
            "OC1 artifact changed while it was read");
    }
    return bytes;
}

std::filesystem::path checked_path(
    const std::string& path) {
    if (path.empty() ||
        path.find('\0') != std::string::npos) {
        throw std::invalid_argument(
            "OC1 artifact path is empty or contains NUL");
    }
    const std::filesystem::path result(path);
    if (result.filename().empty()) {
        throw std::invalid_argument(
            "OC1 artifact path must name a file");
    }
    return result;
}

void write_all(
    int descriptor,
    std::span<const std::uint8_t> bytes,
    const std::filesystem::path& path) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count = ::write(
            descriptor, bytes.data() + offset,
            bytes.size() - offset);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        throw_system(
            "cannot write temporary OC1 artifact",
            path, count < 0 ? errno : EIO);
    }
}

void sync_descriptor(
    int descriptor,
    std::string_view operation,
    const std::filesystem::path& path) {
    while (::fsync(descriptor) != 0) {
        const int error = errno;
        if (error == EINTR) {
            continue;
        }
        throw_system(operation, path, error);
    }
}

void close_checked(
    FileDescriptor& descriptor,
    std::string_view operation,
    const std::filesystem::path& path) {
    const int raw = descriptor.release();
    while (::close(raw) != 0) {
        const int error = errno;
        if (error == EINTR) {
            continue;
        }
        throw_system(operation, path, error);
    }
}

void publish_atomic_no_replace(
    const std::filesystem::path& destination,
    std::span<const std::uint8_t> bytes) {
    std::error_code directory_error;
    std::filesystem::create_directories(
        destination.parent_path().empty()
            ? std::filesystem::path(".")
            : destination.parent_path(),
        directory_error);
    if (directory_error) {
        throw std::system_error(
            directory_error,
            "cannot create OC1 artifact directory");
    }
    const std::filesystem::path directory =
        destination.parent_path().empty()
            ? std::filesystem::path(".")
            : destination.parent_path();

    static std::atomic<std::uint64_t> counter{0};
    std::filesystem::path temporary;
    FileDescriptor output;
    for (std::size_t attempt = 0; attempt < 64; ++attempt) {
        temporary = directory /
            ("." + destination.filename().string() +
             ".tmp." + std::to_string(
                 static_cast<unsigned long long>(::getpid())) +
             "." + std::to_string(
                 counter.fetch_add(
                     1, std::memory_order_relaxed)));
        int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
        const int descriptor =
            ::open(temporary.c_str(), flags, 0644);
        if (descriptor >= 0) {
            output = FileDescriptor(descriptor);
            break;
        }
        const int error = errno;
        if (error == EINTR || error == EEXIST) {
            continue;
        }
        throw_system(
            "cannot create temporary OC1 artifact",
            temporary, error);
    }
    if (output.get() < 0) {
        throw std::runtime_error(
            "could not reserve a temporary OC1 artifact");
    }

    bool linked = false;
    try {
        write_all(output.get(), bytes, temporary);
        sync_descriptor(
            output.get(),
            "cannot sync temporary OC1 artifact",
            temporary);
        close_checked(
            output,
            "cannot close temporary OC1 artifact",
            temporary);

        while (::link(
                   temporary.c_str(),
                   destination.c_str()) != 0) {
            const int error = errno;
            if (error == EINTR) {
                continue;
            }
            if (error == EEXIST) {
                throw std::runtime_error(
                    "OC1 artifact destination already exists: '" +
                    destination.string() + "'");
            }
            throw_system(
                "cannot atomically publish OC1 artifact",
                destination, error);
        }
        linked = true;
        while (::unlink(temporary.c_str()) != 0) {
            const int error = errno;
            if (error == EINTR) {
                continue;
            }
            throw_system(
                "cannot unlink temporary OC1 artifact",
                temporary, error);
        }
        temporary.clear();

        int directory_flags = O_RDONLY;
#ifdef O_DIRECTORY
        directory_flags |= O_DIRECTORY;
#endif
#ifdef O_CLOEXEC
        directory_flags |= O_CLOEXEC;
#endif
        FileDescriptor directory_descriptor(
            ::open(directory.c_str(), directory_flags));
        if (directory_descriptor.get() < 0) {
            throw_system(
                "cannot open OC1 artifact directory",
                directory, errno);
        }
        sync_descriptor(
            directory_descriptor.get(),
            "cannot sync OC1 artifact directory",
            directory);
        close_checked(
            directory_descriptor,
            "cannot close OC1 artifact directory",
            directory);
    } catch (...) {
        if (!temporary.empty()) {
            static_cast<void>(::unlink(temporary.c_str()));
        }
        if (linked) {
            // Publication already succeeded. Never remove the destination:
            // preserving a durable candidate is safer than converting a
            // post-link directory-sync error into data loss.
        }
        throw;
    }
}

OutputCalibrationArtifactReport parse_file(
    const std::filesystem::path& path) {
    const std::vector<std::uint8_t> bytes =
        read_bounded_file(path);
    BinaryReader file(bytes);
    for (const std::uint8_t expected : kMagic) {
        if (file.byte("file magic") != expected) {
            throw std::runtime_error(
                "OC1 artifact has the wrong family magic");
        }
    }
    const std::uint32_t schema =
        file.unsigned32("artifact schema");
    if (schema != kSchema) {
        throw std::runtime_error(
            "OC1 artifact uses unsupported schema " +
            std::to_string(schema));
    }
    const std::uint64_t payload_length =
        file.unsigned64("payload length");
    if (payload_length >
            static_cast<std::uint64_t>(
                kMaximumArtifactBytes) ||
        file.remaining() <
            kSha256Length ||
        payload_length !=
            static_cast<std::uint64_t>(
                file.remaining() - kSha256Length)) {
        throw std::runtime_error(
            "OC1 artifact has an invalid payload length or "
            "trailing bytes");
    }
    std::string stored_checksum;
    stored_checksum.reserve(kSha256Length);
    for (std::size_t index = 0;
         index < kSha256Length; ++index) {
        stored_checksum.push_back(
            static_cast<char>(
                file.byte("payload checksum")));
    }
    if (!is_lower_hex_sha256(stored_checksum)) {
        throw std::runtime_error(
            "OC1 artifact payload checksum is malformed");
    }
    const auto payload = file.take(
        static_cast<std::size_t>(payload_length),
        "payload");
    if (!file.at_end()) {
        throw std::runtime_error(
            "OC1 artifact has trailing bytes");
    }
    const std::string computed_checksum =
        artifact_integrity::sha256_bytes(
            byte_span(payload));
    if (stored_checksum != computed_checksum) {
        throw std::runtime_error(
            "OC1 artifact failed its payload checksum");
    }
    BinaryReader payload_reader(payload);
    OutputCalibrationArtifactReport report =
        read_report(payload_reader);
    if (!payload_reader.at_end()) {
        throw std::runtime_error(
            "OC1 artifact has trailing payload bytes");
    }
    return report;
}

void validate_verified_parent_current(
    const VerifiedParentArtifact& parent) {
    validate_parent_identity(parent.identity());
    if (!parent.model()) {
        throw std::invalid_argument(
            "verified OC1 parent has no model");
    }
    if (learned_model_fingerprint(parent.model()) !=
        parent.identity().model_fingerprint) {
        throw std::runtime_error(
            "verified OC1 parent model changed");
    }
    const auto current =
        artifact_integrity::snapshot_regular_file(
            checked_path(parent.path()));
    if (current.byte_size != parent.identity().byte_size ||
        current.sha256 != parent.identity().sha256) {
        throw std::runtime_error(
            "verified OC1 parent artifact changed");
    }
}

} // namespace

VerifiedParentArtifact::VerifiedParentArtifact(
    std::shared_ptr<const LearnedModel> model,
    ParentArtifactIdentity identity,
    std::string path)
    : model_(std::move(model)),
      identity_(std::move(identity)),
      path_(std::move(path)) {}

std::shared_ptr<const LearnedModel>
VerifiedParentArtifact::model() const {
    return model_;
}

const ParentArtifactIdentity&
VerifiedParentArtifact::identity() const {
    return identity_;
}

const std::string& VerifiedParentArtifact::path() const {
    return path_;
}

VerifiedParentArtifact
verify_output_calibration_parent(
    const std::string& path,
    std::shared_ptr<const LearnedModel> supplied_model,
    const ParentArtifactIdentity& requirement) {
    validate_parent_identity(requirement);
    if (!supplied_model) {
        throw std::invalid_argument(
            "OC1 parent verification requires a model");
    }
    const std::filesystem::path checked =
        checked_path(path);
    const auto before =
        artifact_integrity::snapshot_regular_file(checked);
    if (before.byte_size != requirement.byte_size ||
        before.sha256 != requirement.sha256) {
        throw std::runtime_error(
            "OC1 parent artifact does not match its required "
            "size/SHA-256 identity");
    }
    const auto loaded =
        load_learned_value_challenger_artifact(
            path, requirement.training_games,
            requirement.training_seed,
            requirement.generations);
    const auto after =
        artifact_integrity::snapshot_regular_file(checked);
    if (before != after) {
        throw std::runtime_error(
            "OC1 parent artifact changed during verification");
    }
    const std::string loaded_fingerprint =
        learned_model_fingerprint(loaded.model());
    const std::string supplied_fingerprint =
        learned_model_fingerprint(supplied_model);
    if (loaded_fingerprint !=
            requirement.model_fingerprint ||
        supplied_fingerprint !=
            requirement.model_fingerprint) {
        throw std::runtime_error(
            "OC1 parent artifact, requirement, and supplied "
            "model fingerprints do not match");
    }
    return VerifiedParentArtifact(
        std::move(supplied_model), requirement,
        before.path);
}

OutputCalibrationArtifact::OutputCalibrationArtifact(
    std::shared_ptr<const LearnedModel> model,
    OutputCalibrationArtifactReport report,
    VerifiedParentArtifact parent)
    : model_(std::move(model)),
      report_(std::move(report)),
      parent_(std::move(parent)) {}

std::shared_ptr<const LearnedModel>
OutputCalibrationArtifact::model() const {
    return model_;
}

const OutputCalibrationArtifactReport&
OutputCalibrationArtifact::report() const {
    return report_;
}

OutputCalibrationArtifact
make_output_calibration_artifact(
    const VerifiedParentArtifact& parent,
    const TrainingCorpus& fit_corpus,
    LearnedOutputCalibrationConfig optimizer) {
    validate_verified_parent_current(parent);
    validate_fit_corpus(fit_corpus);
    const TrainingCorpus reproduced =
        collect_training_corpus(
            parent.model(),
            fit_corpus.accounting.config);
    if (reproduced != fit_corpus) {
        throw std::runtime_error(
            "OC1 artifact independently reproduced fit corpus "
            "does not match the supplied corpus");
    }
    validate_fit_corpus(reproduced);
    const auto examples = training_examples(reproduced);
    const LearnedOutputCalibrationResult calibration =
        calibrate_learned_value_output_layer(
            parent.model(), examples, optimizer);
    const LearnedOutputCalibrationResult
        hidden_repartition_calibration =
            calibrate_learned_value_output_layer(
                parent.model(), examples, optimizer);

    const auto before =
        learned_output_calibration_parameters(parent.model());
    const auto after =
        learned_output_calibration_parameters(
            calibration.model);
    const auto hidden_after =
        learned_output_calibration_parameters(
            hidden_repartition_calibration.model);
    if (hidden_after != after ||
        hidden_repartition_calibration.diagnostics !=
            calibration.diagnostics ||
        learned_model_fingerprint(
            hidden_repartition_calibration.model) !=
            learned_model_fingerprint(calibration.model)) {
        throw std::invalid_argument(
            "OC1 artifact hidden-repartition refit is not "
            "bit-identical");
    }
    const auto [changed, maximum] =
        parameter_delta(before, after);
    OutputCalibrationArtifactReport report{
        .family = std::string(kArtifactFamily),
        .environment_schema =
            std::string(kArtifactEnvironmentSchema),
        .optimizer_recipe =
            std::string(kArtifactOptimizerRecipe),
        .weighting_recipe =
            std::string(kArtifactWeightingRecipe),
        .parent = parent.identity(),
        .fit_config = reproduced.accounting.config,
        .fit_accounting = reproduced.accounting,
        .fit_hashes = reproduced.hashes,
        .optimizer = optimizer,
        .optimizer_diagnostics =
            calibration.diagnostics,
        .parent_components =
            learned_model_component_fingerprints(
                parent.model()),
        .candidate_components =
            learned_model_component_fingerprints(
                calibration.model),
        .parent_tensors =
            learned_critic_tensor_fingerprints(
                parent.model()),
        .candidate_tensors =
            learned_critic_tensor_fingerprints(
                calibration.model),
        .output_parameters = {
            .before = before,
            .after = after,
            .changed_parameters = changed,
            .maximum_absolute_delta = maximum,
        },
        .hidden_refit = {
            .original_parameters_sha256 =
                parameter_sha256(after),
            .repartitioned_parameters_sha256 =
                parameter_sha256(hidden_after),
            .original_candidate_fingerprint =
                learned_model_fingerprint(
                    calibration.model),
            .repartitioned_candidate_fingerprint =
                learned_model_fingerprint(
                    hidden_repartition_calibration.model),
            .repartitioned_optimizer_diagnostics =
                hidden_repartition_calibration.diagnostics,
        },
        .candidate_fingerprint =
            learned_model_fingerprint(calibration.model),
    };
    validate_report_and_models(
        report, parent.model(), calibration.model);
    return OutputCalibrationArtifact(
        calibration.model, std::move(report), parent);
}

void write_output_calibration_artifact_atomic_no_replace(
    const std::string& path,
    const OutputCalibrationArtifact& artifact) {
    validate_verified_parent_current(artifact.parent_);
    validate_report_and_models(
        artifact.report_, artifact.parent_.model(),
        artifact.model_);
    const std::vector<std::uint8_t> bytes =
        serialize_artifact(artifact.report_);
    publish_atomic_no_replace(checked_path(path), bytes);
}

OutputCalibrationArtifact
load_output_calibration_artifact(
    const std::string& path,
    const VerifiedParentArtifact& exact_parent,
    const CollectionConfig& expected_fit_config,
    LearnedOutputCalibrationConfig expected_optimizer) {
    validate_verified_parent_current(exact_parent);
    OutputCalibrationArtifactReport report =
        parse_file(checked_path(path));
    if (report.parent != exact_parent.identity()) {
        throw std::runtime_error(
            "OC1 artifact parent identity mismatch");
    }
    if (!same_collection_recipe(
            report.fit_config, expected_fit_config)) {
        throw std::runtime_error(
            "OC1 artifact fit configuration mismatch");
    }
    if (report.optimizer.max_iterations !=
            expected_optimizer.max_iterations ||
        report.optimizer.l2_tether !=
            expected_optimizer.l2_tether ||
        report.optimizer.gradient_tolerance !=
            expected_optimizer.gradient_tolerance) {
        throw std::runtime_error(
            "OC1 artifact optimizer configuration mismatch");
    }
    const auto candidate =
        with_learned_output_calibration_parameters(
            exact_parent.model(),
            report.output_parameters.after);
    try {
        validate_report_and_models(
            report, exact_parent.model(), candidate);
    } catch (const std::invalid_argument& error) {
        throw std::runtime_error(
            "invalid OC1 artifact '" + path +
            "': " + error.what());
    }
    return OutputCalibrationArtifact(
        candidate, std::move(report), exact_parent);
}

std::string output_calibration_cache_path(
    std::size_t training_games,
    std::uint64_t parent_training_seed,
    std::uint64_t fit_seed) {
    if (training_games == 0) {
        throw std::invalid_argument(
            "OC1 cache training games must be positive");
    }
    return "build/model-cache/"
           "old-school-value-output-calibration-v1-c16-t" +
           std::to_string(training_games) + "-p" +
           std::to_string(parent_training_seed) + "-f" +
           std::to_string(fit_seed) + ".bin";
}

} // namespace old_school::output_calibration
