#include "old_school/fq4_dev_generator.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/fq0_information_set.hpp"
#include "old_school/fq4_neutral_supplement.hpp"
#include "old_school/fq4_priority_math.hpp"
#include "old_school/oc1_action_scoring.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace old_school::fq4_dev_generator {
namespace {

namespace bundle = fq4_dev_bundle;
namespace cache = fq4_work0_cache;
namespace collection = fq4_priority_collection;
namespace coverage = fq4_dev_coverage_census;
namespace integrity = artifact_integrity;
namespace math = fq4_priority_math;
namespace neutral = fq4_neutral_supplement;
namespace information = fq0_information_set;
namespace schedule_data = fq4_dev_schedule;
namespace scoring = oc1_action_scoring;

constexpr std::size_t kPlayerCount = 2;
constexpr std::size_t kExpectedCardCount = 26;
constexpr std::size_t kStateScalarFeatureCount = 50;
constexpr std::size_t kStateCardPlaneCount = 24;
constexpr std::size_t kPolicyDecisionCount = 4;
constexpr std::size_t kPolicyPhaseCount = 7;
constexpr std::size_t kPolicyVerbCount = 8;
constexpr std::size_t kPolicyCardPlaneCount = 6;
constexpr std::size_t kPolicyScalarCount = 44;

static_assert(kCardCount == kExpectedCardCount);
static_assert(
    schedule_data::kFitSeedBase ==
    bundle::kFitSeedBase);
static_assert(
    schedule_data::kCheckSeedBase ==
    bundle::kCheckSeedBase);
static_assert(
    schedule_data::kGenerationNamespace ==
    bundle::kGenerationNamespace);
static_assert(
    kStateScalarFeatureCount +
        kStateCardPlaneCount * kExpectedCardCount +
        kPolicyDecisionCount + kPolicyPhaseCount +
        kPolicyVerbCount +
        kPolicyCardPlaneCount * kExpectedCardCount +
        kPolicyScalarCount ==
    bundle::kFeatureCount);

[[noreturn]] void fail(std::string_view message) {
    throw std::runtime_error(
        "FQ4 development generator: " +
        std::string(message));
}

std::size_t deck_index(DeckId deck) {
    const std::size_t result =
        static_cast<std::size_t>(deck);
    if (result >= kDeckCount) {
        fail("deck is outside the five-deck field");
    }
    return result;
}

std::vector<CardId> cards_for_deck(DeckId deck) {
    switch (deck) {
    case DeckId::Green:
        return green_deck();
    case DeckId::Red:
        return red_deck();
    case DeckId::Blue:
        return blue_deck();
    case DeckId::White:
        return white_control_deck();
    case DeckId::RUAggro:
        return ru_aggro_deck();
    }
    fail("unknown source deck");
}

bool same_double(double first, double second) {
    return std::bit_cast<std::uint64_t>(first) ==
           std::bit_cast<std::uint64_t>(second);
}

bool bit_identical(
    const std::vector<double>& first,
    const std::vector<double>& second) {
    if (first.size() != second.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < first.size(); ++index) {
        if (!same_double(first[index], second[index])) {
            return false;
        }
    }
    return true;
}

bool bit_identical(
    const std::vector<std::vector<double>>& first,
    const std::vector<std::vector<double>>& second) {
    if (first.size() != second.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < first.size(); ++index) {
        if (!bit_identical(first[index], second[index])) {
            return false;
        }
    }
    return true;
}

class CanonicalBytes {
  public:
    void u8(std::uint8_t value) {
        bytes_.push_back(static_cast<char>(value));
    }

    void u64(std::uint64_t value) {
        for (std::size_t byte = 0; byte < 8; ++byte) {
            u8(static_cast<std::uint8_t>(
                (value >> (byte * 8U)) & 0xffU));
        }
    }

    void boolean(bool value) {
        u8(value ? 1U : 0U);
    }

    void text(std::string_view value) {
        u64(static_cast<std::uint64_t>(value.size()));
        bytes_.append(value);
    }

    void hash(const bundle::Hash256& digest) {
        for (const std::uint8_t byte : digest) {
            u8(byte);
        }
    }

    void binary64(double value) {
        u64(std::bit_cast<std::uint64_t>(value));
    }

    const std::string& bytes() const {
        return bytes_;
    }

  private:
    std::string bytes_;
};

bundle::Hash256 domain_hash(
    std::string_view domain,
    const CanonicalBytes& payload) {
    CanonicalBytes framed;
    framed.text(kGeneratorSchema);
    framed.text(domain);
    framed.text(payload.bytes());
    return bundle::sha256(framed.bytes());
}

bool canonical_lower_hex(
    std::string_view value, std::size_t size) {
    return value.size() == size &&
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

bool forbidden_seed(std::uint64_t seed) {
    return std::find(
               kForbiddenSourceSeedBases.begin(),
               kForbiddenSourceSeedBases.end(),
               seed) !=
           kForbiddenSourceSeedBases.end();
}

collection::SourceGame collection_source(
    const schedule_data::SourceGame& source) {
    return {
        .source_block = source.schedule_block,
        .source_seed_base = source.source_seed_base,
        .schedule_index = source.schedule_index,
        .pairing_index = source.pairing_index,
        .seat_decks = source.seat_decks,
        .starting_player = source.starting_player,
        .game_seed = source.game_seed,
    };
}

std::array<std::vector<CardId>, 2> original_decks(
    const schedule_data::SourceGame& source) {
    return {
        cards_for_deck(source.seat_decks[0]),
        cards_for_deck(source.seat_decks[1]),
    };
}

schedule_data::Split split_for_seed_base(
    std::uint64_t source_seed_base) {
    if (source_seed_base ==
        schedule_data::kFitSeedBase) {
        return schedule_data::Split::Fit;
    }
    if (source_seed_base ==
        schedule_data::kCheckSeedBase) {
        return schedule_data::Split::Check;
    }
    throw std::invalid_argument(
        "root source seed is outside frozen FIT/CHECK");
}

bool root_matches_frozen_schedule(
    const collection::ReplayRootManifest& root) {
    const collection::RootLocator& locator =
        root.locator;
    if (locator.source_block >=
            schedule_data::kScheduleBlocks ||
        locator.schedule_index >=
            schedule_data::kPhysicalGamesPerSplit ||
        locator.owner_seat >= kPlayerCount) {
        return false;
    }
    try {
        const auto schedule =
            schedule_data::source_schedule(
                split_for_seed_base(
                    locator.source_seed_base));
        const auto& source =
            schedule.at(locator.schedule_index);
        return
            source.schedule_block ==
                locator.source_block &&
            source.source_seed_base ==
                locator.source_seed_base &&
            source.schedule_index ==
                locator.schedule_index &&
            source.game_seed == locator.game_seed &&
            root.owner_deck ==
                source.seat_decks[
                    locator.owner_seat] &&
            root.opponent_deck ==
                source.seat_decks[
                    1U - locator.owner_seat];
    } catch (const std::exception&) {
        return false;
    }
}

GameConfig source_game_config(
    const std::shared_ptr<const LearnedModel>& parent,
    std::size_t starting_player) {
    const BotConfig bot{
        .kind = BotKind::Learned,
        .learned_variant =
            LearnedVariant::ValueSearchChampion,
        .rollouts_per_action =
            scoring::kProductionWorlds,
        .exploration_rate = 0.0,
        .value_continuation_epsilon = 0.0,
        .value_priority_residual_weight = 0.0,
        .value_pass_dominance = false,
        .value_adversarial_blocks = false,
        .value_continuation_controller =
            LearnedContinuationController::Legacy,
        .training_games = kParentTrainingGames,
        .learned_model = parent,
    };
    return {
        .max_turns = kSourceTurnCap,
        .starting_player = starting_player,
        .bots = {bot, bot},
        .learned_training_seed =
            kParentTrainingSeed,
        .learned_model = parent,
        .learned_search_depth = 1,
    };
}

bool source_config_exact(
    const GameConfig& config,
    const std::shared_ptr<const LearnedModel>& parent,
    std::size_t starting_player) {
    if (config.max_turns != kSourceTurnCap ||
        config.starting_player !=
            std::optional<std::size_t>(
                starting_player) ||
        config.learned_training_seed !=
            kParentTrainingSeed ||
        config.learned_search_depth != 1 ||
        config.learned_model != parent ||
        config.learned_policy_recorder != nullptr ||
        std::any_of(
            config.human_controllers.begin(),
            config.human_controllers.end(),
            [](const auto& controller) {
                return controller.has_value();
            })) {
        return false;
    }
    for (const BotConfig& bot : config.bots) {
        if (bot.kind != BotKind::Learned ||
            bot.learned_variant !=
                LearnedVariant::
                    ValueSearchChampion ||
            bot.rollouts_per_action !=
                scoring::kProductionWorlds ||
            !same_double(bot.exploration_rate, 0.0) ||
            !same_double(
                bot.value_continuation_epsilon, 0.0) ||
            !same_double(
                bot.value_priority_residual_weight, 0.0) ||
            bot.value_pass_dominance ||
            bot.value_resolved_shallow_prior_weight != 0.0 ||
            bot.value_adversarial_blocks ||
            bot.value_continuation_controller !=
                LearnedContinuationController::Legacy ||
            bot.training_games !=
                kParentTrainingGames ||
            bot.learned_model != parent) {
            return false;
        }
    }
    return true;
}

std::vector<PriorityAction> priority_actions(
    const probes::DecisionProbe& probe) {
    std::vector<PriorityAction> result;
    result.reserve(probe.candidates.size());
    for (const probes::Candidate& candidate :
         probe.candidates) {
        const auto* action =
            std::get_if<PriorityAction>(
                &candidate.action);
        if (action == nullptr) {
            fail("non-Priority action reached Priority scoring");
        }
        result.push_back(*action);
    }
    return result;
}

bool sorcery_actions_for(TurnPhase phase) {
    return phase == TurnPhase::FirstMain ||
           phase == TurnPhase::SecondMain;
}

bool production_recipe_exact(
    const scoring::DecisionScore& score) {
    const scoring::AppliedRecipe& recipe =
        score.recipe;
    return
        score.decision_kind ==
            probes::DecisionKind::Priority &&
        score.score_mode ==
            scoring::ScoreMode::
                ProductionPrioritySearch &&
        recipe.seed_source ==
            scoring::SeedSource::Derived &&
        recipe.seed_tag == scoring::kProductionTag &&
        recipe.seed_base ==
            scoring::kProductionSeedBase &&
        recipe.resolved_seed.has_value() &&
        recipe.worlds ==
            scoring::kProductionWorlds &&
        recipe.horizon_turns ==
            scoring::kProductionHorizonTurns &&
        recipe.rollouts_per_world ==
            scoring::kProductionRolloutsPerWorld &&
        recipe.blend_shallow_prior ==
            scoring::kProductionBlendShallowPrior &&
        recipe.evaluation_threads ==
            scoring::kProductionEvaluationThreads &&
        recipe.value_mirror &&
        same_double(
            recipe.value_continuation_epsilon, 0.0) &&
        same_double(
            recipe.value_priority_residual_weight, 0.0) &&
        !recipe.value_pass_dominance &&
        recipe.value_continuation_controller ==
            LearnedContinuationController::Legacy &&
        score.accounting.sampled_worlds ==
            scoring::kProductionWorlds &&
        score.accounting.rollout_evaluations ==
            score.actions.size() *
                scoring::kProductionWorlds *
                scoring::kProductionRolloutsPerWorld &&
        score.accounting.terminal_evaluations +
                score.accounting.bootstrapped_evaluations ==
            score.accounting.rollout_evaluations;
}

std::uint16_t narrow_u16(
    std::size_t value, std::string_view context) {
    if (value >
        std::numeric_limits<std::uint16_t>::max()) {
        fail(std::string(context) + " exceeds u16");
    }
    return static_cast<std::uint16_t>(value);
}

std::uint32_t narrow_u32(
    std::size_t value, std::string_view context) {
    if (value >
        std::numeric_limits<std::uint32_t>::max()) {
        fail(std::string(context) + " exceeds u32");
    }
    return static_cast<std::uint32_t>(value);
}

std::uint8_t narrow_u8(
    std::size_t value, std::string_view context) {
    if (value >
        std::numeric_limits<std::uint8_t>::max()) {
        fail(std::string(context) + " exceeds u8");
    }
    return static_cast<std::uint8_t>(value);
}

struct RetainedRoot {
    collection::CanonicalRoot root;
    collection::HiddenClone hidden;
    collection::RobustDominance dominance;
    std::vector<std::vector<double>> features;
    bundle::CensusRow census;
    std::uint8_t selected_roles = 0;
};

struct CapturedNeutralRoot {
    neutral::RankedLocator locator;
    RetainedRoot retained;
};

struct SplitBuild {
    std::vector<bundle::CensusRow> census;
    std::vector<bundle::SelectedRow> selected;
    std::vector<ParentWitness> witnesses;
    bundle::SplitManifest manifest;
    SplitSupport support;
    std::vector<neutral::EligibleRoot> neutral_eligible;
    std::vector<CapturedNeutralRoot> neutral_selected;
};

struct CompleteBuild {
    bundle::Bundle bundle;
    SplitSupport fit;
    SplitSupport check;
};

void append_census_digest(
    CanonicalBytes& output,
    const bundle::CensusRow& row) {
    output.u64(row.schedule_block);
    output.u64(row.schedule_index);
    output.u64(row.owner_seat);
    output.u64(row.trace_ordinal);
    output.u64(row.owner_deck);
    output.u64(row.opponent_deck);
    output.hash(row.stable_root_id);
    output.hash(row.physical_game_sha256);
    output.hash(row.information_action_sha256);
    output.hash(row.descriptor_set_sha256);
    output.u64(row.pass_index);
    output.u64(row.dominance.size());
    for (const bundle::DominanceCount count :
         row.dominance) {
        output.u64(count.complete);
        output.u64(count.strict);
    }
}

void append_selected_digest(
    CanonicalBytes& output,
    const bundle::SelectedRow& row) {
    output.u64(
        static_cast<std::uint64_t>(row.split));
    append_census_digest(output, row.census);
    output.u64(row.roles);
    output.u64(row.production_seed);
    output.u64(row.accounting.score_calls);
    output.u64(row.accounting.scored_actions);
    output.u64(row.accounting.sampled_worlds);
    output.u64(row.accounting.rollout_evaluations);
    output.u64(row.accounting.terminal_evaluations);
    output.u64(row.accounting.bootstrap_evaluations);
    output.u64(row.actions.size());
    for (const bundle::ActionRow& action :
         row.actions) {
        output.text(action.descriptor);
        output.boolean(action.is_pass);
        output.u64(action.dominance.complete);
        output.u64(action.dominance.strict);
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
        output.u64(action.features.size());
        for (const bundle::SparseFeature feature :
             action.features) {
            output.u64(feature.index);
            output.u64(feature.value_bits);
        }
    }
}

struct ProductionRootScore {
    scoring::DecisionScore decision;
    math::CenteredResidualScores residual;
};

ProductionRootScore prepare_production_root_score(
    const RetainedRoot& retained,
    const probes::DecisionProbe& probe,
    const std::vector<std::vector<double>>& features,
    scoring::DecisionScore base,
    const std::shared_ptr<const LearnedModel>& parent,
    std::string_view identity) {
    if (!production_recipe_exact(base) ||
        !base.recipe.resolved_seed.has_value() ||
        base.stable_id !=
            retained.root.manifest.stable_id ||
        base.actions.size() !=
            retained.root.manifest
                .canonical_descriptors.size() ||
        base.actions.size() !=
            features.size()) {
        fail(
            retained.root.manifest.stable_id +
            ": " + std::string(identity) +
            " production scorer drifted");
    }

    std::vector<double> base_scores;
    base_scores.reserve(base.actions.size());
    for (std::size_t action = 0;
         action < base.actions.size(); ++action) {
        if (base.actions[action].descriptor !=
                retained.root.manifest
                    .canonical_descriptors[action] ||
            base.actions[action].raw_samples.size() !=
                bundle::kWorldCount ||
            base.actions[action]
                    .shallow_prior_samples.size() !=
                bundle::kWorldCount ||
            base.actions[action]
                    .continuation_samples.size() !=
                bundle::kWorldCount) {
            fail(
                retained.root.manifest.stable_id +
                ": " + std::string(identity) +
                " descriptor or K8 production trace drifted");
        }
        base_scores.push_back(
            base.actions[action].raw_score);
    }

    const std::vector<double> logits =
        learned_policy_head_logits(
            features,
            LearnedPolicyDecisionKind::Priority,
            parent);
    const math::CenteredResidualScores shared =
        math::centered_tanh_scores(
            base_scores, logits,
            kParentPriorityResidualWeight);
    const auto deployed =
        diagnose_learned_value_priority_residual(
            probe.state,
            probe.root_player,
            sorcery_actions_for(
                probe.phase),
            probe.phase,
            probe.consecutive_passes,
            priority_actions(probe),
            parent,
            kParentPriorityResidualWeight);
    if (!bit_identical(
            shared.centered_logits,
            deployed.centered_policy_logits) ||
        !bit_identical(
            shared.residuals,
            deployed.residuals)) {
        fail(
            retained.root.manifest.stable_id +
            ": " + std::string(identity) +
            " shared tensor evaluator did not reproduce "
            "the deployed parent residual");
    }
    return {
        .decision = std::move(base),
        .residual = shared,
    };
}

ProductionRootScore score_canonical_root(
    const RetainedRoot& retained,
    const std::shared_ptr<const LearnedModel>& parent) {
    return prepare_production_root_score(
        retained, retained.root.probe,
        retained.features,
        scoring::score_production(
            retained.root.probe, parent),
        parent, "canonical");
}

ProductionRootScore score_hidden_root(
    const RetainedRoot& retained,
    const std::vector<std::vector<double>>& hidden_features,
    const std::shared_ptr<const LearnedModel>& parent) {
    return prepare_production_root_score(
        retained, retained.hidden.probe,
        hidden_features,
        scoring::score_production_hidden_clone(
            retained.root.probe,
            retained.hidden.probe, parent),
        parent, "hidden-clone");
}

bundle::ScoreAccounting score_accounting(
    const scoring::DecisionScore& score) {
    return {
        .score_calls = 1,
        .scored_actions =
            static_cast<std::uint64_t>(
                score.actions.size()),
        .sampled_worlds =
            static_cast<std::uint64_t>(
                score.accounting.sampled_worlds),
        .rollout_evaluations =
            static_cast<std::uint64_t>(
                score.accounting.rollout_evaluations),
        .terminal_evaluations =
            static_cast<std::uint64_t>(
                score.accounting.terminal_evaluations),
        .bootstrap_evaluations =
            static_cast<std::uint64_t>(
                score.accounting.bootstrapped_evaluations),
    };
}

bundle::SelectedRow score_selected_root(
    bundle::Split split, std::uint8_t roles,
    const RetainedRoot& retained,
    const ProductionRootScore& scored,
    std::vector<ParentWitness>& witnesses) {
    const scoring::DecisionScore& base =
        scored.decision;
    const math::CenteredResidualScores& shared =
        scored.residual;

    bundle::SelectedRow row{
        .split = split,
        .census = retained.census,
        .roles = roles,
        .production_seed =
            *base.recipe.resolved_seed,
        .accounting = score_accounting(base),
    };
    row.actions.reserve(base.actions.size());
    for (std::size_t action = 0;
         action < base.actions.size(); ++action) {
        bundle::ActionRow stored{
            .descriptor =
                base.actions[action].descriptor,
            .is_pass =
                action ==
                retained.root.manifest.pass_index,
            .dominance =
                retained.census.dominance[action],
            .base_score_bits =
                std::bit_cast<std::uint64_t>(
                    base.actions[action].raw_score),
            .parent_residual_bits =
                std::bit_cast<std::uint64_t>(
                    shared.residuals[action]),
            .features =
                sparsify_priority_features(
                    retained.features[action]),
        };
        for (std::size_t world = 0;
             world < bundle::kWorldCount; ++world) {
            stored.raw_sample_bits[world] =
                std::bit_cast<std::uint64_t>(
                    base.actions[action]
                        .raw_samples[world]);
            stored.shallow_prior_sample_bits[world] =
                std::bit_cast<std::uint64_t>(
                    base.actions[action]
                        .shallow_prior_samples[world]);
            stored.continuation_sample_bits[world] =
                std::bit_cast<std::uint64_t>(
                    base.actions[action]
                        .continuation_samples[world]);
        }
        row.actions.push_back(std::move(stored));
    }

    if ((roles &
         static_cast<std::uint8_t>(
             bundle::Role::DominancePositive)) != 0) {
        std::vector<double> base_scores;
        std::vector<std::vector<double>> raw_samples;
        base_scores.reserve(base.actions.size());
        raw_samples.reserve(base.actions.size());
        for (const scoring::DescriptorScore& action :
             base.actions) {
            base_scores.push_back(action.raw_score);
            raw_samples.push_back(action.raw_samples);
        }
        const collection::ParentClassResult classified =
            collection::classify_parent({
                .canonical_descriptors =
                    retained.root.manifest
                        .canonical_descriptors,
                .base_scores = base_scores,
                .combined_scores =
                    shared.combined_scores,
                .base_samples =
                    std::move(raw_samples),
                .robustly_pass_dominated =
                    retained.dominance
                        .robustly_pass_dominated,
            });
        if (!classified.valid) {
            fail(
                retained.root.manifest.stable_id +
                ": parent classification is invalid");
        }
        witnesses.push_back({
            .owner_deck =
                retained.root.manifest.owner_deck,
            .stable_root_id =
                retained.census.stable_root_id,
            .physical_game_sha256 =
                retained.census
                    .physical_game_sha256,
            .classification =
                classified.classification,
        });
    }
    return row;
}

bundle::SelectedRow score_selected_root(
    bundle::Split split, std::uint8_t roles,
    const RetainedRoot& retained,
    const std::shared_ptr<const LearnedModel>& parent,
    std::vector<ParentWitness>& witnesses) {
    return score_selected_root(
        split, roles, retained,
        score_canonical_root(retained, parent),
        witnesses);
}

bundle::Split split_for(
    schedule_data::Split split) {
    switch (split) {
    case schedule_data::Split::Fit:
        return bundle::Split::Fit;
    case schedule_data::Split::Check:
        return bundle::Split::Check;
    }
    fail("invalid development split");
}

CoverageRootObservation make_coverage_observation(
    bundle::Split split,
    const std::vector<schedule_data::SourceGame>& schedule,
    const RetainedRoot& row) {
    if (row.census.schedule_index >= schedule.size()) {
        fail("coverage row schedule index is out of range");
    }
    const schedule_data::SourceGame& source =
        schedule[row.census.schedule_index];
    if (row.census.owner_seat >= kPlayerCount ||
        source.schedule_block !=
            row.census.schedule_block ||
        source.seat_decks[row.census.owner_seat] !=
            row.root.manifest.owner_deck) {
        fail("coverage row does not match its frozen source");
    }
    CoverageRootObservation observation{
        .split = split,
        .schedule_block = row.census.schedule_block,
        .owner_deck =
            narrow_u8(
                deck_index(row.root.manifest.owner_deck),
                "coverage owner deck"),
        .opponent_deck =
            narrow_u8(
                deck_index(row.root.manifest.opponent_deck),
                "coverage opponent deck"),
        .owner_seat = row.census.owner_seat,
        .owner_on_play =
            source.starting_player ==
                row.census.owner_seat,
        .stable_root_id = row.census.stable_root_id,
        .physical_game_sha256 =
            row.census.physical_game_sha256,
        .option_count = row.features.size(),
        .dominance_positive =
            row.dominance.any_dominated(),
        .selected_roles = row.selected_roles,
        .public_stack_size =
            row.root.probe.state.stack.size(),
        .phase = row.root.probe.phase,
    };
    observation.stack_feature_bits.reserve(
        row.features.size());
    for (const auto& action_features : row.features) {
        if (action_features.size() !=
            bundle::kFeatureCount) {
            fail(
                "coverage Priority feature dimension drifted");
        }
        observation.stack_feature_bits.push_back(
            std::bit_cast<std::uint64_t>(
                action_features[
                    kCoverageStackSizeFeatureIndex]));
    }
    return observation;
}

neutral::EligibleRoot make_neutral_eligible_root(
    bundle::Split split,
    const std::vector<schedule_data::SourceGame>& schedule,
    const RetainedRoot& row) {
    const CoverageRootObservation observation =
        make_coverage_observation(split, schedule, row);
    if (observation.selected_roles != 0 ||
        observation.dominance_positive ||
        observation.public_stack_size != 0) {
        fail("non-neutral root reached neutral selection");
    }
    return {
        .rank = {
            .split = split,
            .owner_deck = observation.owner_deck,
            .schedule_block =
                observation.schedule_block,
            .physical_game_sha256 =
                observation.physical_game_sha256,
            .stable_root_id =
                observation.stable_root_id,
        },
        .schedule_index = row.census.schedule_index,
        .owner_seat = observation.owner_seat,
        .owner_on_play = observation.owner_on_play,
        .opponent_deck = observation.opponent_deck,
        .trace_ordinal = row.census.trace_ordinal,
        .legal_action_count =
            narrow_u8(
                row.features.size(),
                "neutral legal-action count"),
        .retained_nontrivial = true,
        .public_stack_size = 0,
        .dominance_positive = false,
        .existing_selected_roles = 0,
    };
}

SplitBuild construct_split(
    schedule_data::Split split,
    const std::vector<schedule_data::SourceGame>& schedule,
    const std::shared_ptr<const LearnedModel>& parent,
    std::uint64_t& completed_source_games,
    std::vector<CoverageRootObservation>*
        coverage_observations = nullptr,
    bool capture_neutral = false) {
    const bundle::Split bundle_split =
        split_for(split);
    CanonicalBytes trajectory;
    trajectory.text(kGeneratorSchema);
    trajectory.text("trajectory");
    trajectory.u64(
        static_cast<std::uint64_t>(bundle_split));

    std::vector<RetainedRoot> retained;
    retained.reserve(
        schedule_data::kOwnerPerspectivesPerSplit *
        collection::kMaximumRootsPerOwnerGame);
    std::vector<collection::ReplayRootManifest>
        replay_manifest;
    std::map<std::string, std::string>
        global_information_bytes;

    for (const schedule_data::SourceGame& source :
         schedule) {
        const collection::SourceGame common_source =
            collection_source(source);
        const GameConfig config =
            source_game_config(
                parent, source.starting_player);
        if (!source_config_exact(
                config, parent,
                source.starting_player)) {
            fail("source C16 K8/H4/R1 recipe drifted");
        }
        std::vector<LearnedDecisionTracePoint> trace;
        try {
            const auto decks = original_decks(source);
            Game game(
                decks[0], decks[1],
                source.game_seed, config);
            static_cast<void>(
                game.run_with_priority_root_trace(
                    trace));
        } catch (const std::exception& error) {
            fail(
                "source game " +
                std::to_string(source.schedule_index) +
                " threw: " + error.what());
        }

        trajectory.u64(source.schedule_block);
        trajectory.u64(source.schedule_index);
        trajectory.u64(trace.size());
        for (std::size_t owner = 0;
             owner < kPlayerCount; ++owner) {
            std::vector<collection::CanonicalRoot>
                owner_candidates;
            std::vector<collection::RetentionCandidate>
                retention_candidates;
            std::size_t raw = 0;
            std::size_t malformed = 0;
            std::size_t trivial = 0;
            std::size_t over_cap = 0;
            std::size_t eligible = 0;
            for (std::size_t ordinal = 0;
                 ordinal < trace.size(); ++ordinal) {
                const LearnedDecisionTracePoint& point =
                    trace[ordinal];
                if (point.context.decision_player != owner) {
                    continue;
                }
                ++raw;
                const collection::RootBuildResult attempt =
                    collection::build_canonical_root(
                        point, common_source, owner,
                        ordinal, collection_spec());
                trajectory.u64(owner);
                trajectory.u64(ordinal);
                trajectory.u64(
                    static_cast<std::uint64_t>(
                        attempt.disposition));
                trajectory.text(
                    attempt
                        .information_action_fingerprint);
                switch (attempt.disposition) {
                case collection::RootDisposition::Malformed:
                    ++malformed;
                    fail(
                        "malformed source root at schedule=" +
                        std::to_string(
                            source.schedule_index) +
                        " owner=" +
                        std::to_string(owner) +
                        " trace=" +
                        std::to_string(ordinal) +
                        ": " + attempt.error);
                case collection::RootDisposition::Trivial:
                    ++trivial;
                    continue;
                case collection::RootDisposition::OverCap:
                    ++over_cap;
                    continue;
                case collection::RootDisposition::
                    RetentionCandidate:
                    ++eligible;
                    break;
                }
                if (!attempt.root.has_value()) {
                    fail(
                        "eligible root was not materialized");
                }
                const auto [known, inserted] =
                    global_information_bytes.emplace(
                        attempt
                            .information_action_fingerprint,
                        attempt.root
                            ->information_action_bytes);
                if (!inserted &&
                    known->second !=
                        attempt.root
                            ->information_action_bytes) {
                    fail(
                        "information/action SHA-256 collision");
                }
                retention_candidates.push_back({
                    .trace_ordinal = ordinal,
                    .information_action_fingerprint =
                        attempt
                            .information_action_fingerprint,
                    .information_action_bytes =
                        attempt.root
                            ->information_action_bytes,
                    .stable_id =
                        attempt.root
                            ->manifest.stable_id,
                });
                owner_candidates.push_back(
                    *attempt.root);
            }

            const collection::RetentionResult selection =
                collection::retain_owner_game_roots(
                    retention_candidates);
            if (!selection.valid ||
                selection.hash_collision_count != 0 ||
                selection.unique_input_indices.size() +
                        selection.duplicate_count !=
                    eligible ||
                raw !=
                    malformed + trivial + over_cap +
                        eligible) {
                fail(
                    "owner-game retention cross-sums failed");
            }
            for (const std::size_t index :
                 selection.retained_input_indices) {
                collection::CanonicalRoot root =
                    std::move(owner_candidates[index]);
                collection::HiddenClone hidden =
                    collection::make_hidden_clone(root);
                if (hidden.eligible != hidden.distinct ||
                    !collection::replay_exact(
                        root, hidden,
                        collection_spec()) ||
                    !collection::
                        priority_feature_bits_identical(
                            root.probe,
                            hidden.probe)) {
                    fail(
                        root.manifest.stable_id +
                        ": hidden-repartition control failed");
                }
                std::vector<std::vector<double>> features =
                    collection::priority_option_features(
                        root.probe);
                if (features.size() !=
                        root.probe.candidates.size() ||
                    std::any_of(
                        features.begin(), features.end(),
                        [](const auto& option) {
                            return option.size() !=
                                       bundle::kFeatureCount ||
                                   !std::all_of(
                                       option.begin(),
                                       option.end(),
                                       [](double value) {
                                           return std::isfinite(
                                               value);
                                       });
                        })) {
                    fail(
                        root.manifest.stable_id +
                        ": neutral Priority tensor is invalid");
                }
                replay_manifest.push_back(
                    root.manifest);
                retained.push_back({
                    .root = std::move(root),
                    .hidden = std::move(hidden),
                    .features = std::move(features),
                });
            }
        }
        std::atomic_ref<std::uint64_t>(
            completed_source_games)
            .fetch_add(
                1, std::memory_order_relaxed);
    }

    if (!collection::validate_replay_manifest(
            replay_manifest, kStableRootSchema,
            collection::kMaximumLegalActions,
            true)) {
        fail("complete retained replay manifest is invalid");
    }
    const std::string retained_hash =
        collection::replay_manifest_sha256(
            replay_manifest, kReplayManifestSchema,
            kStableRootSchema,
            collection::kMaximumLegalActions,
            true);

    // Phase boundary: every retained root exists before the first
    // rules-owned dominance transition begins.
    std::vector<std::string> dominance_failures;
    for (RetainedRoot& row : retained) {
        row.dominance =
            collection::evaluate_robust_dominance(
                row.root, collection_spec(),
                dominance_failures);
        if (!row.dominance.shape_valid) {
            dominance_failures.push_back(
                row.root.manifest.stable_id +
                ": robust dominance shape is invalid");
        }
    }
    if (!dominance_failures.empty()) {
        fail(dominance_failures.front());
    }

    // Second phase boundary: only after the complete dominance census is
    // frozen are blind role rows selected. No parent score or source choice
    // is present in either input.
    SplitBuild result;
    result.census.reserve(retained.size());
    std::vector<collection::BlindSelectionInput>
        blind_inputs;
    blind_inputs.reserve(retained.size());
    CanonicalBytes dominance_digest;
    dominance_digest.text(kGeneratorSchema);
    dominance_digest.text("dominance");
    dominance_digest.u64(
        static_cast<std::uint64_t>(bundle_split));
    for (RetainedRoot& row : retained) {
        row.census =
            make_census_row(
                row.root, row.dominance);
        result.census.push_back(row.census);
        blind_inputs.push_back({
            .stable_id =
                row.root.manifest.stable_id,
            .physical_game_sha256 =
                bundle::format_sha256(
                    row.census
                        .physical_game_sha256),
            .owner_deck =
                row.root.manifest.owner_deck,
            .dominance_positive =
                row.dominance.any_dominated(),
        });
        append_census_digest(
            dominance_digest, row.census);
    }

    const collection::BlindSelection selection =
        collection::select_development_rows(
            blind_inputs);
    if (!selection.valid) {
        fail("blind development-row selection failed");
    }
    CanonicalBytes selection_digest;
    selection_digest.text(kGeneratorSchema);
    selection_digest.text("selection");
    selection_digest.u64(
        static_cast<std::uint64_t>(bundle_split));
    selection_digest.u64(selection.rows.size());
    std::vector<std::pair<std::size_t, std::uint8_t>>
        blind_selected;
    blind_selected.reserve(selection.rows.size());
    for (const collection::BlindSelectionRow& selected :
         selection.rows) {
        if (selected.input_index >= retained.size()) {
            fail("blind selection index is out of range");
        }
        std::uint8_t roles = 0;
        if ((selected.roles &
             collection::DevelopmentRolePositive) != 0) {
            roles |= static_cast<std::uint8_t>(
                bundle::Role::DominancePositive);
        }
        if ((selected.roles &
             collection::DevelopmentRoleBackground) != 0) {
            roles |= static_cast<std::uint8_t>(
                bundle::Role::BackgroundControl);
        }
        selection_digest.hash(
            retained[selected.input_index]
                .census.stable_root_id);
        selection_digest.u64(roles);
        if (roles == 0 ||
            retained[selected.input_index]
                    .selected_roles != 0) {
            fail(
                "blind selection produced an invalid or duplicate role");
        }
        retained[selected.input_index]
            .selected_roles = roles;
        blind_selected.emplace_back(
            selected.input_index, roles);
    }

    // Freeze every role and export every aggregate-only coverage witness
    // before any selected-row parent score is evaluated. DEV5 additionally
    // freezes its split-local neutral locators at this boundary; neither
    // selection path can receive a score, outcome, phase, or source choice.
    if (coverage_observations != nullptr) {
        coverage_observations->reserve(
            coverage_observations->size() +
            retained.size());
        for (const RetainedRoot& row : retained) {
            coverage_observations->push_back(
                make_coverage_observation(
                    bundle_split, schedule, row));
        }
    }
    if (capture_neutral) {
        result.neutral_eligible.reserve(
            retained.size());
        std::map<bundle::Hash256, std::size_t>
            retained_by_stable;
        for (std::size_t index = 0;
             index < retained.size(); ++index) {
            const RetainedRoot& row = retained[index];
            if (!retained_by_stable
                     .emplace(
                         row.census.stable_root_id,
                         index)
                     .second) {
                fail(
                    "neutral candidate stable root is duplicated");
            }
            if (row.selected_roles == 0 &&
                !row.dominance.any_dominated() &&
                row.root.probe.state.stack.empty()) {
                result.neutral_eligible.push_back(
                    make_neutral_eligible_root(
                        bundle_split, schedule, row));
            }
        }
        const std::vector<neutral::RankedLocator>
            provisional =
                neutral::freeze_split_selection(
                    bundle_split,
                    result.neutral_eligible,
                    neutral::accepted_dev4_capacity());
        result.neutral_selected.reserve(
            provisional.size());
        for (const neutral::RankedLocator& locator :
             provisional) {
            const auto found =
                retained_by_stable.find(
                    locator.root.rank.stable_root_id);
            if (found == retained_by_stable.end()) {
                fail(
                    "provisional neutral locator lacks a live root");
            }
            RetainedRoot& row = retained[found->second];
            if (row.selected_roles != 0 ||
                row.dominance.any_dominated() ||
                !row.root.probe.state.stack.empty()) {
                fail(
                    "provisional neutral root changed eligibility");
            }
            result.neutral_selected.push_back({
                .locator = locator,
                .retained = std::move(row),
            });
        }
    }

    result.selected.reserve(blind_selected.size());
    for (const auto [input_index, roles] :
         blind_selected) {
        result.selected.push_back(
            score_selected_root(
                bundle_split, roles,
                retained[input_index],
                parent, result.witnesses));
    }

    result.support =
        summarize_support(
            result.census, result.selected,
            result.witnesses);

    CanonicalBytes scored_digest;
    scored_digest.text(kGeneratorSchema);
    scored_digest.text("scored");
    scored_digest.u64(
        static_cast<std::uint64_t>(bundle_split));
    scored_digest.u64(result.selected.size());
    for (const bundle::SelectedRow& row :
         result.selected) {
        append_selected_digest(
            scored_digest, row);
    }

    result.manifest = {
        .source_seed_base =
            schedule_data::seed_base(split),
        .schedule_sha256 =
            bundle::parse_sha256(
                schedule_data::
                    expected_schedule_sha256(split)),
        .trajectory_sha256 =
            domain_hash("trajectory", trajectory),
        .retained_sha256 =
            bundle::parse_sha256(retained_hash),
        .dominance_sha256 =
            domain_hash(
                "dominance", dominance_digest),
        .selection_sha256 =
            domain_hash(
                "selection", selection_digest),
        .scored_sha256 =
            domain_hash("scored", scored_digest),
        .census_rows =
            narrow_u32(
                result.census.size(),
                "split census count"),
        .selected_rows =
            narrow_u32(
                result.selected.size(),
                "split selected count"),
    };
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        result.manifest.census_by_deck[deck] =
            narrow_u16(
                result.support
                    .census_by_deck[deck],
                "per-deck census count");
        result.manifest.selected_by_deck[deck] =
            narrow_u16(
                result.support
                    .selected_by_deck[deck],
                "per-deck selected count");
        result.manifest.positive_by_deck[deck] =
            narrow_u16(
                result.support
                    .positive_by_deck[deck],
                "per-deck positive count");
        result.manifest.background_by_deck[deck] =
            narrow_u16(
                result.support
                    .background_by_deck[deck],
                "per-deck background count");
    }
    return result;
}

void require_parent_identity(
    const std::shared_ptr<const LearnedModel>& parent) {
    if (!parent ||
        learned_model_fingerprint(parent) !=
            bundle::kParentModelFingerprint) {
        fail("loaded model is not exact frozen C16");
    }
    const LearnedModelComponentFingerprints components =
        learned_model_component_fingerprints(parent);
    if (components.critic !=
            bundle::kParentCriticFingerprint ||
        components.priority !=
            bundle::kParentPriorityFingerprint ||
        components.attack !=
            bundle::kParentAttackFingerprint ||
        components.block !=
            bundle::kParentBlockFingerprint ||
        components.damage_order !=
            bundle::kParentDamageOrderFingerprint) {
        fail("frozen C16 component identity drifted");
    }
}

bundle::Manifest base_manifest(
    const integrity::RegularFileSnapshot& executable,
    std::string_view producer_commit,
    const SplitBuild& fit,
    const SplitBuild& check) {
    const std::string commit_domain =
        "git-commit\n" +
        std::string(producer_commit) + "\n";
    return {
        .purpose = std::string(bundle::kPurpose),
        .producer_commit_sha256 =
            bundle::sha256(commit_domain),
        .producer_executable_sha256 =
            bundle::parse_sha256(
                executable.sha256),
        .parent_artifact_sha256 =
            bundle::parse_sha256(
                bundle::kParentArtifactSha256),
        .parent_model_fingerprint =
            bundle::parse_sha256(
                bundle::kParentModelFingerprint),
        .parent_components = {
            .critic =
                bundle::parse_sha256(
                    bundle::kParentCriticFingerprint),
            .priority =
                bundle::parse_sha256(
                    bundle::kParentPriorityFingerprint),
            .attack =
                bundle::parse_sha256(
                    bundle::kParentAttackFingerprint),
            .block =
                bundle::parse_sha256(
                    bundle::kParentBlockFingerprint),
            .damage_order =
                bundle::parse_sha256(
                    bundle::
                        kParentDamageOrderFingerprint),
        },
        .generation_namespace =
            bundle::kGenerationNamespace,
        .hidden_namespace =
            bundle::kHiddenNamespace,
        .dominance_namespace =
            bundle::kDominanceNamespace,
        .collection_spec_sha256 =
            collection_spec_contract_sha256(),
        .production_recipe =
            std::string(bundle::kProductionRecipe),
        .feature_schema =
            std::string(bundle::kFeatureSchema),
        .feature_count =
            static_cast<std::uint16_t>(
                bundle::kFeatureCount),
        .feature_contract_sha256 =
            feature_contract_sha256(),
        .fit = fit.manifest,
        .check = check.manifest,
    };
}

CompleteBuild construct_complete(
    const std::vector<schedule_data::SourceGame>& fit_schedule,
    const std::vector<schedule_data::SourceGame>& check_schedule,
    const std::shared_ptr<const LearnedModel>& parent,
    const integrity::RegularFileSnapshot& executable,
    std::string_view producer_commit,
    std::size_t construction_index,
    GenerationProgress& progress) {
    if (construction_index >= kCompleteConstructions) {
        fail("complete-construction index is out of range");
    }
    require_parent_identity(parent);
    SplitBuild fit =
        construct_split(
            schedule_data::Split::Fit,
            fit_schedule, parent,
            progress
                .source_games_completed[
                    construction_index][0]);
    require_parent_identity(parent);
    SplitBuild check =
        construct_split(
            schedule_data::Split::Check,
            check_schedule, parent,
            progress
                .source_games_completed[
                    construction_index][1]);
    require_parent_identity(parent);

    CompleteBuild result;
    result.bundle.manifest =
        base_manifest(
            executable, producer_commit,
            fit, check);
    result.bundle.fit_census =
        std::move(fit.census);
    result.bundle.fit_rows =
        std::move(fit.selected);
    result.bundle.check_census =
        std::move(check.census);
    result.bundle.check_rows =
        std::move(check.selected);
    result.fit = fit.support;
    result.check = check.support;
    bundle::validate_prepublication_construction(
        result.bundle);
    // The strict artifact validator requires publishable support. A valid
    // count-only support miss still returns both split constructions so the
    // caller can compare and report them before rejecting publication.
    if (result.fit.publishable() &&
        result.check.publishable()) {
        static_cast<void>(
            bundle::encode(result.bundle));
    }
    return result;
}

bool published_scientific_manifest_exact(
    const bundle::Manifest& manifest) {
    return
        manifest.purpose == bundle::kPurpose &&
        manifest.parent_artifact_sha256 ==
            bundle::parse_sha256(
                bundle::kParentArtifactSha256) &&
        manifest.parent_model_fingerprint ==
            bundle::parse_sha256(
                bundle::kParentModelFingerprint) &&
        manifest.parent_components.critic ==
            bundle::parse_sha256(
                bundle::kParentCriticFingerprint) &&
        manifest.parent_components.priority ==
            bundle::parse_sha256(
                bundle::kParentPriorityFingerprint) &&
        manifest.parent_components.attack ==
            bundle::parse_sha256(
                bundle::kParentAttackFingerprint) &&
        manifest.parent_components.block ==
            bundle::parse_sha256(
                bundle::kParentBlockFingerprint) &&
        manifest.parent_components.damage_order ==
            bundle::parse_sha256(
                bundle::kParentDamageOrderFingerprint) &&
        manifest.generation_namespace ==
            bundle::kGenerationNamespace &&
        manifest.hidden_namespace ==
            bundle::kHiddenNamespace &&
        manifest.dominance_namespace ==
            bundle::kDominanceNamespace &&
        manifest.collection_spec_sha256 ==
            collection_spec_contract_sha256() &&
        manifest.production_recipe ==
            bundle::kProductionRecipe &&
        manifest.feature_schema ==
            bundle::kFeatureSchema &&
        manifest.feature_count ==
            bundle::kFeatureCount &&
        manifest.feature_contract_sha256 ==
            feature_contract_sha256();
}

void add_score_accounting(
    bundle::ScoreAccounting& total,
    const bundle::ScoreAccounting& addend) {
    const auto add =
        [](std::uint64_t& target,
           std::uint64_t value) {
            if (value >
                std::numeric_limits<std::uint64_t>::max() -
                    target) {
                fail("score accounting overflows");
            }
            target += value;
        };
    add(total.score_calls, addend.score_calls);
    add(total.scored_actions, addend.scored_actions);
    add(total.sampled_worlds, addend.sampled_worlds);
    add(
        total.rollout_evaluations,
        addend.rollout_evaluations);
    add(
        total.terminal_evaluations,
        addend.terminal_evaluations);
    add(
        total.bootstrap_evaluations,
        addend.bootstrap_evaluations);
}

bundle::ScoreAccounting summarize_scoring(
    const std::vector<bundle::SelectedRow>& rows) {
    bundle::ScoreAccounting result;
    for (const bundle::SelectedRow& row : rows) {
        const std::uint64_t actions =
            static_cast<std::uint64_t>(
                row.actions.size());
        if (row.accounting.score_calls != 1 ||
            row.accounting.scored_actions != actions ||
            row.accounting.sampled_worlds !=
                scoring::kProductionWorlds ||
            row.accounting.rollout_evaluations !=
                actions *
                    scoring::kProductionWorlds *
                    scoring::kProductionRolloutsPerWorld ||
            row.accounting.terminal_evaluations +
                    row.accounting.bootstrap_evaluations !=
                row.accounting.rollout_evaluations) {
            fail("production score accounting drifted");
        }
        add_score_accounting(result, row.accounting);
    }
    return result;
}

neutral::NeutralRow adapt_neutral_row(
    const neutral::RankedLocator& locator,
    const RetainedRoot& retained,
    const bundle::SelectedRow& selected) {
    const bundle::CensusRow& census = selected.census;
    const neutral::EligibleRoot& eligible =
        locator.root;
    if (selected.split != eligible.rank.split ||
        selected.roles != 0 ||
        census != retained.census ||
        census.schedule_block !=
            eligible.rank.schedule_block ||
        census.schedule_index !=
            eligible.schedule_index ||
        census.owner_seat != eligible.owner_seat ||
        census.trace_ordinal !=
            eligible.trace_ordinal ||
        census.owner_deck !=
            eligible.rank.owner_deck ||
        census.opponent_deck !=
            eligible.opponent_deck ||
        census.stable_root_id !=
            eligible.rank.stable_root_id ||
        census.physical_game_sha256 !=
            eligible.rank.physical_game_sha256 ||
        selected.actions.size() !=
            eligible.legal_action_count ||
        selected.actions.size() !=
            retained.root.manifest
                .canonical_descriptors.size() ||
        selected.actions.size() !=
            census.dominance.size() ||
        census.pass_index !=
            retained.root.manifest.pass_index ||
        census.descriptor_set_sha256 !=
            bundle::descriptor_set_sha256(
                retained.root.manifest
                    .canonical_descriptors) ||
        retained.selected_roles != 0 ||
        retained.dominance.any_dominated() ||
        !retained.root.probe.state.stack.empty()) {
        fail(
            retained.root.manifest.stable_id +
            ": neutral adapter boundary drifted");
    }

    neutral::NeutralRow result{
        .locator = locator,
        .information_action_sha256 =
            census.information_action_sha256,
        .descriptor_set_sha256 =
            census.descriptor_set_sha256,
        .pass_index = census.pass_index,
        .production_seed = selected.production_seed,
        .hidden_clone_eligible =
            retained.hidden.eligible,
        .hidden_clone_distinct =
            retained.hidden.distinct,
        .accounting = selected.accounting,
    };
    result.actions.reserve(selected.actions.size());
    for (std::size_t index = 0;
         index < selected.actions.size(); ++index) {
        const bundle::ActionRow& source =
            selected.actions[index];
        if (source.descriptor !=
                retained.root.manifest
                    .canonical_descriptors[index] ||
            source.dominance !=
                census.dominance[index] ||
            source.is_pass !=
                (index == census.pass_index)) {
            fail(
                retained.root.manifest.stable_id +
                ": neutral action adapter drifted");
        }
        result.actions.push_back({
            .is_pass = source.is_pass,
            .dominance = source.dominance,
            .raw_sample_bits =
                source.raw_sample_bits,
            .shallow_prior_sample_bits =
                source.shallow_prior_sample_bits,
            .continuation_sample_bits =
                source.continuation_sample_bits,
            .base_score_bits =
                source.base_score_bits,
            .parent_residual_bits =
                source.parent_residual_bits,
            .features = source.features,
        });
    }
    return result;
}

neutral::NeutralRow score_neutral_root(
    const CapturedNeutralRoot& captured,
    const std::shared_ptr<const LearnedModel>& parent,
    bundle::ScoreAccounting& canonical_accounting,
    bundle::ScoreAccounting& hidden_accounting,
    std::uint64_t& bit_identical_actions) {
    const RetainedRoot& retained = captured.retained;
    const std::vector<std::vector<double>>
        hidden_features =
            collection::priority_option_features(
                retained.hidden.probe);
    if (!collection::priority_feature_bits_identical(
            retained.root.probe,
            retained.hidden.probe) ||
        !bit_identical(
            retained.features, hidden_features)) {
        fail(
            retained.root.manifest.stable_id +
            ": hidden neutral features drifted");
    }

    const ProductionRootScore canonical =
        score_canonical_root(retained, parent);
    const ProductionRootScore hidden =
        score_hidden_root(
            retained, hidden_features, parent);
    if (!scoring::bit_identical(
            canonical.decision, hidden.decision) ||
        !bit_identical(
            canonical.residual.centered_logits,
            hidden.residual.centered_logits) ||
        !bit_identical(
            canonical.residual.residuals,
            hidden.residual.residuals) ||
        !bit_identical(
            canonical.residual.combined_scores,
            hidden.residual.combined_scores) ||
        canonical.residual.exact_max_indices !=
            hidden.residual.exact_max_indices) {
        fail(
            retained.root.manifest.stable_id +
            ": canonical and hidden neutral scores differ");
    }

    std::vector<ParentWitness> no_witnesses;
    const bundle::SelectedRow selected =
        score_selected_root(
            captured.locator.root.rank.split, 0,
            retained, canonical, no_witnesses);
    if (!no_witnesses.empty()) {
        fail("neutral row unexpectedly emitted a parent witness");
    }
    add_score_accounting(
        canonical_accounting, selected.accounting);
    add_score_accounting(
        hidden_accounting,
        score_accounting(hidden.decision));
    if (selected.actions.size() >
        std::numeric_limits<std::uint64_t>::max() -
            bit_identical_actions) {
        fail("bit-identical action count overflows");
    }
    bit_identical_actions +=
        static_cast<std::uint64_t>(
            selected.actions.size());
    return adapt_neutral_row(
        captured.locator, retained, selected);
}

struct Work0Target {
    cache::SourceFamily family =
        cache::SourceFamily::Dev1Selected;
    bundle::Split split = bundle::Split::Fit;
    std::size_t artifact_index = 0;
    std::uint32_t source_row = 0;
    std::uint8_t source_roles = 0;
    std::uint64_t production_seed = 0;
    collection::RootLocator locator;
    std::uint8_t owner_deck = 0;
    std::uint8_t opponent_deck = 0;
    bundle::Hash256 stable_root_id{};
    bundle::Hash256 physical_game_sha256{};
    bundle::Hash256 information_action_sha256{};
    bundle::Hash256 descriptor_set_sha256{};
    std::size_t action_count = 0;
    std::size_t pass_index = 0;
    bool has_neutral_locator = false;
    neutral::RankedLocator neutral_locator;
};

struct Work0RootBuild {
    cache::Root root;
    bool normalized_state_exact = false;
    bool hidden_clone_eligible = false;
    bool hidden_clone_distinct = false;
    bool hidden_feature_exact = false;
};

cache::NeutralLocator work0_neutral_locator(
    const neutral::RankedLocator& locator) {
    const neutral::EligibleRoot& root =
        locator.root;
    return {
        .split = root.rank.split,
        .owner_deck = root.rank.owner_deck,
        .schedule_block =
            root.rank.schedule_block,
        .physical_game_sha256 =
            root.rank.physical_game_sha256,
        .stable_root_id =
            root.rank.stable_root_id,
        .schedule_index = root.schedule_index,
        .owner_seat = root.owner_seat,
        .owner_on_play = root.owner_on_play,
        .opponent_deck = root.opponent_deck,
        .trace_ordinal = root.trace_ordinal,
        .legal_action_count =
            root.legal_action_count,
        .retained_nontrivial =
            root.retained_nontrivial,
        .public_stack_size =
            root.public_stack_size,
        .dominance_positive =
            root.dominance_positive,
        .existing_selected_roles =
            root.existing_selected_roles,
        .representative_rank =
            locator.representative_rank,
        .game_rank = locator.game_rank,
    };
}

std::array<std::uint8_t, kCardCount>
work0_deck_composition(const std::vector<CardId>& deck) {
    std::array<std::uint8_t, kCardCount> result{};
    for (const CardId card : deck) {
        std::uint8_t& count =
            result[static_cast<std::size_t>(card)];
        if (count ==
            std::numeric_limits<std::uint8_t>::max()) {
            fail("WORK0 deck composition overflows");
        }
        ++count;
    }
    return result;
}

cache::OwnerVisibleState work0_owner_state(
    const GameState& state, std::size_t observer) {
    const PlayerObservation observation =
        observe_game_state(state, observer);
    if (observation.revealed_opponent_hand.has_value()) {
        fail("WORK0 owner observation revealed opponent hand");
    }
    return {
        .observer = observation.observer,
        .players = observation.players,
        .owner_hand = observation.hand,
        .stack = observation.stack,
        .extra_turns_pending =
            observation.extra_turns_pending,
        .active_player = observation.active_player,
        .starting_player =
            observation.starting_player,
        .turn_number = observation.turn_number,
        .failed_draw = state.failed_draw,
        .next_permanent_id =
            state.next_permanent_id,
        .next_stack_object_id =
            state.next_stack_object_id,
    };
}

Work0RootBuild make_work0_root(
    const Work0Target& target,
    const collection::CanonicalRoot& canonical) {
    const collection::ReplayRootManifest& manifest =
        canonical.manifest;
    const probes::DecisionProbe& probe =
        canonical.probe;
    const PlayerGameStats zero_stats{};
    if (manifest.locator != target.locator ||
        manifest.owner_deck !=
            static_cast<DeckId>(target.owner_deck) ||
        manifest.opponent_deck !=
            static_cast<DeckId>(
                target.opponent_deck) ||
        bundle::parse_sha256(manifest.stable_id) !=
            target.stable_root_id ||
        bundle::parse_sha256(
            manifest.information_action_fingerprint) !=
            target.information_action_sha256 ||
        bundle::sha256(
            collection::block_bound_physical_game_id(
                manifest.locator)) !=
            target.physical_game_sha256 ||
        bundle::descriptor_set_sha256(
            manifest.canonical_descriptors) !=
            target.descriptor_set_sha256 ||
        probe.state.stats[0] != zero_stats ||
        probe.state.stats[1] != zero_stats ||
        probe.root_player !=
            target.locator.owner_seat ||
        probe.candidates.size() !=
            target.action_count ||
        manifest.pass_index != target.pass_index) {
        fail("WORK0 canonical root disagrees with source row");
    }

    const LearnedDecisionContext context{
        .valid = true,
        .phase = probe.phase,
        .decision_player = probe.root_player,
        .consecutive_passes =
            probe.consecutive_passes,
        .sorcery_actions =
            sorcery_actions_for(probe.phase),
    };
    const std::vector<PriorityAction> raw =
        legal_priority_actions(
            probe.state, probe.root_player,
            context.sorcery_actions);
    const information::InformationSetKey key =
        information::make_information_set_key(
            probe.state, context, raw);
    const auto canonical_rows =
        information::descriptor_canonical_action_rows(key);
    if (canonical_rows.size() !=
        probe.candidates.size()) {
        fail("WORK0 canonical action count drifted");
    }

    std::vector<PriorityAction> canonical_typed;
    canonical_typed.reserve(canonical_rows.size());
    for (std::size_t index = 0;
         index < canonical_rows.size(); ++index) {
        const auto* stored =
            std::get_if<PriorityAction>(
                &probe.candidates[index].action);
        if (stored == nullptr ||
            probe.candidates[index].descriptor !=
                canonical_rows[index].descriptor ||
            *stored != canonical_rows[index].action) {
            fail("WORK0 canonical/raw action join drifted");
        }
        canonical_typed.push_back(*stored);
    }
    std::vector<cache::CanonicalAction> actions =
        cache::bind_canonical_actions(
            raw, canonical_typed);

    cache::Root result{
        .source = target.family,
        .split = target.split,
        .source_row = target.source_row,
        .source_roles = target.source_roles,
        .production_seed =
            target.production_seed,
        .locator = target.locator,
        .owner_deck = target.owner_deck,
        .opponent_deck = target.opponent_deck,
        .stable_root_id = target.stable_root_id,
        .physical_game_sha256 =
            target.physical_game_sha256,
        .information_action_sha256 =
            target.information_action_sha256,
        .descriptor_set_sha256 =
            target.descriptor_set_sha256,
        .has_neutral_locator =
            target.has_neutral_locator,
        .neutral_locator =
            target.has_neutral_locator
                ? work0_neutral_locator(
                      target.neutral_locator)
                : cache::NeutralLocator{},
        .state =
            work0_owner_state(
                probe.state, probe.root_player),
        .context = context,
        .raw_actions = raw,
        .canonical_actions = std::move(actions),
        .pass_index =
            static_cast<std::uint8_t>(
                manifest.pass_index),
    };
    for (std::size_t player = 0; player < 2; ++player) {
        result.deck_compositions[player] =
            work0_deck_composition(
                probe.original_decks[player]);
    }
    result.raw_actions_sha256 =
        cache::raw_actions_sha256(
            result.raw_actions);
    result.canonical_actions_sha256 =
        cache::canonical_actions_sha256(
            result.canonical_actions);

    const collection::HiddenClone hidden =
        collection::make_hidden_clone(canonical);
    const bool normalized_state_exact =
        cache::sample_world(
            result,
            collection::owner_safe_normalization_seed(
                target.locator,
                collection_spec())) ==
        probe.state;
    const bool hidden_feature_exact =
        collection::priority_feature_bits_identical(
            canonical.probe, hidden.probe);
    if (!normalized_state_exact ||
        !hidden.eligible ||
        !hidden.distinct ||
        !hidden_feature_exact) {
        fail(
            "WORK0 canonical normalization or hidden-clone "
            "proof failed");
    }
    for (std::size_t world = 0;
         world < scoring::kProductionWorlds;
         ++world) {
        const std::uint64_t seed =
            learned_search_world_seed(
                target.production_seed, world);
        const GameState expected =
            sample_determinization(
                probe.state, probe.original_decks,
                probe.root_player, seed);
        const GameState hidden_expected =
            sample_determinization(
                hidden.probe.state,
                hidden.probe.original_decks,
                hidden.probe.root_player, seed);
        if (expected != hidden_expected ||
            cache::sample_world(result, seed) !=
                expected) {
            fail(
                "WORK0 owner-visible rehydration changed "
                "an information-set world");
        }
    }
    return {
        .root = std::move(result),
        .normalized_state_exact =
            normalized_state_exact,
        .hidden_clone_eligible =
            hidden.eligible,
        .hidden_clone_distinct =
            hidden.distinct,
        .hidden_feature_exact =
            hidden_feature_exact,
    };
}

} // namespace

const collection::CollectionSpec& collection_spec() {
    static constexpr collection::CollectionSpec spec{
        .owner_information_schema =
            kOwnerInformationSchema,
        .stable_root_schema = kStableRootSchema,
        .block_bound_ids = true,
        .hidden_seed_namespace =
            bundle::kHiddenNamespace,
        .hidden_seed_scope = kHiddenSeedScope,
        .dominance_seed_namespace =
            bundle::kDominanceNamespace,
        .dominance_seed_scope =
            kDominanceSeedScope,
        .maximum_legal_actions =
            collection::kMaximumLegalActions,
        .maximum_roots_per_owner_game =
            collection::kMaximumRootsPerOwnerGame,
        .dominance_worlds =
            collection::kDominanceWorlds,
    };
    return spec;
}

std::string collection_spec_contract_bytes() {
    const collection::CollectionSpec& spec =
        collection_spec();
    return
        "old-school-fq4-priority-dev-collection-spec-v2\n"
        "owner-information-schema=" +
        std::string(spec.owner_information_schema) + "\n" +
        "stable-root-schema=" +
        std::string(spec.stable_root_schema) + "\n" +
        "block-bound-ids=" +
        std::to_string(
            spec.block_bound_ids ? 1 : 0) + "\n" +
        "replay-manifest-schema=" +
        std::string(kReplayManifestSchema) + "\n" +
        "hidden-seed-namespace=" +
        std::to_string(spec.hidden_seed_namespace) + "\n" +
        "hidden-seed-scope=" +
        std::string(spec.hidden_seed_scope) + "\n" +
        "dominance-seed-namespace=" +
        std::to_string(spec.dominance_seed_namespace) + "\n" +
        "dominance-seed-scope=" +
        std::string(spec.dominance_seed_scope) + "\n" +
        "maximum-legal-actions=" +
        std::to_string(spec.maximum_legal_actions) + "\n" +
        "maximum-roots-per-owner-game=" +
        std::to_string(
            spec.maximum_roots_per_owner_game) + "\n" +
        "dominance-worlds=" +
        std::to_string(spec.dominance_worlds) + "\n";
}

bundle::Hash256 collection_spec_contract_sha256() {
    const bundle::Hash256 result =
        bundle::sha256(
            collection_spec_contract_bytes());
    if (result !=
        bundle::parse_sha256(
            bundle::kCollectionSpecSha256)) {
        throw std::logic_error(
            "FQ4 collection-spec bytes drifted");
    }
    return result;
}

SchedulePreflight preflight_schedules(
    const std::vector<schedule_data::SourceGame>& fit,
    const std::vector<schedule_data::SourceGame>& check) {
    SchedulePreflight result;
    const auto record_failure =
        [&](std::string message) {
            result.failures.push_back(
                std::move(message));
        };
    try {
        const std::string fit_bytes =
            schedule_data::serialize_source_schedule(
                fit);
        const std::string check_bytes =
            schedule_data::serialize_source_schedule(
                check);
        result.fit_sha256 =
            integrity::sha256_string(fit_bytes);
        result.check_sha256 =
            integrity::sha256_string(check_bytes);
        result.fit_balance =
            schedule_data::audit_schedule_balance(fit);
        result.check_balance =
            schedule_data::audit_schedule_balance(check);

        if (fit_bytes.size() !=
                schedule_data::
                    kExpectedFitScheduleBytes ||
            result.fit_sha256 !=
                schedule_data::
                    kExpectedFitScheduleSha256 ||
            !result.fit_balance.exact) {
            record_failure(
                "FIT schedule identity or balance drifted");
        }
        if (check_bytes.size() !=
                schedule_data::
                    kExpectedCheckScheduleBytes ||
            result.check_sha256 !=
                schedule_data::
                    kExpectedCheckScheduleSha256 ||
            !result.check_balance.exact) {
            record_failure(
                "CHECK schedule identity or balance drifted");
        }
        if (result.fit_sha256 ==
                kForbiddenHeldOutScheduleSha256 ||
            result.check_sha256 ==
                kForbiddenHeldOutScheduleSha256) {
            record_failure(
                "development schedule overlaps held-out identity");
        }
        std::set<std::uint64_t> fit_seeds;
        std::set<std::uint64_t> check_seeds;
        for (const auto& source : fit) {
            if (source.split !=
                    schedule_data::Split::Fit ||
                source.source_seed_base !=
                    schedule_data::kFitSeedBase ||
                forbidden_seed(source.source_seed_base) ||
                forbidden_seed(source.game_seed) ||
                !fit_seeds.insert(
                    source.game_seed).second) {
                record_failure(
                    "FIT schedule row is malformed or forbidden");
                break;
            }
        }
        for (const auto& source : check) {
            if (source.split !=
                    schedule_data::Split::Check ||
                source.source_seed_base !=
                    schedule_data::kCheckSeedBase ||
                forbidden_seed(source.source_seed_base) ||
                forbidden_seed(source.game_seed) ||
                !check_seeds.insert(
                    source.game_seed).second) {
                record_failure(
                    "CHECK schedule row is malformed or forbidden");
                break;
            }
        }
        std::vector<std::uint64_t> overlap;
        std::set_intersection(
            fit_seeds.begin(), fit_seeds.end(),
            check_seeds.begin(), check_seeds.end(),
            std::back_inserter(overlap));
        if (!overlap.empty()) {
            record_failure(
                "FIT and CHECK game seeds overlap");
        }
        if (schedule_data::kGenerationNamespace ==
                kForbiddenHeldOutGenerationNamespace ||
            bundle::kHiddenNamespace ==
                kForbiddenHeldOutHiddenNamespace ||
            bundle::kDominanceNamespace ==
                kForbiddenHeldOutDominanceNamespace ||
            schedule_data::kGenerationNamespace ==
                bundle::kHiddenNamespace ||
            schedule_data::kGenerationNamespace ==
                bundle::kDominanceNamespace ||
            bundle::kHiddenNamespace ==
                bundle::kDominanceNamespace ||
            !collection_spec().valid()) {
            record_failure(
                "development collection namespaces are not isolated");
        }
    } catch (const std::exception& error) {
        record_failure(
            std::string("schedule preflight threw: ") +
            error.what());
    }
    result.exact = result.failures.empty();
    return result;
}

std::string feature_contract_bytes() {
    return
        std::string(bundle::kFeatureSchema) + "\n" +
        "wire=binary64-bits\n" +
        "card-count=26\n" +
        "state-scalar-count=50\n" +
        "state-card-plane-count=24\n" +
        "policy-decision-one-hot-count=4\n" +
        "policy-phase-one-hot-count=7\n" +
        "policy-verb-one-hot-count=8\n" +
        "policy-card-plane-count=6\n" +
        "policy-card-plane-order=source,target,"
        "selected-attackers,assigned-blockers,"
        "relevant-blockers,ordered-blockers\n" +
        "policy-scalar-count=44\n" +
        "layout=state,decision,phase,verb,"
        "policy-card-planes,policy-scalars\n" +
        "feature-count=893\n";
}

bundle::Hash256 feature_contract_sha256() {
    const bundle::Hash256 result =
        bundle::sha256(
            feature_contract_bytes());
    if (result !=
        bundle::parse_sha256(
            bundle::kFeatureContractSha256)) {
        throw std::logic_error(
            "FQ4 feature-contract bytes drifted");
    }
    return result;
}

bundle::CensusRow make_census_row(
    const collection::CanonicalRoot& root,
    const collection::RobustDominance& dominance) {
    const auto& manifest = root.manifest;
    const std::size_t action_count =
        manifest.canonical_descriptors.size();
    if (!dominance.shape_valid ||
        dominance.pass_index != manifest.pass_index ||
        dominance.complete_world_counts.size() !=
            action_count ||
        dominance.strict_world_counts.size() !=
            action_count ||
        dominance.robustly_pass_dominated.size() !=
            action_count ||
        root.probe.candidates.size() != action_count ||
        !root_matches_frozen_schedule(manifest) ||
        manifest.owner_deck ==
            manifest.opponent_deck ||
        root.probe.stable_id != manifest.stable_id ||
        root.probe.root_deck != manifest.owner_deck ||
        root.probe.opponent_deck !=
            manifest.opponent_deck ||
        root.probe.root_player !=
            manifest.locator.owner_seat ||
        manifest.stable_id !=
            collection::block_bound_stable_root_id(
                manifest.locator,
                manifest
                    .information_action_fingerprint,
                kStableRootSchema)) {
        throw std::invalid_argument(
            "cannot encode malformed FQ4 census root");
    }
    for (std::size_t action = 0;
         action < action_count; ++action) {
        if (root.probe.candidates[action].descriptor !=
            manifest.canonical_descriptors[action]) {
            throw std::invalid_argument(
                "cannot encode descriptor-drifted FQ4 census root");
        }
    }
    bundle::CensusRow result{
        .schedule_block =
            narrow_u8(
                manifest.locator.source_block,
                "schedule block"),
        .schedule_index =
            narrow_u16(
                manifest.locator.schedule_index,
                "schedule index"),
        .owner_seat =
            narrow_u8(
                manifest.locator.owner_seat,
                "owner seat"),
        .trace_ordinal =
            narrow_u32(
                manifest.locator.trace_ordinal,
                "trace ordinal"),
        .owner_deck =
            narrow_u8(
                deck_index(manifest.owner_deck),
                "owner deck"),
        .opponent_deck =
            narrow_u8(
                deck_index(manifest.opponent_deck),
                "opponent deck"),
        .stable_root_id =
            bundle::parse_sha256(
                manifest.stable_id),
        .physical_game_sha256 =
            bundle::sha256(
                collection::block_bound_physical_game_id(
                    manifest.locator)),
        .information_action_sha256 =
            bundle::parse_sha256(
                manifest
                    .information_action_fingerprint),
        .descriptor_set_sha256 =
            bundle::descriptor_set_sha256(
                manifest.canonical_descriptors),
        .pass_index =
            narrow_u8(
                manifest.pass_index,
                "Pass index"),
    };
    result.dominance.reserve(action_count);
    for (std::size_t action = 0;
         action < action_count; ++action) {
        const std::size_t complete =
            dominance.complete_world_counts[action];
        const std::size_t strict =
            dominance.strict_world_counts[action];
        if (complete > bundle::kWorldCount ||
            strict > complete ||
            dominance.robustly_pass_dominated[action] !=
                (action != manifest.pass_index &&
                 complete == bundle::kWorldCount &&
                 strict == bundle::kWorldCount) ||
            (action == manifest.pass_index &&
             (complete != 0 || strict != 0))) {
            throw std::invalid_argument(
                "cannot encode inconsistent FQ4 dominance counts");
        }
        result.dominance.push_back({
            .complete = narrow_u8(
                complete, "dominance complete count"),
            .strict = narrow_u8(
                strict, "dominance strict count"),
        });
    }
    const bundle::Split split =
        split_for(
            split_for_seed_base(
                manifest.locator
                    .source_seed_base));
    if (result.physical_game_sha256 !=
            bundle::expected_physical_game_sha256(
                split, result.schedule_block,
                result.schedule_index) ||
        result.stable_root_id !=
            bundle::expected_stable_root_sha256(
                split, result.schedule_block,
                result.schedule_index,
                result.owner_seat,
                result.trace_ordinal,
                result
                    .information_action_sha256)) {
        throw std::invalid_argument(
            "collector and strict codec root identities disagree");
    }
    return result;
}

std::vector<bundle::SparseFeature>
sparsify_priority_features(
    const std::vector<double>& dense_features) {
    if (dense_features.size() !=
        bundle::kFeatureCount) {
        throw std::invalid_argument(
            "Priority feature vector has wrong dimension");
    }
    std::vector<bundle::SparseFeature> result;
    result.reserve(dense_features.size());
    for (std::size_t index = 0;
         index < dense_features.size(); ++index) {
        const double value = dense_features[index];
        if (!std::isfinite(value)) {
            throw std::invalid_argument(
                "Priority feature vector is nonfinite");
        }
        const std::uint64_t bits =
            std::bit_cast<std::uint64_t>(value);
        if (bits == 0) {
            continue;
        }
        result.push_back({
            .index = narrow_u16(
                index, "Priority feature index"),
            .value_bits = bits,
        });
    }
    return result;
}

SplitSupport summarize_support(
    const std::vector<bundle::CensusRow>& census,
    const std::vector<bundle::SelectedRow>& selected,
    const std::vector<ParentWitness>& witnesses) {
    SplitSupport result;
    std::set<bundle::Hash256> census_ids;
    std::set<bundle::Hash256> selected_ids;
    std::set<bundle::Hash256> positive_ids;
    for (const bundle::CensusRow& row : census) {
        if (row.owner_deck >= kDeckCount ||
            !census_ids.insert(
                row.stable_root_id).second) {
            return result;
        }
        ++result.census_by_deck[row.owner_deck];
    }
    for (const bundle::SelectedRow& row :
         selected) {
        if (row.census.owner_deck >= kDeckCount ||
            !census_ids.contains(
                row.census.stable_root_id) ||
            !selected_ids.insert(
                row.census.stable_root_id).second) {
            return SplitSupport{};
        }
        const std::size_t deck =
            row.census.owner_deck;
        ++result.selected_by_deck[deck];
        if ((row.roles &
             static_cast<std::uint8_t>(
                 bundle::Role::DominancePositive)) != 0) {
            ++result.positive_by_deck[deck];
            positive_ids.insert(
                row.census.stable_root_id);
        }
        if ((row.roles &
             static_cast<std::uint8_t>(
                 bundle::Role::BackgroundControl)) != 0) {
            ++result.background_by_deck[deck];
        }
    }

    std::set<bundle::Hash256> high_confidence_ids;
    std::set<bundle::Hash256> high_confidence_games;
    std::set<std::size_t> high_confidence_decks;
    for (const ParentWitness& witness :
         witnesses) {
        const std::size_t deck =
            deck_index(witness.owner_deck);
        if (!positive_ids.contains(
                witness.stable_root_id) ||
            !high_confidence_ids
                 .insert(witness.stable_root_id)
                 .second) {
            return SplitSupport{};
        }
        if (witness.classification ==
                collection::ParentClass::Class1 ||
            witness.classification ==
                collection::ParentClass::Class2) {
            ++result.high_confidence_roots;
            high_confidence_games.insert(
                witness.physical_game_sha256);
            high_confidence_decks.insert(deck);
        }
    }
    result.high_confidence_games =
        high_confidence_games.size();
    result.high_confidence_decks =
        high_confidence_decks.size();

    result.coverage_met =
        !census.empty() &&
        census.size() <=
            bundle::kMaximumCensusRowsPerSplit &&
        !selected.empty() &&
        selected.size() <=
            bundle::kMaximumSelectedRowsPerSplit &&
        std::all_of(
            result.census_by_deck.begin(),
            result.census_by_deck.end(),
            [](std::size_t count) {
                return count > 0 &&
                       count <=
                           bundle::
                               kMaximumCensusRowsPerDeck;
            }) &&
        std::all_of(
            result.selected_by_deck.begin(),
            result.selected_by_deck.end(),
            [](std::size_t count) {
                return count > 0 &&
                       count <=
                           bundle::
                               kMaximumRowsPerDeckAndSplit;
            }) &&
        std::all_of(
            result.positive_by_deck.begin(),
            result.positive_by_deck.end(),
            [](std::size_t count) {
                return count > 0 &&
                       count <=
                           bundle::
                               kMaximumRowsPerDeckAndSplit;
            }) &&
        std::all_of(
            result.background_by_deck.begin(),
            result.background_by_deck.end(),
            [](std::size_t count) {
                return count == 1;
            });
    result.parent_error_floor_met =
        result.high_confidence_roots >=
            kMinimumHighConfidenceRoots &&
        result.high_confidence_games >=
            kMinimumHighConfidenceGames &&
        result.high_confidence_decks >=
            kMinimumHighConfidenceDecks;
    result.failed_gate_mask =
        static_cast<std::uint8_t>(
            (result.coverage_met
                 ? 0U
                 : kCoverageGateFailed) |
            (result.parent_error_floor_met
                 ? 0U
                 : kParentErrorGateFailed));
    return result;
}

std::string format_support_report(
    const SplitSupport& fit,
    const SplitSupport& check) {
    std::string output;
    const auto append =
        [&](std::string_view split,
            const SplitSupport& support) {
            for (std::size_t deck = 0;
                 deck < kDeckCount; ++deck) {
                output +=
                    "support split=" +
                    std::string(split) +
                    " deck=" +
                    std::to_string(deck) +
                    " census=" +
                    std::to_string(
                        support.census_by_deck[deck]) +
                    " selected=" +
                    std::to_string(
                        support.selected_by_deck[deck]) +
                    " positive=" +
                    std::to_string(
                        support.positive_by_deck[deck]) +
                    " background=" +
                    std::to_string(
                        support.background_by_deck[deck]) +
                    "\n";
            }
            output +=
                "support split=" +
                std::string(split) +
                " scope=pooled high_confidence_roots=" +
                std::to_string(
                    support.high_confidence_roots) +
                " high_confidence_games=" +
                std::to_string(
                    support.high_confidence_games) +
                " high_confidence_decks=" +
                std::to_string(
                    support.high_confidence_decks) +
                " coverage_met=" +
                std::to_string(
                    support.coverage_met ? 1 : 0) +
                " parent_error_floor_met=" +
                std::to_string(
                    support.parent_error_floor_met
                        ? 1
                        : 0) +
                " failed_gate_mask=" +
                std::to_string(
                    support.failed_gate_mask) +
                "\n";
        };
    append("fit", fit);
    append("check", check);
    return output;
}

bool complete_constructions_byte_identical(
    std::string_view first,
    std::string_view second) {
    return first == second;
}

FailureScopeReport inspect_failure_scope(
    const std::filesystem::path& executable_path,
    const GenerationProgress& progress) noexcept {
    FailureScopeReport result{
        .progress = progress,
    };
    try {
        const integrity::RegularFileSnapshot executable =
            integrity::snapshot_regular_file(
                executable_path);
        result.executable_after_sha256 =
            executable.sha256;
        result.executable_snapshot_ok = true;
    } catch (const std::exception&) {
    }
    try {
        const integrity::RegularFileSnapshot parent =
            integrity::snapshot_regular_file(
                std::filesystem::path(
                    kParentArtifactPath));
        result.parent_after_sha256 =
            parent.sha256;
        result.parent_snapshot_ok = true;
    } catch (const std::exception&) {
    }

    const std::filesystem::path artifact(
        bundle::kArtifactPath);
    std::error_code artifact_error;
    const std::filesystem::file_status artifact_status =
        std::filesystem::symlink_status(
            artifact, artifact_error);
    if (!artifact_error) {
        result.artifact_status_known = true;
        result.artifact_present =
            std::filesystem::exists(
                artifact_status);
    } else if (artifact_error ==
               std::errc::no_such_file_or_directory) {
        result.artifact_status_known = true;
        result.artifact_present = false;
    }

    const std::filesystem::path parent_directory =
        artifact.has_parent_path()
            ? artifact.parent_path()
            : std::filesystem::path(".");
    const std::string temporary_prefix =
        "." + artifact.filename().string() +
        ".tmp.";
    bool temporary_present = false;
    std::error_code directory_error;
    std::filesystem::directory_iterator iterator(
        parent_directory, directory_error);
    if (!directory_error) {
        const std::filesystem::directory_iterator end;
        while (iterator != end) {
            const std::string filename =
                iterator->path().filename().string();
            if (filename.starts_with(
                    temporary_prefix)) {
                temporary_present = true;
            }
            iterator.increment(directory_error);
            if (directory_error) {
                break;
            }
        }
        if (!directory_error) {
            result.temporary_status_known = true;
            result.temporary_absent =
                !temporary_present;
        }
    } else if (directory_error ==
               std::errc::no_such_file_or_directory) {
        result.temporary_status_known = true;
        result.temporary_absent = true;
    }
    return result;
}

std::string format_failure_scope_report(
    const FailureScopeReport& report) {
    const auto digest_or_unavailable =
        [](bool available,
           const std::string& digest) {
            return available
                       ? digest
                       : std::string("unavailable");
        };
    std::string output =
        "postcondition executable_after_sha256=" +
        digest_or_unavailable(
            report.executable_snapshot_ok,
            report.executable_after_sha256) +
        " parent_after_sha256=" +
        digest_or_unavailable(
            report.parent_snapshot_ok,
            report.parent_after_sha256) +
        " executable_snapshot_ok=" +
        std::to_string(
            report.executable_snapshot_ok ? 1 : 0) +
        " parent_snapshot_ok=" +
        std::to_string(
            report.parent_snapshot_ok ? 1 : 0) +
        " artifact_status_known=" +
        std::to_string(
            report.artifact_status_known ? 1 : 0) +
        " artifact_present=" +
        std::to_string(
            report.artifact_present ? 1 : 0) +
        " temporary_status_known=" +
        std::to_string(
            report.temporary_status_known ? 1 : 0) +
        " temporary_absent=" +
        std::to_string(
            report.temporary_absent ? 1 : 0) +
        " candidate_rollout_evaluations=" +
        std::to_string(
            report.progress
                .candidate_rollout_evaluations) +
        "\n";
    constexpr std::array<std::string_view, 2>
        split_names{"fit", "check"};
    for (std::size_t construction = 0;
         construction < kCompleteConstructions;
         ++construction) {
        for (std::size_t split = 0;
             split < kGenerationSplitCount;
             ++split) {
            output +=
                "postcondition construction=" +
                std::to_string(construction) +
                " split=" +
                std::string(split_names[split]) +
                " source_games_completed=" +
                std::to_string(
                    report.progress
                        .source_games_completed[
                            construction][split]) +
                "\n";
        }
    }
    return output;
}

std::string format_support_rejection_output(
    const SplitSupport& fit,
    const SplitSupport& check,
    const FailureScopeReport& scope) {
    return
        format_support_report(fit, check) +
        format_failure_scope_report(scope) +
        "result=NOT_PUBLISHED"
        " reason=support_gate_failed"
        " failed_gate_mask_fit=" +
        std::to_string(fit.failed_gate_mask) +
        " failed_gate_mask_check=" +
        std::to_string(check.failed_gate_mask) +
        "\n";
}

GenerationFailure::GenerationFailure(
    std::string message,
    FailureScopeReport scope)
    : std::runtime_error(std::move(message)),
      scope_(std::move(scope)) {}

bool CoverageReconstruction::exact() const {
    return
        source_games_reconstructed ==
            2 * schedule_data::kPhysicalGamesPerSplit &&
        selected_rows_reconstructed != 0 &&
        parent_scoring.score_calls ==
            selected_rows_reconstructed &&
        parent_scoring.scored_actions >=
            parent_scoring.score_calls &&
        parent_scoring.scored_actions <=
            selected_rows_reconstructed *
                bundle::kMaximumActions &&
        parent_scoring.sampled_worlds ==
            parent_scoring.score_calls *
                scoring::kProductionWorlds &&
        parent_scoring.rollout_evaluations ==
            parent_scoring.scored_actions *
                scoring::kProductionWorlds *
                scoring::kProductionRolloutsPerWorld &&
        parent_scoring.terminal_evaluations +
                parent_scoring.bootstrap_evaluations ==
            parent_scoring.rollout_evaluations &&
        parent_artifact_sha256 ==
            bundle::kParentArtifactSha256 &&
        bundle_bytes ==
            bundle::kPublishedArtifactBytes &&
        bundle_sha256 ==
            bundle::kPublishedArtifactSha256 &&
        schedules_exact &&
        scientific_manifest_exact &&
        fit_census_exact &&
        check_census_exact &&
        fit_selected_exact &&
        check_selected_exact &&
        fit_manifest_exact &&
        check_manifest_exact &&
        parent_immutable &&
        bundle_immutable &&
        parent_models_loaded == 1 &&
        fits == 0 &&
        candidate_rollout_evaluations == 0 &&
        gameplay_evaluation_seeds == 0;
}

bool Work0Reconstruction::exact() const {
    return
        referenced_source_games != 0 &&
        source_games_replayed ==
            referenced_source_games &&
        requested_roots == cache::kRootCount &&
        artifact.roots.size() == requested_roots &&
        reconstructed_options ==
            cache::kOptionCount &&
        normalized_state_exact_roots ==
            requested_roots &&
        hidden_clone_eligible_roots ==
            requested_roots &&
        hidden_clone_distinct_roots ==
            requested_roots &&
        hidden_feature_exact_roots ==
            requested_roots &&
        source_rows_exact &&
        codec_round_trip_exact &&
        encoding_bit_identical &&
        inputs_immutable;
}

Work0Reconstruction
reconstruct_work0_selected_roots_once() {
    const auto fit_schedule =
        schedule_data::source_schedule(
            schedule_data::Split::Fit);
    const auto check_schedule =
        schedule_data::source_schedule(
            schedule_data::Split::Check);
    const SchedulePreflight preflight =
        preflight_schedules(
            fit_schedule, check_schedule);
    if (!preflight.exact ||
        feature_contract_sha256() !=
            bundle::parse_sha256(
                bundle::kFeatureContractSha256) ||
        collection_spec_contract_sha256() !=
            bundle::parse_sha256(
                bundle::kCollectionSpecSha256)) {
        fail("WORK0 scientific preflight drifted");
    }

    const std::filesystem::path parent_path(
        kParentArtifactPath);
    const std::filesystem::path bundle_path(
        bundle::kArtifactPath);
    const std::filesystem::path neutral_path =
        neutral::production_artifact_path();
    const integrity::RegularFileSnapshot parent_before =
        integrity::snapshot_regular_file(parent_path);
    const integrity::RegularFileSnapshot bundle_before =
        integrity::snapshot_regular_file(bundle_path);
    const integrity::RegularFileSnapshot neutral_before =
        integrity::snapshot_regular_file(neutral_path);
    if (parent_before.byte_size !=
            cache::kParentArtifactBytes ||
        parent_before.sha256 !=
            cache::kParentArtifactSha256 ||
        bundle_before.byte_size !=
            cache::kDev1ArtifactBytes ||
        bundle_before.sha256 !=
            cache::kDev1ArtifactSha256 ||
        neutral_before.byte_size !=
            cache::kNeutralArtifactBytes ||
        neutral_before.sha256 !=
            cache::kNeutralArtifactSha256) {
        fail("WORK0 immutable input identity drifted");
    }

    const bundle::Bundle dev1 =
        bundle::load_published();
    if (!published_scientific_manifest_exact(
            dev1.manifest)) {
        fail("WORK0 DEV1 manifest drifted");
    }
    const auto capacity =
        neutral::accepted_dev4_capacity();
    const neutral::Artifact neutral_artifact =
        neutral::load_published(
            neutral::make_contract(
                dev1.manifest, capacity),
            {
                .bytes = cache::kNeutralArtifactBytes,
                .sha256 =
                    std::string(
                        cache::kNeutralArtifactSha256),
            });

    const auto parent =
        load_learned_value_challenger_artifact(
            std::string(kParentArtifactPath),
            kParentTrainingGames,
            kParentTrainingSeed,
            kParentGenerations)
            .model();
    require_parent_identity(parent);

    std::vector<Work0Target> targets;
    targets.reserve(cache::kRootCount);
    const auto append_dev1 =
        [&](bundle::Split split,
            const std::vector<bundle::SelectedRow>& rows) {
            const auto& source_schedule =
                split == bundle::Split::Fit
                    ? fit_schedule
                    : check_schedule;
            for (const bundle::SelectedRow& row : rows) {
                const bundle::CensusRow& census =
                    row.census;
                if (census.schedule_index >=
                    source_schedule.size()) {
                    fail(
                        "WORK0 DEV1 source index is invalid");
                }
                const schedule_data::SourceGame& source =
                    source_schedule[census.schedule_index];
                targets.push_back({
                    .family =
                        cache::SourceFamily::
                            Dev1Selected,
                    .split = split,
                    .artifact_index = targets.size(),
                    .source_row =
                        narrow_u32(
                            targets.size(),
                            "WORK0 DEV1 row"),
                    .source_roles = row.roles,
                    .production_seed =
                        row.production_seed,
                    .locator = {
                        .source_block =
                            source.schedule_block,
                        .source_seed_base =
                            source.source_seed_base,
                        .schedule_index =
                            source.schedule_index,
                        .game_seed = source.game_seed,
                        .owner_seat =
                            census.owner_seat,
                        .trace_ordinal =
                            census.trace_ordinal,
                    },
                    .owner_deck = census.owner_deck,
                    .opponent_deck =
                        census.opponent_deck,
                    .stable_root_id =
                        census.stable_root_id,
                    .physical_game_sha256 =
                        census.physical_game_sha256,
                    .information_action_sha256 =
                        census.information_action_sha256,
                    .descriptor_set_sha256 =
                        census.descriptor_set_sha256,
                    .action_count = row.actions.size(),
                    .pass_index = census.pass_index,
                });
            }
        };
    append_dev1(bundle::Split::Fit, dev1.fit_rows);
    append_dev1(
        bundle::Split::Check, dev1.check_rows);
    if (targets.size() != cache::kDev1RootCount) {
        fail("WORK0 DEV1 target count drifted");
    }

    for (std::size_t index = 0;
         index < neutral_artifact.rows.size(); ++index) {
        const neutral::NeutralRow& row =
            neutral_artifact.rows[index];
        const neutral::EligibleRoot& eligible =
            row.locator.root;
        const auto& source_schedule =
            eligible.rank.split == bundle::Split::Fit
                ? fit_schedule
                : check_schedule;
        if (eligible.schedule_index >=
            source_schedule.size()) {
            fail(
                "WORK0 neutral source index is invalid");
        }
        const schedule_data::SourceGame& source =
            source_schedule[eligible.schedule_index];
        targets.push_back({
            .family =
                cache::SourceFamily::Dev5Neutral,
            .split = eligible.rank.split,
            .artifact_index = targets.size(),
            .source_row =
                narrow_u32(
                    index, "WORK0 neutral row"),
            .source_roles = 0,
            .production_seed =
                row.production_seed,
            .locator = {
                .source_block =
                    source.schedule_block,
                .source_seed_base =
                    source.source_seed_base,
                .schedule_index =
                    source.schedule_index,
                .game_seed = source.game_seed,
                .owner_seat = eligible.owner_seat,
                .trace_ordinal =
                    eligible.trace_ordinal,
            },
            .owner_deck =
                eligible.rank.owner_deck,
            .opponent_deck =
                eligible.opponent_deck,
            .stable_root_id =
                eligible.rank.stable_root_id,
            .physical_game_sha256 =
                eligible.rank
                    .physical_game_sha256,
            .information_action_sha256 =
                row.information_action_sha256,
            .descriptor_set_sha256 =
                row.descriptor_set_sha256,
            .action_count = row.actions.size(),
            .pass_index = row.pass_index,
            .has_neutral_locator = true,
            .neutral_locator = row.locator,
        });
    }
    if (targets.size() != cache::kRootCount) {
        fail("WORK0 target count drifted");
    }

    using GameKey = std::pair<std::uint8_t, std::size_t>;
    std::map<GameKey, std::vector<std::size_t>>
        targets_by_game;
    for (std::size_t index = 0;
         index < targets.size(); ++index) {
        const Work0Target& target = targets[index];
        targets_by_game[
            {static_cast<std::uint8_t>(
                 target.split),
             target.locator.schedule_index}]
            .push_back(index);
    }
    std::vector<std::optional<Work0RootBuild>> captured(
        targets.size());
    std::size_t games_replayed = 0;
    for (const auto& [key, indices] :
         targets_by_game) {
        const bundle::Split split =
            static_cast<bundle::Split>(key.first);
        const auto& source_schedule =
            split == bundle::Split::Fit
                ? fit_schedule
                : check_schedule;
        if (key.second >= source_schedule.size()) {
            fail("WORK0 grouped source index is invalid");
        }
        const schedule_data::SourceGame& source =
            source_schedule[key.second];
        const GameConfig config =
            source_game_config(
                parent, source.starting_player);
        if (!source_config_exact(
                config, parent,
                source.starting_player)) {
            fail("WORK0 source recipe drifted");
        }
        std::vector<LearnedDecisionTracePoint> trace;
        const auto decks = original_decks(source);
        Game game(
            decks[0], decks[1],
            source.game_seed, config);
        static_cast<void>(
            game.run_with_priority_root_trace(trace));
        ++games_replayed;

        const collection::SourceGame common_source =
            collection_source(source);
        for (const std::size_t target_index :
             indices) {
            const Work0Target& target =
                targets[target_index];
            if (target.locator.trace_ordinal >=
                    trace.size() ||
                captured[target.artifact_index]
                    .has_value()) {
                fail(
                    "WORK0 trace ordinal is missing or duplicated");
            }
            const LearnedDecisionTracePoint& point =
                trace[target.locator.trace_ordinal];
            if (point.context.decision_player !=
                target.locator.owner_seat) {
                fail(
                    "WORK0 trace owner disagrees with locator");
            }
            const collection::RootBuildResult built =
                collection::build_canonical_root(
                    point, common_source,
                    target.locator.owner_seat,
                    target.locator.trace_ordinal,
                    collection_spec());
            if (built.disposition !=
                    collection::RootDisposition::
                        RetentionCandidate ||
                !built.root.has_value()) {
                fail(
                    "WORK0 requested root was not canonical");
            }
            captured[target.artifact_index] =
                make_work0_root(
                    target, *built.root);
        }
    }

    cache::Artifact artifact;
    artifact.manifest = {
        .schema = std::string(cache::kSchema),
        .environment =
            std::string(cache::kEnvironment),
        .sources = {
            .parent_artifact = {
                .bytes = parent_before.byte_size,
                .sha256 =
                    bundle::parse_sha256(
                        parent_before.sha256),
            },
            .parent_model_fingerprint =
                bundle::parse_sha256(
                    cache::kParentModelFingerprint),
            .dev1_artifact = {
                .bytes = bundle_before.byte_size,
                .sha256 =
                    bundle::parse_sha256(
                        bundle_before.sha256),
            },
            .dev1_fit_selection_sha256 =
                dev1.manifest.fit.selection_sha256,
            .dev1_fit_scored_sha256 =
                dev1.manifest.fit.scored_sha256,
            .dev1_check_selection_sha256 =
                dev1.manifest.check.selection_sha256,
            .dev1_check_scored_sha256 =
                dev1.manifest.check.scored_sha256,
            .neutral_artifact = {
                .bytes = neutral_before.byte_size,
                .sha256 =
                    bundle::parse_sha256(
                        neutral_before.sha256),
            },
            .neutral_selected_order_sha256 =
                neutral_artifact.manifest
                    .selected_order_sha256,
        },
        .dev1_options =
            cache::kDev1OptionCount,
        .neutral_options =
            cache::kNeutralOptionCount,
    };
    artifact.roots.reserve(captured.size());
    std::size_t options = 0;
    std::size_t normalized_state_exact_roots = 0;
    std::size_t hidden_clone_eligible_roots = 0;
    std::size_t hidden_clone_distinct_roots = 0;
    std::size_t hidden_feature_exact_roots = 0;
    for (std::optional<Work0RootBuild>& built :
         captured) {
        if (!built.has_value()) {
            fail("WORK0 targeted replay omitted a root");
        }
        options += built->root.raw_actions.size();
        normalized_state_exact_roots +=
            built->normalized_state_exact ? 1U : 0U;
        hidden_clone_eligible_roots +=
            built->hidden_clone_eligible ? 1U : 0U;
        hidden_clone_distinct_roots +=
            built->hidden_clone_distinct ? 1U : 0U;
        hidden_feature_exact_roots +=
            built->hidden_feature_exact ? 1U : 0U;
        artifact.roots.push_back(
            std::move(built->root));
    }
    artifact.manifest.root_order_sha256 =
        cache::root_order_sha256(artifact.roots);
    cache::validate_against_sources(
        artifact, dev1, neutral_artifact);
    const std::string first_encoding =
        cache::encode(artifact);
    const std::string second_encoding =
        cache::encode(artifact);
    const bool encoding_bit_identical =
        first_encoding == second_encoding;
    const bool codec_round_trip_exact =
        cache::decode(first_encoding) == artifact;
    if (!encoding_bit_identical ||
        !codec_round_trip_exact) {
        fail("WORK0 cache codec gate failed");
    }
    require_parent_identity(parent);

    const integrity::RegularFileSnapshot parent_after =
        integrity::snapshot_regular_file(parent_path);
    const integrity::RegularFileSnapshot bundle_after =
        integrity::snapshot_regular_file(bundle_path);
    const integrity::RegularFileSnapshot neutral_after =
        integrity::snapshot_regular_file(neutral_path);
    Work0Reconstruction result{
        .artifact = std::move(artifact),
        .referenced_source_games =
            targets_by_game.size(),
        .source_games_replayed = games_replayed,
        .requested_roots = targets.size(),
        .reconstructed_options = options,
        .normalized_state_exact_roots =
            normalized_state_exact_roots,
        .hidden_clone_eligible_roots =
            hidden_clone_eligible_roots,
        .hidden_clone_distinct_roots =
            hidden_clone_distinct_roots,
        .hidden_feature_exact_roots =
            hidden_feature_exact_roots,
        .source_rows_exact = true,
        .codec_round_trip_exact =
            codec_round_trip_exact,
        .encoding_bit_identical =
            encoding_bit_identical,
        .inputs_immutable =
            parent_after == parent_before &&
            bundle_after == bundle_before &&
            neutral_after == neutral_before,
    };
    if (!result.exact()) {
        fail("WORK0 targeted reconstruction is not exact");
    }
    return result;
}

CoverageReconstruction reconstruct_published_coverage_once() {
    const auto fit_schedule =
        schedule_data::source_schedule(
            schedule_data::Split::Fit);
    const auto check_schedule =
        schedule_data::source_schedule(
            schedule_data::Split::Check);
    const SchedulePreflight preflight =
        preflight_schedules(
            fit_schedule, check_schedule);
    if (!preflight.exact) {
        fail(
            preflight.failures.empty()
                ? "coverage schedule preflight failed"
                : preflight.failures.front());
    }
    if (feature_contract_sha256() !=
            bundle::parse_sha256(
                bundle::kFeatureContractSha256) ||
        collection_spec_contract_sha256() !=
            bundle::parse_sha256(
                bundle::kCollectionSpecSha256)) {
        fail("coverage scientific-contract preflight drifted");
    }

    const auto parent_path =
        std::filesystem::path(kParentArtifactPath);
    const auto bundle_path =
        std::filesystem::path(bundle::kArtifactPath);
    const integrity::RegularFileSnapshot parent_before =
        integrity::snapshot_regular_file(parent_path);
    const integrity::RegularFileSnapshot bundle_before =
        integrity::snapshot_regular_file(bundle_path);
    if (parent_before.sha256 !=
            bundle::kParentArtifactSha256 ||
        bundle_before.byte_size !=
            bundle::kPublishedArtifactBytes ||
        bundle_before.sha256 !=
            bundle::kPublishedArtifactSha256) {
        fail("coverage immutable input identity drifted");
    }

    const bundle::Bundle published =
        bundle::load_published();
    const bundle::Manifest& manifest =
        published.manifest;
    const bool scientific_manifest_exact =
        manifest.purpose == bundle::kPurpose &&
        manifest.parent_artifact_sha256 ==
            bundle::parse_sha256(
                bundle::kParentArtifactSha256) &&
        manifest.parent_model_fingerprint ==
            bundle::parse_sha256(
                bundle::kParentModelFingerprint) &&
        manifest.parent_components.critic ==
            bundle::parse_sha256(
                bundle::kParentCriticFingerprint) &&
        manifest.parent_components.priority ==
            bundle::parse_sha256(
                bundle::kParentPriorityFingerprint) &&
        manifest.parent_components.attack ==
            bundle::parse_sha256(
                bundle::kParentAttackFingerprint) &&
        manifest.parent_components.block ==
            bundle::parse_sha256(
                bundle::kParentBlockFingerprint) &&
        manifest.parent_components.damage_order ==
            bundle::parse_sha256(
                bundle::kParentDamageOrderFingerprint) &&
        manifest.generation_namespace ==
            bundle::kGenerationNamespace &&
        manifest.hidden_namespace ==
            bundle::kHiddenNamespace &&
        manifest.dominance_namespace ==
            bundle::kDominanceNamespace &&
        manifest.collection_spec_sha256 ==
            collection_spec_contract_sha256() &&
        manifest.production_recipe ==
            bundle::kProductionRecipe &&
        manifest.feature_schema ==
            bundle::kFeatureSchema &&
        manifest.feature_count ==
            bundle::kFeatureCount &&
        manifest.feature_contract_sha256 ==
            feature_contract_sha256();
    if (!scientific_manifest_exact) {
        fail("coverage published scientific manifest drifted");
    }

    const auto parent =
        load_learned_value_challenger_artifact(
            std::string(kParentArtifactPath),
            kParentTrainingGames,
            kParentTrainingSeed,
            kParentGenerations)
            .model();
    require_parent_identity(parent);

    CoverageReconstruction result;
    result.roots.reserve(
        published.fit_census.size() +
        published.check_census.size());
    std::uint64_t fit_games = 0;
    std::uint64_t check_games = 0;
    SplitBuild fit =
        construct_split(
            schedule_data::Split::Fit,
            fit_schedule, parent, fit_games,
            &result.roots);
    require_parent_identity(parent);
    SplitBuild check =
        construct_split(
            schedule_data::Split::Check,
            check_schedule, parent, check_games,
            &result.roots);
    require_parent_identity(parent);

    result.source_games_reconstructed =
        static_cast<std::size_t>(
            fit_games + check_games);
    result.selected_rows_reconstructed =
        fit.selected.size() +
        check.selected.size();
    const auto summarize_parent_scoring =
        [](const std::vector<bundle::SelectedRow>& rows) {
            bundle::ScoreAccounting total;
            for (const bundle::SelectedRow& row : rows) {
                const std::uint64_t actions =
                    static_cast<std::uint64_t>(
                        row.actions.size());
                if (row.accounting.score_calls != 1 ||
                    row.accounting.scored_actions !=
                        actions ||
                    row.accounting.sampled_worlds !=
                        scoring::kProductionWorlds ||
                    row.accounting
                            .rollout_evaluations !=
                        actions *
                            scoring::
                                kProductionWorlds *
                            scoring::
                                kProductionRolloutsPerWorld ||
                    row.accounting
                                .terminal_evaluations +
                            row.accounting
                                .bootstrap_evaluations !=
                        row.accounting
                            .rollout_evaluations) {
                    fail(
                        "published parent scoring accounting drifted");
                }
                total.score_calls +=
                    row.accounting.score_calls;
                total.scored_actions +=
                    row.accounting.scored_actions;
                total.sampled_worlds +=
                    row.accounting.sampled_worlds;
                total.rollout_evaluations +=
                    row.accounting
                        .rollout_evaluations;
                total.terminal_evaluations +=
                    row.accounting
                        .terminal_evaluations;
                total.bootstrap_evaluations +=
                    row.accounting
                        .bootstrap_evaluations;
            }
            return total;
        };
    result.parent_scoring =
        summarize_parent_scoring(fit.selected);
    const bundle::ScoreAccounting check_scoring =
        summarize_parent_scoring(check.selected);
    result.parent_scoring.score_calls +=
        check_scoring.score_calls;
    result.parent_scoring.scored_actions +=
        check_scoring.scored_actions;
    result.parent_scoring.sampled_worlds +=
        check_scoring.sampled_worlds;
    result.parent_scoring.rollout_evaluations +=
        check_scoring.rollout_evaluations;
    result.parent_scoring.terminal_evaluations +=
        check_scoring.terminal_evaluations;
    result.parent_scoring.bootstrap_evaluations +=
        check_scoring.bootstrap_evaluations;
    result.parent_artifact_sha256 =
        parent_before.sha256;
    result.bundle_bytes =
        static_cast<std::size_t>(
            bundle_before.byte_size);
    result.bundle_sha256 =
        bundle_before.sha256;
    result.schedules_exact = preflight.exact;
    result.scientific_manifest_exact =
        scientific_manifest_exact;
    result.fit_census_exact =
        fit.census == published.fit_census;
    result.check_census_exact =
        check.census == published.check_census;
    result.fit_selected_exact =
        fit.selected == published.fit_rows;
    result.check_selected_exact =
        check.selected == published.check_rows;
    result.fit_manifest_exact =
        fit.manifest == manifest.fit;
    result.check_manifest_exact =
        check.manifest == manifest.check;
    result.parent_models_loaded = 1;

    const integrity::RegularFileSnapshot parent_after =
        integrity::snapshot_regular_file(parent_path);
    const integrity::RegularFileSnapshot bundle_after =
        integrity::snapshot_regular_file(bundle_path);
    result.parent_immutable =
        parent_after == parent_before;
    result.bundle_immutable =
        bundle_after == bundle_before;

    if (result.roots.size() !=
            published.fit_census.size() +
                published.check_census.size() ||
        !result.exact()) {
        fail("coverage reconstruction did not reproduce the bundle");
    }
    return result;
}

neutral::Artifact materialize_neutral_supplement(
    const std::filesystem::path& executable_path,
    std::string_view producer_commit) {
    constexpr std::uintmax_t kParentArtifactBytes =
        3'111'437;
    constexpr std::uint64_t kExpectedSourceGames = 320;
    constexpr std::uint64_t kExpectedRetainedRoots = 9'728;
    constexpr std::uint64_t kExpectedRetainedOptions = 29'108;
    const bundle::ScoreAccounting expected_parent_scoring{
        .score_calls = 192,
        .scored_actions = 1'141,
        .sampled_worlds = 1'536,
        .rollout_evaluations = 9'128,
        .terminal_evaluations = 1'787,
        .bootstrap_evaluations = 7'341,
    };
    if (!canonical_lower_hex(producer_commit, 40) ||
        std::all_of(
            producer_commit.begin(),
            producer_commit.end(),
            [](char character) {
                return character == '0';
            })) {
        throw std::invalid_argument(
            "FQ4 neutral producer commit must be nonzero "
            "canonical lowercase 40-hex");
    }

    const auto fit_schedule =
        schedule_data::source_schedule(
            schedule_data::Split::Fit);
    const auto check_schedule =
        schedule_data::source_schedule(
            schedule_data::Split::Check);
    const SchedulePreflight preflight =
        preflight_schedules(
            fit_schedule, check_schedule);
    if (!preflight.exact) {
        fail(
            preflight.failures.empty()
                ? "neutral schedule preflight failed"
                : preflight.failures.front());
    }
    if (feature_contract_sha256() !=
            bundle::parse_sha256(
                bundle::kFeatureContractSha256) ||
        collection_spec_contract_sha256() !=
            bundle::parse_sha256(
                bundle::kCollectionSpecSha256)) {
        fail("neutral scientific-contract preflight drifted");
    }

    const std::filesystem::path parent_path(
        kParentArtifactPath);
    const std::filesystem::path bundle_path(
        bundle::kArtifactPath);
    const integrity::RegularFileSnapshot executable_before =
        integrity::snapshot_regular_file(executable_path);
    const integrity::RegularFileSnapshot parent_before =
        integrity::snapshot_regular_file(parent_path);
    const integrity::RegularFileSnapshot bundle_before =
        integrity::snapshot_regular_file(bundle_path);
    if (parent_before.byte_size !=
            kParentArtifactBytes ||
        parent_before.sha256 !=
            bundle::kParentArtifactSha256 ||
        bundle_before.byte_size !=
            bundle::kPublishedArtifactBytes ||
        bundle_before.sha256 !=
            bundle::kPublishedArtifactSha256) {
        fail("neutral immutable input identity drifted");
    }

    const bundle::Bundle published =
        bundle::load_published();
    if (!published_scientific_manifest_exact(
            published.manifest)) {
        fail("neutral published scientific manifest drifted");
    }

    const auto parent =
        load_learned_value_challenger_artifact(
            std::string(kParentArtifactPath),
            kParentTrainingGames,
            kParentTrainingSeed,
            kParentGenerations)
            .model();
    require_parent_identity(parent);

    std::vector<CoverageRootObservation> observations;
    observations.reserve(kExpectedRetainedRoots);
    std::uint64_t fit_games = 0;
    std::uint64_t check_games = 0;
    SplitBuild fit =
        construct_split(
            schedule_data::Split::Fit,
            fit_schedule, parent, fit_games,
            &observations, true);
    require_parent_identity(parent);
    SplitBuild check =
        construct_split(
            schedule_data::Split::Check,
            check_schedule, parent, check_games,
            &observations, true);
    require_parent_identity(parent);

    const bool dev1_sections_exact =
        fit.census == published.fit_census &&
        check.census == published.check_census &&
        fit.selected == published.fit_rows &&
        check.selected == published.check_rows &&
        fit.manifest == published.manifest.fit &&
        check.manifest == published.manifest.check;
    if (!dev1_sections_exact ||
        fit_games + check_games !=
            kExpectedSourceGames) {
        fail(
            "neutral reconstruction did not reproduce DEV1");
    }

    bundle::ScoreAccounting parent_scoring =
        summarize_scoring(fit.selected);
    add_score_accounting(
        parent_scoring,
        summarize_scoring(check.selected));
    std::uint64_t retained_options = 0;
    for (const CoverageRootObservation& observation :
         observations) {
        if (observation.option_count >
            std::numeric_limits<std::uint64_t>::max() -
                retained_options) {
            fail("neutral retained-option count overflows");
        }
        retained_options +=
            static_cast<std::uint64_t>(
                observation.option_count);
    }
    if (observations.size() !=
            kExpectedRetainedRoots ||
        retained_options !=
            kExpectedRetainedOptions ||
        parent_scoring != expected_parent_scoring) {
        fail(
            "neutral reconstruction ledger disagrees "
            "with published DEV1");
    }
    const coverage::CoverageCensus dev4_capacity =
        coverage::measure(observations);

    std::vector<neutral::EligibleRoot> eligible;
    eligible.reserve(
        fit.neutral_eligible.size() +
        check.neutral_eligible.size());
    eligible.insert(
        eligible.end(),
        fit.neutral_eligible.begin(),
        fit.neutral_eligible.end());
    eligible.insert(
        eligible.end(),
        check.neutral_eligible.begin(),
        check.neutral_eligible.end());
    const neutral::FrozenSelection frozen =
        neutral::freeze_selection(
            eligible, dev4_capacity);
    const neutral::Hash256 selected_order_digest =
        neutral::selected_order_sha256(frozen.rows);
    if (selected_order_digest !=
            frozen.selected_order_sha256) {
        fail("neutral selected-order digest drifted");
    }

    std::vector<CapturedNeutralRoot> captured;
    captured.reserve(
        fit.neutral_selected.size() +
        check.neutral_selected.size());
    std::move(
        fit.neutral_selected.begin(),
        fit.neutral_selected.end(),
        std::back_inserter(captured));
    std::move(
        check.neutral_selected.begin(),
        check.neutral_selected.end(),
        std::back_inserter(captured));
    if (captured.size() != frozen.rows.size()) {
        fail(
            "provisional and authoritative neutral "
            "selection sizes differ");
    }
    for (std::size_t index = 0;
         index < captured.size(); ++index) {
        if (captured[index].locator !=
            frozen.rows[index]) {
            fail(
                "provisional and authoritative neutral "
                "locators differ");
        }
    }

    neutral::Artifact artifact;
    artifact.manifest.contract =
        neutral::make_contract(
            published.manifest, dev4_capacity);
    artifact.manifest.producer_commit =
        std::string(producer_commit);
    artifact.manifest.producer_executable_sha256 =
        bundle::parse_sha256(
            executable_before.sha256);
    artifact.manifest.selected_order_sha256 =
        selected_order_digest;
    neutral::PublisherAccounting& accounting =
        artifact.manifest.accounting;
    accounting.reconstruction = {
        .source_games =
            static_cast<std::uint64_t>(
                fit_games + check_games),
        .retained_roots =
            static_cast<std::uint64_t>(
                observations.size()),
        .retained_options = retained_options,
        .parent_scoring = parent_scoring,
    };
    accounting.selection_frozen_before_scoring = true;
    accounting.dev1_scientific_sections_exact =
        dev1_sections_exact;
    accounting.parent_models_loaded = 1;

    artifact.rows.reserve(captured.size());
    for (const CapturedNeutralRoot& row : captured) {
        const std::size_t split =
            row.locator.root.rank.split ==
                    bundle::Split::Fit
                ? 0
                : 1;
        const std::size_t deck =
            row.locator.root.rank.owner_deck;
        if (deck >= neutral::kDeckCount) {
            fail("neutral hidden-control deck is invalid");
        }
        std::uint16_t& hidden_count =
            row.retained.hidden.distinct
                ? accounting
                      .distinct_hidden_controls[split][deck]
                : accounting
                      .nondistinct_hidden_controls[split][deck];
        if (hidden_count ==
            std::numeric_limits<std::uint16_t>::max()) {
            fail("neutral hidden-control count overflows");
        }
        ++hidden_count;
        artifact.rows.push_back(
            score_neutral_root(
                row, parent,
                accounting.canonical_neutral,
                accounting.hidden_clone,
                accounting.bit_identical_actions));
    }
    accounting.canonical_hidden_bit_identical = true;
    require_parent_identity(parent);

    const integrity::RegularFileSnapshot executable_after =
        integrity::snapshot_regular_file(executable_path);
    const integrity::RegularFileSnapshot parent_after =
        integrity::snapshot_regular_file(parent_path);
    const integrity::RegularFileSnapshot bundle_after =
        integrity::snapshot_regular_file(bundle_path);
    if (executable_after != executable_before ||
        parent_after != parent_before ||
        bundle_after != bundle_before) {
        fail(
            "neutral immutable input changed during "
            "materialization");
    }
    accounting.parent_immutable = true;
    accounting.bundle_immutable = true;
    accounting.executable_immutable = true;

    neutral::validate(artifact);
    return artifact;
}

GenerationReport generate_and_publish(
    const std::filesystem::path& executable_path,
    std::string_view producer_commit,
    GenerationProgress* progress) {
    GenerationProgress local_progress;
    GenerationProgress& active_progress =
        progress == nullptr
            ? local_progress
            : *progress;
    active_progress = GenerationProgress{};
    try {
    if (!canonical_lower_hex(producer_commit, 40) &&
        !canonical_lower_hex(producer_commit, 64)) {
        throw std::invalid_argument(
            "FQ4 producer commit must be canonical lowercase "
            "Git hexadecimal");
    }

    // This is the only game-free schedule opening point. Nothing below may
    // load a model or construct a Game unless this exact preflight passes.
    const auto fit_schedule =
        schedule_data::source_schedule(
            schedule_data::Split::Fit);
    const auto check_schedule =
        schedule_data::source_schedule(
            schedule_data::Split::Check);
    const SchedulePreflight preflight =
        preflight_schedules(
            fit_schedule, check_schedule);
    if (!preflight.exact) {
        fail(
            preflight.failures.empty()
                ? "schedule preflight failed without a reason"
                : preflight.failures.front());
    }
    if (feature_contract_sha256() !=
        bundle::parse_sha256(
            bundle::kFeatureContractSha256)) {
        fail("feature-contract preflight drifted");
    }
    if (collection_spec_contract_sha256() !=
        bundle::parse_sha256(
            bundle::kCollectionSpecSha256)) {
        fail("collection-spec preflight drifted");
    }

    std::error_code artifact_status_error;
    const auto artifact_status =
        std::filesystem::symlink_status(
            std::filesystem::path(
                bundle::kArtifactPath),
            artifact_status_error);
    if (!artifact_status_error &&
        std::filesystem::exists(artifact_status)) {
        fail(
            "fixed development artifact already exists; "
            "replacement is forbidden");
    }
    if (artifact_status_error &&
        artifact_status_error !=
            std::errc::no_such_file_or_directory) {
        fail(
            "cannot preflight fixed development artifact path");
    }

    const integrity::RegularFileSnapshot executable_before =
        integrity::snapshot_regular_file(
            executable_path);
    const integrity::RegularFileSnapshot parent_before =
        integrity::snapshot_regular_file(
            std::filesystem::path(
                kParentArtifactPath));
    if (parent_before.sha256 !=
        bundle::kParentArtifactSha256) {
        fail("immutable C16 artifact SHA-256 drifted");
    }
    const auto parent =
        load_learned_value_challenger_artifact(
            std::string(kParentArtifactPath),
            kParentTrainingGames,
            kParentTrainingSeed,
            kParentGenerations)
            .model();
    require_parent_identity(parent);

    CompleteBuild first =
        construct_complete(
            fit_schedule, check_schedule,
            parent, executable_before,
            producer_commit, 0,
            active_progress);
    CompleteBuild second =
        construct_complete(
            fit_schedule, check_schedule,
            parent, executable_before,
            producer_commit, 1,
            active_progress);
    if (first.bundle != second.bundle ||
        first.fit != second.fit ||
        first.check != second.check) {
        fail(
            "two complete constructions are not byte identical");
    }

    const integrity::RegularFileSnapshot parent_after =
        integrity::snapshot_regular_file(
            std::filesystem::path(
                kParentArtifactPath));
    const integrity::RegularFileSnapshot executable_after =
        integrity::snapshot_regular_file(
            executable_path);
    if (parent_after != parent_before) {
        fail("immutable C16 artifact changed during generation");
    }
    if (executable_after != executable_before) {
        fail("producer executable changed during generation");
    }
    require_parent_identity(parent);

    GenerationReport report{
        .fit = first.fit,
        .check = first.check,
        .source_games_per_construction =
            2 * schedule_data::
                    kPhysicalGamesPerSplit,
        .complete_constructions = 2,
        .source_game_executions =
            4 * schedule_data::
                    kPhysicalGamesPerSplit,
        .scored_rows =
            first.bundle.fit_rows.size() +
            first.bundle.check_rows.size(),
        .candidate_rollout_evaluations = 0,
        .repeated_construction_bit_identical =
            true,
    };
    if (!first.fit.publishable() ||
        !first.check.publishable()) {
        report.scope =
            inspect_failure_scope(
                executable_path,
                active_progress);
        return report;
    }

    const std::string first_bytes =
        bundle::encode(first.bundle);
    const std::string second_bytes =
        bundle::encode(second.bundle);
    if (!complete_constructions_byte_identical(
            first_bytes, second_bytes)) {
        fail(
            "two publishable artifact encodings are not byte identical");
    }

    // The bundle implementation writes a same-directory temporary, fsyncs
    // it, and links it into the sole fixed target without replacement.
    bundle::publish_atomic_no_replace(first.bundle);
    const integrity::RegularFileSnapshot published =
        integrity::snapshot_regular_file(
            std::filesystem::path(
                bundle::kArtifactPath));
    if (published.byte_size !=
            first_bytes.size() ||
        published.sha256 !=
            integrity::sha256_string(
                first_bytes)) {
        fail("published artifact identity drifted");
    }

    report.artifact_bytes = first_bytes.size();
    report.artifact_sha256 =
        integrity::sha256_string(first_bytes);
    report.published = true;
    report.scope =
        inspect_failure_scope(
            executable_path,
            active_progress);
    return report;
    } catch (const GenerationFailure&) {
        throw;
    } catch (const std::exception& error) {
        throw GenerationFailure(
            error.what(),
            inspect_failure_scope(
                executable_path,
                active_progress));
    }
}

} // namespace old_school::fq4_dev_generator
