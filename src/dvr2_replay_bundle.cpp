#include "old_school/dvr2_replay_bundle.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/dvr1_replay.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace old_school::dvr2_replay_bundle {
namespace {

constexpr std::size_t kPayloadBytes = 220944;
constexpr std::size_t kPayloadFieldCount = 4019;
constexpr std::string_view kPayloadSchemaSha256 =
    "36814934f5bd5d71855779cc64c80b35c9848a3f3af482b8e6e2e88d42180c40";
constexpr std::string_view kRootManifestSha256 =
    "11c17fa294d53c4dd58112b0bf143288c0a1a73051df059ab66d799efe98c95a";
constexpr std::size_t kMaximumFields = 5000;
constexpr std::size_t kMaximumFieldNameBytes = 256;
constexpr std::size_t kMaximumFieldValueBytes = 1U << 20U;
constexpr std::string_view kModelArtifactPath =
    "build/model-cache/"
    "old-school-value-challenger-v3-c16-t800-s424242.bin";
constexpr std::string_view kModelArtifactSha256 =
    "53aeb904bd87311b37201859317f05ab066bdfe134c72460cf94bff6d1f944ca";
constexpr std::string_view kSourceScheduleSha256 =
    "876ac6ce9c89fa3a33b52c1650d46653eb0db9d06c98251b00aaa9733872fd13";
constexpr std::uint64_t kReferenceSeed = 1414213562;
constexpr double kNormal95 = 1.96;

enum class Encoding {
    Scalar,
    Text,
};

struct ParsedField {
    std::string value;
    Encoding encoding = Encoding::Scalar;
};

struct ParsedDocument {
    std::vector<std::string> order;
    std::map<std::string, ParsedField, std::less<>> fields;
};

struct ExpectedReplay {
    std::size_t root_index;
    SourceMetadata source;
    DeckId owner_deck;
    DeckId opponent_deck;
    std::string_view stratum;
    std::string_view stable_id;
    std::string_view information_action_fingerprint;
    std::string_view production_action_descriptor;
    std::string_view record_fingerprint;
};

constexpr std::array<ExpectedReplay, kReplayCount>
    kExpectedReplays = {{
        {
            .root_index = 16,
            .source = {
                .seed_base = 4242,
                .seed_base_index = 0,
                .schedule_index = 29,
                .pairing_index = 7,
                .game_seed = 12617483778894052643ULL,
                .owner_seat = 0,
                .owner_on_play = false,
                .starting_player = 1,
                .trace_ordinal = 63,
            },
            .owner_deck = DeckId::Blue,
            .opponent_deck = DeckId::White,
            .stratum = "blue-opponent-top",
            .stable_id =
                "dvr2.s0.g29.p0.r63.k6af43f6ef6b90e1c",
            .information_action_fingerprint =
                "6af43f6ef6b90e1c",
            .production_action_descriptor =
                "kind-12.card-24.x-0.spell-1",
            .record_fingerprint = "46be73d098e9e94c",
        },
        {
            .root_index = 19,
            .source = {
                .seed_base = 7801,
                .seed_base_index = 1,
                .schedule_index = 28,
                .pairing_index = 7,
                .game_seed = 16797273143022446871ULL,
                .owner_seat = 0,
                .owner_on_play = true,
                .starting_player = 0,
                .trace_ordinal = 69,
            },
            .owner_deck = DeckId::Blue,
            .opponent_deck = DeckId::White,
            .stratum = "blue-opponent-top",
            .stable_id =
                "dvr2.s1.g28.p0.r69.k0c4f5b13903b8e4b",
            .information_action_fingerprint =
                "0c4f5b13903b8e4b",
            .production_action_descriptor =
                "kind-12.card-24.x-0.spell-2",
            .record_fingerprint = "14fa5ce62b1ef025",
        },
        {
            .root_index = 46,
            .source = {
                .seed_base = 4242,
                .seed_base_index = 0,
                .schedule_index = 2,
                .pairing_index = 0,
                .game_seed = 14494967280533501476ULL,
                .owner_seat = 0,
                .owner_on_play = true,
                .starting_player = 0,
                .trace_ordinal = 114,
            },
            .owner_deck = DeckId::Red,
            .opponent_deck = DeckId::Green,
            .stratum = "nonblue-own-top",
            .stable_id =
                "dvr2.s0.g2.p0.r114.k330a025bc74cc0bc",
            .information_action_fingerprint =
                "330a025bc74cc0bc",
            .production_action_descriptor =
                "kind-6.card-3.x-0.target-player-1.creature-7",
            .record_fingerprint = "d36c43b2102e0b06",
        },
        {
            .root_index = 50,
            .source = {
                .seed_base = 4242,
                .seed_base_index = 0,
                .schedule_index = 15,
                .pairing_index = 3,
                .game_seed = 6404566839138605289ULL,
                .owner_seat = 0,
                .owner_on_play = false,
                .starting_player = 1,
                .trace_ordinal = 127,
            },
            .owner_deck = DeckId::RUAggro,
            .opponent_deck = DeckId::Green,
            .stratum = "nonblue-own-top",
            .stable_id =
                "dvr2.s0.g15.p0.r127.kd7d0db3716665337",
            .information_action_fingerprint =
                "d7d0db3716665337",
            .production_action_descriptor =
                "kind-6.card-3.x-0.target-player-1",
            .record_fingerprint = "d48b00cbd5cc90f2",
        },
    }};

[[noreturn]] void fail(std::string_view message) {
    throw std::invalid_argument(
        "invalid sealed DVR2 replay bundle: " +
        std::string(message));
}

bool valid_field_name(std::string_view name) {
    return !name.empty() &&
           name.size() <= kMaximumFieldNameBytes &&
           std::all_of(
               name.begin(), name.end(),
               [](char character) {
                   return (character >= 'a' &&
                           character <= 'z') ||
                          (character >= '0' &&
                           character <= '9') ||
                          character == '.' ||
                          character == '_';
               });
}

std::uint64_t parse_unsigned_token(
    std::string_view token, std::string_view context) {
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(
        token.data(), token.data() + token.size(), value);
    if (token.empty() || error != std::errc{} ||
        end != token.data() + token.size() ||
        std::to_string(value) != token) {
        fail(
            "noncanonical unsigned integer in " +
            std::string(context));
    }
    return value;
}

ParsedDocument parse_document(
    std::string_view bytes, std::string_view context) {
    if (bytes.empty()) {
        fail(std::string(context) + " is empty");
    }
    ParsedDocument result;
    std::size_t cursor = 0;
    while (cursor < bytes.size()) {
        if (result.order.size() >= kMaximumFields) {
            fail(std::string(context) + " has too many fields");
        }
        const std::size_t tab = bytes.find('\t', cursor);
        const std::size_t line_end = bytes.find('\n', cursor);
        if (tab == std::string_view::npos ||
            line_end == std::string_view::npos ||
            tab >= line_end) {
            fail(std::string(context) + " has a truncated field");
        }
        const std::string_view name =
            bytes.substr(cursor, tab - cursor);
        if (!valid_field_name(name)) {
            fail(std::string(context) + " has an invalid field name");
        }

        const std::size_t value_begin = tab + 1;
        const std::size_t colon = bytes.find(':', value_begin);
        ParsedField field;
        if (colon != std::string_view::npos &&
            colon < line_end) {
            const std::uint64_t length = parse_unsigned_token(
                bytes.substr(value_begin, colon - value_begin),
                "text length");
            if (length > kMaximumFieldValueBytes ||
                length >
                    static_cast<std::uint64_t>(
                        bytes.size() - colon - 1)) {
                fail(
                    std::string(context) +
                    " has an out-of-range text length");
            }
            const std::size_t size =
                static_cast<std::size_t>(length);
            const std::size_t end = colon + 1 + size;
            if (end >= bytes.size() || bytes[end] != '\n') {
                fail(
                    std::string(context) +
                    " has a truncated length-framed value");
            }
            field.value =
                std::string(bytes.substr(colon + 1, size));
            field.encoding = Encoding::Text;
            cursor = end + 1;
        } else {
            const std::string_view value =
                bytes.substr(value_begin, line_end - value_begin);
            if (value.empty() ||
                value.size() > kMaximumFieldValueBytes ||
                value.find('\r') != std::string_view::npos ||
                value.find('\0') != std::string_view::npos) {
                fail(
                    std::string(context) +
                    " has an invalid scalar value");
            }
            field.value = std::string(value);
            field.encoding = Encoding::Scalar;
            cursor = line_end + 1;
        }

        std::string owned_name(name);
        if (result.fields.contains(owned_name)) {
            fail(
                std::string(context) + " has duplicate field '" +
                owned_name + "'");
        }
        result.order.push_back(owned_name);
        result.fields.emplace(
            std::move(owned_name), std::move(field));
    }
    if (cursor != bytes.size()) {
        fail(std::string(context) + " has trailing bytes");
    }
    return result;
}

std::string ordered_schema_sha256(
    const ParsedDocument& document) {
    std::string schema;
    schema.reserve(document.order.size() * 32);
    for (const std::string& name : document.order) {
        const auto found = document.fields.find(name);
        if (found == document.fields.end()) {
            fail("ordered schema references a missing field");
        }
        schema += name;
        schema.push_back('\t');
        schema.push_back(
            found->second.encoding == Encoding::Text ? 'T' : 'S');
        schema.push_back('\n');
    }
    return artifact_integrity::sha256_string(schema);
}

const ParsedField& require_field(
    const ParsedDocument& document, std::string_view name,
    Encoding encoding) {
    const auto found = document.fields.find(name);
    if (found == document.fields.end()) {
        fail("missing field '" + std::string(name) + "'");
    }
    if (found->second.encoding != encoding) {
        fail("wrong encoding for field '" + std::string(name) + "'");
    }
    return found->second;
}

const std::string& text(
    const ParsedDocument& document, std::string_view name) {
    return require_field(document, name, Encoding::Text).value;
}

const std::string& scalar(
    const ParsedDocument& document, std::string_view name) {
    return require_field(document, name, Encoding::Scalar).value;
}

std::uint64_t unsigned64(
    const ParsedDocument& document, std::string_view name) {
    return parse_unsigned_token(scalar(document, name), name);
}

std::size_t size_value(
    const ParsedDocument& document, std::string_view name,
    std::size_t maximum =
        std::numeric_limits<std::size_t>::max()) {
    const std::uint64_t value = unsigned64(document, name);
    if (value > maximum ||
        value > std::numeric_limits<std::size_t>::max()) {
        fail("out-of-range size in field '" + std::string(name) + "'");
    }
    return static_cast<std::size_t>(value);
}

bool boolean(
    const ParsedDocument& document, std::string_view name) {
    const std::string& value = scalar(document, name);
    if (value == "0") {
        return false;
    }
    if (value == "1") {
        return true;
    }
    fail("invalid boolean in field '" + std::string(name) + "'");
}

double real(
    const ParsedDocument& document, std::string_view name) {
    const std::string& value = text(document, name);
    std::istringstream input(value);
    input.imbue(std::locale::classic());
    double parsed = 0.0;
    input >> parsed;
    if (!input ||
        input.peek() != std::char_traits<char>::eof() ||
        !std::isfinite(parsed)) {
        fail("invalid real in field '" + std::string(name) + "'");
    }
    return parsed;
}

void require_text_value(
    const ParsedDocument& document, std::string_view name,
    std::string_view expected) {
    if (text(document, name) != expected) {
        fail("unexpected value for field '" + std::string(name) + "'");
    }
}

void require_scalar_value(
    const ParsedDocument& document, std::string_view name,
    std::string_view expected) {
    if (scalar(document, name) != expected) {
        fail("unexpected value for field '" + std::string(name) + "'");
    }
}

std::string nested(
    std::string_view prefix, std::string_view suffix) {
    return std::string(prefix) + "." + std::string(suffix);
}

void append_manifest_value(
    std::string& manifest, std::string_view value) {
    manifest += std::to_string(value.size());
    manifest.push_back(':');
    manifest += value;
    manifest.push_back('\n');
}

std::string root_manifest_sha256(
    const ParsedDocument& document) {
    std::string manifest;
    manifest.reserve(kRootCount * 160);
    for (std::size_t root = 0; root < kRootCount; ++root) {
        const std::string prefix =
            "root." + std::to_string(root);
        append_manifest_value(
            manifest, std::to_string(root));
        for (const std::string_view suffix :
             {"reference.stable_id",
              "information_action_fingerprint",
              "owner_deck",
              "opponent_deck",
              "stratum",
              "production_action_descriptor",
              "classification",
              "dvr1_record_fingerprint"}) {
            append_manifest_value(
                manifest,
                text(document, nested(prefix, suffix)));
        }
    }
    return artifact_integrity::sha256_string(manifest);
}

DeckId parse_deck(std::string_view value) {
    if (value == "Green") {
        return DeckId::Green;
    }
    if (value == "Red") {
        return DeckId::Red;
    }
    if (value == "Blue") {
        return DeckId::Blue;
    }
    if (value == "White") {
        return DeckId::White;
    }
    if (value == "RU Aggro") {
        return DeckId::RUAggro;
    }
    fail("unknown deck name");
}

std::vector<std::string> string_vector(
    const ParsedDocument& document, std::string_view prefix,
    std::size_t maximum) {
    const std::size_t count = size_value(
        document, nested(prefix, "count"), maximum);
    std::vector<std::string> result;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        result.push_back(text(
            document,
            nested(prefix, std::to_string(index))));
    }
    return result;
}

SourceMetadata parse_source(
    const ParsedDocument& document, std::string_view prefix) {
    return {
        .seed_base =
            unsigned64(document, nested(prefix, "seed_base")),
        .seed_base_index = size_value(
            document, nested(prefix, "seed_base_index"), 1),
        .schedule_index = size_value(
            document, nested(prefix, "schedule_index"), 39),
        .pairing_index = size_value(
            document, nested(prefix, "pairing_index"), 9),
        .game_seed =
            unsigned64(document, nested(prefix, "game_seed")),
        .owner_seat = size_value(
            document, nested(prefix, "owner_seat"), 1),
        .owner_on_play =
            boolean(document, nested(prefix, "owner_on_play")),
        .starting_player = size_value(
            document, nested(prefix, "starting_player"), 1),
        .trace_ordinal = size_value(
            document, nested(prefix, "trace_ordinal"), 1000000),
    };
}

bool finite_probability(double value) {
    return std::isfinite(value) && value >= 0.0 &&
           value <= 1.0;
}

bool close_real(double left, double right) {
    if (!std::isfinite(left) || !std::isfinite(right)) {
        return false;
    }
    const double scale =
        std::max({1.0, std::abs(left), std::abs(right)});
    return std::abs(left - right) <= 1.0e-12 * scale;
}

std::vector<std::string> exact_best_actions(
    const std::vector<probes::BsrRootScore::ActionMean>& means,
    bool scout) {
    if (means.empty()) {
        return {};
    }
    double best = -std::numeric_limits<double>::infinity();
    for (const auto& row : means) {
        const double value =
            scout ? row.scout_mean : row.confirmation_mean;
        if (!finite_probability(value)) {
            fail("reference action mean is not a probability");
        }
        best = std::max(best, value);
    }
    std::vector<std::string> result;
    for (const auto& row : means) {
        const double value =
            scout ? row.scout_mean : row.confirmation_mean;
        if (value == best) {
            result.push_back(row.descriptor);
        }
    }
    return result;
}

probes::BsrRootScore parse_reference_score(
    const ParsedDocument& document, std::string_view prefix) {
    probes::BsrRootScore score;
    score.stable_id = text(document, nested(prefix, "stable_id"));
    score.information_action_fingerprint =
        text(
            document,
            nested(prefix, "information_action_fingerprint"));
    score.action_count = size_value(
        document, nested(prefix, "action_count"),
        probes::kBsrMaximumLegalActions);
    score.actual_action_index = size_value(
        document, nested(prefix, "actual_action_index"),
        score.action_count == 0 ? 0 : score.action_count - 1);
    score.actual_action_descriptor = text(
        document, nested(prefix, "actual_action_descriptor"));
    score.reference_model_fingerprint = text(
        document,
        nested(prefix, "reference_model_fingerprint"));
    score.reference_seed_base = unsigned64(
        document, nested(prefix, "reference_seed_base"));
    score.scout_seed =
        unsigned64(document, nested(prefix, "scout_seed"));
    score.confirmation_seed = unsigned64(
        document, nested(prefix, "confirmation_seed"));
    score.scout_worlds = size_value(
        document, nested(prefix, "scout_worlds"), 512);
    score.confirmation_worlds = size_value(
        document, nested(prefix, "confirmation_worlds"), 512);
    score.horizon_turns = size_value(
        document, nested(prefix, "horizon_turns"), 128);
    score.rollouts_per_world = size_value(
        document, nested(prefix, "rollouts_per_world"), 64);
    score.evaluation_threads = size_value(
        document, nested(prefix, "evaluation_threads"), 64);
    score.scout_best_actions = string_vector(
        document, nested(prefix, "scout_best_actions"),
        score.action_count);
    score.confirmation_best_actions = string_vector(
        document,
        nested(prefix, "confirmation_best_actions"),
        score.action_count);
    const std::string means_prefix =
        nested(prefix, "action_means");
    const std::size_t means = size_value(
        document, nested(means_prefix, "count"),
        probes::kBsrMaximumLegalActions);
    score.action_means.reserve(means);
    for (std::size_t index = 0; index < means; ++index) {
        const std::string row =
            nested(means_prefix, std::to_string(index));
        score.action_means.push_back({
            .descriptor =
                text(document, nested(row, "descriptor")),
            .scout_mean =
                real(document, nested(row, "scout_mean")),
            .confirmation_mean =
                real(document, nested(row, "confirmation_mean")),
        });
    }
    score.scout_actual_mean =
        real(document, nested(prefix, "scout_actual_mean"));
    score.scout_best_mean =
        real(document, nested(prefix, "scout_best_mean"));
    score.confirmation_actual_mean = real(
        document, nested(prefix, "confirmation_actual_mean"));
    score.confirmation_best_mean = real(
        document, nested(prefix, "confirmation_best_mean"));
    score.confirmation_regret =
        real(document, nested(prefix, "confirmation_regret"));
    score.paired_standard_error = real(
        document, nested(prefix, "paired_standard_error"));
    score.paired_lower_95 =
        real(document, nested(prefix, "paired_lower_95"));
    score.sampled_worlds = size_value(
        document, nested(prefix, "sampled_worlds"), 4096);
    score.rollout_evaluations = size_value(
        document, nested(prefix, "rollout_evaluations"),
        1U << 20U);
    score.terminal_evaluations = size_value(
        document, nested(prefix, "terminal_evaluations"),
        score.rollout_evaluations);
    score.bootstrapped_evaluations = size_value(
        document, nested(prefix, "bootstrapped_evaluations"),
        score.rollout_evaluations);
    score.scout_confirmation_best_set_stable =
        boolean(document, nested(prefix, "stable_best_set"));
    score.actual_outside_best_sets = boolean(
        document, nested(prefix, "actual_outside_best_sets"));
    score.diagnostic_stable_mistake =
        probes::bsr_diagnostic_stable_mistake(
            score.scout_confirmation_best_set_stable,
            score.actual_outside_best_sets,
            {
                .regret = score.confirmation_regret,
                .standard_error = score.paired_standard_error,
                .lower_95 = score.paired_lower_95,
            });
    score.practical_high_cost_mistake =
        probes::bsr_practical_high_cost_mistake(
            score.scout_confirmation_best_set_stable,
            score.actual_outside_best_sets,
            {
                .regret = score.confirmation_regret,
                .standard_error = score.paired_standard_error,
                .lower_95 = score.paired_lower_95,
            });
    score.descriptor_order_invariant = boolean(
        document, nested(prefix, "descriptor_order_invariant"));
    score.hidden_repartition_eligible = boolean(
        document, nested(prefix, "hidden_repartition_eligible"));
    score.hidden_repartition_bit_identical = boolean(
        document,
        nested(prefix, "hidden_repartition_bit_identical"));
    score.accounting_passed =
        boolean(document, nested(prefix, "accounting_passed"));

    if (score.action_count < 2 ||
        score.action_means.size() != score.action_count ||
        score.actual_action_index >= score.action_count) {
        fail("reference action table shape changed");
    }
    std::vector<std::string> mean_descriptors;
    mean_descriptors.reserve(score.action_means.size());
    for (const auto& row : score.action_means) {
        if (row.descriptor.empty()) {
            fail("reference action mean has an empty descriptor");
        }
        mean_descriptors.push_back(row.descriptor);
    }
    const std::vector<std::string> expected_scout_best =
        exact_best_actions(score.action_means, true);
    const std::vector<std::string> expected_confirmation_best =
        exact_best_actions(score.action_means, false);
    const auto maximum_mean =
        [&](bool scout) {
            double result =
                -std::numeric_limits<double>::infinity();
            for (const auto& row : score.action_means) {
                result = std::max(
                    result,
                    scout ? row.scout_mean
                          : row.confirmation_mean);
            }
            return result;
        };
    const double derived_scout_best = maximum_mean(true);
    const double derived_confirmation_best =
        maximum_mean(false);
    const double derived_regret =
        derived_confirmation_best -
        score.action_means[score.actual_action_index]
            .confirmation_mean;
    const double derived_lower_95 =
        derived_regret -
        kNormal95 * score.paired_standard_error;

    if (!std::is_sorted(
            mean_descriptors.begin(), mean_descriptors.end()) ||
        std::adjacent_find(
            mean_descriptors.begin(), mean_descriptors.end()) !=
            mean_descriptors.end() ||
        score.action_means[score.actual_action_index].descriptor !=
            score.actual_action_descriptor ||
        score.scout_best_actions.empty() ||
        score.scout_best_actions != expected_scout_best ||
        score.confirmation_best_actions !=
            expected_confirmation_best ||
        score.scout_best_actions !=
            score.confirmation_best_actions ||
        !close_real(
            score.scout_actual_mean,
            score.action_means[score.actual_action_index]
                .scout_mean) ||
        !close_real(
            score.confirmation_actual_mean,
            score.action_means[score.actual_action_index]
                .confirmation_mean) ||
        !close_real(
            score.scout_best_mean, derived_scout_best) ||
        !close_real(
            score.confirmation_best_mean,
            derived_confirmation_best) ||
        !close_real(
            score.confirmation_regret, derived_regret) ||
        score.paired_standard_error < 0.0 ||
        !close_real(
            score.paired_lower_95, derived_lower_95) ||
        score.reference_model_fingerprint !=
            kModelFingerprint ||
        score.reference_seed_base != kReferenceSeed ||
        score.scout_worlds != 64 ||
        score.confirmation_worlds != 64 ||
        score.horizon_turns != 8 ||
        score.rollouts_per_world != 1 ||
        score.evaluation_threads != 4 ||
        !score.scout_confirmation_best_set_stable ||
        !score.actual_outside_best_sets ||
        !score.descriptor_order_invariant ||
        !score.hidden_repartition_eligible ||
        !score.hidden_repartition_bit_identical ||
        !score.accounting_passed ||
        score.terminal_evaluations >
            score.rollout_evaluations ||
        score.bootstrapped_evaluations !=
            score.rollout_evaluations -
                score.terminal_evaluations) {
        fail("invalid stable-disagreement reference score");
    }
    return score;
}

const ExpectedReplay* expected_replay(std::size_t root_index) {
    const auto found = std::find_if(
        kExpectedReplays.begin(), kExpectedReplays.end(),
        [&](const ExpectedReplay& expected) {
            return expected.root_index == root_index;
        });
    return found == kExpectedReplays.end() ? nullptr : &*found;
}

void require_protocol_and_census(
    const ParsedDocument& payload) {
    if (payload.fields.size() != kPayloadFieldCount) {
        fail("payload field census changed");
    }
    require_text_value(payload, "schema", kPayloadSchema);
    require_text_value(
        payload, "environment_revision", kEnvironmentRevision);
    require_text_value(
        payload, "model.artifact_path", kModelArtifactPath);
    require_text_value(
        payload, "model.artifact_sha256",
        kModelArtifactSha256);
    require_text_value(
        payload, "model.fingerprint", kModelFingerprint);

    constexpr std::array<std::pair<std::string_view,
                                   std::string_view>,
                         28>
        kScalarRequirements = {{
            {"protocol.training_games", "800"},
            {"protocol.training_seed", "424242"},
            {"protocol.self_play_generations", "16"},
            {"protocol.generation_namespace", "1146507826"},
            {"protocol.source_turn_cap", "128"},
            {"protocol.production_worlds", "8"},
            {"protocol.production_horizon", "4"},
            {"protocol.production_pass_dominance", "0"},
            {"protocol.reference_seed", "1414213562"},
            {"protocol.reference_scout_worlds", "64"},
            {"protocol.reference_confirmation_worlds", "64"},
            {"protocol.reference_horizon", "8"},
            {"protocol.reference_rollouts_per_world", "1"},
            {"protocol.reference_threads", "4"},
            {"protocol.reference_evaluation_cap", "131072"},
            {"protocol.source_seed_bases.count", "2"},
            {"protocol.source_seed_bases.0", "4242"},
            {"protocol.source_seed_bases.1", "7801"},
            {"counts.physical_games", "80"},
            {"counts.owner_game_perspectives", "160"},
            {"counts.traced_priority_roots", "3219"},
            {"counts.selected_roots", "52"},
            {"counts.stable_disagreements", "4"},
            {"counts.stable_agreements", "43"},
            {"counts.unstable_best_sets", "5"},
            {"counts.invalid_invariance", "0"},
            {"roots.count", "52"},
            {"errors.count", "0"},
        }};
    for (const auto& [name, value] : kScalarRequirements) {
        require_scalar_value(payload, name, value);
    }

    constexpr std::array<std::pair<std::string_view,
                                   std::string_view>,
                         4>
        kTextRequirements = {{
            {"protocol.production_epsilon", "0"},
            {"protocol.production_residual", "0"},
            {"protocol.production_controller", "Legacy"},
            {"protocol.source_schedule_sha256",
             kSourceScheduleSha256},
        }};
    for (const auto& [name, value] : kTextRequirements) {
        require_text_value(payload, name, value);
    }

    constexpr std::array<std::pair<std::string_view,
                                   std::string_view>,
                         14>
        kGateRequirements = {{
            {"gate.exact_source_schedule", "1"},
            {"gate.source_balance", "1"},
            {"gate.source_policy_exact", "1"},
            {"gate.model_identity", "1"},
            {"gate.manifest_cross_sums", "1"},
            {"gate.coverage", "0"},
            {"gate.reference_accounting", "1"},
            {"gate.invariance", "1"},
            {"gate.replay", "1"},
            {"gate.controls", "1"},
            {"gate.controls_underpowered_only_by_budget", "0"},
            {"gate.watchdog", "1"},
            {"gate.valid", "1"},
            {"gate.rs1_licensed", "0"},
        }};
    for (const auto& [name, value] : kGateRequirements) {
        require_scalar_value(payload, name, value);
    }
}

void validate_record_against_reference(
    const ReplayRecord& replay) {
    const auto& record = replay.dvr1;
    const auto& score = replay.reference_score;
    if (record.stable_id != score.stable_id ||
        record.production_model_fingerprint !=
            kModelFingerprint ||
        record.information_action_fingerprint !=
            replay.information_action_fingerprint ||
        record.owner_deck != replay.owner_deck ||
        record.opponent_deck != replay.opponent_deck ||
        record.production_action_descriptor !=
            replay.production_action_descriptor ||
        record.reference_model_fingerprint !=
            score.reference_model_fingerprint ||
        record.reference_seed_base !=
            score.reference_seed_base ||
        record.reference_scout_seed != score.scout_seed ||
        record.reference_confirmation_seed !=
            score.confirmation_seed ||
        record.reference_scout_worlds !=
            score.scout_worlds ||
        record.reference_confirmation_worlds !=
            score.confirmation_worlds ||
        record.reference_horizon_turns !=
            score.horizon_turns ||
        record.reference_rollouts_per_world !=
            score.rollouts_per_world ||
        record.reference_evaluation_threads !=
            score.evaluation_threads ||
        record.reference_best_actions !=
            score.scout_best_actions ||
        record.reference_best_actions !=
            score.confirmation_best_actions ||
        record.reference_action_means !=
            score.action_means ||
        record.reference_sampled_worlds !=
            score.sampled_worlds ||
        record.reference_rollout_evaluations !=
            score.rollout_evaluations ||
        record.reference_terminal_evaluations !=
            score.terminal_evaluations ||
        record.reference_bootstrapped_evaluations !=
            score.bootstrapped_evaluations ||
        record.reference_regret !=
            score.confirmation_regret ||
        record.paired_standard_error !=
            score.paired_standard_error ||
        record.paired_lower_95 != score.paired_lower_95 ||
        record.provenance.game_seed !=
            replay.source.game_seed ||
        record.provenance.block != 0 ||
        record.provenance.schedule_index !=
            replay.source.schedule_index ||
        record.provenance.trace_ordinal !=
            replay.source.trace_ordinal ||
        record.provenance.tracked_seat !=
            replay.source.owner_seat ||
        record.provenance.tracked_starts !=
            replay.source.owner_on_play ||
        record.starting_player !=
            replay.source.starting_player ||
        record.owner_on_play !=
            replay.source.owner_on_play ||
        record.legal_action_descriptors.size() !=
            score.action_count ||
        record.legal_action_descriptors.at(
            score.actual_action_index) !=
            replay.production_action_descriptor) {
        fail("DVR1 record and outer reference metadata disagree");
    }
}

void validate_synthetic_hidden_repartition(
    const ReplayRecord& replay) {
    const std::size_t opponent = 1 - replay.probe.root_player;
    const PlayerState& original =
        replay.probe.state.players[opponent];
    probes::DecisionProbe repartitioned = replay.probe;
    PlayerState& changed =
        repartitioned.state.players[opponent];
    const std::size_t hand_size = changed.hand.size();
    std::vector<CardId> hidden = changed.hand;
    hidden.insert(
        hidden.end(), changed.library.begin(),
        changed.library.end());
    if (hidden.size() > 1) {
        std::rotate(
            hidden.begin(), hidden.begin() + 1, hidden.end());
        std::reverse(hidden.begin(), hidden.end());
    }
    const auto hand_end =
        hidden.begin() +
        static_cast<std::ptrdiff_t>(hand_size);
    changed.hand.assign(hidden.begin(), hand_end);
    changed.library.assign(hand_end, hidden.end());

    const bool identities_changed =
        changed.hand != original.hand ||
        changed.library != original.library;
    const PlayerObservation original_observation =
        observe_game_state(
            replay.probe.state, replay.probe.root_player);
    const PlayerObservation changed_observation =
        observe_game_state(
            repartitioned.state, repartitioned.root_player);
    const probes::Validation changed_validation =
        probes::validate_probe(
            repartitioned, replay.dvr1.reference_seed_base);
    if (!identities_changed ||
        original_observation != changed_observation ||
        replay.probe.candidates != repartitioned.candidates ||
        probes::bsr_information_action_fingerprint(
            replay.probe) !=
            probes::bsr_information_action_fingerprint(
                repartitioned) ||
        !changed_validation.ok() ||
        !probes::hidden_clone_is_determinization_invariant(
            replay.probe, replay.dvr1.reference_seed_base) ||
        !probes::hidden_clone_is_determinization_invariant(
            repartitioned,
            replay.dvr1.reference_seed_base)) {
        fail(
            "synthetic opponent hidden repartition changed the "
            "owner observation or action information set");
    }
}

ReplayRecord parse_replay(
    const ParsedDocument& payload,
    const ExpectedReplay& expected) {
    const std::string prefix =
        "root." + std::to_string(expected.root_index);
    ReplayRecord replay;
    replay.root_index = expected.root_index;
    replay.source =
        parse_source(payload, nested(prefix, "source"));
    replay.owner_deck =
        parse_deck(text(payload, nested(prefix, "owner_deck")));
    replay.opponent_deck = parse_deck(
        text(payload, nested(prefix, "opponent_deck")));
    replay.stratum =
        text(payload, nested(prefix, "stratum"));
    replay.information_action_fingerprint = text(
        payload,
        nested(prefix, "information_action_fingerprint"));
    replay.production_action_descriptor = text(
        payload,
        nested(prefix, "production_action_descriptor"));
    require_text_value(
        payload, nested(prefix, "classification"),
        "stable-disagreement");
    replay.reference_score = parse_reference_score(
        payload, nested(prefix, "reference"));
    replay.serialized_dvr1 =
        text(payload, nested(prefix, "dvr1_record"));
    replay.dvr1_record_fingerprint = text(
        payload, nested(prefix, "dvr1_record_fingerprint"));
    if (!boolean(
            payload, nested(prefix, "decoded_replay_exact")) ||
        boolean(
            payload,
            nested(
                prefix,
                "reversed_single_thread_repeat_exact")) ||
        boolean(
            payload,
            nested(
                prefix,
                "regenerated_canonical_repeat_exact")) ||
        boolean(
            payload,
            nested(prefix, "repeat_reference_agreement"))) {
        fail("stable-disagreement replay flags changed");
    }

    if (replay.source != expected.source ||
        replay.owner_deck != expected.owner_deck ||
        replay.opponent_deck != expected.opponent_deck ||
        replay.stratum != expected.stratum ||
        replay.reference_score.stable_id !=
            expected.stable_id ||
        replay.information_action_fingerprint !=
            expected.information_action_fingerprint ||
        replay.reference_score
                .information_action_fingerprint !=
            expected.information_action_fingerprint ||
        replay.production_action_descriptor !=
            expected.production_action_descriptor ||
        replay.reference_score.actual_action_descriptor !=
            expected.production_action_descriptor ||
        replay.dvr1_record_fingerprint !=
            expected.record_fingerprint) {
        fail("stable-disagreement replay identity changed");
    }

    replay.dvr1 =
        probes::deserialize_dvr1_owner_visible_record(
            replay.serialized_dvr1);
    if (probes::serialize_dvr1_owner_visible_record(
            replay.dvr1) != replay.serialized_dvr1 ||
        probes::dvr1_owner_visible_record_fingerprint(
            replay.dvr1) != replay.dvr1_record_fingerprint) {
        fail("DVR1 replay round-trip or fingerprint changed");
    }
    replay.probe =
        probes::rehydrate_dvr1_decision_probe(replay.dvr1);
    const probes::Validation validation =
        probes::validate_probe(
            replay.probe, replay.dvr1.reference_seed_base);
    if (!validation.ok()) {
        fail("rehydrated DVR1 replay is not a valid complete probe");
    }
    validate_record_against_reference(replay);
    validate_synthetic_hidden_repartition(replay);
    return replay;
}

ReplayBundle decode_bundle(
    std::string_view bytes, bool require_sealed_identity) {
    if (require_sealed_identity &&
        (bytes.size() != kArtifactBytes ||
         artifact_integrity::sha256_string(bytes) !=
             kArtifactSha256)) {
        fail("outer artifact identity changed");
    }

    const ParsedDocument outer =
        parse_document(bytes, "outer envelope");
    const std::array<std::string_view, 3> expected_order = {
        "schema",
        "payload_sha256",
        "payload",
    };
    if (outer.order.size() != expected_order.size() ||
        !std::equal(
            outer.order.begin(), outer.order.end(),
            expected_order.begin(), expected_order.end())) {
        fail("outer envelope fields changed");
    }
    require_text_value(outer, "schema", kBundleSchema);
    const std::string& payload_digest =
        text(outer, "payload_sha256");
    const std::string& payload_bytes = text(outer, "payload");
    if (payload_bytes.size() != kPayloadBytes ||
        payload_digest !=
            artifact_integrity::sha256_string(payload_bytes) ||
        payload_digest.size() != 64 ||
        (require_sealed_identity &&
         payload_digest != kPayloadSha256)) {
        fail("payload identity changed");
    }

    const ParsedDocument payload =
        parse_document(payload_bytes, "payload");
    if (ordered_schema_sha256(payload) !=
            kPayloadSchemaSha256) {
        fail(
            "payload ordered key/encoding schema identity "
            "changed");
    }
    require_protocol_and_census(payload);
    if (root_manifest_sha256(payload) !=
            kRootManifestSha256) {
        fail("root identity/classification manifest changed");
    }

    ReplayBundle bundle;
    bundle.artifact_sha256 =
        artifact_integrity::sha256_string(bytes);
    bundle.payload_sha256 = payload_digest;
    bundle.model_artifact_path =
        text(payload, "model.artifact_path");
    bundle.model_artifact_sha256 =
        text(payload, "model.artifact_sha256");
    bundle.model_fingerprint =
        text(payload, "model.fingerprint");
    bundle.selected_roots =
        size_value(payload, "counts.selected_roots");
    bundle.stable_disagreements =
        size_value(payload, "counts.stable_disagreements");
    bundle.stable_agreements =
        size_value(payload, "counts.stable_agreements");
    bundle.unstable_best_sets =
        size_value(payload, "counts.unstable_best_sets");
    bundle.invalid_invariance =
        size_value(payload, "counts.invalid_invariance");

    std::size_t disagreements = 0;
    std::size_t agreements = 0;
    std::size_t unstable = 0;
    std::size_t invalid = 0;
    bundle.replays.reserve(kReplayCount);
    for (std::size_t root = 0; root < kRootCount; ++root) {
        const std::string prefix =
            "root." + std::to_string(root);
        const std::string& classification =
            text(payload, nested(prefix, "classification"));
        const std::string& record =
            text(payload, nested(prefix, "dvr1_record"));
        const std::string& fingerprint = text(
            payload, nested(prefix, "dvr1_record_fingerprint"));
        const ExpectedReplay* expected =
            expected_replay(root);
        if (classification == "stable-disagreement") {
            ++disagreements;
            if (expected == nullptr || record.empty() ||
                fingerprint.empty()) {
                fail("unexpected stable-disagreement replay");
            }
            bundle.replays.push_back(
                parse_replay(payload, *expected));
        } else {
            if (!record.empty() || !fingerprint.empty() ||
                expected != nullptr) {
                fail("non-disagreement root carries DVR1 evidence");
            }
            if (classification == "stable-agreement") {
                ++agreements;
            } else if (
                classification == "unstable-best-set") {
                ++unstable;
            } else if (
                classification == "invalid-invariance") {
                ++invalid;
            } else {
                fail("unknown root classification");
            }
        }
    }
    if (disagreements != bundle.stable_disagreements ||
        agreements != bundle.stable_agreements ||
        unstable != bundle.unstable_best_sets ||
        invalid != bundle.invalid_invariance ||
        bundle.replays.size() != kReplayCount ||
        bundle.selected_roots != kRootCount) {
        fail("root census does not cross-sum");
    }
    return bundle;
}

std::string read_exact_file(
    const std::filesystem::path& path,
    const artifact_integrity::RegularFileSnapshot& snapshot) {
    if (snapshot.byte_size != kArtifactBytes ||
        snapshot.sha256 != kArtifactSha256) {
        fail("artifact size or SHA-256 changed");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(
            "cannot open sealed DVR2 replay bundle '" +
            path.string() + "'");
    }
    std::string bytes(kArtifactBytes, '\0');
    input.read(
        bytes.data(),
        static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() !=
            static_cast<std::streamsize>(bytes.size()) ||
        input.peek() != std::char_traits<char>::eof()) {
        fail("artifact read did not match its sealed size");
    }
    return bytes;
}

} // namespace

ReplayBundle load() {
    return load(std::filesystem::path(kArtifactPath));
}

ReplayBundle load(const std::filesystem::path& path) {
    const artifact_integrity::RegularFileSnapshot before =
        artifact_integrity::snapshot_regular_file(path);
    const std::string bytes = read_exact_file(path, before);
    const artifact_integrity::RegularFileSnapshot after =
        artifact_integrity::snapshot_regular_file(path);
    if (before != after ||
        artifact_integrity::sha256_string(bytes) !=
            kArtifactSha256) {
        fail("artifact changed while it was read");
    }
    ReplayBundle result = decode_bundle(bytes, true);
    result.artifact_path = before.path;
    return result;
}

namespace testing {

ReplayBundle decode_structurally_valid_bundle(
    std::string_view bytes) {
    return decode_bundle(bytes, false);
}

} // namespace testing

} // namespace old_school::dvr2_replay_bundle
