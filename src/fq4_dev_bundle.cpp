#include "old_school/fq4_dev_bundle.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/fq4_dev_schedule.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <unistd.h>
#include <utility>
#include <vector>

namespace old_school::fq4_dev_bundle {
namespace {

constexpr std::uint32_t kEndianMarker = 0x01020304U;
constexpr std::size_t kMaximumManifestBytes = 64U * 1024U;
constexpr std::size_t kMaximumCensusSectionBytes =
    2U * 1024U * 1024U;
constexpr std::size_t kMaximumRowsSectionBytes =
    62U * 1024U * 1024U;
constexpr std::size_t kMaximumPurposeBytes = 64;
constexpr std::size_t kMaximumRecipeBytes = 4096;
constexpr std::size_t kMaximumFeatureSchemaBytes = 256;
constexpr std::size_t kMaximumDescriptorBytes = 512;
constexpr std::uint8_t kKnownRoleBits =
    static_cast<std::uint8_t>(
        Role::DominancePositive |
        Role::BackgroundControl);

[[noreturn]] void fail(std::string_view message) {
    throw std::invalid_argument(
        "invalid FQ4 development bundle: " +
        std::string(message));
}

bool hash_is_zero(const Hash256& digest) {
    return std::all_of(
        digest.begin(), digest.end(),
        [](std::uint8_t byte) { return byte == 0; });
}

void require_nonzero_hash(
    const Hash256& digest, std::string_view context) {
    if (hash_is_zero(digest)) {
        fail(std::string(context) + " is zero");
    }
}

bool canonical_text(
    std::string_view text, std::size_t maximum) {
    return !text.empty() && text.size() <= maximum &&
           std::all_of(
               text.begin(), text.end(),
               [](unsigned char character) {
                   return character >= 0x20U &&
                          character <= 0x7eU;
               });
}

void require_finite_bits(
    std::uint64_t bits, std::string_view context) {
    if (!std::isfinite(std::bit_cast<double>(bits))) {
        fail(std::string(context) + " is nonfinite");
    }
}

class Writer {
  public:
    void u8(std::uint8_t value) {
        bytes_.push_back(static_cast<char>(value));
    }

    void u16(std::uint16_t value) {
        for (std::size_t index = 0; index < 2; ++index) {
            u8(static_cast<std::uint8_t>(
                (value >> (index * 8U)) & 0xffU));
        }
    }

    void u32(std::uint32_t value) {
        for (std::size_t index = 0; index < 4; ++index) {
            u8(static_cast<std::uint8_t>(
                (value >> (index * 8U)) & 0xffU));
        }
    }

    void u64(std::uint64_t value) {
        for (std::size_t index = 0; index < 8; ++index) {
            u8(static_cast<std::uint8_t>(
                (value >> (index * 8U)) & 0xffU));
        }
    }

    void hash(const Hash256& digest) {
        for (const std::uint8_t byte : digest) {
            u8(byte);
        }
    }

    void text(std::string_view value) {
        if (value.size() >
            std::numeric_limits<std::uint32_t>::max()) {
            throw std::length_error(
                "FQ4 development bundle string is too large");
        }
        u32(static_cast<std::uint32_t>(value.size()));
        raw(value);
    }

    void raw(std::string_view value) {
        if (bytes_.size() > kMaximumArtifactBytes ||
            value.size() >
            kMaximumArtifactBytes - bytes_.size()) {
            throw std::length_error(
                "FQ4 development bundle exceeds byte limit");
        }
        bytes_.append(value);
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

    std::uint16_t u16(std::string_view context) {
        std::uint16_t value = 0;
        for (std::size_t index = 0; index < 2; ++index) {
            value |= static_cast<std::uint16_t>(
                         u8(context))
                     << (index * 8U);
        }
        return value;
    }

    std::uint32_t u32(std::string_view context) {
        std::uint32_t value = 0;
        for (std::size_t index = 0; index < 4; ++index) {
            value |= static_cast<std::uint32_t>(
                         u8(context))
                     << (index * 8U);
        }
        return value;
    }

    std::uint64_t u64(std::string_view context) {
        std::uint64_t value = 0;
        for (std::size_t index = 0; index < 8; ++index) {
            value |= static_cast<std::uint64_t>(
                         u8(context))
                     << (index * 8U);
        }
        return value;
    }

    Hash256 hash(std::string_view context) {
        Hash256 result{};
        for (std::uint8_t& byte : result) {
            byte = u8(context);
        }
        return result;
    }

    std::string text(
        std::size_t maximum, std::string_view context) {
        const std::uint32_t size = u32(context);
        if (size > maximum) {
            fail(std::string(context) + " exceeds its byte limit");
        }
        const std::string_view value = view(size, context);
        return std::string(value);
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
        if (size > bytes_.size() - cursor_) {
            fail(std::string(context) + " is truncated");
        }
    }

    std::string_view bytes_;
    std::size_t cursor_ = 0;
};

std::string_view section_name(Section section) {
    switch (section) {
    case Section::Manifest:
        return "manifest";
    case Section::FitCensus:
        return "fit-census";
    case Section::FitRows:
        return "fit-rows";
    case Section::CheckCensus:
        return "check-census";
    case Section::CheckRows:
        return "check-rows";
    }
    fail("unknown section");
}

std::size_t maximum_section_bytes(Section section) {
    switch (section) {
    case Section::Manifest:
        return kMaximumManifestBytes;
    case Section::FitCensus:
    case Section::CheckCensus:
        return kMaximumCensusSectionBytes;
    case Section::FitRows:
    case Section::CheckRows:
        return kMaximumRowsSectionBytes;
    }
    fail("unknown section");
}

Hash256 domain_hash(
    std::string_view domain, std::string_view payload) {
    Writer framed;
    framed.text(kBundleSchema);
    framed.text(domain);
    framed.u64(static_cast<std::uint64_t>(payload.size()));
    framed.raw(payload);
    return sha256(framed.bytes());
}

Hash256 section_hash(
    Section section, std::string_view payload) {
    return domain_hash(
        std::string("section/") +
            std::string(section_name(section)),
        payload);
}

Hash256 payload_hash(
    const std::array<std::string, kSectionCount>& payloads) {
    Writer framed;
    framed.text(kBundleSchema);
    framed.text("payload");
    for (std::size_t index = 0;
         index < payloads.size(); ++index) {
        framed.u8(static_cast<std::uint8_t>(index));
        framed.u64(
            static_cast<std::uint64_t>(
                payloads[index].size()));
        framed.raw(payloads[index]);
    }
    return sha256(framed.bytes());
}

void write_split_manifest(
    Writer& output, const SplitManifest& split) {
    output.u64(split.source_seed_base);
    output.hash(split.schedule_sha256);
    output.hash(split.trajectory_sha256);
    output.hash(split.retained_sha256);
    output.hash(split.dominance_sha256);
    output.hash(split.selection_sha256);
    output.hash(split.scored_sha256);
    output.u32(split.census_rows);
    output.u32(split.selected_rows);
    for (const auto& counts :
         {split.census_by_deck,
          split.selected_by_deck,
          split.positive_by_deck,
          split.background_by_deck}) {
        for (const std::uint16_t count : counts) {
            output.u16(count);
        }
    }
}

SplitManifest read_split_manifest(
    Reader& input, std::string_view context) {
    SplitManifest result;
    result.source_seed_base =
        input.u64(std::string(context) + " source seed");
    result.schedule_sha256 =
        input.hash(std::string(context) + " schedule hash");
    result.trajectory_sha256 =
        input.hash(std::string(context) + " trajectory hash");
    result.retained_sha256 =
        input.hash(std::string(context) + " retained hash");
    result.dominance_sha256 =
        input.hash(std::string(context) + " dominance hash");
    result.selection_sha256 =
        input.hash(std::string(context) + " selection hash");
    result.scored_sha256 =
        input.hash(std::string(context) + " scored hash");
    result.census_rows =
        input.u32(std::string(context) + " census count");
    result.selected_rows =
        input.u32(std::string(context) + " selected count");
    for (auto* counts :
         {&result.census_by_deck,
          &result.selected_by_deck,
          &result.positive_by_deck,
          &result.background_by_deck}) {
        for (std::uint16_t& count : *counts) {
            count = input.u16(
                std::string(context) + " deck count");
        }
    }
    return result;
}

std::string encode_manifest(const Manifest& manifest) {
    Writer output;
    output.text(manifest.purpose);
    output.hash(manifest.producer_commit_sha256);
    output.hash(manifest.producer_executable_sha256);
    output.hash(manifest.parent_artifact_sha256);
    output.hash(manifest.parent_model_fingerprint);
    output.hash(manifest.parent_components.critic);
    output.hash(manifest.parent_components.priority);
    output.hash(manifest.parent_components.attack);
    output.hash(manifest.parent_components.block);
    output.hash(manifest.parent_components.damage_order);
    output.u64(manifest.generation_namespace);
    output.u64(manifest.hidden_namespace);
    output.u64(manifest.dominance_namespace);
    output.hash(manifest.collection_spec_sha256);
    output.text(manifest.production_recipe);
    output.text(manifest.feature_schema);
    output.u16(manifest.feature_count);
    output.hash(manifest.feature_contract_sha256);
    write_split_manifest(output, manifest.fit);
    write_split_manifest(output, manifest.check);
    return output.take();
}

Manifest decode_manifest(std::string_view payload) {
    Reader input(payload);
    Manifest result;
    result.purpose =
        input.text(kMaximumPurposeBytes, "manifest purpose");
    result.producer_commit_sha256 =
        input.hash("producer commit hash");
    result.producer_executable_sha256 =
        input.hash("producer executable hash");
    result.parent_artifact_sha256 =
        input.hash("parent artifact hash");
    result.parent_model_fingerprint =
        input.hash("parent model fingerprint");
    result.parent_components.critic =
        input.hash("parent critic fingerprint");
    result.parent_components.priority =
        input.hash("parent Priority fingerprint");
    result.parent_components.attack =
        input.hash("parent attack fingerprint");
    result.parent_components.block =
        input.hash("parent block fingerprint");
    result.parent_components.damage_order =
        input.hash("parent damage-order fingerprint");
    result.generation_namespace =
        input.u64("generation namespace");
    result.hidden_namespace =
        input.u64("hidden namespace");
    result.dominance_namespace =
        input.u64("dominance namespace");
    result.collection_spec_sha256 =
        input.hash("collection-spec hash");
    result.production_recipe =
        input.text(kMaximumRecipeBytes, "production recipe");
    result.feature_schema =
        input.text(
            kMaximumFeatureSchemaBytes,
            "feature schema");
    result.feature_count =
        input.u16("feature count");
    result.feature_contract_sha256 =
        input.hash("feature contract hash");
    result.fit = read_split_manifest(input, "FIT manifest");
    result.check =
        read_split_manifest(input, "CHECK manifest");
    if (!input.empty()) {
        fail("manifest section has trailing bytes");
    }
    return result;
}

void write_census_row(
    Writer& output, const CensusRow& row) {
    output.u16(row.schedule_index);
    output.u8(row.owner_seat);
    output.u32(row.trace_ordinal);
    output.u8(row.owner_deck);
    output.u8(row.opponent_deck);
    output.hash(row.stable_root_id);
    output.hash(row.physical_game_sha256);
    output.hash(row.information_action_sha256);
    output.hash(row.descriptor_set_sha256);
    output.u8(row.pass_index);
    if (row.dominance.size() >
        std::numeric_limits<std::uint8_t>::max()) {
        throw std::length_error(
            "FQ4 development census action count is too large");
    }
    output.u8(
        static_cast<std::uint8_t>(row.dominance.size()));
    for (const DominanceCount count : row.dominance) {
        output.u8(count.complete);
        output.u8(count.strict);
    }
}

CensusRow read_census_row(Reader& input) {
    CensusRow result;
    result.schedule_index =
        input.u16("census schedule index");
    result.owner_seat = input.u8("census owner seat");
    result.trace_ordinal =
        input.u32("census trace ordinal");
    result.owner_deck = input.u8("census owner deck");
    result.opponent_deck =
        input.u8("census opponent deck");
    result.stable_root_id =
        input.hash("census stable root ID");
    result.physical_game_sha256 =
        input.hash("census physical game ID");
    result.information_action_sha256 =
        input.hash("census information/action hash");
    result.descriptor_set_sha256 =
        input.hash("census descriptor-set hash");
    result.pass_index = input.u8("census Pass index");
    const std::uint8_t action_count =
        input.u8("census action count");
    if (action_count < 2 ||
        action_count > kMaximumActions) {
        fail("census action count is out of bounds");
    }
    result.dominance.reserve(action_count);
    for (std::size_t action = 0;
         action < action_count; ++action) {
        result.dominance.push_back({
            .complete =
                input.u8("census complete count"),
            .strict =
                input.u8("census strict count"),
        });
    }
    return result;
}

std::string encode_census(
    const std::vector<CensusRow>& rows) {
    Writer output;
    if (rows.size() >
        std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error(
            "FQ4 development census is too large");
    }
    output.u32(static_cast<std::uint32_t>(rows.size()));
    for (const CensusRow& row : rows) {
        write_census_row(output, row);
    }
    return output.take();
}

std::vector<CensusRow> decode_census(
    std::string_view payload) {
    Reader input(payload);
    const std::uint32_t count =
        input.u32("census row count");
    if (count > kMaximumCensusRowsPerSplit) {
        fail("census row count exceeds its bound");
    }
    std::vector<CensusRow> result;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        result.push_back(read_census_row(input));
    }
    if (!input.empty()) {
        fail("census section has trailing bytes");
    }
    return result;
}

void write_accounting(
    Writer& output, const ScoreAccounting& accounting) {
    output.u64(accounting.score_calls);
    output.u64(accounting.scored_actions);
    output.u64(accounting.sampled_worlds);
    output.u64(accounting.rollout_evaluations);
    output.u64(accounting.terminal_evaluations);
    output.u64(accounting.bootstrap_evaluations);
}

ScoreAccounting read_accounting(Reader& input) {
    return {
        .score_calls =
            input.u64("score-call accounting"),
        .scored_actions =
            input.u64("scored-action accounting"),
        .sampled_worlds =
            input.u64("sampled-world accounting"),
        .rollout_evaluations =
            input.u64("rollout-evaluation accounting"),
        .terminal_evaluations =
            input.u64("terminal-evaluation accounting"),
        .bootstrap_evaluations =
            input.u64("bootstrap-evaluation accounting"),
    };
}

void write_selected_row(
    Writer& output, const SelectedRow& row) {
    output.u8(static_cast<std::uint8_t>(row.split));
    write_census_row(output, row.census);
    output.u8(row.roles);
    output.u64(row.production_seed);
    write_accounting(output, row.accounting);
    if (row.actions.size() >
        std::numeric_limits<std::uint8_t>::max()) {
        throw std::length_error(
            "FQ4 development selected action count is too large");
    }
    output.u8(
        static_cast<std::uint8_t>(row.actions.size()));
    for (const ActionRow& action : row.actions) {
        output.text(action.descriptor);
        output.u8(action.is_pass ? 1 : 0);
        output.u8(action.dominance.complete);
        output.u8(action.dominance.strict);
        for (const std::uint64_t bits :
             action.raw_sample_bits) {
            output.u64(bits);
        }
        for (const std::uint64_t bits :
             action.shallow_prior_sample_bits) {
            output.u64(bits);
        }
        for (const std::uint64_t bits :
             action.continuation_sample_bits) {
            output.u64(bits);
        }
        output.u64(action.base_score_bits);
        output.u64(action.parent_residual_bits);
        if (action.features.size() >
            std::numeric_limits<std::uint16_t>::max()) {
            throw std::length_error(
                "FQ4 development sparse feature count is too large");
        }
        output.u16(
            static_cast<std::uint16_t>(
                action.features.size()));
        for (const SparseFeature feature :
             action.features) {
            output.u16(feature.index);
            output.u64(feature.value_bits);
        }
    }
}

SelectedRow read_selected_row(Reader& input) {
    SelectedRow result;
    const std::uint8_t split =
        input.u8("selected-row split");
    if (split > static_cast<std::uint8_t>(Split::Check)) {
        fail("selected-row split is invalid");
    }
    result.split = static_cast<Split>(split);
    result.census = read_census_row(input);
    result.roles = input.u8("selected-row role bits");
    result.production_seed =
        input.u64("selected-row production seed");
    result.accounting = read_accounting(input);
    const std::uint8_t action_count =
        input.u8("selected-row action count");
    if (action_count < 2 ||
        action_count > kMaximumActions) {
        fail("selected-row action count is out of bounds");
    }
    result.actions.reserve(action_count);
    for (std::size_t action_index = 0;
         action_index < action_count; ++action_index) {
        ActionRow action;
        action.descriptor =
            input.text(
                kMaximumDescriptorBytes,
                "action descriptor");
        const std::uint8_t is_pass =
            input.u8("typed Pass flag");
        if (is_pass > 1) {
            fail("typed Pass flag is not boolean");
        }
        action.is_pass = is_pass != 0;
        action.dominance = {
            .complete =
                input.u8("action complete count"),
            .strict =
                input.u8("action strict count"),
        };
        for (std::uint64_t& bits :
             action.raw_sample_bits) {
            bits = input.u64("raw sample bits");
        }
        for (std::uint64_t& bits :
             action.shallow_prior_sample_bits) {
            bits =
                input.u64("shallow-prior sample bits");
        }
        for (std::uint64_t& bits :
             action.continuation_sample_bits) {
            bits =
                input.u64("continuation sample bits");
        }
        action.base_score_bits =
            input.u64("base-score bits");
        action.parent_residual_bits =
            input.u64("parent-residual bits");
        const std::uint16_t feature_count =
            input.u16("sparse feature count");
        if (feature_count >
            kMaximumFeaturesPerAction) {
            fail("sparse feature count exceeds its bound");
        }
        action.features.reserve(feature_count);
        for (std::size_t feature = 0;
             feature < feature_count; ++feature) {
            action.features.push_back({
                .index =
                    input.u16("sparse feature index"),
                .value_bits =
                    input.u64("sparse feature value"),
            });
        }
        result.actions.push_back(std::move(action));
    }
    return result;
}

std::string encode_selected(
    const std::vector<SelectedRow>& rows) {
    Writer output;
    if (rows.size() >
        std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error(
            "FQ4 development selected rows are too large");
    }
    output.u32(static_cast<std::uint32_t>(rows.size()));
    for (const SelectedRow& row : rows) {
        write_selected_row(output, row);
    }
    return output.take();
}

std::vector<SelectedRow> decode_selected(
    std::string_view payload) {
    Reader input(payload);
    const std::uint32_t count =
        input.u32("selected-row count");
    if (count > kMaximumSelectedRowsPerSplit) {
        fail("selected-row count exceeds its bound");
    }
    std::vector<SelectedRow> result;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        result.push_back(read_selected_row(input));
    }
    if (!input.empty()) {
        fail("selected-row section has trailing bytes");
    }
    return result;
}

std::array<std::string, kSectionCount> encode_payloads(
    const Bundle& bundle) {
    return {
        encode_manifest(bundle.manifest),
        encode_census(bundle.fit_census),
        encode_selected(bundle.fit_rows),
        encode_census(bundle.check_census),
        encode_selected(bundle.check_rows),
    };
}

std::string encode_wire(const Bundle& bundle) {
    const auto payloads = encode_payloads(bundle);
    Writer output;
    output.text(kBundleSchema);
    output.u32(kEndianMarker);
    output.u8(static_cast<std::uint8_t>(kSectionCount));
    for (std::size_t index = 0;
         index < payloads.size(); ++index) {
        const auto section =
            static_cast<Section>(index);
        output.u8(static_cast<std::uint8_t>(section));
        output.u64(
            static_cast<std::uint64_t>(
                payloads[index].size()));
        output.hash(section_hash(section, payloads[index]));
        output.raw(payloads[index]);
    }
    output.hash(payload_hash(payloads));
    if (output.bytes().size() > kMaximumArtifactBytes) {
        throw std::length_error(
            "FQ4 development artifact exceeds its byte bound");
    }
    return output.take();
}

Bundle decode_wire(std::string_view bytes) {
    if (bytes.empty() ||
        bytes.size() > kMaximumArtifactBytes) {
        fail("artifact byte count is out of bounds");
    }
    Reader input(bytes);
    if (input.text(64, "bundle schema") != kBundleSchema) {
        fail("bundle schema is wrong");
    }
    if (input.u32("endian marker") != kEndianMarker) {
        fail("endian marker is wrong");
    }
    if (input.u8("section count") != kSectionCount) {
        fail("section count is wrong");
    }
    std::array<std::string, kSectionCount> payloads;
    for (std::size_t index = 0;
         index < payloads.size(); ++index) {
        const auto expected =
            static_cast<Section>(index);
        if (input.u8("section ID") !=
            static_cast<std::uint8_t>(expected)) {
            fail("section order or identity is wrong");
        }
        const std::uint64_t encoded_size =
            input.u64("section byte count");
        if (encoded_size >
                maximum_section_bytes(expected) ||
            encoded_size >
                std::numeric_limits<std::size_t>::max()) {
            fail("section byte count exceeds its bound");
        }
        const Hash256 expected_hash =
            input.hash("section domain hash");
        const std::string_view payload =
            input.view(
                static_cast<std::size_t>(encoded_size),
                "section payload");
        if (section_hash(expected, payload) !=
            expected_hash) {
            fail("section domain hash mismatch");
        }
        payloads[index] = std::string(payload);
    }
    const Hash256 expected_payload_hash =
        input.hash("payload hash");
    if (!input.empty()) {
        fail("artifact has trailing bytes");
    }
    if (payload_hash(payloads) !=
        expected_payload_hash) {
        fail("payload hash mismatch");
    }

    Bundle result;
    result.manifest = decode_manifest(payloads[0]);
    result.fit_census = decode_census(payloads[1]);
    result.fit_rows = decode_selected(payloads[2]);
    result.check_census = decode_census(payloads[3]);
    result.check_rows = decode_selected(payloads[4]);
    return result;
}

auto census_key(const CensusRow& row) {
    return std::tuple{
        row.schedule_index,
        row.owner_seat,
        row.trace_ordinal,
        row.stable_root_id,
    };
}

auto selected_key(const SelectedRow& row) {
    return std::tuple{
        row.census.owner_deck,
        row.census.schedule_index,
        row.census.owner_seat,
        row.census.trace_ordinal,
        row.census.stable_root_id,
    };
}

bool dominance_positive(const CensusRow& row) {
    for (std::size_t action = 0;
         action < row.dominance.size(); ++action) {
        if (action != row.pass_index &&
            row.dominance[action].complete == kWorldCount &&
            row.dominance[action].strict == kWorldCount) {
            return true;
        }
    }
    return false;
}

bool high_confidence_parent_error(
    const SelectedRow& row) {
    std::optional<std::size_t> best_dominated;
    std::optional<std::size_t> best_nondominated;
    std::vector<double> combined;
    combined.reserve(row.actions.size());
    for (std::size_t action = 0;
         action < row.actions.size(); ++action) {
        const ActionRow& candidate = row.actions[action];
        const double score =
            std::bit_cast<double>(
                candidate.base_score_bits) +
            std::bit_cast<double>(
                candidate.parent_residual_bits);
        combined.push_back(score);
        const bool robustly_dominated =
            action != row.census.pass_index &&
            candidate.dominance.complete ==
                kWorldCount &&
            candidate.dominance.strict ==
                kWorldCount;
        std::optional<std::size_t>& best =
            robustly_dominated
                ? best_dominated
                : best_nondominated;
        if (!best.has_value() ||
            score > combined[*best]) {
            best = action;
        }
    }
    if (!best_dominated.has_value() ||
        !best_nondominated.has_value()) {
        return false;
    }
    const double margin =
        combined[*best_dominated] -
        combined[*best_nondominated];
    if (!(margin > 0.0)) {
        return false;
    }
    const double residual_difference =
        std::bit_cast<double>(
            row.actions[*best_dominated]
                .parent_residual_bits) -
        std::bit_cast<double>(
            row.actions[*best_nondominated]
                .parent_residual_bits);
    std::array<double, kWorldCount> differences{};
    double mean = 0.0;
    for (std::size_t world = 0;
         world < kWorldCount; ++world) {
        differences[world] =
            std::bit_cast<double>(
                row.actions[*best_dominated]
                    .raw_sample_bits[world]) -
            std::bit_cast<double>(
                row.actions[*best_nondominated]
                    .raw_sample_bits[world]) +
            residual_difference;
        mean += differences[world];
    }
    mean /= static_cast<double>(kWorldCount);
    double squared = 0.0;
    for (const double difference : differences) {
        const double centered = difference - mean;
        squared += centered * centered;
    }
    const double standard_error =
        std::sqrt(
            squared /
            static_cast<double>(
                kWorldCount *
                (kWorldCount - 1)));
    return standard_error == 0.0 ||
           margin / standard_error >= 3.0;
}

// Reproduce learned_iteration::evenly_spaced_retained_indices locally so
// the strict codec remains a small, game-free binary. This is the same
// overflow-safe floor-boundary algorithm frozen by the DEV0 declaration.
std::vector<std::size_t> evenly_spaced_indices(
    std::size_t total, std::size_t cap) {
    if (cap == 0) {
        fail("selection cap is zero");
    }
    std::vector<std::size_t> retained;
    retained.reserve(std::min(total, cap));
    if (total <= cap) {
        for (std::size_t index = 0;
             index < total; ++index) {
            retained.push_back(index);
        }
        return retained;
    }
    const std::size_t wrap_threshold = total - cap;
    std::size_t phase = 0;
    for (std::size_t index = 0;
         index < total; ++index) {
        if (phase >= wrap_threshold) {
            retained.push_back(index);
            phase -= wrap_threshold;
        } else {
            phase += cap;
        }
    }
    if (retained.size() != cap) {
        fail("selection spacing count is inconsistent");
    }
    return retained;
}

std::uint64_t mix_seed(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value =
        (value ^ (value >> 30)) *
        0xbf58476d1ce4e5b9ULL;
    value =
        (value ^ (value >> 27)) *
        0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

std::uint64_t fixed_seed_base(Split split) {
    switch (split) {
    case Split::Fit:
        return kFitSeedBase;
    case Split::Check:
        return kCheckSeedBase;
    }
    fail("split is invalid");
}

std::uint64_t source_game_seed(
    Split split, std::uint16_t schedule_index) {
    constexpr std::uint64_t kSelfPlayGameDomain =
        0x53454c46504c4159ULL;
    std::uint64_t seed = mix_seed(
        fixed_seed_base(split) ^
        mix_seed(kSelfPlayGameDomain));
    seed = mix_seed(
        seed ^
        mix_seed(
            kGenerationNamespace ^
            0xd1b54a32d192ed03ULL));
    seed = mix_seed(
        seed ^
        mix_seed(
            std::uint64_t{0} ^
            0x94d049bb133111ebULL));
    return mix_seed(
        seed ^
        mix_seed(
            static_cast<std::uint64_t>(
                schedule_index) ^
            0xbf58476d1ce4e5b9ULL));
}

std::array<std::uint8_t, 2> expected_seat_decks(
    std::uint16_t schedule_index) {
    constexpr std::array<std::array<std::uint8_t, 2>, 10>
        pairings{{
            {0, 1},
            {0, 2},
            {0, 3},
            {0, 4},
            {1, 2},
            {1, 3},
            {1, 4},
            {2, 3},
            {2, 4},
            {3, 4},
        }};
    if (schedule_index >= 40) {
        fail("census schedule index is out of range");
    }
    const std::size_t pairing = schedule_index / 4;
    const std::size_t orientation =
        (schedule_index % 4) / 2;
    return orientation == 0
               ? pairings[pairing]
               : std::array<std::uint8_t, 2>{
                     pairings[pairing][1],
                     pairings[pairing][0],
                 };
}

void validate_census_row(const CensusRow& row) {
    if (row.schedule_index >= 40) {
        fail("census schedule index is out of range");
    }
    if (row.owner_seat >= 2) {
        fail("census owner seat is out of range");
    }
    if (row.owner_deck >= kDeckCount ||
        row.opponent_deck >= kDeckCount ||
        row.owner_deck == row.opponent_deck) {
        fail("census deck IDs are invalid");
    }
    const auto seats =
        expected_seat_decks(row.schedule_index);
    if (row.owner_deck != seats[row.owner_seat] ||
        row.opponent_deck !=
            seats[1U - row.owner_seat]) {
        fail("census deck IDs disagree with the frozen schedule");
    }
    require_nonzero_hash(
        row.stable_root_id, "stable root ID");
    require_nonzero_hash(
        row.physical_game_sha256, "physical game digest");
    require_nonzero_hash(
        row.information_action_sha256,
        "information/action digest");
    require_nonzero_hash(
        row.descriptor_set_sha256,
        "descriptor-set digest");
    if (row.dominance.size() < 2 ||
        row.dominance.size() > kMaximumActions ||
        row.pass_index >= row.dominance.size()) {
        fail("census action or Pass count is invalid");
    }
    for (const DominanceCount count : row.dominance) {
        if (count.complete > kWorldCount ||
            count.strict > count.complete) {
            fail("dominance counts are invalid");
        }
    }
    const DominanceCount pass =
        row.dominance[row.pass_index];
    if (pass.complete != 0 || pass.strict != 0) {
        fail("typed Pass dominance counts are invalid");
    }
}

struct SplitStatistics {
    std::array<std::uint16_t, kDeckCount> census{};
    std::array<std::uint16_t, kDeckCount> selected{};
    std::array<std::uint16_t, kDeckCount> positive{};
    std::array<std::uint16_t, kDeckCount> background{};
};

SplitStatistics validate_split(
    Split split,
    const std::vector<CensusRow>& census,
    const std::vector<SelectedRow>& selected,
    std::set<Hash256>& global_roots,
    std::set<Hash256>& global_selected,
    std::map<Hash256, std::pair<Split, std::uint16_t>>&
        physical_games) {
    if (census.empty() ||
        census.size() > kMaximumCensusRowsPerSplit) {
        fail("split census size is invalid");
    }
    if (selected.empty() ||
        selected.size() >
            kMaximumSelectedRowsPerSplit) {
        fail("split selected-row size is invalid");
    }

    SplitStatistics statistics;
    std::map<Hash256, const CensusRow*> census_by_id;
    std::set<std::tuple<std::uint16_t, std::uint8_t, std::uint32_t>>
        public_locators;
    std::map<std::uint16_t, Hash256> game_ids_by_schedule;
    std::map<std::pair<std::uint16_t, std::uint8_t>, std::size_t>
        owner_perspective_counts;
    std::array<const CensusRow*, kDeckCount> first_by_deck{};
    std::array<std::vector<const CensusRow*>, kDeckCount>
        chronological_by_deck;
    for (std::size_t index = 0;
         index < census.size(); ++index) {
        const CensusRow& row = census[index];
        validate_census_row(row);
        if (row.physical_game_sha256 !=
                expected_physical_game_sha256(
                    split, row.schedule_index) ||
            row.stable_root_id !=
                expected_stable_root_sha256(
                    split, row.schedule_index,
                    row.owner_seat,
                    row.trace_ordinal,
                    row.information_action_sha256)) {
            fail("census root is not bound to its frozen split schedule");
        }
        if (index != 0 &&
            !(census_key(census[index - 1]) <
              census_key(row))) {
            fail("census rows are not in canonical order");
        }
        if (!global_roots.insert(row.stable_root_id).second ||
            !census_by_id.emplace(
                row.stable_root_id, &row)
                 .second) {
            fail("duplicate stable root ID");
        }
        if (!public_locators
                 .emplace(
                     row.schedule_index, row.owner_seat,
                     row.trace_ordinal)
                 .second) {
            fail("duplicate public root locator");
        }
        const std::size_t perspective_count =
            ++owner_perspective_counts[
                {row.schedule_index, row.owner_seat}];
        if (perspective_count >
            kMaximumCensusRowsPerOwnerPerspective) {
            fail("owner perspective exceeds its retained-root cap");
        }
        const auto [schedule_game, schedule_inserted] =
            game_ids_by_schedule.emplace(
                row.schedule_index, row.physical_game_sha256);
        if (!schedule_inserted &&
            schedule_game->second !=
                row.physical_game_sha256) {
            fail("one schedule row has multiple physical game IDs");
        }
        const auto [game, inserted] =
            physical_games.emplace(
                row.physical_game_sha256,
                std::pair{split, row.schedule_index});
        if (!inserted &&
            game->second !=
                std::pair{split, row.schedule_index}) {
            fail("physical game ID crosses source games");
        }
        const std::size_t deck = row.owner_deck;
        if (statistics.census[deck] ==
            std::numeric_limits<std::uint16_t>::max()) {
            fail("per-deck census count overflows");
        }
        ++statistics.census[deck];
        if (statistics.census[deck] >
            kMaximumCensusRowsPerDeck) {
            fail("owner deck exceeds its retained-root cap");
        }
        if (first_by_deck[deck] == nullptr) {
            first_by_deck[deck] = &row;
        }
        chronological_by_deck[deck].push_back(&row);
    }

    std::map<Hash256, std::uint8_t> expected_roles;
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const auto& rows = chronological_by_deck[deck];
        if (rows.empty()) {
            fail("split is missing a deck census");
        }
        const CensusRow* const background = rows.front();
        std::uint8_t background_roles =
            static_cast<std::uint8_t>(
                Role::BackgroundControl);
        if (dominance_positive(*background)) {
            background_roles |=
                static_cast<std::uint8_t>(
                    Role::DominancePositive);
        }
        expected_roles.emplace(
            background->stable_root_id,
            background_roles);

        std::vector<const CensusRow*> positives;
        for (std::size_t index = 1;
             index < rows.size(); ++index) {
            if (dominance_positive(*rows[index])) {
                positives.push_back(rows[index]);
            }
        }
        for (const std::size_t position :
             evenly_spaced_indices(
                 positives.size(), 15)) {
            expected_roles.emplace(
                positives[position]->stable_root_id,
                static_cast<std::uint8_t>(
                    Role::DominancePositive));
        }
    }

    std::array<std::size_t, kDeckCount>
        nonbackground_positive{};
    std::set<Hash256> seen_expected;
    std::size_t high_confidence_roots = 0;
    std::set<Hash256> high_confidence_games;
    std::set<std::uint8_t> high_confidence_decks;
    for (std::size_t index = 0;
         index < selected.size(); ++index) {
        const SelectedRow& row = selected[index];
        if (row.split != split) {
            fail("selected-row split disagrees with its section");
        }
        validate_census_row(row.census);
        if (index != 0 &&
            !(selected_key(selected[index - 1]) <
              selected_key(row))) {
            fail("selected rows are not in canonical deck/root order");
        }
        if ((row.roles & ~kKnownRoleBits) != 0 ||
            row.roles == 0) {
            fail("selected-row role bits are invalid");
        }
        const auto expected =
            expected_roles.find(
                row.census.stable_root_id);
        if (expected == expected_roles.end() ||
            expected->second != row.roles ||
            !seen_expected.insert(
                 row.census.stable_root_id)
                 .second) {
            fail("selected row disagrees with frozen blind selection");
        }
        const auto census_match =
            census_by_id.find(row.census.stable_root_id);
        if (census_match == census_by_id.end() ||
            *census_match->second != row.census) {
            fail("selected row does not exactly match its census row");
        }
        if (!global_selected
                 .insert(row.census.stable_root_id)
                 .second) {
            fail("duplicate selected root ID");
        }
        if (row.actions.size() !=
            row.census.dominance.size()) {
            fail("selected action count drifted from census");
        }
        const bool positive =
            dominance_positive(row.census);
        const bool positive_role =
            (row.roles &
             static_cast<std::uint8_t>(
                 Role::DominancePositive)) != 0;
        const bool background_role =
            (row.roles &
             static_cast<std::uint8_t>(
                 Role::BackgroundControl)) != 0;
        if (positive != positive_role) {
            fail("positive role disagrees with robust dominance");
        }
        if (!positive_role && !background_role) {
            fail("selected row is neither positive nor background");
        }

        std::vector<std::string> descriptors;
        descriptors.reserve(row.actions.size());
        std::size_t pass_count = 0;
        for (std::size_t action_index = 0;
             action_index < row.actions.size();
             ++action_index) {
            const ActionRow& action =
                row.actions[action_index];
            if (!canonical_text(
                    action.descriptor,
                    kMaximumDescriptorBytes)) {
                fail("action descriptor is not canonical text");
            }
            if (action_index != 0 &&
                !(row.actions[action_index - 1].descriptor <
                  action.descriptor)) {
                fail("action descriptors are not strictly ordered");
            }
            if (action.is_pass) {
                ++pass_count;
                if (action_index != row.census.pass_index) {
                    fail("typed Pass flag disagrees with Pass index");
                }
            }
            if (action.dominance !=
                row.census.dominance[action_index]) {
                fail("selected dominance counts drifted from census");
            }
            double shallow_mean = 0.0;
            for (std::size_t world = 0;
                 world < kWorldCount; ++world) {
                const std::uint64_t raw_bits =
                    action.raw_sample_bits[world];
                const std::uint64_t shallow_bits =
                    action.shallow_prior_sample_bits[world];
                const std::uint64_t continuation_bits =
                    action.continuation_sample_bits[world];
                require_finite_bits(raw_bits, "raw sample");
                require_finite_bits(
                    shallow_bits,
                    "shallow-prior sample");
                require_finite_bits(
                    continuation_bits,
                    "continuation sample");
                const double raw =
                    std::bit_cast<double>(raw_bits);
                const double shallow =
                    std::bit_cast<double>(shallow_bits);
                const double continuation =
                    std::bit_cast<double>(
                        continuation_bits);
                if (raw < 0.0 || raw > 1.0 ||
                    shallow < 0.0 || shallow > 1.0 ||
                    continuation < 0.0 ||
                    continuation > 1.0) {
                    fail(
                        "production score sample is outside "
                        "[0, 1]");
                }
                const double expected_raw =
                    (shallow +
                     static_cast<double>(kWorldCount) *
                         continuation) /
                    static_cast<double>(
                        kWorldCount + 1U);
                if (raw_bits !=
                    std::bit_cast<std::uint64_t>(
                        expected_raw)) {
                    fail(
                        "raw sample does not reproduce its "
                        "shallow/continuation blend");
                }
                shallow_mean += shallow;
            }
            shallow_mean /=
                static_cast<double>(kWorldCount);
            double reconstructed_base = shallow_mean;
            for (const std::uint64_t bits :
                 action.continuation_sample_bits) {
                reconstructed_base +=
                    std::bit_cast<double>(bits);
            }
            reconstructed_base /=
                static_cast<double>(kWorldCount + 1U);
            require_finite_bits(
                action.base_score_bits, "base score");
            const double base_score =
                std::bit_cast<double>(
                    action.base_score_bits);
            if (base_score < 0.0 || base_score > 1.0) {
                fail("base score is outside [0, 1]");
            }
            if (action.base_score_bits !=
                std::bit_cast<std::uint64_t>(
                    reconstructed_base)) {
                fail(
                    "base score does not reproduce deployed "
                    "shallow/continuation aggregation");
            }
            require_finite_bits(
                action.parent_residual_bits,
                "parent residual");
            if (std::abs(
                    std::bit_cast<double>(
                        action.parent_residual_bits)) >
                0.10) {
                fail("parent residual is outside [-0.10, 0.10]");
            }
            if (action.features.size() >
                kMaximumFeaturesPerAction) {
                fail("sparse feature count exceeds its bound");
            }
            for (std::size_t feature = 0;
                 feature < action.features.size();
                 ++feature) {
                const SparseFeature value =
                    action.features[feature];
                if (value.index >= kFeatureCount) {
                    fail("sparse feature index is out of range");
                }
                if (feature != 0 &&
                    action.features[feature - 1].index >=
                        value.index) {
                    fail("sparse feature indices are not increasing");
                }
                if (value.value_bits == 0) {
                    fail("sparse features must omit positive zero");
                }
                require_finite_bits(
                    value.value_bits,
                    "sparse feature value");
            }
            descriptors.push_back(action.descriptor);
        }
        if (pass_count != 1) {
            fail("selected row does not have exactly one typed Pass");
        }
        if (descriptor_set_sha256(descriptors) !=
            row.census.descriptor_set_sha256) {
            fail("descriptor-set digest mismatch");
        }

        const std::uint64_t action_count =
            row.actions.size();
        const std::uint64_t expected_evaluations =
            action_count * kWorldCount;
        if (row.accounting.score_calls != 1 ||
            row.accounting.scored_actions != action_count ||
            row.accounting.sampled_worlds != kWorldCount ||
            row.accounting.rollout_evaluations !=
                expected_evaluations ||
            row.accounting.terminal_evaluations >
                expected_evaluations ||
            row.accounting.bootstrap_evaluations !=
                expected_evaluations -
                    row.accounting.terminal_evaluations) {
            fail("selected-row production accounting is inconsistent");
        }
        if (high_confidence_parent_error(row)) {
            ++high_confidence_roots;
            high_confidence_games.insert(
                row.census.physical_game_sha256);
            high_confidence_decks.insert(
                row.census.owner_deck);
        }

        const std::size_t deck = row.census.owner_deck;
        ++statistics.selected[deck];
        if (positive_role) {
            ++statistics.positive[deck];
        }
        if (background_role) {
            ++statistics.background[deck];
            if (first_by_deck[deck] == nullptr ||
                first_by_deck[deck]->stable_root_id !=
                    row.census.stable_root_id) {
                fail("background row is not the deck's first retained root");
            }
        } else if (positive_role) {
            ++nonbackground_positive[deck];
        }
    }
    if (seen_expected.size() !=
        expected_roles.size()) {
        fail("frozen blind selection omitted a required row");
    }
    if (high_confidence_roots < 5 ||
        high_confidence_games.size() < 5 ||
        high_confidence_decks.size() < 2) {
        fail("split misses the 5/5/2 parent-error support floor");
    }

    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        if (statistics.census[deck] == 0 ||
            statistics.selected[deck] == 0 ||
            statistics.positive[deck] == 0 ||
            statistics.background[deck] != 1 ||
            statistics.selected[deck] >
                kMaximumRowsPerDeckAndSplit ||
            statistics.positive[deck] >
                kMaximumRowsPerDeckAndSplit ||
            nonbackground_positive[deck] > 15) {
            fail("per-deck split coverage or row cap is invalid");
        }
    }
    return statistics;
}

void require_split_manifest(
    const SplitManifest& manifest,
    const SplitStatistics& statistics,
    std::size_t census_size,
    std::size_t selected_size,
    Split split) {
    const bool fit = split == Split::Fit;
    const std::uint64_t expected_seed =
        fit ? kFitSeedBase : kCheckSeedBase;
    const std::string_view expected_schedule =
        fit
            ? fq4_dev_schedule::
                  kExpectedFitScheduleSha256
            : fq4_dev_schedule::
                  kExpectedCheckScheduleSha256;
    if (manifest.source_seed_base != expected_seed ||
        manifest.schedule_sha256 !=
            parse_sha256(expected_schedule)) {
        fail("split source seed or schedule hash drifted");
    }
    for (const auto& [digest, name] :
         std::array{
             std::pair{
                 &manifest.trajectory_sha256,
                 "trajectory"},
             std::pair{
                 &manifest.retained_sha256,
                 "retained"},
             std::pair{
                 &manifest.dominance_sha256,
                 "dominance"},
             std::pair{
                 &manifest.selection_sha256,
                 "selection"},
             std::pair{
                 &manifest.scored_sha256,
                 "scored"},
         }) {
        require_nonzero_hash(
            *digest,
            std::string("split ") + name + " hash");
    }
    if (manifest.census_rows != census_size ||
        manifest.selected_rows != selected_size ||
        manifest.census_by_deck != statistics.census ||
        manifest.selected_by_deck != statistics.selected ||
        manifest.positive_by_deck != statistics.positive ||
        manifest.background_by_deck !=
            statistics.background) {
        fail("manifest split counts drifted from sections");
    }
}

std::string read_exact_file(
    const std::filesystem::path& path,
    std::size_t expected_size) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(
            "cannot open FQ4 development artifact");
    }
    std::string bytes(expected_size, '\0');
    if (expected_size != 0) {
        input.read(
            bytes.data(),
            static_cast<std::streamsize>(expected_size));
    }
    if (!input ||
        input.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error(
            "cannot read exact FQ4 development artifact bytes");
    }
    return bytes;
}

Bundle load_from_impl(
    const std::filesystem::path& path,
    const PublishedArtifactExpectation& expectation) {
    if (expectation.byte_size == 0 ||
        expectation.byte_size > kMaximumArtifactBytes ||
        format_sha256(parse_sha256(expectation.sha256)) !=
            expectation.sha256) {
        throw std::invalid_argument(
            "invalid published FQ4 artifact expectation");
    }
    const auto before =
        artifact_integrity::snapshot_regular_file(path);
    if (before.byte_size != expectation.byte_size ||
        before.sha256 != expectation.sha256) {
        throw std::runtime_error(
            "published FQ4 artifact identity mismatch");
    }
    const std::string bytes =
        read_exact_file(path, expectation.byte_size);
    const auto after =
        artifact_integrity::snapshot_regular_file(path);
    if (before != after ||
        sha256(bytes) !=
            parse_sha256(expectation.sha256)) {
        throw std::runtime_error(
            "published FQ4 artifact changed while loading");
    }
    return decode(bytes);
}

class TemporaryPublication {
  public:
    explicit TemporaryPublication(
        std::filesystem::path path)
        : path_(std::move(path)) {}

    TemporaryPublication(const TemporaryPublication&) = delete;
    TemporaryPublication& operator=(
        const TemporaryPublication&) = delete;

    ~TemporaryPublication() {
        if (!path_.empty()) {
            ::unlink(path_.c_str());
        }
    }

    const std::filesystem::path& path() const {
        return path_;
    }

    void release() {
        path_.clear();
    }

  private:
    std::filesystem::path path_;
};

void throw_errno(
    std::string_view operation,
    const std::filesystem::path& path) {
    throw std::system_error(
        errno, std::generic_category(),
        std::string(operation) + " '" + path.string() + "'");
}

void close_checked(
    int descriptor, const std::filesystem::path& path) {
    if (::close(descriptor) != 0) {
        throw_errno("cannot close", path);
    }
}

void publish_at(
    const std::filesystem::path& path,
    const Bundle& bundle) {
    if (path.empty() || path.filename().empty()) {
        throw std::invalid_argument(
            "FQ4 publication path is empty");
    }
    const std::string bytes = encode(bundle);
    const std::filesystem::path parent =
        path.has_parent_path()
            ? path.parent_path()
            : std::filesystem::path(".");
    std::error_code status_error;
    if (!std::filesystem::is_directory(parent, status_error) ||
        status_error) {
        throw std::runtime_error(
            "FQ4 publication parent is not a directory");
    }

    static std::atomic<std::uint64_t> sequence{0};
    int descriptor = -1;
    std::filesystem::path temporary_path;
    for (std::size_t attempt = 0;
         attempt < 64 && descriptor < 0; ++attempt) {
        const std::uint64_t current_sequence =
            sequence.fetch_add(1, std::memory_order_relaxed) + 1;
        temporary_path =
            parent /
            ("." + path.filename().string() +
             ".tmp." +
             std::to_string(
                 static_cast<std::uint64_t>(::getpid())) +
             "." + std::to_string(current_sequence));
        int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
        descriptor =
            ::open(temporary_path.c_str(), flags, 0644);
        if (descriptor < 0 && errno != EEXIST &&
            errno != EINTR) {
            throw_errno(
                "cannot create publication temporary",
                temporary_path);
        }
    }
    if (descriptor < 0) {
        throw std::runtime_error(
            "cannot allocate publication temporary path");
    }
    TemporaryPublication temporary(temporary_path);

    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count =
            ::write(
                descriptor, bytes.data() + offset,
                bytes.size() - offset);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            const int saved = errno;
            ::close(descriptor);
            errno = saved;
            throw_errno(
                "cannot write publication temporary",
                temporary_path);
        }
        if (count == 0) {
            ::close(descriptor);
            throw std::runtime_error(
                "zero-length FQ4 publication write");
        }
        offset += static_cast<std::size_t>(count);
    }
    if (::fsync(descriptor) != 0) {
        const int saved = errno;
        ::close(descriptor);
        errno = saved;
        throw_errno(
            "cannot sync publication temporary",
            temporary_path);
    }
    close_checked(descriptor, temporary_path);

    if (::link(temporary_path.c_str(), path.c_str()) != 0) {
        throw_errno(
            "cannot publish without replacement", path);
    }
    if (::unlink(temporary_path.c_str()) != 0) {
        throw_errno(
            "cannot remove publication temporary",
            temporary_path);
    }
    temporary.release();

    const auto published =
        artifact_integrity::snapshot_regular_file(path);
    if (published.byte_size != bytes.size() ||
        published.sha256 !=
            artifact_integrity::sha256_string(bytes)) {
        throw std::runtime_error(
            "published FQ4 artifact failed identity check");
    }
}

} // namespace

std::string format_sha256(const Hash256& digest) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string result(64, '0');
    for (std::size_t index = 0;
         index < digest.size(); ++index) {
        result[index * 2] =
            kHex[(digest[index] >> 4U) & 0x0fU];
        result[index * 2 + 1] =
            kHex[digest[index] & 0x0fU];
    }
    return result;
}

Hash256 parse_sha256(std::string_view hexadecimal) {
    if (hexadecimal.size() != 64) {
        fail("SHA-256 text has wrong length");
    }
    auto nibble = [](char character) -> std::uint8_t {
        if (character >= '0' && character <= '9') {
            return static_cast<std::uint8_t>(
                character - '0');
        }
        if (character >= 'a' && character <= 'f') {
            return static_cast<std::uint8_t>(
                character - 'a' + 10);
        }
        fail("SHA-256 text is not canonical lowercase hexadecimal");
    };
    Hash256 result{};
    for (std::size_t index = 0;
         index < result.size(); ++index) {
        result[index] =
            static_cast<std::uint8_t>(
                (nibble(hexadecimal[index * 2]) << 4U) |
                nibble(hexadecimal[index * 2 + 1]));
    }
    return result;
}

Hash256 sha256(std::string_view bytes) {
    return parse_sha256(
        artifact_integrity::sha256_string(bytes));
}

Hash256 expected_physical_game_sha256(
    Split split, std::uint16_t schedule_index) {
    const std::uint64_t seed_base =
        fixed_seed_base(split);
    return sha256(
        "source_seed_base=" +
        std::to_string(seed_base) +
        "\nschedule_index=" +
        std::to_string(schedule_index) +
        "\n");
}

Hash256 expected_stable_root_sha256(
    Split split, std::uint16_t schedule_index,
    std::uint8_t owner_seat,
    std::uint32_t trace_ordinal,
    const Hash256& information_action_sha256) {
    const std::uint64_t seed_base =
        fixed_seed_base(split);
    std::string key(kStableRootSchema);
    key +=
        "\nsource_seed_base_index=0"
        "\nsource_seed_base=" +
        std::to_string(seed_base) +
        "\nschedule_index=" +
        std::to_string(schedule_index) +
        "\ngame_seed=" +
        std::to_string(
            source_game_seed(
                split, schedule_index)) +
        "\nowner=" +
        std::to_string(owner_seat) +
        "\ntrace=" +
        std::to_string(trace_ordinal) +
        "\ninformation_action_sha256=" +
        format_sha256(
            information_action_sha256) +
        "\n";
    return sha256(key);
}

Hash256 descriptor_set_sha256(
    const std::vector<std::string>& descriptors) {
    Writer canonical;
    canonical.text(kBundleSchema);
    canonical.text("descriptor-set");
    canonical.u32(
        static_cast<std::uint32_t>(
            descriptors.size()));
    for (const std::string& descriptor : descriptors) {
        canonical.text(descriptor);
    }
    return sha256(canonical.bytes());
}

void validate(const Bundle& bundle) {
    const Manifest& manifest = bundle.manifest;
    if (manifest.purpose != kPurpose ||
        manifest.production_recipe != kProductionRecipe ||
        manifest.feature_schema != kFeatureSchema ||
        manifest.feature_count != kFeatureCount ||
        manifest.generation_namespace !=
            kGenerationNamespace ||
        manifest.hidden_namespace != kHiddenNamespace ||
        manifest.dominance_namespace !=
            kDominanceNamespace) {
        fail("manifest fixed semantics drifted");
    }
    require_nonzero_hash(
        manifest.producer_commit_sha256,
        "producer commit hash");
    require_nonzero_hash(
        manifest.producer_executable_sha256,
        "producer executable hash");
    require_nonzero_hash(
        manifest.parent_artifact_sha256,
        "parent artifact hash");
    require_nonzero_hash(
        manifest.parent_model_fingerprint,
        "parent model fingerprint");
    require_nonzero_hash(
        manifest.parent_components.critic,
        "parent critic fingerprint");
    require_nonzero_hash(
        manifest.parent_components.priority,
        "parent Priority fingerprint");
    require_nonzero_hash(
        manifest.parent_components.attack,
        "parent attack fingerprint");
    require_nonzero_hash(
        manifest.parent_components.block,
        "parent block fingerprint");
    require_nonzero_hash(
        manifest.parent_components.damage_order,
        "parent damage-order fingerprint");
    require_nonzero_hash(
        manifest.feature_contract_sha256,
        "feature-contract hash");
    require_nonzero_hash(
        manifest.collection_spec_sha256,
        "collection-spec hash");
    if (manifest.parent_artifact_sha256 !=
            parse_sha256(kParentArtifactSha256) ||
        manifest.parent_model_fingerprint !=
            parse_sha256(kParentModelFingerprint) ||
        manifest.parent_components.critic !=
            parse_sha256(kParentCriticFingerprint) ||
        manifest.parent_components.priority !=
            parse_sha256(kParentPriorityFingerprint) ||
        manifest.parent_components.attack !=
            parse_sha256(kParentAttackFingerprint) ||
        manifest.parent_components.block !=
            parse_sha256(kParentBlockFingerprint) ||
        manifest.parent_components.damage_order !=
            parse_sha256(
                kParentDamageOrderFingerprint) ||
        manifest.collection_spec_sha256 !=
            parse_sha256(kCollectionSpecSha256) ||
        manifest.feature_contract_sha256 !=
            parse_sha256(kFeatureContractSha256)) {
        fail("frozen C16 identity drifted");
    }

    std::set<Hash256> global_roots;
    std::set<Hash256> global_selected;
    std::map<
        Hash256, std::pair<Split, std::uint16_t>>
        physical_games;
    const SplitStatistics fit =
        validate_split(
            Split::Fit, bundle.fit_census,
            bundle.fit_rows, global_roots,
            global_selected, physical_games);
    const SplitStatistics check =
        validate_split(
            Split::Check, bundle.check_census,
            bundle.check_rows, global_roots,
            global_selected, physical_games);
    require_split_manifest(
        manifest.fit, fit,
        bundle.fit_census.size(),
        bundle.fit_rows.size(), Split::Fit);
    require_split_manifest(
        manifest.check, check,
        bundle.check_census.size(),
        bundle.check_rows.size(), Split::Check);
}

std::string encode(const Bundle& bundle) {
    validate(bundle);
    return encode_wire(bundle);
}

Bundle decode(std::string_view bytes) {
    Bundle result = decode_wire(bytes);
    validate(result);
    return result;
}

Bundle load_published(
    const PublishedArtifactExpectation& expectation) {
    return load_from_impl(
        std::filesystem::path(kArtifactPath),
        expectation);
}

void publish_atomic_no_replace(const Bundle& bundle) {
    publish_at(
        std::filesystem::path(kArtifactPath), bundle);
}

std::string testing::encode_wire_unchecked(
    const Bundle& bundle) {
    return encode_wire(bundle);
}

Bundle testing::load_from(
    const std::filesystem::path& path,
    const PublishedArtifactExpectation& expectation) {
    return load_from_impl(path, expectation);
}

void testing::publish_atomic_no_replace_at(
    const std::filesystem::path& path,
    const Bundle& bundle) {
    publish_at(path, bundle);
}

} // namespace old_school::fq4_dev_bundle
