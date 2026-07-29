#include "old_school/learned_priority_bilinear_artifact.hpp"

#include "old_school/artifact_integrity.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace old_school::learned_priority_bilinear_artifact {
namespace {

constexpr std::array<char, 8> kMagic{
    'O', 'S', 'A', 'Q', '1', '9', 'R', '2',
};
constexpr std::uint32_t kWireVersion = 1;
constexpr std::uint32_t kEndianMarker = 0x01020304U;
constexpr std::size_t kSha256Characters = 64;

[[noreturn]] void fail(std::string_view message) {
    throw std::invalid_argument(
        "invalid AQ19 bilinear artifact: " +
        std::string(message));
}

bool canonical_sha256(std::string_view value) {
    return value.size() == kSha256Characters &&
           std::all_of(
               value.begin(), value.end(),
               [](char character) {
                   return (character >= '0' &&
                           character <= '9') ||
                          (character >= 'a' &&
                           character <= 'f');
               });
}

void validate_contract(const Contract& contract) {
    if (!canonical_sha256(
            contract.parent_fingerprint) ||
        !canonical_sha256(
            contract.parameter_sha256) ||
        std::all_of(
            contract.parent_fingerprint.begin(),
            contract.parent_fingerprint.end(),
            [](char character) {
                return character == '0';
            }) ||
        std::all_of(
            contract.parameter_sha256.begin(),
            contract.parameter_sha256.end(),
            [](char character) {
                return character == '0';
            })) {
        fail("contract hashes are not canonical SHA-256");
    }
}

class Writer {
  public:
    void u32(std::uint32_t value) {
        for (std::size_t byte = 0; byte < 4; ++byte) {
            bytes_.push_back(
                static_cast<char>(
                    static_cast<unsigned char>(
                        value >> (8U * byte))));
        }
    }

    void u64(std::uint64_t value) {
        for (std::size_t byte = 0; byte < 8; ++byte) {
            bytes_.push_back(
                static_cast<char>(
                    static_cast<unsigned char>(
                        value >> (8U * byte))));
        }
    }

    void fixed(std::string_view value) {
        bytes_.append(value);
    }

    const std::string& bytes() const {
        return bytes_;
    }

  private:
    std::string bytes_;
};

class Reader {
  public:
    explicit Reader(std::string_view bytes)
        : bytes_(bytes) {}

    std::string_view take(std::size_t count) {
        if (cursor_ > bytes_.size() ||
            count > bytes_.size() - cursor_) {
            fail("file is truncated");
        }
        const std::string_view result =
            bytes_.substr(cursor_, count);
        cursor_ += count;
        return result;
    }

    std::uint32_t u32() {
        std::uint32_t value = 0;
        const std::string_view bytes = take(4);
        for (std::size_t byte = 0; byte < 4; ++byte) {
            value |=
                static_cast<std::uint32_t>(
                    static_cast<unsigned char>(
                        bytes[byte]))
                << (8U * byte);
        }
        return value;
    }

    std::uint64_t u64() {
        std::uint64_t value = 0;
        const std::string_view bytes = take(8);
        for (std::size_t byte = 0; byte < 8; ++byte) {
            value |=
                static_cast<std::uint64_t>(
                    static_cast<unsigned char>(
                        bytes[byte]))
                << (8U * byte);
        }
        return value;
    }

    bool done() const {
        return cursor_ == bytes_.size();
    }

  private:
    std::string_view bytes_;
    std::size_t cursor_ = 0;
};

std::string encode(
    const LearnedPriorityBilinearParameters& parameters,
    const Contract& contract) {
    validate_contract(contract);
    const std::string payload =
        learned_priority_bilinear_canonical_bytes(
            parameters);
    const std::string parameter_sha256 =
        artifact_integrity::sha256_string(payload);
    if (parameter_sha256 !=
        contract.parameter_sha256) {
        fail(
            "parameter SHA-256 is " +
            parameter_sha256 + ", expected " +
            contract.parameter_sha256);
    }

    Writer writer;
    writer.fixed(
        std::string_view(kMagic.data(), kMagic.size()));
    writer.u32(kWireVersion);
    writer.u32(kEndianMarker);
    writer.u64(
        static_cast<std::uint64_t>(payload.size()));
    writer.fixed(contract.parent_fingerprint);
    writer.fixed(contract.parameter_sha256);
    writer.fixed(payload);
    if (writer.bytes().size() >
        kMaximumArtifactBytes) {
        fail("file exceeds its byte bound");
    }
    return writer.bytes();
}

std::string read_stable_file(
    const std::filesystem::path& path,
    artifact_integrity::RegularFileSnapshot& snapshot) {
    snapshot =
        artifact_integrity::snapshot_regular_file(path);
    if (snapshot.byte_size == 0 ||
        snapshot.byte_size > kMaximumArtifactBytes ||
        snapshot.byte_size >
            std::numeric_limits<std::size_t>::max()) {
        fail("file size is invalid");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(
            "cannot open AQ19 bilinear artifact '" +
            path.string() + "'");
    }
    std::string bytes(
        static_cast<std::size_t>(snapshot.byte_size),
        '\0');
    input.read(
        bytes.data(),
        static_cast<std::streamsize>(bytes.size()));
    if (!input ||
        input.gcount() !=
            static_cast<std::streamsize>(bytes.size()) ||
        input.peek() !=
            std::char_traits<char>::eof()) {
        fail("file read is incomplete");
    }
    const auto after =
        artifact_integrity::snapshot_regular_file(path);
    if (snapshot != after ||
        artifact_integrity::sha256_string(bytes) !=
            snapshot.sha256) {
        fail("file changed while it was read");
    }
    return bytes;
}

void require_absent(
    const std::filesystem::path& path,
    std::string_view context) {
    std::error_code error;
    const auto status =
        std::filesystem::symlink_status(path, error);
    if (error &&
        error !=
            std::errc::no_such_file_or_directory) {
        throw std::system_error(
            error,
            "cannot inspect AQ19 artifact " +
                std::string(context));
    }
    if (!error &&
        status.type() !=
            std::filesystem::file_type::not_found) {
        throw std::runtime_error(
            "AQ19 artifact " +
            std::string(context) +
            " already exists: " + path.string());
    }
}

std::filesystem::path temporary_path_for(
    const std::filesystem::path& destination) {
    std::filesystem::path temporary = destination;
    temporary += ".tmp-aq19";
    return temporary;
}

} // namespace

const Contract& production_contract() {
    static const Contract contract{
        .parent_fingerprint =
            std::string(
                kLearnedPriorityBilinearRequiredFingerprint),
        .parameter_sha256 =
            std::string(kProductionParameterSha256),
    };
    return contract;
}

Identity publish_atomic_no_replace(
    const std::filesystem::path& destination,
    const LearnedPriorityBilinearParameters& parameters,
    const Contract& contract) {
    if (destination.empty() ||
        destination.filename().empty()) {
        throw std::invalid_argument(
            "AQ19 artifact destination is empty");
    }
    const std::filesystem::path temporary =
        temporary_path_for(destination);
    require_absent(destination, "destination");
    require_absent(temporary, "temporary");
    const std::string bytes =
        encode(parameters, contract);

    try {
        std::ofstream output(
            temporary,
            std::ios::binary |
                std::ios::out |
                std::ios::trunc);
        if (!output) {
            throw std::runtime_error(
                "cannot create AQ19 artifact temporary '" +
                temporary.string() + "'");
        }
        output.write(
            bytes.data(),
            static_cast<std::streamsize>(
                bytes.size()));
        output.flush();
        if (!output) {
            throw std::runtime_error(
                "cannot write AQ19 artifact temporary '" +
                temporary.string() + "'");
        }
        output.close();
        if (!output) {
            throw std::runtime_error(
                "cannot close AQ19 artifact temporary '" +
                temporary.string() + "'");
        }

        std::error_code link_error;
        std::filesystem::create_hard_link(
            temporary, destination, link_error);
        if (link_error) {
            throw std::system_error(
                link_error,
                "cannot publish AQ19 artifact without "
                "replacement");
        }
        std::error_code remove_error;
        if (!std::filesystem::remove(
                temporary, remove_error) ||
            remove_error) {
            throw std::system_error(
                remove_error
                    ? remove_error
                    : std::make_error_code(
                          std::errc::io_error),
                "cannot remove published AQ19 artifact "
                "temporary");
        }
    } catch (...) {
        std::error_code cleanup_error;
        static_cast<void>(
            std::filesystem::remove(
                temporary, cleanup_error));
        throw;
    }
    return load(destination, contract).identity;
}

Loaded load(
    const std::filesystem::path& path,
    const Contract& expected_contract) {
    validate_contract(expected_contract);
    artifact_integrity::RegularFileSnapshot snapshot;
    const std::string bytes =
        read_stable_file(path, snapshot);
    Reader reader(bytes);
    if (reader.take(kMagic.size()) !=
            std::string_view(
                kMagic.data(), kMagic.size()) ||
        reader.u32() != kWireVersion ||
        reader.u32() != kEndianMarker) {
        fail("magic, version, or endian marker is wrong");
    }
    const std::uint64_t payload_size = reader.u64();
    const std::string parent(
        reader.take(kSha256Characters));
    const std::string parameter_sha256(
        reader.take(kSha256Characters));
    if (parent !=
            expected_contract.parent_fingerprint ||
        parameter_sha256 !=
            expected_contract.parameter_sha256) {
        fail("contract identity does not match");
    }
    if (payload_size >
            kMaximumArtifactBytes ||
        payload_size >
            std::numeric_limits<std::size_t>::max()) {
        fail("payload size is invalid");
    }
    const std::string_view payload =
        reader.take(
            static_cast<std::size_t>(
                payload_size));
    if (!reader.done()) {
        fail("file has trailing bytes");
    }
    const std::string actual_parameter_sha256 =
        artifact_integrity::sha256_string(payload);
    if (actual_parameter_sha256 !=
            parameter_sha256) {
        fail("parameter SHA-256 does not match payload");
    }
    const auto parameters =
        learned_priority_bilinear_parameters_from_canonical_bytes(
            payload);
    const auto residual =
        std::make_shared<
            const LearnedPriorityBilinear>(
            parameters);
    return {
        .residual = residual,
        .identity = {
            .bytes = snapshot.byte_size,
            .file_sha256 = snapshot.sha256,
            .parameter_sha256 =
                actual_parameter_sha256,
            .parent_fingerprint = parent,
        },
    };
}

} // namespace old_school::learned_priority_bilinear_artifact
