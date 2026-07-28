#include "old_school/artifact_integrity.hpp"
#include "old_school/fq4_dev_bundle.hpp"
#include "old_school/fq4_dev_schedule.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace bundle = old_school::fq4_dev_bundle;

namespace {

class TestRunner {
  public:
    template <typename Function>
    void run(std::string_view name, Function function) {
        try {
            function();
            ++passed_;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failed_;
            std::cerr << "[FAIL] " << name << ": "
                      << error.what() << '\n';
        }
    }

    int finish() const {
        std::cout << passed_ << " passed, " << failed_
                  << " failed\n";
        return failed_ == 0 ? 0 : 1;
    }

  private:
    std::size_t passed_ = 0;
    std::size_t failed_ = 0;
};

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Function>
void expect_rejected(
    Function function, std::string_view message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

bundle::Hash256 hash(std::string_view label) {
    return bundle::sha256(label);
}

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

void fill_score_trace(
    bundle::ActionRow& row, double shallow,
    double continuation) {
    double shallow_mean = 0.0;
    for (std::size_t world = 0;
         world < bundle::kWorldCount; ++world) {
        row.shallow_prior_sample_bits[world] =
            bits(shallow);
        row.continuation_sample_bits[world] =
            bits(continuation);
        row.raw_sample_bits[world] =
            bits(
                (shallow +
                 static_cast<double>(
                     bundle::kWorldCount) *
                     continuation) /
                static_cast<double>(
                    bundle::kWorldCount + 1U));
        shallow_mean += shallow;
    }
    shallow_mean /=
        static_cast<double>(bundle::kWorldCount);
    double base_score = shallow_mean;
    for (std::size_t world = 0;
         world < bundle::kWorldCount; ++world) {
        base_score += continuation;
    }
    base_score /=
        static_cast<double>(
            bundle::kWorldCount + 1U);
    row.base_score_bits = bits(base_score);
}

std::array<std::uint8_t, 2> seat_decks(
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
    const std::size_t local_index =
        schedule_index %
        old_school::fq4_dev_schedule::
            kPhysicalGamesPerBlock;
    const auto pairing = pairings[local_index / 4];
    return (local_index % 4) / 2 == 0
               ? pairing
               : std::array<std::uint8_t, 2>{
                     pairing[1], pairing[0]};
}

bundle::CensusRow make_census_row(
    bundle::Split split, std::size_t deck,
    std::uint16_t schedule_index,
    std::uint32_t trace_ordinal) {
    const std::string prefix =
        (split == bundle::Split::Fit ? "fit-" : "check-") +
        std::to_string(deck) + "-" +
        std::to_string(schedule_index) + "-" +
        std::to_string(trace_ordinal);
    const std::vector<std::string> descriptors{
        "kind-00-pass",
        "kind-01-action",
    };
    bundle::CensusRow result{
        .schedule_block = static_cast<std::uint8_t>(
            schedule_index /
            old_school::fq4_dev_schedule::
                kPhysicalGamesPerBlock),
        .schedule_index = schedule_index,
        .owner_seat = 0,
        .trace_ordinal = trace_ordinal,
        .owner_deck =
            static_cast<std::uint8_t>(deck),
        .opponent_deck = seat_decks(schedule_index)[1],
        .information_action_sha256 =
            hash(prefix + "-information"),
        .descriptor_set_sha256 =
            bundle::descriptor_set_sha256(descriptors),
        .pass_index = 0,
        .dominance = {
            {.complete = 0, .strict = 0},
            {.complete = 8, .strict = 8},
        },
    };
    result.physical_game_sha256 =
        bundle::expected_physical_game_sha256(
            split, result.schedule_block,
            schedule_index);
    result.stable_root_id =
        bundle::expected_stable_root_sha256(
            split, result.schedule_block,
            schedule_index,
            result.owner_seat,
            trace_ordinal,
            result.information_action_sha256);
    return result;
}

bundle::SelectedRow make_selected_row(
    const bundle::CensusRow& census,
    bundle::Split split,
    bool background = true) {
    bundle::SelectedRow result{
        .split = split,
        .census = census,
        .roles = static_cast<std::uint8_t>(
            bundle::Role::DominancePositive |
            (background
                 ? bundle::Role::BackgroundControl
                 : 0)),
        .production_seed =
            900000U + census.trace_ordinal,
        .accounting = {
            .score_calls = 1,
            .scored_actions = 2,
            .sampled_worlds = 8,
            .rollout_evaluations = 16,
            .terminal_evaluations = 5,
            .bootstrap_evaluations = 11,
        },
    };
    for (std::size_t action = 0; action < 2; ++action) {
        bundle::ActionRow row{
            .descriptor =
                action == 0
                    ? "kind-00-pass"
                    : "kind-01-action",
            .is_pass = action == 0,
            .dominance = census.dominance[action],
            .parent_residual_bits =
                bits(action == 0 ? -0.01 : 0.01),
            .features = {
                {.index = 0,
                 .value_bits =
                     bits(action == 0 ? 1.0 : -1.0)},
                // Negative zero is data and must not be confused with the
                // explicitly omitted positive-zero sparse encoding.
                {.index = 892,
                 .value_bits = 0x8000000000000000ULL},
            },
        };
        fill_score_trace(
            row,
            0.05 * static_cast<double>(action + 1),
            0.1 * static_cast<double>(action + 1));
        result.actions.push_back(std::move(row));
    }
    return result;
}

void refresh_counts(bundle::Bundle& value) {
    auto refresh =
        [](bundle::SplitManifest& manifest,
           const std::vector<bundle::CensusRow>& census,
           const std::vector<bundle::SelectedRow>& rows) {
            manifest.census_rows =
                static_cast<std::uint32_t>(census.size());
            manifest.selected_rows =
                static_cast<std::uint32_t>(rows.size());
            manifest.census_by_deck.fill(0);
            manifest.selected_by_deck.fill(0);
            manifest.positive_by_deck.fill(0);
            manifest.background_by_deck.fill(0);
            for (const auto& row : census) {
                ++manifest.census_by_deck[row.owner_deck];
            }
            for (const auto& row : rows) {
                const std::size_t deck =
                    row.census.owner_deck;
                ++manifest.selected_by_deck[deck];
                if ((row.roles &
                     bundle::Role::DominancePositive) != 0) {
                    ++manifest.positive_by_deck[deck];
                }
                if ((row.roles &
                     bundle::Role::BackgroundControl) != 0) {
                    ++manifest.background_by_deck[deck];
                }
            }
        };
    refresh(
        value.manifest.fit,
        value.fit_census, value.fit_rows);
    refresh(
        value.manifest.check,
        value.check_census, value.check_rows);
}

bundle::Bundle make_bundle() {
    bundle::Bundle result;
    result.manifest = {
        .purpose = std::string(bundle::kPurpose),
        .producer_commit_sha256 = hash("producer-commit"),
        .producer_executable_sha256 =
            hash("producer-executable"),
        .parent_artifact_sha256 =
            bundle::parse_sha256(
                bundle::kParentArtifactSha256),
        .parent_model_fingerprint =
            bundle::parse_sha256(
                bundle::kParentModelFingerprint),
        .parent_components = {
            .critic = bundle::parse_sha256(
                bundle::kParentCriticFingerprint),
            .priority = bundle::parse_sha256(
                bundle::kParentPriorityFingerprint),
            .attack = bundle::parse_sha256(
                bundle::kParentAttackFingerprint),
            .block = bundle::parse_sha256(
                bundle::kParentBlockFingerprint),
            .damage_order = bundle::parse_sha256(
                bundle::kParentDamageOrderFingerprint),
        },
        .generation_namespace =
            bundle::kGenerationNamespace,
        .hidden_namespace = bundle::kHiddenNamespace,
        .dominance_namespace =
            bundle::kDominanceNamespace,
        .collection_spec_sha256 =
            bundle::parse_sha256(
                bundle::kCollectionSpecSha256),
        .production_recipe =
            std::string(bundle::kProductionRecipe),
        .feature_schema =
            std::string(bundle::kFeatureSchema),
        .feature_count =
            static_cast<std::uint16_t>(
                bundle::kFeatureCount),
        .feature_contract_sha256 =
            bundle::parse_sha256(
                bundle::kFeatureContractSha256),
    };
    result.manifest.fit = {
        .source_seed_base = bundle::kFitSeedBase,
        .schedule_sha256 = bundle::parse_sha256(
            old_school::fq4_dev_schedule::
                kExpectedFitScheduleSha256),
        .trajectory_sha256 = hash("fit-trajectory"),
        .retained_sha256 = hash("fit-retained"),
        .dominance_sha256 = hash("fit-dominance"),
        .selection_sha256 = hash("fit-selection"),
        .scored_sha256 = hash("fit-scored"),
    };
    result.manifest.check = {
        .source_seed_base = bundle::kCheckSeedBase,
        .schedule_sha256 = bundle::parse_sha256(
            old_school::fq4_dev_schedule::
                kExpectedCheckScheduleSha256),
        .trajectory_sha256 = hash("check-trajectory"),
        .retained_sha256 = hash("check-retained"),
        .dominance_sha256 = hash("check-dominance"),
        .selection_sha256 = hash("check-selection"),
        .scored_sha256 = hash("check-scored"),
    };

    for (std::size_t deck = 0;
         deck < bundle::kDeckCount; ++deck) {
        constexpr std::array<std::uint16_t, bundle::kDeckCount>
            schedule_by_deck{0, 2, 6, 10, 14};
        auto fit = make_census_row(
            bundle::Split::Fit, deck,
            schedule_by_deck[deck],
            static_cast<std::uint32_t>(100 + deck));
        auto check = make_census_row(
            bundle::Split::Check, deck,
            schedule_by_deck[deck],
            static_cast<std::uint32_t>(200 + deck));
        result.fit_census.push_back(fit);
        result.fit_rows.push_back(
            make_selected_row(
                fit, bundle::Split::Fit));
        result.check_census.push_back(check);
        result.check_rows.push_back(
            make_selected_row(
                check, bundle::Split::Check));
    }
    refresh_counts(result);
    return result;
}

void decode_coherently_rehashed(
    const bundle::Bundle& value) {
    (void)bundle::decode(
        bundle::testing::encode_wire_unchecked(value));
}

void mutate_byte(
    std::string& bytes, std::size_t index) {
    if (index >= bytes.size()) {
        throw std::runtime_error(
            "test mutation index is out of range");
    }
    bytes[index] =
        static_cast<char>(
            static_cast<unsigned char>(bytes[index]) ^
            0x01U);
}

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        std::string pattern =
            (std::filesystem::temp_directory_path() /
             "old-school-fq4-dev-bundle-XXXXXX")
                .string();
        std::vector<char> buffer(
            pattern.begin(), pattern.end());
        buffer.push_back('\0');
        char* const created = ::mkdtemp(buffer.data());
        if (created == nullptr) {
            throw std::system_error(
                errno, std::generic_category(),
                "cannot create FQ4 bundle test directory");
        }
        path_ = created;
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(
        const TemporaryDirectory&) = delete;

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

void write_file(
    const std::filesystem::path& path,
    std::string_view bytes) {
    std::ofstream output(
        path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error(
            "cannot create bundle test file");
    }
    output.write(
        bytes.data(),
        static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw std::runtime_error(
            "cannot write bundle test file");
    }
}

std::string read_file(
    const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(
            "cannot read bundle test file");
    }
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>(),
    };
}

void test_round_trip_is_deterministic() {
    const bundle::Bundle original = make_bundle();
    const std::string first = bundle::encode(original);
    const std::string second = bundle::encode(original);
    const std::string synthetic_sha256 =
        old_school::artifact_integrity::
            sha256_string(first);
    if (synthetic_sha256 !=
        "fcccd0d3ea91c9a2734bfdcb180a0c5c68ee6f33c2a9fab15bdc1791aaa5591d") {
        throw std::runtime_error(
            "synthetic bundle wire golden drifted: " +
            synthetic_sha256);
    }
    expect(first == second,
           "bundle encoding is not deterministic");
    expect(bundle::decode(first) == original,
           "bundle did not round-trip");
    expect(first.size() < bundle::kMaximumArtifactBytes,
           "synthetic bundle is unexpectedly oversized");
    expect(
        bundle::format_sha256(bundle::sha256(first)) ==
            old_school::artifact_integrity::
                sha256_string(first),
        "public SHA helpers disagree");
}

void test_wire_hashes_order_endian_and_eof() {
    const std::string valid =
        bundle::encode(make_bundle());

    std::string changed = valid;
    mutate_byte(changed, changed.size() / 2);
    expect_rejected(
        [&] { (void)bundle::decode(changed); },
        "section mutation passed its domain hash");

    changed = valid;
    mutate_byte(changed, changed.size() - 1);
    expect_rejected(
        [&] { (void)bundle::decode(changed); },
        "payload hash mutation passed");

    changed = valid;
    mutate_byte(changed, 4);
    expect_rejected(
        [&] { (void)bundle::decode(changed); },
        "wrong bundle schema passed");

    changed = valid;
    changed.push_back('\0');
    expect_rejected(
        [&] { (void)bundle::decode(changed); },
        "trailing artifact byte passed");

    changed = valid.substr(0, valid.size() - 1);
    expect_rejected(
        [&] { (void)bundle::decode(changed); },
        "truncated artifact passed");

    const std::size_t schema_end =
        4 + bundle::kBundleSchema.size();
    changed = valid;
    mutate_byte(changed, schema_end);
    expect_rejected(
        [&] { (void)bundle::decode(changed); },
        "wrong endian marker passed");

    changed = valid;
    mutate_byte(changed, schema_end + 4);
    expect_rejected(
        [&] { (void)bundle::decode(changed); },
        "wrong section count passed");

    changed = valid;
    mutate_byte(changed, schema_end + 5);
    expect_rejected(
        [&] { (void)bundle::decode(changed); },
        "wrong first section identity passed");
}

void test_bounded_counts_reject_before_allocation() {
    auto changed = make_bundle();
    changed.fit_census.resize(
        bundle::kMaximumCensusRowsPerSplit + 1);
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "oversized census passed");

    changed = make_bundle();
    changed.fit_rows.resize(
        bundle::kMaximumSelectedRowsPerSplit + 1);
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "oversized selected-row section passed");

    changed = make_bundle();
    changed.fit_census.front().dominance.resize(
        bundle::kMaximumActions + 1);
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "oversized action set passed");

    changed = make_bundle();
    changed.fit_rows.front()
        .actions.front()
        .features.resize(
            bundle::kMaximumFeaturesPerAction + 1,
            {.index = 1, .value_bits = bits(1.0)});
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "oversized sparse feature set passed");
}

void test_manifest_and_cross_section_counts() {
    auto changed = make_bundle();
    changed.manifest.purpose = "promotion-evidence";
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "wrong purpose passed");

    changed = make_bundle();
    changed.manifest.feature_count = 892;
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "wrong feature count passed");

    changed = make_bundle();
    changed.manifest.production_recipe += "-changed";
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "coherently changed production recipe passed");

    changed = make_bundle();
    changed.manifest.feature_schema = "other-features-v1";
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "coherently changed feature schema passed");

    changed = make_bundle();
    changed.manifest.feature_contract_sha256 =
        hash("other-valid-feature-contract");
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "coherently changed feature contract passed");

    changed = make_bundle();
    changed.manifest.collection_spec_sha256 =
        hash("other-valid-collection-spec");
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "coherently changed collection spec passed");

    changed = make_bundle();
    changed.manifest.fit.source_seed_base = 790;
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "held-out seed passed");

    changed = make_bundle();
    changed.manifest.fit.schedule_sha256 =
        hash("wrong-schedule");
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "wrong schedule hash passed");

    changed = make_bundle();
    ++changed.manifest.fit.census_rows;
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "manifest census-count drift passed");

    changed = make_bundle();
    ++changed.manifest.check.positive_by_deck[0];
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "manifest per-deck count drift passed");

    changed = make_bundle();
    changed.manifest.parent_components.priority = {};
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "zero component fingerprint passed");

    changed = make_bundle();
    changed.manifest.parent_model_fingerprint =
        hash("other-valid-model");
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "different nonzero parent identity passed");
}

void test_census_identity_and_selection_consistency() {
    auto changed = make_bundle();
    changed.fit_census[1].stable_root_id =
        changed.fit_census[0].stable_root_id;
    refresh_counts(changed);
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "duplicate census root passed");

    changed = make_bundle();
    changed.check_census[0].stable_root_id =
        changed.fit_census[0].stable_root_id;
    changed.check_rows[0].census.stable_root_id =
        changed.fit_census[0].stable_root_id;
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "cross-split duplicate root passed");

    changed = make_bundle();
    std::swap(
        changed.fit_census[0],
        changed.check_census[0]);
    std::swap(
        changed.fit_rows[0],
        changed.check_rows[0]);
    changed.fit_rows[0].split =
        bundle::Split::Fit;
    changed.check_rows[0].split =
        bundle::Split::Check;
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "coherent FIT/CHECK root swap passed");

    changed = make_bundle();
    ++changed.fit_rows[0].census.trace_ordinal;
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "selected/census row mismatch passed");

    changed = make_bundle();
    changed.fit_rows[0].actions.pop_back();
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "selected/census action-count drift passed");

    changed = make_bundle();
    changed.fit_rows[0].split = bundle::Split::Check;
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "selected-row split/section mismatch passed");

    changed = make_bundle();
    changed.fit_census[1].schedule_index =
        changed.fit_census[0].schedule_index;
    changed.fit_census[1].trace_ordinal =
        changed.fit_census[0].trace_ordinal;
    changed.fit_rows[1].census =
        changed.fit_census[1];
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "duplicate public locator passed");

    changed = make_bundle();
    changed.fit_census[1].schedule_index =
        changed.fit_census[0].schedule_index;
    changed.fit_rows[1].census =
        changed.fit_census[1];
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "multiple physical IDs for one schedule row passed");

    changed = make_bundle();
    changed.fit_census[0].schedule_block = 1;
    changed.fit_rows[0].census =
        changed.fit_census[0];
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "schedule-block/index disagreement passed");

    changed = make_bundle();
    changed.fit_census[0].schedule_block = 1;
    changed.fit_census[0].schedule_index = 40;
    changed.fit_rows[0].census =
        changed.fit_census[0];
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "coherent block/index mutation with stale identities passed");

    changed = make_bundle();
    changed.fit_census[0].physical_game_sha256[0] ^=
        1U;
    changed.fit_rows[0].census =
        changed.fit_census[0];
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "one-bit physical-game digest mutation passed");

    changed = make_bundle();
    std::swap(
        changed.fit_census[0],
        changed.fit_census[1]);
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "noncanonical census order passed");

    changed = make_bundle();
    std::swap(
        changed.fit_rows[0],
        changed.fit_rows[1]);
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "noncanonical selected-row order passed");
}

void test_descriptors_pass_and_dominance() {
    auto changed = make_bundle();
    changed.fit_rows[0].actions[0].descriptor =
        "kind-99-wrong";
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "descriptor ordering/digest drift passed");

    changed = make_bundle();
    changed.fit_rows[0].census.descriptor_set_sha256 =
        hash("wrong-descriptor-set");
    changed.fit_census[0].descriptor_set_sha256 =
        changed.fit_rows[0].census.descriptor_set_sha256;
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "coherently rehashed descriptor-set drift passed");

    changed = make_bundle();
    changed.fit_rows[0].actions[0].is_pass = false;
    changed.fit_rows[0].actions[1].is_pass = true;
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "typed Pass/index drift passed");

    changed = make_bundle();
    changed.fit_census[0].dominance[1].strict = 9;
    changed.fit_rows[0].census =
        changed.fit_census[0];
    changed.fit_rows[0].actions[1].dominance =
        changed.fit_census[0].dominance[1];
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "invalid dominance counts passed");

    changed = make_bundle();
    changed.fit_census[0].dominance[1] =
        {.complete = 8, .strict = 7};
    changed.fit_rows[0].census =
        changed.fit_census[0];
    changed.fit_rows[0].actions[1].dominance =
        changed.fit_census[0].dominance[1];
    // Keep a positive role while coherently weakening the label.
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "role/robust-dominance disagreement passed");
}

void test_sparse_features_and_nonfinite_values() {
    const auto decoded =
        bundle::decode(bundle::encode(make_bundle()));
    expect(
        decoded.fit_rows[0]
                .actions[0]
                .features[1]
                .value_bits ==
            0x8000000000000000ULL,
        "negative zero was not preserved");

    auto changed = make_bundle();
    changed.fit_rows[0]
        .actions[0]
        .features[1]
        .index = 0;
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "duplicate sparse index passed");

    changed = make_bundle();
    changed.fit_rows[0]
        .actions[0]
        .features[1]
        .index = 893;
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "out-of-range sparse index passed");

    changed = make_bundle();
    changed.fit_rows[0]
        .actions[0]
        .features[0]
        .value_bits = 0;
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "explicit positive-zero sparse feature passed");

    changed = make_bundle();
    changed.fit_rows[0]
        .actions[0]
        .features[0]
        .value_bits = 0x7ff0000000000000ULL;
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "infinite sparse feature passed");

    changed = make_bundle();
    changed.fit_rows[0]
        .actions[0]
        .raw_sample_bits[3] = 0x7ff8000000000000ULL;
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "NaN raw sample passed");

    changed = make_bundle();
    changed.fit_rows[0]
        .actions[0]
        .shallow_prior_sample_bits[3] =
        0x7ff8000000000000ULL;
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "NaN shallow-prior sample passed");

    changed = make_bundle();
    changed.fit_rows[0]
        .actions[0]
        .continuation_sample_bits[3] =
        0x7ff8000000000000ULL;
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "NaN continuation sample passed");

    changed = make_bundle();
    changed.fit_rows[0]
        .actions[0]
        .base_score_bits = 0x7ff0000000000000ULL;
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "infinite base score passed");

    changed = make_bundle();
    changed.fit_rows[0]
        .actions[0]
        .parent_residual_bits =
        0xfff0000000000000ULL;
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "infinite parent residual passed");

    changed = make_bundle();
    changed.fit_rows[0]
        .actions[0]
        .raw_sample_bits[0] = bits(1.01);
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "finite out-of-range raw sample passed");

    changed = make_bundle();
    changed.fit_rows[0]
        .actions[0]
        .shallow_prior_sample_bits[0] = bits(1.01);
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "finite out-of-range shallow-prior sample passed");

    changed = make_bundle();
    changed.fit_rows[0]
        .actions[0]
        .continuation_sample_bits[0] = bits(-0.01);
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "finite out-of-range continuation sample passed");

    changed = make_bundle();
    changed.fit_rows[0]
        .actions[0]
        .base_score_bits = bits(-0.01);
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "finite out-of-range base score passed");

    changed = make_bundle();
    changed.fit_rows[0]
        .actions[0]
        .base_score_bits = bits(0.50);
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "finite base-score/production-trace mismatch passed");

    changed = make_bundle();
    changed.fit_rows[0]
        .actions[0]
        .raw_sample_bits[0] = bits(0.50);
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "raw-sample/production-trace mismatch passed");

    changed = make_bundle();
    changed.fit_rows[0]
        .actions[0]
        .parent_residual_bits = bits(0.100001);
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "finite out-of-range parent residual passed");
}

void test_roles_caps_background_and_accounting() {
    auto changed = make_bundle();
    changed.fit_rows[0].roles =
        bundle::Role::BackgroundControl;
    refresh_counts(changed);
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "positive background row lost positive role");

    changed = make_bundle();
    changed.fit_rows[0].roles =
        bundle::Role::DominancePositive;
    refresh_counts(changed);
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "deck without background passed");

    changed = make_bundle();
    ++changed.fit_rows[0]
          .accounting.rollout_evaluations;
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "production accounting drift passed");

    changed = make_bundle();
    for (auto* rows :
         {&changed.fit_rows, &changed.check_rows}) {
        for (auto& row : *rows) {
            fill_score_trace(
                row.actions[0], 0.90, 0.90);
            row.actions[0].parent_residual_bits =
                bits(0.0);
            fill_score_trace(
                row.actions[1], 0.10, 0.10);
            row.actions[1].parent_residual_bits =
                bits(0.0);
        }
    }
    bundle::validate_prepublication_construction(
        changed);
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "zero parent-error support passed publication validation");

    changed = make_bundle();
    changed.fit_census[0].dominance[1] = {
        .complete = 8,
        .strict = 7,
    };
    changed.fit_rows[0].census =
        changed.fit_census[0];
    changed.fit_rows[0].roles =
        bundle::Role::BackgroundControl;
    changed.fit_rows[0].actions[1].dominance =
        changed.fit_census[0].dominance[1];
    refresh_counts(changed);
    bundle::validate_prepublication_construction(
        changed);
    expect_rejected(
        [&] { bundle::validate(changed); },
        "zero-positive deck passed publication validation");

    changed = make_bundle();
    changed.fit_census.push_back(
        make_census_row(
            bundle::Split::Fit, 0, 40, 500));
    std::sort(
        changed.fit_census.begin(),
        changed.fit_census.end(),
        [](const auto& left, const auto& right) {
            return std::tuple{
                       left.schedule_index,
                       left.owner_seat,
                       left.trace_ordinal,
                       left.stable_root_id} <
                   std::tuple{
                       right.schedule_index,
                       right.owner_seat,
                       right.trace_ordinal,
                       right.stable_root_id};
        });
    refresh_counts(changed);
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "required nonbackground positive row was omitted");

    changed = make_bundle();
    for (std::size_t extra = 0; extra < 16; ++extra) {
        const auto row = make_census_row(
            bundle::Split::Fit, 0,
            static_cast<std::uint16_t>(
                extra < 15 ? 0 : 1),
            static_cast<std::uint32_t>(600 + extra));
        changed.fit_census.push_back(row);
        if (extra < 15) {
            changed.fit_rows.push_back(
                make_selected_row(
                    row, bundle::Split::Fit, false));
        }
    }
    std::sort(
        changed.fit_census.begin(),
        changed.fit_census.end(),
        [](const auto& left, const auto& right) {
            return std::tuple{
                       left.schedule_index,
                       left.owner_seat,
                       left.trace_ordinal,
                       left.stable_root_id} <
                   std::tuple{
                       right.schedule_index,
                       right.owner_seat,
                       right.trace_ordinal,
                       right.stable_root_id};
        });
    std::sort(
        changed.fit_rows.begin(),
        changed.fit_rows.end(),
        [](const auto& left, const auto& right) {
            return std::tuple{
                       left.census.owner_deck,
                       left.census.schedule_index,
                       left.census.owner_seat,
                       left.census.trace_ordinal,
                       left.census.stable_root_id} <
                   std::tuple{
                       right.census.owner_deck,
                       right.census.schedule_index,
                       right.census.owner_seat,
                       right.census.trace_ordinal,
                       right.census.stable_root_id};
        });
    refresh_counts(changed);
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "wrong evenly-spaced positive subset passed");

    changed = make_bundle();
    const auto later = make_census_row(
        bundle::Split::Fit, 0, 0, 500);
    changed.fit_census.push_back(later);
    std::sort(
        changed.fit_census.begin(),
        changed.fit_census.end(),
        [](const auto& left, const auto& right) {
            return std::tuple{
                       left.schedule_index,
                       left.owner_seat,
                       left.trace_ordinal,
                       left.stable_root_id} <
                   std::tuple{
                       right.schedule_index,
                       right.owner_seat,
                       right.trace_ordinal,
                       right.stable_root_id};
        });
    changed.fit_rows[0] =
        make_selected_row(
            later, bundle::Split::Fit);
    std::sort(
        changed.fit_rows.begin(),
        changed.fit_rows.end(),
        [](const auto& left, const auto& right) {
            return std::tuple{
                       left.census.owner_deck,
                       left.census.schedule_index,
                       left.census.owner_seat,
                       left.census.trace_ordinal,
                       left.census.stable_root_id} <
                   std::tuple{
                       right.census.owner_deck,
                       right.census.schedule_index,
                       right.census.owner_seat,
                       right.census.trace_ordinal,
                       right.census.stable_root_id};
        });
    refresh_counts(changed);
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "non-first background row passed");

    changed = make_bundle();
    for (std::size_t extra = 0; extra < 16; ++extra) {
        const auto row = make_census_row(
            bundle::Split::Fit, 0,
            static_cast<std::uint16_t>(
                extra < 15 ? 0 : 1),
            static_cast<std::uint32_t>(500 + extra));
        changed.fit_census.push_back(row);
        changed.fit_rows.push_back(
            make_selected_row(
                row, bundle::Split::Fit, false));
    }
    std::sort(
        changed.fit_census.begin(),
        changed.fit_census.end(),
        [](const auto& left, const auto& right) {
            return std::tuple{
                       left.schedule_index,
                       left.owner_seat,
                       left.trace_ordinal,
                       left.stable_root_id} <
                   std::tuple{
                       right.schedule_index,
                       right.owner_seat,
                       right.trace_ordinal,
                       right.stable_root_id};
        });
    std::sort(
        changed.fit_rows.begin(),
        changed.fit_rows.end(),
        [](const auto& left, const auto& right) {
            return std::tuple{
                       left.census.owner_deck,
                       left.census.schedule_index,
                       left.census.owner_seat,
                       left.census.trace_ordinal,
                       left.census.stable_root_id} <
                   std::tuple{
                       right.census.owner_deck,
                       right.census.schedule_index,
                       right.census.owner_seat,
                       right.census.trace_ordinal,
                       right.census.stable_root_id};
        });
    refresh_counts(changed);
    expect_rejected(
        [&] { decode_coherently_rehashed(changed); },
        "17-row per-deck union passed");
}

void test_fixed_loader_and_atomic_no_replace() {
    expect(
        bundle::kPublishedArtifactBytes == 2250909,
        "published byte count drifted");
    expect(
        bundle::kPublishedArtifactSha256 ==
            "0911fc2eb8b14ddc9165543eb1e4c4edb0b058256a58dedf61f6c4ea4ca859df",
        "published artifact hash drifted");

    TemporaryDirectory temporary;
    const std::filesystem::path direct =
        temporary.path() / "direct.fq4dev";
    const bundle::Bundle original = make_bundle();
    const std::string bytes = bundle::encode(original);
    write_file(direct, bytes);
    const bundle::testing::PublishedArtifactExpectation expected{
        .byte_size = bytes.size(),
        .sha256 =
            old_school::artifact_integrity::
                sha256_string(bytes),
    };
    expect(
        bundle::testing::load_from(direct, expected) ==
            original,
        "fixed-identity loader did not load exact artifact");

    auto wrong = expected;
    ++wrong.byte_size;
    expect_rejected(
        [&] {
            (void)bundle::testing::load_from(direct, wrong);
        },
        "wrong expected byte count passed");
    wrong = expected;
    wrong.sha256[0] =
        wrong.sha256[0] == '0' ? '1' : '0';
    expect_rejected(
        [&] {
            (void)bundle::testing::load_from(direct, wrong);
        },
        "wrong expected artifact hash passed");

    const std::filesystem::path symlink =
        temporary.path() / "artifact-link.fq4dev";
    std::filesystem::create_symlink(direct, symlink);
    expect_rejected(
        [&] {
            (void)bundle::testing::load_from(
                symlink, expected);
        },
        "final-component artifact symlink passed");

    const std::filesystem::path published =
        temporary.path() / "published.fq4dev";
    bundle::testing::publish_atomic_no_replace_at(
        published, original);
    expect(
        read_file(published) == bytes,
        "atomic publisher wrote wrong bytes");
    expect_rejected(
        [&] {
            bundle::testing::publish_atomic_no_replace_at(
                published, original);
        },
        "atomic publisher replaced an existing artifact");
    expect(
        read_file(published) == bytes,
        "failed no-replace publication changed target");

    std::size_t temporary_count = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(
             temporary.path())) {
        if (entry.path().filename().string().find(
                ".tmp.") != std::string::npos) {
            ++temporary_count;
        }
    }
    expect(
        temporary_count == 0,
        "publication left a temporary file");
}

} // namespace

int main() {
    TestRunner runner;
    runner.run(
        "deterministic strict round trip",
        test_round_trip_is_deterministic);
    runner.run(
        "wire hashes order endian and EOF",
        test_wire_hashes_order_endian_and_eof);
    runner.run(
        "bounded counts before allocation",
        test_bounded_counts_reject_before_allocation);
    runner.run(
        "manifest and cross-section counts",
        test_manifest_and_cross_section_counts);
    runner.run(
        "census identity and selected consistency",
        test_census_identity_and_selection_consistency);
    runner.run(
        "descriptors Pass and dominance",
        test_descriptors_pass_and_dominance);
    runner.run(
        "sparse features and finite binary64",
        test_sparse_features_and_nonfinite_values);
    runner.run(
        "roles caps background and accounting",
        test_roles_caps_background_and_accounting);
    runner.run(
        "fixed loader and atomic no-replace",
        test_fixed_loader_and_atomic_no_replace);
    return runner.finish();
}
