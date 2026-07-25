#include "alpha/game.hpp"
#include "alpha/learned_iteration.hpp"

#include <algorithm>
#include <atomic>
#include <array>
#include <bit>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <span>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

namespace alpha {

constexpr std::size_t kLearnedCardCount =
    static_cast<std::size_t>(CardId::Moat) + 1;

class ModelFingerprintHash {
  public:
    void add(std::uint64_t value) {
        static constexpr std::array<std::uint64_t, 4> kSalt = {
            0x9E3779B97F4A7C15ULL,
            0xD1B54A32D192ED03ULL,
            0x94D049BB133111EBULL,
            0xBF58476D1CE4E5B9ULL,
        };
        for (std::size_t index = 0; index < state_.size(); ++index) {
            const unsigned int rotation =
                static_cast<unsigned int>(17 + 6 * index);
            state_[index] = std::rotl(
                state_[index] ^ value ^ kSalt[index], rotation);
            state_[index] =
                state_[index] * (kSalt[(index + 1) % kSalt.size()] | 1ULL) +
                kSalt[(index + 2) % kSalt.size()] + count_;
        }
        ++count_;
    }

    std::string finish() const {
        static constexpr char kHex[] = "0123456789abcdef";
        std::array<std::uint64_t, 4> digest = state_;
        for (std::size_t index = 0; index < digest.size(); ++index) {
            digest[index] ^= count_ +
                             0x9E3779B97F4A7C15ULL *
                                 static_cast<std::uint64_t>(index + 1);
            digest[index] ^= digest[index] >> 30;
            digest[index] *= 0xBF58476D1CE4E5B9ULL;
            digest[index] ^= digest[index] >> 27;
            digest[index] *= 0x94D049BB133111EBULL;
            digest[index] ^= digest[index] >> 31;
        }
        std::string result(64, '0');
        for (std::size_t word = 0; word < digest.size(); ++word) {
            for (std::size_t nibble = 0; nibble < 16; ++nibble) {
                const unsigned int shift =
                    static_cast<unsigned int>(60 - 4 * nibble);
                result[word * 16 + nibble] =
                    kHex[(digest[word] >> shift) & 0xFULL];
            }
        }
        return result;
    }

  private:
    std::array<std::uint64_t, 4> state_ = {
        0x6A09E667F3BCC909ULL,
        0xBB67AE8584CAA73BULL,
        0x3C6EF372FE94F82BULL,
        0xA54FF53A5F1D36F1ULL,
    };
    std::uint64_t count_ = 0;
};

void add_model_fingerprint_value(
    ModelFingerprintHash& hash, double value) {
    hash.add(std::bit_cast<std::uint64_t>(value));
}

template <typename Value, std::size_t Size>
void add_model_fingerprint_value(
    ModelFingerprintHash& hash,
    const std::array<Value, Size>& values) {
    hash.add(static_cast<std::uint64_t>(Size));
    for (const Value& value : values) {
        add_model_fingerprint_value(hash, value);
    }
}

namespace {

constexpr std::array<std::uint8_t, 8> kValueG8ArtifactMagic = {
    'M', 'T', 'G', 'V', 'G', '8', 'B', '1',
};
constexpr std::uint32_t kValueG8ArtifactSchema = 1;
constexpr std::string_view kValueG8RecipeId =
    "alpha.learned-value-g8.bootstrap-replay-k1h4.v1";
constexpr std::array<std::uint8_t, 8>
    kValueG8Mix50ArtifactMagic = {
        'M', 'T', 'G', 'V', 'M', '5', 'B', '1',
    };
constexpr std::uint32_t kValueG8Mix50ArtifactSchema = 1;
constexpr std::string_view kValueG8Mix50RecipeId =
    "alpha.learned-value-g8.bootstrap-replay-k1h4."
    "late-mix50.v1";
constexpr std::size_t kMaximumValueG8ArtifactBytes =
    64U * 1024U * 1024U;
constexpr std::size_t kMaximumValueG8ArtifactStringBytes = 256;
constexpr std::size_t kMaximumValueG8ArtifactNodes = 128;
constexpr std::size_t kMaximumValueG8ArtifactDepth = 8;
constexpr std::size_t kMaximumValueG8EnsembleMembers = 16;

static_assert(
    sizeof(double) == sizeof(std::uint64_t) &&
        std::numeric_limits<double>::is_iec559,
    "Value G8 artifacts require IEEE-754 binary64 doubles");

void reject_embedded_nul_in_value_g8_path(
    const std::string& path) {
    if (path.find('\0') != std::string::npos) {
        throw std::invalid_argument(
            "Value G8 artifact path must not contain embedded "
            "NUL bytes");
    }
}

class ValueG8BinaryWriter {
  public:
    void byte(std::uint8_t value) {
        bytes_.push_back(value);
    }

    void bytes(std::span<const std::uint8_t> values) {
        if (values.size() >
            kMaximumValueG8ArtifactBytes - bytes_.size()) {
            throw std::runtime_error(
                "Value G8 artifact exceeds the 64 MiB limit");
        }
        bytes_.insert(bytes_.end(), values.begin(), values.end());
    }

    void unsigned32(std::uint32_t value) {
        for (unsigned int shift = 0; shift < 32; shift += 8) {
            byte(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void unsigned64(std::uint64_t value) {
        for (unsigned int shift = 0; shift < 64; shift += 8) {
            byte(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void size(std::size_t value) {
        unsigned64(static_cast<std::uint64_t>(value));
    }

    void real(double value) {
        if (!std::isfinite(value)) {
            throw std::runtime_error(
                "Value G8 artifact cannot contain a non-finite real");
        }
        unsigned64(std::bit_cast<std::uint64_t>(value));
    }

    void text(std::string_view value) {
        if (value.size() > kMaximumValueG8ArtifactStringBytes) {
            throw std::runtime_error(
                "Value G8 artifact string exceeds its bound");
        }
        unsigned32(static_cast<std::uint32_t>(value.size()));
        bytes(std::span(
            reinterpret_cast<const std::uint8_t*>(value.data()),
            value.size()));
    }

    const std::vector<std::uint8_t>& data() const {
        return bytes_;
    }

  private:
    std::vector<std::uint8_t> bytes_;
};

class ValueG8BinaryReader {
  public:
    explicit ValueG8BinaryReader(
        std::span<const std::uint8_t> bytes)
        : bytes_(bytes) {}

    std::uint8_t byte(std::string_view field) {
        require(1, field);
        return bytes_[cursor_++];
    }

    std::uint32_t unsigned32(std::string_view field) {
        require(4, field);
        std::uint32_t value = 0;
        for (unsigned int shift = 0; shift < 32; shift += 8) {
            value |= static_cast<std::uint32_t>(
                         bytes_[cursor_++])
                     << shift;
        }
        return value;
    }

    std::uint64_t unsigned64(std::string_view field) {
        require(8, field);
        std::uint64_t value = 0;
        for (unsigned int shift = 0; shift < 64; shift += 8) {
            value |= static_cast<std::uint64_t>(
                         bytes_[cursor_++])
                     << shift;
        }
        return value;
    }

    std::size_t size(std::string_view field) {
        const std::uint64_t value = unsigned64(field);
        if (value >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max())) {
            fail(field, "does not fit in size_t");
        }
        return static_cast<std::size_t>(value);
    }

    double real(std::string_view field) {
        const double value =
            std::bit_cast<double>(unsigned64(field));
        if (!std::isfinite(value)) {
            fail(field, "is non-finite");
        }
        return value;
    }

    bool boolean(std::string_view field) {
        const std::uint8_t value = byte(field);
        if (value > 1) {
            fail(field, "is not a canonical boolean");
        }
        return value != 0;
    }

    std::string text(std::string_view field) {
        const std::uint32_t length =
            unsigned32(std::string(field) + " length");
        if (length > kMaximumValueG8ArtifactStringBytes) {
            fail(field, "exceeds the string-size bound");
        }
        require(length, field);
        const char* begin = reinterpret_cast<const char*>(
            bytes_.data() + cursor_);
        std::string value(begin, begin + length);
        cursor_ += length;
        return value;
    }

    std::span<const std::uint8_t> take(
        std::size_t count, std::string_view field) {
        require(count, field);
        const auto value = bytes_.subspan(cursor_, count);
        cursor_ += count;
        return value;
    }

    bool at_end() const {
        return cursor_ == bytes_.size();
    }

    std::size_t remaining() const {
        return bytes_.size() - cursor_;
    }

  private:
    [[noreturn]] static void fail(
        std::string_view field, std::string_view detail) {
        throw std::runtime_error(
            "Value G8 artifact field '" + std::string(field) +
            "' " + std::string(detail));
    }

    void require(std::size_t count, std::string_view field) const {
        if (count > bytes_.size() - cursor_) {
            fail(field, "is truncated");
        }
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t cursor_ = 0;
};

template <typename Value>
void write_value_g8_fixed(
    ValueG8BinaryWriter& writer, const Value& value) {
    if constexpr (std::is_same_v<Value, double>) {
        writer.real(value);
    } else {
        for (const auto& child : value) {
            write_value_g8_fixed(writer, child);
        }
    }
}

template <typename Value>
void read_value_g8_fixed(
    ValueG8BinaryReader& reader, Value& value,
    std::string_view field) {
    if constexpr (std::is_same_v<Value, double>) {
        value = reader.real(field);
    } else {
        for (auto& child : value) {
            read_value_g8_fixed(reader, child, field);
        }
    }
}

std::uint64_t value_g8_payload_checksum(
    std::span<const std::uint8_t> bytes) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const std::uint8_t value : bytes) {
        hash ^= value;
        hash *= 1099511628211ULL;
    }
    hash ^= 0x56414C5545473842ULL;
    hash *= 1099511628211ULL;
    return hash;
}

bool is_lower_hex_fingerprint(std::string_view value) {
    return value.size() == 64 &&
           std::all_of(
               value.begin(), value.end(), [](char character) {
                   return (character >= '0' &&
                           character <= '9') ||
                          (character >= 'a' &&
                           character <= 'f');
               });
}

std::vector<std::uint8_t> read_bounded_value_g8_file(
    const std::string& path) {
    reject_embedded_nul_in_value_g8_path(path);
    std::error_code size_error;
    const std::uintmax_t file_size =
        std::filesystem::file_size(path, size_error);
    if (size_error) {
        throw std::runtime_error(
            "cannot inspect Value G8 artifact '" + path +
            "': " + size_error.message());
    }
    if (file_size > kMaximumValueG8ArtifactBytes) {
        throw std::runtime_error(
            "Value G8 artifact '" + path +
            "' exceeds the 64 MiB limit");
    }
    std::vector<std::uint8_t> bytes(
        static_cast<std::size_t>(file_size));
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(
            "cannot open Value G8 artifact '" + path + "'");
    }
    if (!bytes.empty()) {
        input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    }
    if (input.gcount() !=
        static_cast<std::streamsize>(bytes.size())) {
        throw std::runtime_error(
            "Value G8 artifact '" + path +
            "' was truncated while reading");
    }
    if (input.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error(
            "Value G8 artifact '" + path +
            "' grew while reading");
    }
    return bytes;
}

void write_value_g8_file_atomic(
    const std::string& path,
    std::span<const std::uint8_t> bytes) {
    reject_embedded_nul_in_value_g8_path(path);
    const std::filesystem::path target(path);
    if (path.empty() || target.filename().empty()) {
        throw std::invalid_argument(
            "Value G8 artifact path must name a file");
    }
    const std::filesystem::path directory =
        target.has_parent_path()
            ? target.parent_path()
            : std::filesystem::path(".");
    std::error_code directory_error;
    std::filesystem::create_directories(
        directory, directory_error);
    if (directory_error) {
        throw std::runtime_error(
            "cannot create Value G8 artifact directory '" +
            directory.string() + "': " +
            directory_error.message());
    }

    static std::atomic<std::uint64_t> temporary_counter{0};
    std::filesystem::path temporary;
    int descriptor = -1;
    for (std::size_t attempt = 0; attempt < 128; ++attempt) {
        temporary =
            directory /
            (target.filename().string() + ".tmp." +
             std::to_string(
                 static_cast<unsigned long long>(::getpid())) +
             "." +
             std::to_string(
                 temporary_counter.fetch_add(
                     1, std::memory_order_relaxed)));
        descriptor = ::open(
            temporary.c_str(),
            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
        if (descriptor >= 0) {
            break;
        }
        if (errno != EEXIST) {
            throw std::runtime_error(
                "cannot create temporary Value G8 artifact '" +
                temporary.string() + "': " +
                std::strerror(errno));
        }
    }
    if (descriptor < 0) {
        throw std::runtime_error(
            "could not reserve a temporary Value G8 artifact");
    }

    const auto cleanup = [&] {
        if (descriptor >= 0) {
            static_cast<void>(::close(descriptor));
            descriptor = -1;
        }
        static_cast<void>(::unlink(temporary.c_str()));
    };
    std::size_t cursor = 0;
    while (cursor < bytes.size()) {
        const ssize_t written = ::write(
            descriptor, bytes.data() + cursor,
            bytes.size() - cursor);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            const std::string detail = std::strerror(errno);
            cleanup();
            throw std::runtime_error(
                "cannot write temporary Value G8 artifact: " +
                detail);
        }
        if (written == 0) {
            cleanup();
            throw std::runtime_error(
                "temporary Value G8 artifact write made no "
                "progress");
        }
        cursor += static_cast<std::size_t>(written);
    }
    if (::fsync(descriptor) != 0) {
        const std::string detail = std::strerror(errno);
        cleanup();
        throw std::runtime_error(
            "cannot sync temporary Value G8 artifact: " + detail);
    }
    if (::close(descriptor) != 0) {
        const std::string detail = std::strerror(errno);
        descriptor = -1;
        static_cast<void>(::unlink(temporary.c_str()));
        throw std::runtime_error(
            "cannot close temporary Value G8 artifact: " + detail);
    }
    descriptor = -1;

    const int directory_descriptor = ::open(
        directory.c_str(), O_RDONLY | O_CLOEXEC);
    if (directory_descriptor < 0) {
        const std::string detail = std::strerror(errno);
        static_cast<void>(::unlink(temporary.c_str()));
        throw std::runtime_error(
            "cannot open Value G8 artifact directory for sync: " +
            detail);
    }
    if (::rename(temporary.c_str(), target.c_str()) != 0) {
        const std::string detail = std::strerror(errno);
        static_cast<void>(::close(directory_descriptor));
        static_cast<void>(::unlink(temporary.c_str()));
        throw std::runtime_error(
            "cannot atomically publish Value G8 artifact '" +
            path + "': " + detail);
    }
    if (::fsync(directory_descriptor) != 0) {
        const std::string detail = std::strerror(errno);
        static_cast<void>(::close(directory_descriptor));
        throw std::runtime_error(
            "cannot sync published Value G8 artifact directory: " +
            detail);
    }
    if (::close(directory_descriptor) != 0) {
        const std::string detail = std::strerror(errno);
        throw std::runtime_error(
            "cannot close published Value G8 artifact directory: " +
            detail);
    }
}

}  // namespace

class LearnedModel {
  public:
    static constexpr std::size_t kScalarFeatureCount = 22;
    static constexpr std::size_t kCardPlanes = 14;
    static constexpr std::size_t kFeatureCount =
        kScalarFeatureCount + kCardPlanes * kLearnedCardCount;
    static constexpr std::size_t kHiddenCount = 16;
    static constexpr std::size_t kPolicyDecisionCount = 4;
    static constexpr std::size_t kPolicyPhaseCount = 7;
    static constexpr std::size_t kPolicyVerbCount = 8;
    static constexpr std::size_t kPolicyCardPlanes = 6;
    static constexpr std::size_t kPolicyScalarCount = 36;
    static constexpr std::size_t kPolicyFeatureCount =
        kFeatureCount + kPolicyDecisionCount + kPolicyPhaseCount +
        kPolicyVerbCount + kPolicyCardPlanes * kLearnedCardCount +
        kPolicyScalarCount;
    static constexpr std::size_t kPolicyHiddenCount = 32;
    using FeatureVector = std::array<double, kFeatureCount>;
    using TrainingExample = std::pair<FeatureVector, double>;
    using PolicyFeatureVector =
        std::array<double, kPolicyFeatureCount>;

    struct PolicyTrainingExample {
        std::vector<PolicyFeatureVector> options;
        std::vector<double> target_probabilities;
        std::size_t chosen = 0;
        std::size_t decision_kind = 0;
        double advantage = 0.0;
        double weight = 1.0;
    };

    explicit LearnedModel(std::uint64_t seed,
                          LearnedVariant variant)
        : variant_(variant) {
        std::mt19937_64 random(seed);
        std::normal_distribution<double> initialize(0.0, 0.12);
        for (auto& hidden_weights : input_weights_) {
            for (double& weight : hidden_weights) {
                weight = initialize(random);
            }
        }
        for (double& weight : output_weights_) {
            weight = initialize(random);
        }
        initialize_policy(seed ^ 0x504F4C494359ULL);
    }

    explicit LearnedModel(
        std::vector<std::shared_ptr<const LearnedModel>> members,
        std::uint64_t policy_seed, LearnedVariant variant)
        : variant_(variant), ensemble_(std::move(members)) {
        if (ensemble_.empty()) {
            throw std::invalid_argument(
                "learned ensemble requires at least one member");
        }
        initialize_policy(policy_seed);
    }

    LearnedVariant variant() const {
        return variant_;
    }

    double predict(const FeatureVector& features) const {
        if (!ensemble_.empty()) {
            double total = 0.0;
            for (const auto& member : ensemble_) {
                total += member->predict(features);
            }
            return total /
                   static_cast<double>(ensemble_.size());
        }
        const auto hidden = hidden_values(features);
        double output = output_bias_;
        for (std::size_t index = 0; index < hidden.size(); ++index) {
            output += output_weights_[index] * hidden[index];
        }
        for (std::size_t feature = 0; feature < features.size();
             ++feature) {
            if (features[feature] != 0.0) {
                output += direct_output_weights_[feature] *
                          features[feature];
            }
        }
        return 1.0 / (1.0 + std::exp(-output));
    }

    double policy_logit(const PolicyFeatureVector& features,
                        std::size_t decision_kind) const {
        if (decision_kind >= kPolicyDecisionCount) {
            throw std::out_of_range(
                "unknown Learned policy decision kind");
        }
        const auto hidden =
            policy_hidden_values(features, decision_kind);
        double output = policy_output_bias_[decision_kind];
        for (std::size_t index = 0; index < hidden.size(); ++index) {
            output +=
                policy_output_weights_[decision_kind][index] *
                hidden[index];
        }
        for (std::size_t feature = 0; feature < features.size();
             ++feature) {
            if (features[feature] != 0.0) {
                output +=
                    policy_direct_output_weights_[decision_kind]
                                                 [feature] *
                    features[feature];
            }
        }
        return output;
    }

    void train(const std::vector<TrainingExample>& examples,
               std::size_t epochs, double learning_rate,
               std::uint64_t seed) {
        if (!ensemble_.empty()) {
            throw std::logic_error(
                "cannot train a composite learned ensemble");
        }
        std::mt19937_64 random(seed);
        std::vector<std::size_t> order(examples.size());
        for (std::size_t index = 0; index < order.size(); ++index) {
            order[index] = index;
        }
        for (std::size_t epoch = 0; epoch < epochs; ++epoch) {
            std::shuffle(order.begin(), order.end(), random);
            const double rate =
                learning_rate /
                (1.0 + 0.15 * static_cast<double>(epoch));
            for (const std::size_t example_index : order) {
                const auto& [features, target] =
                    examples[example_index];
                const auto hidden = hidden_values(features);
                double output_sum = output_bias_;
                for (std::size_t index = 0; index < hidden.size();
                     ++index) {
                    output_sum +=
                        output_weights_[index] * hidden[index];
                }
                for (std::size_t feature = 0;
                     feature < features.size(); ++feature) {
                    if (features[feature] != 0.0) {
                        output_sum +=
                            direct_output_weights_[feature] *
                            features[feature];
                    }
                }
                const double output =
                    1.0 / (1.0 + std::exp(-output_sum));
                // Sigmoid + cross entropy derivative.
                const double output_error = output - target;
                const auto old_output_weights = output_weights_;

                for (std::size_t hidden_index = 0;
                     hidden_index < kHiddenCount; ++hidden_index) {
                    output_weights_[hidden_index] -=
                        rate * output_error * hidden[hidden_index];
                }
                for (std::size_t feature = 0;
                     feature < features.size(); ++feature) {
                    if (features[feature] != 0.0) {
                        direct_output_weights_[feature] -=
                            rate * output_error *
                            features[feature];
                    }
                }
                output_bias_ -= rate * output_error;

                for (std::size_t hidden_index = 0;
                     hidden_index < kHiddenCount; ++hidden_index) {
                    const double hidden_error =
                        output_error *
                        old_output_weights[hidden_index] *
                        (1.0 - hidden[hidden_index] *
                                   hidden[hidden_index]);
                    for (std::size_t feature = 0;
                         feature < kFeatureCount; ++feature) {
                        if (features[feature] != 0.0) {
                            input_weights_[hidden_index][feature] -=
                                rate * hidden_error *
                                features[feature];
                        }
                    }
                    hidden_biases_[hidden_index] -= rate * hidden_error;
                }
            }
        }
    }

    void train_policy(
        const std::vector<PolicyTrainingExample>& examples,
        std::size_t epochs, double learning_rate,
        std::uint64_t seed) {
        if (examples.empty()) {
            return;
        }

        std::mt19937_64 random(seed);
        std::vector<std::size_t> order(examples.size());
        for (std::size_t index = 0; index < order.size(); ++index) {
            order[index] = index;
        }
        for (std::size_t epoch = 0; epoch < epochs; ++epoch) {
            std::shuffle(order.begin(), order.end(), random);
            const double rate =
                learning_rate /
                (1.0 + 0.15 * static_cast<double>(epoch));
            for (const std::size_t example_index : order) {
                const auto& example = examples[example_index];
                if (example.options.size() < 2 ||
                    example.chosen >= example.options.size() ||
                    example.decision_kind >=
                        kPolicyDecisionCount) {
                    continue;
                }
                const std::size_t decision_kind =
                    example.decision_kind;

                std::vector<std::array<double, kPolicyHiddenCount>>
                    hidden;
                std::vector<double> logits;
                hidden.reserve(example.options.size());
                logits.reserve(example.options.size());
                for (const auto& option : example.options) {
                    hidden.push_back(policy_hidden_values(
                        option, decision_kind));
                    double logit =
                        policy_output_bias_[decision_kind];
                    for (std::size_t hidden_index = 0;
                         hidden_index < kPolicyHiddenCount;
                         ++hidden_index) {
                        logit +=
                            policy_output_weights_[decision_kind]
                                                  [hidden_index] *
                            hidden.back()[hidden_index];
                    }
                    for (std::size_t feature = 0;
                         feature < option.size(); ++feature) {
                        if (option[feature] != 0.0) {
                            logit +=
                                policy_direct_output_weights_
                                    [decision_kind][feature] *
                                option[feature];
                        }
                    }
                    logits.push_back(logit);
                }

                const double max_logit =
                    *std::max_element(logits.begin(), logits.end());
                std::vector<double> probabilities(logits.size());
                double probability_sum = 0.0;
                for (std::size_t index = 0;
                     index < logits.size(); ++index) {
                    probabilities[index] =
                        std::exp(logits[index] - max_logit);
                    probability_sum += probabilities[index];
                }
                double probability_log_average = 0.0;
                for (double& probability : probabilities) {
                    probability /= probability_sum;
                    probability_log_average +=
                        probability *
                        std::log(std::max(probability, 1.0e-12));
                }

                const auto old_output_weights =
                    policy_output_weights_[decision_kind];
                constexpr double entropy_weight = 0.01;
                const bool has_soft_target =
                    example.target_probabilities.size() ==
                    example.options.size();
                for (std::size_t option_index = 0;
                     option_index < example.options.size();
                     ++option_index) {
                    const double chosen =
                        option_index == example.chosen ? 1.0 : 0.0;
                    const double policy_gradient =
                        has_soft_target
                            ? example.weight *
                                  (probabilities[option_index] -
                                   example.target_probabilities
                                       [option_index])
                            : example.weight * example.advantage *
                                  (probabilities[option_index] -
                                   chosen);
                    const double entropy_gradient =
                        has_soft_target
                            ? 0.0
                            : entropy_weight * example.weight *
                                  probabilities[option_index] *
                                  (std::log(std::max(
                                       probabilities[option_index],
                                       1.0e-12)) -
                                   probability_log_average);
                    const double output_error = std::clamp(
                        policy_gradient + entropy_gradient, -1.0, 1.0);

                    for (std::size_t hidden_index = 0;
                         hidden_index < kPolicyHiddenCount;
                         ++hidden_index) {
                        policy_output_weights_[decision_kind]
                                              [hidden_index] -=
                            rate * output_error *
                            hidden[option_index][hidden_index];
                    }
                    for (std::size_t feature = 0;
                         feature < kPolicyFeatureCount; ++feature) {
                        if (example.options[option_index][feature] !=
                            0.0) {
                            policy_direct_output_weights_
                                [decision_kind][feature] -=
                                rate * output_error *
                                example.options[option_index][feature];
                        }
                    }
                    policy_output_bias_[decision_kind] -=
                        rate * output_error;

                    for (std::size_t hidden_index = 0;
                         hidden_index < kPolicyHiddenCount;
                         ++hidden_index) {
                        const double hidden_error =
                            output_error *
                            old_output_weights[hidden_index] *
                            (1.0 -
                             hidden[option_index][hidden_index] *
                                 hidden[option_index][hidden_index]);
                        for (std::size_t feature = 0;
                             feature < kPolicyFeatureCount; ++feature) {
                            if (example.options[option_index][feature] !=
                                0.0) {
                                policy_input_weights_
                                    [decision_kind][hidden_index]
                                    [feature] -=
                                    rate * hidden_error *
                                    example.options[option_index][feature];
                            }
                        }
                        policy_hidden_biases_[decision_kind]
                                               [hidden_index] -=
                            rate * hidden_error;
                    }
                }
            }
        }
    }

    std::shared_ptr<LearnedModel> deep_clone_mutable(
        std::vector<std::shared_ptr<LearnedModel>>& critic_leaves)
        const {
        // The implicit copy is intentionally followed by recursive
        // replacement of every ensemble pointer. Published models may share
        // immutable children; a training candidate must not.
        auto clone =
            std::shared_ptr<LearnedModel>(new LearnedModel(*this));
        clone->ensemble_.clear();
        if (ensemble_.empty()) {
            critic_leaves.push_back(clone);
            return clone;
        }
        clone->ensemble_.reserve(ensemble_.size());
        for (const auto& member : ensemble_) {
            if (!member) {
                throw std::logic_error(
                    "cannot clone a null Learned critic member");
            }
            clone->ensemble_.push_back(
                member->deep_clone_mutable(critic_leaves));
        }
        return clone;
    }

  private:
    void initialize_policy(std::uint64_t seed) {
        std::mt19937_64 random(seed);
        std::normal_distribution<double> initialize(0.0, 0.06);
        for (auto& decision_weights : policy_input_weights_) {
            for (auto& hidden_weights : decision_weights) {
                for (double& weight : hidden_weights) {
                    weight = initialize(random);
                }
            }
        }
        // Zero output paths make generation-zero behavior exactly uniform.
        // The random hidden trunk only breaks symmetry after the first
        // supervised/on-policy update.
    }

    std::array<double, kHiddenCount>
    hidden_values(const FeatureVector& features) const {
        std::array<double, kHiddenCount> hidden;
        for (std::size_t hidden_index = 0;
             hidden_index < kHiddenCount; ++hidden_index) {
            double sum = hidden_biases_[hidden_index];
            for (std::size_t feature = 0; feature < kFeatureCount;
                 ++feature) {
                if (features[feature] != 0.0) {
                    sum += input_weights_[hidden_index][feature] *
                           features[feature];
                }
            }
            hidden[hidden_index] = std::tanh(sum);
        }
        return hidden;
    }

    std::array<double, kPolicyHiddenCount> policy_hidden_values(
        const PolicyFeatureVector& features,
        std::size_t decision_kind) const {
        std::array<double, kPolicyHiddenCount> hidden;
        for (std::size_t hidden_index = 0;
             hidden_index < kPolicyHiddenCount; ++hidden_index) {
            double sum =
                policy_hidden_biases_[decision_kind][hidden_index];
            for (std::size_t feature = 0;
                 feature < kPolicyFeatureCount; ++feature) {
                if (features[feature] != 0.0) {
                    sum +=
                        policy_input_weights_
                            [decision_kind][hidden_index][feature] *
                        features[feature];
                }
            }
            hidden[hidden_index] = std::tanh(sum);
        }
        return hidden;
    }

    void append_fingerprint(ModelFingerprintHash& hash) const {
        hash.add(0x4C4541524E45444DULL);
        hash.add(static_cast<std::uint64_t>(variant_));
        hash.add(static_cast<std::uint64_t>(kFeatureCount));
        hash.add(static_cast<std::uint64_t>(kHiddenCount));
        hash.add(static_cast<std::uint64_t>(kPolicyFeatureCount));
        hash.add(static_cast<std::uint64_t>(kPolicyHiddenCount));
        add_model_fingerprint_value(hash, input_weights_);
        add_model_fingerprint_value(hash, hidden_biases_);
        add_model_fingerprint_value(hash, output_weights_);
        add_model_fingerprint_value(hash, direct_output_weights_);
        add_model_fingerprint_value(hash, output_bias_);
        add_model_fingerprint_value(hash, policy_input_weights_);
        add_model_fingerprint_value(hash, policy_hidden_biases_);
        add_model_fingerprint_value(hash, policy_output_weights_);
        add_model_fingerprint_value(
            hash, policy_direct_output_weights_);
        add_model_fingerprint_value(hash, policy_output_bias_);
        hash.add(static_cast<std::uint64_t>(ensemble_.size()));
        for (const auto& member : ensemble_) {
            hash.add(member ? 1ULL : 0ULL);
            if (member) {
                member->append_fingerprint(hash);
            }
        }
    }

    friend std::string learned_model_fingerprint(
        std::shared_ptr<const LearnedModel> model);
    friend void write_learned_value_g8_bundle_atomic(
        const std::string& path,
        const LearnedValueG8Result& result);
    friend LearnedValueG8Result load_learned_value_g8_bundle(
        const std::string& path,
        std::size_t expected_training_games,
        std::uint64_t expected_seed);
    friend void write_learned_value_g8_mix50_bundle_atomic(
        const std::string& path,
        const LearnedValueG8Result& result);
    friend LearnedValueG8Result
    load_learned_value_g8_mix50_bundle(
        const std::string& path,
        std::size_t expected_training_games,
        std::uint64_t expected_seed);

    std::array<std::array<double, kFeatureCount>, kHiddenCount>
        input_weights_{};
    std::array<double, kHiddenCount> hidden_biases_{};
    std::array<double, kHiddenCount> output_weights_{};
    std::array<double, kFeatureCount> direct_output_weights_{};
    double output_bias_ = 0.0;
    std::array<std::array<
                   std::array<double, kPolicyFeatureCount>,
                   kPolicyHiddenCount>,
               kPolicyDecisionCount>
        policy_input_weights_{};
    std::array<std::array<double, kPolicyHiddenCount>,
               kPolicyDecisionCount>
        policy_hidden_biases_{};
    std::array<std::array<double, kPolicyHiddenCount>,
               kPolicyDecisionCount>
        policy_output_weights_{};
    std::array<std::array<double, kPolicyFeatureCount>,
               kPolicyDecisionCount>
        policy_direct_output_weights_{};
    std::array<double, kPolicyDecisionCount> policy_output_bias_{};
    LearnedVariant variant_;
    std::vector<std::shared_ptr<const LearnedModel>> ensemble_;
};

std::string learned_model_fingerprint(
    std::shared_ptr<const LearnedModel> model) {
    if (!model) {
        throw std::invalid_argument(
            "cannot fingerprint a null Learned model");
    }
    ModelFingerprintHash hash;
    // Domain and schema bind the digest to this exact serialization. Bump
    // the schema whenever field order or fingerprint semantics change.
    hash.add(0x4D41474943414946ULL);
    hash.add(1);
    model->append_fingerprint(hash);
    return hash.finish();
}

namespace {

void add_value_g8_report_text(
    ModelFingerprintHash& hash, std::string_view value) {
    hash.add(static_cast<std::uint64_t>(value.size()));
    for (const unsigned char character : value) {
        hash.add(character);
    }
}

std::string value_g8_report_fingerprint(
    const LearnedValueG8Report& report) {
    ModelFingerprintHash hash;
    hash.add(0x5647385245504F52ULL);
    hash.add(1);
    hash.add(static_cast<std::uint64_t>(
        report.training_games));
    hash.add(report.root_seed);
    hash.add(static_cast<std::uint64_t>(
        report.base_examples));
    add_value_g8_report_text(hash, report.base_fingerprint);
    add_value_g8_report_text(hash, report.final_fingerprint);
    hash.add(static_cast<std::uint64_t>(
        report.generations.size()));
    for (const auto& generation : report.generations) {
        hash.add(static_cast<std::uint64_t>(
            generation.generation));
        hash.add(static_cast<std::uint64_t>(
            generation.self_play_games));
        hash.add(static_cast<std::uint64_t>(
            generation.generation_examples));
        hash.add(static_cast<std::uint64_t>(
            generation.anchor_examples));
        hash.add(static_cast<std::uint64_t>(
            generation.replay_generations));
        hash.add(static_cast<std::uint64_t>(
            generation.replay_examples));
        hash.add(generation.search_enabled ? 1U : 0U);
        hash.add(static_cast<std::uint64_t>(
            generation.search_worlds));
        hash.add(static_cast<std::uint64_t>(
            generation.search_horizon_turns));
        hash.add(static_cast<std::uint64_t>(
            generation.rollout_evaluations));
        hash.add(std::bit_cast<std::uint64_t>(
            generation.exploration_rate));
        add_value_g8_report_text(
            hash, generation.parent_fingerprint);
        add_value_g8_report_text(
            hash, generation.candidate_fingerprint);
    }
    return hash.finish();
}

std::string value_g8_mix50_report_fingerprint(
    const LearnedValueG8Report& report) {
    ModelFingerprintHash hash;
    hash.add(0x5647384D49583530ULL);
    hash.add(1);
    add_value_g8_report_text(
        hash, value_g8_report_fingerprint(report));
    hash.add(static_cast<std::uint64_t>(report.recipe));
    for (const auto& generation : report.generations) {
        hash.add(static_cast<std::uint64_t>(
            generation.raw_collection_games));
        hash.add(static_cast<std::uint64_t>(
            generation.search_collection_games));
        hash.add(static_cast<std::uint64_t>(
            generation.raw_collection_examples));
        hash.add(static_cast<std::uint64_t>(
            generation.search_collection_examples));
    }
    return hash.finish();
}

[[noreturn]] void invalid_value_g8_bundle(
    std::string_view detail) {
    throw std::runtime_error(
        "invalid Value G8 artifact bundle: " +
        std::string(detail));
}

std::vector<std::string> validate_value_g8_bundle_for_recipe(
    const LearnedValueG8Result& result,
    LearnedValueG8Recipe expected_recipe) {
    if (result.report.recipe != expected_recipe) {
        invalid_value_g8_bundle("recipe does not match writer");
    }
    if (result.report.training_games == 0) {
        invalid_value_g8_bundle(
            "training_games must be positive");
    }
    if (result.report.base_examples == 0) {
        invalid_value_g8_bundle(
            "base_examples must be positive");
    }
    if (result.checkpoints.size() !=
        kLearnedValueG8Generations + 1) {
        invalid_value_g8_bundle(
            "checkpoint count must be exactly 9");
    }
    if (result.report.generations.size() !=
        kLearnedValueG8Generations) {
        invalid_value_g8_bundle(
            "generation report count must be exactly 8");
    }
    if (!result.model ||
        result.model != result.checkpoints.back()) {
        invalid_value_g8_bundle(
            "model must alias the final checkpoint");
    }

    std::vector<std::string> fingerprints;
    fingerprints.reserve(result.checkpoints.size());
    for (std::size_t index = 0;
         index < result.checkpoints.size(); ++index) {
        const auto& checkpoint = result.checkpoints[index];
        if (!checkpoint) {
            invalid_value_g8_bundle(
                "checkpoint " + std::to_string(index) +
                " is null");
        }
        if (checkpoint->variant() !=
            LearnedVariant::ValueSearchChampion) {
            invalid_value_g8_bundle(
                "checkpoint " + std::to_string(index) +
                " is not a Value model");
        }
        fingerprints.push_back(
            learned_model_fingerprint(checkpoint));
    }
    if (!is_lower_hex_fingerprint(
            result.report.base_fingerprint) ||
        result.report.base_fingerprint !=
            fingerprints.front()) {
        invalid_value_g8_bundle(
            "base_fingerprint does not match checkpoint G0");
    }
    if (!is_lower_hex_fingerprint(
            result.report.final_fingerprint) ||
        result.report.final_fingerprint !=
            fingerprints.back()) {
        invalid_value_g8_bundle(
            "final_fingerprint does not match checkpoint G8");
    }

    const std::size_t expected_self_play_games =
        std::max<std::size_t>(
            1, result.report.training_games / 4);
    for (std::size_t index = 0;
         index < result.report.generations.size();
         ++index) {
        const auto& generation =
            result.report.generations[index];
        if (generation.generation != index + 1) {
            invalid_value_g8_bundle(
                "generation numbering is not canonical at G" +
                std::to_string(index + 1));
        }
        if (generation.self_play_games !=
            expected_self_play_games) {
            invalid_value_g8_bundle(
                "self_play_games is not canonical at G" +
                std::to_string(index + 1));
        }
        if (generation.generation_examples == 0 ||
            generation.anchor_examples !=
                result.report.base_examples) {
            invalid_value_g8_bundle(
                "example accounting is invalid at G" +
                std::to_string(index + 1));
        }
        const std::size_t expected_replay_generations =
            std::min<std::size_t>(index + 1, 3);
        std::size_t expected_replay_examples = 0;
        const std::size_t replay_begin =
            index + 1 - expected_replay_generations;
        for (std::size_t replay_index = replay_begin;
             replay_index <= index; ++replay_index) {
            const std::size_t examples =
                result.report.generations[replay_index]
                    .generation_examples;
            if (examples >
                std::numeric_limits<std::size_t>::max() -
                    expected_replay_examples) {
                invalid_value_g8_bundle(
                    "replay example count overflows size_t");
            }
            expected_replay_examples += examples;
        }
        if (generation.replay_generations !=
                expected_replay_generations ||
            generation.replay_examples !=
                expected_replay_examples) {
            invalid_value_g8_bundle(
                "replay accounting is invalid at G" +
                std::to_string(index + 1));
        }
        const bool expected_search = index >= 4;
        if (expected_recipe ==
            LearnedValueG8Recipe::CanonicalAllSearchLate) {
            if (generation.raw_collection_games != 0 ||
                generation.search_collection_games != 0 ||
                generation.raw_collection_examples != 0 ||
                generation.search_collection_examples != 0) {
                invalid_value_g8_bundle(
                    "canonical collection breakdown must remain "
                    "absent at G" +
                    std::to_string(index + 1));
            }
        } else {
            const std::size_t expected_raw_games =
                expected_search
                    ? expected_self_play_games / 2
                    : expected_self_play_games;
            const std::size_t expected_search_games =
                expected_search
                    ? expected_self_play_games / 2
                    : 0;
            if (expected_self_play_games % 2 != 0 ||
                generation.raw_collection_games !=
                    expected_raw_games ||
                generation.search_collection_games !=
                    expected_search_games ||
                generation.raw_collection_games +
                        generation.search_collection_games !=
                    generation.self_play_games ||
                generation.raw_collection_examples +
                        generation.search_collection_examples !=
                    generation.generation_examples ||
                generation.raw_collection_examples == 0 ||
                (expected_search &&
                 generation.search_collection_examples == 0) ||
                (!expected_search &&
                 generation.search_collection_examples != 0)) {
                invalid_value_g8_bundle(
                    "Late-Mix50 collection accounting is invalid "
                    "at G" +
                    std::to_string(index + 1));
            }
        }
        if (generation.search_enabled != expected_search ||
            generation.search_worlds !=
                (expected_search ? 1U : 0U) ||
            generation.search_horizon_turns !=
                (expected_search ? 4U : 0U) ||
            (!expected_search &&
             generation.rollout_evaluations != 0) ||
            (expected_search &&
             generation.rollout_evaluations == 0)) {
            invalid_value_g8_bundle(
                "search accounting is invalid at G" +
                std::to_string(index + 1));
        }
        const double expected_exploration =
            index < 2 ? 0.10 : 0.05;
        if (std::bit_cast<std::uint64_t>(
                generation.exploration_rate) !=
            std::bit_cast<std::uint64_t>(
                expected_exploration)) {
            invalid_value_g8_bundle(
                "exploration rate is not canonical at G" +
                std::to_string(index + 1));
        }
        if (!is_lower_hex_fingerprint(
                generation.parent_fingerprint) ||
            !is_lower_hex_fingerprint(
                generation.candidate_fingerprint) ||
            generation.parent_fingerprint !=
                fingerprints[index] ||
            generation.candidate_fingerprint !=
                fingerprints[index + 1] ||
            generation.parent_fingerprint ==
                generation.candidate_fingerprint) {
            invalid_value_g8_bundle(
                "checkpoint fingerprint link is invalid at G" +
                std::to_string(index + 1));
        }
    }
    return fingerprints;
}

std::vector<std::string> validate_value_g8_bundle(
    const LearnedValueG8Result& result) {
    return validate_value_g8_bundle_for_recipe(
        result,
        LearnedValueG8Recipe::CanonicalAllSearchLate);
}

std::vector<std::string> validate_value_g8_mix50_bundle(
    const LearnedValueG8Result& result) {
    return validate_value_g8_bundle_for_recipe(
        result, LearnedValueG8Recipe::LateMix50);
}

std::shared_ptr<const LearnedModel>
update_learned_value_model_encoded(
    std::shared_ptr<const LearnedModel> parent,
    const std::vector<LearnedModel::TrainingExample>& examples,
    LearnedValueUpdateConfig config) {
    if (!parent) {
        throw std::invalid_argument(
            "Learned Value update requires a parent model");
    }
    if (parent->variant() !=
        LearnedVariant::ValueSearchChampion) {
        throw std::invalid_argument(
            "Learned Value update requires a Value model");
    }
    if (!examples.empty() &&
        (config.epochs == 0 ||
         !std::isfinite(config.learning_rate) ||
         config.learning_rate <= 0.0)) {
        throw std::invalid_argument(
            "Learned Value update parameters must be positive");
    }

    std::vector<std::shared_ptr<LearnedModel>> critic_leaves;
    auto candidate =
        parent->deep_clone_mutable(critic_leaves);
    std::vector<std::jthread> trainers;
    trainers.reserve(critic_leaves.size());
    for (std::size_t member = 0;
         member < critic_leaves.size(); ++member) {
        trainers.emplace_back([&, member] {
            critic_leaves[member]->train(
                examples, config.epochs,
                config.learning_rate,
                config.root_seed ^
                    (config.member_training_tag + member));
        });
    }
    // jthread destruction joins every independent critic fit before the
    // immutable candidate is published.
    trainers.clear();
    return candidate;
}

} // namespace

std::string learned_value_g8_cache_path(
    std::size_t training_games, std::uint64_t seed) {
    return "build/model-cache/value-g8-v1-t" +
           std::to_string(training_games) + "-s" +
           std::to_string(seed) + ".bin";
}

std::string learned_value_g8_mix50_cache_path(
    std::size_t training_games, std::uint64_t seed) {
    return "build/model-cache/value-g8-mix50-v1-t" +
           std::to_string(training_games) + "-s" +
           std::to_string(seed) + ".bin";
}

void write_learned_value_g8_bundle_atomic(
    const std::string& path,
    const LearnedValueG8Result& result) {
    const std::vector<std::string> fingerprints =
        validate_value_g8_bundle(result);

    ValueG8BinaryWriter payload;
    payload.text(kValueG8RecipeId);
    payload.size(kLearnedCardCount);
    payload.size(LearnedModel::kScalarFeatureCount);
    payload.size(LearnedModel::kCardPlanes);
    payload.size(LearnedModel::kFeatureCount);
    payload.size(LearnedModel::kHiddenCount);
    payload.size(LearnedModel::kPolicyDecisionCount);
    payload.size(LearnedModel::kPolicyPhaseCount);
    payload.size(LearnedModel::kPolicyVerbCount);
    payload.size(LearnedModel::kPolicyCardPlanes);
    payload.size(LearnedModel::kPolicyScalarCount);
    payload.size(LearnedModel::kPolicyFeatureCount);
    payload.size(LearnedModel::kPolicyHiddenCount);

    payload.size(result.report.training_games);
    payload.unsigned64(result.report.root_seed);
    payload.text(value_g8_report_fingerprint(result.report));
    payload.size(result.report.training_games);
    payload.unsigned64(result.report.root_seed);
    payload.size(result.report.base_examples);
    payload.text(result.report.base_fingerprint);
    payload.text(result.report.final_fingerprint);
    payload.size(result.report.generations.size());
    for (const auto& generation : result.report.generations) {
        payload.size(generation.generation);
        payload.size(generation.self_play_games);
        payload.size(generation.generation_examples);
        payload.size(generation.anchor_examples);
        payload.size(generation.replay_generations);
        payload.size(generation.replay_examples);
        payload.byte(generation.search_enabled ? 1U : 0U);
        payload.size(generation.search_worlds);
        payload.size(generation.search_horizon_turns);
        payload.size(generation.rollout_evaluations);
        payload.real(generation.exploration_rate);
        payload.text(generation.parent_fingerprint);
        payload.text(generation.candidate_fingerprint);
    }

    payload.size(result.checkpoints.size());
    std::size_t node_count = 0;
    std::function<void(
        const std::shared_ptr<const LearnedModel>&,
        std::size_t)>
        write_model;
    write_model =
        [&](const std::shared_ptr<const LearnedModel>& model,
            std::size_t depth) {
            if (!model) {
                invalid_value_g8_bundle(
                    "serialized model node is null");
            }
            if (depth > kMaximumValueG8ArtifactDepth ||
                ++node_count >
                    kMaximumValueG8ArtifactNodes) {
                invalid_value_g8_bundle(
                    "serialized model graph exceeds its bound");
            }
            if (model->ensemble_.size() >
                kMaximumValueG8EnsembleMembers) {
                invalid_value_g8_bundle(
                    "serialized ensemble exceeds its bound");
            }
            payload.unsigned32(0x4D4F444C);
            payload.unsigned32(
                static_cast<std::uint32_t>(model->variant_));
            write_value_g8_fixed(
                payload, model->input_weights_);
            write_value_g8_fixed(
                payload, model->hidden_biases_);
            write_value_g8_fixed(
                payload, model->output_weights_);
            write_value_g8_fixed(
                payload, model->direct_output_weights_);
            payload.real(model->output_bias_);
            write_value_g8_fixed(
                payload, model->policy_input_weights_);
            write_value_g8_fixed(
                payload, model->policy_hidden_biases_);
            write_value_g8_fixed(
                payload, model->policy_output_weights_);
            write_value_g8_fixed(
                payload,
                model->policy_direct_output_weights_);
            write_value_g8_fixed(
                payload, model->policy_output_bias_);
            payload.unsigned32(
                static_cast<std::uint32_t>(
                    model->ensemble_.size()));
            for (const auto& member : model->ensemble_) {
                write_model(member, depth + 1);
            }
        };
    for (std::size_t index = 0;
         index < result.checkpoints.size(); ++index) {
        payload.text(fingerprints[index]);
        write_model(result.checkpoints[index], 0);
    }

    ValueG8BinaryWriter file;
    file.bytes(kValueG8ArtifactMagic);
    file.unsigned32(kValueG8ArtifactSchema);
    file.size(payload.data().size());
    file.unsigned64(
        value_g8_payload_checksum(payload.data()));
    file.bytes(payload.data());
    write_value_g8_file_atomic(path, file.data());
}

void write_learned_value_g8_mix50_bundle_atomic(
    const std::string& path,
    const LearnedValueG8Result& result) {
    const std::vector<std::string> fingerprints =
        validate_value_g8_mix50_bundle(result);

    ValueG8BinaryWriter payload;
    payload.text(kValueG8Mix50RecipeId);
    payload.size(kLearnedCardCount);
    payload.size(LearnedModel::kScalarFeatureCount);
    payload.size(LearnedModel::kCardPlanes);
    payload.size(LearnedModel::kFeatureCount);
    payload.size(LearnedModel::kHiddenCount);
    payload.size(LearnedModel::kPolicyDecisionCount);
    payload.size(LearnedModel::kPolicyPhaseCount);
    payload.size(LearnedModel::kPolicyVerbCount);
    payload.size(LearnedModel::kPolicyCardPlanes);
    payload.size(LearnedModel::kPolicyScalarCount);
    payload.size(LearnedModel::kPolicyFeatureCount);
    payload.size(LearnedModel::kPolicyHiddenCount);

    payload.size(result.report.training_games);
    payload.unsigned64(result.report.root_seed);
    payload.text(
        value_g8_mix50_report_fingerprint(result.report));
    payload.size(result.report.training_games);
    payload.unsigned64(result.report.root_seed);
    payload.size(result.report.base_examples);
    payload.text(result.report.base_fingerprint);
    payload.text(result.report.final_fingerprint);
    payload.size(result.report.generations.size());
    for (const auto& generation : result.report.generations) {
        payload.size(generation.generation);
        payload.size(generation.self_play_games);
        payload.size(generation.generation_examples);
        payload.size(generation.anchor_examples);
        payload.size(generation.replay_generations);
        payload.size(generation.replay_examples);
        payload.size(generation.raw_collection_games);
        payload.size(generation.search_collection_games);
        payload.size(generation.raw_collection_examples);
        payload.size(generation.search_collection_examples);
        payload.byte(generation.search_enabled ? 1U : 0U);
        payload.size(generation.search_worlds);
        payload.size(generation.search_horizon_turns);
        payload.size(generation.rollout_evaluations);
        payload.real(generation.exploration_rate);
        payload.text(generation.parent_fingerprint);
        payload.text(generation.candidate_fingerprint);
    }

    payload.size(result.checkpoints.size());
    std::size_t node_count = 0;
    std::function<void(
        const std::shared_ptr<const LearnedModel>&,
        std::size_t)>
        write_model;
    write_model =
        [&](const std::shared_ptr<const LearnedModel>& model,
            std::size_t depth) {
            if (!model) {
                invalid_value_g8_bundle(
                    "serialized model node is null");
            }
            if (depth > kMaximumValueG8ArtifactDepth ||
                ++node_count >
                    kMaximumValueG8ArtifactNodes) {
                invalid_value_g8_bundle(
                    "serialized model graph exceeds its bound");
            }
            if (model->ensemble_.size() >
                kMaximumValueG8EnsembleMembers) {
                invalid_value_g8_bundle(
                    "serialized ensemble exceeds its bound");
            }
            payload.unsigned32(0x4D4F444C);
            payload.unsigned32(
                static_cast<std::uint32_t>(model->variant_));
            write_value_g8_fixed(
                payload, model->input_weights_);
            write_value_g8_fixed(
                payload, model->hidden_biases_);
            write_value_g8_fixed(
                payload, model->output_weights_);
            write_value_g8_fixed(
                payload, model->direct_output_weights_);
            payload.real(model->output_bias_);
            write_value_g8_fixed(
                payload, model->policy_input_weights_);
            write_value_g8_fixed(
                payload, model->policy_hidden_biases_);
            write_value_g8_fixed(
                payload, model->policy_output_weights_);
            write_value_g8_fixed(
                payload,
                model->policy_direct_output_weights_);
            write_value_g8_fixed(
                payload, model->policy_output_bias_);
            payload.unsigned32(
                static_cast<std::uint32_t>(
                    model->ensemble_.size()));
            for (const auto& member : model->ensemble_) {
                write_model(member, depth + 1);
            }
        };
    for (std::size_t index = 0;
         index < result.checkpoints.size(); ++index) {
        payload.text(fingerprints[index]);
        write_model(result.checkpoints[index], 0);
    }

    ValueG8BinaryWriter file;
    file.bytes(kValueG8Mix50ArtifactMagic);
    file.unsigned32(kValueG8Mix50ArtifactSchema);
    file.size(payload.data().size());
    file.unsigned64(
        value_g8_payload_checksum(payload.data()));
    file.bytes(payload.data());
    write_value_g8_file_atomic(path, file.data());
}

LearnedValueG8Result load_learned_value_g8_bundle(
    const std::string& path,
    std::size_t expected_training_games,
    std::uint64_t expected_seed) {
    if (expected_training_games == 0) {
        throw std::invalid_argument(
            "expected Value G8 training_games must be positive");
    }
    const std::vector<std::uint8_t> file_bytes =
        read_bounded_value_g8_file(path);
    ValueG8BinaryReader file(file_bytes);
    for (const std::uint8_t expected : kValueG8ArtifactMagic) {
        const std::uint8_t actual =
            file.byte("file magic");
        if (actual != expected) {
            throw std::runtime_error(
                "Value G8 artifact '" + path +
                "' has the wrong magic");
        }
    }
    const std::uint32_t schema =
        file.unsigned32("schema");
    if (schema != kValueG8ArtifactSchema) {
        throw std::runtime_error(
            "Value G8 artifact '" + path +
            "' uses unsupported schema " +
            std::to_string(schema) + " (expected " +
            std::to_string(kValueG8ArtifactSchema) + ")");
    }
    const std::size_t payload_size =
        file.size("payload length");
    if (file.remaining() < 8 ||
        payload_size > kMaximumValueG8ArtifactBytes ||
        payload_size != file.remaining() - 8) {
        throw std::runtime_error(
            "Value G8 artifact '" + path +
            "' has an invalid payload length");
    }
    const std::uint64_t stored_checksum =
        file.unsigned64("payload checksum");
    const auto payload_bytes =
        file.take(payload_size, "payload");
    if (!file.at_end()) {
        throw std::runtime_error(
            "Value G8 artifact '" + path +
            "' has trailing bytes");
    }
    if (value_g8_payload_checksum(payload_bytes) !=
        stored_checksum) {
        throw std::runtime_error(
            "Value G8 artifact '" + path +
            "' failed its payload checksum");
    }

    ValueG8BinaryReader payload(payload_bytes);
    const std::string recipe = payload.text("recipe ID");
    if (recipe != kValueG8RecipeId) {
        throw std::runtime_error(
            "Value G8 artifact '" + path +
            "' recipe mismatch: found '" + recipe +
            "', expected '" + std::string(kValueG8RecipeId) +
            "'");
    }
    const auto require_dimension =
        [&](std::string_view name, std::size_t expected) {
            const std::size_t actual =
                payload.size(name);
            if (actual != expected) {
                throw std::runtime_error(
                    "Value G8 artifact '" + path +
                    "' dimension '" + std::string(name) +
                    "' is " + std::to_string(actual) +
                    ", expected " + std::to_string(expected));
            }
        };
    require_dimension("card count", kLearnedCardCount);
    require_dimension(
        "scalar feature count",
        LearnedModel::kScalarFeatureCount);
    require_dimension(
        "card planes", LearnedModel::kCardPlanes);
    require_dimension(
        "feature count", LearnedModel::kFeatureCount);
    require_dimension(
        "hidden count", LearnedModel::kHiddenCount);
    require_dimension(
        "policy decision count",
        LearnedModel::kPolicyDecisionCount);
    require_dimension(
        "policy phase count",
        LearnedModel::kPolicyPhaseCount);
    require_dimension(
        "policy verb count",
        LearnedModel::kPolicyVerbCount);
    require_dimension(
        "policy card planes",
        LearnedModel::kPolicyCardPlanes);
    require_dimension(
        "policy scalar count",
        LearnedModel::kPolicyScalarCount);
    require_dimension(
        "policy feature count",
        LearnedModel::kPolicyFeatureCount);
    require_dimension(
        "policy hidden count",
        LearnedModel::kPolicyHiddenCount);

    const std::size_t metadata_training_games =
        payload.size("metadata training_games");
    const std::uint64_t metadata_seed =
        payload.unsigned64("metadata seed");
    if (metadata_training_games != expected_training_games) {
        throw std::runtime_error(
            "Value G8 artifact '" + path +
            "' training_games mismatch: found " +
            std::to_string(metadata_training_games) +
            ", expected " +
            std::to_string(expected_training_games));
    }
    if (metadata_seed != expected_seed) {
        throw std::runtime_error(
            "Value G8 artifact '" + path +
            "' training seed mismatch: found " +
            std::to_string(metadata_seed) + ", expected " +
            std::to_string(expected_seed));
    }
    const std::string stored_report_fingerprint =
        payload.text("report fingerprint");
    if (!is_lower_hex_fingerprint(
            stored_report_fingerprint)) {
        throw std::runtime_error(
            "Value G8 artifact '" + path +
            "' has a malformed report fingerprint");
    }

    LearnedValueG8Report report;
    report.training_games =
        payload.size("report training_games");
    report.root_seed =
        payload.unsigned64("report root_seed");
    report.base_examples =
        payload.size("report base_examples");
    report.base_fingerprint =
        payload.text("report base_fingerprint");
    report.final_fingerprint =
        payload.text("report final_fingerprint");
    const std::size_t generation_count =
        payload.size("report generation count");
    if (generation_count != kLearnedValueG8Generations) {
        throw std::runtime_error(
            "Value G8 artifact '" + path +
            "' report must contain exactly 8 generations");
    }
    report.generations.reserve(generation_count);
    for (std::size_t index = 0;
         index < generation_count; ++index) {
        LearnedValueGenerationReport generation;
        generation.generation =
            payload.size("generation index");
        generation.self_play_games =
            payload.size("generation self_play_games");
        generation.generation_examples =
            payload.size("generation examples");
        generation.anchor_examples =
            payload.size("generation anchor examples");
        generation.replay_generations =
            payload.size("generation replay generations");
        generation.replay_examples =
            payload.size("generation replay examples");
        generation.search_enabled =
            payload.boolean("generation search enabled");
        generation.search_worlds =
            payload.size("generation search worlds");
        generation.search_horizon_turns =
            payload.size("generation search horizon");
        generation.rollout_evaluations =
            payload.size("generation rollout evaluations");
        generation.exploration_rate =
            payload.real("generation exploration rate");
        generation.parent_fingerprint =
            payload.text("generation parent fingerprint");
        generation.candidate_fingerprint =
            payload.text("generation candidate fingerprint");
        report.generations.push_back(std::move(generation));
    }
    if (report.training_games != metadata_training_games ||
        report.root_seed != metadata_seed) {
        throw std::runtime_error(
            "Value G8 artifact '" + path +
            "' report metadata disagrees with its header");
    }
    if (value_g8_report_fingerprint(report) !=
        stored_report_fingerprint) {
        throw std::runtime_error(
            "Value G8 artifact '" + path +
            "' failed its report fingerprint");
    }

    const std::size_t checkpoint_count =
        payload.size("checkpoint count");
    if (checkpoint_count !=
        kLearnedValueG8Generations + 1) {
        throw std::runtime_error(
            "Value G8 artifact '" + path +
            "' must contain exactly 9 checkpoints");
    }
    std::size_t node_count = 0;
    std::function<std::shared_ptr<const LearnedModel>(
        std::size_t)>
        read_model;
    read_model = [&](std::size_t depth)
        -> std::shared_ptr<const LearnedModel> {
        if (depth > kMaximumValueG8ArtifactDepth ||
            ++node_count > kMaximumValueG8ArtifactNodes) {
            throw std::runtime_error(
                "Value G8 artifact '" + path +
                "' model graph exceeds its bound");
        }
        if (payload.unsigned32("model marker") !=
            0x4D4F444C) {
            throw std::runtime_error(
                "Value G8 artifact '" + path +
                "' has an invalid model marker");
        }
        const std::uint32_t raw_variant =
            payload.unsigned32("model variant");
        if (raw_variant !=
            static_cast<std::uint32_t>(
                LearnedVariant::ValueSearchChampion)) {
            throw std::runtime_error(
                "Value G8 artifact '" + path +
                "' contains a non-Value model variant");
        }
        const auto variant =
            static_cast<LearnedVariant>(raw_variant);
        auto model = std::shared_ptr<LearnedModel>(
            new LearnedModel(0, variant));
        read_value_g8_fixed(
            payload, model->input_weights_,
            "critic input weight");
        read_value_g8_fixed(
            payload, model->hidden_biases_,
            "critic hidden bias");
        read_value_g8_fixed(
            payload, model->output_weights_,
            "critic output weight");
        read_value_g8_fixed(
            payload, model->direct_output_weights_,
            "critic direct weight");
        model->output_bias_ =
            payload.real("critic output bias");
        read_value_g8_fixed(
            payload, model->policy_input_weights_,
            "policy input weight");
        read_value_g8_fixed(
            payload, model->policy_hidden_biases_,
            "policy hidden bias");
        read_value_g8_fixed(
            payload, model->policy_output_weights_,
            "policy output weight");
        read_value_g8_fixed(
            payload,
            model->policy_direct_output_weights_,
            "policy direct weight");
        read_value_g8_fixed(
            payload, model->policy_output_bias_,
            "policy output bias");
        const std::uint32_t member_count =
            payload.unsigned32("ensemble member count");
        if (member_count >
            kMaximumValueG8EnsembleMembers) {
            throw std::runtime_error(
                "Value G8 artifact '" + path +
                "' ensemble exceeds its bound");
        }
        model->ensemble_.reserve(member_count);
        for (std::uint32_t member = 0;
             member < member_count; ++member) {
            model->ensemble_.push_back(
                read_model(depth + 1));
        }
        return model;
    };

    LearnedValueG8Result result;
    result.report = std::move(report);
    result.checkpoints.reserve(checkpoint_count);
    for (std::size_t index = 0;
         index < checkpoint_count; ++index) {
        const std::string stored_fingerprint =
            payload.text("checkpoint fingerprint");
        if (!is_lower_hex_fingerprint(stored_fingerprint)) {
            throw std::runtime_error(
                "Value G8 artifact '" + path +
                "' has a malformed checkpoint fingerprint at G" +
                std::to_string(index));
        }
        auto checkpoint = read_model(0);
        const std::string actual_fingerprint =
            learned_model_fingerprint(checkpoint);
        if (actual_fingerprint != stored_fingerprint) {
            throw std::runtime_error(
                "Value G8 artifact '" + path +
                "' checkpoint G" + std::to_string(index) +
                " fingerprint mismatch");
        }
        result.checkpoints.push_back(
            std::move(checkpoint));
    }
    if (!payload.at_end()) {
        throw std::runtime_error(
            "Value G8 artifact '" + path +
            "' has trailing payload bytes");
    }
    result.model = result.checkpoints.back();
    static_cast<void>(validate_value_g8_bundle(result));
    return result;
}

LearnedValueG8Result load_learned_value_g8_mix50_bundle(
    const std::string& path,
    std::size_t expected_training_games,
    std::uint64_t expected_seed) {
    if (expected_training_games == 0) {
        throw std::invalid_argument(
            "expected Value G8 Late-Mix50 training_games must "
            "be positive");
    }
    const std::vector<std::uint8_t> file_bytes =
        read_bounded_value_g8_file(path);
    ValueG8BinaryReader file(file_bytes);
    for (const std::uint8_t expected :
         kValueG8Mix50ArtifactMagic) {
        const std::uint8_t actual = file.byte("file magic");
        if (actual != expected) {
            throw std::runtime_error(
                "Value G8 Late-Mix50 artifact '" + path +
                "' has the wrong magic");
        }
    }
    const std::uint32_t schema = file.unsigned32("schema");
    if (schema != kValueG8Mix50ArtifactSchema) {
        throw std::runtime_error(
            "Value G8 Late-Mix50 artifact '" + path +
            "' uses unsupported schema " +
            std::to_string(schema) + " (expected " +
            std::to_string(kValueG8Mix50ArtifactSchema) + ")");
    }
    const std::size_t payload_size =
        file.size("payload length");
    if (file.remaining() < 8 ||
        payload_size > kMaximumValueG8ArtifactBytes ||
        payload_size != file.remaining() - 8) {
        throw std::runtime_error(
            "Value G8 Late-Mix50 artifact '" + path +
            "' has an invalid payload length");
    }
    const std::uint64_t stored_checksum =
        file.unsigned64("payload checksum");
    const auto payload_bytes =
        file.take(payload_size, "payload");
    if (!file.at_end()) {
        throw std::runtime_error(
            "Value G8 Late-Mix50 artifact '" + path +
            "' has trailing bytes");
    }
    if (value_g8_payload_checksum(payload_bytes) !=
        stored_checksum) {
        throw std::runtime_error(
            "Value G8 Late-Mix50 artifact '" + path +
            "' failed its payload checksum");
    }

    ValueG8BinaryReader payload(payload_bytes);
    const std::string recipe = payload.text("recipe ID");
    if (recipe != kValueG8Mix50RecipeId) {
        throw std::runtime_error(
            "Value G8 Late-Mix50 artifact '" + path +
            "' recipe mismatch: found '" + recipe +
            "', expected '" +
            std::string(kValueG8Mix50RecipeId) + "'");
    }
    const auto require_dimension =
        [&](std::string_view name, std::size_t expected) {
            const std::size_t actual = payload.size(name);
            if (actual != expected) {
                throw std::runtime_error(
                    "Value G8 Late-Mix50 artifact '" + path +
                    "' dimension '" + std::string(name) +
                    "' is " + std::to_string(actual) +
                    ", expected " + std::to_string(expected));
            }
        };
    require_dimension("card count", kLearnedCardCount);
    require_dimension(
        "scalar feature count",
        LearnedModel::kScalarFeatureCount);
    require_dimension(
        "card planes", LearnedModel::kCardPlanes);
    require_dimension(
        "feature count", LearnedModel::kFeatureCount);
    require_dimension(
        "hidden count", LearnedModel::kHiddenCount);
    require_dimension(
        "policy decision count",
        LearnedModel::kPolicyDecisionCount);
    require_dimension(
        "policy phase count",
        LearnedModel::kPolicyPhaseCount);
    require_dimension(
        "policy verb count",
        LearnedModel::kPolicyVerbCount);
    require_dimension(
        "policy card planes",
        LearnedModel::kPolicyCardPlanes);
    require_dimension(
        "policy scalar count",
        LearnedModel::kPolicyScalarCount);
    require_dimension(
        "policy feature count",
        LearnedModel::kPolicyFeatureCount);
    require_dimension(
        "policy hidden count",
        LearnedModel::kPolicyHiddenCount);

    const std::size_t metadata_training_games =
        payload.size("metadata training_games");
    const std::uint64_t metadata_seed =
        payload.unsigned64("metadata seed");
    if (metadata_training_games != expected_training_games) {
        throw std::runtime_error(
            "Value G8 Late-Mix50 artifact '" + path +
            "' training_games mismatch: found " +
            std::to_string(metadata_training_games) +
            ", expected " +
            std::to_string(expected_training_games));
    }
    if (metadata_seed != expected_seed) {
        throw std::runtime_error(
            "Value G8 Late-Mix50 artifact '" + path +
            "' training seed mismatch: found " +
            std::to_string(metadata_seed) + ", expected " +
            std::to_string(expected_seed));
    }
    const std::string stored_report_fingerprint =
        payload.text("report fingerprint");
    if (!is_lower_hex_fingerprint(
            stored_report_fingerprint)) {
        throw std::runtime_error(
            "Value G8 Late-Mix50 artifact '" + path +
            "' has a malformed report fingerprint");
    }

    LearnedValueG8Report report;
    report.recipe = LearnedValueG8Recipe::LateMix50;
    report.training_games =
        payload.size("report training_games");
    report.root_seed =
        payload.unsigned64("report root_seed");
    report.base_examples =
        payload.size("report base_examples");
    report.base_fingerprint =
        payload.text("report base_fingerprint");
    report.final_fingerprint =
        payload.text("report final_fingerprint");
    const std::size_t generation_count =
        payload.size("report generation count");
    if (generation_count != kLearnedValueG8Generations) {
        throw std::runtime_error(
            "Value G8 Late-Mix50 artifact '" + path +
            "' report must contain exactly 8 generations");
    }
    report.generations.reserve(generation_count);
    for (std::size_t index = 0;
         index < generation_count; ++index) {
        LearnedValueGenerationReport generation;
        generation.generation =
            payload.size("generation index");
        generation.self_play_games =
            payload.size("generation self_play_games");
        generation.generation_examples =
            payload.size("generation examples");
        generation.anchor_examples =
            payload.size("generation anchor examples");
        generation.replay_generations =
            payload.size("generation replay generations");
        generation.replay_examples =
            payload.size("generation replay examples");
        generation.raw_collection_games =
            payload.size("generation raw collection games");
        generation.search_collection_games =
            payload.size("generation search collection games");
        generation.raw_collection_examples =
            payload.size("generation raw collection examples");
        generation.search_collection_examples =
            payload.size("generation search collection examples");
        generation.search_enabled =
            payload.boolean("generation search enabled");
        generation.search_worlds =
            payload.size("generation search worlds");
        generation.search_horizon_turns =
            payload.size("generation search horizon");
        generation.rollout_evaluations =
            payload.size("generation rollout evaluations");
        generation.exploration_rate =
            payload.real("generation exploration rate");
        generation.parent_fingerprint =
            payload.text("generation parent fingerprint");
        generation.candidate_fingerprint =
            payload.text("generation candidate fingerprint");
        report.generations.push_back(std::move(generation));
    }
    if (report.training_games != metadata_training_games ||
        report.root_seed != metadata_seed) {
        throw std::runtime_error(
            "Value G8 Late-Mix50 artifact '" + path +
            "' report metadata disagrees with its header");
    }
    if (value_g8_mix50_report_fingerprint(report) !=
        stored_report_fingerprint) {
        throw std::runtime_error(
            "Value G8 Late-Mix50 artifact '" + path +
            "' failed its report fingerprint");
    }

    const std::size_t checkpoint_count =
        payload.size("checkpoint count");
    if (checkpoint_count !=
        kLearnedValueG8Generations + 1) {
        throw std::runtime_error(
            "Value G8 Late-Mix50 artifact '" + path +
            "' must contain exactly 9 checkpoints");
    }
    std::size_t node_count = 0;
    std::function<std::shared_ptr<const LearnedModel>(
        std::size_t)>
        read_model;
    read_model = [&](std::size_t depth)
        -> std::shared_ptr<const LearnedModel> {
        if (depth > kMaximumValueG8ArtifactDepth ||
            ++node_count > kMaximumValueG8ArtifactNodes) {
            throw std::runtime_error(
                "Value G8 Late-Mix50 artifact '" + path +
                "' model graph exceeds its bound");
        }
        if (payload.unsigned32("model marker") !=
            0x4D4F444C) {
            throw std::runtime_error(
                "Value G8 Late-Mix50 artifact '" + path +
                "' has an invalid model marker");
        }
        const std::uint32_t raw_variant =
            payload.unsigned32("model variant");
        if (raw_variant !=
            static_cast<std::uint32_t>(
                LearnedVariant::ValueSearchChampion)) {
            throw std::runtime_error(
                "Value G8 Late-Mix50 artifact '" + path +
                "' contains a non-Value model variant");
        }
        const auto variant =
            static_cast<LearnedVariant>(raw_variant);
        auto model = std::shared_ptr<LearnedModel>(
            new LearnedModel(0, variant));
        read_value_g8_fixed(
            payload, model->input_weights_,
            "critic input weight");
        read_value_g8_fixed(
            payload, model->hidden_biases_,
            "critic hidden bias");
        read_value_g8_fixed(
            payload, model->output_weights_,
            "critic output weight");
        read_value_g8_fixed(
            payload, model->direct_output_weights_,
            "critic direct weight");
        model->output_bias_ =
            payload.real("critic output bias");
        read_value_g8_fixed(
            payload, model->policy_input_weights_,
            "policy input weight");
        read_value_g8_fixed(
            payload, model->policy_hidden_biases_,
            "policy hidden bias");
        read_value_g8_fixed(
            payload, model->policy_output_weights_,
            "policy output weight");
        read_value_g8_fixed(
            payload,
            model->policy_direct_output_weights_,
            "policy direct weight");
        read_value_g8_fixed(
            payload, model->policy_output_bias_,
            "policy output bias");
        const std::uint32_t member_count =
            payload.unsigned32("ensemble member count");
        if (member_count >
            kMaximumValueG8EnsembleMembers) {
            throw std::runtime_error(
                "Value G8 Late-Mix50 artifact '" + path +
                "' ensemble exceeds its bound");
        }
        model->ensemble_.reserve(member_count);
        for (std::uint32_t member = 0;
             member < member_count; ++member) {
            model->ensemble_.push_back(
                read_model(depth + 1));
        }
        return model;
    };

    LearnedValueG8Result result;
    result.report = std::move(report);
    result.checkpoints.reserve(checkpoint_count);
    for (std::size_t index = 0;
         index < checkpoint_count; ++index) {
        const std::string stored_fingerprint =
            payload.text("checkpoint fingerprint");
        if (!is_lower_hex_fingerprint(stored_fingerprint)) {
            throw std::runtime_error(
                "Value G8 Late-Mix50 artifact '" + path +
                "' has a malformed checkpoint fingerprint at G" +
                std::to_string(index));
        }
        auto checkpoint = read_model(0);
        const std::string actual_fingerprint =
            learned_model_fingerprint(checkpoint);
        if (actual_fingerprint != stored_fingerprint) {
            throw std::runtime_error(
                "Value G8 Late-Mix50 artifact '" + path +
                "' checkpoint G" + std::to_string(index) +
                " fingerprint mismatch");
        }
        result.checkpoints.push_back(
            std::move(checkpoint));
    }
    if (!payload.at_end()) {
        throw std::runtime_error(
            "Value G8 Late-Mix50 artifact '" + path +
            "' has trailing payload bytes");
    }
    result.model = result.checkpoints.back();
    static_cast<void>(
        validate_value_g8_mix50_bundle(result));
    return result;
}

std::shared_ptr<const LearnedModel>
learned_value_g8_generation_checkpoint(
    const LearnedValueG8Result& result, std::size_t generation) {
    if (generation == 0 ||
        generation > kLearnedValueG8Generations) {
        throw std::out_of_range(
            "Value G8 bundle generation must be between one "
            "and eight");
    }
    if (result.report.recipe ==
        LearnedValueG8Recipe::LateMix50) {
        static_cast<void>(
            validate_value_g8_mix50_bundle(result));
    } else {
        static_cast<void>(validate_value_g8_bundle(result));
    }
    return result.checkpoints[generation];
}

std::shared_ptr<const LearnedModel> update_learned_value_model(
    std::shared_ptr<const LearnedModel> parent,
    const std::vector<LearnedCriticTrainingExample>& examples,
    LearnedValueUpdateConfig config) {
    std::vector<LearnedModel::TrainingExample> encoded;
    encoded.reserve(examples.size());
    for (const auto& example : examples) {
        if (example.features.size() !=
                LearnedModel::kFeatureCount ||
            !std::all_of(
                example.features.begin(),
                example.features.end(),
                [](double value) {
                    return std::isfinite(value);
                }) ||
            !std::isfinite(example.target) ||
            example.target < 0.0 || example.target > 1.0) {
            throw std::invalid_argument(
                "invalid Learned Value training example");
        }
        LearnedModel::FeatureVector features{};
        std::copy(
            example.features.begin(), example.features.end(),
            features.begin());
        encoded.emplace_back(features, example.target);
    }

    return update_learned_value_model_encoded(
        std::move(parent), encoded, config);
}

std::shared_ptr<const LearnedModel> update_learned_actor_model(
    std::shared_ptr<const LearnedModel> parent,
    const std::vector<LearnedCriticTrainingExample>& critic_examples,
    const std::vector<LearnedPolicyTrainingExample>& policy_examples,
    LearnedActorUpdateConfig config) {
    if (!parent) {
        throw std::invalid_argument(
            "Learned Actor update requires a parent model");
    }
    if (parent->variant() != LearnedVariant::UnifiedActor) {
        throw std::invalid_argument(
            "Learned Actor update requires a Unified Actor model");
    }
    if (!critic_examples.empty() &&
        (config.critic_epochs == 0 ||
         !std::isfinite(config.critic_learning_rate) ||
         config.critic_learning_rate <= 0.0)) {
        throw std::invalid_argument(
            "Learned critic update parameters must be positive");
    }
    if (!policy_examples.empty() &&
        (config.policy_epochs == 0 ||
         !std::isfinite(config.policy_learning_rate) ||
         config.policy_learning_rate <= 0.0)) {
        throw std::invalid_argument(
            "Learned policy update parameters must be positive");
    }

    std::vector<LearnedModel::TrainingExample> encoded_critic;
    encoded_critic.reserve(critic_examples.size());
    for (const auto& example : critic_examples) {
        if (example.features.size() != LearnedModel::kFeatureCount ||
            !std::all_of(
                example.features.begin(), example.features.end(),
                [](double value) { return std::isfinite(value); }) ||
            !std::isfinite(example.target) ||
            example.target < 0.0 || example.target > 1.0) {
            throw std::invalid_argument(
                "invalid Learned critic training example");
        }
        LearnedModel::FeatureVector features{};
        std::copy(example.features.begin(), example.features.end(),
                  features.begin());
        encoded_critic.emplace_back(features, example.target);
    }

    std::vector<LearnedModel::PolicyTrainingExample> encoded_policy;
    encoded_policy.reserve(policy_examples.size());
    for (const auto& example : policy_examples) {
        const std::size_t decision_kind =
            static_cast<std::size_t>(example.decision_kind);
        if (example.options.size() < 2 ||
            example.target_probabilities.size() !=
                example.options.size() ||
            decision_kind >= LearnedModel::kPolicyDecisionCount ||
            !std::isfinite(example.weight) ||
            example.weight <= 0.0) {
            throw std::invalid_argument(
                "invalid Learned policy training example");
        }

        LearnedModel::PolicyTrainingExample encoded;
        encoded.options.reserve(example.options.size());
        double target_total = 0.0;
        for (std::size_t option_index = 0;
             option_index < example.options.size(); ++option_index) {
            const auto& option = example.options[option_index];
            const double target =
                example.target_probabilities[option_index];
            if (option.size() !=
                    LearnedModel::kPolicyFeatureCount ||
                !std::all_of(
                    option.begin(), option.end(),
                    [](double value) {
                        return std::isfinite(value);
                    }) ||
                !std::isfinite(target) || target < 0.0 ||
                target > 1.0) {
                throw std::invalid_argument(
                    "invalid Learned policy training example");
            }
            LearnedModel::PolicyFeatureVector features{};
            std::copy(option.begin(), option.end(),
                      features.begin());
            encoded.options.push_back(features);
            target_total += target;
        }
        if (std::abs(target_total - 1.0) > 1.0e-9) {
            throw std::invalid_argument(
                "Learned policy targets must sum to one");
        }
        encoded.target_probabilities =
            example.target_probabilities;
        encoded.chosen = 0;
        encoded.decision_kind = decision_kind;
        encoded.weight = example.weight;
        encoded_policy.push_back(std::move(encoded));
    }

    std::vector<std::shared_ptr<LearnedModel>> critic_leaves;
    auto candidate =
        parent->deep_clone_mutable(critic_leaves);
    for (std::size_t member = 0; member < critic_leaves.size();
         ++member) {
        critic_leaves[member]->train(
            encoded_critic, config.critic_epochs,
            config.critic_learning_rate,
            config.critic_seed ^
                (0x4352495449430000ULL + member));
    }
    candidate->train_policy(
        encoded_policy, config.policy_epochs,
        config.policy_learning_rate, config.policy_seed);
    return candidate;
}

namespace {

struct LearnedPolicyFitSnapshot {
    std::size_t example_count = 0;
    double total_weight = 0.0;
    double weighted_top_one_agreement = 0.0;
    double weighted_teacher_entropy = 0.0;
    double weighted_cross_entropy = 0.0;
    std::vector<double> example_weights;
    std::vector<std::vector<std::size_t>> model_argmax_sets;
};

struct LearnedCriticFitSnapshot {
    std::size_t example_count = 0;
    double squared_error_sum = 0.0;
    double binary_cross_entropy_sum = 0.0;
    double target_sum = 0.0;
    double target_squared_sum = 0.0;
};

struct LearnedActorFitSnapshot {
    LearnedPolicyFitSnapshot priority;
    LearnedPolicyFitSnapshot attack;
    LearnedCriticFitSnapshot critic;
};

std::vector<std::size_t> exact_argmax_set(
    const std::vector<double>& values) {
    if (values.empty() ||
        !std::all_of(
            values.begin(), values.end(),
            [](double value) { return std::isfinite(value); })) {
        throw std::invalid_argument(
            "fit diagnostics require finite nonempty scores");
    }
    const double maximum =
        *std::max_element(values.begin(), values.end());
    std::vector<std::size_t> result;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (values[index] == maximum) {
            result.push_back(index);
        }
    }
    return result;
}

double expected_top_one_agreement(
    const std::vector<std::size_t>& model_argmax_set,
    const std::vector<std::size_t>& teacher_argmax_set) {
    std::size_t accepted = 0;
    for (const std::size_t model_action : model_argmax_set) {
        accepted +=
            std::find(
                teacher_argmax_set.begin(),
                teacher_argmax_set.end(), model_action) !=
                    teacher_argmax_set.end()
                ? 1
                : 0;
    }
    return static_cast<double>(accepted) /
           static_cast<double>(model_argmax_set.size());
}

LearnedActorFitSnapshot evaluate_learned_actor_fit_snapshot(
    const std::shared_ptr<const LearnedModel>& model,
    const std::vector<LearnedCriticTrainingExample>& critic_examples,
    const std::vector<LearnedPolicyTrainingExample>& policy_examples) {
    if (!model ||
        model->variant() != LearnedVariant::UnifiedActor) {
        throw std::invalid_argument(
            "fit diagnostics require a Unified Actor model");
    }

    LearnedActorFitSnapshot snapshot;
    for (const auto& example : policy_examples) {
        LearnedPolicyFitSnapshot* head = nullptr;
        switch (example.decision_kind) {
        case LearnedPolicyDecisionKind::Priority:
            head = &snapshot.priority;
            break;
        case LearnedPolicyDecisionKind::Attack:
            head = &snapshot.attack;
            break;
        case LearnedPolicyDecisionKind::Block:
        case LearnedPolicyDecisionKind::DamageOrder:
            throw std::invalid_argument(
                "fit diagnostics only support Priority and Attack");
        }
        if (example.options.size() < 2 ||
            example.target_probabilities.size() !=
                example.options.size() ||
            !std::isfinite(example.weight) ||
            example.weight <= 0.0) {
            throw std::invalid_argument(
                "invalid policy fit diagnostic example");
        }

        std::vector<double> logits;
        logits.reserve(example.options.size());
        double target_sum = 0.0;
        for (std::size_t option_index = 0;
             option_index < example.options.size(); ++option_index) {
            const auto& option = example.options[option_index];
            const double target =
                example.target_probabilities[option_index];
            if (option.size() !=
                    LearnedModel::kPolicyFeatureCount ||
                !std::all_of(
                    option.begin(), option.end(),
                    [](double value) {
                        return std::isfinite(value);
                    }) ||
                !std::isfinite(target) || target < 0.0 ||
                target > 1.0) {
                throw std::invalid_argument(
                    "invalid policy fit diagnostic example");
            }
            LearnedModel::PolicyFeatureVector features{};
            std::copy(
                option.begin(), option.end(), features.begin());
            logits.push_back(model->policy_logit(
                features,
                static_cast<std::size_t>(
                    example.decision_kind)));
            target_sum += target;
        }
        if (std::abs(target_sum - 1.0) > 1.0e-9) {
            throw std::invalid_argument(
                "policy fit targets must sum to one");
        }

        const double maximum_logit =
            *std::max_element(logits.begin(), logits.end());
        double exponential_sum = 0.0;
        for (const double logit : logits) {
            exponential_sum +=
                std::exp(logit - maximum_logit);
        }
        const double log_normalizer =
            maximum_logit + std::log(exponential_sum);
        double cross_entropy = 0.0;
        double teacher_entropy = 0.0;
        for (std::size_t index = 0;
             index < logits.size(); ++index) {
            const double target =
                example.target_probabilities[index];
            cross_entropy -=
                target * (logits[index] - log_normalizer);
            if (target > 0.0) {
                teacher_entropy -= target * std::log(target);
            }
        }
        const auto teacher_argmax_set =
            exact_argmax_set(example.target_probabilities);
        const auto model_argmax_set =
            exact_argmax_set(logits);
        ++head->example_count;
        head->total_weight += example.weight;
        head->weighted_top_one_agreement +=
            example.weight *
            expected_top_one_agreement(
                model_argmax_set, teacher_argmax_set);
        head->weighted_teacher_entropy +=
            example.weight * teacher_entropy;
        head->weighted_cross_entropy +=
            example.weight * cross_entropy;
        head->example_weights.push_back(example.weight);
        head->model_argmax_sets.push_back(model_argmax_set);
    }

    for (const auto& example : critic_examples) {
        if (example.features.size() !=
                LearnedModel::kFeatureCount ||
            !std::all_of(
                example.features.begin(),
                example.features.end(),
                [](double value) {
                    return std::isfinite(value);
                }) ||
            !std::isfinite(example.target) ||
            example.target < 0.0 || example.target > 1.0) {
            throw std::invalid_argument(
                "invalid critic fit diagnostic example");
        }
        LearnedModel::FeatureVector features{};
        std::copy(
            example.features.begin(), example.features.end(),
            features.begin());
        const double prediction = model->predict(features);
        if (!std::isfinite(prediction) || prediction < 0.0 ||
            prediction > 1.0) {
            throw std::logic_error(
                "critic fit prediction must be a probability");
        }
        const double error = prediction - example.target;
        snapshot.critic.squared_error_sum += error * error;
        constexpr double kProbabilityFloor = 1.0e-12;
        const double bounded_prediction = std::clamp(
            prediction, kProbabilityFloor,
            1.0 - kProbabilityFloor);
        snapshot.critic.binary_cross_entropy_sum -=
            example.target * std::log(bounded_prediction) +
            (1.0 - example.target) *
                std::log(1.0 - bounded_prediction);
        snapshot.critic.target_sum += example.target;
        snapshot.critic.target_squared_sum +=
            example.target * example.target;
        ++snapshot.critic.example_count;
    }
    return snapshot;
}

LearnedPolicyFitDiagnostics combine_policy_fit_snapshots(
    const LearnedPolicyFitSnapshot& parent,
    const LearnedPolicyFitSnapshot& candidate) {
    if (parent.example_count != candidate.example_count ||
        parent.model_argmax_sets.size() !=
            candidate.model_argmax_sets.size() ||
        parent.example_weights != candidate.example_weights ||
        parent.total_weight != candidate.total_weight ||
        parent.weighted_teacher_entropy !=
            candidate.weighted_teacher_entropy) {
        throw std::logic_error(
            "policy fit snapshots are not aligned");
    }
    LearnedPolicyFitDiagnostics result;
    result.example_count = parent.example_count;
    result.total_weight = parent.total_weight;
    if (result.example_count == 0) {
        return result;
    }
    if (!std::isfinite(result.total_weight) ||
        result.total_weight <= 0.0) {
        throw std::logic_error(
            "policy fit total weight must be positive");
    }
    result.parent_expected_top_one_agreement =
        parent.weighted_top_one_agreement /
        result.total_weight;
    result.candidate_expected_top_one_agreement =
        candidate.weighted_top_one_agreement /
        result.total_weight;
    result.weighted_teacher_entropy =
        parent.weighted_teacher_entropy / result.total_weight;
    result.parent_weighted_cross_entropy =
        parent.weighted_cross_entropy /
        result.total_weight;
    result.candidate_weighted_cross_entropy =
        candidate.weighted_cross_entropy /
        result.total_weight;
    result.parent_excess_cross_entropy = std::max(
        0.0, result.parent_weighted_cross_entropy -
                 result.weighted_teacher_entropy);
    result.candidate_excess_cross_entropy = std::max(
        0.0, result.candidate_weighted_cross_entropy -
                 result.weighted_teacher_entropy);
    for (std::size_t index = 0;
         index < parent.model_argmax_sets.size(); ++index) {
        if (parent.model_argmax_sets[index] !=
            candidate.model_argmax_sets[index]) {
            ++result.changed_argmax_examples;
            result.changed_argmax_weight +=
                parent.example_weights[index];
        }
    }
    result.changed_argmax_weight_fraction =
        result.changed_argmax_weight / result.total_weight;
    const std::array<double, 10> finite_metrics = {
        result.total_weight,
        result.parent_expected_top_one_agreement,
        result.candidate_expected_top_one_agreement,
        result.weighted_teacher_entropy,
        result.parent_weighted_cross_entropy,
        result.candidate_weighted_cross_entropy,
        result.parent_excess_cross_entropy,
        result.candidate_excess_cross_entropy,
        result.changed_argmax_weight,
        result.changed_argmax_weight_fraction,
    };
    if (!std::all_of(
            finite_metrics.begin(), finite_metrics.end(),
            [](double value) { return std::isfinite(value); })) {
        throw std::logic_error(
            "policy fit diagnostics became non-finite");
    }
    return result;
}

LearnedActorFitDiagnostics combine_actor_fit_snapshots(
    const LearnedActorFitSnapshot& parent,
    const LearnedActorFitSnapshot& candidate) {
    if (parent.critic.example_count !=
            candidate.critic.example_count ||
        parent.critic.target_sum !=
            candidate.critic.target_sum ||
        parent.critic.target_squared_sum !=
            candidate.critic.target_squared_sum) {
        throw std::logic_error(
            "critic fit snapshots are not aligned");
    }
    LearnedActorFitDiagnostics result;
    result.priority = combine_policy_fit_snapshots(
        parent.priority, candidate.priority);
    result.attack = combine_policy_fit_snapshots(
        parent.attack, candidate.attack);
    result.critic.example_count =
        parent.critic.example_count;
    if (result.critic.example_count != 0) {
        const double count =
            static_cast<double>(result.critic.example_count);
        result.critic.target_mean =
            parent.critic.target_sum / count;
        result.critic.target_variance = std::max(
            0.0,
            parent.critic.target_squared_sum / count -
                result.critic.target_mean *
                    result.critic.target_mean);
        result.critic.parent_mean_squared_error =
            parent.critic.squared_error_sum / count;
        result.critic.candidate_mean_squared_error =
            candidate.critic.squared_error_sum / count;
        result.critic.parent_binary_cross_entropy =
            parent.critic.binary_cross_entropy_sum / count;
        result.critic.candidate_binary_cross_entropy =
            candidate.critic.binary_cross_entropy_sum / count;
    }
    const std::array<double, 6> critic_metrics = {
        result.critic.target_mean,
        result.critic.target_variance,
        result.critic.parent_mean_squared_error,
        result.critic.candidate_mean_squared_error,
        result.critic.parent_binary_cross_entropy,
        result.critic.candidate_binary_cross_entropy,
    };
    if (!std::all_of(
            critic_metrics.begin(), critic_metrics.end(),
            [](double value) { return std::isfinite(value); })) {
        throw std::logic_error(
            "critic fit diagnostics became non-finite");
    }
    return result;
}

} // namespace

LearnedActorFitDiagnostics diagnose_learned_actor_fit(
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate,
    const std::vector<LearnedCriticTrainingExample>& critic_examples,
    const std::vector<LearnedPolicyTrainingExample>& policy_examples) {
    const auto parent_snapshot =
        evaluate_learned_actor_fit_snapshot(
            parent, critic_examples, policy_examples);
    const auto candidate_snapshot =
        evaluate_learned_actor_fit_snapshot(
            candidate, critic_examples, policy_examples);
    return combine_actor_fit_snapshots(
        parent_snapshot, candidate_snapshot);
}

using LearnedDecisionKind = LearnedPolicyDecisionKind;

class LearnedPolicyRecorder {
  public:
    struct Step {
        std::vector<LearnedModel::PolicyFeatureVector> options;
        LearnedModel::FeatureVector critic_features{};
        std::size_t chosen = 0;
        std::size_t actor = 0;
        LearnedDecisionKind kind = LearnedDecisionKind::Priority;
        double critic_baseline = 0.5;
        std::vector<double> target_probabilities;
    };

    struct GenerationCollection {
        std::uint64_t root_seed = 0;
        std::uint64_t generation = 1;
        std::size_t schedule_index = 0;
        std::size_t worlds = 8;
        std::size_t rollouts_per_world = 1;
        std::size_t horizon_turns = 0;
        std::size_t max_roots_per_seat_kind = 24;
        std::array<std::array<std::size_t, 2>, 2>
            searched_roots{};
        std::array<std::array<std::size_t, 2>, 2>
            rollout_evaluations{};
        std::array<std::array<std::size_t, 2>, 2>
            action_choice_indices{};
        std::array<std::size_t, 2> attack_includes{};
    };

    std::vector<Step> steps;
    std::optional<GenerationCollection> generation_collection;
};

namespace {

constexpr std::array<CardDefinition, 13> kCardDefinitions = {{
    {CardId::Forest, "Forest", CardType::Land, {}, 0, 0, 0},
    {CardId::Mountain, "Mountain", CardType::Land, {}, 0, 0, 0},
    {CardId::GrizzlyBears,
     "Grizzly Bears",
     CardType::Creature,
     {.generic = 1, .green = 1, .red = 0},
     2,
     2,
     0},
    {CardId::LightningBolt,
     "Lightning Bolt",
     CardType::Instant,
     {.generic = 0, .green = 0, .red = 1},
     0,
     0,
     3},
    {CardId::IronrootTreefolk,
     "Ironroot Treefolk",
     CardType::Creature,
     {.generic = 4, .green = 1, .red = 0},
     3,
     5,
     0},
    {CardId::FireElemental,
     "Fire Elemental",
     CardType::Creature,
     {.generic = 3, .green = 0, .red = 2},
     5,
     4,
     0},
    {CardId::Island, "Island", CardType::Land, {}, 0, 0, 0},
    {CardId::Counterspell,
     "Counterspell",
     CardType::Instant,
     {.generic = 0, .green = 0, .red = 0, .blue = 2},
     0,
     0,
     0},
    {CardId::WaterElemental,
     "Water Elemental",
     CardType::Creature,
     {.generic = 3, .green = 0, .red = 0, .blue = 2},
     5,
     4,
     0},
    {CardId::Tsunami,
     "Tsunami",
     CardType::Sorcery,
     {.generic = 3, .green = 1, .red = 0, .blue = 0},
     0,
     0,
     0},
    {CardId::Plains, "Plains", CardType::Land, {}, 0, 0, 0},
    {CardId::Millstone,
     "Millstone",
     CardType::Artifact,
     {.generic = 2},
     0,
     0,
     0},
    {CardId::Moat,
     "Moat",
     CardType::Enchantment,
     {.generic = 2,
      .green = 0,
      .red = 0,
      .blue = 0,
      .white = 2},
     0,
     0,
     0},
}};

constexpr std::array<CardId, 4> kCreatureCards = {
    CardId::GrizzlyBears,
    CardId::IronrootTreefolk,
    CardId::FireElemental,
    CardId::WaterElemental,
};

constexpr std::array<CardId, 1> kSorceryCards = {
    CardId::Tsunami,
};

constexpr std::array<CardId, 1> kArtifactCards = {
    CardId::Millstone,
};

constexpr std::array<CardId, 1> kEnchantmentCards = {
    CardId::Moat,
};

constexpr ManaCost kMillstoneActivationCost = {.generic = 2};

bool has_card(const std::vector<CardId>& cards, CardId wanted) {
    return std::find(cards.begin(), cards.end(), wanted) != cards.end();
}

bool remove_card(std::vector<CardId>& cards, CardId wanted) {
    const auto position = std::find(cards.begin(), cards.end(), wanted);
    if (position == cards.end()) {
        return false;
    }
    cards.erase(position);
    return true;
}

using CardCounts = std::array<std::size_t, kCardDefinitions.size()>;

std::size_t checked_card_index(CardId card) {
    const auto index = static_cast<std::size_t>(card);
    if (index >= kCardDefinitions.size()) {
        throw std::invalid_argument("state contains an unknown card");
    }
    return index;
}

void add_card(CardCounts& counts, CardId card) {
    ++counts[checked_card_index(card)];
}

void subtract_card(CardCounts& counts, CardId card) {
    auto& count = counts[checked_card_index(card)];
    if (count == 0) {
        throw std::invalid_argument(
            "public state is inconsistent with original deck");
    }
    --count;
}

CardCounts card_counts(const std::vector<CardId>& cards) {
    CardCounts counts{};
    for (const CardId card : cards) {
        add_card(counts, card);
    }
    return counts;
}

void subtract_public_cards(CardCounts& remaining,
                           const PlayerState& player) {
    for (const CardId card : player.graveyard) {
        subtract_card(remaining, card);
    }
    for (const auto& land : player.lands) {
        subtract_card(remaining, land.card);
    }
    for (const auto& creature : player.creatures) {
        subtract_card(remaining, creature.card);
    }
    for (const auto& artifact : player.artifacts) {
        subtract_card(remaining, artifact.card);
    }
    for (const CardId card : player.enchantments) {
        subtract_card(remaining, card);
    }
}

std::vector<CardId> expand_card_counts(const CardCounts& counts) {
    std::vector<CardId> cards;
    for (std::size_t index = 0; index < counts.size(); ++index) {
        cards.insert(cards.end(), counts[index],
                     static_cast<CardId>(index));
    }
    return cards;
}

CardCounts physical_card_counts(const GameState& state,
                                std::size_t player) {
    CardCounts counts{};
    const auto& player_state = state.players[player];
    for (const CardId card : player_state.library) {
        add_card(counts, card);
    }
    for (const CardId card : player_state.hand) {
        add_card(counts, card);
    }
    for (const CardId card : player_state.graveyard) {
        add_card(counts, card);
    }
    for (const auto& land : player_state.lands) {
        add_card(counts, land.card);
    }
    for (const auto& creature : player_state.creatures) {
        add_card(counts, creature.card);
    }
    for (const auto& artifact : player_state.artifacts) {
        add_card(counts, artifact.card);
    }
    for (const CardId card : player_state.enchantments) {
        add_card(counts, card);
    }
    for (const auto& object : state.stack) {
        if (object.controller == player &&
            object.kind == StackObjectKind::Spell) {
            add_card(counts, object.card);
        }
    }
    return counts;
}

bool is_land(CardId card) {
    return card == CardId::Forest || card == CardId::Mountain ||
           card == CardId::Island || card == CardId::Plains;
}

bool can_pay(const PlayerState& player, const ManaCost& cost) {
    int forests = 0;
    int mountains = 0;
    int islands = 0;
    int plains = 0;
    int total = 0;
    for (const auto& land : player.lands) {
        if (land.tapped) {
            continue;
        }
        ++total;
        if (land.card == CardId::Forest) {
            ++forests;
        } else if (land.card == CardId::Mountain) {
            ++mountains;
        } else if (land.card == CardId::Island) {
            ++islands;
        } else if (land.card == CardId::Plains) {
            ++plains;
        }
    }

    if (forests < cost.green || mountains < cost.red ||
        islands < cost.blue || plains < cost.white) {
        return false;
    }
    return total >= cost.green + cost.red + cost.blue + cost.white +
                        cost.generic;
}

bool pay_mana(PlayerState& player, const ManaCost& cost) {
    if (!can_pay(player, cost)) {
        return false;
    }

    std::vector<bool> selected(player.lands.size(), false);
    auto select_colored = [&](CardId land_type, int amount) {
        for (std::size_t index = 0;
             index < player.lands.size() && amount > 0; ++index) {
            if (!player.lands[index].tapped &&
                player.lands[index].card == land_type) {
                selected[index] = true;
                --amount;
            }
        }
    };

    select_colored(CardId::Forest, cost.green);
    select_colored(CardId::Mountain, cost.red);
    select_colored(CardId::Island, cost.blue);
    select_colored(CardId::Plains, cost.white);

    int generic_remaining = cost.generic;
    for (std::size_t index = 0;
         index < player.lands.size() && generic_remaining > 0; ++index) {
        if (!player.lands[index].tapped && !selected[index]) {
            selected[index] = true;
            --generic_remaining;
        }
    }

    for (std::size_t index = 0; index < player.lands.size(); ++index) {
        if (selected[index]) {
            player.lands[index].tapped = true;
        }
    }
    return true;
}

CreaturePermanent* find_creature(PlayerState& player, PermanentId id) {
    const auto position = std::find_if(
        player.creatures.begin(), player.creatures.end(),
        [id](const CreaturePermanent& creature) { return creature.id == id; });
    return position == player.creatures.end() ? nullptr : &*position;
}

ArtifactPermanent* find_artifact(PlayerState& player, PermanentId id) {
    const auto position = std::find_if(
        player.artifacts.begin(), player.artifacts.end(),
        [id](const ArtifactPermanent& artifact) {
            return artifact.id == id;
        });
    return position == player.artifacts.end() ? nullptr : &*position;
}

bool moat_on_battlefield(const GameState& state) {
    return std::any_of(
        state.players.begin(), state.players.end(),
        [](const PlayerState& player) {
            return std::find(player.enchantments.begin(),
                             player.enchantments.end(),
                             CardId::Moat) != player.enchantments.end();
        });
}

bool can_attack_through_moat(const GameState& state,
                             const CreaturePermanent& creature) {
    return !moat_on_battlefield(state) ||
           card_definition(creature.card).flying;
}

std::vector<std::vector<PermanentId>>
learned_value_attack_candidates(
    const std::vector<PermanentId>& legal_attackers,
    std::mt19937_64& random) {
    constexpr std::size_t kExhaustiveLimit = 256;
    std::vector<std::vector<PermanentId>> candidates;
    if (legal_attackers.size() < 63 &&
        (std::uint64_t{1} << legal_attackers.size()) <=
            kExhaustiveLimit) {
        const std::uint64_t combinations =
            std::uint64_t{1} << legal_attackers.size();
        candidates.reserve(static_cast<std::size_t>(combinations));
        for (std::uint64_t mask = 0; mask < combinations; ++mask) {
            std::vector<PermanentId> candidate;
            for (std::size_t index = 0;
                 index < legal_attackers.size(); ++index) {
                if ((mask & (std::uint64_t{1} << index)) != 0) {
                    candidate.push_back(legal_attackers[index]);
                }
            }
            candidates.push_back(std::move(candidate));
        }
        return candidates;
    }

    candidates = {{}, legal_attackers};
    for (const PermanentId attacker : legal_attackers) {
        candidates.push_back({attacker});
    }
    std::uniform_int_distribution<int> include_attacker(0, 1);
    constexpr int kRandomCandidates = 48;
    for (int sample = 0; sample < kRandomCandidates; ++sample) {
        std::vector<PermanentId> candidate;
        for (const PermanentId attacker : legal_attackers) {
            if (include_attacker(random) == 1) {
                candidate.push_back(attacker);
            }
        }
        candidates.push_back(std::move(candidate));
    }
    return candidates;
}

std::vector<std::vector<std::pair<PermanentId, PermanentId>>>
learned_value_block_candidates(
    const std::vector<PermanentId>& attackers,
    const std::vector<PermanentId>& available_blockers,
    std::mt19937_64& random, std::size_t exhaustive_limit,
    int random_samples) {
    using Block = std::pair<PermanentId, PermanentId>;
    std::vector<std::vector<Block>> candidates;
    const std::size_t choices = attackers.size() + 1;
    std::size_t combinations = 1;
    bool exhaustive = !attackers.empty();
    for (std::size_t blocker = 0;
         blocker < available_blockers.size(); ++blocker) {
        if (combinations > exhaustive_limit / choices) {
            exhaustive = false;
            break;
        }
        combinations *= choices;
    }
    if (exhaustive) {
        candidates.reserve(combinations);
        for (std::size_t encoding = 0; encoding < combinations;
             ++encoding) {
            std::size_t remaining = encoding;
            std::vector<Block> candidate;
            for (const PermanentId blocker : available_blockers) {
                const std::size_t choice = remaining % choices;
                remaining /= choices;
                if (choice != 0) {
                    candidate.emplace_back(attackers[choice - 1],
                                           blocker);
                }
            }
            candidates.push_back(std::move(candidate));
        }
        return candidates;
    }

    candidates.push_back({});
    for (const PermanentId attacker : attackers) {
        std::vector<Block> all_on_attacker;
        for (const PermanentId blocker : available_blockers) {
            all_on_attacker.emplace_back(attacker, blocker);
        }
        candidates.push_back(std::move(all_on_attacker));
    }
    for (int sample = 0; sample < random_samples; ++sample) {
        std::vector<Block> candidate;
        std::uniform_int_distribution<std::size_t> choose_block(
            0, attackers.size());
        for (const PermanentId blocker : available_blockers) {
            const std::size_t choice = choose_block(random);
            if (choice != 0) {
                candidate.emplace_back(attackers[choice - 1],
                                       blocker);
            }
        }
        std::shuffle(candidate.begin(), candidate.end(), random);
        candidates.push_back(std::move(candidate));
    }
    return candidates;
}

void remove_dead_creatures(PlayerState& player) {
    auto creature = player.creatures.begin();
    while (creature != player.creatures.end()) {
        const auto& definition = card_definition(creature->card);
        if (creature->damage >= definition.toughness) {
            player.graveyard.push_back(creature->card);
            creature = player.creatures.erase(creature);
        } else {
            ++creature;
        }
    }
}

bool contains_action(const std::vector<PriorityAction>& actions,
                     const PriorityAction& wanted) {
    return std::find(actions.begin(), actions.end(), wanted) != actions.end();
}

std::size_t opponent_of(std::size_t player) {
    return 1 - player;
}

double handcrafted_card_value(CardId card) {
    switch (card) {
    case CardId::Forest:
    case CardId::Mountain:
    case CardId::Island:
    case CardId::Plains:
        return 100.0;
    case CardId::GrizzlyBears:
        return 400.0;
    case CardId::IronrootTreefolk:
        return 700.0;
    case CardId::FireElemental:
    case CardId::WaterElemental:
        return 900.0;
    case CardId::LightningBolt:
        return 800.0;
    case CardId::Counterspell:
        return 1'000.0;
    case CardId::Tsunami:
        return 700.0;
    case CardId::Millstone:
        return 1'100.0;
    case CardId::Moat:
        return 1'300.0;
    }
    return 0.0;
}

LearnedModel::FeatureVector learned_features(const GameState& state,
                                             std::size_t perspective) {
    const std::size_t opponent = opponent_of(perspective);
    const auto& self = state.players[perspective];
    const auto& enemy = state.players[opponent];
    const auto untapped_lands = [](const PlayerState& player) {
        return std::count_if(
            player.lands.begin(), player.lands.end(),
            [](const LandPermanent& land) { return !land.tapped; });
    };
    const auto total_power = [](const PlayerState& player) {
        int total = 0;
        for (const auto& creature : player.creatures) {
            total += card_definition(creature.card).power;
        }
        return total;
    };
    const auto total_toughness = [](const PlayerState& player) {
        int total = 0;
        for (const auto& creature : player.creatures) {
            total += std::max(
                0, card_definition(creature.card).toughness -
                       creature.damage);
        }
        return total;
    };
    const auto available_power = [](const PlayerState& player) {
        int total = 0;
        for (const auto& creature : player.creatures) {
            if (!creature.tapped && !creature.summoning_sick) {
                total += card_definition(creature.card).power;
            }
        }
        return total;
    };
    const auto permanent_count = [](const PlayerState& player) {
        return player.artifacts.size() + player.enchantments.size();
    };
    int stack_advantage = 0;
    for (const auto& object : state.stack) {
        stack_advantage += object.controller == perspective ? 1 : -1;
    }
    LearnedModel::FeatureVector features = {
        static_cast<double>(self.life) / 20.0,
        static_cast<double>(enemy.life) / 20.0,
        static_cast<double>(self.library.size()) / 40.0,
        static_cast<double>(enemy.library.size()) / 40.0,
        static_cast<double>(self.hand.size()) / 10.0,
        static_cast<double>(enemy.hand.size()) / 10.0,
        static_cast<double>(self.lands.size()) / 10.0,
        static_cast<double>(enemy.lands.size()) / 10.0,
        static_cast<double>(untapped_lands(self)) / 10.0,
        static_cast<double>(untapped_lands(enemy)) / 10.0,
        static_cast<double>(self.creatures.size()) / 10.0,
        static_cast<double>(enemy.creatures.size()) / 10.0,
        static_cast<double>(total_power(self)) / 20.0,
        static_cast<double>(total_power(enemy)) / 20.0,
        static_cast<double>(total_toughness(self)) / 20.0,
        static_cast<double>(total_toughness(enemy)) / 20.0,
        static_cast<double>(available_power(self)) / 20.0,
        static_cast<double>(available_power(enemy)) / 20.0,
        static_cast<double>(permanent_count(self)) / 5.0,
        static_cast<double>(permanent_count(enemy)) / 5.0,
        static_cast<double>(state.stack.size()) / 5.0,
        static_cast<double>(stack_advantage) / 5.0 +
            (state.active_player == perspective ? 0.25 : -0.25) +
            std::min(1.0, static_cast<double>(state.turn_number) / 80.0) *
                0.1,
    };

    using CardPlane = std::array<double, kLearnedCardCount>;
    CardPlane own_library{};
    CardPlane own_hand{};
    CardPlane own_battlefield{};
    CardPlane enemy_battlefield{};
    CardPlane own_tapped{};
    CardPlane enemy_tapped{};
    CardPlane own_summoning_sick{};
    CardPlane enemy_summoning_sick{};
    CardPlane own_creature_damage{};
    CardPlane enemy_creature_damage{};
    CardPlane own_graveyard{};
    CardPlane enemy_graveyard{};
    CardPlane own_stack{};
    CardPlane enemy_stack{};
    const auto card_index = [](CardId card) {
        return static_cast<std::size_t>(card);
    };
    for (const CardId card : self.library) {
        ++own_library[card_index(card)];
    }
    for (const CardId card : self.hand) {
        ++own_hand[card_index(card)];
    }
    const auto add_battlefield =
        [&](const PlayerState& player, CardPlane& battlefield,
            CardPlane& tapped, CardPlane& summoning_sick,
            CardPlane& creature_damage) {
            for (const auto& land : player.lands) {
                ++battlefield[card_index(land.card)];
                if (land.tapped) {
                    ++tapped[card_index(land.card)];
                }
            }
            for (const auto& creature : player.creatures) {
                ++battlefield[card_index(creature.card)];
                if (creature.tapped) {
                    ++tapped[card_index(creature.card)];
                }
                if (creature.summoning_sick) {
                    ++summoning_sick[card_index(creature.card)];
                }
                creature_damage[card_index(creature.card)] +=
                    static_cast<double>(creature.damage);
            }
            for (const auto& artifact : player.artifacts) {
                ++battlefield[card_index(artifact.card)];
                if (artifact.tapped) {
                    ++tapped[card_index(artifact.card)];
                }
            }
            for (const CardId enchantment : player.enchantments) {
                ++battlefield[card_index(enchantment)];
            }
        };
    add_battlefield(self, own_battlefield, own_tapped,
                    own_summoning_sick, own_creature_damage);
    add_battlefield(enemy, enemy_battlefield, enemy_tapped,
                    enemy_summoning_sick, enemy_creature_damage);
    for (const CardId card : self.graveyard) {
        ++own_graveyard[card_index(card)];
    }
    for (const CardId card : enemy.graveyard) {
        ++enemy_graveyard[card_index(card)];
    }
    for (const auto& object : state.stack) {
        auto& plane =
            object.controller == perspective ? own_stack : enemy_stack;
        ++plane[card_index(object.card)];
    }

    std::size_t feature = LearnedModel::kScalarFeatureCount;
    const auto append_plane =
        [&](const CardPlane& plane, double normalization) {
            for (const double value : plane) {
                features[feature++] = value / normalization;
            }
        };
    append_plane(own_library, 20.0);
    append_plane(own_hand, 10.0);
    append_plane(own_battlefield, 10.0);
    append_plane(enemy_battlefield, 10.0);
    append_plane(own_tapped, 10.0);
    append_plane(enemy_tapped, 10.0);
    append_plane(own_summoning_sick, 10.0);
    append_plane(enemy_summoning_sick, 10.0);
    append_plane(own_creature_damage, 10.0);
    append_plane(enemy_creature_damage, 10.0);
    append_plane(own_graveyard, 20.0);
    append_plane(enemy_graveyard, 20.0);
    append_plane(own_stack, 5.0);
    append_plane(enemy_stack, 5.0);
    return features;
}

LearnedValueAttackSetScores score_learned_value_attack_sets(
    const GameState& state, std::size_t attacking_player,
    const std::vector<std::vector<PermanentId>>& candidates,
    const std::shared_ptr<const LearnedModel>& model,
    std::mt19937_64& random) {
    if (attacking_player >= state.players.size()) {
        throw std::out_of_range(
            "Learned Value attacking player must be 0 or 1");
    }
    if (state.active_player != attacking_player) {
        throw std::invalid_argument(
            "Learned Value attack scoring requires the active player");
    }
    if (!state.stack.empty()) {
        throw std::invalid_argument(
            "Learned Value attack scoring requires an empty stack");
    }
    if (!model ||
        model->variant() !=
            LearnedVariant::ValueSearchChampion) {
        throw std::invalid_argument(
            "Learned Value attack scoring requires a Value model");
    }
    if (candidates.empty()) {
        throw std::invalid_argument(
            "Learned Value attack scoring requires candidates");
    }
    for (const auto& candidate : candidates) {
        GameState legality_check = state;
        if (!resolve_combat(
                legality_check, attacking_player, candidate, {})) {
            throw std::invalid_argument(
                "Learned Value attack set is not legal");
        }
    }

    const std::size_t defending_player =
        opponent_of(attacking_player);
    std::vector<PermanentId> available_blockers;
    for (const CreaturePermanent& creature :
         state.players[defending_player].creatures) {
        if (!creature.tapped) {
            available_blockers.push_back(creature.id);
        }
    }

    LearnedValueAttackSetScores result;
    result.scores.reserve(candidates.size());
    double best_score =
        -std::numeric_limits<double>::infinity();
    for (std::size_t candidate_index = 0;
         candidate_index < candidates.size();
         ++candidate_index) {
        const auto& candidate = candidates[candidate_index];
        // These bounds and the candidate ordering are intentionally the
        // deployed distribution. Do not "improve" them in an evaluation
        // refactor: label parity depends on preserving this exact behavior.
        const auto block_candidates =
            learned_value_block_candidates(
                candidate, available_blockers, random, 64, 48);
        double total_score = 0.0;
        for (const auto& sampled_blocks : block_candidates) {
            GameState successor = state;
            if (!resolve_combat(
                    successor, attacking_player, candidate,
                    sampled_blocks)) {
                throw std::logic_error(
                    "Learned Value sampled illegal combat");
            }
            double sample_score = 0.5;
            if (successor.players[defending_player].life <= 0) {
                sample_score = 1.0;
            } else if (
                successor.players[attacking_player].life <= 0) {
                sample_score = 0.0;
            } else {
                sample_score = model->predict(
                    learned_features(successor, attacking_player));
            }
            total_score += sample_score;
        }
        const double expected_score =
            total_score /
            static_cast<double>(block_candidates.size());
        result.scores.push_back(expected_score);
        if (expected_score > best_score) {
            best_score = expected_score;
            result.selected_candidate = candidate_index;
        }
    }
    return result;
}

enum class LearnedPolicyVerb : std::uint8_t {
    Pass,
    Play,
    Cast,
    Activate,
    Skip,
    Include,
    Assign,
    ChooseNext,
};

enum class LearnedTargetRelation : std::uint8_t {
    None,
    PlayerSelf,
    PlayerOpponent,
    PermanentSelf,
    PermanentOpponent,
    StackSelf,
    StackOpponent,
};

using LearnedCardPlane = std::array<double, kLearnedCardCount>;

struct LearnedPolicyObject {
    std::optional<CardId> card;
    int power = 0;
    int toughness = 0;
    int damage = 0;
    bool tapped = false;
    bool summoning_sick = false;
};

struct LearnedPolicyOption {
    LearnedDecisionKind decision = LearnedDecisionKind::Priority;
    TurnPhase phase = TurnPhase::FirstMain;
    LearnedPolicyVerb verb = LearnedPolicyVerb::Pass;
    LearnedPolicyObject source;
    LearnedPolicyObject target;
    LearnedTargetRelation target_relation =
        LearnedTargetRelation::None;
    LearnedCardPlane selected_attackers{};
    LearnedCardPlane assigned_blockers{};
    LearnedCardPlane relevant_blockers{};
    LearnedCardPlane ordered_blockers{};
    double remaining_options = 0.0;
    double chosen_count = 0.0;
    double assigned_to_target_count = 0.0;
    double selected_power = 0.0;
    double assigned_power = 0.0;
    double consecutive_passes = 0.0;
    bool sorcery_actions = false;
};

LearnedPolicyObject policy_card_object(CardId card) {
    const auto& definition = card_definition(card);
    return {
        .card = card,
        .power = definition.power,
        .toughness = definition.toughness,
    };
}

LearnedPolicyObject
policy_creature_object(const CreaturePermanent& creature) {
    const auto& definition = card_definition(creature.card);
    return {
        .card = creature.card,
        .power = definition.power,
        .toughness = definition.toughness,
        .damage = creature.damage,
        .tapped = creature.tapped,
        .summoning_sick = creature.summoning_sick,
    };
}

const CreaturePermanent*
find_creature_for_policy(const PlayerState& player, PermanentId id) {
    const auto creature = std::find_if(
        player.creatures.begin(), player.creatures.end(),
        [id](const CreaturePermanent& candidate) {
            return candidate.id == id;
        });
    return creature == player.creatures.end() ? nullptr : &*creature;
}

LearnedModel::PolicyFeatureVector learned_policy_features(
    const GameState& state, std::size_t perspective,
    const LearnedPolicyOption& option) {
    LearnedModel::PolicyFeatureVector features{};
    const auto observation = learned_features(state, perspective);
    std::copy(observation.begin(), observation.end(), features.begin());
    std::size_t feature = LearnedModel::kFeatureCount;

    features[feature +
             static_cast<std::size_t>(option.decision)] = 1.0;
    feature += LearnedModel::kPolicyDecisionCount;
    features[feature + static_cast<std::size_t>(option.phase)] = 1.0;
    feature += LearnedModel::kPolicyPhaseCount;
    features[feature + static_cast<std::size_t>(option.verb)] = 1.0;
    feature += LearnedModel::kPolicyVerbCount;

    LearnedCardPlane source_card{};
    LearnedCardPlane target_card{};
    if (option.source.card.has_value()) {
        source_card[static_cast<std::size_t>(*option.source.card)] =
            1.0;
    }
    if (option.target.card.has_value()) {
        target_card[static_cast<std::size_t>(*option.target.card)] =
            1.0;
    }
    const auto append_plane = [&](const LearnedCardPlane& plane) {
        for (const double value : plane) {
            features[feature++] = value / 10.0;
        }
    };
    const auto append_identity_plane =
        [&](const LearnedCardPlane& plane) {
            for (const double value : plane) {
                features[feature++] = value;
            }
        };
    append_identity_plane(source_card);
    append_identity_plane(target_card);
    append_plane(option.selected_attackers);
    append_plane(option.assigned_blockers);
    append_plane(option.relevant_blockers);
    append_plane(option.ordered_blockers);

    std::array<double, LearnedModel::kPolicyScalarCount> scalars{};
    std::size_t scalar = 0;
    if (option.source.card.has_value()) {
        const auto& definition =
            card_definition(*option.source.card);
        scalars[scalar + static_cast<std::size_t>(definition.type)] =
            1.0;
        scalar += 6;
        scalars[scalar++] =
            static_cast<double>(definition.cost.generic) / 10.0;
        scalars[scalar++] =
            static_cast<double>(definition.cost.green) / 5.0;
        scalars[scalar++] =
            static_cast<double>(definition.cost.red) / 5.0;
        scalars[scalar++] =
            static_cast<double>(definition.cost.blue) / 5.0;
        scalars[scalar++] =
            static_cast<double>(definition.cost.white) / 5.0;
        scalars[scalar++] =
            static_cast<double>(definition.power) / 10.0;
        scalars[scalar++] =
            static_cast<double>(definition.toughness) / 10.0;
        scalars[scalar++] =
            static_cast<double>(definition.effect_damage) / 10.0;
    } else {
        scalar += 14;
    }
    scalars[scalar++] =
        static_cast<double>(option.source.damage) / 10.0;
    scalars[scalar++] = option.source.tapped ? 1.0 : 0.0;
    scalars[scalar++] =
        option.source.summoning_sick ? 1.0 : 0.0;

    scalars[scalar +
            static_cast<std::size_t>(option.target_relation)] = 1.0;
    scalar += 7;
    scalars[scalar++] =
        static_cast<double>(option.target.power) / 10.0;
    scalars[scalar++] =
        static_cast<double>(option.target.toughness) / 10.0;
    scalars[scalar++] =
        static_cast<double>(option.target.damage) / 10.0;
    scalars[scalar++] = option.target.tapped ? 1.0 : 0.0;
    scalars[scalar++] =
        option.target.summoning_sick ? 1.0 : 0.0;
    scalars[scalar++] = option.remaining_options / 10.0;
    scalars[scalar++] = option.chosen_count / 10.0;
    scalars[scalar++] = option.assigned_to_target_count / 10.0;
    scalars[scalar++] = option.selected_power / 20.0;
    scalars[scalar++] = option.assigned_power / 20.0;
    scalars[scalar++] = option.consecutive_passes / 2.0;
    scalars[scalar++] = option.sorcery_actions ? 1.0 : 0.0;
    if (scalar != scalars.size()) {
        throw std::logic_error(
            "Learned policy scalar feature count is inconsistent");
    }
    for (const double value : scalars) {
        features[feature++] = value;
    }
    if (feature != features.size()) {
        throw std::logic_error(
            "Learned policy feature count is inconsistent");
    }
    return features;
}

std::vector<LearnedModel::PolicyFeatureVector>
encode_learned_policy_options(
    const GameState& state, std::size_t player,
    const std::vector<LearnedPolicyOption>& options) {
    std::vector<LearnedModel::PolicyFeatureVector> encoded;
    encoded.reserve(options.size());
    for (const auto& option : options) {
        encoded.push_back(
            learned_policy_features(state, player, option));
    }
    return encoded;
}

std::shared_ptr<const LearnedModel> configured_learned_model(
    const GameConfig& config, std::size_t player) {
    if (player >= config.bots.size()) {
        throw std::out_of_range("Learned model player must be 0 or 1");
    }
    if (config.bots[player].learned_model) {
        return config.bots[player].learned_model;
    }
    return config.learned_model;
}

std::optional<std::size_t> generation_kind_index(
    LearnedDecisionKind kind) {
    switch (kind) {
    case LearnedDecisionKind::Priority:
        return 0;
    case LearnedDecisionKind::Attack:
        return 1;
    case LearnedDecisionKind::Block:
    case LearnedDecisionKind::DamageOrder:
        return std::nullopt;
    }
    return std::nullopt;
}

std::uint64_t generation_decision_subindex(
    std::size_t player, std::size_t kind_index,
    std::size_t ordinal) {
    return
        (static_cast<std::uint64_t>(player) << 56) ^
        (static_cast<std::uint64_t>(kind_index) << 48) ^
        static_cast<std::uint64_t>(ordinal);
}

std::optional<std::uint64_t> reserve_generation_search_seed(
    const GameConfig& config, std::size_t player,
    LearnedDecisionKind kind) {
    if (!config.learned_policy_recorder ||
        !config.learned_policy_recorder->generation_collection) {
        return std::nullopt;
    }
    const auto kind_index = generation_kind_index(kind);
    if (!kind_index.has_value()) {
        return std::nullopt;
    }
    auto& collection =
        *config.learned_policy_recorder->generation_collection;
    auto& roots =
        collection.searched_roots[player][*kind_index];
    if (roots >= collection.max_roots_per_seat_kind) {
        return std::nullopt;
    }
    const std::size_t ordinal = roots++;
    const auto domain =
        kind == LearnedDecisionKind::Priority
            ? learned_iteration::SeedDomain::PrioritySearch
            : learned_iteration::SeedDomain::AttackSearch;
    return learned_iteration::derive_seed(
        collection.root_seed, domain, collection.generation,
        collection.schedule_index,
        generation_decision_subindex(
            player, *kind_index, ordinal));
}

std::optional<std::uint64_t> next_generation_choice_seed(
    const GameConfig& config, std::size_t player,
    LearnedDecisionKind kind) {
    if (!config.learned_policy_recorder ||
        !config.learned_policy_recorder->generation_collection) {
        return std::nullopt;
    }
    const auto kind_index = generation_kind_index(kind);
    if (!kind_index.has_value()) {
        return std::nullopt;
    }
    auto& collection =
        *config.learned_policy_recorder->generation_collection;
    auto& choices =
        collection.action_choice_indices[player][*kind_index];
    const std::size_t ordinal = choices++;
    const auto domain =
        kind == LearnedDecisionKind::Priority
            ? learned_iteration::SeedDomain::PriorityChoice
            : learned_iteration::SeedDomain::AttackChoice;
    return learned_iteration::derive_seed(
        collection.root_seed, domain, collection.generation,
        collection.schedule_index,
        generation_decision_subindex(
            player, *kind_index, ordinal));
}

void add_generation_rollout_evaluations(
    const GameConfig& config, std::size_t player,
    LearnedDecisionKind kind, std::size_t evaluations) {
    const auto kind_index = generation_kind_index(kind);
    if (!kind_index.has_value() ||
        !config.learned_policy_recorder ||
        !config.learned_policy_recorder->generation_collection) {
        throw std::logic_error(
            "generation rollout accounting requires collection mode");
    }
    config.learned_policy_recorder->generation_collection
        ->rollout_evaluations[player][*kind_index] +=
        evaluations;
}

std::size_t choose_best_generation_score(
    const std::vector<double>& scores, std::uint64_t seed) {
    if (scores.empty() ||
        !std::all_of(
            scores.begin(), scores.end(),
            [](double value) { return std::isfinite(value); })) {
        throw std::invalid_argument(
            "generation choices require finite scores");
    }
    const double best =
        *std::max_element(scores.begin(), scores.end());
    std::vector<std::size_t> best_options;
    for (std::size_t index = 0; index < scores.size(); ++index) {
        if (scores[index] == best) {
            best_options.push_back(index);
        }
    }
    std::mt19937_64 random(seed);
    std::uniform_int_distribution<std::size_t> break_tie(
        0, best_options.size() - 1);
    return best_options[break_tie(random)];
}

std::vector<double> mean_generation_action_scores(
    const LearnedActionSamples& samples) {
    std::vector<double> means;
    means.reserve(samples.q_samples.size());
    for (const auto& action_samples : samples.q_samples) {
        if (action_samples.empty()) {
            throw std::logic_error(
                "generation search returned no action samples");
        }
        double total = 0.0;
        for (const double sample : action_samples) {
            total += sample;
        }
        means.push_back(
            total / static_cast<double>(action_samples.size()));
    }
    return means;
}

void record_learned_policy_choice(
    const GameState& state, const GameConfig& config,
    std::size_t player, LearnedDecisionKind kind,
    std::vector<LearnedModel::PolicyFeatureVector> encoded,
    std::size_t chosen,
    std::vector<double> target_probabilities = {}) {
    if (!config.learned_policy_recorder || encoded.size() < 2) {
        return;
    }
    if (config.learned_policy_recorder->generation_collection &&
        (!generation_kind_index(kind).has_value() ||
         target_probabilities.size() != encoded.size())) {
        // G1 only learns from its searched Priority/Attack roots. In
        // particular, copied Block/Damage heads and capped raw choices must
        // not leak old on-policy targets into this generation.
        return;
    }
    const auto model = configured_learned_model(config, player);
    const double baseline =
        model ? model->predict(learned_features(state, player)) : 0.5;
    config.learned_policy_recorder->steps.push_back({
        .options = std::move(encoded),
        .critic_features = learned_features(state, player),
        .chosen = chosen,
        .actor = player,
        .kind = kind,
        .critic_baseline = baseline,
        .target_probabilities =
            std::move(target_probabilities),
    });
}

std::size_t choose_learned_policy_option(
    const GameState& state, const GameConfig& config,
    const std::vector<LearnedPolicyOption>& options,
    std::size_t player, std::mt19937_64& random) {
    const auto model = configured_learned_model(config, player);
    if (!model) {
        throw std::logic_error("Learned bot has no policy model");
    }
    if (options.empty()) {
        throw std::logic_error("Learned policy has no legal options");
    }
    auto encoded =
        encode_learned_policy_options(state, player, options);
    std::vector<double> logits;
    logits.reserve(encoded.size());
    for (const auto& option : encoded) {
        logits.push_back(model->policy_logit(
            option,
            static_cast<std::size_t>(
                options.front().decision)));
    }

    std::optional<std::mt19937_64> indexed_random;
    if (const auto seed = next_generation_choice_seed(
            config, player, options.front().decision);
        seed.has_value()) {
        indexed_random.emplace(*seed);
    }
    std::mt19937_64& choice_random =
        indexed_random.has_value() ? *indexed_random : random;

    std::size_t chosen = 0;
    const double temperature =
        config.bots[player].exploration_rate;
    if (temperature > 0.0) {
        const double max_logit =
            *std::max_element(logits.begin(), logits.end());
        std::vector<double> weights;
        weights.reserve(logits.size());
        for (const double logit : logits) {
            weights.push_back(
                std::exp((logit - max_logit) / temperature));
        }
        std::discrete_distribution<std::size_t> sample(
            weights.begin(), weights.end());
        chosen = sample(choice_random);
    } else {
        const double best =
            *std::max_element(logits.begin(), logits.end());
        std::vector<std::size_t> best_options;
        for (std::size_t index = 0; index < logits.size(); ++index) {
            if (logits[index] == best) {
                best_options.push_back(index);
            }
        }
        std::uniform_int_distribution<std::size_t> break_tie(
            0, best_options.size() - 1);
        chosen = best_options[break_tie(choice_random)];
    }
    record_learned_policy_choice(
        state, config, player, options.front().decision,
        std::move(encoded), chosen);
    return chosen;
}

void record_uniform_policy_option(
    const GameState& state, const GameConfig& config,
    const std::vector<LearnedPolicyOption>& options,
    std::size_t player, std::size_t chosen) {
    if (!config.learned_policy_recorder) {
        return;
    }
    record_learned_policy_choice(
        state, config, player, options.front().decision,
        encode_learned_policy_options(state, player, options), chosen);
}

LearnedPolicyOption priority_policy_option(
    const GameState& state, std::size_t player,
    const PriorityAction& action, bool sorcery_actions,
    TurnPhase phase, int consecutive_passes) {
    LearnedPolicyOption option;
    option.decision = LearnedDecisionKind::Priority;
    option.phase = phase;
    option.sorcery_actions = sorcery_actions;
    option.consecutive_passes =
        static_cast<double>(consecutive_passes);
    switch (action.kind) {
    case PriorityActionKind::Pass:
        option.verb = LearnedPolicyVerb::Pass;
        return option;
    case PriorityActionKind::PlayLand:
        option.verb = LearnedPolicyVerb::Play;
        break;
    case PriorityActionKind::ActivateMillstone:
        option.verb = LearnedPolicyVerb::Activate;
        break;
    case PriorityActionKind::CastCreature:
    case PriorityActionKind::CastSorcery:
    case PriorityActionKind::CastArtifact:
    case PriorityActionKind::CastEnchantment:
    case PriorityActionKind::CastLightningBolt:
    case PriorityActionKind::CastCounterspell:
        option.verb = LearnedPolicyVerb::Cast;
        break;
    }
    option.source = policy_card_object(action.card);

    if (action.target.has_value()) {
        const Target& target = *action.target;
        if (!target.creature.has_value()) {
            option.target_relation =
                target.player == player
                    ? LearnedTargetRelation::PlayerSelf
                    : LearnedTargetRelation::PlayerOpponent;
        } else {
            option.target_relation =
                target.player == player
                    ? LearnedTargetRelation::PermanentSelf
                    : LearnedTargetRelation::PermanentOpponent;
            const auto* creature = find_creature_for_policy(
                state.players[target.player], *target.creature);
            if (creature != nullptr) {
                option.target = policy_creature_object(*creature);
            }
        }
    } else if (action.spell_target.has_value()) {
        const auto stack_object = std::find_if(
            state.stack.begin(), state.stack.end(),
            [&](const StackObject& object) {
                return object.id == *action.spell_target;
            });
        if (stack_object != state.stack.end()) {
            option.target =
                policy_card_object(stack_object->card);
            option.target_relation =
                stack_object->controller == player
                    ? LearnedTargetRelation::StackSelf
                    : LearnedTargetRelation::StackOpponent;
        }
    }
    return option;
}

std::vector<LearnedPolicyOption> priority_policy_options(
    const GameState& state, std::size_t player,
    const std::vector<PriorityAction>& actions,
    bool sorcery_actions, TurnPhase phase,
    int consecutive_passes) {
    std::vector<LearnedPolicyOption> options;
    options.reserve(actions.size());
    for (const auto& action : actions) {
        options.push_back(
            priority_policy_option(
                state, player, action, sorcery_actions,
                phase, consecutive_passes));
    }
    return options;
}

void add_policy_creature(
    const PlayerState& player, PermanentId id,
    LearnedCardPlane& plane, double& total_power) {
    const auto* creature = find_creature_for_policy(player, id);
    if (creature == nullptr) {
        throw std::logic_error(
            "Learned policy context references a missing creature");
    }
    ++plane[static_cast<std::size_t>(creature->card)];
    total_power +=
        static_cast<double>(card_definition(creature->card).power);
}

std::vector<LearnedPolicyOption> attack_policy_options(
    const GameState& state, std::size_t attacking_player,
    PermanentId subject, const std::vector<PermanentId>& selected,
    std::size_t remaining) {
    const auto* creature = find_creature_for_policy(
        state.players[attacking_player], subject);
    if (creature == nullptr) {
        throw std::logic_error(
            "Learned attack decision has a missing attacker");
    }

    LearnedPolicyOption base;
    base.decision = LearnedDecisionKind::Attack;
    base.phase = TurnPhase::DeclareAttackers;
    base.source = policy_creature_object(*creature);
    base.remaining_options = static_cast<double>(remaining);
    base.chosen_count = static_cast<double>(selected.size());
    for (const PermanentId attacker : selected) {
        add_policy_creature(
            state.players[attacking_player], attacker,
            base.selected_attackers, base.selected_power);
    }

    auto skip = base;
    skip.verb = LearnedPolicyVerb::Skip;
    auto include = base;
    include.verb = LearnedPolicyVerb::Include;
    return {std::move(skip), std::move(include)};
}

std::vector<LearnedPolicyOption> block_policy_options(
    const GameState& state, std::size_t attacking_player,
    PermanentId subject, const std::vector<PermanentId>& attackers,
    const std::unordered_map<PermanentId, std::vector<PermanentId>>&
        blockers_by_attacker,
    std::size_t remaining) {
    const std::size_t defending_player =
        opponent_of(attacking_player);
    const auto* blocker = find_creature_for_policy(
        state.players[defending_player], subject);
    if (blocker == nullptr) {
        throw std::logic_error(
            "Learned block decision has a missing blocker");
    }

    LearnedPolicyOption base;
    base.decision = LearnedDecisionKind::Block;
    base.phase = TurnPhase::DeclareBlockers;
    base.source = policy_creature_object(*blocker);
    base.remaining_options = static_cast<double>(remaining);
    for (const PermanentId attacker : attackers) {
        double ignored_power = 0.0;
        add_policy_creature(
            state.players[attacking_player], attacker,
            base.selected_attackers, ignored_power);
    }
    for (const auto& [attacker, assigned] :
         blockers_by_attacker) {
        static_cast<void>(attacker);
        for (const PermanentId assigned_blocker : assigned) {
            add_policy_creature(
                state.players[defending_player],
                assigned_blocker, base.assigned_blockers,
                base.assigned_power);
            base.chosen_count += 1.0;
        }
    }

    std::vector<LearnedPolicyOption> options;
    options.reserve(attackers.size() + 1);
    auto skip = base;
    skip.verb = LearnedPolicyVerb::Skip;
    options.push_back(std::move(skip));
    for (const PermanentId attacker : attackers) {
        const auto* target = find_creature_for_policy(
            state.players[attacking_player], attacker);
        if (target == nullptr) {
            throw std::logic_error(
                "Learned block target is missing");
        }
        auto assign = base;
        assign.verb = LearnedPolicyVerb::Assign;
        assign.target = policy_creature_object(*target);
        assign.target_relation =
            LearnedTargetRelation::PermanentOpponent;
        const auto assigned =
            blockers_by_attacker.find(attacker);
        if (assigned != blockers_by_attacker.end()) {
            assign.assigned_to_target_count =
                static_cast<double>(assigned->second.size());
            double ignored_power = 0.0;
            for (const PermanentId existing : assigned->second) {
                add_policy_creature(
                    state.players[defending_player], existing,
                    assign.relevant_blockers, ignored_power);
            }
        }
        options.push_back(std::move(assign));
    }
    return options;
}

std::vector<LearnedPolicyOption> damage_order_policy_options(
    const GameState& state, std::size_t attacking_player,
    PermanentId attacker, const std::vector<PermanentId>& all_blockers,
    const std::vector<PermanentId>& remaining,
    const std::vector<PermanentId>& ordered) {
    const std::size_t defending_player =
        opponent_of(attacking_player);
    const auto* source = find_creature_for_policy(
        state.players[attacking_player], attacker);
    if (source == nullptr) {
        throw std::logic_error(
            "Learned damage-order attacker is missing");
    }

    LearnedPolicyOption base;
    base.decision = LearnedDecisionKind::DamageOrder;
    base.phase = TurnPhase::DamageOrder;
    base.verb = LearnedPolicyVerb::ChooseNext;
    base.source = policy_creature_object(*source);
    base.remaining_options = static_cast<double>(remaining.size());
    base.chosen_count = static_cast<double>(ordered.size());
    for (const PermanentId blocker : all_blockers) {
        double ignored_power = 0.0;
        add_policy_creature(
            state.players[defending_player], blocker,
            base.relevant_blockers, ignored_power);
    }
    for (const PermanentId blocker : ordered) {
        double ignored_power = 0.0;
        add_policy_creature(
            state.players[defending_player], blocker,
            base.ordered_blockers, ignored_power);
    }

    std::vector<LearnedPolicyOption> options;
    options.reserve(remaining.size());
    for (const PermanentId blocker : remaining) {
        const auto* target = find_creature_for_policy(
            state.players[defending_player], blocker);
        if (target == nullptr) {
            throw std::logic_error(
                "Learned damage-order blocker is missing");
        }
        auto option = base;
        option.target = policy_creature_object(*target);
        option.target_relation =
            LearnedTargetRelation::PermanentOpponent;
        options.push_back(std::move(option));
    }
    return options;
}

} // namespace

const CardDefinition& card_definition(CardId card) {
    const auto index = static_cast<std::size_t>(card);
    if (index >= kCardDefinitions.size()) {
        throw std::out_of_range("unknown card ID");
    }
    return kCardDefinitions[index];
}

std::vector<CardId> green_alpha_deck() {
    std::vector<CardId> deck(18, CardId::Forest);
    deck.insert(deck.end(), 9, CardId::GrizzlyBears);
    deck.insert(deck.end(), 12, CardId::IronrootTreefolk);
    deck.insert(deck.end(), 1, CardId::Tsunami);
    return deck;
}

std::vector<CardId> red_alpha_deck() {
    std::vector<CardId> deck(18, CardId::Mountain);
    deck.insert(deck.end(), 10, CardId::LightningBolt);
    deck.insert(deck.end(), 12, CardId::FireElemental);
    return deck;
}

std::vector<CardId> blue_alpha_deck() {
    std::vector<CardId> deck(18, CardId::Island);
    deck.insert(deck.end(), 14, CardId::Counterspell);
    deck.insert(deck.end(), 8, CardId::WaterElemental);
    return deck;
}

std::vector<CardId> white_control_deck() {
    std::vector<CardId> deck(22, CardId::Plains);
    deck.insert(deck.end(), 3, CardId::Millstone);
    deck.insert(deck.end(), 15, CardId::Moat);
    return deck;
}

GameState white_lock_plan_diagnostic_state() {
    GameState state;
    state.active_player = 0;
    state.starting_player = 0;
    state.turn_number = 13;
    state.next_permanent_id = 3;
    state.next_stack_object_id = 4;

    auto& white = state.players[0];
    white.library.assign(18, CardId::Plains);
    white.library.insert(
        white.library.end(), 2, CardId::Millstone);
    white.library.insert(white.library.end(), 7, CardId::Moat);
    white.hand.assign(7, CardId::Moat);
    white.lands.assign(
        4, LandPermanent{.card = CardId::Plains, .tapped = false});
    white.artifacts = {
        ArtifactPermanent{
            .id = 1,
            .card = CardId::Millstone,
            .tapped = false,
        },
    };
    white.enchantments = {CardId::Moat};
    white.land_played_this_turn = true;

    auto& red = state.players[1];
    red.library.assign(13, CardId::Mountain);
    red.library.insert(
        red.library.end(), 3, CardId::LightningBolt);
    red.library.insert(
        red.library.end(), 11, CardId::FireElemental);
    red.hand.assign(7, CardId::LightningBolt);
    red.lands.assign(
        5, LandPermanent{.card = CardId::Mountain, .tapped = false});
    red.creatures = {
        CreaturePermanent{
            .id = 2,
            .card = CardId::FireElemental,
            .tapped = false,
            .summoning_sick = false,
            .damage = 0,
        },
    };

    return state;
}

GameState sample_determinization(
    const GameState& state,
    const std::array<std::vector<CardId>, 2>& original_decks,
    std::size_t observer, std::uint64_t seed) {
    if (observer >= state.players.size()) {
        throw std::out_of_range("observer must be player 0 or 1");
    }
    for (const auto& object : state.stack) {
        if (object.controller >= state.players.size()) {
            throw std::invalid_argument(
                "stack object has an invalid controller");
        }
    }

    GameState sampled = state;
    std::mt19937_64 random(seed);
    for (std::size_t player = 0; player < state.players.size();
         ++player) {
        CardCounts remaining = card_counts(original_decks[player]);
        subtract_public_cards(remaining, state.players[player]);
        for (const auto& object : state.stack) {
            if (object.controller == player &&
                object.kind == StackObjectKind::Spell) {
                subtract_card(remaining, object.card);
            }
        }

        if (player == observer) {
            for (const CardId card : state.players[player].hand) {
                subtract_card(remaining, card);
            }
        }

        auto hidden_cards = expand_card_counts(remaining);
        const std::size_t expected_hidden =
            player == observer
                ? state.players[player].library.size()
                : state.players[player].hand.size() +
                      state.players[player].library.size();
        if (hidden_cards.size() != expected_hidden) {
            throw std::invalid_argument(
                "hidden zone sizes are inconsistent with original deck");
        }
        std::shuffle(hidden_cards.begin(), hidden_cards.end(), random);

        if (player == observer) {
            sampled.players[player].hand = state.players[player].hand;
            sampled.players[player].library = std::move(hidden_cards);
            continue;
        }

        const std::size_t hand_size = state.players[player].hand.size();
        const auto hand_end = hidden_cards.begin() +
                              static_cast<std::ptrdiff_t>(hand_size);
        sampled.players[player].hand.assign(hidden_cards.begin(),
                                            hand_end);
        sampled.players[player].library.assign(hand_end,
                                               hidden_cards.end());
    }

    for (std::size_t player = 0; player < state.players.size();
         ++player) {
        if (physical_card_counts(sampled, player) !=
            card_counts(original_decks[player])) {
            throw std::logic_error(
                "determinization failed physical card conservation");
        }
    }
    return sampled;
}

Target Target::player_target(std::size_t player_index) {
    return {.player = player_index, .creature = std::nullopt};
}

Target Target::creature_target(std::size_t controller,
                               PermanentId creature_id) {
    return {.player = controller, .creature = creature_id};
}

PriorityAction PriorityAction::pass() {
    return {};
}

PriorityAction PriorityAction::play_land(CardId land) {
    return {.kind = PriorityActionKind::PlayLand, .card = land};
}

PriorityAction PriorityAction::cast_creature(CardId creature) {
    return {.kind = PriorityActionKind::CastCreature, .card = creature};
}

PriorityAction PriorityAction::cast_sorcery(CardId sorcery) {
    return {.kind = PriorityActionKind::CastSorcery, .card = sorcery};
}

PriorityAction PriorityAction::cast_artifact(CardId artifact) {
    return {.kind = PriorityActionKind::CastArtifact, .card = artifact};
}

PriorityAction
PriorityAction::cast_enchantment(CardId enchantment) {
    return {
        .kind = PriorityActionKind::CastEnchantment,
        .card = enchantment,
    };
}

PriorityAction PriorityAction::cast_lightning_bolt(Target bolt_target) {
    return {.kind = PriorityActionKind::CastLightningBolt,
            .card = CardId::LightningBolt,
            .target = bolt_target};
}

PriorityAction
PriorityAction::cast_counterspell(StackObjectId target_spell) {
    return {
        .kind = PriorityActionKind::CastCounterspell,
        .card = CardId::Counterspell,
        .target = std::nullopt,
        .spell_target = target_spell,
    };
}

PriorityAction
PriorityAction::activate_millstone(PermanentId millstone,
                                   Target mill_target) {
    return {
        .kind = PriorityActionKind::ActivateMillstone,
        .card = CardId::Millstone,
        .target = mill_target,
        .spell_target = std::nullopt,
        .source_permanent = millstone,
    };
}

std::vector<PriorityAction>
legal_priority_actions(const GameState& state, std::size_t player,
                       bool sorcery_actions) {
    if (player >= state.players.size()) {
        return {};
    }

    const auto& player_state = state.players[player];
    std::vector<PriorityAction> actions = {PriorityAction::pass()};
    const bool has_sorcery_timing =
        sorcery_actions && player == state.active_player &&
        state.stack.empty();

    if (has_sorcery_timing && !player_state.land_played_this_turn) {
        for (const CardId land :
             {CardId::Forest, CardId::Mountain, CardId::Island,
              CardId::Plains}) {
            if (has_card(player_state.hand, land)) {
                actions.push_back(PriorityAction::play_land(land));
            }
        }
    }

    if (has_sorcery_timing) {
        for (const CardId creature : kCreatureCards) {
            const auto& definition = card_definition(creature);
            if (has_card(player_state.hand, creature) &&
                can_pay(player_state, definition.cost)) {
                actions.push_back(
                    PriorityAction::cast_creature(creature));
            }
        }
        for (const CardId sorcery : kSorceryCards) {
            const auto& definition = card_definition(sorcery);
            if (has_card(player_state.hand, sorcery) &&
                can_pay(player_state, definition.cost)) {
                actions.push_back(
                    PriorityAction::cast_sorcery(sorcery));
            }
        }
        for (const CardId artifact : kArtifactCards) {
            const auto& definition = card_definition(artifact);
            if (has_card(player_state.hand, artifact) &&
                can_pay(player_state, definition.cost)) {
                actions.push_back(
                    PriorityAction::cast_artifact(artifact));
            }
        }
        for (const CardId enchantment : kEnchantmentCards) {
            const auto& definition = card_definition(enchantment);
            if (has_card(player_state.hand, enchantment) &&
                can_pay(player_state, definition.cost)) {
                actions.push_back(
                    PriorityAction::cast_enchantment(enchantment));
            }
        }
    }

    const auto& bolt = card_definition(CardId::LightningBolt);
    if (has_card(player_state.hand, CardId::LightningBolt) &&
        can_pay(player_state, bolt.cost)) {
        for (std::size_t controller = 0; controller < state.players.size();
             ++controller) {
            actions.push_back(PriorityAction::cast_lightning_bolt(
                Target::player_target(controller)));
            for (const auto& creature : state.players[controller].creatures) {
                actions.push_back(PriorityAction::cast_lightning_bolt(
                    Target::creature_target(controller, creature.id)));
            }
        }
    }

    const auto& counterspell = card_definition(CardId::Counterspell);
    if (has_card(player_state.hand, CardId::Counterspell) &&
        can_pay(player_state, counterspell.cost)) {
        for (const auto& spell : state.stack) {
            if (spell.kind == StackObjectKind::Spell) {
                actions.push_back(
                    PriorityAction::cast_counterspell(spell.id));
            }
        }
    }

    if (can_pay(player_state, kMillstoneActivationCost)) {
        for (const auto& artifact : player_state.artifacts) {
            if (artifact.card != CardId::Millstone ||
                artifact.tapped) {
                continue;
            }
            for (std::size_t target = 0;
                 target < state.players.size(); ++target) {
                actions.push_back(PriorityAction::activate_millstone(
                    artifact.id, Target::player_target(target)));
            }
        }
    }

    return actions;
}

bool apply_priority_action(GameState& state, std::size_t player,
                           const PriorityAction& action,
                           bool sorcery_actions) {
    const auto actions =
        legal_priority_actions(state, player, sorcery_actions);
    if (!contains_action(actions, action)) {
        return false;
    }

    auto& player_state = state.players[player];
    switch (action.kind) {
    case PriorityActionKind::Pass:
        return true;

    case PriorityActionKind::PlayLand:
        if (!is_land(action.card) ||
            !remove_card(player_state.hand, action.card)) {
            return false;
        }
        player_state.lands.push_back({.card = action.card, .tapped = false});
        player_state.land_played_this_turn = true;
        ++state.stats[player].lands_played;
        return true;

    case PriorityActionKind::CastCreature: {
        const auto& definition = card_definition(action.card);
        if (definition.type != CardType::Creature) {
            return false;
        }
        if (!pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, action.card)) {
            return false;
        }
        state.stack.push_back({
            .id = state.next_stack_object_id++,
            .card = action.card,
            .controller = player,
            .target = std::nullopt,
            .spell_target = std::nullopt,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastSorcery: {
        const auto& definition = card_definition(action.card);
        if (definition.type != CardType::Sorcery ||
            !pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, action.card)) {
            return false;
        }
        state.stack.push_back({
            .id = state.next_stack_object_id++,
            .card = action.card,
            .controller = player,
            .target = std::nullopt,
            .spell_target = std::nullopt,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastArtifact: {
        const auto& definition = card_definition(action.card);
        if (definition.type != CardType::Artifact ||
            !pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, action.card)) {
            return false;
        }
        state.stack.push_back({
            .id = state.next_stack_object_id++,
            .card = action.card,
            .controller = player,
            .target = std::nullopt,
            .spell_target = std::nullopt,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastEnchantment: {
        const auto& definition = card_definition(action.card);
        if (definition.type != CardType::Enchantment ||
            !pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, action.card)) {
            return false;
        }
        state.stack.push_back({
            .id = state.next_stack_object_id++,
            .card = action.card,
            .controller = player,
            .target = std::nullopt,
            .spell_target = std::nullopt,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastLightningBolt: {
        if (!action.target.has_value()) {
            return false;
        }
        const auto& definition = card_definition(CardId::LightningBolt);
        if (!pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, CardId::LightningBolt)) {
            return false;
        }
        state.stack.push_back({
            .id = state.next_stack_object_id++,
            .card = CardId::LightningBolt,
            .controller = player,
            .target = action.target,
            .spell_target = std::nullopt,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastCounterspell: {
        const auto& definition = card_definition(CardId::Counterspell);
        if (!action.spell_target.has_value() ||
            !pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, CardId::Counterspell)) {
            return false;
        }
        state.stack.push_back({
            .id = state.next_stack_object_id++,
            .card = CardId::Counterspell,
            .controller = player,
            .target = std::nullopt,
            .spell_target = action.spell_target,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::ActivateMillstone: {
        if (!action.source_permanent.has_value() ||
            !action.target.has_value() ||
            action.target->creature.has_value()) {
            return false;
        }
        auto* millstone =
            find_artifact(player_state, *action.source_permanent);
        if (millstone == nullptr || millstone->tapped ||
            millstone->card != CardId::Millstone ||
            !pay_mana(player_state, kMillstoneActivationCost)) {
            return false;
        }
        millstone->tapped = true;
        state.stack.push_back({
            .kind = StackObjectKind::ActivatedAbility,
            .id = state.next_stack_object_id++,
            .card = CardId::Millstone,
            .controller = player,
            .target = action.target,
            .spell_target = std::nullopt,
        });
        return true;
    }
    }

    return false;
}

bool resolve_top_of_stack(GameState& state) {
    if (state.stack.empty()) {
        return false;
    }

    const StackObject spell = state.stack.back();
    state.stack.pop_back();
    auto& controller = state.players[spell.controller];
    const auto& definition = card_definition(spell.card);

    if (spell.kind == StackObjectKind::ActivatedAbility) {
        if (spell.card != CardId::Millstone ||
            !spell.target.has_value() ||
            spell.target->creature.has_value()) {
            return false;
        }
        auto& target = state.players[spell.target->player];
        for (int card = 0; card < 2 && !target.library.empty();
             ++card) {
            target.graveyard.push_back(target.library.back());
            target.library.pop_back();
            ++state.stats[spell.controller].cards_milled;
        }
        return true;
    }

    if (definition.type == CardType::Creature) {
        controller.creatures.push_back(
            {.id = state.next_permanent_id++,
             .card = spell.card,
             .tapped = false,
             .summoning_sick = true,
             .damage = 0});
        return true;
    }

    if (definition.type == CardType::Artifact) {
        controller.artifacts.push_back(
            {.id = state.next_permanent_id++,
             .card = spell.card,
             .tapped = false});
        return true;
    }

    if (definition.type == CardType::Enchantment) {
        controller.enchantments.push_back(spell.card);
        return true;
    }

    if (spell.card == CardId::LightningBolt) {
        if (spell.target.has_value()) {
            const Target& target = *spell.target;
            if (target.creature.has_value()) {
                auto* creature = find_creature(
                    state.players[target.player], *target.creature);
                if (creature != nullptr) {
                    creature->damage += definition.effect_damage;
                    remove_dead_creatures(state.players[target.player]);
                }
            } else {
                state.players[target.player].life -=
                    definition.effect_damage;
                if (target.player == opponent_of(spell.controller)) {
                    state.stats[spell.controller].damage_to_opponent +=
                        static_cast<std::size_t>(
                            definition.effect_damage);
                }
            }
        }
        controller.graveyard.push_back(spell.card);
        return true;
    }

    if (spell.card == CardId::Counterspell) {
        if (spell.spell_target.has_value()) {
            const auto target = std::find_if(
                state.stack.begin(), state.stack.end(),
                [&](const StackObject& candidate) {
                    return candidate.id == *spell.spell_target;
                });
            if (target != state.stack.end()) {
                state.players[target->controller].graveyard.push_back(
                    target->card);
                state.stack.erase(target);
                ++state.stats[spell.controller].spells_countered;
            }
        }
        controller.graveyard.push_back(spell.card);
        return true;
    }

    if (spell.card == CardId::Tsunami) {
        for (auto& player : state.players) {
            auto land = player.lands.begin();
            while (land != player.lands.end()) {
                if (land->card == CardId::Island) {
                    player.graveyard.push_back(land->card);
                    land = player.lands.erase(land);
                } else {
                    ++land;
                }
            }
        }
        controller.graveyard.push_back(spell.card);
        return true;
    }

    return false;
}

PriorityPassResult pass_priority(GameState& state,
                                 PriorityState& priority) {
    if (priority.player >= state.players.size()) {
        throw std::out_of_range("priority player must be 0 or 1");
    }

    ++priority.consecutive_passes;
    if (priority.consecutive_passes < 2) {
        priority.player = opponent_of(priority.player);
        return PriorityPassResult::Passed;
    }

    if (state.stack.empty()) {
        return PriorityPassResult::WindowEnded;
    }
    if (!resolve_top_of_stack(state)) {
        throw std::logic_error("failed to resolve the stack");
    }

    priority.player = state.active_player;
    priority.consecutive_passes = 0;
    return PriorityPassResult::StackObjectResolved;
}

bool resolve_combat(
    GameState& state, std::size_t attacking_player,
    const std::vector<PermanentId>& attackers,
    const std::vector<std::pair<PermanentId, PermanentId>>& blocks) {
    if (attacking_player >= state.players.size()) {
        return false;
    }
    const std::size_t defending_player = opponent_of(attacking_player);

    std::unordered_set<PermanentId> attacker_ids;
    for (const PermanentId attacker_id : attackers) {
        const auto* creature =
            find_creature(state.players[attacking_player], attacker_id);
        if (creature == nullptr || creature->tapped ||
            creature->summoning_sick ||
            !can_attack_through_moat(state, *creature) ||
            !attacker_ids.insert(attacker_id).second) {
            return false;
        }
    }

    std::unordered_set<PermanentId> blocker_ids;
    for (const auto& [attacker_id, blocker_id] : blocks) {
        if (!attacker_ids.contains(attacker_id)) {
            return false;
        }
        const auto* blocker =
            find_creature(state.players[defending_player], blocker_id);
        if (blocker == nullptr || blocker->tapped ||
            !blocker_ids.insert(blocker_id).second) {
            return false;
        }
    }

    std::unordered_map<PermanentId, std::vector<PermanentId>>
        blockers_by_attacker;
    for (const auto& [attacker_id, blocker_id] : blocks) {
        blockers_by_attacker[attacker_id].push_back(blocker_id);
    }

    for (const PermanentId attacker_id : attackers) {
        auto* attacker =
            find_creature(state.players[attacking_player], attacker_id);
        attacker->tapped = true;
        const auto& attacker_definition = card_definition(attacker->card);
        const auto blocker_group = blockers_by_attacker.find(attacker_id);

        if (blocker_group == blockers_by_attacker.end()) {
            state.players[defending_player].life -=
                attacker_definition.power;
            state.stats[attacking_player].damage_to_opponent +=
                static_cast<std::size_t>(attacker_definition.power);
            continue;
        }

        int attacker_damage = 0;
        for (const PermanentId blocker_id : blocker_group->second) {
            const auto* blocker =
                find_creature(state.players[defending_player], blocker_id);
            attacker_damage += card_definition(blocker->card).power;
        }
        attacker->damage += attacker_damage;

        int damage_remaining = attacker_definition.power;
        for (const PermanentId blocker_id : blocker_group->second) {
            auto* blocker =
                find_creature(state.players[defending_player], blocker_id);
            const int lethal_damage =
                std::max(0, card_definition(blocker->card).toughness -
                                blocker->damage);
            const int assigned_damage =
                std::min(damage_remaining, lethal_damage);
            blocker->damage += assigned_damage;
            damage_remaining -= assigned_damage;
        }
    }

    remove_dead_creatures(state.players[attacking_player]);
    remove_dead_creatures(state.players[defending_player]);
    return true;
}

void begin_turn(GameState& state, std::size_t player) {
    auto& player_state = state.players.at(player);
    player_state.land_played_this_turn = false;
    for (auto& land : player_state.lands) {
        land.tapped = false;
    }
    for (auto& creature : player_state.creatures) {
        creature.tapped = false;
        creature.summoning_sick = false;
    }
    for (auto& artifact : player_state.artifacts) {
        artifact.tapped = false;
    }
}

void cleanup_turn(GameState& state) {
    for (auto& player : state.players) {
        for (auto& creature : player.creatures) {
            creature.damage = 0;
        }
    }
}

Game::Game(std::vector<CardId> player_zero_deck,
           std::vector<CardId> player_one_deck, std::uint64_t seed,
           GameConfig config)
    : decks_({std::move(player_zero_deck), std::move(player_one_deck)}),
      random_(seed), config_(config) {
    if (config_.starting_player.has_value() &&
        *config_.starting_player >= state_.players.size()) {
        throw std::invalid_argument("starting player must be 0 or 1");
    }
    if (config_.max_turns == 0) {
        throw std::invalid_argument("maximum turns must be positive");
    }
    for (const auto& bot : config_.bots) {
        if ((bot.kind == BotKind::MonteCarlo ||
             bot.kind == BotKind::DeepMonteCarlo) &&
            bot.rollouts_per_action == 0) {
            throw std::invalid_argument(
                "Monte Carlo rollouts per action must be positive");
        }
    }
    for (std::size_t player = 0; player < config_.bots.size();
         ++player) {
        if (config_.bots[player].kind == BotKind::Learned) {
            const auto model =
                configured_learned_model(config_, player);
            if (!model) {
                throw std::invalid_argument(
                    "Learned bot requires a frozen trained model");
            }
            if (model->variant() !=
                config_.bots[player].learned_variant) {
                throw std::invalid_argument(
                    "Learned bot model does not match its variant");
            }
        }
    }
}

std::shared_ptr<const LearnedModel>
Game::learned_model_for(std::size_t player) const {
    return configured_learned_model(config_, player);
}

void Game::initialize() {
    state_ = GameState{};
    setup_result_.reset();

    if (config_.starting_player.has_value()) {
        state_.starting_player = *config_.starting_player;
    } else {
        std::uniform_int_distribution<std::size_t> choose_player(0, 1);
        state_.starting_player = choose_player(random_);
    }

    for (std::size_t player = 0; player < state_.players.size(); ++player) {
        state_.players[player].library = decks_[player];
        std::shuffle(state_.players[player].library.begin(),
                     state_.players[player].library.end(), random_);
    }

    for (int card = 0; card < 7; ++card) {
        for (std::size_t player = 0; player < state_.players.size(); ++player) {
            if (!draw_card(player)) {
                setup_result_ =
                    make_result(static_cast<int>(opponent_of(player)),
                                EndReason::EmptyLibrary);
                return;
            }
        }
    }
}

bool Game::draw_card(std::size_t player) {
    auto& player_state = state_.players[player];
    if (player_state.library.empty()) {
        return false;
    }
    player_state.hand.push_back(player_state.library.back());
    player_state.library.pop_back();
    ++state_.stats[player].cards_drawn;
    return true;
}

GameResult Game::make_result(int winner, EndReason reason) const {
    return {
        .winner = winner,
        .reason = reason,
        .turns = state_.turn_number,
        .starting_player = state_.starting_player,
        .ending_life = {
            state_.players[0].life,
            state_.players[1].life,
        },
        .player_stats = state_.stats,
        .bots = {
            config_.bots[0].kind,
            config_.bots[1].kind,
        },
    };
}

std::optional<GameResult> Game::life_total_result() const {
    const bool player_zero_lost = state_.players[0].life <= 0;
    const bool player_one_lost = state_.players[1].life <= 0;
    if (!player_zero_lost && !player_one_lost) {
        return std::nullopt;
    }

    int winner = -1;
    if (player_zero_lost != player_one_lost) {
        winner = player_zero_lost ? 1 : 0;
    }
    return make_result(winner, EndReason::LifeTotal);
}

std::optional<GameResult>
Game::play_priority_window(bool sorcery_actions, TurnPhase phase) {
    PriorityState priority = {
        .player = state_.active_player,
        .consecutive_passes = 0,
    };
    return continue_priority_window(
        sorcery_actions, phase, priority);
}

std::optional<GameResult>
Game::continue_priority_window(bool sorcery_actions,
                               TurnPhase phase,
                               PriorityState priority) {
    while (true) {
        const auto actions =
            legal_priority_actions(state_, priority.player,
                                   sorcery_actions);
        const PriorityAction action =
            choose_priority_action(actions, priority.player,
                                   sorcery_actions, phase,
                                   priority.consecutive_passes);

        if (action.kind == PriorityActionKind::Pass) {
            const PriorityPassResult pass =
                pass_priority(state_, priority);
            if (pass == PriorityPassResult::Passed) {
                continue;
            }
            if (pass == PriorityPassResult::WindowEnded) {
                return std::nullopt;
            }
            if (const auto result = life_total_result();
                result.has_value()) {
                return result;
            }
            continue;
        }

        if (!apply_priority_action(state_, priority.player, action,
                                   sorcery_actions)) {
            throw std::logic_error("bot policy selected an illegal action");
        }
        // The player who acted receives priority again.
        priority.consecutive_passes = 0;
    }
}

PriorityAction Game::choose_priority_action(
    const std::vector<PriorityAction>& actions, std::size_t player,
    bool sorcery_actions, TurnPhase phase,
    int consecutive_passes) {
    if (actions.empty()) {
        throw std::logic_error("priority window has no pass action");
    }
    if (trace_ != nullptr && actions.size() > 1) {
        const bool stack_choice = !state_.stack.empty();
        const bool activated_ability_choice = std::any_of(
            actions.begin(), actions.end(),
            [](const PriorityAction& action) {
                return action.kind ==
                       PriorityActionKind::ActivateMillstone;
            });
        if (stack_choice || activated_ability_choice) {
            trace_->push_back(state_);
        }
    }
    if (actions.size() == 1) {
        return actions.front();
    }

    ++state_.stats[player].decisions;
    const auto& bot = config_.bots[player];
    if (bot.kind == BotKind::Random) {
        std::uniform_int_distribution<std::size_t> choose_action(
            0, actions.size() - 1);
        const std::size_t chosen = choose_action(random_);
        if (config_.learned_policy_recorder) {
            record_uniform_policy_option(
                state_, config_,
                priority_policy_options(
                    state_, player, actions, sorcery_actions,
                    phase, consecutive_passes),
                player, chosen);
        }
        return actions[chosen];
    }
    if (bot.kind == BotKind::Handcrafted) {
        return choose_handcrafted_action(actions, player);
    }
    if (bot.kind == BotKind::Learned) {
        if (bot.learned_variant == LearnedVariant::UnifiedActor) {
            const auto options =
                priority_policy_options(
                    state_, player, actions, sorcery_actions,
                    phase, consecutive_passes);
            const bool collecting_generation =
                config_.learned_policy_recorder &&
                config_.learned_policy_recorder
                    ->generation_collection.has_value();
            if (collecting_generation) {
                const auto search_seed =
                    reserve_generation_search_seed(
                        config_, player,
                        LearnedDecisionKind::Priority);
                if (search_seed.has_value()) {
                    const auto& collection =
                        *config_.learned_policy_recorder
                             ->generation_collection;
                    const auto samples =
                        learned_priority_action_samples(
                            state_, decks_, player,
                            sorcery_actions, phase,
                            consecutive_passes, actions,
                            learned_model_for(player),
                            {
                                .seed = *search_seed,
                                .worlds = collection.worlds,
                                .rollouts_per_world =
                                    collection.rollouts_per_world,
                                .horizon_turns =
                                    collection.horizon_turns,
                                .continuation_variant =
                                    LearnedVariant::UnifiedActor,
                                .blend_shallow_prior = false,
                            });
                    const auto scores =
                        mean_generation_action_scores(samples);
                    const auto choice_seed =
                        next_generation_choice_seed(
                            config_, player,
                            LearnedDecisionKind::Priority);
                    if (!choice_seed.has_value()) {
                        throw std::logic_error(
                            "generation priority choice has no seed");
                    }
                    const std::size_t chosen =
                        choose_best_generation_score(
                            scores, *choice_seed);
                    add_generation_rollout_evaluations(
                        config_, player,
                        LearnedDecisionKind::Priority,
                        samples.rollout_evaluations);
                    state_.stats[player].monte_carlo_rollouts +=
                        samples.rollout_evaluations;
                    record_learned_policy_choice(
                        state_, config_, player,
                        LearnedDecisionKind::Priority,
                        encode_learned_policy_options(
                            state_, player, options),
                        chosen,
                        learned_soft_priority_target(scores));
                    return actions[chosen];
                }
                // Once the per-game cap is reached, continue with the
                // frozen parent head. The helper still uses a choice-domain
                // seed, but this unsearched root is not recorded.
                return actions[choose_learned_policy_option(
                    state_, config_, options, player, random_)];
            }
            if (bot.rollouts_per_action == 0) {
                return actions[choose_learned_policy_option(
                    state_, config_, options, player, random_)];
            }

            struct SampledWorld {
                GameState state;
                std::uint64_t continuation_seed = 0;
            };
            std::vector<SampledWorld> worlds;
            worlds.reserve(bot.rollouts_per_action);
            for (std::size_t rollout = 0;
                 rollout < bot.rollouts_per_action; ++rollout) {
                const std::uint64_t world_seed = random_();
                worlds.push_back({
                    .state = sample_determinization(
                        state_, decks_, player, world_seed),
                    .continuation_seed = random_(),
                });
            }

            std::vector<double> scores(actions.size(), 0.0);
            for (std::size_t action_index = 0;
                 action_index < actions.size(); ++action_index) {
                for (const auto& world : worlds) {
                    scores[action_index] +=
                        learned_information_set_action_score(
                            actions[action_index], player,
                            sorcery_actions, phase,
                            consecutive_passes,
                            world.state,
                            world.continuation_seed);
                }
            }
            state_.stats[player].monte_carlo_rollouts +=
                actions.size() * bot.rollouts_per_action;

            std::vector<double> average_scores = scores;
            for (double& score : average_scores) {
                score /= static_cast<double>(worlds.size());
            }
            const double best = *std::max_element(
                average_scores.begin(), average_scores.end());
            std::vector<std::size_t> best_actions;
            for (std::size_t index = 0;
                 index < average_scores.size(); ++index) {
                if (average_scores[index] == best) {
                    best_actions.push_back(index);
                }
            }
            std::uniform_int_distribution<std::size_t> break_tie(
                0, best_actions.size() - 1);
            const std::size_t chosen =
                best_actions[break_tie(random_)];
            if (config_.learned_policy_recorder) {
                record_learned_policy_choice(
                    state_, config_, player,
                    LearnedDecisionKind::Priority,
                    encode_learned_policy_options(
                        state_, player, options),
                    chosen,
                    learned_soft_priority_target(
                        average_scores));
            }
            return actions[chosen];
        }

        if (bot.exploration_rate > 0.0) {
            std::bernoulli_distribution explore(
                bot.exploration_rate);
            if (explore(random_)) {
                std::uniform_int_distribution<std::size_t>
                    choose_action(0, actions.size() - 1);
                return actions[choose_action(random_)];
            }
        }

        struct SampledWorld {
            GameState state;
            std::uint64_t continuation_seed = 0;
        };
        const bool root_search =
            bot.rollouts_per_action > 0 &&
            config_.learned_search_depth > 0;
        const std::size_t world_count =
            root_search ? bot.rollouts_per_action : 1;
        std::vector<SampledWorld> worlds;
        worlds.reserve(world_count);
        for (std::size_t rollout = 0; rollout < world_count;
             ++rollout) {
            const std::uint64_t world_seed = random_();
            worlds.push_back({
                .state = sample_determinization(
                    state_, decks_, player, world_seed),
                .continuation_seed = random_(),
            });
        }

        std::vector<double> scores(actions.size(), 0.0);
        for (std::size_t action_index = 0;
             action_index < actions.size(); ++action_index) {
            // The historical champion blended a one-ply value prior with
            // complete continuations. This prior never force-resolves hidden
            // state: its mean is evaluated over the same common worlds.
            for (const auto& world : worlds) {
                scores[action_index] +=
                    learned_value_shallow_action_score(
                    actions[action_index], player,
                    sorcery_actions, consecutive_passes,
                    world.state);
            }
            scores[action_index] /=
                static_cast<double>(worlds.size());
            if (root_search) {
                for (const auto& world : worlds) {
                    scores[action_index] +=
                        learned_value_search_action_score(
                            actions[action_index], player,
                            sorcery_actions, phase,
                            consecutive_passes,
                            world.state,
                            world.continuation_seed);
                }
                scores[action_index] /=
                    static_cast<double>(worlds.size() + 1);
            }
        }
        if (root_search) {
            state_.stats[player].monte_carlo_rollouts +=
                actions.size() * bot.rollouts_per_action;
        }

        const double best =
            *std::max_element(scores.begin(), scores.end());
        std::vector<std::size_t> best_actions;
        for (std::size_t index = 0; index < scores.size(); ++index) {
            if (scores[index] == best) {
                best_actions.push_back(index);
            }
        }
        std::uniform_int_distribution<std::size_t> break_tie(
            0, best_actions.size() - 1);
        return actions[best_actions[break_tie(random_)]];
    }

    std::vector<double> scores(actions.size(), 0.0);
    for (std::size_t action_index = 0; action_index < actions.size();
         ++action_index) {
        for (std::size_t rollout = 0;
             rollout < bot.rollouts_per_action; ++rollout) {
            scores[action_index] +=
                rollout_action(actions[action_index], player,
                               sorcery_actions, random_());
        }
    }
    state_.stats[player].monte_carlo_rollouts +=
        actions.size() * bot.rollouts_per_action;

    const double best_score =
        *std::max_element(scores.begin(), scores.end());
    std::vector<std::size_t> best_actions;
    for (std::size_t action_index = 0; action_index < scores.size();
         ++action_index) {
        if (scores[action_index] == best_score) {
            best_actions.push_back(action_index);
        }
    }
    std::uniform_int_distribution<std::size_t> break_tie(
        0, best_actions.size() - 1);
    return actions[best_actions[break_tie(random_)]];
}

double Game::learned_information_set_action_score(
    const PriorityAction& action, std::size_t player,
    bool sorcery_actions, TurnPhase phase,
    int consecutive_passes,
    const GameState& sampled_state, std::uint64_t seed) const {
    Game simulation = *this;
    simulation.state_ = sampled_state;
    simulation.random_.seed(seed);
    simulation.trace_ = nullptr;
    simulation.config_.learned_policy_recorder.reset();
    simulation.config_.bots = {
        BotConfig{
            .kind = BotKind::Learned,
            .learned_variant = LearnedVariant::UnifiedActor,
            .rollouts_per_action = 0,
            .exploration_rate =
                config_.bots[player].exploration_rate,
            .learned_model = learned_model_for(player),
        },
        BotConfig{
            .kind = BotKind::Learned,
            .learned_variant = LearnedVariant::UnifiedActor,
            .rollouts_per_action = 0,
            .exploration_rate =
                config_.bots[player].exploration_rate,
            .learned_model = learned_model_for(player),
        },
    };

    PriorityState priority = {
        .player = player,
        .consecutive_passes = consecutive_passes,
    };
    const auto result_score =
        [player](const GameResult& result) {
            return result.winner < 0
                       ? 0.5
                       : (result.winner ==
                                  static_cast<int>(player)
                              ? 1.0
                              : 0.0);
        };
    bool window_ended = false;
    if (action.kind == PriorityActionKind::Pass) {
        const PriorityPassResult pass =
            pass_priority(simulation.state_, priority);
        if (pass == PriorityPassResult::WindowEnded) {
            window_ended = true;
        }
        if (pass == PriorityPassResult::StackObjectResolved) {
            if (const auto result = simulation.life_total_result();
                result.has_value()) {
                return result_score(*result);
            }
        }
    } else {
        if (!apply_priority_action(
                simulation.state_, player, action,
                sorcery_actions)) {
            return 0.0;
        }
        priority = {
            .player = player,
            .consecutive_passes = 0,
        };
    }

    if (!window_ended) {
        if (const auto result =
                simulation.continue_priority_window(
                    sorcery_actions, phase, priority);
            result.has_value()) {
            return result_score(*result);
        }
    }

    const auto phase_result_score =
        [&](const std::optional<GameResult>& result)
        -> std::optional<double> {
        if (!result.has_value()) {
            return std::nullopt;
        }
        return result_score(*result);
    };
    switch (phase) {
    case TurnPhase::FirstMain:
        if (const auto score =
                phase_result_score(simulation.play_combat());
            score.has_value()) {
            return *score;
        }
        if (const auto score = phase_result_score(
                simulation.play_priority_window(
                    true, TurnPhase::SecondMain));
            score.has_value()) {
            return *score;
        }
        break;
    case TurnPhase::BeginCombat:
        if (const auto score = phase_result_score(
                simulation.play_combat_after_beginning());
            score.has_value()) {
            return *score;
        }
        if (const auto score = phase_result_score(
                simulation.play_priority_window(
                    true, TurnPhase::SecondMain));
            score.has_value()) {
            return *score;
        }
        break;
    case TurnPhase::EndCombat:
        if (const auto score = phase_result_score(
                simulation.play_priority_window(
                    true, TurnPhase::SecondMain));
            score.has_value()) {
            return *score;
        }
        break;
    case TurnPhase::SecondMain:
        break;
    case TurnPhase::DeclareAttackers:
    case TurnPhase::DeclareBlockers:
    case TurnPhase::DamageOrder:
        throw std::logic_error(
            "priority search started from a declaration phase");
    }

    cleanup_turn(simulation.state_);
    if (simulation.state_.turn_number >=
        simulation.config_.max_turns) {
        return result_score(
            simulation.make_result(-1, EndReason::TurnLimit));
    }
    const std::size_t next_turn =
        simulation.state_.turn_number + 1;
    simulation.state_.turn_number = next_turn;
    simulation.state_.active_player =
        (simulation.state_.starting_player + next_turn - 1) % 2;
    begin_turn(
        simulation.state_, simulation.state_.active_player);
    const bool starting_player_first_turn =
        next_turn == 1 &&
        simulation.state_.active_player ==
            simulation.state_.starting_player;
    if (!starting_player_first_turn &&
        !simulation.draw_card(simulation.state_.active_player)) {
        const GameResult result = simulation.make_result(
            static_cast<int>(opponent_of(
                simulation.state_.active_player)),
            EndReason::EmptyLibrary);
        return result_score(result);
    }
    return simulation.learned_model_for(player)->predict(
        learned_features(simulation.state_, player));
}

double Game::learned_value_shallow_action_score(
    const PriorityAction& action, std::size_t player,
    bool sorcery_actions, int consecutive_passes,
    const GameState& sampled_state) const {
    GameState successor = sampled_state;
    PriorityState priority = {
        .player = player,
        .consecutive_passes = consecutive_passes,
    };
    if (action.kind == PriorityActionKind::Pass) {
        pass_priority(successor, priority);
    } else if (!apply_priority_action(
                   successor, player, action, sorcery_actions)) {
        return -std::numeric_limits<double>::infinity();
    }

    const bool player_zero_lost = successor.players[0].life <= 0;
    const bool player_one_lost = successor.players[1].life <= 0;
    if (player_zero_lost || player_one_lost) {
        if (player_zero_lost == player_one_lost) {
            return 0.5;
        }
        const std::size_t winner = player_zero_lost ? 1 : 0;
        return winner == player ? 1.0 : 0.0;
    }
    return learned_model_for(player)->predict(
        learned_features(successor, player));
}

std::optional<GameResult>
Game::finish_turn_after_priority_phase(TurnPhase phase) {
    switch (phase) {
    case TurnPhase::FirstMain:
        if (const auto result = play_combat(); result.has_value()) {
            return result;
        }
        if (const auto result = play_priority_window(
                true, TurnPhase::SecondMain);
            result.has_value()) {
            return result;
        }
        break;
    case TurnPhase::BeginCombat:
        if (const auto result = play_combat_after_beginning();
            result.has_value()) {
            return result;
        }
        if (const auto result = play_priority_window(
                true, TurnPhase::SecondMain);
            result.has_value()) {
            return result;
        }
        break;
    case TurnPhase::EndCombat:
        if (const auto result = play_priority_window(
                true, TurnPhase::SecondMain);
            result.has_value()) {
            return result;
        }
        break;
    case TurnPhase::SecondMain:
        break;
    case TurnPhase::DeclareAttackers:
    case TurnPhase::DeclareBlockers:
    case TurnPhase::DamageOrder:
        throw std::logic_error(
            "priority search started from a declaration phase");
    }
    cleanup_turn(state_);
    return std::nullopt;
}

double Game::finish_learned_evaluation_horizon(
    std::size_t perspective, std::size_t horizon_turns) {
    if (perspective >= state_.players.size()) {
        throw std::out_of_range(
            "Learned evaluation perspective must be 0 or 1");
    }
    const auto model = learned_model_for(perspective);
    if (!model) {
        throw std::logic_error(
            "Learned evaluation has no frozen model");
    }
    const auto result_score =
        [perspective](const GameResult& result) {
            return result.winner < 0
                       ? 0.5
                       : (result.winner ==
                                  static_cast<int>(perspective)
                              ? 1.0
                              : 0.0);
        };
    const auto prepare_next_turn =
        [&]() -> std::optional<GameResult> {
            if (state_.turn_number ==
                std::numeric_limits<std::size_t>::max()) {
                throw std::overflow_error(
                    "Learned evaluation turn number overflow");
            }
            ++state_.turn_number;
            state_.active_player =
                (state_.starting_player + state_.turn_number - 1) %
                state_.players.size();
            begin_turn(state_, state_.active_player);
            const bool starting_player_first_turn =
                state_.turn_number == 1 &&
                state_.active_player == state_.starting_player;
            if (!starting_player_first_turn &&
                !draw_card(state_.active_player)) {
                return make_result(
                    static_cast<int>(
                        opponent_of(state_.active_player)),
                    EndReason::EmptyLibrary);
            }
            return std::nullopt;
        };
    const auto play_prepared_turn =
        [&]() -> std::optional<GameResult> {
            if (const auto result = play_priority_window(
                    true, TurnPhase::FirstMain);
                result.has_value()) {
                return result;
            }
            if (const auto result = play_combat();
                result.has_value()) {
                return result;
            }
            if (const auto result = play_priority_window(
                    true, TurnPhase::SecondMain);
                result.has_value()) {
                return result;
            }
            cleanup_turn(state_);
            return std::nullopt;
        };

    if (const auto result = prepare_next_turn();
        result.has_value()) {
        return result_score(*result);
    }
    if (horizon_turns == 0) {
        return model->predict(
            learned_features(state_, perspective));
    }
    for (std::size_t turn = 0; turn < horizon_turns; ++turn) {
        if (const auto result = play_prepared_turn();
            result.has_value()) {
            return result_score(*result);
        }
        if (turn + 1 < horizon_turns) {
            if (const auto result = prepare_next_turn();
                result.has_value()) {
                return result_score(*result);
            }
        }
    }
    return model->predict(learned_features(state_, perspective));
}

double Game::learned_value_search_action_score(
    const PriorityAction& action, std::size_t player,
    bool sorcery_actions, TurnPhase phase,
    int consecutive_passes,
    const GameState& sampled_state, std::uint64_t seed) const {
    const auto root_model = learned_model_for(player);
    Game simulation = *this;
    simulation.state_ = sampled_state;
    simulation.random_.seed(seed);
    simulation.trace_ = nullptr;
    simulation.config_.learned_policy_recorder.reset();
    simulation.config_.learned_search_depth = 0;
    simulation.config_.bots = {
        BotConfig{
            .kind = BotKind::Learned,
            .learned_variant =
                LearnedVariant::ValueSearchChampion,
            .rollouts_per_action = 0,
            .learned_model = root_model,
        },
        BotConfig{
            .kind = BotKind::Learned,
            .learned_variant =
                LearnedVariant::ValueSearchChampion,
            .rollouts_per_action = 0,
            .learned_model = root_model,
        },
    };

    const auto result_score =
        [player](const GameResult& result) {
            return result.winner < 0
                       ? 0.5
                       : (result.winner ==
                                  static_cast<int>(player)
                              ? 1.0
                              : 0.0);
        };
    PriorityState priority = {
        .player = player,
        .consecutive_passes = consecutive_passes,
    };
    bool window_ended = false;
    if (action.kind == PriorityActionKind::Pass) {
        const PriorityPassResult pass =
            pass_priority(simulation.state_, priority);
        window_ended =
            pass == PriorityPassResult::WindowEnded;
        if (pass == PriorityPassResult::StackObjectResolved) {
            if (const auto result = simulation.life_total_result();
                result.has_value()) {
                return result_score(*result);
            }
        }
    } else {
        if (!apply_priority_action(
                simulation.state_, player, action,
                sorcery_actions)) {
            return 0.0;
        }
        priority = {
            .player = player,
            .consecutive_passes = 0,
        };
    }

    if (!window_ended) {
        if (const auto result =
                simulation.continue_priority_window(
                    sorcery_actions, phase, priority);
            result.has_value()) {
            return result_score(*result);
        }
    }
    if (const auto result =
            simulation.finish_turn_after_priority_phase(phase);
        result.has_value()) {
        return result_score(*result);
    }

    constexpr std::size_t kSearchHorizonTurns = 4;
    simulation.config_.max_turns =
        std::min(simulation.config_.max_turns,
                 simulation.state_.turn_number +
                     kSearchHorizonTurns);
    const GameResult result =
        simulation.run_from_turn(
            simulation.state_.turn_number + 1);
    if (result.reason != EndReason::TurnLimit) {
        return result_score(result);
    }
    return root_model->predict(
        learned_features(simulation.state_, player));
}

PriorityAction Game::choose_handcrafted_action(
    const std::vector<PriorityAction>& actions, std::size_t player) {
    std::vector<double> scores;
    scores.reserve(actions.size());
    for (const auto& action : actions) {
        scores.push_back(handcrafted_action_score(action, player));
    }

    const double best_score =
        *std::max_element(scores.begin(), scores.end());
    std::vector<std::size_t> best_actions;
    for (std::size_t index = 0; index < scores.size(); ++index) {
        if (scores[index] == best_score) {
            best_actions.push_back(index);
        }
    }
    std::uniform_int_distribution<std::size_t> break_tie(
        0, best_actions.size() - 1);
    return actions[best_actions[break_tie(random_)]];
}

double Game::handcrafted_action_score(const PriorityAction& action,
                                      std::size_t player) const {
    const auto& player_state = state_.players[player];
    const std::size_t opponent = opponent_of(player);
    const auto& opponent_state = state_.players[opponent];

    switch (action.kind) {
    case PriorityActionKind::Pass:
        if (!state_.stack.empty() &&
            state_.stack.back().controller == player) {
            return 5'000.0;
        }
        return state_.stack.empty() ? -10.0 : 0.0;

    case PriorityActionKind::PlayLand:
        return 4'000.0;

    case PriorityActionKind::CastCreature: {
        double score = 1'200.0 + handcrafted_card_value(action.card);
        const bool holding_counterspell =
            has_card(player_state.hand, CardId::Counterspell);
        const int untapped_lands = static_cast<int>(std::count_if(
            player_state.lands.begin(), player_state.lands.end(),
            [](const LandPermanent& land) { return !land.tapped; }));
        const auto& cost = card_definition(action.card).cost;
        const int total_cost = cost.generic + cost.green + cost.red +
                               cost.blue + cost.white;
        if (holding_counterspell && untapped_lands < total_cost + 2) {
            score -= 1'500.0;
        }
        return score;
    }

    case PriorityActionKind::CastSorcery: {
        const auto count_islands = [](const PlayerState& state) {
            return std::count_if(
                state.lands.begin(), state.lands.end(),
                [](const LandPermanent& land) {
                    return land.card == CardId::Island;
                });
        };
        const auto enemy_islands = count_islands(opponent_state);
        const auto own_islands = count_islands(player_state);
        return 700.0 + 800.0 * static_cast<double>(enemy_islands) -
               800.0 * static_cast<double>(own_islands);
    }

    case PriorityActionKind::CastArtifact:
        return 1'500.0;

    case PriorityActionKind::CastEnchantment: {
        const double battlefield_swing =
            250.0 * static_cast<double>(opponent_state.creatures.size()) -
            150.0 * static_cast<double>(player_state.creatures.size());
        return 1'800.0 + battlefield_swing;
    }

    case PriorityActionKind::CastLightningBolt:
        if (!action.target.has_value()) {
            return -10'000.0;
        }
        if (!action.target->creature.has_value()) {
            if (action.target->player == player) {
                return -10'000.0;
            }
            if (opponent_state.life <= 3) {
                return 10'000.0;
            }
            return 900.0 +
                   10.0 * static_cast<double>(20 - opponent_state.life);
        } else {
            if (action.target->player == player) {
                return -10'000.0;
            }
            const auto target = std::find_if(
                opponent_state.creatures.begin(),
                opponent_state.creatures.end(),
                [&](const CreaturePermanent& creature) {
                    return creature.id == *action.target->creature;
                });
            if (target == opponent_state.creatures.end()) {
                return -10'000.0;
            }
            const auto& definition = card_definition(target->card);
            const bool lethal =
                target->damage + card_definition(CardId::LightningBolt)
                                     .effect_damage >=
                definition.toughness;
            return (lethal ? 2'000.0 : 500.0) +
                   handcrafted_card_value(target->card);
        }

    case PriorityActionKind::CastCounterspell: {
        if (!action.spell_target.has_value()) {
            return -10'000.0;
        }
        const auto target = std::find_if(
            state_.stack.begin(), state_.stack.end(),
            [&](const StackObject& object) {
                return object.id == *action.spell_target;
            });
        if (target == state_.stack.end() ||
            target->controller == player) {
            return -10'000.0;
        }
        return 3'000.0 + handcrafted_card_value(target->card);
    }

    case PriorityActionKind::ActivateMillstone:
        if (!action.target.has_value() ||
            action.target->player == player) {
            return -10'000.0;
        }
        if (opponent_state.library.size() <= 2) {
            return 10'000.0;
        }
        return 1'600.0 +
               static_cast<double>(40 - opponent_state.library.size()) *
                   10.0;
    }
    return -10'000.0;
}

double Game::rollout_action(const PriorityAction& action,
                            std::size_t player, bool sorcery_actions,
                            std::uint64_t seed) const {
    Game rollout = *this;
    rollout.random_.seed(seed);
    rollout.config_.bots = {
        BotConfig{.kind = BotKind::Random},
        BotConfig{.kind = BotKind::Random},
    };

    // The order of each library is hidden information. Re-randomizing it makes
    // each rollout a separate determinization instead of letting the bot peek
    // at the already-shuffled future.
    for (auto& player_state : rollout.state_.players) {
        std::shuffle(player_state.library.begin(),
                     player_state.library.end(), rollout.random_);
    }

    if (action.kind != PriorityActionKind::Pass &&
        !apply_priority_action(rollout.state_, player, action,
                               sorcery_actions)) {
        return -std::numeric_limits<double>::infinity();
    }

    // Resolve the candidate and anything already below it, then use a complete
    // random continuation from the following turn. This keeps rollout cost
    // bounded while the real game still uses normal stack priority.
    while (!rollout.state_.stack.empty()) {
        if (!resolve_top_of_stack(rollout.state_)) {
            throw std::logic_error("rollout failed to resolve the stack");
        }
        if (const auto result = rollout.life_total_result();
            result.has_value()) {
            if (result->winner < 0) {
                return 0.5;
            }
            return result->winner == static_cast<int>(player) ? 1.0 : 0.0;
        }
    }

    cleanup_turn(rollout.state_);
    const GameResult result =
        rollout.run_from_turn(rollout.state_.turn_number + 1);
    if (result.winner < 0) {
        return 0.5;
    }
    return result.winner == static_cast<int>(player) ? 1.0 : 0.0;
}

std::optional<GameResult> Game::play_combat() {
    if (const auto result = play_priority_window(
            false, TurnPhase::BeginCombat);
        result.has_value()) {
        return result;
    }
    return play_combat_after_beginning();
}

std::optional<GameResult> Game::play_combat_after_beginning() {
    auto& attacking_state = state_.players[state_.active_player];
    const std::size_t defending_player = opponent_of(state_.active_player);
    auto& defending_state = state_.players[defending_player];

    std::vector<PermanentId> attackers;
    const bool handcrafted_attacker =
        config_.bots[state_.active_player].kind == BotKind::Handcrafted;
    const bool learned_value_attacker =
        config_.bots[state_.active_player].kind == BotKind::Learned &&
        config_.bots[state_.active_player].learned_variant ==
            LearnedVariant::ValueSearchChampion;
    const bool learned_actor_attacker =
        config_.bots[state_.active_player].kind == BotKind::Learned &&
        config_.bots[state_.active_player].learned_variant ==
            LearnedVariant::UnifiedActor;
    if (handcrafted_attacker) {
        std::vector<const CreaturePermanent*> legal_attackers;
        int total_power = 0;
        for (const auto& creature : attacking_state.creatures) {
            if (!creature.tapped && !creature.summoning_sick &&
                can_attack_through_moat(state_, creature)) {
                legal_attackers.push_back(&creature);
                total_power += card_definition(creature.card).power;
            }
        }

        const bool attack_for_lethal =
            total_power >= defending_state.life;
        for (const auto* creature : legal_attackers) {
            bool favorable_attack = defending_state.creatures.empty();
            const auto& attacker = card_definition(creature->card);
            for (const auto& blocker : defending_state.creatures) {
                if (blocker.tapped) {
                    continue;
                }
                const auto& blocker_definition =
                    card_definition(blocker.card);
                if (attacker.power >= blocker_definition.toughness) {
                    favorable_attack = true;
                    break;
                }
            }
            if (attack_for_lethal || favorable_attack) {
                attackers.push_back(creature->id);
            }
        }
    } else if (learned_value_attacker) {
        std::vector<PermanentId> legal_attackers;
        for (const auto& creature : attacking_state.creatures) {
            if (!creature.tapped && !creature.summoning_sick &&
                can_attack_through_moat(state_, creature)) {
                legal_attackers.push_back(creature.id);
            }
        }
        const auto candidates =
            learned_value_attack_candidates(
                legal_attackers, random_);
        const auto model =
            learned_model_for(state_.active_player);
        const auto evaluation =
            score_learned_value_attack_sets(
                state_, state_.active_player, candidates, model,
                random_);
        attackers =
            candidates[evaluation.selected_candidate];
    } else if (learned_actor_attacker) {
        std::vector<PermanentId> legal_attackers;
        for (const auto& creature : attacking_state.creatures) {
            if (!creature.tapped && !creature.summoning_sick &&
                can_attack_through_moat(state_, creature)) {
                legal_attackers.push_back(creature.id);
            }
        }
        for (std::size_t index = 0;
             index < legal_attackers.size(); ++index) {
            const auto options = attack_policy_options(
                state_, state_.active_player, legal_attackers[index],
                attackers, legal_attackers.size() - index);
            std::size_t chosen = 0;
            const bool collecting_generation =
                config_.learned_policy_recorder &&
                config_.learned_policy_recorder
                    ->generation_collection.has_value();
            const auto search_seed =
                collecting_generation
                    ? reserve_generation_search_seed(
                          config_, state_.active_player,
                          LearnedDecisionKind::Attack)
                    : std::nullopt;
            if (search_seed.has_value()) {
                const auto& collection =
                    *config_.learned_policy_recorder
                         ->generation_collection;
                const std::vector<PermanentId> remaining(
                    legal_attackers.begin() +
                        static_cast<std::ptrdiff_t>(index + 1),
                    legal_attackers.end());
                const auto samples =
                    learned_binary_attack_samples(
                        state_, decks_, state_.active_player,
                        attackers, legal_attackers[index], remaining,
                        learned_model_for(state_.active_player),
                        {
                            .seed = *search_seed,
                            .worlds = collection.worlds,
                            .rollouts_per_world =
                                collection.rollouts_per_world,
                            .horizon_turns =
                                collection.horizon_turns,
                            .continuation_variant =
                                LearnedVariant::UnifiedActor,
                            .blend_shallow_prior = false,
                        });
                const auto scores =
                    mean_generation_action_scores(samples);
                const auto choice_seed =
                    next_generation_choice_seed(
                        config_, state_.active_player,
                        LearnedDecisionKind::Attack);
                if (!choice_seed.has_value()) {
                    throw std::logic_error(
                        "generation attack choice has no seed");
                }
                chosen = choose_best_generation_score(
                    scores, *choice_seed);
                add_generation_rollout_evaluations(
                    config_, state_.active_player,
                    LearnedDecisionKind::Attack,
                    samples.rollout_evaluations);
                state_.stats[state_.active_player]
                    .monte_carlo_rollouts +=
                    samples.rollout_evaluations;
                record_learned_policy_choice(
                    state_, config_, state_.active_player,
                    LearnedDecisionKind::Attack,
                    encode_learned_policy_options(
                        state_, state_.active_player, options),
                    chosen, learned_soft_priority_target(scores));
                if (chosen == 1) {
                    ++config_.learned_policy_recorder
                          ->generation_collection
                          ->attack_includes[state_.active_player];
                }
            } else {
                chosen = choose_learned_policy_option(
                    state_, config_, options,
                    state_.active_player, random_);
            }
            if (chosen == 1) {
                attackers.push_back(legal_attackers[index]);
            }
        }
    } else {
        std::uniform_int_distribution<int> attack_or_not(0, 1);
        std::vector<PermanentId> legal_attackers;
        for (const auto& creature : attacking_state.creatures) {
            if (!creature.tapped && !creature.summoning_sick &&
                can_attack_through_moat(state_, creature)) {
                legal_attackers.push_back(creature.id);
            }
        }
        for (std::size_t index = 0;
             index < legal_attackers.size(); ++index) {
            const std::size_t chosen =
                static_cast<std::size_t>(attack_or_not(random_));
            if (config_.learned_policy_recorder) {
                record_uniform_policy_option(
                    state_, config_,
                    attack_policy_options(
                        state_, state_.active_player,
                        legal_attackers[index], attackers,
                        legal_attackers.size() - index),
                    state_.active_player, chosen);
            }
            if (chosen == 1) {
                attackers.push_back(legal_attackers[index]);
            }
        }
    }

    return play_combat_with_attackers(std::move(attackers));
}

std::optional<GameResult> Game::play_combat_with_attackers(
    std::vector<PermanentId> attackers) {
    auto& attacking_state = state_.players[state_.active_player];
    const std::size_t defending_player =
        opponent_of(state_.active_player);
    auto& defending_state = state_.players[defending_player];
    const bool handcrafted_attacker =
        config_.bots[state_.active_player].kind ==
        BotKind::Handcrafted;
    const bool learned_actor_attacker =
        config_.bots[state_.active_player].kind ==
            BotKind::Learned &&
        config_.bots[state_.active_player].learned_variant ==
            LearnedVariant::UnifiedActor;

    if (attackers.empty()) {
        return play_priority_window(false, TurnPhase::EndCombat);
    }

    std::vector<PermanentId> available_blockers;
    for (const auto& creature : defending_state.creatures) {
        if (!creature.tapped) {
            available_blockers.push_back(creature.id);
        }
    }
    std::unordered_map<PermanentId, std::vector<PermanentId>>
        blockers_by_attacker;
    const bool handcrafted_defender =
        config_.bots[defending_player].kind == BotKind::Handcrafted;
    const bool learned_value_defender =
        config_.bots[defending_player].kind == BotKind::Learned &&
        config_.bots[defending_player].learned_variant ==
            LearnedVariant::ValueSearchChampion;
    const bool learned_actor_defender =
        config_.bots[defending_player].kind == BotKind::Learned &&
        config_.bots[defending_player].learned_variant ==
            LearnedVariant::UnifiedActor;
    if (handcrafted_defender) {
        std::vector<PermanentId> unassigned = available_blockers;
        std::vector<PermanentId> ordered_attackers = attackers;
        std::sort(ordered_attackers.begin(), ordered_attackers.end(),
                  [&](PermanentId left, PermanentId right) {
                      const auto* left_creature =
                          find_creature(attacking_state, left);
                      const auto* right_creature =
                          find_creature(attacking_state, right);
                      return card_definition(left_creature->card).power >
                             card_definition(right_creature->card).power;
                  });

        int incoming_power = 0;
        for (const PermanentId attacker_id : attackers) {
            const auto* attacker =
                find_creature(attacking_state, attacker_id);
            incoming_power += card_definition(attacker->card).power;
        }
        const bool must_block = incoming_power >= defending_state.life;

        for (const PermanentId attacker_id : ordered_attackers) {
            const auto* attacker =
                find_creature(attacking_state, attacker_id);
            const auto& attacker_definition =
                card_definition(attacker->card);
            auto best_blocker = unassigned.end();
            double best_score = must_block ? 1.0 : 0.0;

            for (auto blocker = unassigned.begin();
                 blocker != unassigned.end(); ++blocker) {
                const auto* blocker_creature =
                    find_creature(defending_state, *blocker);
                const auto& blocker_definition =
                    card_definition(blocker_creature->card);
                const bool kills_attacker =
                    blocker_definition.power >=
                    attacker_definition.toughness;
                const bool survives =
                    blocker_definition.toughness >
                    attacker_definition.power;
                double score =
                    20.0 * static_cast<double>(attacker_definition.power);
                if (kills_attacker) {
                    score += 1'000.0 +
                             handcrafted_card_value(attacker->card);
                }
                if (survives) {
                    score += 500.0;
                } else {
                    score -= handcrafted_card_value(
                        blocker_creature->card);
                }
                if (score > best_score) {
                    best_score = score;
                    best_blocker = blocker;
                }
            }
            if (best_blocker != unassigned.end()) {
                blockers_by_attacker[attacker_id].push_back(
                    *best_blocker);
                unassigned.erase(best_blocker);
            }
        }
    } else if (learned_value_defender) {
        const auto candidates =
            learned_value_block_candidates(
                attackers, available_blockers, random_, 512,
                96);
        double best_score =
            -std::numeric_limits<double>::infinity();
        std::vector<std::pair<PermanentId, PermanentId>>
            best_blocks;
        const auto model =
            learned_model_for(defending_player);
        for (const auto& candidate : candidates) {
            GameState successor = state_;
            if (!resolve_combat(
                    successor, state_.active_player,
                    attackers, candidate)) {
                throw std::logic_error(
                    "Learned Value sampled illegal blocks");
            }
            double score = 0.5;
            if (successor.players[defending_player].life <= 0) {
                score = 0.0;
            } else if (
                successor.players[state_.active_player].life <=
                0) {
                score = 1.0;
            } else {
                score = model->predict(
                    learned_features(
                        successor, defending_player));
            }
            if (score > best_score) {
                best_score = score;
                best_blocks = candidate;
            }
        }
        for (const auto& [attacker, blocker] : best_blocks) {
            blockers_by_attacker[attacker].push_back(
                blocker);
        }
    } else if (learned_actor_defender) {
        for (std::size_t index = 0;
             index < available_blockers.size(); ++index) {
            const auto options = block_policy_options(
                state_, state_.active_player,
                available_blockers[index], attackers,
                blockers_by_attacker,
                available_blockers.size() - index);
            const std::size_t chosen =
                choose_learned_policy_option(
                    state_, config_, options, defending_player,
                    random_);
            if (chosen != 0) {
                blockers_by_attacker[attackers[chosen - 1]]
                    .push_back(available_blockers[index]);
            }
        }
    } else {
        std::shuffle(available_blockers.begin(),
                     available_blockers.end(), random_);
        for (std::size_t index = 0;
             index < available_blockers.size(); ++index) {
            // Zero means no block; other values select an attacker. Multiple
            // blockers may legally select the same attacker.
            std::uniform_int_distribution<std::size_t> choose_block(
                0, attackers.size());
            const std::size_t choice = choose_block(random_);
            if (config_.learned_policy_recorder) {
                record_uniform_policy_option(
                    state_, config_,
                    block_policy_options(
                        state_, state_.active_player,
                        available_blockers[index], attackers,
                        blockers_by_attacker,
                        available_blockers.size() - index),
                    defending_player, choice);
            }
            if (choice != 0) {
                blockers_by_attacker[attackers[choice - 1]].push_back(
                    available_blockers[index]);
            }
        }
    }

    std::vector<std::pair<PermanentId, PermanentId>> blocks;
    for (const PermanentId attacker : attackers) {
        auto& blockers = blockers_by_attacker[attacker];
        // The attacking player chooses damage assignment order. The
        // handcrafted
        // policy orders the easiest creatures to kill first.
        if (handcrafted_attacker) {
            std::sort(blockers.begin(), blockers.end(),
                      [&](PermanentId left, PermanentId right) {
                          const auto* left_creature =
                              find_creature(defending_state, left);
                          const auto* right_creature =
                              find_creature(defending_state, right);
                          return card_definition(left_creature->card)
                                     .toughness <
                                 card_definition(right_creature->card)
                                     .toughness;
                      });
        } else if (learned_actor_attacker) {
            const std::vector<PermanentId> all_blockers = blockers;
            std::vector<PermanentId> remaining = blockers;
            std::vector<PermanentId> ordered;
            ordered.reserve(blockers.size());
            while (!remaining.empty()) {
                const auto options = damage_order_policy_options(
                    state_, state_.active_player, attacker,
                    all_blockers, remaining, ordered);
                const std::size_t chosen =
                    choose_learned_policy_option(
                        state_, config_, options,
                        state_.active_player, random_);
                ordered.push_back(remaining[chosen]);
                remaining.erase(
                    remaining.begin() +
                    static_cast<std::ptrdiff_t>(chosen));
            }
            blockers = std::move(ordered);
        } else if (config_.learned_policy_recorder) {
            const std::vector<PermanentId> all_blockers = blockers;
            std::vector<PermanentId> remaining = blockers;
            std::vector<PermanentId> ordered;
            ordered.reserve(blockers.size());
            while (!remaining.empty()) {
                std::uniform_int_distribution<std::size_t>
                    choose_next(0, remaining.size() - 1);
                const std::size_t chosen = choose_next(random_);
                record_uniform_policy_option(
                    state_, config_,
                    damage_order_policy_options(
                        state_, state_.active_player, attacker,
                        all_blockers, remaining, ordered),
                    state_.active_player, chosen);
                ordered.push_back(remaining[chosen]);
                remaining.erase(
                    remaining.begin() +
                    static_cast<std::ptrdiff_t>(chosen));
            }
            blockers = std::move(ordered);
        } else {
            std::shuffle(blockers.begin(), blockers.end(), random_);
        }
        for (const PermanentId blocker : blockers) {
            blocks.emplace_back(attacker, blocker);
        }
    }

    if (!resolve_combat(state_, state_.active_player, attackers, blocks)) {
        throw std::logic_error("random policy declared illegal combat");
    }
    if (const auto result = life_total_result(); result.has_value()) {
        return result;
    }
    return play_priority_window(false, TurnPhase::EndCombat);
}

GameResult Game::run() {
    initialize();
    if (setup_result_.has_value()) {
        return *setup_result_;
    }
    return run_from_turn(1);
}

GameResult Game::run_with_trace(std::vector<GameState>& trace) {
    trace.clear();
    trace_ = &trace;
    const GameResult result = run();
    trace_ = nullptr;
    return result;
}

GameResult Game::run_from_turn(std::size_t first_turn) {
    for (std::size_t turn = first_turn; turn <= config_.max_turns;
         ++turn) {
        state_.turn_number = turn;
        state_.active_player = (state_.starting_player + turn - 1) % 2;
        begin_turn(state_, state_.active_player);

        const bool starting_player_first_turn =
            turn == 1 && state_.active_player == state_.starting_player;
        if (!starting_player_first_turn &&
            !draw_card(state_.active_player)) {
            return make_result(
                static_cast<int>(opponent_of(state_.active_player)),
                EndReason::EmptyLibrary);
        }
        if (trace_ != nullptr) {
            trace_->push_back(state_);
        }

        if (const auto result = play_priority_window(
                true, TurnPhase::FirstMain);
            result.has_value()) {
            return *result;
        }
        if (const auto result = play_combat(); result.has_value()) {
            return *result;
        }
        if (const auto result = play_priority_window(
                true, TurnPhase::SecondMain);
            result.has_value()) {
            return *result;
        }
        cleanup_turn(state_);
    }

    return make_result(-1, EndReason::TurnLimit);
}

const GameState& Game::state() const {
    return state_;
}

double SimulationSummary::average_turns() const {
    return games == 0 ? 0.0
                      : static_cast<double>(total_turns) /
                            static_cast<double>(games);
}

namespace {

double percentage(std::size_t numerator, std::size_t denominator) {
    return denominator == 0
               ? 0.0
               : 100.0 * static_cast<double>(numerator) /
                     static_cast<double>(denominator);
}

double average(std::int64_t total, std::size_t count) {
    return count == 0
               ? 0.0
               : static_cast<double>(total) / static_cast<double>(count);
}

} // namespace

double DeckSimulationStats::win_rate() const {
    return percentage(wins, games);
}

double DeckSimulationStats::on_play_win_rate() const {
    return percentage(on_play_wins, on_play_games);
}

double DeckSimulationStats::on_draw_win_rate() const {
    return percentage(on_draw_wins, on_draw_games);
}

double DeckSimulationStats::average_ending_life() const {
    return average(total_ending_life, games);
}

double DeckSimulationStats::average_cards_drawn() const {
    return average(static_cast<std::int64_t>(total_cards_drawn), games);
}

double DeckSimulationStats::average_lands_played() const {
    return average(static_cast<std::int64_t>(total_lands_played), games);
}

double DeckSimulationStats::average_spells_cast() const {
    return average(static_cast<std::int64_t>(total_spells_cast), games);
}

double DeckSimulationStats::average_spells_countered() const {
    return average(static_cast<std::int64_t>(total_spells_countered),
                   games);
}

double DeckSimulationStats::average_damage_to_opponent() const {
    return average(static_cast<std::int64_t>(total_damage_to_opponent),
                   games);
}

double DeckSimulationStats::average_cards_milled() const {
    return average(static_cast<std::int64_t>(total_cards_milled), games);
}

double BotSimulationStats::win_rate() const {
    return percentage(wins, games);
}

double BotSimulationStats::average_decisions() const {
    return average(static_cast<std::int64_t>(total_decisions), games);
}

double BotSimulationStats::average_rollouts() const {
    return average(static_cast<std::int64_t>(total_rollouts), games);
}

double BotSimulationStats::average_rollouts_per_decision() const {
    return average(static_cast<std::int64_t>(total_rollouts),
                   total_decisions);
}

double BotMatchupStats::first_win_rate() const {
    return percentage(first_wins, games);
}

double BotMatchupStats::second_win_rate() const {
    return percentage(second_wins, games);
}

namespace {

std::array<BotMatchupStats, kBotMatchupCount>
empty_bot_matchups() {
    std::array<BotMatchupStats, kBotMatchupCount> matchups;
    std::size_t matchup = 0;
    for (std::size_t first = 0; first < kBotKindCount; ++first) {
        for (std::size_t second = first + 1;
             second < kBotKindCount; ++second) {
            matchups[matchup++] = {
                .first_bot = static_cast<BotKind>(first),
                .second_bot = static_cast<BotKind>(second),
            };
        }
    }
    return matchups;
}

std::size_t bot_matchup_index(BotKind first, BotKind second) {
    const auto low = std::min(static_cast<std::size_t>(first),
                              static_cast<std::size_t>(second));
    const auto high = std::max(static_cast<std::size_t>(first),
                               static_cast<std::size_t>(second));
    std::size_t matchup = 0;
    for (std::size_t candidate_low = 0;
         candidate_low < kBotKindCount; ++candidate_low) {
        for (std::size_t candidate_high = candidate_low + 1;
             candidate_high < kBotKindCount;
             ++candidate_high, ++matchup) {
            if (low == candidate_low && high == candidate_high) {
                return matchup;
            }
        }
    }
    throw std::logic_error("bot matchup requires two different bots");
}

void configure_bots(GameConfig& game_config, std::size_t game_index,
                    const TournamentConfig& tournament_config) {
    const BotConfig random = {
        .kind = BotKind::Random,
        .rollouts_per_action =
            tournament_config.monte_carlo_rollouts,
    };
    const BotConfig monte_carlo = {
        .kind = BotKind::MonteCarlo,
        .rollouts_per_action =
            tournament_config.monte_carlo_rollouts,
    };
    const BotConfig deep_monte_carlo = {
        .kind = BotKind::DeepMonteCarlo,
        .rollouts_per_action =
            tournament_config.deep_monte_carlo_rollouts,
    };
    const BotConfig handcrafted = {
        .kind = BotKind::Handcrafted,
        .rollouts_per_action = 1,
    };
    const BotConfig learned = {
        .kind = BotKind::Learned,
        .learned_variant =
            tournament_config.bot_field == BotField::Mixed
                ? LearnedVariant::ValueSearchChampion
                : tournament_config.learned_variant,
        .rollouts_per_action = 2,
        .training_games = tournament_config.learned_training_games,
    };

    switch (tournament_config.bot_field) {
    case BotField::Random:
        game_config.bots = {random, random};
        break;
    case BotField::MonteCarlo:
        game_config.bots = {monte_carlo, monte_carlo};
        break;
    case BotField::DeepMonteCarlo:
        game_config.bots = {deep_monte_carlo, deep_monte_carlo};
        break;
    case BotField::Handcrafted:
        game_config.bots = {handcrafted, handcrafted};
        break;
    case BotField::Learned:
        game_config.bots = {learned, learned};
        break;
    case BotField::Mixed:
        // The square rotation covers all ordered pairings, including mirrors.
        // Every bot has equal exposure in both seats.
        {
            const std::array<BotConfig, kBotKindCount> bots = {
                random,
                monte_carlo,
                deep_monte_carlo,
                handcrafted,
                learned,
            };
            const std::size_t pairing =
                game_index % (kBotKindCount * kBotKindCount);
            game_config.bots = {
                bots[pairing / kBotKindCount],
                bots[pairing % kBotKindCount],
            };
        }
        break;
    }
}

void record_deck_result(DeckSimulationStats& deck,
                        const GameResult& result,
                        std::size_t player) {
    ++deck.games;
    if (result.winner < 0) {
        ++deck.draws;
    } else if (result.winner == static_cast<int>(player)) {
        ++deck.wins;
    } else {
        ++deck.losses;
    }

    if (result.starting_player == player) {
        ++deck.on_play_games;
        if (result.winner == static_cast<int>(player)) {
            ++deck.on_play_wins;
        }
    } else {
        ++deck.on_draw_games;
        if (result.winner == static_cast<int>(player)) {
            ++deck.on_draw_wins;
        }
    }

    deck.total_ending_life += result.ending_life[player];
    deck.total_cards_drawn += result.player_stats[player].cards_drawn;
    deck.total_lands_played += result.player_stats[player].lands_played;
    deck.total_spells_cast += result.player_stats[player].spells_cast;
    deck.total_spells_countered +=
        result.player_stats[player].spells_countered;
    deck.total_damage_to_opponent +=
        result.player_stats[player].damage_to_opponent;
    deck.total_cards_milled += result.player_stats[player].cards_milled;
}

void record_bot_result(BotSimulationStats& bot,
                       const GameResult& result,
                       std::size_t player) {
    ++bot.games;
    if (result.winner < 0) {
        ++bot.draws;
    } else if (result.winner == static_cast<int>(player)) {
        ++bot.wins;
    } else {
        ++bot.losses;
    }
    bot.total_decisions += result.player_stats[player].decisions;
    bot.total_rollouts +=
        result.player_stats[player].monte_carlo_rollouts;
}

SimulationSummary run_matchup(const std::vector<CardId>& first_deck,
                              const std::vector<CardId>& second_deck,
                              std::size_t games, std::uint64_t seed,
                              GameConfig game_config,
                              std::optional<TournamentConfig>
                                  tournament_config = std::nullopt,
                              std::size_t schedule_offset = 0) {
    SimulationSummary summary;
    summary.games = games;
    summary.bot_matchups = empty_bot_matchups();
    std::mt19937_64 seed_generator(seed);

    std::vector<GameConfig> configs(games, game_config);
    std::vector<std::uint64_t> game_seeds(games);
    std::size_t current_matrix =
        std::numeric_limits<std::size_t>::max();
    std::uint64_t matrix_seed = 0;
    for (std::size_t game_index = 0; game_index < games; ++game_index) {
        if (tournament_config.has_value()) {
            configure_bots(configs[game_index],
                           schedule_offset + game_index,
                           *tournament_config);
        }
        if (tournament_config.has_value() &&
            tournament_config->bot_field == BotField::Mixed) {
            const std::size_t scheduled_game =
                schedule_offset + game_index;
            const std::size_t matrix =
                scheduled_game /
                (kBotKindCount * kBotKindCount);
            if (matrix != current_matrix) {
                current_matrix = matrix;
                matrix_seed = seed_generator();
            }
            game_seeds[game_index] = matrix_seed;
            if (!game_config.starting_player.has_value()) {
                const std::size_t pairing =
                    scheduled_game %
                    (kBotKindCount * kBotKindCount);
                configs[game_index].starting_player =
                    (pairing + matrix) % 2;
            }
        } else {
            game_seeds[game_index] = seed_generator();
        }
    }

    std::vector<GameResult> results(games);
    std::atomic_size_t next_game = 0;
    const std::size_t worker_count = std::min<std::size_t>(
        games, std::max(1U, std::thread::hardware_concurrency()));
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (std::size_t worker = 0; worker < worker_count; ++worker) {
        workers.emplace_back([&] {
            while (true) {
                const std::size_t game_index =
                    next_game.fetch_add(1, std::memory_order_relaxed);
                if (game_index >= games) {
                    return;
                }
                Game game(first_deck, second_deck,
                          game_seeds[game_index],
                          configs[game_index]);
                results[game_index] = game.run();
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    for (const GameResult& result : results) {
        summary.total_turns += result.turns;

        for (std::size_t player = 0; player < summary.decks.size();
             ++player) {
            auto& deck = summary.decks[player];
            const auto bot_index =
                static_cast<std::size_t>(result.bots[player]);
            record_deck_result(deck, result, player);
            record_deck_result(
                summary.deck_bots[player][bot_index], result, player);

            record_bot_result(summary.bots[bot_index], result, player);
        }

        if (result.bots[0] != result.bots[1]) {
            auto& matchup = summary.bot_matchups[bot_matchup_index(
                result.bots[0], result.bots[1])];
            ++matchup.games;
            if (result.winner < 0) {
                ++matchup.draws;
            } else if (result.bots[static_cast<std::size_t>(
                           result.winner)] == matchup.first_bot) {
                ++matchup.first_wins;
            } else {
                ++matchup.second_wins;
            }
        }

        if (result.winner < 0) {
            ++summary.draws;
        }

        switch (result.reason) {
        case EndReason::LifeTotal:
            ++summary.life_total_finishes;
            break;
        case EndReason::EmptyLibrary:
            ++summary.empty_library_finishes;
            break;
        case EndReason::TurnLimit:
            ++summary.turn_limit_draws;
            break;
        }
    }

    return summary;
}

std::vector<CardId> deck_cards(DeckId deck) {
    switch (deck) {
    case DeckId::Green:
        return green_alpha_deck();
    case DeckId::Red:
        return red_alpha_deck();
    case DeckId::Blue:
        return blue_alpha_deck();
    case DeckId::White:
        return white_control_deck();
    }
    throw std::out_of_range("unknown deck ID");
}

void merge_deck_stats(DeckSimulationStats& destination,
                      const DeckSimulationStats& source) {
    destination.games += source.games;
    destination.wins += source.wins;
    destination.losses += source.losses;
    destination.draws += source.draws;
    destination.on_play_games += source.on_play_games;
    destination.on_play_wins += source.on_play_wins;
    destination.on_draw_games += source.on_draw_games;
    destination.on_draw_wins += source.on_draw_wins;
    destination.total_ending_life += source.total_ending_life;
    destination.total_cards_drawn += source.total_cards_drawn;
    destination.total_lands_played += source.total_lands_played;
    destination.total_spells_cast += source.total_spells_cast;
    destination.total_spells_countered +=
        source.total_spells_countered;
    destination.total_damage_to_opponent +=
        source.total_damage_to_opponent;
    destination.total_cards_milled += source.total_cards_milled;
}

void merge_bot_stats(BotSimulationStats& destination,
                     const BotSimulationStats& source) {
    destination.games += source.games;
    destination.wins += source.wins;
    destination.losses += source.losses;
    destination.draws += source.draws;
    destination.total_decisions += source.total_decisions;
    destination.total_rollouts += source.total_rollouts;
}

void merge_bot_matchup_stats(BotMatchupStats& destination,
                             const BotMatchupStats& source) {
    if (destination.first_bot != source.first_bot ||
        destination.second_bot != source.second_bot) {
        throw std::logic_error("cannot merge different bot matchups");
    }
    destination.games += source.games;
    destination.first_wins += source.first_wins;
    destination.second_wins += source.second_wins;
    destination.draws += source.draws;
}

} // namespace

SimulationSummary run_simulation(std::size_t games, std::uint64_t seed,
                                 GameConfig game_config) {
    return run_matchup(green_alpha_deck(), red_alpha_deck(), games, seed,
                       game_config);
}

std::string_view deck_name(DeckId deck) {
    switch (deck) {
    case DeckId::Green:
        return "Green";
    case DeckId::Red:
        return "Red";
    case DeckId::Blue:
        return "Blue";
    case DeckId::White:
        return "White";
    }
    return "Unknown";
}

std::string_view deck_list(DeckId deck) {
    switch (deck) {
    case DeckId::Green:
        return "18 Forest / 9 Grizzly Bears / 12 Ironroot Treefolk / "
               "1 Tsunami";
    case DeckId::Red:
        return "18 Mountain / 10 Lightning Bolt / 12 Fire Elemental";
    case DeckId::Blue:
        return "18 Island / 14 Counterspell / 8 Water Elemental";
    case DeckId::White:
        return "22 Plains / 3 Millstone / 15 Moat";
    }
    return "Unknown";
}

std::string_view bot_name(BotKind bot) {
    switch (bot) {
    case BotKind::Random:
        return "Random";
    case BotKind::MonteCarlo:
        return "Monte Carlo";
    case BotKind::DeepMonteCarlo:
        return "Deep Monte Carlo";
    case BotKind::Handcrafted:
        return "Handcrafted Policy";
    case BotKind::Learned:
        return "Learned Value";
    }
    return "Unknown";
}

std::string_view learned_variant_name(LearnedVariant variant) {
    switch (variant) {
    case LearnedVariant::ValueSearchChampion:
        return "Learned Value";
    case LearnedVariant::UnifiedActor:
        return "Learned Actor";
    }
    return "Unknown Learned";
}

std::string bot_config_name(const BotConfig& bot) {
    if (bot.kind == BotKind::Learned) {
        return std::string(learned_variant_name(bot.learned_variant));
    }
    return std::string(bot_name(bot.kind));
}

double TournamentSummary::average_turns() const {
    return total_games == 0
               ? 0.0
               : static_cast<double>(total_turns) /
                     static_cast<double>(total_games);
}

bool LearnedDeckLiftSummary::complete() const {
    return std::all_of(
        decks.begin(), decks.end(),
        [](const DeckLiftComparison& deck) { return deck.available; });
}

bool LearnedDeckLiftSummary::learned_is_best_on_every_deck() const {
    return complete() &&
           std::all_of(
               decks.begin(), decks.end(),
               [](const DeckLiftComparison& deck) {
                   return deck.learned_is_best;
               });
}

LearnedDeckLiftSummary
compare_learned_deck_lifts(const TournamentSummary& summary) {
    constexpr std::array<BotKind, 3> comparison_bots = {
        BotKind::MonteCarlo,
        BotKind::DeepMonteCarlo,
        BotKind::Handcrafted,
    };
    constexpr double comparison_tolerance = 1.0e-12;
    const auto random_index =
        static_cast<std::size_t>(BotKind::Random);
    const auto learned_index =
        static_cast<std::size_t>(BotKind::Learned);

    LearnedDeckLiftSummary result;
    for (std::size_t deck = 0; deck < result.decks.size(); ++deck) {
        DeckLiftComparison& comparison = result.decks[deck];
        comparison.deck = static_cast<DeckId>(deck);
        const auto& random = summary.deck_bots[deck][random_index];
        const auto& learned = summary.deck_bots[deck][learned_index];
        if (random.games == 0 || learned.games == 0) {
            continue;
        }

        bool has_every_comparison = true;
        bool has_best_other = false;
        double best_other_rate = 0.0;
        for (const BotKind bot : comparison_bots) {
            const auto& stats = summary.deck_bots[deck][
                static_cast<std::size_t>(bot)];
            if (stats.games == 0) {
                has_every_comparison = false;
                break;
            }
            if (!has_best_other ||
                stats.win_rate() > best_other_rate) {
                has_best_other = true;
                best_other_rate = stats.win_rate();
                comparison.best_other = bot;
            }
        }
        if (!has_every_comparison || !has_best_other) {
            continue;
        }

        comparison.available = true;
        comparison.learned_lift =
            learned.win_rate() - random.win_rate();
        comparison.best_other_lift =
            best_other_rate - random.win_rate();
        comparison.learned_is_best =
            comparison.learned_lift + comparison_tolerance >=
            comparison.best_other_lift;
    }
    return result;
}

TournamentSummary run_tournament(std::size_t games_per_matchup,
                                 std::uint64_t seed,
                                 GameConfig game_config,
                                 TournamentConfig tournament_config) {
    const bool uses_monte_carlo =
        tournament_config.bot_field == BotField::MonteCarlo ||
        tournament_config.bot_field == BotField::Mixed;
    const bool uses_deep_monte_carlo =
        tournament_config.bot_field == BotField::DeepMonteCarlo ||
        tournament_config.bot_field == BotField::Mixed;
    const bool uses_learned =
        tournament_config.bot_field == BotField::Learned ||
        tournament_config.bot_field == BotField::Mixed;
    if (uses_monte_carlo &&
        tournament_config.monte_carlo_rollouts == 0) {
        throw std::invalid_argument(
            "Monte Carlo rollouts per action must be positive");
    }
    if (uses_deep_monte_carlo &&
        tournament_config.deep_monte_carlo_rollouts == 0) {
        throw std::invalid_argument(
            "deep Monte Carlo rollouts per action must be positive");
    }
    if (tournament_config.bot_field == BotField::Mixed &&
        tournament_config.deep_monte_carlo_rollouts <=
            tournament_config.monte_carlo_rollouts) {
        throw std::invalid_argument(
            "deep Monte Carlo must use more rollouts than Monte Carlo");
    }
    if (uses_learned &&
        tournament_config.learned_training_games == 0 &&
        !game_config.learned_model) {
        throw std::invalid_argument(
            "Learned bot training games must be positive");
    }
    if (uses_learned && !game_config.learned_model) {
        const LearnedVariant variant =
            tournament_config.bot_field == BotField::Mixed
                ? LearnedVariant::ValueSearchChampion
                : tournament_config.learned_variant;
        game_config.learned_model =
            variant == LearnedVariant::UnifiedActor
                ? train_learned_actor_model(
                      tournament_config.learned_training_games,
                      game_config.learned_training_seed)
                : train_learned_value_champion(
                      tournament_config.learned_training_games,
                      game_config.learned_training_seed);
    }

    TournamentSummary summary;
    summary.games_per_matchup = games_per_matchup;
    summary.total_games = games_per_matchup * summary.matchups.size();
    summary.learned_training_seed =
        game_config.learned_training_seed;
    summary.bot_matchups = empty_bot_matchups();

    constexpr std::array<std::pair<DeckId, DeckId>, 6> pairings = {{
        {DeckId::Green, DeckId::Red},
        {DeckId::Green, DeckId::Blue},
        {DeckId::Green, DeckId::White},
        {DeckId::Red, DeckId::Blue},
        {DeckId::Red, DeckId::White},
        {DeckId::Blue, DeckId::White},
    }};
    std::mt19937_64 seed_generator(seed);

    for (std::size_t index = 0; index < pairings.size(); ++index) {
        const auto [first, second] = pairings[index];
        SimulationSummary matchup =
            run_matchup(deck_cards(first), deck_cards(second),
                        games_per_matchup, seed_generator(), game_config,
                        tournament_config,
                        index * games_per_matchup);
        summary.matchups[index] = {
            .first_deck = first,
            .second_deck = second,
            .result = matchup,
        };

        merge_deck_stats(
            summary.decks[static_cast<std::size_t>(first)],
            matchup.decks[0]);
        merge_deck_stats(
            summary.decks[static_cast<std::size_t>(second)],
            matchup.decks[1]);
        for (std::size_t bot = 0; bot < summary.bots.size(); ++bot) {
            merge_bot_stats(summary.bots[bot], matchup.bots[bot]);
            merge_deck_stats(
                summary
                    .deck_bots[static_cast<std::size_t>(first)][bot],
                matchup.deck_bots[0][bot]);
            merge_deck_stats(
                summary
                    .deck_bots[static_cast<std::size_t>(second)][bot],
                matchup.deck_bots[1][bot]);
        }
        for (std::size_t bot_matchup = 0;
             bot_matchup < summary.bot_matchups.size();
             ++bot_matchup) {
            merge_bot_matchup_stats(
                summary.bot_matchups[bot_matchup],
                matchup.bot_matchups[bot_matchup]);
        }
        summary.draws += matchup.draws;
        summary.life_total_finishes += matchup.life_total_finishes;
        summary.empty_library_finishes +=
            matchup.empty_library_finishes;
        summary.turn_limit_draws += matchup.turn_limit_draws;
        summary.total_turns += matchup.total_turns;
    }

    return summary;
}

std::vector<double>
learned_observation(const GameState& state, std::size_t perspective) {
    if (perspective >= state.players.size()) {
        throw std::out_of_range(
            "Learned observation perspective must be 0 or 1");
    }
    const auto features = learned_features(state, perspective);
    return {features.begin(), features.end()};
}

std::vector<double> learned_priority_policy_features(
    const GameState& state, std::size_t perspective,
    const PriorityAction& action, bool sorcery_actions,
    TurnPhase phase, int consecutive_passes) {
    if (perspective >= state.players.size()) {
        throw std::out_of_range(
            "Learned policy perspective must be 0 or 1");
    }
    if (consecutive_passes < 0 || consecutive_passes > 1) {
        throw std::out_of_range(
            "Learned policy pass count must be zero or one");
    }
    const auto features = learned_policy_features(
        state, perspective,
        priority_policy_option(
            state, perspective, action, sorcery_actions,
            phase, consecutive_passes));
    return {features.begin(), features.end()};
}

namespace {

void validate_learned_model(
    const std::shared_ptr<const LearnedModel>& model,
    std::optional<LearnedVariant> required_variant = std::nullopt) {
    if (!model) {
        throw std::invalid_argument(
            "Learned evaluation requires a frozen model");
    }
    if (required_variant.has_value() &&
        model->variant() != *required_variant) {
        throw std::invalid_argument(
            "Learned evaluation model does not match its variant");
    }
}

void validate_priority_candidates(
    const GameState& state, std::size_t player,
    bool sorcery_actions, TurnPhase phase, int consecutive_passes,
    const std::vector<PriorityAction>& candidates) {
    if (player >= state.players.size()) {
        throw std::out_of_range(
            "Learned priority player must be 0 or 1");
    }
    if (consecutive_passes < 0 || consecutive_passes > 1) {
        throw std::out_of_range(
            "Learned priority pass count must be zero or one");
    }
    const bool main_phase =
        phase == TurnPhase::FirstMain ||
        phase == TurnPhase::SecondMain;
    if (main_phase != sorcery_actions) {
        throw std::invalid_argument(
            "priority phase and sorcery-action context disagree");
    }
    if (phase == TurnPhase::DeclareAttackers ||
        phase == TurnPhase::DeclareBlockers ||
        phase == TurnPhase::DamageOrder) {
        throw std::invalid_argument(
            "priority evaluation requires a supported priority phase");
    }
    if (candidates.empty()) {
        throw std::invalid_argument(
            "priority evaluation requires at least one candidate");
    }

    const auto legal =
        legal_priority_actions(state, player, sorcery_actions);
    std::vector<PriorityAction> seen;
    seen.reserve(candidates.size());
    for (const PriorityAction& candidate : candidates) {
        if (!contains_action(legal, candidate)) {
            throw std::invalid_argument(
                "priority candidate is not legal in the root state");
        }
        if (contains_action(seen, candidate)) {
            throw std::invalid_argument(
                "priority candidates must be unique");
        }
        seen.push_back(candidate);
    }
}

std::vector<PermanentId> legal_attackers(
    const GameState& state, std::size_t attacking_player) {
    if (attacking_player >= state.players.size()) {
        throw std::out_of_range(
            "attacking player must be 0 or 1");
    }
    std::vector<PermanentId> result;
    for (const CreaturePermanent& creature :
         state.players[attacking_player].creatures) {
        if (!creature.tapped && !creature.summoning_sick &&
            can_attack_through_moat(state, creature)) {
            result.push_back(creature.id);
        }
    }
    return result;
}

std::vector<PermanentId> validate_binary_attack_context(
    const GameState& state, std::size_t attacking_player,
    const std::vector<PermanentId>& selected_attackers,
    PermanentId subject,
    const std::vector<PermanentId>& remaining_attackers) {
    if (attacking_player >= state.players.size()) {
        throw std::out_of_range(
            "attacking player must be 0 or 1");
    }
    if (state.active_player != attacking_player) {
        throw std::invalid_argument(
            "binary attack evaluation requires the active player");
    }
    if (!state.stack.empty()) {
        throw std::invalid_argument(
            "binary attack evaluation requires an empty stack");
    }

    const std::vector<PermanentId> legal =
        legal_attackers(state, attacking_player);
    const auto subject_position =
        std::find(legal.begin(), legal.end(), subject);
    if (subject_position == legal.end()) {
        throw std::invalid_argument(
            "binary attack subject cannot legally attack");
    }
    const std::size_t subject_index =
        static_cast<std::size_t>(
            std::distance(legal.begin(), subject_position));

    std::unordered_set<PermanentId> supplied;
    std::size_t prior_position = 0;
    bool have_prior_position = false;
    for (const PermanentId selected : selected_attackers) {
        const auto position =
            std::find(legal.begin(), subject_position, selected);
        if (position == subject_position ||
            !supplied.insert(selected).second) {
            throw std::invalid_argument(
                "selected attackers must be a unique legal prefix");
        }
        const std::size_t index =
            static_cast<std::size_t>(
                std::distance(legal.begin(), position));
        if (have_prior_position && index <= prior_position) {
            throw std::invalid_argument(
                "selected attackers are outside deployed order");
        }
        prior_position = index;
        have_prior_position = true;
    }
    if (!supplied.insert(subject).second) {
        throw std::invalid_argument(
            "binary attack subject appears in the selected prefix");
    }

    const std::vector<PermanentId> expected_remaining(
        legal.begin() +
            static_cast<std::ptrdiff_t>(subject_index + 1),
        legal.end());
    if (remaining_attackers != expected_remaining) {
        throw std::invalid_argument(
            "remaining attackers must be the complete deployed suffix");
    }
    for (const PermanentId remaining : remaining_attackers) {
        if (!supplied.insert(remaining).second) {
            throw std::invalid_argument(
                "binary attack context contains a duplicate creature");
        }
    }
    return legal;
}

} // namespace

std::vector<double> learned_actor_priority_logits(
    const GameState& state, std::size_t player,
    bool sorcery_actions, TurnPhase phase, int consecutive_passes,
    const std::vector<PriorityAction>& candidates,
    std::shared_ptr<const LearnedModel> model) {
    validate_learned_model(
        model, LearnedVariant::UnifiedActor);
    validate_priority_candidates(
        state, player, sorcery_actions, phase,
        consecutive_passes, candidates);
    const auto encoded = encode_learned_policy_options(
        state, player,
        priority_policy_options(
            state, player, candidates, sorcery_actions,
            phase, consecutive_passes));
    std::vector<double> logits;
    logits.reserve(encoded.size());
    for (const auto& option : encoded) {
        logits.push_back(model->policy_logit(
            option,
            static_cast<std::size_t>(
                LearnedDecisionKind::Priority)));
    }
    return logits;
}

std::array<double, 2> learned_actor_binary_attack_logits(
    const GameState& state, std::size_t attacking_player,
    const std::vector<PermanentId>& selected_attackers,
    PermanentId subject,
    const std::vector<PermanentId>& remaining_attackers,
    std::shared_ptr<const LearnedModel> model) {
    validate_learned_model(
        model, LearnedVariant::UnifiedActor);
    validate_binary_attack_context(
        state, attacking_player, selected_attackers, subject,
        remaining_attackers);
    const auto encoded = encode_learned_policy_options(
        state, attacking_player,
        attack_policy_options(
            state, attacking_player, subject,
            selected_attackers, remaining_attackers.size() + 1));
    return {
        model->policy_logit(
            encoded[0],
            static_cast<std::size_t>(
                LearnedDecisionKind::Attack)),
        model->policy_logit(
            encoded[1],
            static_cast<std::size_t>(
                LearnedDecisionKind::Attack)),
    };
}

double learned_critic_value(
    const GameState& state, std::size_t perspective,
    std::shared_ptr<const LearnedModel> model) {
    validate_learned_model(model);
    if (perspective >= state.players.size()) {
        throw std::out_of_range(
            "Learned critic perspective must be 0 or 1");
    }
    return model->predict(learned_features(state, perspective));
}

LearnedValueAttackSetScores learned_value_attack_set_scores(
    const GameState& state, std::size_t attacking_player,
    const std::vector<std::vector<PermanentId>>& candidates,
    std::shared_ptr<const LearnedModel> model, std::uint64_t seed) {
    std::mt19937_64 random(seed);
    return score_learned_value_attack_sets(
        state, attacking_player, candidates, model, random);
}

std::vector<double> handcrafted_priority_scores(
    const GameState& state, std::size_t player,
    const std::vector<PriorityAction>& candidates) {
    if (player >= state.players.size()) {
        throw std::out_of_range(
            "Handcrafted priority player must be 0 or 1");
    }
    if (candidates.empty()) {
        throw std::invalid_argument(
            "Handcrafted priority scoring requires candidates");
    }
    std::vector<PriorityAction> seen;
    seen.reserve(candidates.size());
    for (const PriorityAction& candidate : candidates) {
        if (contains_action(seen, candidate)) {
            throw std::invalid_argument(
                "Handcrafted priority candidates must be unique");
        }
        seen.push_back(candidate);
    }

    Game evaluator({}, {}, 0);
    evaluator.state_ = state;
    std::vector<double> scores;
    scores.reserve(candidates.size());
    for (const PriorityAction& candidate : candidates) {
        scores.push_back(
            evaluator.handcrafted_action_score(candidate, player));
    }
    return scores;
}

std::array<double, 2> handcrafted_binary_attack_scores(
    const GameState& state, std::size_t attacking_player,
    const std::vector<PermanentId>& selected_attackers,
    PermanentId subject,
    const std::vector<PermanentId>& remaining_attackers) {
    const std::vector<PermanentId> legal =
        validate_binary_attack_context(
            state, attacking_player, selected_attackers, subject,
            remaining_attackers);
    const std::size_t defending_player =
        opponent_of(attacking_player);
    const PlayerState& attacking_state =
        state.players[attacking_player];
    const PlayerState& defending_state =
        state.players[defending_player];

    int total_power = 0;
    for (const PermanentId attacker : legal) {
        const auto* creature =
            find_creature_for_policy(attacking_state, attacker);
        total_power += card_definition(creature->card).power;
    }
    bool favorable_attack = defending_state.creatures.empty();
    const auto* subject_creature =
        find_creature_for_policy(attacking_state, subject);
    const auto& subject_definition =
        card_definition(subject_creature->card);
    for (const CreaturePermanent& blocker :
         defending_state.creatures) {
        if (!blocker.tapped &&
            subject_definition.power >=
                card_definition(blocker.card).toughness) {
            favorable_attack = true;
            break;
        }
    }
    const bool include =
        total_power >= defending_state.life || favorable_attack;
    return include ? std::array<double, 2>{0.0, 1.0}
                   : std::array<double, 2>{1.0, 0.0};
}

std::vector<double>
learned_soft_priority_target(const std::vector<double>& scores) {
    if (scores.empty()) {
        return {};
    }
    if (!std::all_of(scores.begin(), scores.end(),
                     [](double score) {
                         return std::isfinite(score);
                     })) {
        throw std::invalid_argument(
            "priority teacher scores must be finite");
    }
    constexpr double temperature = 0.10;
    constexpr double teacher_weight = 0.90;
    const double maximum =
        *std::max_element(scores.begin(), scores.end());
    std::vector<double> targets;
    targets.reserve(scores.size());
    double total = 0.0;
    for (const double score : scores) {
        targets.push_back(
            std::exp((score - maximum) / temperature));
        total += targets.back();
    }
    const double uniform =
        (1.0 - teacher_weight) /
        static_cast<double>(scores.size());
    for (double& target : targets) {
        target =
            teacher_weight * target / total + uniform;
    }
    return targets;
}

std::shared_ptr<const LearnedModel>
train_learned_actor_model(std::size_t training_games,
                          std::uint64_t seed) {
    if (training_games == 0) {
        throw std::invalid_argument(
            "Learned model training games must be positive");
    }

    std::mt19937_64 random(seed);
    std::uniform_int_distribution<std::size_t> choose_deck(0, 3);
    const auto choose_distinct_decks = [&] {
        const std::size_t first = choose_deck(random);
        std::size_t second = choose_deck(random);
        while (second == first) {
            second = choose_deck(random);
        }
        return std::pair{
            static_cast<DeckId>(first),
            static_cast<DeckId>(second),
        };
    };

    constexpr std::size_t kEnsembleMembers = 2;
    std::array<std::shared_ptr<LearnedModel>, kEnsembleMembers>
        members;
    for (std::size_t member = 0; member < members.size(); ++member) {
        members[member] = std::make_shared<LearnedModel>(
            seed ^ (0x4D4F44454C000000ULL + member),
            LearnedVariant::UnifiedActor);
    }

    std::vector<LearnedModel::TrainingExample> examples;
    examples.reserve(training_games * 120);
    const auto add_trace =
        [&](const std::vector<GameState>& trace,
            const GameResult& result,
            std::vector<LearnedModel::TrainingExample>& destination) {
        for (const auto& state : trace) {
            for (std::size_t perspective = 0; perspective < 2;
                 ++perspective) {
                double target = 0.5;
                if (result.winner >= 0) {
                    const double discounted_outcome =
                        0.5 * std::pow(
                                  0.985,
                                  static_cast<double>(result.turns));
                    target =
                        result.winner ==
                                static_cast<int>(perspective)
                            ? 0.5 + discounted_outcome
                            : 0.5 - discounted_outcome;
                }
                destination.emplace_back(
                    learned_features(state, perspective), target);
            }
        }
    };

    for (std::size_t game_index = 0; game_index < training_games;
         ++game_index) {
        const auto [first_deck, second_deck] =
            choose_distinct_decks();
        Game game(deck_cards(first_deck), deck_cards(second_deck),
                  random());
        std::vector<GameState> trace;
        const GameResult result = game.run_with_trace(trace);
        add_trace(trace, result, examples);
    }
    {
        std::array<std::thread, kEnsembleMembers> trainers;
        for (std::size_t member = 0; member < members.size();
             ++member) {
            trainers[member] = std::thread([&, member] {
                members[member]->train(
                    examples, 8, 0.015,
                    seed ^ (0x545241494E000000ULL + member));
            });
        }
        for (auto& trainer : trainers) {
            trainer.join();
        }
    }

    std::vector<std::shared_ptr<const LearnedModel>> ensemble_members;
    ensemble_members.reserve(members.size());
    for (const auto& member : members) {
        ensemble_members.push_back(member);
    }
    auto model = std::make_shared<LearnedModel>(
        std::move(ensemble_members),
        seed ^ 0x534841524544504FULL,
        LearnedVariant::UnifiedActor);

    std::vector<LearnedModel::PolicyTrainingExample> policy_batch;
    const auto append_policy_game =
        [&](LearnedPolicyRecorder& recorder,
            const GameResult& result) {
        constexpr std::size_t kDecisionKinds =
            LearnedModel::kPolicyDecisionCount;
        constexpr std::size_t kMaxStepsPerActorKind = 24;
        const auto has_training_target =
            [](const LearnedPolicyRecorder::Step& step) {
                if (step.kind != LearnedDecisionKind::Priority) {
                    return true;
                }
                return step.target_probabilities.size() ==
                       step.options.size();
            };
        std::array<std::array<std::size_t, kDecisionKinds>, 2>
            totals{};
        for (const auto& step : recorder.steps) {
            if (!has_training_target(step)) {
                continue;
            }
            ++totals[step.actor][static_cast<std::size_t>(
                step.kind)];
        }

        std::array<std::array<std::size_t, kDecisionKinds>, 2>
            seen{};
        std::array<std::array<std::size_t, kDecisionKinds>, 2>
            retained{};
        for (const auto& step : recorder.steps) {
            if (!has_training_target(step)) {
                continue;
            }
            const std::size_t kind =
                static_cast<std::size_t>(step.kind);
            const std::size_t ordinal = seen[step.actor][kind]++;
            const std::size_t total = totals[step.actor][kind];
            if (total <= kMaxStepsPerActorKind ||
                ((ordinal + 1) * kMaxStepsPerActorKind / total !=
                 ordinal * kMaxStepsPerActorKind / total)) {
                ++retained[step.actor][kind];
            }
        }

        seen = {};
        for (auto& step : recorder.steps) {
            if (!has_training_target(step)) {
                continue;
            }
            const std::size_t kind =
                static_cast<std::size_t>(step.kind);
            const std::size_t ordinal = seen[step.actor][kind]++;
            const std::size_t total = totals[step.actor][kind];
            const bool retain =
                total <= kMaxStepsPerActorKind ||
                ((ordinal + 1) * kMaxStepsPerActorKind / total !=
                 ordinal * kMaxStepsPerActorKind / total);
            if (!retain) {
                continue;
            }

            double outcome = 0.0;
            if (result.winner >= 0) {
                outcome =
                    result.winner == static_cast<int>(step.actor)
                        ? 1.0
                        : -1.0;
            }
            const double critic =
                2.0 * step.critic_baseline - 1.0;
            const bool priority =
                step.kind == LearnedDecisionKind::Priority;
            policy_batch.push_back({
                .options = std::move(step.options),
                .target_probabilities =
                    std::move(step.target_probabilities),
                .chosen = step.chosen,
                .decision_kind = kind,
                .advantage =
                    priority
                        ? 0.0
                        : std::clamp(
                              outcome - critic, -1.0, 1.0),
                .weight =
                    1.0 /
                    static_cast<double>(
                        std::max<std::size_t>(
                            1, retained[step.actor][kind])),
            });
        }
    };

    // The critic is fit once from random-play traces, then frozen throughout
    // actor collection. Each actor generation is likewise frozen while its
    // common-world Priority teacher and on-policy combat data are collected.
    constexpr std::size_t kPolicyGenerations = 4;
    for (std::size_t generation = 0;
         generation < kPolicyGenerations; ++generation) {
        const std::size_t generation_games =
            std::max<std::size_t>(1, training_games / 2);
        policy_batch.clear();
        for (std::size_t game_index = 0;
             game_index < generation_games; ++game_index) {
            GameConfig config;
            config.learned_model = model;
            config.learned_policy_recorder =
                std::make_shared<LearnedPolicyRecorder>();
            config.bots = {
                BotConfig{
                    .kind = BotKind::Learned,
                    .learned_variant =
                        LearnedVariant::UnifiedActor,
                    .rollouts_per_action = 2,
                    .exploration_rate = 1.0,
                },
                BotConfig{
                    .kind = BotKind::Learned,
                    .learned_variant =
                        LearnedVariant::UnifiedActor,
                    .rollouts_per_action = 2,
                    .exploration_rate = 1.0,
                },
            };
            const auto [first_deck, second_deck] =
                choose_distinct_decks();
            Game game(deck_cards(first_deck), deck_cards(second_deck),
                      random(), config);
            const GameResult result = game.run();
            append_policy_game(
                *config.learned_policy_recorder, result);
        }
        model->train_policy(
            policy_batch, 1, 0.001,
            seed ^ (0x504F4C4943590000ULL + generation));
    }

    return model;
}

LearnedActorGenerationResult train_learned_actor_generation(
    std::shared_ptr<const LearnedModel> parent,
    std::uint64_t root_seed,
    LearnedActorGenerationConfig config) {
    if (!parent ||
        parent->variant() != LearnedVariant::UnifiedActor) {
        throw std::invalid_argument(
            "actor generation requires a frozen Unified Actor parent");
    }
    if (config.search_worlds == 0 ||
        config.search_worlds > 4096 ||
        config.rollouts_per_world == 0 ||
        config.rollouts_per_world > 256 ||
        config.horizon_turns > 128 ||
        config.max_roots_per_seat_kind == 0 ||
        config.generation == 0 ||
        !std::isfinite(config.td_lambda) ||
        config.td_lambda < 0.0 ||
        config.td_lambda > 1.0 ||
        config.critic_epochs == 0 ||
        !std::isfinite(config.critic_learning_rate) ||
        config.critic_learning_rate <= 0.0 ||
        config.policy_epochs == 0 ||
        !std::isfinite(config.policy_learning_rate) ||
        config.policy_learning_rate <= 0.0) {
        throw std::invalid_argument(
            "invalid Learned Actor generation configuration");
    }

    LearnedActorGenerationResult output;
    auto& report = output.report;
    report.root_seed = root_seed;
    report.generation = config.generation;
    report.parent_fingerprint =
        learned_model_fingerprint(parent);
    report.games.reserve(
        learned_iteration::kBalancedScheduleGames);

    std::vector<LearnedCriticTrainingExample>
        generation_critic_examples;
    std::vector<LearnedPolicyTrainingExample>
        generation_policy_examples;
    double minimum_target_sum =
        std::numeric_limits<double>::infinity();
    double maximum_target_sum =
        -std::numeric_limits<double>::infinity();

    const auto schedule = learned_iteration::balanced_schedule(
        root_seed, config.generation);
    for (const auto& scheduled : schedule) {
        auto recorder =
            std::make_shared<LearnedPolicyRecorder>();
        recorder->generation_collection =
            LearnedPolicyRecorder::GenerationCollection{
                .root_seed = root_seed,
                .generation = config.generation,
                .schedule_index = scheduled.schedule_index,
                .worlds = config.search_worlds,
                .rollouts_per_world =
                    config.rollouts_per_world,
                .horizon_turns = config.horizon_turns,
                .max_roots_per_seat_kind =
                    config.max_roots_per_seat_kind,
            };

        GameConfig game_config;
        game_config.starting_player =
            scheduled.starting_player;
        game_config.learned_model = parent;
        game_config.learned_search_depth = 0;
        game_config.learned_policy_recorder = recorder;
        game_config.bots = {
            BotConfig{
                .kind = BotKind::Learned,
                .learned_variant =
                    LearnedVariant::UnifiedActor,
                .rollouts_per_action = 0,
                .exploration_rate = 0.0,
                .learned_model = parent,
            },
            BotConfig{
                .kind = BotKind::Learned,
                .learned_variant =
                    LearnedVariant::UnifiedActor,
                .rollouts_per_action = 0,
                .exploration_rate = 0.0,
                .learned_model = parent,
            },
        };

        Game game(
            deck_cards(scheduled.seat_decks[0]),
            deck_cards(scheduled.seat_decks[1]),
            scheduled.seed, game_config);
        const GameResult result = game.run();
        const auto& collection =
            *recorder->generation_collection;

        LearnedActorGenerationGameReport game_report{
            .schedule_index = scheduled.schedule_index,
            .pairing_index = scheduled.pairing_index,
            .seat_decks = scheduled.seat_decks,
            .starting_player = result.starting_player,
            .game_seed = scheduled.seed,
            .winner = result.winner,
            .priority_roots_by_seat = {
                collection.searched_roots[0][0],
                collection.searched_roots[1][0],
            },
            .attack_roots_by_seat = {
                collection.searched_roots[0][1],
                collection.searched_roots[1][1],
            },
            .attack_includes_by_seat =
                collection.attack_includes,
        };
        for (std::size_t player = 0; player < 2; ++player) {
            game_report.priority_rollout_evaluations +=
                collection.rollout_evaluations[player][0];
            game_report.attack_rollout_evaluations +=
                collection.rollout_evaluations[player][1];
        }

        std::array<std::array<std::size_t, 2>, 2>
            policy_counts{};
        for (const auto& step : recorder->steps) {
            const auto kind_index =
                generation_kind_index(step.kind);
            if (!kind_index.has_value() ||
                step.target_probabilities.size() !=
                    step.options.size()) {
                throw std::logic_error(
                    "generation recorder retained an invalid step");
            }
            ++policy_counts[step.actor][*kind_index];
        }

        for (const auto& step : recorder->steps) {
            const std::size_t kind_index =
                *generation_kind_index(step.kind);
            const std::size_t count =
                policy_counts[step.actor][kind_index];
            if (count == 0) {
                throw std::logic_error(
                    "generation policy normalization count is zero");
            }
            std::vector<std::vector<double>> options;
            options.reserve(step.options.size());
            for (const auto& encoded : step.options) {
                options.emplace_back(
                    encoded.begin(), encoded.end());
            }
            double target_sum = 0.0;
            for (const double target :
                 step.target_probabilities) {
                target_sum += target;
            }
            minimum_target_sum =
                std::min(minimum_target_sum, target_sum);
            maximum_target_sum =
                std::max(maximum_target_sum, target_sum);
            const double weight =
                1.0 / static_cast<double>(count);
            generation_policy_examples.push_back({
                .options = std::move(options),
                .target_probabilities =
                    step.target_probabilities,
                .decision_kind = step.kind,
                .weight = weight,
            });
            if (kind_index == 0) {
                ++report.priority_policy_examples;
                game_report.priority_policy_weight_sums
                    [step.actor] += weight;
            } else {
                ++report.attack_policy_examples;
                game_report.attack_policy_weight_sums
                    [step.actor] += weight;
            }
        }

        for (std::size_t player = 0; player < 2; ++player) {
            std::vector<const LearnedPolicyRecorder::Step*>
                chronological;
            for (const auto& step : recorder->steps) {
                if (step.actor != player) {
                    continue;
                }
                if (!chronological.empty() &&
                    chronological.back()->critic_features ==
                        step.critic_features) {
                    ++report.deduplicated_critic_observations;
                    continue;
                }
                chronological.push_back(&step);
            }
            std::vector<double> baselines;
            baselines.reserve(chronological.size());
            for (const auto* step : chronological) {
                baselines.push_back(step->critic_baseline);
            }
            const auto targets =
                learned_iteration::td_lambda_targets(
                    baselines,
                    learned_iteration::
                        terminal_value_for_perspective(
                            result.winner, player),
                    config.td_lambda);
            for (std::size_t index = 0;
                 index < chronological.size(); ++index) {
                generation_critic_examples.push_back({
                    .features = std::vector<double>(
                        chronological[index]
                            ->critic_features.begin(),
                        chronological[index]
                            ->critic_features.end()),
                    .target = targets[index],
                });
            }
        }

        for (const std::size_t roots :
             game_report.priority_roots_by_seat) {
            report.priority_roots += roots;
        }
        for (const std::size_t roots :
             game_report.attack_roots_by_seat) {
            report.attack_roots += roots;
        }
        report.priority_rollout_evaluations +=
            game_report.priority_rollout_evaluations;
        report.attack_rollout_evaluations +=
            game_report.attack_rollout_evaluations;
        report.games.push_back(std::move(game_report));
    }

    if (generation_policy_examples.empty() ||
        generation_critic_examples.empty()) {
        throw std::logic_error(
            "balanced actor generation collected no training data");
    }
    report.minimum_policy_target_sum = minimum_target_sum;
    report.maximum_policy_target_sum = maximum_target_sum;
    report.critic_examples =
        generation_critic_examples.size();

    learned_iteration::ReplayWindow<
        LearnedCriticTrainingExample>
        critic_replay;
    learned_iteration::ReplayWindow<
        LearnedPolicyTrainingExample>
        policy_replay;
    critic_replay.append_generation(
        config.generation,
        std::move(generation_critic_examples));
    policy_replay.append_generation(
        config.generation,
        std::move(generation_policy_examples));
    if (critic_replay.generation_count() !=
        policy_replay.generation_count()) {
        throw std::logic_error(
            "generation replay shards became misaligned");
    }
    report.replay_generations =
        critic_replay.generation_count();

    std::vector<LearnedCriticTrainingExample> critic_fit;
    std::vector<LearnedPolicyTrainingExample> policy_fit;
    critic_fit.reserve(critic_replay.example_count());
    policy_fit.reserve(policy_replay.example_count());
    critic_replay.for_each(
        [&](std::uint64_t,
            const LearnedCriticTrainingExample& example) {
            critic_fit.push_back(example);
        });
    policy_replay.for_each(
        [&](std::uint64_t,
            const LearnedPolicyTrainingExample& example) {
            policy_fit.push_back(example);
        });

    const auto parent_fit_snapshot =
        evaluate_learned_actor_fit_snapshot(
            parent, critic_fit, policy_fit);
    output.model = update_learned_actor_model(
        parent, critic_fit, policy_fit,
        {
            .critic_epochs = config.critic_epochs,
            .critic_learning_rate =
                config.critic_learning_rate,
            .critic_seed =
                learned_iteration::derive_seed(
                    root_seed,
                    learned_iteration::SeedDomain::CriticFit,
                    config.generation, 0),
            .policy_epochs = config.policy_epochs,
            .policy_learning_rate =
                config.policy_learning_rate,
            .policy_seed =
                learned_iteration::derive_seed(
                    root_seed,
                    learned_iteration::SeedDomain::PolicyFit,
                    config.generation, 0),
        });
    const auto candidate_fit_snapshot =
        evaluate_learned_actor_fit_snapshot(
            output.model, critic_fit, policy_fit);
    report.fit = combine_actor_fit_snapshots(
        parent_fit_snapshot, candidate_fit_snapshot);
    report.candidate_fingerprint =
        learned_model_fingerprint(output.model);
    if (learned_model_fingerprint(parent) !=
        report.parent_fingerprint) {
        throw std::logic_error(
            "actor generation mutated its frozen parent");
    }
    if (report.candidate_fingerprint ==
        report.parent_fingerprint) {
        throw std::logic_error(
            "actor generation did not update the candidate");
    }
    return output;
}

std::shared_ptr<const LearnedModel>
train_learned_value_champion(std::size_t training_games,
                             std::uint64_t seed) {
    if (training_games == 0) {
        throw std::invalid_argument(
            "Learned Value training games must be positive");
    }

    std::mt19937_64 random(seed);
    std::uniform_int_distribution<std::size_t> choose_deck(0, 3);
    const auto choose_distinct_decks = [&] {
        const std::size_t first = choose_deck(random);
        std::size_t second = choose_deck(random);
        while (second == first) {
            second = choose_deck(random);
        }
        return std::pair{
            static_cast<DeckId>(first),
            static_cast<DeckId>(second),
        };
    };
    const auto add_trace =
        [](const std::vector<GameState>& trace,
           const GameResult& result,
           std::vector<LearnedModel::TrainingExample>& destination) {
            for (const auto& state : trace) {
                for (std::size_t perspective = 0;
                     perspective < 2; ++perspective) {
                    double target = 0.5;
                    if (result.winner >= 0) {
                        const double discounted_outcome =
                            0.5 * std::pow(
                                      0.985,
                                      static_cast<double>(
                                          result.turns));
                        target =
                            result.winner ==
                                    static_cast<int>(perspective)
                                ? 0.5 + discounted_outcome
                                : 0.5 - discounted_outcome;
                    }
                    destination.emplace_back(
                        learned_features(state, perspective),
                        target);
                }
            }
        };

    std::vector<LearnedModel::TrainingExample> examples;
    examples.reserve(training_games * 120);
    for (std::size_t game_index = 0;
         game_index < training_games; ++game_index) {
        const auto [first_deck, second_deck] =
            choose_distinct_decks();
        Game game(deck_cards(first_deck),
                  deck_cards(second_deck), random());
        std::vector<GameState> trace;
        const GameResult result = game.run_with_trace(trace);
        add_trace(trace, result, examples);
    }

    constexpr std::size_t kEnsembleMembers = 2;
    std::array<std::shared_ptr<LearnedModel>, kEnsembleMembers>
        members;
    for (std::size_t member = 0; member < members.size();
         ++member) {
        members[member] = std::make_shared<LearnedModel>(
            seed ^ (0x4D4F44454C000000ULL + member),
            LearnedVariant::ValueSearchChampion);
    }
    const auto train_members =
        [&](std::size_t epochs, double learning_rate,
            std::uint64_t training_tag) {
            std::array<std::thread, kEnsembleMembers> trainers;
            for (std::size_t member = 0;
                 member < members.size(); ++member) {
                trainers[member] = std::thread([&, member] {
                    members[member]->train(
                        examples, epochs, learning_rate,
                        seed ^ (training_tag + member));
                });
            }
            for (auto& trainer : trainers) {
                trainer.join();
            }
        };
    const auto make_ensemble = [&] {
        std::vector<std::shared_ptr<const LearnedModel>>
            ensemble_members;
        ensemble_members.reserve(members.size());
        for (const auto& member : members) {
            ensemble_members.push_back(member);
        }
        return std::make_shared<LearnedModel>(
            std::move(ensemble_members),
            seed ^ 0x56414C5545534541ULL,
            LearnedVariant::ValueSearchChampion);
    };

    train_members(8, 0.015, 0x545241494E000000ULL);
    std::shared_ptr<const LearnedModel> model =
        make_ensemble();

    // Value-only fitted self-play: neither the unified actor nor
    // Handcrafted contributes actions or labels.
    for (std::size_t generation = 0; generation < 2;
         ++generation) {
        std::vector<LearnedModel::TrainingExample>
            self_play_examples;
        const std::size_t generation_games =
            std::max<std::size_t>(1, training_games / 2);
        self_play_examples.reserve(generation_games * 60);
        for (std::size_t game_index = 0;
             game_index < generation_games; ++game_index) {
            GameConfig config;
            config.learned_model = model;
            config.learned_search_depth = 0;
            config.bots = {
                BotConfig{
                    .kind = BotKind::Learned,
                    .learned_variant =
                        LearnedVariant::ValueSearchChampion,
                    .rollouts_per_action = 0,
                    .exploration_rate =
                        generation == 0 ? 0.10 : 0.05,
                },
                BotConfig{
                    .kind = BotKind::Learned,
                    .learned_variant =
                        LearnedVariant::ValueSearchChampion,
                    .rollouts_per_action = 0,
                    .exploration_rate =
                        generation == 0 ? 0.10 : 0.05,
                },
            };
            const auto [first_deck, second_deck] =
                choose_distinct_decks();
            Game game(deck_cards(first_deck),
                      deck_cards(second_deck), random(), config);
            std::vector<GameState> trace;
            const GameResult result =
                game.run_with_trace(trace);
            add_trace(trace, result, self_play_examples);
        }
        examples.insert(
            examples.end(), self_play_examples.begin(),
            self_play_examples.end());
        train_members(
            3, 0.006,
            0x53454C4600000000ULL +
                0x100ULL * generation);
        model = make_ensemble();
    }
    return model;
}

static LearnedValueG8Result train_learned_value_g8_recipe(
    std::size_t training_games, std::uint64_t seed,
    LearnedValueG8Recipe recipe) {
    if (training_games == 0) {
        throw std::invalid_argument(
            "Learned Value G8 training games must be positive");
    }
    const std::size_t generation_games =
        std::max<std::size_t>(1, training_games / 4);
    if (recipe == LearnedValueG8Recipe::LateMix50 &&
        generation_games % 2 != 0) {
        throw std::invalid_argument(
            "Learned Value G8 Late-Mix50 requires an even "
            "self-play game count per generation");
    }

    std::mt19937_64 random(seed);
    std::uniform_int_distribution<std::size_t> choose_deck(0, 3);
    const auto choose_distinct_decks = [&] {
        const std::size_t first = choose_deck(random);
        std::size_t second = choose_deck(random);
        while (second == first) {
            second = choose_deck(random);
        }
        return std::pair{
            static_cast<DeckId>(first),
            static_cast<DeckId>(second),
        };
    };
    const auto terminal_target =
        [](const GameResult& result,
           std::size_t perspective) {
            if (result.winner < 0) {
                return 0.5;
            }
            const double discounted_outcome =
                0.5 * std::pow(
                          0.985,
                          static_cast<double>(result.turns));
            return result.winner ==
                           static_cast<int>(perspective)
                       ? 0.5 + discounted_outcome
                       : 0.5 - discounted_outcome;
        };
    const auto add_terminal_trace =
        [&](const std::vector<GameState>& trace,
            const GameResult& result,
            std::vector<LearnedModel::TrainingExample>&
                destination) {
            for (const auto& state : trace) {
                for (std::size_t perspective = 0;
                     perspective < 2; ++perspective) {
                    destination.emplace_back(
                        learned_features(state, perspective),
                        terminal_target(result, perspective));
                }
            }
        };
    const auto add_bootstrap_trace =
        [&](const std::vector<GameState>& trace,
            const GameResult& result,
            const std::shared_ptr<const LearnedModel>& parent,
            std::vector<LearnedModel::TrainingExample>&
                destination) {
            std::array<std::vector<double>, 2>
                parent_values;
            std::array<std::vector<double>, 2> targets;
            for (std::size_t perspective = 0;
                 perspective < 2; ++perspective) {
                parent_values[perspective].reserve(
                    trace.size());
                for (const auto& state : trace) {
                    parent_values[perspective].push_back(
                        parent->predict(
                            learned_features(
                                state, perspective)));
                }
                targets[perspective] =
                    learned_iteration::
                        four_state_bootstrap_targets(
                            parent_values[perspective],
                            terminal_target(
                                result, perspective));
            }
            // State-major/perspective-minor order intentionally matches the
            // legacy and independently reviewed training corpus.
            for (std::size_t index = 0;
                 index < trace.size(); ++index) {
                for (std::size_t perspective = 0;
                     perspective < 2; ++perspective) {
                    destination.emplace_back(
                        learned_features(
                            trace[index], perspective),
                        targets[perspective][index]);
                }
            }
        };

    std::vector<LearnedModel::TrainingExample>
        anchor_examples;
    anchor_examples.reserve(training_games * 120);
    for (std::size_t game_index = 0;
         game_index < training_games; ++game_index) {
        const auto [first_deck, second_deck] =
            choose_distinct_decks();
        Game game(
            deck_cards(first_deck), deck_cards(second_deck),
            random());
        std::vector<GameState> trace;
        const GameResult result = game.run_with_trace(trace);
        add_terminal_trace(
            trace, result, anchor_examples);
    }

    constexpr std::size_t kEnsembleMembers = 2;
    std::array<std::shared_ptr<LearnedModel>, kEnsembleMembers>
        base_members;
    for (std::size_t member = 0;
         member < base_members.size(); ++member) {
        base_members[member] =
            std::make_shared<LearnedModel>(
                seed ^
                    (0x4D4F44454C000000ULL + member),
                LearnedVariant::ValueSearchChampion);
    }
    {
        std::array<std::thread, kEnsembleMembers> trainers;
        for (std::size_t member = 0;
             member < base_members.size(); ++member) {
            trainers[member] = std::thread([&, member] {
                base_members[member]->train(
                    anchor_examples, 8, 0.015,
                    seed ^
                        (0x545241494E000000ULL + member));
            });
        }
        for (auto& trainer : trainers) {
            trainer.join();
        }
    }
    std::vector<std::shared_ptr<const LearnedModel>>
        base_ensemble;
    base_ensemble.reserve(base_members.size());
    for (const auto& member : base_members) {
        base_ensemble.push_back(member);
    }
    std::shared_ptr<const LearnedModel> model =
        std::make_shared<LearnedModel>(
            std::move(base_ensemble),
            seed ^ 0x56414C5545534541ULL,
            LearnedVariant::ValueSearchChampion);

    LearnedValueG8Result output;
    output.report.recipe = recipe;
    output.report.training_games = training_games;
    output.report.root_seed = seed;
    output.report.base_examples = anchor_examples.size();
    output.report.base_fingerprint =
        learned_model_fingerprint(model);
    output.report.generations.reserve(
        kLearnedValueG8Generations);
    output.checkpoints.reserve(
        kLearnedValueG8Generations + 1);
    output.checkpoints.push_back(model);

    learned_iteration::ReplayWindow<
        LearnedModel::TrainingExample>
        replay;
    for (std::size_t generation_index = 0;
         generation_index < kLearnedValueG8Generations;
         ++generation_index) {
        const std::size_t published_generation =
            generation_index + 1;
        const double exploration_rate =
            generation_index < 2 ? 0.10 : 0.05;
        const bool generation_has_search =
            generation_index >= 4;

        const std::shared_ptr<const LearnedModel> parent =
            model;
        const std::string parent_fingerprint =
            learned_model_fingerprint(parent);
        std::vector<LearnedModel::TrainingExample>
            generation_examples;
        generation_examples.reserve(
            generation_games * 60);
        std::size_t rollout_evaluations = 0;
        std::size_t raw_collection_games = 0;
        std::size_t search_collection_games = 0;
        std::size_t raw_collection_examples = 0;
        std::size_t search_collection_examples = 0;
        for (std::size_t game_index = 0;
             game_index < generation_games; ++game_index) {
            const bool game_uses_search =
                generation_has_search &&
                (recipe ==
                         LearnedValueG8Recipe::
                             CanonicalAllSearchLate ||
                 learned_iteration::
                     value_g8_mix50_game_uses_search(
                         published_generation, game_index));
            GameConfig config;
            config.learned_model = parent;
            config.learned_search_depth =
                game_uses_search ? 1 : 0;
            config.bots = {
                BotConfig{
                    .kind = BotKind::Learned,
                    .learned_variant =
                        LearnedVariant::ValueSearchChampion,
                    .rollouts_per_action =
                        game_uses_search ? 1U : 0U,
                    .exploration_rate = exploration_rate,
                    .learned_model = parent,
                },
                BotConfig{
                    .kind = BotKind::Learned,
                    .learned_variant =
                        LearnedVariant::ValueSearchChampion,
                    .rollouts_per_action =
                        game_uses_search ? 1U : 0U,
                    .exploration_rate = exploration_rate,
                    .learned_model = parent,
                },
            };
            const auto [first_deck, second_deck] =
                choose_distinct_decks();
            Game game(
                deck_cards(first_deck),
                deck_cards(second_deck), random(), config);
            std::vector<GameState> trace;
            const GameResult result =
                game.run_with_trace(trace);
            const std::size_t example_begin =
                generation_examples.size();
            add_bootstrap_trace(
                trace, result, parent,
                generation_examples);
            const std::size_t game_examples =
                generation_examples.size() - example_begin;
            if (recipe ==
                LearnedValueG8Recipe::LateMix50) {
                if (game_uses_search) {
                    ++search_collection_games;
                    search_collection_examples += game_examples;
                } else {
                    ++raw_collection_games;
                    raw_collection_examples += game_examples;
                }
            }
            for (const auto& stats : result.player_stats) {
                rollout_evaluations +=
                    stats.monte_carlo_rollouts;
            }
        }

        const std::size_t generation_example_count =
            generation_examples.size();
        replay.append_generation(
            published_generation,
            std::move(generation_examples));
        std::vector<LearnedModel::TrainingExample>
            fit_examples;
        fit_examples.reserve(
            anchor_examples.size() +
            replay.example_count());
        fit_examples.insert(
            fit_examples.end(), anchor_examples.begin(),
            anchor_examples.end());
        replay.for_each(
            [&](std::uint64_t,
                const LearnedModel::TrainingExample& example) {
                fit_examples.push_back(example);
            });

        model = update_learned_value_model_encoded(
            parent, fit_examples,
            {
                .epochs = 3,
                .learning_rate = 0.006,
                .root_seed = seed,
                .member_training_tag =
                    0x53454C4600000000ULL +
                    0x100ULL * generation_index,
            });
        const std::string candidate_fingerprint =
            learned_model_fingerprint(model);
        if (learned_model_fingerprint(parent) !=
            parent_fingerprint) {
            throw std::logic_error(
                "Value G8 update mutated a frozen checkpoint");
        }
        if (candidate_fingerprint ==
            parent_fingerprint) {
            throw std::logic_error(
                "Value G8 generation did not update");
        }

        output.report.generations.push_back({
            .generation = published_generation,
            .self_play_games = generation_games,
            .generation_examples =
                generation_example_count,
            .anchor_examples = anchor_examples.size(),
            .replay_generations =
                replay.generation_count(),
            .replay_examples = replay.example_count(),
            .raw_collection_games =
                raw_collection_games,
            .search_collection_games =
                search_collection_games,
            .raw_collection_examples =
                raw_collection_examples,
            .search_collection_examples =
                search_collection_examples,
            .search_enabled = generation_has_search,
            .search_worlds =
                generation_has_search ? 1U : 0U,
            .search_horizon_turns =
                generation_has_search ? 4U : 0U,
            .rollout_evaluations =
                rollout_evaluations,
            .exploration_rate = exploration_rate,
            .parent_fingerprint =
                parent_fingerprint,
            .candidate_fingerprint =
                candidate_fingerprint,
        });
        output.checkpoints.push_back(model);
    }

    output.model = model;
    output.report.final_fingerprint =
        learned_model_fingerprint(model);
    return output;
}

LearnedValueG8Result train_learned_value_g8(
    std::size_t training_games, std::uint64_t seed) {
    return train_learned_value_g8_recipe(
        training_games, seed,
        LearnedValueG8Recipe::CanonicalAllSearchLate);
}

LearnedValueG8Result train_learned_value_g8_mix50(
    std::size_t training_games, std::uint64_t seed) {
    return train_learned_value_g8_recipe(
        training_games, seed,
        LearnedValueG8Recipe::LateMix50);
}

std::shared_ptr<const LearnedModel>
train_learned_model(std::size_t training_games, std::uint64_t seed) {
    return train_learned_value_champion(training_games, seed);
}

namespace {

constexpr std::size_t kMaximumEvaluationWorlds = 4096;
constexpr std::size_t kMaximumEvaluationRolloutsPerWorld = 256;
constexpr std::size_t kMaximumEvaluationHorizonTurns = 128;

std::uint64_t mix_search_seed(std::uint64_t value) {
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
}

std::uint64_t indexed_search_seed(
    std::uint64_t seed, std::uint64_t domain,
    std::size_t first, std::size_t second = 0) {
    return mix_search_seed(
        seed ^ mix_search_seed(domain) ^
        mix_search_seed(static_cast<std::uint64_t>(first)) ^
        mix_search_seed(
            static_cast<std::uint64_t>(second) ^
            0xD1B54A32D192ED03ULL));
}

void validate_search_config(
    const LearnedSearchConfig& config,
    const std::shared_ptr<const LearnedModel>& model) {
    validate_learned_model(model, config.continuation_variant);
    if (config.worlds == 0 ||
        config.worlds > kMaximumEvaluationWorlds) {
        throw std::invalid_argument(
            "Learned search worlds must be in [1, 4096]");
    }
    if (config.rollouts_per_world == 0 ||
        config.rollouts_per_world >
            kMaximumEvaluationRolloutsPerWorld) {
        throw std::invalid_argument(
            "Learned search rollouts per world must be in [1, 256]");
    }
    if (config.horizon_turns >
        kMaximumEvaluationHorizonTurns) {
        throw std::invalid_argument(
            "Learned search horizon must be at most 128 turns");
    }
}

std::size_t checked_rollout_evaluations(
    std::size_t candidate_count,
    const LearnedSearchConfig& config) {
    const std::size_t maximum =
        std::numeric_limits<std::size_t>::max();
    if (config.worlds > maximum / config.rollouts_per_world) {
        throw std::overflow_error(
            "Learned search sample count overflow");
    }
    const std::size_t samples =
        config.worlds * config.rollouts_per_world;
    if (candidate_count > maximum / samples) {
        throw std::overflow_error(
            "Learned search evaluation count overflow");
    }
    return candidate_count * samples;
}

struct LearnedEvaluationWorld {
    GameState state;
    std::vector<std::uint64_t> continuation_seeds;
};

std::vector<LearnedEvaluationWorld> sample_evaluation_worlds(
    const GameState& state,
    const std::array<std::vector<CardId>, 2>& original_decks,
    std::size_t observer, const LearnedSearchConfig& config) {
    std::vector<LearnedEvaluationWorld> worlds;
    worlds.reserve(config.worlds);
    for (std::size_t world = 0; world < config.worlds; ++world) {
        LearnedEvaluationWorld sampled{
            .state = sample_determinization(
                state, original_decks, observer,
                indexed_search_seed(
                    config.seed, 0x574F524C44ULL, world)),
        };
        sampled.continuation_seeds.reserve(
            config.rollouts_per_world);
        for (std::size_t rollout = 0;
             rollout < config.rollouts_per_world; ++rollout) {
            sampled.continuation_seeds.push_back(
                indexed_search_seed(
                    config.seed, 0x434F4E54494E5545ULL,
                    world, rollout));
        }
        worlds.push_back(std::move(sampled));
    }
    return worlds;
}

GameConfig learned_evaluation_game_config(
    const std::shared_ptr<const LearnedModel>& model,
    LearnedVariant variant) {
    GameConfig config;
    config.learned_model = model;
    config.learned_search_depth = 0;
    config.bots = {
        BotConfig{
            .kind = BotKind::Learned,
            .learned_variant = variant,
            .rollouts_per_action = 0,
            .learned_model = model,
        },
        BotConfig{
            .kind = BotKind::Learned,
            .learned_variant = variant,
            .rollouts_per_action = 0,
            .learned_model = model,
        },
    };
    return config;
}

double learned_result_value(
    const GameResult& result, std::size_t perspective) {
    if (result.winner < 0) {
        return 0.5;
    }
    return result.winner == static_cast<int>(perspective)
               ? 1.0
               : 0.0;
}

double blend_evaluation_score(
    double continuation, double shallow_prior,
    bool blend_shallow_prior,
    std::size_t continuation_sample_count) {
    if (!blend_shallow_prior) {
        return continuation;
    }
    const double continuation_weight =
        static_cast<double>(continuation_sample_count);
    return (shallow_prior +
            continuation_weight * continuation) /
           (continuation_weight + 1.0);
}

} // namespace

LearnedActionSamples learned_priority_action_samples(
    const GameState& state,
    const std::array<std::vector<CardId>, 2>& original_decks,
    std::size_t player, bool sorcery_actions, TurnPhase phase,
    int consecutive_passes,
    const std::vector<PriorityAction>& candidates,
    std::shared_ptr<const LearnedModel> model,
    LearnedSearchConfig config) {
    validate_search_config(config, model);
    validate_priority_candidates(
        state, player, sorcery_actions, phase,
        consecutive_passes, candidates);
    const auto worlds = sample_evaluation_worlds(
        state, original_decks, player, config);

    Game evaluator(
        original_decks[0], original_decks[1],
        indexed_search_seed(
            config.seed, 0x4556414C55415445ULL, 0),
        learned_evaluation_game_config(
            model, config.continuation_variant));
    evaluator.state_ = state;

    LearnedActionSamples result;
    result.sampled_worlds = worlds.size();
    const std::size_t expected_evaluations =
        checked_rollout_evaluations(candidates.size(), config);
    result.q_samples.resize(candidates.size());
    const std::size_t samples_per_action =
        config.worlds * config.rollouts_per_world;
    for (auto& samples : result.q_samples) {
        samples.reserve(samples_per_action);
    }

    for (std::size_t action_index = 0;
         action_index < candidates.size(); ++action_index) {
        const PriorityAction& action = candidates[action_index];
        for (const LearnedEvaluationWorld& world : worlds) {
            for (const std::uint64_t continuation_seed :
                 world.continuation_seeds) {
                Game simulation = evaluator;
                simulation.state_ = world.state;
                simulation.random_.seed(continuation_seed);
                simulation.trace_ = nullptr;
                simulation.config_.learned_policy_recorder.reset();
                simulation.config_.learned_search_depth = 0;

                const double shallow_prior =
                    config.blend_shallow_prior
                        ? simulation
                              .learned_value_shallow_action_score(
                                  action, player, sorcery_actions,
                                  consecutive_passes, world.state)
                        : 0.0;
                PriorityState priority{
                    .player = player,
                    .consecutive_passes = consecutive_passes,
                };
                bool window_ended = false;
                std::optional<GameResult> terminal;
                if (action.kind == PriorityActionKind::Pass) {
                    const PriorityPassResult pass =
                        pass_priority(simulation.state_, priority);
                    window_ended =
                        pass == PriorityPassResult::WindowEnded;
                    if (pass ==
                        PriorityPassResult::StackObjectResolved) {
                        terminal = simulation.life_total_result();
                    }
                } else {
                    if (!apply_priority_action(
                            simulation.state_, player, action,
                            sorcery_actions)) {
                        throw std::logic_error(
                            "validated priority action became illegal");
                    }
                    priority = {
                        .player = player,
                        .consecutive_passes = 0,
                    };
                }

                if (!terminal.has_value() && !window_ended) {
                    terminal =
                        simulation.continue_priority_window(
                            sorcery_actions, phase, priority);
                }
                if (!terminal.has_value()) {
                    terminal =
                        simulation.finish_turn_after_priority_phase(
                            phase);
                }

                const double continuation =
                    terminal.has_value()
                        ? learned_result_value(*terminal, player)
                        : simulation
                              .finish_learned_evaluation_horizon(
                                  player, config.horizon_turns);
                result.q_samples[action_index].push_back(
                    blend_evaluation_score(
                        continuation, shallow_prior,
                        config.blend_shallow_prior,
                        samples_per_action));
                ++result.rollout_evaluations;
            }
        }
    }
    if (result.rollout_evaluations != expected_evaluations) {
        throw std::logic_error(
            "Learned priority evaluation accounting mismatch");
    }
    return result;
}

LearnedActionSamples learned_binary_attack_samples(
    const GameState& state,
    const std::array<std::vector<CardId>, 2>& original_decks,
    std::size_t attacking_player,
    const std::vector<PermanentId>& selected_attackers,
    PermanentId subject,
    const std::vector<PermanentId>& remaining_attackers,
    std::shared_ptr<const LearnedModel> model,
    LearnedSearchConfig config) {
    validate_search_config(config, model);
    validate_binary_attack_context(
        state, attacking_player, selected_attackers, subject,
        remaining_attackers);
    const auto worlds = sample_evaluation_worlds(
        state, original_decks, attacking_player, config);

    Game evaluator(
        original_decks[0], original_decks[1],
        indexed_search_seed(
            config.seed, 0x41545441434B4556ULL, 0),
        learned_evaluation_game_config(
            model, config.continuation_variant));
    evaluator.state_ = state;

    LearnedActionSamples result;
    result.sampled_worlds = worlds.size();
    const std::size_t expected_evaluations =
        checked_rollout_evaluations(2, config);
    result.q_samples.resize(2);
    const std::size_t samples_per_action =
        config.worlds * config.rollouts_per_world;
    for (auto& samples : result.q_samples) {
        samples.reserve(samples_per_action);
    }

    for (std::size_t root_choice = 0; root_choice < 2;
         ++root_choice) {
        for (const LearnedEvaluationWorld& world : worlds) {
            for (const std::uint64_t continuation_seed :
                 world.continuation_seeds) {
                Game simulation = evaluator;
                simulation.state_ = world.state;
                simulation.random_.seed(continuation_seed);
                simulation.trace_ = nullptr;
                simulation.config_.learned_policy_recorder.reset();
                simulation.config_.learned_search_depth = 0;

                std::vector<PermanentId> attackers =
                    selected_attackers;
                if (root_choice == 1) {
                    attackers.push_back(subject);
                }
                if (config.continuation_variant ==
                    LearnedVariant::UnifiedActor) {
                    for (std::size_t index = 0;
                         index < remaining_attackers.size();
                         ++index) {
                        const auto options = attack_policy_options(
                            simulation.state_, attacking_player,
                            remaining_attackers[index], attackers,
                            remaining_attackers.size() - index);
                        if (choose_learned_policy_option(
                                simulation.state_,
                                simulation.config_, options,
                                attacking_player,
                                simulation.random_) == 1) {
                            attackers.push_back(
                                remaining_attackers[index]);
                        }
                    }
                } else {
                    const auto suffix_candidates =
                        learned_value_attack_candidates(
                            remaining_attackers,
                            simulation.random_);
                    std::vector<std::vector<PermanentId>>
                        attack_candidates;
                    attack_candidates.reserve(
                        suffix_candidates.size());
                    for (const auto& suffix : suffix_candidates) {
                        std::vector<PermanentId> candidate =
                            attackers;
                        candidate.insert(
                            candidate.end(), suffix.begin(),
                            suffix.end());
                        attack_candidates.push_back(
                            std::move(candidate));
                    }
                    const auto evaluation =
                        score_learned_value_attack_sets(
                            simulation.state_, attacking_player,
                            attack_candidates, model,
                            simulation.random_);
                    attackers = attack_candidates[
                        evaluation.selected_candidate];
                }

                std::optional<GameResult> terminal =
                    simulation.play_combat_with_attackers(
                        std::move(attackers));
                double shallow_prior = 0.0;
                if (terminal.has_value()) {
                    shallow_prior = learned_result_value(
                        *terminal, attacking_player);
                } else {
                    shallow_prior = model->predict(
                        learned_features(
                            simulation.state_,
                            attacking_player));
                    terminal = simulation.play_priority_window(
                        true, TurnPhase::SecondMain);
                }
                if (!terminal.has_value()) {
                    cleanup_turn(simulation.state_);
                }

                const double continuation =
                    terminal.has_value()
                        ? learned_result_value(
                              *terminal, attacking_player)
                        : simulation
                              .finish_learned_evaluation_horizon(
                                  attacking_player,
                                  config.horizon_turns);
                result.q_samples[root_choice].push_back(
                    blend_evaluation_score(
                        continuation, shallow_prior,
                        config.blend_shallow_prior,
                        samples_per_action));
                ++result.rollout_evaluations;
            }
        }
    }
    if (result.rollout_evaluations != expected_evaluations) {
        throw std::logic_error(
            "Learned attack evaluation accounting mismatch");
    }
    return result;
}

LearnedActorGenerationPriorityDiagnostic
diagnose_learned_actor_generation_priority(
    const GameState& state,
    const std::array<std::vector<CardId>, 2>& original_decks,
    std::size_t player, bool sorcery_actions, TurnPhase phase,
    int consecutive_passes, std::shared_ptr<const LearnedModel> parent,
    LearnedSearchConfig search) {
    if (search.continuation_variant !=
            LearnedVariant::UnifiedActor ||
        search.blend_shallow_prior) {
        throw std::invalid_argument(
            "generation priority diagnostic requires unblended Actor search");
    }
    validate_search_config(search, parent);
    if (player >= state.players.size()) {
        throw std::out_of_range(
            "generation priority diagnostic player is invalid");
    }

    auto recorder =
        std::make_shared<LearnedPolicyRecorder>();
    recorder->generation_collection =
        LearnedPolicyRecorder::GenerationCollection{
            .root_seed = search.seed,
            .generation = 1,
            .schedule_index = 0,
            .worlds = search.worlds,
            .rollouts_per_world =
                search.rollouts_per_world,
            .horizon_turns = search.horizon_turns,
            .max_roots_per_seat_kind = 1,
        };
    GameConfig config;
    config.learned_model = parent;
    config.learned_search_depth = 0;
    config.learned_policy_recorder = recorder;
    config.bots = {
        BotConfig{
            .kind = BotKind::Learned,
            .learned_variant =
                LearnedVariant::UnifiedActor,
            .rollouts_per_action = 0,
            .exploration_rate = 0.0,
            .learned_model = parent,
        },
        BotConfig{
            .kind = BotKind::Learned,
            .learned_variant =
                LearnedVariant::UnifiedActor,
            .rollouts_per_action = 0,
            .exploration_rate = 0.0,
            .learned_model = parent,
        },
    };
    Game simulation(
        original_decks[0], original_decks[1],
        learned_iteration::derive_seed(
            search.seed,
            learned_iteration::SeedDomain::SelfPlayGame,
            1, 0),
        config);
    simulation.state_ = state;

    const auto actions =
        legal_priority_actions(state, player, sorcery_actions);
    const PriorityAction selected =
        simulation.choose_priority_action(
            actions, player, sorcery_actions, phase,
            consecutive_passes);
    bool transition_applied = false;
    std::optional<PriorityPassResult> pass_result;
    std::optional<GameResult> terminal;
    if (selected.kind == PriorityActionKind::Pass) {
        PriorityState priority{
            .player = player,
            .consecutive_passes = consecutive_passes,
        };
        pass_result =
            pass_priority(simulation.state_, priority);
        transition_applied = true;
        if (*pass_result ==
            PriorityPassResult::StackObjectResolved) {
            terminal = simulation.life_total_result();
        }
    } else {
        transition_applied =
            apply_priority_action(
                simulation.state_, player, selected,
                sorcery_actions);
    }

    const auto& collection =
        *recorder->generation_collection;
    return {
        .searched_roots =
            collection.searched_roots[player][0],
        .rollout_evaluations =
            collection.rollout_evaluations[player][0],
        .selected_action = selected,
        .transition_applied = transition_applied,
        .pass_result = pass_result,
        .terminal_result = terminal,
        .final_state = simulation.state_,
    };
}

LearnedActorGenerationAttackDiagnostic
diagnose_learned_actor_generation_attack(
    const GameState& state,
    const std::array<std::vector<CardId>, 2>& original_decks,
    std::shared_ptr<const LearnedModel> parent,
    LearnedSearchConfig search) {
    if (search.continuation_variant !=
            LearnedVariant::UnifiedActor ||
        search.blend_shallow_prior) {
        throw std::invalid_argument(
            "generation attack diagnostic requires unblended Actor search");
    }
    validate_search_config(search, parent);
    if (state.active_player >= state.players.size()) {
        throw std::out_of_range(
            "generation attack diagnostic active player is invalid");
    }

    auto recorder =
        std::make_shared<LearnedPolicyRecorder>();
    recorder->generation_collection =
        LearnedPolicyRecorder::GenerationCollection{
            .root_seed = search.seed,
            .generation = 1,
            .schedule_index = 0,
            .worlds = search.worlds,
            .rollouts_per_world =
                search.rollouts_per_world,
            .horizon_turns = search.horizon_turns,
            .max_roots_per_seat_kind = 1,
        };
    GameConfig config;
    config.learned_model = parent;
    config.learned_search_depth = 0;
    config.learned_policy_recorder = recorder;
    config.bots = {
        BotConfig{
            .kind = BotKind::Learned,
            .learned_variant =
                LearnedVariant::UnifiedActor,
            .rollouts_per_action = 0,
            .exploration_rate = 0.0,
            .learned_model = parent,
        },
        BotConfig{
            .kind = BotKind::Learned,
            .learned_variant =
                LearnedVariant::UnifiedActor,
            .rollouts_per_action = 0,
            .exploration_rate = 0.0,
            .learned_model = parent,
        },
    };
    Game simulation(
        original_decks[0], original_decks[1],
        learned_iteration::derive_seed(
            search.seed,
            learned_iteration::SeedDomain::SelfPlayGame,
            1, 0),
        config);
    simulation.state_ = state;
    const auto terminal =
        simulation.play_combat_after_beginning();
    const auto& collection =
        *recorder->generation_collection;
    const std::size_t player = state.active_player;
    return {
        .searched_roots =
            collection.searched_roots[player][1],
        .rollout_evaluations =
            collection.rollout_evaluations[player][1],
        .included_attackers =
            collection.attack_includes[player],
        .terminal_result = terminal,
        .final_state = simulation.state_,
    };
}

LearnedValuePriorityDiagnostic diagnose_learned_value_priority(
    const GameState& state,
    const std::array<std::vector<CardId>, 2>& original_decks,
    std::size_t player, bool sorcery_actions, TurnPhase phase,
    int consecutive_passes, std::shared_ptr<const LearnedModel> model,
    std::size_t rollouts_per_action, std::uint64_t seed) {
    if (!model) {
        throw std::invalid_argument(
            "Learned Value diagnostic requires a frozen model");
    }
    if (player >= state.players.size()) {
        throw std::out_of_range(
            "Learned Value diagnostic player must be 0 or 1");
    }
    if (consecutive_passes < 0 || consecutive_passes > 1) {
        throw std::out_of_range(
            "Learned Value diagnostic pass count must be zero or one");
    }

    GameConfig config;
    config.learned_model = model;
    config.learned_search_depth =
        rollouts_per_action == 0 ? 0 : 1;
    config.bots = {
        BotConfig{
            .kind = BotKind::Learned,
            .learned_variant =
                LearnedVariant::ValueSearchChampion,
            .rollouts_per_action = rollouts_per_action,
            .learned_model = model,
        },
        BotConfig{
            .kind = BotKind::Learned,
            .learned_variant =
                LearnedVariant::ValueSearchChampion,
            .rollouts_per_action = rollouts_per_action,
            .learned_model = model,
        },
    };
    Game evaluator(
        original_decks[0], original_decks[1],
        seed ^ 0x56414C5545444941ULL, config);
    evaluator.state_ = state;

    LearnedValuePriorityDiagnostic diagnostic;
    diagnostic.actions =
        legal_priority_actions(state, player, sorcery_actions);
    diagnostic.sampled_worlds =
        std::max<std::size_t>(1, rollouts_per_action);
    diagnostic.rollout_evaluations =
        diagnostic.actions.size() * rollouts_per_action;
    diagnostic.scores.assign(
        diagnostic.actions.size(), 0.0);

    struct SampledWorld {
        GameState state;
        std::uint64_t continuation_seed = 0;
    };
    std::mt19937_64 random(seed);
    std::vector<SampledWorld> worlds;
    worlds.reserve(diagnostic.sampled_worlds);
    for (std::size_t world = 0;
         world < diagnostic.sampled_worlds; ++world) {
        const std::uint64_t world_seed = random();
        worlds.push_back({
            .state = sample_determinization(
                state, original_decks, player, world_seed),
            .continuation_seed = random(),
        });
    }
    for (std::size_t action_index = 0;
         action_index < diagnostic.actions.size();
         ++action_index) {
        for (const auto& world : worlds) {
            diagnostic.scores[action_index] +=
                evaluator.learned_value_shallow_action_score(
                    diagnostic.actions[action_index], player,
                    sorcery_actions, consecutive_passes,
                    world.state);
        }
        diagnostic.scores[action_index] /=
            static_cast<double>(worlds.size());
        if (rollouts_per_action == 0) {
            continue;
        }
        for (const auto& world : worlds) {
            diagnostic.scores[action_index] +=
                evaluator.learned_value_search_action_score(
                    diagnostic.actions[action_index], player,
                    sorcery_actions, phase, consecutive_passes,
                    world.state, world.continuation_seed);
        }
        diagnostic.scores[action_index] /=
            static_cast<double>(worlds.size() + 1);
    }
    return diagnostic;
}

double WhitePlanTeacherDiagnostic::
    two_world_reference_agreement_rate() const {
    return two_world_trials == 0
               ? 0.0
               : 100.0 *
                     static_cast<double>(
                         two_world_reference_agreements) /
                     static_cast<double>(two_world_trials);
}

double WhitePlanTeacherDiagnostic::
    two_world_plan_order_agreement_rate() const {
    return two_world_trials == 0
               ? 0.0
               : 100.0 *
                     static_cast<double>(
                         two_world_plan_order_agreements) /
                     static_cast<double>(two_world_trials);
}

WhitePlanTeacherDiagnostic diagnose_white_lock_plan_teacher(
    std::shared_ptr<const LearnedModel> model, std::uint64_t seed) {
    if (!model) {
        throw std::invalid_argument(
            "White plan diagnostic requires a Learned model");
    }

    constexpr std::size_t kReferenceWorlds = 64;
    constexpr std::size_t kTwoWorldTrials = 32;
    WhitePlanTeacherDiagnostic diagnostic;
    diagnostic.state = white_lock_plan_diagnostic_state();
    diagnostic.reference_worlds = kReferenceWorlds;
    diagnostic.two_world_trials = kTwoWorldTrials;

    const auto actions =
        legal_priority_actions(diagnostic.state, 0, true);
    if (actions.size() < 2) {
        throw std::logic_error(
            "White plan diagnostic has no non-pass action");
    }

    GameConfig config;
    config.learned_model = std::move(model);
    config.bots = {
        BotConfig{
            .kind = BotKind::Learned,
            .learned_variant = LearnedVariant::UnifiedActor,
            .rollouts_per_action = 0,
            .exploration_rate = 1.0,
        },
        BotConfig{
            .kind = BotKind::Learned,
            .learned_variant = LearnedVariant::UnifiedActor,
            .rollouts_per_action = 0,
            .exploration_rate = 1.0,
        },
    };
    Game evaluator(
        white_control_deck(), red_alpha_deck(),
        seed ^ 0x444941474E4F5354ULL, config);
    evaluator.state_ = diagnostic.state;

    const auto evaluate_common_worlds =
        [&](std::size_t world_count,
            std::uint64_t evaluation_seed) {
            struct SampledWorld {
                GameState state;
                std::uint64_t continuation_seed = 0;
            };
            std::mt19937_64 random(evaluation_seed);
            std::vector<SampledWorld> worlds;
            worlds.reserve(world_count);
            for (std::size_t world = 0; world < world_count;
                 ++world) {
                const std::uint64_t world_seed = random();
                worlds.push_back({
                    .state = sample_determinization(
                        diagnostic.state, evaluator.decks_, 0,
                        world_seed),
                    .continuation_seed = random(),
                });
            }

            std::vector<double> scores(actions.size(), 0.0);
            for (std::size_t action_index = 0;
                 action_index < actions.size(); ++action_index) {
                for (const auto& world : worlds) {
                    scores[action_index] +=
                        evaluator
                            .learned_information_set_action_score(
                                actions[action_index], 0, true,
                                TurnPhase::FirstMain, 0,
                                world.state,
                                world.continuation_seed);
                }
                scores[action_index] /=
                    static_cast<double>(world_count);
            }
            return scores;
        };
    const auto best_action =
        [](const std::vector<double>& scores) {
            return static_cast<std::size_t>(
                std::distance(
                    scores.begin(),
                    std::max_element(
                        scores.begin(), scores.end())));
        };

    std::mt19937_64 evaluation_seeds(
        seed ^ 0x5748495445504C41ULL);
    const auto reference_scores =
        evaluate_common_worlds(
            kReferenceWorlds, evaluation_seeds());
    diagnostic.reference_best_action =
        best_action(reference_scores);
    diagnostic.actions.reserve(actions.size());
    for (std::size_t index = 0; index < actions.size(); ++index) {
        diagnostic.actions.push_back({
            .action = actions[index],
            .reference_score = reference_scores[index],
        });
        if (actions[index].kind ==
                PriorityActionKind::ActivateMillstone &&
            actions[index].target.has_value() &&
            actions[index].target->player == 1 &&
            !actions[index].target->creature.has_value()) {
            diagnostic.opponent_millstone_action = index;
        }
        if (actions[index].kind ==
                PriorityActionKind::CastEnchantment &&
            actions[index].card == CardId::Moat) {
            diagnostic.redundant_moat_action = index;
        }
    }
    if (!diagnostic.opponent_millstone_action.has_value() ||
        !diagnostic.redundant_moat_action.has_value()) {
        throw std::logic_error(
            "White plan diagnostic is missing a plan action");
    }
    const std::size_t millstone_action =
        *diagnostic.opponent_millstone_action;
    const std::size_t moat_action =
        *diagnostic.redundant_moat_action;
    const auto compare_plan =
        [millstone_action, moat_action](
            const std::vector<double>& scores) {
            if (scores[millstone_action] >
                scores[moat_action]) {
                return 1;
            }
            if (scores[moat_action] >
                scores[millstone_action]) {
                return -1;
            }
            return 0;
        };
    const int reference_plan_order =
        compare_plan(reference_scores);

    for (std::size_t trial = 0; trial < kTwoWorldTrials;
         ++trial) {
        const auto trial_scores =
            evaluate_common_worlds(2, evaluation_seeds());
        const std::size_t trial_best =
            best_action(trial_scores);
        ++diagnostic.actions[trial_best]
              .two_world_first_place_count;
        if (trial_best == diagnostic.reference_best_action) {
            ++diagnostic.two_world_reference_agreements;
        }
        const int trial_plan_order = compare_plan(trial_scores);
        if (trial_plan_order > 0) {
            ++diagnostic.two_world_millstone_preferences;
        } else if (trial_plan_order < 0) {
            ++diagnostic.two_world_moat_preferences;
        } else {
            ++diagnostic.two_world_plan_ties;
        }
        if (trial_plan_order == reference_plan_order) {
            ++diagnostic.two_world_plan_order_agreements;
        }
    }

    return diagnostic;
}

double BotBenchmarkSummary::challenger_win_rate() const {
    return challenger_stats.win_rate();
}

namespace {

std::pair<double, double> wilson_interval_95(std::size_t wins,
                                             std::size_t games) {
    if (games == 0) {
        return {0.0, 0.0};
    }
    constexpr double z = 1.959963984540054;
    constexpr double z_squared = z * z;
    const double count = static_cast<double>(games);
    const double proportion = static_cast<double>(wins) / count;
    const double denominator = 1.0 + z_squared / count;
    const double center =
        (proportion + z_squared / (2.0 * count)) / denominator;
    const double margin =
        z * std::sqrt((proportion * (1.0 - proportion) +
                       z_squared / (4.0 * count)) /
                      count) /
        denominator;
    return {
        100.0 * std::max(0.0, center - margin),
        100.0 * std::min(1.0, center + margin),
    };
}

} // namespace

double BotBenchmarkSummary::confidence_low_95() const {
    return wilson_interval_95(challenger_stats.wins,
                              challenger_stats.games)
        .first;
}

double BotBenchmarkSummary::confidence_high_95() const {
    return wilson_interval_95(challenger_stats.wins,
                              challenger_stats.games)
        .second;
}

bool BotBenchmarkSummary::challenger_is_better_95() const {
    return confidence_low_95() > 50.0;
}

BotBenchmarkSummary
run_bot_benchmark(std::size_t repetitions_per_deck_pairing,
                  std::uint64_t seed, BotConfig challenger,
                  BotConfig baseline, GameConfig game_config) {
    if (repetitions_per_deck_pairing == 0) {
        throw std::invalid_argument(
            "benchmark repetitions must be positive");
    }
    const bool distinct_explicit_models =
        challenger.kind == BotKind::Learned &&
        baseline.kind == BotKind::Learned &&
        challenger.learned_model && baseline.learned_model &&
        challenger.learned_model != baseline.learned_model;
    const bool same_policy =
        challenger.kind == baseline.kind &&
        (challenger.kind != BotKind::Learned ||
         (challenger.learned_variant ==
              baseline.learned_variant &&
          !distinct_explicit_models));
    if (same_policy &&
        challenger.rollouts_per_action ==
            baseline.rollouts_per_action) {
        throw std::invalid_argument(
            "benchmark bots must use different policies or rollout counts");
    }
    const bool distinct_learned_variants =
        challenger.kind == BotKind::Learned &&
        baseline.kind == BotKind::Learned &&
        challenger.learned_variant != baseline.learned_variant;
    if (!game_config.learned_model || distinct_learned_variants) {
        std::array<std::shared_ptr<const LearnedModel>, 2>
            trained_by_variant;
        const auto ensure_frozen_model =
            [&](BotConfig& bot) {
                if (bot.kind != BotKind::Learned ||
                    bot.learned_model) {
                    return;
                }
                const std::size_t variant =
                    static_cast<std::size_t>(
                        bot.learned_variant);
                if (!trained_by_variant[variant]) {
                    trained_by_variant[variant] =
                        bot.learned_variant ==
                                LearnedVariant::UnifiedActor
                            ? train_learned_actor_model(
                                  bot.training_games,
                                  game_config
                                      .learned_training_seed)
                            : train_learned_value_champion(
                                  bot.training_games,
                                  game_config
                                      .learned_training_seed);
                }
                bot.learned_model =
                    trained_by_variant[variant];
            };
        ensure_frozen_model(challenger);
        ensure_frozen_model(baseline);
    }

    BotBenchmarkSummary summary = {
        .challenger = challenger,
        .baseline = baseline,
        .learned_training_seed =
            game_config.learned_training_seed,
        .repetitions_per_deck_pairing =
            repetitions_per_deck_pairing,
    };
    std::mt19937_64 seed_generator(seed);
    struct BenchmarkTask {
        std::size_t first_deck;
        std::size_t second_deck;
        std::size_t challenger_player;
        std::size_t baseline_player;
        std::size_t challenger_deck;
        std::size_t baseline_deck;
        std::size_t starting_player;
        std::uint64_t seed;
    };
    std::vector<BenchmarkTask> tasks;
    tasks.reserve(repetitions_per_deck_pairing * 40);
    for (std::size_t first_deck = 0; first_deck < 4; ++first_deck) {
        for (std::size_t second_deck = first_deck;
             second_deck < 4; ++second_deck) {
            for (std::size_t repetition = 0;
                 repetition < repetitions_per_deck_pairing;
                 ++repetition) {
                const std::uint64_t game_seed = seed_generator();
                for (std::size_t assignment = 0; assignment < 2;
                     ++assignment) {
                    const std::size_t challenger_player = assignment;
                    const std::size_t baseline_player =
                        opponent_of(challenger_player);
                    const std::size_t challenger_deck =
                        challenger_player == 0 ? first_deck
                                               : second_deck;
                    const std::size_t baseline_deck =
                        baseline_player == 0 ? first_deck
                                            : second_deck;

                    for (std::size_t starting_player = 0;
                         starting_player < 2; ++starting_player) {
                        tasks.push_back({
                            .first_deck = first_deck,
                            .second_deck = second_deck,
                            .challenger_player = challenger_player,
                            .baseline_player = baseline_player,
                            .challenger_deck = challenger_deck,
                            .baseline_deck = baseline_deck,
                            .starting_player = starting_player,
                            .seed = game_seed,
                        });
                    }
                }
            }
        }
    }

    const std::array<std::vector<CardId>, 4> decks = {
        deck_cards(DeckId::Green),
        deck_cards(DeckId::Red),
        deck_cards(DeckId::Blue),
        deck_cards(DeckId::White),
    };
    std::vector<GameResult> results(tasks.size());
    std::atomic_size_t next_task = 0;
    const std::size_t worker_count = std::min<std::size_t>(
        tasks.size(),
        std::max(1U, std::thread::hardware_concurrency()));
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (std::size_t worker = 0; worker < worker_count; ++worker) {
        workers.emplace_back([&] {
            while (true) {
                const std::size_t task_index =
                    next_task.fetch_add(1, std::memory_order_relaxed);
                if (task_index >= tasks.size()) {
                    return;
                }
                const auto& task = tasks[task_index];
                GameConfig current_config = game_config;
                current_config.starting_player =
                    task.starting_player;
                current_config.bots[task.challenger_player] =
                    challenger;
                current_config.bots[task.baseline_player] = baseline;
                Game game(decks[task.first_deck],
                          decks[task.second_deck], task.seed,
                          current_config);
                results[task_index] = game.run();
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    for (std::size_t task_index = 0; task_index < tasks.size();
         ++task_index) {
        const auto& task = tasks[task_index];
        const auto& result = results[task_index];
        ++summary.total_games;
        record_bot_result(summary.challenger_stats, result,
                          task.challenger_player);
        record_bot_result(summary.baseline_stats, result,
                          task.baseline_player);
        record_deck_result(
            summary.challenger_decks[task.challenger_deck], result,
            task.challenger_player);
        record_deck_result(
            summary.baseline_decks[task.baseline_deck], result,
            task.baseline_player);
    }

    return summary;
}

DeckEvolutionSummary evolve_deck(DeckEvolutionConfig config,
                                 std::uint64_t seed,
                                 GameConfig game_config) {
    if (config.generations == 0) {
        throw std::invalid_argument(
            "deck evolution generations must be positive");
    }
    if (config.population < 4) {
        throw std::invalid_argument(
            "deck evolution population must be at least four");
    }
    if (config.repetitions_per_opponent == 0) {
        throw std::invalid_argument(
            "deck evolution repetitions must be positive");
    }
    if ((config.pilot.kind == BotKind::MonteCarlo ||
         config.pilot.kind == BotKind::DeepMonteCarlo) &&
        config.pilot.rollouts_per_action == 0) {
        throw std::invalid_argument(
            "deck evolution Monte Carlo rollouts must be positive");
    }
    if (config.pilot.kind == BotKind::Learned &&
        !game_config.learned_model) {
        game_config.learned_model = train_learned_model(
            config.pilot.training_games,
            game_config.learned_training_seed);
    }
    game_config.bots = {config.pilot, config.pilot};

    const std::array<std::vector<CardId>, 4> metagame = {
        deck_cards(DeckId::Green),
        deck_cards(DeckId::Red),
        deck_cards(DeckId::Blue),
        deck_cards(DeckId::White),
    };
    std::vector<CardId> card_pool;
    std::array<bool, kLearnedCardCount> seen_cards{};
    for (const auto& deck : metagame) {
        for (const CardId card : deck) {
            const auto index = static_cast<std::size_t>(card);
            if (!seen_cards[index]) {
                seen_cards[index] = true;
                card_pool.push_back(card);
            }
        }
    }

    std::mt19937_64 random(seed);
    std::uniform_int_distribution<std::size_t> choose_card(
        0, card_pool.size() - 1);
    const auto mutate = [&](std::vector<CardId>& deck,
                            std::size_t mutations) {
        std::uniform_int_distribution<std::size_t> choose_slot(
            0, deck.size() - 1);
        for (std::size_t mutation = 0; mutation < mutations;
             ++mutation) {
            deck[choose_slot(random)] = card_pool[choose_card(random)];
        }
    };

    std::vector<std::vector<CardId>> population;
    population.reserve(config.population);
    for (const auto& deck : metagame) {
        population.push_back(deck);
    }
    while (population.size() < config.population) {
        std::vector<CardId> candidate =
            metagame[population.size() % metagame.size()];
        mutate(candidate, 1 + population.size() % 8);
        population.push_back(std::move(candidate));
    }

    const std::uint64_t evaluation_seed =
        seed ^ 0x4556414C55415445ULL;
    const auto evaluate_population =
        [&](const std::vector<std::vector<CardId>>& candidates) {
            std::vector<EvolvedDeck> evaluations(candidates.size());
            std::atomic_size_t next_candidate = 0;
            const std::size_t worker_count =
                std::min<std::size_t>(
                    candidates.size(),
                    std::max(
                        1U, std::thread::hardware_concurrency()));
            std::vector<std::thread> workers;
            workers.reserve(worker_count);
            for (std::size_t worker = 0; worker < worker_count;
                 ++worker) {
                workers.emplace_back([&] {
                    while (true) {
                        const std::size_t candidate_index =
                            next_candidate.fetch_add(
                                1, std::memory_order_relaxed);
                        if (candidate_index >= candidates.size()) {
                            return;
                        }
                        auto& evaluation =
                            evaluations[candidate_index];
                        evaluation.cards =
                            candidates[candidate_index];
                        std::mt19937_64 game_seeds(
                            evaluation_seed);
                        for (std::size_t opponent = 0;
                             opponent < metagame.size(); ++opponent) {
                            for (std::size_t repetition = 0;
                                 repetition <
                                 config.repetitions_per_opponent;
                                 ++repetition) {
                                const std::uint64_t game_seed =
                                    game_seeds();
                                for (std::size_t candidate_player = 0;
                                     candidate_player < 2;
                                     ++candidate_player) {
                                    for (std::size_t starting_player = 0;
                                         starting_player < 2;
                                         ++starting_player) {
                                        GameConfig current_config =
                                            game_config;
                                        current_config.starting_player =
                                            starting_player;
                                        const bool candidate_first =
                                            candidate_player == 0;
                                        Game game(
                                            candidate_first
                                                ? candidates[candidate_index]
                                                : metagame[opponent],
                                            candidate_first
                                                ? metagame[opponent]
                                                : candidates[candidate_index],
                                            game_seed, current_config);
                                        const GameResult result =
                                            game.run();
                                        record_deck_result(
                                            evaluation.total, result,
                                            candidate_player);
                                        record_deck_result(
                                            evaluation
                                                .by_opponent[opponent],
                                            result, candidate_player);
                                    }
                                }
                            }
                        }
                    }
                });
            }
            for (auto& worker : workers) {
                worker.join();
            }
            return evaluations;
        };

    DeckEvolutionSummary summary;
    for (std::size_t generation = 0;
         generation < config.generations; ++generation) {
        auto evaluations = evaluate_population(population);
        std::vector<std::size_t> order(evaluations.size());
        for (std::size_t index = 0; index < order.size(); ++index) {
            order[index] = index;
        }
        std::sort(
            order.begin(), order.end(),
            [&](std::size_t left, std::size_t right) {
                const double left_rate =
                    evaluations[left].total.win_rate();
                const double right_rate =
                    evaluations[right].total.win_rate();
                if (left_rate != right_rate) {
                    return left_rate > right_rate;
                }
                return evaluations[left].cards <
                       evaluations[right].cards;
            });
        const EvolvedDeck& generation_best =
            evaluations[order.front()];
        summary.generation_best_win_rates.push_back(
            generation_best.total.win_rate());
        if (summary.best.cards.empty() ||
            generation_best.total.win_rate() >
                summary.best.total.win_rate()) {
            summary.best = generation_best;
        }

        const std::size_t elite_count =
            std::max<std::size_t>(2, config.population / 4);
        std::vector<std::vector<CardId>> next_population;
        next_population.reserve(config.population);
        for (std::size_t elite = 0; elite < elite_count;
             ++elite) {
            next_population.push_back(
                evaluations[order[elite]].cards);
        }
        std::uniform_int_distribution<std::size_t> choose_elite(
            0, elite_count - 1);
        std::uniform_int_distribution<std::size_t> mutation_count(1, 4);
        while (next_population.size() < config.population) {
            auto child =
                next_population[choose_elite(random)];
            mutate(child, mutation_count(random));
            next_population.push_back(std::move(child));
        }
        population = std::move(next_population);
    }
    return summary;
}

} // namespace alpha
