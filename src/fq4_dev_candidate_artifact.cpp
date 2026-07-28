#include "old_school/fq4_dev_candidate_artifact.hpp"

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
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace old_school::fq4_dev_candidate_artifact {
namespace {

static_assert(
    sizeof(double) == sizeof(std::uint64_t) &&
    std::numeric_limits<double>::is_iec559);
static_assert(kLearnedValueSearchRolloutsPerWorld == 1);
static_assert(kLearnedValueSearchBlendsShallowPrior);

constexpr std::array<char, 8> kMagic{
    'O', 'S', 'F', 'Q', '4', 'C', '1', '\0',
};
constexpr std::uint32_t kWireVersion = 1;
constexpr std::uint32_t kEndianMarker = 0x01020304U;
constexpr std::size_t kMaximumTextBytes = 256;
constexpr std::size_t kMaximumHiddenCount = 256;
constexpr std::size_t kMaximumFeatureCount = 4096;
constexpr std::size_t kMaximumParameterCount = 1'100'000;
constexpr std::uint64_t kMaximumMetadataCount = 1'000'000'000ULL;

[[noreturn]] void fail(std::string_view message) {
    throw std::invalid_argument(
        "invalid FQ4 DEV1 candidate artifact: " +
        std::string(message));
}

bool canonical_text(std::string_view text) {
    return !text.empty() &&
           text.size() <= kMaximumTextBytes &&
           std::all_of(
               text.begin(), text.end(),
               [](unsigned char character) {
                   return character >= 0x20U &&
                          character <= 0x7eU;
               });
}

bool canonical_sha256(std::string_view value) {
    return value.size() == 64 &&
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
    std::string_view value, std::string_view context) {
    if (!canonical_sha256(value) ||
        std::all_of(
            value.begin(), value.end(),
            [](char character) { return character == '0'; })) {
        fail(std::string(context) + " is not a canonical SHA-256");
    }
}

void require_components(
    const LearnedModelComponentFingerprints& components,
    std::string_view context) {
    require_sha256(
        components.critic,
        std::string(context) + " critic fingerprint");
    require_sha256(
        components.priority,
        std::string(context) + " Priority fingerprint");
    require_sha256(
        components.attack,
        std::string(context) + " Attack fingerprint");
    require_sha256(
        components.block,
        std::string(context) + " Block fingerprint");
    require_sha256(
        components.damage_order,
        std::string(context) + " damage-order fingerprint");
}

std::uint64_t checked_parameter_count(
    std::uint64_t hidden, std::uint64_t features) {
    if (hidden == 0 || hidden > kMaximumHiddenCount ||
        features == 0 || features > kMaximumFeatureCount ||
        hidden >
            (std::numeric_limits<std::uint64_t>::max() -
             features - 1U) /
                (features + 2U)) {
        fail("Priority tensor dimensions are invalid");
    }
    const std::uint64_t count =
        hidden * features + 2U * hidden + features + 1U;
    if (count > kMaximumParameterCount) {
        fail("Priority parameter count exceeds its bound");
    }
    return count;
}

void require_optimizer(
    const LearnedValuePriorityHeadUpdateConfig& optimizer) {
    if (optimizer.batch_size == 0 ||
        optimizer.batch_size > kMaximumMetadataCount ||
        optimizer.epochs == 0 ||
        optimizer.epochs > kMaximumMetadataCount ||
        !std::isfinite(optimizer.learning_rate) ||
        optimizer.learning_rate <= 0.0 ||
        !std::isfinite(optimizer.beta1) ||
        optimizer.beta1 < 0.0 || optimizer.beta1 >= 1.0 ||
        !std::isfinite(optimizer.beta2) ||
        optimizer.beta2 < 0.0 || optimizer.beta2 >= 1.0 ||
        !std::isfinite(optimizer.epsilon) ||
        optimizer.epsilon <= 0.0 ||
        !std::isfinite(optimizer.global_gradient_norm_clip) ||
        optimizer.global_gradient_norm_clip <= 0.0 ||
        !std::isfinite(optimizer.residual_weight) ||
        optimizer.residual_weight <= 0.0 ||
        optimizer.residual_weight > 1.0 ||
        !std::isfinite(optimizer.policy_temperature) ||
        optimizer.policy_temperature <= 0.0) {
        fail("optimizer recipe is invalid");
    }
}

void validate_contract(const Contract& contract) {
    if (!canonical_text(contract.family) ||
        !canonical_text(contract.environment)) {
        fail("family or environment is not canonical text");
    }
    if (contract.parent.artifact_bytes == 0 ||
        contract.parent.training_games == 0 ||
        contract.parent.training_games >
            kMaximumMetadataCount ||
        contract.parent.generation > kMaximumMetadataCount) {
        fail("parent provenance counts are invalid");
    }
    require_sha256(
        contract.parent.artifact_sha256,
        "parent artifact hash");
    require_sha256(
        contract.parent.model_fingerprint,
        "parent model fingerprint");
    require_components(
        contract.parent.components, "parent");

    if (contract.corpus.artifact_bytes == 0) {
        fail("corpus byte count is zero");
    }
    require_sha256(
        contract.corpus.artifact_sha256,
        "corpus artifact hash");
    require_sha256(
        contract.fit.input_sha256, "FIT input hash");
    if (contract.fit.examples == 0 ||
        contract.fit.examples > kMaximumMetadataCount ||
        contract.fit.options > kMaximumMetadataCount ||
        contract.fit.options < contract.fit.examples ||
        contract.fit.check_examples > kMaximumMetadataCount ||
        contract.fit.background_only_examples >
            kMaximumMetadataCount ||
        contract.fit.optimizer_calls == 0 ||
        contract.fit.optimizer_calls >
            kMaximumMetadataCount) {
        fail("FIT boundary counts are invalid");
    }
    require_optimizer(contract.fit.optimizer);
    require_sha256(
        contract.candidate_model_fingerprint,
        "candidate model fingerprint");
    if (contract.candidate_model_fingerprint ==
        contract.parent.model_fingerprint) {
        fail("candidate and parent model fingerprints are equal");
    }

    const std::uint64_t count = checked_parameter_count(
        contract.priority_hidden_count,
        contract.priority_feature_count);
    if (count != contract.priority_parameter_count) {
        fail("contract Priority parameter count is inconsistent");
    }

    const auto& deployment = contract.deployment;
    if ((deployment.variant !=
             LearnedVariant::ValueSearchChampion &&
         deployment.variant != LearnedVariant::UnifiedActor) ||
        deployment.training_games == 0 ||
        deployment.training_games !=
            contract.parent.training_games ||
        deployment.worlds_per_action == 0 ||
        deployment.worlds_per_action >
            kMaximumMetadataCount ||
        deployment.horizon_turns == 0 ||
        deployment.horizon_turns >
            kMaximumMetadataCount ||
        deployment.rollouts_per_world == 0 ||
        deployment.rollouts_per_world >
            kMaximumMetadataCount ||
        deployment.root_search_depth == 0 ||
        deployment.root_search_depth >
            kMaximumMetadataCount ||
        !std::isfinite(deployment.root_exploration) ||
        deployment.root_exploration < 0.0 ||
        deployment.root_exploration > 1.0 ||
        !std::isfinite(deployment.continuation_epsilon) ||
        deployment.continuation_epsilon < 0.0 ||
        deployment.continuation_epsilon > 1.0 ||
        !std::isfinite(
            deployment.priority_residual_weight) ||
        deployment.priority_residual_weight <= 0.0 ||
        deployment.priority_residual_weight > 1.0 ||
        (deployment.continuation_controller !=
             LearnedContinuationController::Legacy &&
         deployment.continuation_controller !=
             LearnedContinuationController::
                 PublicStackPassV1) ||
        deployment.max_turns == 0 ||
        deployment.max_turns > kMaximumMetadataCount) {
        fail("deployment recipe is invalid");
    }
}

class Writer {
  public:
    void u8(std::uint8_t value) {
        bytes_.push_back(static_cast<char>(value));
    }

    void u32(std::uint32_t value) {
        for (std::size_t byte = 0; byte < 4; ++byte) {
            u8(static_cast<std::uint8_t>(
                (value >> (8U * byte)) & 0xffU));
        }
    }

    void u64(std::uint64_t value) {
        for (std::size_t byte = 0; byte < 8; ++byte) {
            u8(static_cast<std::uint8_t>(
                (value >> (8U * byte)) & 0xffU));
        }
    }

    void boolean(bool value) {
        u8(value ? 1U : 0U);
    }

    void real(double value) {
        u64(std::bit_cast<std::uint64_t>(value));
    }

    void raw(std::string_view value) {
        if (bytes_.size() > kMaximumArtifactBytes ||
            value.size() >
                kMaximumArtifactBytes - bytes_.size()) {
            throw std::length_error(
                "FQ4 candidate artifact exceeds byte limit");
        }
        bytes_.append(value);
    }

    void text(std::string_view value) {
        if (value.size() >
            std::numeric_limits<std::uint32_t>::max()) {
            throw std::length_error(
                "FQ4 candidate text exceeds wire limit");
        }
        u32(static_cast<std::uint32_t>(value.size()));
        raw(value);
    }

    void hash(std::string_view hexadecimal) {
        require_sha256(hexadecimal, "encoded hash");
        const auto nibble = [](char character) {
            return static_cast<std::uint8_t>(
                character <= '9'
                    ? character - '0'
                    : character - 'a' + 10);
        };
        for (std::size_t byte = 0; byte < 32; ++byte) {
            u8(static_cast<std::uint8_t>(
                (nibble(hexadecimal[2 * byte]) << 4U) |
                nibble(hexadecimal[2 * byte + 1])));
        }
    }

    const std::string& bytes() const {
        return bytes_;
    }

    std::string take() {
        return std::move(bytes_);
    }

  private:
    std::string bytes_;
};

class Reader {
  public:
    explicit Reader(std::string_view bytes)
        : bytes_(bytes) {}

    std::uint8_t u8(std::string_view context) {
        require(1, context);
        return static_cast<std::uint8_t>(
            static_cast<unsigned char>(bytes_[cursor_++]));
    }

    std::uint32_t u32(std::string_view context) {
        std::uint32_t value = 0;
        for (std::size_t byte = 0; byte < 4; ++byte) {
            value |=
                static_cast<std::uint32_t>(u8(context))
                << (8U * byte);
        }
        return value;
    }

    std::uint64_t u64(std::string_view context) {
        std::uint64_t value = 0;
        for (std::size_t byte = 0; byte < 8; ++byte) {
            value |=
                static_cast<std::uint64_t>(u8(context))
                << (8U * byte);
        }
        return value;
    }

    bool boolean(std::string_view context) {
        const std::uint8_t value = u8(context);
        if (value > 1U) {
            fail(std::string(context) + " is not a boolean");
        }
        return value != 0;
    }

    double real(std::string_view context) {
        const double value =
            std::bit_cast<double>(u64(context));
        if (!std::isfinite(value)) {
            fail(std::string(context) + " is nonfinite");
        }
        return value;
    }

    std::string text(std::string_view context) {
        const std::uint32_t size = u32(context);
        if (size == 0 || size > kMaximumTextBytes) {
            fail(std::string(context) + " has invalid length");
        }
        return std::string(view(size, context));
    }

    std::string hash(std::string_view context) {
        static constexpr char kHex[] =
            "0123456789abcdef";
        std::string result(64, '0');
        for (std::size_t byte = 0; byte < 32; ++byte) {
            const std::uint8_t value = u8(context);
            result[2 * byte] = kHex[value >> 4U];
            result[2 * byte + 1] =
                kHex[value & 0x0fU];
        }
        return result;
    }

    std::string_view view(
        std::size_t size, std::string_view context) {
        require(size, context);
        const std::string_view result =
            bytes_.substr(cursor_, size);
        cursor_ += size;
        return result;
    }

    bool empty() const {
        return cursor_ == bytes_.size();
    }

  private:
    void require(
        std::size_t size, std::string_view context) const {
        if (cursor_ > bytes_.size() ||
            size > bytes_.size() - cursor_) {
            fail(std::string(context) + " is truncated");
        }
    }

    std::string_view bytes_;
    std::size_t cursor_ = 0;
};

void write_components(
    Writer& output,
    const LearnedModelComponentFingerprints& components) {
    output.hash(components.critic);
    output.hash(components.priority);
    output.hash(components.attack);
    output.hash(components.block);
    output.hash(components.damage_order);
}

LearnedModelComponentFingerprints read_components(
    Reader& input, std::string_view context) {
    return {
        .critic =
            input.hash(std::string(context) + " critic"),
        .priority =
            input.hash(std::string(context) + " Priority"),
        .attack =
            input.hash(std::string(context) + " Attack"),
        .block =
            input.hash(std::string(context) + " Block"),
        .damage_order =
            input.hash(
                std::string(context) + " damage order"),
    };
}

void write_optimizer(
    Writer& output,
    const LearnedValuePriorityHeadUpdateConfig& optimizer) {
    output.u64(optimizer.batch_size);
    output.u64(optimizer.epochs);
    output.real(optimizer.learning_rate);
    output.real(optimizer.beta1);
    output.real(optimizer.beta2);
    output.real(optimizer.epsilon);
    output.real(optimizer.global_gradient_norm_clip);
    output.u64(optimizer.seed);
    output.real(optimizer.residual_weight);
    output.real(optimizer.policy_temperature);
}

std::size_t read_size(
    Reader& input, std::string_view context) {
    const std::uint64_t value = input.u64(context);
    if (value >
        std::numeric_limits<std::size_t>::max()) {
        fail(std::string(context) + " exceeds size_t");
    }
    return static_cast<std::size_t>(value);
}

LearnedValuePriorityHeadUpdateConfig read_optimizer(
    Reader& input) {
    LearnedValuePriorityHeadUpdateConfig optimizer{
        .batch_size =
            read_size(input, "optimizer batch size"),
        .epochs = read_size(input, "optimizer epochs"),
        .learning_rate =
            input.real("optimizer learning rate"),
        .beta1 = input.real("optimizer beta1"),
        .beta2 = input.real("optimizer beta2"),
        .epsilon = input.real("optimizer epsilon"),
        .global_gradient_norm_clip =
            input.real("optimizer gradient clip"),
        .seed = input.u64("optimizer seed"),
        .residual_weight =
            input.real("optimizer residual weight"),
        .policy_temperature =
            input.real("optimizer policy temperature"),
    };
    require_optimizer(optimizer);
    return optimizer;
}

void write_contract(
    Writer& output, const Contract& contract) {
    output.text(contract.family);
    output.text(contract.environment);
    output.u64(contract.parent.artifact_bytes);
    output.hash(contract.parent.artifact_sha256);
    output.hash(contract.parent.model_fingerprint);
    write_components(output, contract.parent.components);
    output.u64(contract.parent.training_games);
    output.u64(contract.parent.training_seed);
    output.u64(contract.parent.generation);
    output.u64(contract.corpus.artifact_bytes);
    output.hash(contract.corpus.artifact_sha256);
    output.hash(contract.fit.input_sha256);
    output.u64(contract.fit.examples);
    output.u64(contract.fit.options);
    output.u64(contract.fit.check_examples);
    output.u64(contract.fit.background_only_examples);
    output.u64(contract.fit.optimizer_calls);
    write_optimizer(output, contract.fit.optimizer);
    output.hash(contract.candidate_model_fingerprint);
    output.u32(contract.priority_hidden_count);
    output.u32(contract.priority_feature_count);
    output.u64(contract.priority_parameter_count);
    output.u8(static_cast<std::uint8_t>(
        contract.deployment.variant));
    output.u64(contract.deployment.training_games);
    output.u64(contract.deployment.worlds_per_action);
    output.u64(contract.deployment.horizon_turns);
    output.u64(contract.deployment.rollouts_per_world);
    output.u64(contract.deployment.root_search_depth);
    output.boolean(contract.deployment.shallow_prior);
    output.real(contract.deployment.root_exploration);
    output.real(contract.deployment.continuation_epsilon);
    output.real(
        contract.deployment.priority_residual_weight);
    output.boolean(contract.deployment.pass_dominance);
    output.u8(static_cast<std::uint8_t>(
        contract.deployment.continuation_controller));
    output.u64(contract.deployment.max_turns);
}

Contract read_contract(Reader& input) {
    Contract contract;
    contract.family = input.text("contract family");
    contract.environment =
        input.text("contract environment");
    contract.parent.artifact_bytes =
        input.u64("parent artifact bytes");
    contract.parent.artifact_sha256 =
        input.hash("parent artifact hash");
    contract.parent.model_fingerprint =
        input.hash("parent model fingerprint");
    contract.parent.components =
        read_components(input, "parent");
    contract.parent.training_games =
        input.u64("parent training games");
    contract.parent.training_seed =
        input.u64("parent training seed");
    contract.parent.generation =
        input.u64("parent generation");
    contract.corpus.artifact_bytes =
        input.u64("corpus artifact bytes");
    contract.corpus.artifact_sha256 =
        input.hash("corpus artifact hash");
    contract.fit.input_sha256 =
        input.hash("FIT input hash");
    contract.fit.examples = input.u64("FIT examples");
    contract.fit.options = input.u64("FIT options");
    contract.fit.check_examples =
        input.u64("FIT CHECK examples");
    contract.fit.background_only_examples =
        input.u64("FIT background examples");
    contract.fit.optimizer_calls =
        input.u64("FIT optimizer calls");
    contract.fit.optimizer = read_optimizer(input);
    contract.candidate_model_fingerprint =
        input.hash("candidate model fingerprint");
    contract.priority_hidden_count =
        input.u32("Priority hidden count");
    contract.priority_feature_count =
        input.u32("Priority feature count");
    contract.priority_parameter_count =
        input.u64("Priority parameter count");

    const std::uint8_t variant =
        input.u8("deployment variant");
    if (variant >
        static_cast<std::uint8_t>(
            LearnedVariant::UnifiedActor)) {
        fail("deployment variant is unknown");
    }
    contract.deployment.variant =
        static_cast<LearnedVariant>(variant);
    contract.deployment.training_games =
        input.u64("deployment training games");
    contract.deployment.worlds_per_action =
        input.u64("deployment worlds");
    contract.deployment.horizon_turns =
        input.u64("deployment horizon");
    contract.deployment.rollouts_per_world =
        input.u64("deployment rollouts");
    contract.deployment.root_search_depth =
        input.u64("deployment root search depth");
    contract.deployment.shallow_prior =
        input.boolean("deployment shallow prior");
    contract.deployment.root_exploration =
        input.real("deployment root exploration");
    contract.deployment.continuation_epsilon =
        input.real("deployment continuation epsilon");
    contract.deployment.priority_residual_weight =
        input.real("deployment Priority residual weight");
    contract.deployment.pass_dominance =
        input.boolean("deployment Pass dominance");
    const std::uint8_t controller =
        input.u8("deployment continuation controller");
    if (controller >
        static_cast<std::uint8_t>(
            LearnedContinuationController::
                PublicStackPassV1)) {
        fail("deployment continuation controller is unknown");
    }
    contract.deployment.continuation_controller =
        static_cast<LearnedContinuationController>(
            controller);
    contract.deployment.max_turns =
        input.u64("deployment maximum turns");
    validate_contract(contract);
    return contract;
}

std::string encoded_contract(const Contract& contract) {
    validate_contract(contract);
    Writer output;
    write_contract(output, contract);
    return output.take();
}

struct TensorBits {
    std::uint32_t hidden_count = 0;
    std::uint32_t feature_count = 0;
    std::vector<std::uint64_t> values;
};

TensorBits tensor_bits(
    const LearnedPriorityHeadParameters& parameters) {
    if (parameters.input_hidden.size() >
            std::numeric_limits<std::uint32_t>::max() ||
        parameters.direct.size() >
            std::numeric_limits<std::uint32_t>::max()) {
        fail("Priority tensor dimensions exceed wire limits");
    }
    TensorBits result{
        .hidden_count = static_cast<std::uint32_t>(
            parameters.input_hidden.size()),
        .feature_count = static_cast<std::uint32_t>(
            parameters.direct.size()),
        .values = {},
    };
    const std::uint64_t count = checked_parameter_count(
        result.hidden_count, result.feature_count);
    if (parameters.hidden_bias.size() !=
            result.hidden_count ||
        parameters.hidden_output.size() !=
            result.hidden_count) {
        fail("Priority vector dimensions are inconsistent");
    }
    result.values.reserve(
        static_cast<std::size_t>(count));
    const auto append = [&](double value) {
        if (!std::isfinite(value)) {
            fail("Priority tensor contains a nonfinite value");
        }
        result.values.push_back(
            std::bit_cast<std::uint64_t>(value));
    };
    for (const auto& row : parameters.input_hidden) {
        if (row.size() != result.feature_count) {
            fail("Priority input-hidden matrix is ragged");
        }
        for (double value : row) {
            append(value);
        }
    }
    for (double value : parameters.hidden_bias) {
        append(value);
    }
    for (double value : parameters.hidden_output) {
        append(value);
    }
    for (double value : parameters.direct) {
        append(value);
    }
    append(parameters.output_bias);
    if (result.values.size() != count) {
        fail("Priority tensor flattening count drifted");
    }
    return result;
}

LearnedPriorityHeadParameters parameters_from_bits(
    const TensorBits& bits) {
    const std::uint64_t expected = checked_parameter_count(
        bits.hidden_count, bits.feature_count);
    if (bits.values.size() != expected) {
        fail("Priority tensor bit count is inconsistent");
    }
    LearnedPriorityHeadParameters parameters;
    parameters.input_hidden.assign(
        bits.hidden_count,
        std::vector<double>(bits.feature_count));
    parameters.hidden_bias.resize(bits.hidden_count);
    parameters.hidden_output.resize(bits.hidden_count);
    parameters.direct.resize(bits.feature_count);
    std::size_t cursor = 0;
    const auto next = [&]() {
        const double value =
            std::bit_cast<double>(bits.values[cursor++]);
        if (!std::isfinite(value)) {
            fail("reconstructed Priority tensor is nonfinite");
        }
        return value;
    };
    for (auto& row : parameters.input_hidden) {
        for (double& value : row) {
            value = next();
        }
    }
    for (double& value : parameters.hidden_bias) {
        value = next();
    }
    for (double& value : parameters.hidden_output) {
        value = next();
    }
    for (double& value : parameters.direct) {
        value = next();
    }
    parameters.output_bias = next();
    return parameters;
}

std::string tensor_sha256(
    const TensorBits& tensors,
    std::string_view domain) {
    Writer output;
    output.text(kSchema);
    output.text(domain);
    output.u32(tensors.hidden_count);
    output.u32(tensors.feature_count);
    output.u64(tensors.values.size());
    for (std::uint64_t value : tensors.values) {
        output.u64(value);
    }
    return artifact_integrity::sha256_string(
        output.bytes());
}

TensorBits xor_delta(
    const TensorBits& parent,
    const TensorBits& candidate) {
    if (parent.hidden_count != candidate.hidden_count ||
        parent.feature_count != candidate.feature_count ||
        parent.values.size() != candidate.values.size()) {
        fail("parent and candidate Priority layouts differ");
    }
    TensorBits delta{
        .hidden_count = parent.hidden_count,
        .feature_count = parent.feature_count,
        .values =
            std::vector<std::uint64_t>(
                parent.values.size()),
    };
    for (std::size_t index = 0;
         index < delta.values.size(); ++index) {
        delta.values[index] =
            parent.values[index] ^ candidate.values[index];
    }
    return delta;
}

TensorBits apply_delta(
    const TensorBits& parent, const TensorBits& delta) {
    if (parent.hidden_count != delta.hidden_count ||
        parent.feature_count != delta.feature_count ||
        parent.values.size() != delta.values.size()) {
        fail("Priority XOR delta layout does not match parent");
    }
    TensorBits candidate{
        .hidden_count = parent.hidden_count,
        .feature_count = parent.feature_count,
        .values =
            std::vector<std::uint64_t>(
                parent.values.size()),
    };
    for (std::size_t index = 0;
         index < candidate.values.size(); ++index) {
        candidate.values[index] =
            parent.values[index] ^ delta.values[index];
    }
    return candidate;
}

void require_priority_only_change(
    const LearnedModelComponentFingerprints& parent,
    const LearnedModelComponentFingerprints& candidate) {
    require_components(candidate, "candidate");
    if (candidate.priority == parent.priority ||
        candidate.critic != parent.critic ||
        candidate.attack != parent.attack ||
        candidate.block != parent.block ||
        candidate.damage_order != parent.damage_order) {
        fail("candidate is not an isolated Priority-only change");
    }
}

void validate_manifest(const Manifest& manifest) {
    validate_contract(manifest.contract);
    require_priority_only_change(
        manifest.contract.parent.components,
        manifest.candidate_components);
    if (manifest.tensors.hidden_count !=
            manifest.contract.priority_hidden_count ||
        manifest.tensors.feature_count !=
            manifest.contract.priority_feature_count ||
        manifest.tensors.parameter_count !=
            manifest.contract.priority_parameter_count ||
        checked_parameter_count(
            manifest.tensors.hidden_count,
            manifest.tensors.feature_count) !=
            manifest.tensors.parameter_count) {
        fail("tensor manifest layout disagrees with contract");
    }
    require_sha256(
        manifest.tensors.parent_sha256,
        "parent tensor hash");
    require_sha256(
        manifest.tensors.candidate_sha256,
        "candidate tensor hash");
    require_sha256(
        manifest.tensors.xor_delta_sha256,
        "XOR-delta hash");
    if (manifest.tensors.parent_sha256 ==
        manifest.tensors.candidate_sha256) {
        fail("parent and candidate tensor hashes are equal");
    }
}

struct DecodedPayload {
    Manifest manifest;
    TensorBits delta;
};

std::string encode_payload(
    const Manifest& manifest, const TensorBits& delta) {
    validate_manifest(manifest);
    if (delta.hidden_count !=
            manifest.tensors.hidden_count ||
        delta.feature_count !=
            manifest.tensors.feature_count ||
        delta.values.size() !=
            manifest.tensors.parameter_count ||
        tensor_sha256(delta, "priority-xor-delta") !=
            manifest.tensors.xor_delta_sha256) {
        fail("XOR delta disagrees with its manifest");
    }

    Writer output;
    write_contract(output, manifest.contract);
    write_components(
        output, manifest.candidate_components);
    output.u32(manifest.tensors.hidden_count);
    output.u32(manifest.tensors.feature_count);
    output.u64(manifest.tensors.parameter_count);
    output.hash(manifest.tensors.parent_sha256);
    output.hash(manifest.tensors.candidate_sha256);
    output.hash(manifest.tensors.xor_delta_sha256);
    output.u64(delta.values.size());
    for (std::uint64_t mask : delta.values) {
        output.u64(mask);
    }
    return output.take();
}

DecodedPayload decode_payload(std::string_view bytes) {
    Reader input(bytes);
    DecodedPayload decoded;
    decoded.manifest.contract = read_contract(input);
    decoded.manifest.candidate_components =
        read_components(input, "candidate");
    decoded.manifest.tensors.hidden_count =
        input.u32("tensor hidden count");
    decoded.manifest.tensors.feature_count =
        input.u32("tensor feature count");
    decoded.manifest.tensors.parameter_count =
        input.u64("tensor parameter count");
    decoded.manifest.tensors.parent_sha256 =
        input.hash("parent tensor hash");
    decoded.manifest.tensors.candidate_sha256 =
        input.hash("candidate tensor hash");
    decoded.manifest.tensors.xor_delta_sha256 =
        input.hash("XOR-delta hash");
    validate_manifest(decoded.manifest);

    const std::uint64_t delta_count =
        input.u64("XOR-delta count");
    if (delta_count !=
            decoded.manifest.tensors.parameter_count ||
        delta_count > kMaximumParameterCount ||
        delta_count >
            std::numeric_limits<std::size_t>::max()) {
        fail("XOR-delta count is invalid");
    }
    decoded.delta.hidden_count =
        decoded.manifest.tensors.hidden_count;
    decoded.delta.feature_count =
        decoded.manifest.tensors.feature_count;
    decoded.delta.values.resize(
        static_cast<std::size_t>(delta_count));
    for (std::uint64_t& mask : decoded.delta.values) {
        mask = input.u64("XOR-delta mask");
    }
    if (!input.empty()) {
        fail("payload has trailing bytes");
    }
    if (tensor_sha256(
            decoded.delta, "priority-xor-delta") !=
        decoded.manifest.tensors.xor_delta_sha256) {
        fail("XOR-delta hash mismatch");
    }
    return decoded;
}

std::string encode_file(
    const Manifest& manifest, const TensorBits& delta) {
    const std::string payload =
        encode_payload(manifest, delta);
    const std::string payload_sha256 =
        artifact_integrity::sha256_string(payload);
    Writer output;
    output.raw(std::string_view(
        kMagic.data(), kMagic.size()));
    output.u32(kWireVersion);
    output.u32(kEndianMarker);
    output.text(kSchema);
    output.u64(payload.size());
    output.hash(payload_sha256);
    output.raw(payload);
    return output.take();
}

DecodedPayload decode_file(std::string_view bytes) {
    if (bytes.size() > kMaximumArtifactBytes) {
        fail("file exceeds byte limit");
    }
    Reader input(bytes);
    const std::string_view magic =
        input.view(kMagic.size(), "magic");
    if (!std::equal(
            magic.begin(), magic.end(), kMagic.begin())) {
        fail("magic mismatch");
    }
    if (input.u32("wire version") != kWireVersion) {
        fail("schema version mismatch");
    }
    if (input.u32("endian marker") != kEndianMarker) {
        fail("endian marker mismatch");
    }
    if (input.text("schema") != kSchema) {
        fail("schema name mismatch");
    }
    const std::uint64_t payload_size =
        input.u64("payload size");
    if (payload_size > kMaximumArtifactBytes ||
        payload_size >
            std::numeric_limits<std::size_t>::max()) {
        fail("payload size exceeds its bound");
    }
    const std::string payload_sha256 =
        input.hash("payload SHA-256");
    const std::string_view payload = input.view(
        static_cast<std::size_t>(payload_size), "payload");
    if (!input.empty()) {
        fail("file has trailing bytes");
    }
    if (artifact_integrity::sha256_string(payload) !=
        payload_sha256) {
        fail("payload SHA-256 mismatch");
    }
    return decode_payload(payload);
}

struct PreparedArtifact {
    Manifest manifest;
    TensorBits delta;
};

void require_parent(
    std::shared_ptr<const LearnedModel> parent,
    const Contract& contract,
    const std::string& fingerprint,
    const LearnedModelComponentFingerprints& components) {
    if (!parent) {
        fail("parent model is null");
    }
    if (fingerprint != contract.parent.model_fingerprint ||
        components != contract.parent.components) {
        fail("supplied parent does not match contract");
    }
}

PreparedArtifact prepare_artifact(
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate,
    const Contract& contract) {
    validate_contract(contract);
    if (!parent || !candidate) {
        fail("parent or candidate model is null");
    }
    const std::string parent_fingerprint =
        learned_model_fingerprint(parent);
    const auto parent_components =
        learned_model_component_fingerprints(parent);
    require_parent(
        parent, contract, parent_fingerprint,
        parent_components);

    const std::string candidate_fingerprint =
        learned_model_fingerprint(candidate);
    const auto candidate_components =
        learned_model_component_fingerprints(candidate);
    if (candidate_fingerprint !=
        contract.candidate_model_fingerprint) {
        fail("candidate fingerprint does not match contract");
    }
    require_priority_only_change(
        parent_components, candidate_components);

    const TensorBits parent_tensors = tensor_bits(
        learned_priority_head_parameters(parent));
    const TensorBits candidate_tensors = tensor_bits(
        learned_priority_head_parameters(candidate));
    if (parent_tensors.hidden_count !=
            contract.priority_hidden_count ||
        parent_tensors.feature_count !=
            contract.priority_feature_count ||
        parent_tensors.values.size() !=
            contract.priority_parameter_count) {
        fail("live Priority layout does not match contract");
    }
    TensorBits delta =
        xor_delta(parent_tensors, candidate_tensors);
    if (std::all_of(
            delta.values.begin(), delta.values.end(),
            [](std::uint64_t value) {
                return value == 0;
            })) {
        fail("Priority XOR delta is empty");
    }

    Manifest manifest{
        .contract = contract,
        .candidate_components = candidate_components,
        .tensors = {
            .hidden_count = parent_tensors.hidden_count,
            .feature_count = parent_tensors.feature_count,
            .parameter_count =
                parent_tensors.values.size(),
            .parent_sha256 =
                tensor_sha256(
                    parent_tensors, "parent-priority"),
            .candidate_sha256 =
                tensor_sha256(
                    candidate_tensors,
                    "candidate-priority"),
            .xor_delta_sha256 =
                tensor_sha256(
                    delta, "priority-xor-delta"),
        },
    };
    validate_manifest(manifest);

    const TensorBits reconstructed_tensors =
        apply_delta(parent_tensors, delta);
    const auto reconstructed =
        with_learned_priority_head_parameters(
            parent,
            parameters_from_bits(reconstructed_tensors));
    if (learned_model_fingerprint(reconstructed) !=
            candidate_fingerprint ||
        learned_model_component_fingerprints(
            reconstructed) != candidate_components) {
        fail("candidate does not reconstruct exactly from parent");
    }
    if (learned_model_fingerprint(parent) !=
            parent_fingerprint ||
        learned_model_component_fingerprints(parent) !=
            parent_components) {
        fail("artifact preparation mutated parent");
    }
    return {
        .manifest = std::move(manifest),
        .delta = std::move(delta),
    };
}

std::shared_ptr<const LearnedModel> reconstruct(
    std::shared_ptr<const LearnedModel> parent,
    const DecodedPayload& decoded,
    const Contract& expected_contract) {
    if (encoded_contract(decoded.manifest.contract) !=
        encoded_contract(expected_contract)) {
        fail("stored contract does not match expected contract");
    }
    const std::string parent_fingerprint =
        learned_model_fingerprint(parent);
    const auto parent_components =
        learned_model_component_fingerprints(parent);
    require_parent(
        parent, expected_contract, parent_fingerprint,
        parent_components);

    const TensorBits parent_tensors = tensor_bits(
        learned_priority_head_parameters(parent));
    if (parent_tensors.hidden_count !=
            decoded.manifest.tensors.hidden_count ||
        parent_tensors.feature_count !=
            decoded.manifest.tensors.feature_count ||
        parent_tensors.values.size() !=
            decoded.manifest.tensors.parameter_count ||
        tensor_sha256(
            parent_tensors, "parent-priority") !=
            decoded.manifest.tensors.parent_sha256) {
        fail("supplied parent tensors do not match manifest");
    }
    const TensorBits candidate_tensors =
        apply_delta(parent_tensors, decoded.delta);
    if (tensor_sha256(
            candidate_tensors, "candidate-priority") !=
        decoded.manifest.tensors.candidate_sha256) {
        fail("reconstructed candidate tensor hash mismatch");
    }
    const auto candidate =
        with_learned_priority_head_parameters(
            parent,
            parameters_from_bits(candidate_tensors));
    const std::string candidate_fingerprint =
        learned_model_fingerprint(candidate);
    const auto candidate_components =
        learned_model_component_fingerprints(candidate);
    if (candidate_fingerprint !=
            expected_contract.candidate_model_fingerprint ||
        candidate_components !=
            decoded.manifest.candidate_components) {
        fail("reconstructed candidate fingerprint mismatch");
    }
    require_priority_only_change(
        parent_components, candidate_components);
    if (learned_model_fingerprint(parent) !=
            parent_fingerprint ||
        learned_model_component_fingerprints(parent) !=
            parent_components) {
        fail("artifact loading mutated parent");
    }
    return candidate;
}

[[noreturn]] void throw_errno(
    std::string_view operation,
    const std::filesystem::path& path, int error) {
    throw std::system_error(
        error, std::generic_category(),
        std::string(operation) + " '" + path.string() + "'");
}

void require_absent(
    const std::filesystem::path& path,
    std::string_view context) {
    struct stat status {};
    if (::lstat(path.c_str(), &status) == 0) {
        throw std::runtime_error(
            std::string(context) + " already exists: '" +
            path.string() + "'");
    }
    const int error = errno;
    if (error != ENOENT) {
        throw_errno(
            std::string("cannot inspect ") +
                std::string(context),
            path, error);
    }
}

class TemporaryFile {
  public:
    explicit TemporaryFile(std::filesystem::path path)
        : path_(std::move(path)) {}

    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;

    ~TemporaryFile() {
        if (!path_.empty()) {
            static_cast<void>(::unlink(path_.c_str()));
        }
    }

    void release() {
        path_.clear();
    }

  private:
    std::filesystem::path path_;
};

void close_checked(
    int descriptor, const std::filesystem::path& path) {
    while (::close(descriptor) != 0) {
        const int error = errno;
        if (error == EINTR) {
            continue;
        }
        throw_errno("cannot close", path, error);
    }
}

void write_all(
    int descriptor, std::string_view bytes,
    const std::filesystem::path& path) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count = ::write(
            descriptor, bytes.data() + offset,
            bytes.size() - offset);
        if (count < 0) {
            const int error = errno;
            if (error == EINTR) {
                continue;
            }
            throw_errno("cannot write", path, error);
        }
        if (count == 0) {
            throw std::runtime_error(
                "zero-length artifact write");
        }
        offset += static_cast<std::size_t>(count);
    }
}

FileIdentity publish_bytes(
    const std::filesystem::path& destination,
    std::string_view bytes) {
    if (destination.empty() ||
        destination.filename().empty()) {
        throw std::invalid_argument(
            "FQ4 candidate destination is empty");
    }
    if (bytes.empty() ||
        bytes.size() > kMaximumArtifactBytes) {
        throw std::invalid_argument(
            "FQ4 candidate bytes are invalid");
    }
    const std::filesystem::path parent =
        destination.has_parent_path()
            ? destination.parent_path()
            : std::filesystem::path(".");
    std::error_code status_error;
    const auto parent_status =
        std::filesystem::symlink_status(
            parent, status_error);
    if (status_error ||
        !std::filesystem::is_directory(parent_status) ||
        std::filesystem::is_symlink(parent_status)) {
        throw std::runtime_error(
            "FQ4 candidate publication parent is not a "
            "non-symlink directory");
    }
    const std::filesystem::path temporary =
        temporary_path_for(destination);
    require_absent(destination, "destination");
    require_absent(temporary, "publication temporary");

    int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int descriptor = -1;
    while (descriptor < 0) {
        descriptor =
            ::open(temporary.c_str(), flags, 0644);
        if (descriptor < 0 && errno != EINTR) {
            throw_errno(
                "cannot create publication temporary",
                temporary, errno);
        }
    }
    TemporaryFile cleanup(temporary);
    try {
        write_all(descriptor, bytes, temporary);
        while (::fsync(descriptor) != 0) {
            const int error = errno;
            if (error == EINTR) {
                continue;
            }
            throw_errno(
                "cannot sync publication temporary",
                temporary, error);
        }
        close_checked(descriptor, temporary);
        descriptor = -1;
    } catch (...) {
        if (descriptor >= 0) {
            static_cast<void>(::close(descriptor));
        }
        throw;
    }

    if (::link(
            temporary.c_str(),
            destination.c_str()) != 0) {
        throw_errno(
            "cannot publish artifact without replacement",
            destination, errno);
    }
    if (::unlink(temporary.c_str()) != 0) {
        throw_errno(
            "cannot remove publication temporary",
            temporary, errno);
    }
    cleanup.release();

    int directory_flags = O_RDONLY;
#ifdef O_CLOEXEC
    directory_flags |= O_CLOEXEC;
#endif
#ifdef O_DIRECTORY
    directory_flags |= O_DIRECTORY;
#endif
    const int directory_descriptor =
        ::open(parent.c_str(), directory_flags);
    if (directory_descriptor < 0) {
        throw_errno(
            "cannot open publication directory",
            parent, errno);
    }
    if (::fsync(directory_descriptor) != 0) {
        const int error = errno;
        static_cast<void>(::close(directory_descriptor));
        throw_errno(
            "cannot sync publication directory",
            parent, error);
    }
    close_checked(directory_descriptor, parent);

    const auto snapshot =
        artifact_integrity::snapshot_regular_file(
            destination);
    const FileIdentity identity{
        .bytes =
            static_cast<std::uint64_t>(
                snapshot.byte_size),
        .sha256 = snapshot.sha256,
    };
    if (identity.bytes != bytes.size() ||
        identity.sha256 !=
            artifact_integrity::sha256_string(bytes)) {
        throw std::runtime_error(
            "published artifact identity mismatch");
    }
    return identity;
}

std::string read_exact_file(
    const std::filesystem::path& path,
    std::uint64_t byte_count) {
    if (byte_count == 0 ||
        byte_count > kMaximumArtifactBytes ||
        byte_count >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::streamsize>::max())) {
        fail("expected artifact byte count is invalid");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(
            "cannot open FQ4 candidate artifact");
    }
    std::string bytes(
        static_cast<std::size_t>(byte_count), '\0');
    input.read(
        bytes.data(),
        static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() !=
            static_cast<std::streamsize>(bytes.size()) ||
        input.peek() !=
            std::char_traits<char>::eof()) {
        throw std::runtime_error(
            "FQ4 candidate artifact length changed");
    }
    return bytes;
}

void validate_file_identity(
    const FileIdentity& identity) {
    if (identity.bytes == 0 ||
        identity.bytes > kMaximumArtifactBytes) {
        fail("expected artifact byte count is invalid");
    }
    require_sha256(
        identity.sha256, "expected artifact hash");
}

} // namespace

const std::shared_ptr<const LearnedModel>&
LoadedCandidate::model() const {
    return model_;
}

const Manifest& LoadedCandidate::manifest() const {
    return report_.manifest;
}

const Report& LoadedCandidate::report() const {
    return report_;
}

const Contract& production_contract() {
    static const Contract contract{
        .family = "FQ4-DEV1",
        .environment =
            "old-school-environment-v3-cleanup-discard",
        .parent = {
            .artifact_bytes = 3'111'437,
            .artifact_sha256 =
                "53aeb904bd87311b37201859317f05ab0"
                "66bdfe134c72460cf94bff6d1f944ca",
            .model_fingerprint =
                "68126afc5a3e3757eb1d510a056585aa9"
                "74c4f54ce1b4a789ff430f1c7413e2f",
            .components = {
                .critic =
                    "2982b155a02a4a2a3ce8442ae28f6d8c"
                    "f7829103e538c60f0625b3332502e568",
                .priority =
                    "32dc6688a5c970e3eda4325bea5ee4190"
                    "77027e160697899e3b00c963fa1bb22",
                .attack =
                    "dfd3aaa16755bee5d0c2c40956851b94"
                    "ef5676a271a602eb23a57719f7358b01",
                .block =
                    "d64e40796bd1587958b7386996e6a1e5"
                    "660778d40ec7b40b0ee6324b8e39adbb",
                .damage_order =
                    "f0a84ed549bbf95197dd00c13ab04c0a"
                    "4f6b1771f14bdb30a7dca937d2d79c76",
            },
            .training_games = 800,
            .training_seed = 424242,
            .generation = 16,
        },
        .corpus = {
            .artifact_bytes = 2'250'909,
            .artifact_sha256 =
                "0911fc2eb8b14ddc9165543eb1e4c4ed"
                "b0b058256a58dedf61f6c4ea4ca859df",
        },
        .fit = {
            .input_sha256 =
                "586b121c3c9bdb1a61305cac86882cd2"
                "0b5d2ba332b4d5a54defc2c7756393a1",
            .examples = 88,
            .options = 548,
            .check_examples = 0,
            .background_only_examples = 0,
            .optimizer_calls = 1,
            .optimizer = {
                .batch_size = 64,
                .epochs = 16,
                .learning_rate = 0.001,
                .beta1 = 0.9,
                .beta2 = 0.999,
                .epsilon = 1.0e-8,
                .global_gradient_norm_clip = 5.0,
                .seed = 202607280212ULL,
                .residual_weight = 0.10,
                .policy_temperature = 0.10,
            },
        },
        .candidate_model_fingerprint =
            "712600783152e89ff1a53394149764db2"
            "27e55289a656530342226b7e1ee6151",
        .priority_hidden_count = 32,
        .priority_feature_count = 893,
        .priority_parameter_count = 29'534,
        .deployment = {
            .variant =
                LearnedVariant::ValueSearchChampion,
            .training_games = 800,
            .worlds_per_action = 8,
            .horizon_turns = 4,
            .rollouts_per_world = 1,
            .root_search_depth = 1,
            .shallow_prior = true,
            .root_exploration = 0.0,
            .continuation_epsilon = 0.0,
            .priority_residual_weight = 0.10,
            .pass_dominance = false,
            .continuation_controller =
                LearnedContinuationController::Legacy,
            .max_turns = 500,
        },
    };
    static const bool validated = [] {
        validate_contract(contract);
        return true;
    }();
    static_cast<void>(validated);
    return contract;
}

std::filesystem::path production_artifact_path() {
    return std::filesystem::path(kProductionArtifactPath);
}

std::filesystem::path temporary_path_for(
    const std::filesystem::path& destination) {
    if (destination.empty() ||
        destination.filename().empty()) {
        throw std::invalid_argument(
            "FQ4 candidate destination is empty");
    }
    const std::filesystem::path parent =
        destination.has_parent_path()
            ? destination.parent_path()
            : std::filesystem::path(".");
    return parent /
           ("." + destination.filename().string() +
            ".publishing.tmp");
}

std::filesystem::path production_temporary_path() {
    return temporary_path_for(
        production_artifact_path());
}

Report publish_atomic_no_replace(
    const std::filesystem::path& destination,
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate,
    const Contract& contract) {
    const PreparedArtifact prepared =
        prepare_artifact(
            std::move(parent), std::move(candidate),
            contract);
    const std::string bytes =
        encode_file(prepared.manifest, prepared.delta);
    // Exercise the complete decoder before any filesystem state changes.
    const DecodedPayload decoded = decode_file(bytes);
    if (decoded.manifest != prepared.manifest ||
        decoded.delta.hidden_count !=
            prepared.delta.hidden_count ||
        decoded.delta.feature_count !=
            prepared.delta.feature_count ||
        decoded.delta.values != prepared.delta.values) {
        throw std::runtime_error(
            "artifact encoder self-check failed");
    }
    return {
        .artifact =
            publish_bytes(destination, bytes),
        .manifest = prepared.manifest,
    };
}

LoadedCandidate load(
    const std::filesystem::path& path,
    std::shared_ptr<const LearnedModel> parent,
    const Contract& expected_contract,
    const FileIdentity& expected_artifact) {
    validate_contract(expected_contract);
    validate_file_identity(expected_artifact);
    if (!parent) {
        fail("parent model is null");
    }
    const std::string parent_before =
        learned_model_fingerprint(parent);
    const auto parent_components_before =
        learned_model_component_fingerprints(parent);
    require_parent(
        parent, expected_contract, parent_before,
        parent_components_before);

    const auto before =
        artifact_integrity::snapshot_regular_file(path);
    if (before.byte_size != expected_artifact.bytes ||
        before.sha256 != expected_artifact.sha256) {
        throw std::runtime_error(
            "FQ4 candidate artifact identity mismatch");
    }
    const std::string bytes =
        read_exact_file(path, expected_artifact.bytes);
    const auto after =
        artifact_integrity::snapshot_regular_file(path);
    if (after != before ||
        artifact_integrity::sha256_string(bytes) !=
            expected_artifact.sha256) {
        throw std::runtime_error(
            "FQ4 candidate artifact changed while loading");
    }
    const DecodedPayload decoded = decode_file(bytes);
    auto candidate = reconstruct(
        parent, decoded, expected_contract);
    if (learned_model_fingerprint(parent) !=
            parent_before ||
        learned_model_component_fingerprints(parent) !=
            parent_components_before) {
        fail("artifact load mutated supplied parent");
    }

    LoadedCandidate loaded;
    loaded.model_ = std::move(candidate);
    loaded.report_ = {
        .artifact = expected_artifact,
        .manifest = decoded.manifest,
    };
    return loaded;
}

} // namespace old_school::fq4_dev_candidate_artifact
