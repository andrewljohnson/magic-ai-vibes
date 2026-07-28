#include "old_school/fq4_dev_bundle.hpp"
#include "old_school/fq4_dev_schedule.hpp"
#include "old_school/fq4_neutral_supplement.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

namespace artifact =
    old_school::fq4_neutral_supplement;
namespace bundle = old_school::fq4_dev_bundle;
namespace census =
    old_school::fq4_dev_coverage_census;
namespace schedule = old_school::fq4_dev_schedule;

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
        std::cout << passed_ << " passed, "
                  << failed_ << " failed\n";
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

template <typename Function>
void expect_rejected_containing(
    Function function, std::string_view expected,
    std::string_view message) {
    try {
        function();
    } catch (const std::exception& error) {
        if (std::string_view(error.what()).find(expected) !=
            std::string_view::npos) {
            return;
        }
        throw std::runtime_error(
            std::string(message) + ": wrong rejection: " +
            error.what());
    }
    throw std::runtime_error(std::string(message));
}

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        std::string pattern =
            (std::filesystem::temp_directory_path() /
             "old-school-fq4-neutral-XXXXXX")
                .string();
        std::vector<char> bytes(
            pattern.begin(), pattern.end());
        bytes.push_back('\0');
        char* const created = ::mkdtemp(bytes.data());
        if (created == nullptr) {
            throw std::system_error(
                errno, std::generic_category(),
                "cannot create neutral test directory");
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

bundle::Hash256 digest(std::string_view text) {
    return bundle::sha256(text);
}

bundle::Hash256 oracle_rank(
    std::string_view domain,
    const artifact::RankKey& key) {
    std::string bytes;
    const auto append_u64 =
        [&](std::uint64_t value) {
            for (std::size_t byte = 0;
                 byte < sizeof(value); ++byte) {
                bytes.push_back(static_cast<char>(
                    (value >> (byte * 8U)) & 0xffU));
            }
        };
    append_u64(static_cast<std::uint64_t>(domain.size()));
    bytes.append(domain);
    bytes.push_back(static_cast<char>(
        key.split == bundle::Split::Fit ? 0 : 1));
    bytes.push_back(
        static_cast<char>(key.owner_deck));
    bytes.push_back(
        static_cast<char>(key.schedule_block));
    bytes.append(
        reinterpret_cast<const char*>(
            key.physical_game_sha256.data()),
        key.physical_game_sha256.size());
    bytes.append(
        reinterpret_cast<const char*>(
            key.stable_root_id.data()),
        key.stable_root_id.size());
    return digest(bytes);
}

std::uint64_t oracle_production_seed(
    const bundle::Hash256& stable_root) {
    constexpr std::uint64_t offset =
        14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t result = offset;
    const auto byte =
        [&](std::uint8_t value) {
            result ^= value;
            result *= prime;
        };
    const auto number =
        [&](std::uint64_t value) {
            for (std::size_t index = 0;
                 index < sizeof(value); ++index) {
                byte(static_cast<std::uint8_t>(
                    value >> (index * 8U)));
            }
        };
    const auto text =
        [&](std::string_view value) {
            number(
                static_cast<std::uint64_t>(value.size()));
            for (const unsigned char value_byte : value) {
                byte(value_byte);
            }
        };
    text("old-school-environment-v3-cleanup-discard");
    text("old-school-oc1-action-regression-v1.production");
    text(bundle::format_sha256(stable_root));
    number(5787775625948253273ULL);
    return result;
}

struct CandidateFixture {
    std::vector<artifact::EligibleRoot> roots;
    std::map<bundle::Hash256, bundle::Hash256>
        information_by_stable;
};

census::CoverageCensus exact_capacity();

CandidateFixture candidate_fixture() {
    struct GameContext {
        bundle::Split split = bundle::Split::Fit;
        schedule::SourceGame source;
        std::uint8_t owner_seat = 0;
    };
    std::array<
        std::array<std::vector<GameContext>,
                   artifact::kDeckCount>,
        artifact::kSplitCount>
        games;
    for (std::size_t split_index = 0;
         split_index < artifact::kSplitCount;
         ++split_index) {
        const bundle::Split split =
            split_index == 0
                ? bundle::Split::Fit
                : bundle::Split::Check;
        const schedule::Split source_split =
            split_index == 0
                ? schedule::Split::Fit
                : schedule::Split::Check;
        for (const schedule::SourceGame& source :
             schedule::source_schedule(source_split)) {
            for (std::size_t seat = 0; seat < 2; ++seat) {
                const std::size_t deck =
                    static_cast<std::size_t>(
                        source.seat_decks[seat]);
                games[split_index][deck].push_back({
                    .split = split,
                    .source = source,
                    .owner_seat =
                        static_cast<std::uint8_t>(seat),
                });
            }
        }
    }

    const census::CoverageCensus capacity =
        exact_capacity();
    CandidateFixture result;
    for (std::size_t split_index = 0;
         split_index < artifact::kSplitCount;
         ++split_index) {
        const auto& split_capacity =
            split_index == 0
                ? capacity.fit
                : capacity.check;
        for (std::size_t deck = 0;
             deck < artifact::kDeckCount; ++deck) {
            const auto& contexts = games[split_index][deck];
            expect(
                contexts.size() == 64,
                "candidate fixture does not expose 64 games");
            const census::Count expected =
                split_capacity.decks[deck]
                    .eligible_stack_empty.all;
            expect(
                expected.roots >= contexts.size() &&
                    expected.options >=
                        expected.roots * 2 &&
                    expected.options <=
                        expected.roots *
                            bundle::kMaximumActions,
                "candidate fixture exact count is infeasible");
            std::size_t extra_options =
                expected.options - expected.roots * 2;
            for (std::size_t root_index = 0;
                 root_index < expected.roots;
                 ++root_index) {
                const GameContext& context =
                    contexts[root_index % contexts.size()];
                const schedule::SourceGame& source =
                    context.source;
                const std::size_t seat =
                    context.owner_seat;
                const std::uint32_t trace_ordinal =
                    static_cast<std::uint32_t>(
                        1U + root_index / contexts.size());
                const std::string identity =
                    std::to_string(split_index) + ":" +
                    std::to_string(deck) + ":" +
                    std::to_string(source.schedule_index) +
                    ":" + std::to_string(seat) + ":" +
                    std::to_string(trace_ordinal);
                const bundle::Hash256 information =
                    digest("information:" + identity);
                const bundle::Hash256 stable =
                    bundle::expected_stable_root_sha256(
                        context.split,
                        source.schedule_block,
                        source.schedule_index,
                        static_cast<std::uint8_t>(seat),
                        trace_ordinal, information);
                const std::size_t option_increment =
                    std::min<std::size_t>(
                        extra_options,
                        bundle::kMaximumActions - 2);
                extra_options -= option_increment;
                result.roots.push_back({
                    .rank = {
                        .split = context.split,
                        .owner_deck =
                            static_cast<std::uint8_t>(
                                source.seat_decks[seat]),
                        .schedule_block =
                            static_cast<std::uint8_t>(
                                source.schedule_block),
                        .physical_game_sha256 =
                            bundle::
                                expected_physical_game_sha256(
                                    context.split,
                                    source.schedule_block,
                                    source.schedule_index),
                        .stable_root_id = stable,
                    },
                    .schedule_index =
                        static_cast<std::uint16_t>(
                            source.schedule_index),
                    .owner_seat =
                        static_cast<std::uint8_t>(seat),
                    .owner_on_play =
                        source.starting_player == seat,
                    .opponent_deck =
                        static_cast<std::uint8_t>(
                            source.seat_decks[1U - seat]),
                    .trace_ordinal = trace_ordinal,
                    .legal_action_count =
                        static_cast<std::uint8_t>(
                            2 + option_increment),
                    .retained_nontrivial = true,
                    .public_stack_size = 0,
                    .dominance_positive = false,
                    .existing_selected_roles = 0,
                });
                result.information_by_stable.emplace(
                    stable, information);
            }
            expect(
                extra_options == 0,
                "candidate fixture left legal options");
        }
    }
    expect(
        result.roots.size() == 8622,
        "candidate fixture does not match exact root count");
    return result;
}

census::Count count(
    std::size_t roots, std::size_t options,
    std::size_t games) {
    return {
        .roots = roots,
        .options = options,
        .distinct_games = games,
    };
}

struct DeckFacts {
    census::Count retained;
    census::Count positive_empty;
    census::Count positive_active;
    census::Count negative_empty;
    census::Count negative_active;
    census::Count main;
    census::Count other;
    std::size_t background_options = 0;
};

census::CoverageCensus exact_capacity() {
    const std::array<DeckFacts, 10> facts{{
        {count(921, 2318, 64), count(16, 50, 11),
         count(0, 0, 0), count(892, 2232, 64),
         count(12, 34, 12), count(887, 2221, 64),
         count(5, 11, 5), 2},
        {count(978, 3166, 64), count(10, 65, 4),
         count(0, 0, 0), count(948, 3019, 64),
         count(19, 80, 17), count(921, 2878, 64),
         count(27, 141, 11), 2},
        {count(1002, 2564, 64), count(35, 210, 31),
         count(54, 180, 30), count(683, 1616, 64),
         count(229, 556, 62), count(683, 1616, 64),
         count(0, 0, 0), 2},
        {count(1001, 3155, 64), count(6, 26, 6),
         count(7, 21, 7), count(906, 2835, 64),
         count(81, 271, 35), count(903, 2826, 64),
         count(3, 9, 1), 2},
        {count(969, 3062, 64), count(51, 509, 29),
         count(0, 0, 0), count(912, 2527, 64),
         count(5, 23, 5), count(902, 2487, 64),
         count(10, 40, 8), 3},
        {count(942, 2443, 64), count(22, 60, 20),
         count(0, 0, 0), count(909, 2350, 64),
         count(10, 31, 9), count(899, 2319, 64),
         count(10, 31, 7), 2},
        {count(977, 3226, 64), count(10, 64, 5),
         count(0, 0, 0), count(943, 3033, 64),
         count(23, 127, 18), count(915, 2873, 64),
         count(28, 160, 18), 2},
        {count(995, 2517, 64), count(32, 213, 30),
         count(46, 146, 24), count(688, 1597, 64),
         count(228, 559, 63), count(688, 1597, 64),
         count(0, 0, 0), 2},
        {count(993, 3031, 64), count(2, 7, 1),
         count(6, 18, 6), count(909, 2751, 64),
         count(75, 253, 33), count(908, 2748, 64),
         count(1, 3, 1), 2},
        {count(950, 3626, 64), count(112, 1250, 42),
         count(0, 0, 0), count(832, 2346, 64),
         count(5, 27, 4), count(818, 2282, 64),
         count(14, 64, 9), 3},
    }};

    census::CoverageCensus result;
    for (std::size_t split = 0; split < 2; ++split) {
        census::SplitCensus& target =
            split == 0 ? result.fit : result.check;
        for (std::size_t deck = 0;
             deck < artifact::kDeckCount; ++deck) {
            const DeckFacts& source =
                facts[split * artifact::kDeckCount + deck];
            census::DeckCensus& output =
                target.decks[deck];
            output.retained = source.retained;
            output.dominance_positive = {
                .empty = source.positive_empty,
                .active = source.positive_active,
            };
            output.selected_background = {
                .empty =
                    count(
                        1, source.background_options, 1),
                .active = count(0, 0, 0),
            };
            output.unselected_dominance_negative = {
                .empty = source.negative_empty,
                .active = source.negative_active,
            };
            output.eligible_stack_empty = {
                .all = source.negative_empty,
                .first_or_second_main = source.main,
                .other = source.other,
            };
            for (census::BlockEligibility& block :
                 output.eligible_blocks) {
                block.distinct_games = 16;
                block.balanced_eight_feasible = true;
                for (std::size_t opponent = 0;
                     opponent < artifact::kDeckCount;
                     ++opponent) {
                    for (std::size_t quadrant = 0;
                         quadrant <
                             artifact::kQuadrantCount;
                         ++quadrant) {
                        block.games_by_opponent_quadrant
                            [opponent][quadrant] =
                                opponent == deck ? 0 : 1;
                    }
                }
            }
            output.capacity_met = true;
        }
    }
    result.retained_rows = 9728;
    result.retained_options = 29108;
    result.selected_rows = 192;
    result.selected_positive_rows = 182;
    result.selected_background_rows = 10;
    result.action_invariant_stack_rows = 9728;
    result.exact_public_stack_rows = 9728;
    result.valid_public_phase_rows = 9728;
    result.capacity_licensed = true;
    return result;
}

artifact::NeutralAction neutral_action(bool is_pass) {
    artifact::NeutralAction result{
        .is_pass = is_pass,
        .base_score_bits =
            std::bit_cast<std::uint64_t>(0.5),
        .parent_residual_bits =
            std::bit_cast<std::uint64_t>(0.0),
    };
    for (std::size_t world = 0;
         world < bundle::kWorldCount; ++world) {
        result.raw_sample_bits[world] =
            std::bit_cast<std::uint64_t>(0.5);
        result.shallow_prior_sample_bits[world] =
            std::bit_cast<std::uint64_t>(0.5);
        result.continuation_sample_bits[world] =
            std::bit_cast<std::uint64_t>(0.5);
    }
    return result;
}

artifact::Artifact valid_artifact() {
    const CandidateFixture candidates =
        candidate_fixture();
    const artifact::FrozenSelection selection =
        artifact::freeze_selection(
            candidates.roots, exact_capacity());
    artifact::Artifact result;
    result.manifest.contract =
        artifact::make_contract(
            bundle::load_published().manifest,
            exact_capacity());
    result.manifest.producer_commit =
        std::string(40, 'a');
    result.manifest.producer_executable_sha256 =
        digest("test executable");
    result.manifest.selected_order_sha256 =
        selection.selected_order_sha256;
    result.rows.reserve(selection.rows.size());
    bundle::ScoreAccounting neutral;
    for (const artifact::RankedLocator& locator :
         selection.rows) {
        const auto information =
            candidates.information_by_stable.find(
                locator.root.rank.stable_root_id);
        expect(
            information !=
                candidates.information_by_stable.end(),
            "selected row lacks information digest");
        const std::uint64_t action_count =
            locator.root.legal_action_count;
        artifact::NeutralRow row{
            .locator = locator,
            .information_action_sha256 =
                information->second,
            .descriptor_set_sha256 =
                digest(
                    "descriptors:" +
                    bundle::format_sha256(
                        locator.root.rank.stable_root_id)),
            .pass_index = 0,
            .production_seed =
                artifact::production_seed_for_stable_root(
                    locator.root.rank.stable_root_id),
            .hidden_clone_eligible = false,
            .hidden_clone_distinct = false,
            .accounting = {
                .score_calls = 1,
                .scored_actions = action_count,
                .sampled_worlds = 8,
                .rollout_evaluations =
                    action_count * bundle::kWorldCount,
                .terminal_evaluations = 0,
                .bootstrap_evaluations =
                    action_count * bundle::kWorldCount,
            },
        };
        row.actions.reserve(
            locator.root.legal_action_count);
        row.actions.push_back(neutral_action(true));
        while (row.actions.size() <
               locator.root.legal_action_count) {
            row.actions.push_back(neutral_action(false));
        }
        ++neutral.score_calls;
        neutral.scored_actions += action_count;
        neutral.sampled_worlds += bundle::kWorldCount;
        neutral.rollout_evaluations +=
            action_count * bundle::kWorldCount;
        neutral.bootstrap_evaluations +=
            action_count * bundle::kWorldCount;
        result.rows.push_back(std::move(row));
    }
    result.manifest.accounting = {
        .reconstruction = {
            .source_games = 320,
            .retained_roots = 9728,
            .retained_options = 29108,
            .parent_scoring = {
                .score_calls = 192,
                .scored_actions = 1141,
                .sampled_worlds = 1536,
                .rollout_evaluations = 9128,
                .terminal_evaluations = 1787,
                .bootstrap_evaluations = 7341,
            },
        },
        .canonical_neutral = neutral,
        .hidden_clone = neutral,
        .bit_identical_actions = neutral.scored_actions,
        .selection_frozen_before_scoring = true,
        .dev1_scientific_sections_exact = true,
        .canonical_hidden_bit_identical = true,
        .parent_immutable = true,
        .bundle_immutable = true,
        .executable_immutable = true,
        .parent_models_loaded = 1,
    };
    for (auto& split :
         result.manifest.accounting
             .nondistinct_hidden_controls) {
        split.fill(32);
    }
    artifact::validate(result);
    return result;
}

void test_rank_framing_and_selection() {
    const CandidateFixture candidates =
        candidate_fixture();
    const census::CoverageCensus capacity =
        exact_capacity();
    expect(
        artifact::accepted_dev4_capacity() == capacity,
        "public accepted DEV4 contract drifted from the "
        "independent exact fixture");
    const bundle::Bundle published =
        bundle::load_published();
    const auto verify_published_seeds =
        [](const std::vector<bundle::SelectedRow>& rows) {
            for (const bundle::SelectedRow& row : rows) {
                expect(
                    row.production_seed ==
                            artifact::
                                production_seed_for_stable_root(
                                    row.census.stable_root_id) &&
                        row.production_seed ==
                            oracle_production_seed(
                                row.census.stable_root_id),
                    "fixed production seed derivation does not "
                    "reproduce accepted DEV1");
            }
        };
    verify_published_seeds(published.fit_rows);
    verify_published_seeds(published.check_rows);
    const artifact::RankKey& key =
        candidates.roots.front().rank;
    const std::string preimage =
        artifact::rank_preimage(
            artifact::kRepresentativeRankDomain, key);
    const std::uint64_t domain_size =
        artifact::kRepresentativeRankDomain.size();
    expect(
        preimage.size() ==
            8 + domain_size + 3 + 32 + 32,
        "rank preimage has the wrong size");
    std::uint64_t encoded_size = 0;
    for (std::size_t byte = 0; byte < 8; ++byte) {
        encoded_size |=
            static_cast<std::uint64_t>(
                static_cast<unsigned char>(
                    preimage[byte]))
            << (8U * byte);
    }
    expect(
        encoded_size == domain_size &&
            preimage.substr(8, domain_size) ==
                artifact::kRepresentativeRankDomain,
        "rank domain framing drifted");

    const artifact::FrozenSelection first =
        artifact::freeze_selection(
            candidates.roots, capacity);
    auto reversed = candidates.roots;
    std::reverse(reversed.begin(), reversed.end());
    const artifact::FrozenSelection second =
        artifact::freeze_selection(reversed, capacity);
    std::vector<artifact::EligibleRoot> fit_candidates;
    std::vector<artifact::EligibleRoot> check_candidates;
    for (const artifact::EligibleRoot& root :
         candidates.roots) {
        (root.rank.split == bundle::Split::Fit
             ? fit_candidates
             : check_candidates)
            .push_back(root);
    }
    std::vector<artifact::RankedLocator> split_rows =
        artifact::freeze_split_selection(
            bundle::Split::Fit, fit_candidates,
            capacity);
    const auto check_rows =
        artifact::freeze_split_selection(
            bundle::Split::Check, check_candidates,
            capacity);
    split_rows.insert(
        split_rows.end(),
        check_rows.begin(), check_rows.end());
    expect(
        first == second &&
            split_rows == first.rows &&
            first.rows.size() == artifact::kTotalRows &&
            first.selected_order_sha256 ==
                artifact::selected_order_sha256(
                    first.rows),
        "selection is not input-order invariant");

    struct Population {
        std::size_t roots = 0;
        std::size_t options = 0;
        std::set<bundle::Hash256> games;
    };
    std::array<
        std::array<Population, artifact::kDeckCount>,
        artifact::kSplitCount>
        population;
    using OracleGameKey =
        std::tuple<std::uint8_t, std::uint8_t,
                   std::uint8_t, bundle::Hash256>;
    std::map<
        OracleGameKey,
        std::pair<bundle::Hash256, bundle::Hash256>>
        representative_by_game;
    bool saw_multiple_roots = false;
    std::map<OracleGameKey, std::size_t> roots_by_game;
    for (const artifact::EligibleRoot& root :
         candidates.roots) {
        const std::size_t split =
            root.rank.split == bundle::Split::Fit ? 0 : 1;
        Population& observed =
            population[split][root.rank.owner_deck];
        ++observed.roots;
        observed.options += root.legal_action_count;
        observed.games.insert(
            root.rank.physical_game_sha256);
        const OracleGameKey game{
            static_cast<std::uint8_t>(split),
            root.rank.owner_deck,
            root.rank.schedule_block,
            root.rank.physical_game_sha256};
        saw_multiple_roots =
            ++roots_by_game[game] > 1 ||
            saw_multiple_roots;
        const bundle::Hash256 rank =
            oracle_rank(
                artifact::kRepresentativeRankDomain,
                root.rank);
        const auto found =
            representative_by_game.find(game);
        if (found == representative_by_game.end() ||
            rank < found->second.first) {
            representative_by_game[game] = {
                rank, root.rank.stable_root_id};
        }
    }
    expect(
        saw_multiple_roots,
        "fixture did not exercise multiple roots per game");
    for (std::size_t split = 0;
         split < artifact::kSplitCount; ++split) {
        const auto& split_capacity =
            split == 0 ? capacity.fit : capacity.check;
        for (std::size_t deck = 0;
             deck < artifact::kDeckCount; ++deck) {
            const census::Count expected =
                split_capacity.decks[deck]
                    .eligible_stack_empty.all;
            expect(
                population[split][deck].roots ==
                        expected.roots &&
                    population[split][deck].options ==
                        expected.options &&
                    population[split][deck].games.size() ==
                        expected.distinct_games,
                "independent population oracle disagrees with "
                "the accepted census");
        }
    }
    for (const artifact::RankedLocator& row :
         first.rows) {
        const OracleGameKey game{
            static_cast<std::uint8_t>(
                row.root.rank.split ==
                        bundle::Split::Fit
                    ? 0
                    : 1),
            row.root.rank.owner_deck,
            row.root.rank.schedule_block,
            row.root.rank.physical_game_sha256};
        const auto found =
            representative_by_game.find(game);
        expect(
            found != representative_by_game.end() &&
                row.representative_rank ==
                    found->second.first &&
                row.root.rank.stable_root_id ==
                    found->second.second &&
                artifact::production_seed_for_stable_root(
                    row.root.rank.stable_root_id) ==
                    oracle_production_seed(
                        row.root.rank.stable_root_id),
            "selector did not retain the independent minimum "
            "representative or exact production seed");
    }

    std::array<
        std::array<
            std::array<std::size_t, artifact::kBlockCount>,
            artifact::kDeckCount>,
        artifact::kSplitCount>
        blocks{};
    for (const artifact::RankedLocator& row : first.rows) {
        const std::size_t split =
            row.root.rank.split == bundle::Split::Fit
                ? 0
                : 1;
        ++blocks[split][row.root.rank.owner_deck]
                [row.root.rank.schedule_block];
    }
    for (const auto& split : blocks) {
        for (const auto& deck : split) {
            for (const std::size_t block : deck) {
                expect(
                    block == artifact::kRowsPerBlock,
                    "selection block is not eight rows");
            }
        }
    }
}

void test_selection_rejects_collisions_and_capacity_loss() {
    const CandidateFixture candidates =
        candidate_fixture();
    const census::CoverageCensus capacity =
        exact_capacity();
    std::size_t calls = 0;
    std::optional<bundle::Hash256> first_digest;
    expect_rejected(
        [&] {
            static_cast<void>(
                artifact::testing::
                    freeze_selection_with_rank_function(
                        candidates.roots,
                        capacity,
                        [&](std::string_view preimage) {
                            const bundle::Hash256 real =
                                digest(preimage);
                            if (calls++ == 0) {
                                first_digest = real;
                                return real;
                            }
                            if (calls == 2) {
                                return *first_digest;
                            }
                            return real;
                        }));
        },
        "cross-domain rank collision was accepted");

    const artifact::FrozenSelection selected =
        artifact::freeze_selection(
            candidates.roots, capacity);
    std::set<bundle::Hash256> selected_stable;
    for (const artifact::RankedLocator& row :
         selected.rows) {
        selected_stable.insert(
            row.root.rank.stable_root_id);
    }
    auto missing = candidates.roots;
    const auto omitted =
        std::find_if(
            missing.begin(), missing.end(),
            [&](const artifact::EligibleRoot& root) {
                return !selected_stable.contains(
                    root.rank.stable_root_id);
            });
    expect(
        omitted != missing.end(),
        "omission test lacks a nonrepresentative root");
    missing.erase(omitted);
    expect_rejected(
        [&] {
            static_cast<void>(
                artifact::freeze_selection(
                    missing, capacity));
        },
        "omitted nonrepresentative census root was accepted");

    auto missing_option = candidates.roots;
    const auto option =
        std::find_if(
            missing_option.begin(),
            missing_option.end(),
            [](const artifact::EligibleRoot& root) {
                return root.legal_action_count > 2;
            });
    expect(
        option != missing_option.end(),
        "option omission test lacks a wide root");
    --option->legal_action_count;
    expect_rejected(
        [&] {
            static_cast<void>(
                artifact::freeze_selection(
                    missing_option, capacity));
        },
        "omitted eligible legal option was accepted");

    auto duplicate = candidates.roots;
    duplicate.push_back(duplicate.front());
    expect_rejected(
        [&] {
            static_cast<void>(
                artifact::freeze_selection(
                    duplicate, capacity));
        },
        "duplicate eligible locator was accepted");

    census::CoverageCensus forged_capacity = capacity;
    --forged_capacity.fit.decks[0]
          .eligible_stack_empty.all.roots;
    --forged_capacity.fit.decks[0]
          .eligible_stack_empty.first_or_second_main.roots;
    --forged_capacity.fit.decks[0]
          .unselected_dominance_negative.empty.roots;
    expect_rejected(
        [&] {
            static_cast<void>(
                artifact::freeze_selection(
                    candidates.roots,
                    forged_capacity));
        },
        "coherently shifted DEV4 population was accepted");
}

void recompute_artifact_order_and_accounting(
    artifact::Artifact& value) {
    const auto split_byte =
        [](bundle::Split split) {
            return static_cast<std::uint8_t>(
                split == bundle::Split::Fit ? 0 : 1);
        };
    std::sort(
        value.rows.begin(), value.rows.end(),
        [&](const artifact::NeutralRow& left,
            const artifact::NeutralRow& right) {
            return std::tuple{
                       split_byte(
                           left.locator.root.rank.split),
                       left.locator.root.rank.owner_deck,
                       left.locator.root.rank.schedule_block,
                       left.locator.game_rank} <
                   std::tuple{
                       split_byte(
                           right.locator.root.rank.split),
                       right.locator.root.rank.owner_deck,
                       right.locator.root.rank.schedule_block,
                       right.locator.game_rank};
        });
    std::vector<artifact::RankedLocator> locators;
    locators.reserve(value.rows.size());
    bundle::ScoreAccounting total;
    for (const artifact::NeutralRow& row : value.rows) {
        locators.push_back(row.locator);
        total.score_calls += row.accounting.score_calls;
        total.scored_actions +=
            row.accounting.scored_actions;
        total.sampled_worlds +=
            row.accounting.sampled_worlds;
        total.rollout_evaluations +=
            row.accounting.rollout_evaluations;
        total.terminal_evaluations +=
            row.accounting.terminal_evaluations;
        total.bootstrap_evaluations +=
            row.accounting.bootstrap_evaluations;
    }
    value.manifest.selected_order_sha256 =
        artifact::selected_order_sha256(locators);
    value.manifest.accounting.canonical_neutral = total;
    value.manifest.accounting.hidden_clone = total;
    value.manifest.accounting.bit_identical_actions =
        total.scored_actions;
}

artifact::Artifact duplicate_physical_game_mutation(
    artifact::Artifact value) {
    std::vector<std::size_t> cell;
    for (std::size_t index = 0;
         index < value.rows.size(); ++index) {
        const auto& root = value.rows[index].locator.root;
        if (root.rank.split == bundle::Split::Fit &&
            root.rank.owner_deck == 0 &&
            root.rank.schedule_block == 0) {
            cell.push_back(index);
        }
    }
    expect(
        cell.size() == artifact::kRowsPerBlock,
        "duplicate-game mutation cell is not eight");

    std::vector<std::size_t> matching;
    for (std::uint32_t mask = 0;
         mask < (1U << cell.size()); ++mask) {
        if (std::popcount(mask) != 4) {
            continue;
        }
        std::set<std::uint8_t> opponents;
        std::set<std::size_t> quadrants;
        std::vector<std::size_t> candidate;
        for (std::size_t offset = 0;
             offset < cell.size(); ++offset) {
            if ((mask & (1U << offset)) == 0) {
                continue;
            }
            const auto& root =
                value.rows[cell[offset]].locator.root;
            opponents.insert(root.opponent_deck);
            quadrants.insert(
                static_cast<std::size_t>(
                    root.owner_seat) *
                        2U +
                    (root.owner_on_play ? 0U : 1U));
            candidate.push_back(cell[offset]);
        }
        if (opponents.size() == 4 &&
            quadrants.size() == 4) {
            matching = std::move(candidate);
            break;
        }
    }
    expect(
        matching.size() == 4,
        "selected balanced graph lacks a perfect matching");
    std::set<std::size_t> matching_set(
        matching.begin(), matching.end());
    std::vector<std::size_t> complement;
    for (std::size_t index : cell) {
        if (!matching_set.contains(index)) {
            complement.push_back(index);
        }
    }
    expect(
        complement.size() == matching.size(),
        "matching complement has the wrong size");

    for (std::size_t index = 0;
         index < matching.size(); ++index) {
        artifact::NeutralRow clone =
            value.rows[matching[index]];
        clone.locator.root.trace_ordinal =
            static_cast<std::uint32_t>(10'000 + index);
        clone.information_action_sha256 =
            digest(
                "duplicate-physical-information:" +
                std::to_string(index));
        clone.locator.root.rank.stable_root_id =
            bundle::expected_stable_root_sha256(
                clone.locator.root.rank.split,
                clone.locator.root.rank.schedule_block,
                clone.locator.root.schedule_index,
                clone.locator.root.owner_seat,
                clone.locator.root.trace_ordinal,
                clone.information_action_sha256);
        clone.locator.representative_rank =
            artifact::representative_rank(
                clone.locator.root.rank);
        clone.locator.game_rank =
            artifact::game_rank(
                clone.locator.root.rank);
        clone.descriptor_set_sha256 =
            digest(
                "duplicate-physical-descriptor:" +
                std::to_string(index));
        clone.production_seed =
            artifact::production_seed_for_stable_root(
                clone.locator.root.rank.stable_root_id);
        value.rows[complement[index]] =
            std::move(clone);
    }
    recompute_artifact_order_and_accounting(value);
    return value;
}

void test_wire_round_trip_and_semantic_mutations() {
    const artifact::Artifact original = valid_artifact();
    const std::string bytes = artifact::encode(original);
    expect(
        artifact::decode(bytes) == original,
        "neutral artifact round trip drifted");

    std::string trailing = bytes;
    trailing.push_back('\0');
    expect_rejected(
        [&] {
            static_cast<void>(artifact::decode(trailing));
        },
        "trailing bytes were accepted");

    std::string corrupt = bytes;
    corrupt[corrupt.size() / 2] ^= 1;
    expect_rejected(
        [&] {
            static_cast<void>(artifact::decode(corrupt));
        },
        "payload corruption was accepted");

    artifact::Artifact wrong_stable = original;
    wrong_stable.rows.front()
        .information_action_sha256 =
            digest("wrong information");
    expect_rejected(
        [&] {
            static_cast<void>(
                artifact::decode(
                    artifact::testing::
                        encode_wire_unchecked(
                            wrong_stable)));
        },
        "stable-root forgery was accepted");

    artifact::Artifact stack_feature = original;
    stack_feature.rows.front().actions.front()
        .features.push_back({
            .index =
                static_cast<std::uint16_t>(
                    census::kStackSizeFeatureIndex),
            .value_bits =
                std::bit_cast<std::uint64_t>(0.2),
        });
    expect_rejected(
        [&] {
            static_cast<void>(
                artifact::decode(
                    artifact::testing::
                        encode_wire_unchecked(
                            stack_feature)));
        },
        "nonempty stack feature was accepted");

    artifact::Artifact bad_ledger = original;
    ++bad_ledger.manifest.accounting
          .canonical_neutral.scored_actions;
    expect_rejected(
        [&] {
            static_cast<void>(
                artifact::decode(
                    artifact::testing::
                        encode_wire_unchecked(
                            bad_ledger)));
        },
        "forged neutral ledger was accepted");

    artifact::Artifact duplicate_games =
        duplicate_physical_game_mutation(original);
    expect_rejected_containing(
        [&] {
            static_cast<void>(
                artifact::decode(
                    artifact::testing::
                        encode_wire_unchecked(
                            duplicate_games)));
        },
        "eight distinct physical games",
        "coherently rehashed duplicate physical games "
        "were accepted");

    artifact::Artifact wrong_seed = original;
    ++wrong_seed.rows.front().production_seed;
    expect_rejected_containing(
        [&] {
            static_cast<void>(
                artifact::decode(
                    artifact::testing::
                        encode_wire_unchecked(
                            wrong_seed)));
        },
        "neutral row shape",
        "coherently rehashed wrong production seed "
        "was accepted");

    artifact::Artifact wrong_dev1 = original;
    wrong_dev1.manifest.contract.dev1.fit
        .trajectory_sha256 =
            digest("forged exact DEV1 trajectory");
    expect_rejected_containing(
        [&] {
            static_cast<void>(
                artifact::decode(
                    artifact::testing::
                        encode_wire_unchecked(
                            wrong_dev1)));
        },
        "scientific split source binding",
        "coherently rehashed DEV1 science mutation "
        "was accepted");

    artifact::Artifact wrong_dev4 = original;
    auto& green =
        wrong_dev4.manifest.contract.dev4_capacity
            .fit.decks[0];
    --green.eligible_stack_empty.all.roots;
    --green.eligible_stack_empty
          .first_or_second_main.roots;
    --green.unselected_dominance_negative.empty.roots;
    expect_rejected_containing(
        [&] {
            static_cast<void>(
                artifact::decode(
                    artifact::testing::
                        encode_wire_unchecked(
                            wrong_dev4)));
        },
        "accepted exact census",
        "coherently rehashed DEV4 science mutation "
        "was accepted");
}

void test_no_replace_publication_and_exact_load() {
    const artifact::Artifact original = valid_artifact();
    TemporaryDirectory temporary;
    const std::filesystem::path destination =
        temporary.path() / "neutral.fq4neutral";
    const artifact::PublicationReport report =
        artifact::testing::publish_atomic_no_replace_at(
            destination, original);
    expect(
        report.artifact.bytes > 0 &&
            report.manifest == original.manifest &&
            artifact::testing::load_from(
                destination,
                original.manifest.contract,
                report.artifact) ==
                original &&
            !std::filesystem::exists(
                artifact::testing::temporary_path_for(
                    destination)),
        "publication or exact reload drifted");
    expect_rejected(
        [&] {
            static_cast<void>(
                artifact::testing::
                    publish_atomic_no_replace_at(
                        destination, original));
        },
        "artifact replacement was accepted");

    artifact::FileIdentity wrong = report.artifact;
    wrong.sha256 =
        bundle::format_sha256(digest("wrong file"));
    expect_rejected(
        [&] {
            static_cast<void>(
                artifact::testing::load_from(
                    destination,
                    original.manifest.contract,
                    wrong));
        },
        "wrong file identity was accepted");
}

} // namespace

int main() {
    TestRunner tests;
    tests.run(
        "rank framing and deterministic balanced selection",
        test_rank_framing_and_selection);
    tests.run(
        "selection rejects collisions and capacity loss",
        test_selection_rejects_collisions_and_capacity_loss);
    tests.run(
        "strict wire round trip and semantic mutations",
        test_wire_round_trip_and_semantic_mutations);
    tests.run(
        "atomic no-replace publication and exact load",
        test_no_replace_publication_and_exact_load);
    return tests.finish();
}
