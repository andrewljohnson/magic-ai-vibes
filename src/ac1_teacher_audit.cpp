#include "old_school/ac1_teacher_audit.hpp"

#include "old_school/audit_common.hpp"
#include "old_school/oc1_action_regression.hpp"
#include "old_school/output_calibration.hpp"
#include "old_school/output_calibration_artifact.hpp"
#include "old_school/probe_runner.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <numeric>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

namespace old_school::ac1_teacher_audit {
namespace {

namespace ar1 = oc1_action_regression;
namespace calibration = output_calibration;
namespace scoring = oc1_action_scoring;

struct ExpectedRoot {
    std::string_view stable_id;
    std::string_view fingerprint;
    DeckId deck;
    std::string_view factory_contract_fingerprint;
};

constexpr std::array<ExpectedRoot, kPhysicalPriorityRoots>
    kExpectedRoots = {{
        {"field.green.begin-combat-growth-tapped-air.v1",
         "0fe2f0471ba9019a", DeckId::Green,
         "95f65b5658e44f5464cb02aa5d1efa25aa8289135661d32ddca97853c369c364"},
        {"field.green.second-main-sick-bear-growth.v1",
         "6bf340aaaca49e8a", DeckId::Green,
         "e1b8ee97281311efa4270a42ca21de583c0b3a829835415ac1c15b24403df689"},
        {"green.begin-combat-growth-targets.v3",
         "e818f6ca92730bb4", DeckId::Green,
         "df518a8176ae8942963116058a9e08a7408b6107f72a6671837e5eac3779424c"},
        {"green.bolt-on-bear-response.v3",
         "f21baf227fe0161f", DeckId::Green,
         "02f5077bee388f6b6437a45a7ccceda5a9c3bb9147bd91ad8c1b4c31c46b81fc"},
        {"green.develop-bears.v3",
         "13f0067204d7daf9", DeckId::Green,
         "6a4e8f5fab81f966de2674111c567b96883537cf9e3c93dd4c1805f6c251c9d3"},
        {"green.second-main-growth-options.v3",
         "612358f4590a4f23", DeckId::Green,
         "f7f422d7bad3c9d0abe67f0254801f6163b5832d2ff534f31d6a1f7e0752cd68"},
        {"red.bolt-blocker.v3",
         "59ca1ec298807937", DeckId::Red,
         "396099d98558db7afa4c6bdaa230691269d3e02185e96a8a59d22719cd4f392f"},
        {"red.bolt-face-lethal.v3",
         "c0a25e7c6ad3992f", DeckId::Red,
         "014204bde41dd4a76e8282bb272f3012caaf6bb3a24b59332821521217a1890f"},
        {"red.finish-damaged-air.v3",
         "1478f77ad89ad54a", DeckId::Red,
         "f0f84a58cd4627cab41623ce46d5715b28169a800ceaa164bfcbaeebcdde1af2"},
        {"red.stack-race.v3",
         "249de389db436bf5", DeckId::Red,
         "a8315b402d1594156e5d597852f9e5e4cb7b29aaab3331932ed092df67a982e0"},
        {"blue.counter-fire-elemental.v3",
         "6c90355960714c47", DeckId::Blue,
         "7e683fac8c5f6e6add8f2720a255498d083af0ec91db02cafdafc67cd8840b6d"},
        {"blue.counter-lethal-bolt.v3",
         "30ff11b9ec056b21", DeckId::Blue,
         "e8cfd9e9ab30682f5d002788acbcebe3633b313490cfeaefcb51ce3901c4e076"},
        {"blue.counter-war.v3",
         "fc276ae226a9f512", DeckId::Blue,
         "49f7e5a00e93af415ad2ce78e91079707312fbd93c659a986b9fde94f641fce3"},
        {"control.blue.braingeyser-x0.v1",
         "a68cd5b38da84990", DeckId::Blue,
         "c62588ebe6378ec8a86405f993ae4b43baa9f76fa73b9467ddc8762bffc156cb"},
        {"control.blue.counter-redundant-same-target.v1",
         "faf53e39aba9e69b", DeckId::Blue,
         "bc8111f01e70652d4cadb0c596b95712eecd200e5525e44be4139d39dc238738"},
        {"control.blue.counter-same-target-after-intervening-counter.v1",
         "7a6e19b2b016fb58", DeckId::Blue,
         "103b8900b5903f02437a0ad0af093398ae9f4ee8ddd7ea3f1982c3b2fc842d1f"},
        {"control.blue.force-spike-live-gray-ogre.v1",
         "b792d7434096d2cc", DeckId::Blue,
         "3b40f3d75c7192c5a1e0d1278981dd9acefc6a0542d84c8976d59b30da4c6a30"},
        {"control.blue.force-spike-payable-gray-ogre.v1",
         "8e24d4696a7c2ad5", DeckId::Blue,
         "5dace3436bc53eb64eb8331771a96c14fd0c40d668d37c1f9e714042a521bff1"},
        {"white.avoid-redundant-moat.v3",
         "9057eeb3a051d05a", DeckId::White,
         "d8a6262824b75b091064fdd8bdb127d3a96be1b69c8ba9cdb7153260b1f6eeb0"},
        {"white.emergency-moat.v3",
         "dd622f3af7e12e88", DeckId::White,
         "18e8b000842160cd68ea422e25329ccd51c1303c02dd446f63021a42310d7d03"},
        {"white.establish-millstone.v3",
         "01f521b19b58c5f3", DeckId::White,
         "2360a3cbfbdb30990a43a63705805e27858290109c5d692d53fd05b3ff3ee094"},
        {"white.mill-before-draw.v3",
         "7a7bfb7ff8a0cb56", DeckId::White,
         "cdbdbf97b88a92e435ddabc60320554479b8f361d6af1e3fe9f43b01b49a5370"},
        {"ru.second-main-blocker-development.v3",
         "93137492d8498321", DeckId::RUAggro,
         "1bdfddfd27e32072f5d17528f98c46f720c98aa4689b2e03d0b9dc131724ce70"},
        {"ru.disintegrate-player-x.v3",
         "6345aec096735eb9", DeckId::RUAggro,
         "4997260699a9b3305c353aec3dfcc6d33864a05cf269f9ea56c27f072c671424"},
        {"ru.second-main-land-colors.v3",
         "c99c947e9e9eb992", DeckId::RUAggro,
         "be7e1c4fe8587bc83e5af05d5a054ad42846d0ba68a86f059d06363813cec276"},
        {"validation.ru.disintegrate-hold-x0.v1",
         "04d02e0ea36d34be", DeckId::RUAggro,
         "d6cb73bd54a52bf86489fd4322152e1a67b125f83f2c3359e2a61d090c65135a"},
    }};

constexpr std::array<std::size_t, kDeckCount>
    kExpectedPhysicalByDeck = {6, 4, 8, 4, 4};
constexpr std::array<std::size_t, kDeckCount>
    kExpectedLogicalDevByDeck = {4, 4, 4, 4, 4};

void require(bool condition, std::string message) {
    if (!condition) {
        throw std::runtime_error(std::move(message));
    }
}

bool sorcery_actions_for(TurnPhase phase) {
    return phase == TurnPhase::FirstMain ||
           phase == TurnPhase::SecondMain;
}

std::size_t deck_index(DeckId deck) {
    const std::size_t index = static_cast<std::size_t>(deck);
    if (index >= kDeckCount) {
        throw std::invalid_argument("AC1 root has an invalid deck");
    }
    return index;
}

void require_no_errors(
    const std::vector<std::string>& errors,
    std::string_view corpus) {
    if (errors.empty()) {
        return;
    }
    std::string message =
        std::string(corpus) + " is invalid: " + errors.front();
    throw std::runtime_error(message);
}

std::vector<probes::DecisionProbe> select_field_priority_roots(
    const std::vector<probes::DecisionProbe>& field) {
    std::vector<probes::DecisionProbe> result;
    for (const auto& probe : field) {
        if (probe.stable_id ==
                "field.green.second-main-sick-bear-growth.v1" ||
            probe.stable_id ==
                "field.green.begin-combat-growth-tapped-air.v1") {
            result.push_back(probe);
        }
    }
    require(
        result.size() == 2,
        "AC1 field Priority root census drifted");
    return result;
}

const ExpectedRoot& expected_root(std::string_view stable_id) {
    const auto found = std::find_if(
        kExpectedRoots.begin(), kExpectedRoots.end(),
        [&](const ExpectedRoot& expected) {
            return expected.stable_id == stable_id;
        });
    if (found == kExpectedRoots.end()) {
        throw std::runtime_error(
            std::string(stable_id) +
            ": AC1 manifest contains an unpinned root");
    }
    return *found;
}

const probes::DecisionProbe& probe_for(
    const Manifest& manifest, std::string_view stable_id) {
    const auto found = std::find_if(
        manifest.roots.begin(), manifest.roots.end(),
        [&](const ManifestRoot& root) {
            return root.probe.stable_id == stable_id;
        });
    if (found == manifest.roots.end()) {
        throw std::runtime_error(
            std::string(stable_id) +
            ": AC1 manifest root is missing");
    }
    return found->probe;
}

const RootEvidence& evidence_for(
    const ScientificEvidence& evidence,
    std::string_view stable_id) {
    const auto found = std::find_if(
        evidence.roots.begin(), evidence.roots.end(),
        [&](const RootEvidence& root) {
            return root.stable_id == stable_id;
        });
    if (found == evidence.roots.end()) {
        throw std::runtime_error(
            std::string(stable_id) +
            ": AC1 evidence root is missing");
    }
    return *found;
}

const scoring::DescriptorScore& descriptor_for(
    const scoring::DecisionScore& score,
    std::string_view descriptor) {
    const auto found = std::find_if(
        score.actions.begin(), score.actions.end(),
        [&](const scoring::DescriptorScore& action) {
            return action.descriptor == descriptor;
        });
    if (found == score.actions.end()) {
        throw std::runtime_error(
            score.stable_id + ": missing descriptor " +
            std::string(descriptor));
    }
    return *found;
}

bool support_is_exact(
    const scoring::DecisionScore& score,
    std::string_view descriptor) {
    return score.selected_support ==
           std::vector<std::string>{std::string(descriptor)};
}

bool support_contains(
    const scoring::DecisionScore& score,
    std::string_view descriptor) {
    return std::find(
               score.selected_support.begin(),
               score.selected_support.end(),
               descriptor) != score.selected_support.end();
}

bool bit_identical_double(double first, double second) {
    return std::bit_cast<std::uint64_t>(first) ==
           std::bit_cast<std::uint64_t>(second);
}

std::vector<std::string> bit_exact_max_support_impl(
    const std::vector<scoring::DescriptorScore>& actions) {
    if (actions.empty()) {
        throw std::invalid_argument(
            "AC1 exact support requires at least one action");
    }
    for (std::size_t index = 0;
         index < actions.size(); ++index) {
        if (actions[index].descriptor.empty() ||
            !std::isfinite(actions[index].raw_score) ||
            (index != 0 &&
             actions[index - 1].descriptor >=
                 actions[index].descriptor)) {
            throw std::invalid_argument(
                "AC1 exact support requires finite, unique, "
                "descriptor-canonical actions");
        }
    }
    std::size_t maximum = 0;
    for (std::size_t index = 1;
         index < actions.size(); ++index) {
        if (actions[index].raw_score >
            actions[maximum].raw_score) {
            maximum = index;
        }
    }
    std::vector<std::string> support;
    for (const auto& action : actions) {
        if (bit_identical_double(
                action.raw_score,
                actions[maximum].raw_score)) {
            support.push_back(action.descriptor);
        }
    }
    return support;
}

class ByteWriter {
  public:
    void boolean(bool value) {
        data_.push_back(value ? '\1' : '\0');
    }

    template <typename Integer>
    void integer(Integer value) {
        using Unsigned = std::make_unsigned_t<Integer>;
        const Unsigned converted =
            static_cast<Unsigned>(value);
        for (std::size_t byte = 0; byte < sizeof(Unsigned);
             ++byte) {
            data_.push_back(static_cast<char>(
                static_cast<unsigned char>(
                    converted >> (byte * 8U))));
        }
    }

    void real(double value) {
        integer(std::bit_cast<std::uint64_t>(value));
    }

    void text(std::string_view value) {
        integer<std::uint64_t>(value.size());
        data_.append(value);
    }

    template <typename Value, typename Append>
    void sequence(
        const std::vector<Value>& values, Append append) {
        integer<std::uint64_t>(values.size());
        for (const Value& value : values) {
            append(*this, value);
        }
    }

    const std::string& data() const {
        return data_;
    }

  private:
    std::string data_;
};

void append_card(ByteWriter& writer, CardId card) {
    writer.integer<std::uint64_t>(
        static_cast<std::uint64_t>(card));
}

void append_mana(ByteWriter& writer, const ManaCost& mana) {
    writer.integer(mana.generic);
    writer.integer(mana.green);
    writer.integer(mana.red);
    writer.integer(mana.blue);
    writer.integer(mana.white);
}

void append_target(ByteWriter& writer, const Target& target) {
    writer.integer<std::uint64_t>(target.player);
    writer.boolean(target.creature.has_value());
    if (target.creature.has_value()) {
        writer.integer<std::uint64_t>(*target.creature);
    }
}

void append_public_player(
    ByteWriter& writer, const PublicPlayerState& player) {
    writer.integer(player.life);
    writer.integer<std::uint64_t>(player.library_size);
    writer.integer<std::uint64_t>(player.hand_size);
    const auto append_cards =
        [](ByteWriter& output,
           const std::vector<CardId>& cards) {
            output.sequence(
                cards,
                [](ByteWriter& inner, CardId card) {
                    append_card(inner, card);
                });
        };
    append_cards(writer, player.graveyard);
    append_cards(writer, player.exile);
    writer.sequence(
        player.lands,
        [](ByteWriter& output,
           const LandPermanent& land) {
            append_card(output, land.card);
            output.boolean(land.tapped);
        });
    writer.sequence(
        player.creatures,
        [](ByteWriter& output,
           const CreaturePermanent& creature) {
            output.integer<std::uint64_t>(creature.id);
            append_card(output, creature.card);
            output.boolean(creature.tapped);
            output.boolean(creature.summoning_sick);
            output.integer(creature.damage);
            output.integer(creature.temporary_power_bonus);
            output.integer(
                creature.temporary_toughness_bonus);
            output.boolean(
                creature.exile_on_death_this_turn);
        });
    writer.sequence(
        player.artifacts,
        [](ByteWriter& output,
           const ArtifactPermanent& artifact) {
            output.integer<std::uint64_t>(artifact.id);
            append_card(output, artifact.card);
            output.boolean(artifact.tapped);
        });
    append_cards(writer, player.enchantments);
    append_mana(writer, player.mana_pool);
    writer.boolean(player.land_played_this_turn);
}

void append_stack(
    ByteWriter& writer,
    const std::vector<StackObject>& stack) {
    writer.sequence(
        stack,
        [](ByteWriter& output,
           const StackObject& object) {
            output.integer<std::uint64_t>(
                static_cast<std::uint64_t>(object.kind));
            output.integer<std::uint64_t>(object.id);
            append_card(output, object.card);
            output.integer<std::uint64_t>(
                object.controller);
            output.boolean(object.target.has_value());
            if (object.target.has_value()) {
                append_target(output, *object.target);
            }
            output.boolean(
                object.spell_target.has_value());
            if (object.spell_target.has_value()) {
                output.integer<std::uint64_t>(
                    *object.spell_target);
            }
            output.integer(object.x_value);
        });
}

void append_full_player(
    ByteWriter& writer, const PlayerState& player) {
    writer.integer(player.life);
    const auto append_cards =
        [](ByteWriter& output,
           const std::vector<CardId>& cards) {
            output.sequence(
                cards,
                [](ByteWriter& inner, CardId card) {
                    append_card(inner, card);
                });
        };
    append_cards(writer, player.library);
    append_cards(writer, player.hand);
    append_cards(writer, player.graveyard);
    append_cards(writer, player.exile);
    writer.sequence(
        player.lands,
        [](ByteWriter& output,
           const LandPermanent& land) {
            append_card(output, land.card);
            output.boolean(land.tapped);
        });
    writer.sequence(
        player.creatures,
        [](ByteWriter& output,
           const CreaturePermanent& creature) {
            output.integer<std::uint64_t>(creature.id);
            append_card(output, creature.card);
            output.boolean(creature.tapped);
            output.boolean(creature.summoning_sick);
            output.integer(creature.damage);
            output.integer(creature.temporary_power_bonus);
            output.integer(
                creature.temporary_toughness_bonus);
            output.boolean(
                creature.exile_on_death_this_turn);
        });
    writer.sequence(
        player.artifacts,
        [](ByteWriter& output,
           const ArtifactPermanent& artifact) {
            output.integer<std::uint64_t>(artifact.id);
            append_card(output, artifact.card);
            output.boolean(artifact.tapped);
        });
    append_cards(writer, player.enchantments);
    append_mana(writer, player.mana_pool);
    writer.boolean(player.land_played_this_turn);
}

void append_stats(
    ByteWriter& writer, const PlayerGameStats& stats) {
    writer.integer<std::uint64_t>(stats.cards_drawn);
    writer.integer<std::uint64_t>(stats.lands_played);
    writer.integer<std::uint64_t>(stats.spells_cast);
    writer.integer<std::uint64_t>(stats.spells_countered);
    writer.integer<std::uint64_t>(stats.damage_to_opponent);
    writer.integer<std::uint64_t>(stats.cards_milled);
    writer.integer<std::uint64_t>(stats.decisions);
    writer.integer<std::uint64_t>(
        stats.monte_carlo_rollouts);
}

void append_priority_action(
    ByteWriter& writer, const PriorityAction& action) {
    writer.integer<std::uint64_t>(
        static_cast<std::uint64_t>(action.kind));
    append_card(writer, action.card);
    writer.boolean(action.target.has_value());
    if (action.target.has_value()) {
        append_target(writer, *action.target);
    }
    writer.boolean(action.spell_target.has_value());
    if (action.spell_target.has_value()) {
        writer.integer<std::uint64_t>(
            *action.spell_target);
    }
    writer.boolean(action.source_permanent.has_value());
    if (action.source_permanent.has_value()) {
        writer.integer<std::uint64_t>(
            *action.source_permanent);
    }
    writer.integer(action.x_value);
}

std::string manifest_root_contract_fingerprint_impl(
    const probes::DecisionProbe& probe) {
    ByteWriter writer;
    writer.text("old-school-ac1-t0-manifest-root-v1");
    writer.text(probe.stable_id);
    writer.integer<std::uint64_t>(
        static_cast<std::uint64_t>(probe.category));
    writer.integer<std::uint64_t>(
        static_cast<std::uint64_t>(
            probe.decision_kind));
    writer.integer<std::uint64_t>(
        static_cast<std::uint64_t>(probe.root_deck));
    writer.integer<std::uint64_t>(
        static_cast<std::uint64_t>(
            probe.opponent_deck));
    writer.integer<std::uint64_t>(probe.root_player);
    writer.integer<std::uint64_t>(
        static_cast<std::uint64_t>(probe.phase));
    writer.integer(probe.consecutive_passes);
    for (const PlayerState& player :
         probe.state.players) {
        append_full_player(writer, player);
    }
    for (const PlayerGameStats& stats :
         probe.state.stats) {
        append_stats(writer, stats);
    }
    append_stack(writer, probe.state.stack);
    for (const std::size_t turns :
         probe.state.extra_turns_pending) {
        writer.integer<std::uint64_t>(turns);
    }
    for (const bool failed : probe.state.failed_draw) {
        writer.boolean(failed);
    }
    writer.integer<std::uint64_t>(
        probe.state.active_player);
    writer.integer<std::uint64_t>(
        probe.state.starting_player);
    writer.integer<std::uint64_t>(
        probe.state.turn_number);
    writer.integer<std::uint64_t>(
        probe.state.next_permanent_id);
    writer.integer<std::uint64_t>(
        probe.state.next_stack_object_id);
    for (const auto& deck : probe.original_decks) {
        writer.sequence(
            deck,
            [](ByteWriter& output, CardId card) {
                append_card(output, card);
            });
    }
    writer.integer<std::uint64_t>(
        probe.candidates.size());
    for (const probes::Candidate& candidate :
         probe.candidates) {
        writer.text(candidate.descriptor);
        const auto* action =
            std::get_if<PriorityAction>(
                &candidate.action);
        if (action == nullptr) {
            throw std::invalid_argument(
                probe.stable_id +
                ": AC1 manifest contract expected Priority");
        }
        append_priority_action(writer, *action);
    }
    writer.boolean(probe.harvest.has_value());
    if (probe.harvest.has_value()) {
        writer.text(probe.harvest->collector);
        writer.text(probe.harvest->trajectory_script);
        writer.integer<std::uint64_t>(
            probe.harvest->game_seed);
        writer.integer<std::uint64_t>(
            probe.harvest->starting_player);
        writer.integer<std::uint64_t>(
            probe.harvest->priority_decision_ordinal);
        writer.integer<std::uint64_t>(
            probe.harvest->turn_number);
        writer.integer<std::uint64_t>(
            static_cast<std::uint64_t>(
                probe.harvest->phase));
    }
    return artifact_integrity::sha256_string(
        writer.data());
}

std::vector<PriorityAction> priority_actions(
    const probes::DecisionProbe& probe) {
    std::vector<PriorityAction> actions;
    actions.reserve(probe.candidates.size());
    for (const auto& candidate : probe.candidates) {
        const auto* action =
            std::get_if<PriorityAction>(&candidate.action);
        if (action == nullptr) {
            throw std::invalid_argument(
                probe.stable_id +
                ": AC1 root contains a non-Priority action");
        }
        actions.push_back(*action);
    }
    return actions;
}

scoring::AppliedRecipe applied_recipe(
    const scoring::SearchRecipe& recipe,
    std::uint64_t seed) {
    return {
        .seed_source = scoring::SeedSource::Derived,
        .seed_tag = std::string(recipe.seed_tag),
        .seed_base = recipe.seed_base,
        .resolved_seed = seed,
        .worlds = recipe.worlds,
        .horizon_turns = recipe.horizon_turns,
        .rollouts_per_world = recipe.rollouts_per_world,
        .blend_shallow_prior = recipe.blend_shallow_prior,
        .evaluation_threads = recipe.evaluation_threads,
        .value_mirror = true,
        .value_continuation_epsilon = 0.0,
        .value_priority_residual_weight = 0.0,
        .value_pass_dominance = false,
        .value_continuation_controller =
            LearnedContinuationController::Legacy,
    };
}

CapturedScore score_priority_root_impl(
    const probes::DecisionProbe& source,
    const std::shared_ptr<const LearnedModel>& model,
    const scoring::SearchRecipe& recipe) {
    if (model == nullptr || recipe.seed_tag.empty() ||
        recipe.worlds == 0 ||
        recipe.rollouts_per_world == 0 ||
        recipe.horizon_turns != 0 ||
        recipe.blend_shallow_prior ||
        recipe.evaluation_threads == 0) {
        throw std::invalid_argument(
            "AC1 scorer requires a nonnull model and an "
            "unblended H0 recipe");
    }
    const probes::Validation validation =
        probes::validate_probe(source);
    if (!validation.candidates_legal_and_complete ||
        source.decision_kind !=
            probes::DecisionKind::Priority) {
        throw std::invalid_argument(
            source.stable_id +
            ": AC1 requires one complete Priority root");
    }
    // Preserve caller order through the engine call so the reverse-input
    // invariant exercises the sampler rather than being normalized away.
    // Only the returned evidence rows are descriptor-canonicalized.
    const probes::DecisionProbe& probe = source;
    const std::uint64_t seed =
        probe_runner::reference_seed_for_probe(
            recipe.seed_tag, probe.stable_id,
            recipe.seed_base);
    const LearnedSearchConfig config{
        .seed = seed,
        .worlds = recipe.worlds,
        .rollouts_per_world =
            recipe.rollouts_per_world,
        .horizon_turns = 0,
        .continuation_variant =
            LearnedVariant::ValueSearchChampion,
        .value_continuation_epsilon = 0.0,
        .blend_shallow_prior = false,
        .value_priority_residual_weight = 0.0,
        .value_pass_dominance = false,
        .value_continuation_controller =
            LearnedContinuationController::Legacy,
        .evaluation_threads =
            recipe.evaluation_threads,
        .capture_priority_h0_boundaries = true,
    };
    const LearnedActionSamples samples =
        learned_priority_action_samples(
            probe.state, probe.original_decks,
            probe.root_player,
            sorcery_actions_for(probe.phase), probe.phase,
            probe.consecutive_passes,
            priority_actions(probe), model, config);
    const std::size_t row_width =
        recipe.worlds * recipe.rollouts_per_world;
    const std::size_t expected_evaluations =
        probe.candidates.size() * row_width;
    require(
        samples.sampled_worlds == recipe.worlds &&
            samples.rollout_evaluations ==
                expected_evaluations &&
            samples.terminal_evaluations +
                    samples.bootstrapped_evaluations ==
                expected_evaluations &&
            samples.q_samples.size() ==
                probe.candidates.size() &&
            samples.exact_priority_aggregate_scores.size() ==
                probe.candidates.size() &&
            samples.priority_h0_boundaries.size() ==
                probe.candidates.size(),
        probe.stable_id +
            ": AC1 H0 sample accounting is incomplete");

    CapturedScore result;
    result.decision = {
        .stable_id = probe.stable_id,
        .decision_kind =
            probes::DecisionKind::Priority,
        .score_mode =
            scoring::ScoreMode::ReferenceSearch,
        .recipe = applied_recipe(recipe, seed),
        .actions = {},
        .selected_support = {},
        .deterministic_selection = false,
        .accounting = {
            .sampled_worlds = samples.sampled_worlds,
            .rollout_evaluations =
                samples.rollout_evaluations,
            .terminal_evaluations =
                samples.terminal_evaluations,
            .bootstrapped_evaluations =
                samples.bootstrapped_evaluations,
        },
    };
    result.decision.actions.reserve(
        probe.candidates.size());
    result.h0_public_consequence_hashes.reserve(
        probe.candidates.size());
    result.h0_boundaries.reserve(
        probe.candidates.size());
    std::vector<std::size_t> canonical_indices(
        probe.candidates.size());
    std::iota(
        canonical_indices.begin(),
        canonical_indices.end(), 0);
    std::sort(
        canonical_indices.begin(),
        canonical_indices.end(),
        [&](std::size_t left, std::size_t right) {
            return probe.candidates[left].descriptor <
                   probe.candidates[right].descriptor;
        });
    for (const std::size_t index : canonical_indices) {
        const auto& row = samples.q_samples[index];
        const auto& boundaries =
            samples.priority_h0_boundaries[index];
        require(
            row.size() == row_width &&
                boundaries.size() == row_width,
            probe.stable_id +
                ": AC1 H0 row width is wrong");
        const double aggregate =
            samples.exact_priority_aggregate_scores[index];
        require(
            std::isfinite(aggregate),
            probe.stable_id +
                ": AC1 aggregate is non-finite");
        std::vector<std::string> hashes;
        hashes.reserve(row_width);
        for (std::size_t sample = 0;
             sample < row_width; ++sample) {
            require(
                std::isfinite(row[sample]) &&
                    std::isfinite(
                        boundaries[sample]
                            .continuation_score) &&
                    bit_identical_double(
                        row[sample],
                        boundaries[sample]
                            .continuation_score),
                probe.stable_id +
                    ": AC1 raw score/boundary score drifted");
            hashes.push_back(
                    testing::h0_public_consequence_hash(
                        boundaries[sample],
                        probe.root_player));
        }
        result.h0_public_consequence_hashes.push_back(
            std::move(hashes));
        result.h0_boundaries.push_back(boundaries);
        result.decision.actions.push_back({
            .descriptor =
                probe.candidates[index].descriptor,
            .raw_samples = row,
            .raw_score = aggregate,
        });
    }
    result.decision.selected_support =
        bit_exact_max_support_impl(
            result.decision.actions);
    return result;
}

CapturedScore scientific_projection(CapturedScore score) {
    score.h0_boundaries.clear();
    return score;
}

bool captured_score_bit_identical_impl(
    const CapturedScore& first,
    const CapturedScore& second) {
    if (!scoring::bit_identical(
            first.decision, second.decision) ||
        first.h0_public_consequence_hashes !=
            second.h0_public_consequence_hashes ||
        first.h0_boundaries.size() !=
            second.h0_boundaries.size()) {
        return false;
    }
    for (std::size_t action = 0;
         action < first.h0_boundaries.size(); ++action) {
        if (first.h0_boundaries[action].size() !=
            second.h0_boundaries[action].size()) {
            return false;
        }
        for (std::size_t sample = 0;
             sample < first.h0_boundaries[action].size();
             ++sample) {
            const auto& left =
                first.h0_boundaries[action][sample];
            const auto& right =
                second.h0_boundaries[action][sample];
            if (left.state != right.state ||
                left.context != right.context ||
                left.terminal != right.terminal ||
                !bit_identical_double(
                    left.continuation_score,
                    right.continuation_score)) {
                return false;
            }
        }
    }
    return true;
}

bool contrast_bit_identical(
    const PairedContrast& first,
    const PairedContrast& second) {
    return first.name == second.name &&
           first.positive_descriptor ==
               second.positive_descriptor &&
           first.negative_descriptor ==
               second.negative_descriptor &&
           bit_identical_double(first.mean, second.mean) &&
           bit_identical_double(
               first.standard_error,
               second.standard_error) &&
           bit_identical_double(
               first.lower_95, second.lower_95) &&
           first.positive_blocks ==
               second.positive_blocks &&
           first.blocks == second.blocks &&
           first.samples == second.samples &&
           first.complete == second.complete &&
           first.passed == second.passed;
}

bool model_evidence_bit_identical(
    const ModelRootEvidence& first,
    const ModelRootEvidence& second) {
    return first.stable_id == second.stable_id &&
           first.information_action_fingerprint ==
               second.information_action_fingerprint &&
           first.root_deck == second.root_deck &&
           captured_score_bit_identical_impl(
               first.score, second.score) &&
           first.descriptor_order_invariant ==
               second.descriptor_order_invariant &&
           first.complete == second.complete;
}

bool root_evidence_bit_identical(
    const RootEvidence& first,
    const RootEvidence& second) {
    return first.stable_id == second.stable_id &&
           first.information_action_fingerprint ==
               second.information_action_fingerprint &&
           first.root_deck == second.root_deck &&
           first.from_dev_v3 == second.from_dev_v3 &&
           first.hidden_identity_changed ==
               second.hidden_identity_changed &&
           model_evidence_bit_identical(
               first.c16, second.c16) &&
           model_evidence_bit_identical(
               first.oc1, second.oc1) &&
           first.hidden_scores_bit_identical ==
               second.hidden_scores_bit_identical &&
           first.hidden_consequence_hashes_bit_identical ==
               second
                   .hidden_consequence_hashes_bit_identical &&
           first.hidden_bit_identical ==
               second.hidden_bit_identical &&
           first.complete == second.complete;
}

bool scientific_evidence_bit_identical_impl(
    const ScientificEvidence& first,
    const ScientificEvidence& second) {
    if (first.parent_model_fingerprint !=
            second.parent_model_fingerprint ||
        first.candidate_model_fingerprint !=
            second.candidate_model_fingerprint ||
        first.manifest != second.manifest ||
        first.roots.size() != second.roots.size() ||
        first.passed_primary_contrasts !=
            second.passed_primary_contrasts ||
        first.passed_support_controls !=
            second.passed_support_controls ||
        first.required_support_controls !=
            second.required_support_controls ||
        first.support_controls_passed !=
            second.support_controls_passed ||
        first.complete != second.complete ||
        first.passed != second.passed) {
        return false;
    }
    for (std::size_t index = 0;
         index < first.roots.size(); ++index) {
        if (!root_evidence_bit_identical(
                first.roots[index],
                second.roots[index])) {
            return false;
        }
    }
    for (std::size_t index = 0;
         index < first.c16_primary_contrasts.size();
         ++index) {
        if (!contrast_bit_identical(
                first.c16_primary_contrasts[index],
                second.c16_primary_contrasts[index]) ||
            !contrast_bit_identical(
                first.oc1_primary_contrasts[index],
                second.oc1_primary_contrasts[index])) {
            return false;
        }
    }
    return true;
}

bool opponent_hand_histogram_changed(
    const GameState& first, const GameState& second,
    std::size_t observer) {
    if (observer >= first.players.size()) {
        return false;
    }
    const std::size_t opponent = 1 - observer;
    std::array<std::size_t, kCardCount> first_counts{};
    std::array<std::size_t, kCardCount> second_counts{};
    for (const CardId card : first.players[opponent].hand) {
        ++first_counts[static_cast<std::size_t>(card)];
    }
    for (const CardId card : second.players[opponent].hand) {
        ++second_counts[static_cast<std::size_t>(card)];
    }
    return first_counts != second_counts &&
           first.players[opponent].hand.size() ==
               second.players[opponent].hand.size();
}

bool hidden_root_identity_valid(
    const probes::DecisionProbe& original,
    const probes::DecisionProbe& hidden) {
    return original.stable_id == hidden.stable_id &&
           original.candidates == hidden.candidates &&
           original.original_decks ==
               hidden.original_decks &&
           observe_game_state(
               original.state, original.root_player) ==
               observe_game_state(
                   hidden.state, hidden.root_player) &&
           probes::bsr_information_action_fingerprint(
               original) ==
               probes::bsr_information_action_fingerprint(
                   hidden) &&
           probes::validate_probe(hidden)
               .candidates_legal_and_complete;
}

bool same_physical_root_ignoring_stable_id(
    const probes::DecisionProbe& first,
    const probes::DecisionProbe& second) {
    return first.decision_kind == second.decision_kind &&
           first.root_deck == second.root_deck &&
           first.opponent_deck == second.opponent_deck &&
           first.root_player == second.root_player &&
           first.phase == second.phase &&
           first.consecutive_passes ==
               second.consecutive_passes &&
           first.state == second.state &&
           first.original_decks == second.original_decks &&
           first.candidates == second.candidates &&
           first.harvest == second.harvest;
}

ModelRootEvidence model_evidence(
    const ManifestRoot& root,
    const probes::DecisionProbe& hidden,
    const std::shared_ptr<const LearnedModel>& model,
    bool& hidden_scores_identical,
    bool& hidden_consequences_identical) {
    CapturedScore original =
        score_priority_root_impl(
            root.probe, model, kSearchRecipe);
    CapturedScore hidden_score =
        score_priority_root_impl(
            hidden, model, kSearchRecipe);
    probes::DecisionProbe reversed = root.probe;
    std::reverse(
        reversed.candidates.begin(),
        reversed.candidates.end());
    CapturedScore reversed_score =
        score_priority_root_impl(
            reversed, model, kSearchRecipe);
    hidden_scores_identical =
        scoring::bit_identical(
            original.decision, hidden_score.decision);
    hidden_consequences_identical =
        original.h0_public_consequence_hashes ==
        hidden_score.h0_public_consequence_hashes;
    const bool reverse_identical =
        testing::captured_score_bit_identical(
            original, reversed_score);
    const bool complete =
        original.decision.accounting.sampled_worlds ==
            kWorlds &&
        original.decision.accounting.rollout_evaluations ==
            original.decision.actions.size() *
                kSamplesPerAction &&
        std::all_of(
            original.decision.actions.begin(),
            original.decision.actions.end(),
            [](const scoring::DescriptorScore& action) {
                return action.raw_samples.size() ==
                           kSamplesPerAction &&
                       std::isfinite(action.raw_score) &&
                       std::all_of(
                           action.raw_samples.begin(),
                           action.raw_samples.end(),
                           [](double sample) {
                               return std::isfinite(sample);
                           });
            });
    return {
        .stable_id = root.probe.stable_id,
        .information_action_fingerprint =
            root.information_action_fingerprint,
        .root_deck = root.probe.root_deck,
        .score =
            scientific_projection(std::move(original)),
        .descriptor_order_invariant =
            reverse_identical,
        .complete = complete && reverse_identical,
    };
}

struct Construction {
    ScientificEvidence evidence;
    HiddenAudit hidden;
};

void evaluate_support_controls(
    ScientificEvidence& evidence) {
    std::size_t passed = 0;
    std::size_t required = 0;
    const auto exact =
        [&](std::string_view stable_id,
            std::string_view descriptor) {
            ++required;
            if (support_is_exact(
                    evidence_for(evidence, stable_id)
                        .oc1.score.decision,
                    descriptor)) {
                ++passed;
            }
        };

    // The live control is also the exact dev-v3 Force Spike alias. Count the
    // physical support once while preserving both logical obligations.
    exact(
        kCanonicalLiveForceSpike,
        "force-spike-gray-ogre");
    exact(
        "blue.counter-fire-elemental.v3",
        "counter-fire-elemental");
    exact(
        "blue.counter-lethal-bolt.v3",
        "counter-lethal-lightning-bolt");
    exact(
        "blue.counter-war.v3",
        "counter-opponent-counterspell");
    exact(
        "control.blue.counter-redundant-same-target.v1",
        "pass");
    exact(
        "field.green.second-main-sick-bear-growth.v1",
        "pass");

    const auto no_x_zero =
        [&](std::string_view stable_id) {
            ++required;
            const RootEvidence& root =
                evidence_for(evidence, stable_id);
            const probes::DecisionProbe& probe =
                probe_for(evidence.manifest, stable_id);
            if (testing::support_excludes_x_zero(
                    probe, root.oc1.score.decision)) {
                ++passed;
            }
        };
    no_x_zero("control.blue.braingeyser-x0.v1");
    no_x_zero("validation.ru.disintegrate-hold-x0.v1");

    evidence.passed_support_controls = passed;
    evidence.required_support_controls = required;
    evidence.support_controls_passed =
        required == 8 && passed == required;
}

void evaluate_primary_contrasts(
    ScientificEvidence& evidence) {
    const auto model_contrasts =
        [&](bool candidate) {
            const auto contrast =
                [&](std::string name,
                    std::string_view root,
                    std::string_view positive,
                    std::string_view negative) {
                    const RootEvidence& root_evidence =
                        evidence_for(evidence, root);
                    const auto& score =
                        candidate
                            ? root_evidence.oc1.score.decision
                            : root_evidence.c16.score.decision;
                    return paired_contrast(
                        std::move(name),
                        descriptor_for(score, positive),
                        descriptor_for(score, negative));
                };
            return std::array<PairedContrast, 3>{
                contrast(
                    "live-force-spike-minus-pass",
                    kCanonicalLiveForceSpike,
                    "force-spike-gray-ogre", "pass"),
                contrast(
                    "pass-minus-payable-force-spike",
                    "control.blue.force-spike-payable-gray-ogre.v1",
                    "pass", "force-spike-gray-ogre"),
                contrast(
                    "growth-treefolk-minus-opponent-air",
                    "field.green.begin-combat-growth-tapped-air.v1",
                    "growth-own-ironroot-treefolk",
                    "growth-opponent-tapped-air-elemental"),
            };
        };
    evidence.c16_primary_contrasts =
        model_contrasts(false);
    evidence.oc1_primary_contrasts =
        model_contrasts(true);
    auto& growth = evidence.oc1_primary_contrasts[2];
    growth.passed =
        growth.passed &&
        !support_contains(
            evidence_for(
                evidence,
                "field.green.begin-combat-growth-tapped-air.v1")
                .oc1.score.decision,
            "growth-opponent-tapped-air-elemental");
    evidence.passed_primary_contrasts =
        static_cast<std::size_t>(std::count_if(
            evidence.oc1_primary_contrasts.begin(),
            evidence.oc1_primary_contrasts.end(),
            [](const PairedContrast& value) {
                return value.passed;
            }));
}

Construction construct(
    const Manifest& manifest,
    const std::shared_ptr<const LearnedModel>& parent,
    const std::shared_ptr<const LearnedModel>& candidate) {
    Construction construction;
    construction.evidence.parent_model_fingerprint =
        learned_model_fingerprint(parent);
    construction.evidence.candidate_model_fingerprint =
        learned_model_fingerprint(candidate);
    construction.evidence.manifest = manifest;
    construction.evidence.roots.reserve(
        manifest.roots.size());

    bool all_hidden_scores_identical = true;
    bool all_hidden_consequences_identical = true;
    for (const ManifestRoot& root : manifest.roots) {
        probes::DecisionProbe hidden = root.probe;
        hidden.state =
            probe_runner::hidden_repartition_clone(
                root.probe);
        const bool changed =
            opponent_hand_histogram_changed(
                root.probe.state, hidden.state,
                root.probe.root_player);
        require(
            hidden_root_identity_valid(root.probe, hidden),
            root.probe.stable_id +
                ": AC1 hidden clone changed owner-visible identity");

        bool c16_hidden_scores_identical = false;
        bool c16_hidden_consequences_identical = false;
        bool oc1_hidden_scores_identical = false;
        bool oc1_hidden_consequences_identical = false;
        RootEvidence evidence{
            .stable_id = root.probe.stable_id,
            .information_action_fingerprint =
                root.information_action_fingerprint,
            .root_deck = root.probe.root_deck,
            .from_dev_v3 = root.from_dev_v3,
            .hidden_identity_changed = changed,
            .c16 = model_evidence(
                root, hidden, parent,
                c16_hidden_scores_identical,
                c16_hidden_consequences_identical),
            .oc1 = model_evidence(
                root, hidden, candidate,
                oc1_hidden_scores_identical,
                oc1_hidden_consequences_identical),
        };
        evidence.hidden_scores_bit_identical =
            c16_hidden_scores_identical &&
            oc1_hidden_scores_identical;
        evidence.hidden_consequence_hashes_bit_identical =
            c16_hidden_consequences_identical &&
            oc1_hidden_consequences_identical;
        evidence.hidden_bit_identical =
            evidence.hidden_scores_bit_identical &&
            evidence
                .hidden_consequence_hashes_bit_identical;
        evidence.complete =
            evidence.c16.complete &&
            evidence.oc1.complete &&
            evidence.hidden_bit_identical;
        ++construction.hidden.attempted;
        if (changed) {
            ++construction.hidden.changed;
            ++construction.hidden.changed_roots_by_deck[
                deck_index(root.probe.root_deck)];
        }
        all_hidden_scores_identical =
            all_hidden_scores_identical &&
            evidence.hidden_scores_bit_identical;
        all_hidden_consequences_identical =
            all_hidden_consequences_identical &&
            evidence
                .hidden_consequence_hashes_bit_identical;
        construction.evidence.roots.push_back(
            std::move(evidence));
    }
    std::sort(
        construction.evidence.roots.begin(),
        construction.evidence.roots.end(),
        [](const RootEvidence& left,
           const RootEvidence& right) {
            return left.stable_id < right.stable_id;
        });
    construction.hidden.nonvacuous_all_decks =
        std::all_of(
            construction.hidden.changed_roots_by_deck.begin(),
            construction.hidden.changed_roots_by_deck.end(),
            [](std::size_t count) {
                return count > 0;
            });
    construction.hidden.scores_bit_identical =
        all_hidden_scores_identical;
    construction.hidden.consequence_hashes_bit_identical =
        all_hidden_consequences_identical;
    construction.hidden.passed =
        construction.hidden.attempted ==
            kPhysicalPriorityRoots &&
        construction.hidden.nonvacuous_all_decks &&
        construction.hidden.scores_bit_identical &&
        construction.hidden
            .consequence_hashes_bit_identical;

    evaluate_primary_contrasts(
        construction.evidence);
    evaluate_support_controls(construction.evidence);
    construction.evidence.complete =
        construction.evidence.roots.size() ==
            kPhysicalPriorityRoots &&
        std::all_of(
            construction.evidence.roots.begin(),
            construction.evidence.roots.end(),
            [](const RootEvidence& root) {
                return root.complete;
            }) &&
        std::all_of(
            construction.evidence.c16_primary_contrasts.begin(),
            construction.evidence.c16_primary_contrasts.end(),
            [](const PairedContrast& contrast) {
                return contrast.complete;
            }) &&
        std::all_of(
            construction.evidence.oc1_primary_contrasts.begin(),
            construction.evidence.oc1_primary_contrasts.end(),
            [](const PairedContrast& contrast) {
                return contrast.complete;
            });
    construction.evidence.passed =
        construction.evidence.complete &&
        construction.evidence.passed_primary_contrasts == 3 &&
        construction.evidence.support_controls_passed;
    return construction;
}

artifact_integrity::RegularFileSnapshot snapshot_parent() {
    return artifact_integrity::snapshot_regular_file(
        std::string(ar1::kParentArtifactPath));
}

artifact_integrity::RegularFileSnapshot snapshot_candidate() {
    return artifact_integrity::snapshot_regular_file(
        std::string(ar1::kCandidateArtifactPath));
}

bool snapshot_matches(
    const artifact_integrity::RegularFileSnapshot& snapshot,
    std::string_view path, std::uintmax_t bytes,
    std::string_view sha256) {
    std::filesystem::path expected(path);
    if (!expected.is_absolute()) {
        expected = std::filesystem::absolute(expected);
    }
    return snapshot.byte_size == bytes &&
           snapshot.sha256 == sha256 &&
           std::filesystem::path(snapshot.path)
                   .lexically_normal() ==
               expected.lexically_normal();
}

std::pair<
    std::shared_ptr<const LearnedModel>,
    std::shared_ptr<const LearnedModel>>
load_models() {
    const auto parent_artifact =
        load_learned_value_challenger_artifact(
            std::string(ar1::kParentArtifactPath),
            ar1::kParentTrainingGames,
            ar1::kParentTrainingSeed,
            ar1::kParentGenerations);
    const calibration::ParentArtifactIdentity requirement{
        .byte_size = ar1::kParentArtifactBytes,
        .sha256 =
            std::string(ar1::kParentArtifactSha256),
        .model_fingerprint =
            std::string(ar1::kParentModelFingerprint),
        .training_games =
            ar1::kParentTrainingGames,
        .training_seed =
            ar1::kParentTrainingSeed,
        .generations = ar1::kParentGenerations,
    };
    const auto verified =
        calibration::verify_output_calibration_parent(
            std::string(ar1::kParentArtifactPath),
            parent_artifact.model(), requirement);
    const auto candidate =
        calibration::load_output_calibration_artifact(
            std::string(ar1::kCandidateArtifactPath),
            verified, calibration::canonical_fit_config(),
            {});
    require(
        learned_model_fingerprint(verified.model()) ==
                ar1::kParentModelFingerprint &&
            learned_model_fingerprint(candidate.model()) ==
                ar1::kCandidateModelFingerprint &&
            candidate.report().candidate_fingerprint ==
                ar1::kCandidateModelFingerprint &&
            candidate.report().parent == requirement,
        "AC1 model identity verification failed");
    return {verified.model(), candidate.model()};
}

std::string bool_text(bool value) {
    return value ? "1" : "0";
}

std::string double_bits(double value) {
    std::ostringstream output;
    output << std::hex << std::setfill('0')
           << std::setw(16)
           << std::bit_cast<std::uint64_t>(value);
    return output.str();
}

void append_tsv_row(
    std::string& output,
    std::initializer_list<std::string> fields) {
    bool first = true;
    for (const std::string& field : fields) {
        if (!first) {
            output.push_back('\t');
        }
        first = false;
        output += audit_common::sanitize_tsv(field);
    }
    output.push_back('\n');
}

void append_snapshot_rows(
    std::string& output, std::string_view label,
    std::string_view canonical_path,
    const artifact_integrity::RegularFileSnapshot& snapshot) {
    append_tsv_row(
        output,
        {"snapshot", std::string(label), "path",
         std::string(canonical_path)});
    append_tsv_row(
        output,
        {"snapshot", std::string(label), "bytes",
         std::to_string(snapshot.byte_size)});
    append_tsv_row(
        output,
        {"snapshot", std::string(label), "sha256",
         snapshot.sha256});
}

std::string metadata_section(const RunReport& report) {
    std::string output;
    append_tsv_row(
        output,
        {"identity", "parent_model_fingerprint",
         report.scientific.parent_model_fingerprint});
    append_tsv_row(
        output,
        {"identity", "candidate_model_fingerprint",
         report.scientific.candidate_model_fingerprint});
    append_tsv_row(
        output,
        {"artifact", "parent", "path",
         std::string(ar1::kParentArtifactPath)});
    append_tsv_row(
        output,
        {"artifact", "parent", "bytes",
         std::to_string(ar1::kParentArtifactBytes)});
    append_tsv_row(
        output,
        {"artifact", "parent", "sha256",
         std::string(ar1::kParentArtifactSha256)});
    append_tsv_row(
        output,
        {"artifact", "candidate", "path",
         std::string(ar1::kCandidateArtifactPath)});
    append_tsv_row(
        output,
        {"artifact", "candidate", "bytes",
         std::to_string(ar1::kCandidateArtifactBytes)});
    append_tsv_row(
        output,
        {"artifact", "candidate", "sha256",
         std::string(ar1::kCandidateArtifactSha256)});
    append_tsv_row(
        output,
        {"recipe", "seed_tag", std::string(kSeedTag)});
    append_tsv_row(
        output,
        {"recipe", "seed_base",
         std::to_string(kSeedBase)});
    append_tsv_row(
        output,
        {"recipe", "worlds", std::to_string(kWorlds)});
    append_tsv_row(
        output,
        {"recipe", "rollouts_per_world",
         std::to_string(kRolloutsPerWorld)});
    append_tsv_row(
        output,
        {"recipe", "horizon_turns",
         std::to_string(kHorizonTurns)});
    append_tsv_row(
        output,
        {"recipe", "threads",
         std::to_string(kEvaluationThreads)});
    append_tsv_row(
        output,
        {"recipe", "blend_shallow_prior", "0"});
    append_tsv_row(
        output,
        {"recipe", "continuation", "ValueSearchChampion"});
    append_tsv_row(
        output,
        {"recipe", "value_mirror", "1"});
    append_tsv_row(
        output,
        {"recipe", "value_continuation_epsilon_bits",
         double_bits(0.0)});
    append_tsv_row(
        output,
        {"recipe", "value_priority_residual_weight_bits",
         double_bits(0.0)});
    append_tsv_row(
        output,
        {"recipe", "value_pass_dominance", "0"});
    append_tsv_row(
        output,
        {"recipe", "continuation_controller", "Legacy"});
    append_tsv_row(
        output,
        {"statistics", "normal_critical_value_bits",
         double_bits(kNormal95CriticalValue)});
    append_tsv_row(
        output,
        {"statistics", "blocks",
         std::to_string(kBlocks)});
    append_tsv_row(
        output,
        {"statistics", "samples_per_block",
         std::to_string(kSamplesPerBlock)});
    append_tsv_row(
        output,
        {"statistics", "minimum_positive_blocks",
         std::to_string(kMinimumPositiveBlocks)});
    append_tsv_row(
        output,
        {"verdict", "exit_code",
         std::to_string(exit_code(report.gate))});
    append_tsv_row(
        output,
        {"verdict", "passed",
         bool_text(report.gate.passed)});
    return output;
}

std::string manifest_section(const RunReport& report) {
    const Manifest& manifest = report.scientific.manifest;
    std::string output;
    append_tsv_row(
        output,
        {"census", "physical_priority_roots",
         std::to_string(manifest.roots.size())});
    append_tsv_row(
        output,
        {"census", "logical_priority_ids",
         std::to_string(manifest.logical_priority_ids)});
    append_tsv_row(
        output,
        {"alias", manifest.dev_force_spike_alias,
         manifest.canonical_live_force_spike});
    append_tsv_row(
        output,
        {"attack_census", manifest.attack_stable_id,
         manifest.attack_information_action_fingerprint});
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        append_tsv_row(
            output,
            {"deck_census", std::to_string(deck),
             std::to_string(
                 manifest.physical_roots_by_deck[deck]),
             std::to_string(
                 manifest.logical_dev_roots_by_deck[deck])});
    }
    for (const ManifestRoot& root : manifest.roots) {
        append_tsv_row(
            output,
            {"root", root.probe.stable_id,
             std::to_string(
                 static_cast<std::size_t>(
                     root.probe.root_deck)),
             root.information_action_fingerprint,
             root.factory_contract_fingerprint,
             bool_text(root.from_dev_v3)});
    }
    return output;
}

void append_model_score_rows(
    std::string& output, std::string_view stable_id,
    std::string_view model_name,
    const ModelRootEvidence& model) {
    const auto& score = model.score.decision;
    append_tsv_row(
        output,
        {"model_root", std::string(stable_id),
         std::string(model_name), "resolved_seed",
         score.recipe.resolved_seed.has_value()
             ? std::to_string(*score.recipe.resolved_seed)
             : ""});
    append_tsv_row(
        output,
        {"accounting", std::string(stable_id),
         std::string(model_name),
         std::to_string(score.accounting.sampled_worlds),
         std::to_string(
             score.accounting.rollout_evaluations),
         std::to_string(
             score.accounting.terminal_evaluations),
         std::to_string(
             score.accounting.bootstrapped_evaluations)});
    append_tsv_row(
        output,
        {"model_flags", std::string(stable_id),
         std::string(model_name),
         bool_text(model.descriptor_order_invariant),
         bool_text(model.complete)});
    for (const std::string& descriptor :
         score.selected_support) {
        append_tsv_row(
            output,
            {"support", std::string(stable_id),
             std::string(model_name), descriptor});
    }
    for (std::size_t action = 0;
         action < score.actions.size(); ++action) {
        const auto& descriptor = score.actions[action];
        const auto& hashes =
            model.score.h0_public_consequence_hashes[action];
        append_tsv_row(
            output,
            {"action", std::string(stable_id),
             std::string(model_name),
             descriptor.descriptor,
             double_bits(descriptor.raw_score),
             std::to_string(descriptor.raw_samples.size())});
        for (std::size_t sample = 0;
             sample < descriptor.raw_samples.size();
             ++sample) {
            append_tsv_row(
                output,
                {"sample", std::string(stable_id),
                 std::string(model_name),
                 descriptor.descriptor,
                 std::to_string(sample),
                 double_bits(
                     descriptor.raw_samples[sample]),
                 hashes[sample]});
        }
    }
}

std::string scores_section(const RunReport& report) {
    std::string output;
    for (const RootEvidence& root :
         report.scientific.roots) {
        append_tsv_row(
            output,
            {"root_flags", root.stable_id,
             bool_text(root.hidden_identity_changed),
             bool_text(root.hidden_scores_bit_identical),
             bool_text(
                 root
                     .hidden_consequence_hashes_bit_identical),
             bool_text(root.complete)});
        append_model_score_rows(
            output, root.stable_id, "C16", root.c16);
        append_model_score_rows(
            output, root.stable_id, "OC1", root.oc1);
    }
    const RootEvidence& canonical =
        evidence_for(
            report.scientific,
            kCanonicalLiveForceSpike);
    append_tsv_row(
        output,
        {"logical_alias_reuse",
         std::string(kDevForceSpikeAlias),
         std::string(kCanonicalLiveForceSpike),
         canonical.information_action_fingerprint});
    return output;
}

void append_contrast_rows(
    std::string& output, std::string_view model,
    const std::array<PairedContrast, 3>& contrasts) {
    for (const PairedContrast& contrast : contrasts) {
        append_tsv_row(
            output,
            {"contrast", std::string(model), contrast.name,
             contrast.positive_descriptor,
             contrast.negative_descriptor,
             double_bits(contrast.mean),
             double_bits(contrast.standard_error),
             double_bits(contrast.lower_95),
             std::to_string(contrast.positive_blocks),
             std::to_string(contrast.blocks),
             std::to_string(contrast.samples),
             bool_text(contrast.complete),
             bool_text(contrast.passed)});
    }
}

std::string contrasts_section(const RunReport& report) {
    std::string output;
    append_contrast_rows(
        output, "C16",
        report.scientific.c16_primary_contrasts);
    append_contrast_rows(
        output, "OC1",
        report.scientific.oc1_primary_contrasts);
    append_tsv_row(
        output,
        {"support_controls",
         std::to_string(
             report.scientific.passed_support_controls),
         std::to_string(
             report.scientific.required_support_controls),
         bool_text(
             report.scientific.support_controls_passed)});
    return output;
}

std::string integrity_section(const RunReport& report) {
    std::string output;
    append_snapshot_rows(
        output, "parent_before",
        ar1::kParentArtifactPath,
        report.integrity.parent_before);
    append_snapshot_rows(
        output, "parent_after",
        ar1::kParentArtifactPath,
        report.integrity.parent_after);
    append_snapshot_rows(
        output, "candidate_before",
        ar1::kCandidateArtifactPath,
        report.integrity.candidate_before);
    append_snapshot_rows(
        output, "candidate_after",
        ar1::kCandidateArtifactPath,
        report.integrity.candidate_after);
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        append_tsv_row(
            output,
            {"hidden_changed_by_deck",
             std::to_string(deck),
             std::to_string(
                 report.integrity.hidden
                     .changed_roots_by_deck[deck])});
    }
    append_tsv_row(
        output,
        {"hidden", "attempted",
         std::to_string(report.integrity.hidden.attempted)});
    append_tsv_row(
        output,
        {"hidden", "changed",
         std::to_string(report.integrity.hidden.changed)});
    append_tsv_row(
        output,
        {"hidden", "scores_bit_identical",
         bool_text(
             report.integrity.hidden
                 .scores_bit_identical)});
    append_tsv_row(
        output,
        {"hidden", "consequence_hashes_bit_identical",
         bool_text(
             report.integrity.hidden
                 .consequence_hashes_bit_identical)});
    append_tsv_row(
        output,
        {"integrity", "artifact_requirements_match",
         bool_text(
             report.integrity
                 .artifact_requirements_match)});
    append_tsv_row(
        output,
        {"integrity", "model_identities_match",
         bool_text(
             report.integrity.model_identities_match)});
    append_tsv_row(
        output,
        {"integrity", "artifacts_unchanged",
         bool_text(report.integrity.artifacts_unchanged)});
    append_tsv_row(
        output,
        {"integrity", "independent_manifest_bit_identical",
         bool_text(
             report.integrity
                 .independent_manifest_bit_identical)});
    append_tsv_row(
        output,
        {"integrity", "repeated_construction_bit_identical",
         bool_text(
             report.integrity
                 .repeated_construction_bit_identical)});
    append_tsv_row(
        output,
        {"integrity", "passed",
         bool_text(report.integrity.passed)});
    for (const std::string& failure :
         report.gate.failures) {
        append_tsv_row(
            output, {"gate_failure", failure});
    }
    return output;
}

void validate_evidence_report(const RunReport& report) {
    const auto snapshot_content_matches =
        [](const artifact_integrity::RegularFileSnapshot&
               snapshot,
           std::uintmax_t bytes,
           std::string_view sha256) {
            return snapshot.byte_size == bytes &&
                   snapshot.sha256 == sha256;
        };
    require(
        report.integrity.passed &&
            report.scientific.complete &&
            !report.gate.infrastructure_failure &&
            report.gate ==
                evaluate_gate(
                    report.scientific,
                    report.integrity) &&
            report.scientific.parent_model_fingerprint ==
                ar1::kParentModelFingerprint &&
            report.scientific
                    .candidate_model_fingerprint ==
                ar1::kCandidateModelFingerprint &&
            report.scientific.manifest.exact &&
            report.integrity
                .artifact_requirements_match &&
            report.integrity.model_identities_match &&
            report.integrity.artifacts_unchanged &&
            report.integrity
                .independent_manifest_bit_identical &&
            report.integrity
                .repeated_construction_bit_identical &&
            report.integrity.hidden.passed &&
            snapshot_content_matches(
                report.integrity.parent_before,
                ar1::kParentArtifactBytes,
                ar1::kParentArtifactSha256) &&
            snapshot_content_matches(
                report.integrity.parent_after,
                ar1::kParentArtifactBytes,
                ar1::kParentArtifactSha256) &&
            snapshot_content_matches(
                report.integrity.candidate_before,
                ar1::kCandidateArtifactBytes,
                ar1::kCandidateArtifactSha256) &&
            snapshot_content_matches(
                report.integrity.candidate_after,
                ar1::kCandidateArtifactBytes,
                ar1::kCandidateArtifactSha256) &&
            report.scientific.roots.size() ==
                kPhysicalPriorityRoots,
        "AC1 evidence publication requires complete "
        "infrastructure-valid evidence");
    for (const RootEvidence& root :
         report.scientific.roots) {
        const probes::DecisionProbe& manifest_probe =
            probe_for(
                report.scientific.manifest,
                root.stable_id);
        std::vector<std::string> expected_descriptors;
        expected_descriptors.reserve(
            manifest_probe.candidates.size());
        for (const auto& candidate :
             manifest_probe.candidates) {
            expected_descriptors.push_back(
                candidate.descriptor);
        }
        std::sort(
            expected_descriptors.begin(),
            expected_descriptors.end());
        const auto validate_model =
            [&](const ModelRootEvidence& model) {
                const auto& decision =
                    model.score.decision;
                std::vector<std::string>
                    actual_descriptors;
                actual_descriptors.reserve(
                    decision.actions.size());
                for (const auto& action :
                     decision.actions) {
                    actual_descriptors.push_back(
                        action.descriptor);
                }
                require(
                    model.score.h0_boundaries.empty() &&
                        decision.stable_id ==
                            root.stable_id &&
                        decision.decision_kind ==
                            probes::DecisionKind::Priority &&
                        decision.score_mode ==
                            scoring::ScoreMode::
                                ReferenceSearch &&
                        decision.recipe.seed_source ==
                            scoring::SeedSource::Derived &&
                        decision.recipe.seed_tag == kSeedTag &&
                        decision.recipe.seed_base ==
                            kSeedBase &&
                        decision.recipe.resolved_seed ==
                            std::optional<std::uint64_t>(
                                probe_runner::
                                    reference_seed_for_probe(
                                        kSeedTag,
                                        root.stable_id,
                                        kSeedBase)) &&
                        decision.recipe.worlds == kWorlds &&
                        decision.recipe.horizon_turns == 0 &&
                        decision.recipe
                                .rollouts_per_world ==
                            kRolloutsPerWorld &&
                        !decision.recipe
                             .blend_shallow_prior &&
                        decision.recipe
                                .evaluation_threads ==
                            kEvaluationThreads &&
                        decision.recipe.value_mirror &&
                        bit_identical_double(
                            decision.recipe
                                .value_continuation_epsilon,
                            0.0) &&
                        bit_identical_double(
                            decision.recipe
                                .value_priority_residual_weight,
                            0.0) &&
                        !decision.recipe
                             .value_pass_dominance &&
                        decision.recipe
                                .value_continuation_controller ==
                            LearnedContinuationController::
                                Legacy &&
                        actual_descriptors ==
                            expected_descriptors &&
                        decision.selected_support ==
                            bit_exact_max_support_impl(
                                decision.actions) &&
                        decision.accounting.sampled_worlds ==
                            kWorlds &&
                        decision.accounting
                                .rollout_evaluations ==
                            decision.actions.size() *
                                kSamplesPerAction &&
                        decision.accounting
                                .terminal_evaluations +
                                decision.accounting
                                    .bootstrapped_evaluations ==
                            decision.accounting
                                .rollout_evaluations &&
                        decision.actions.size() ==
                            model.score
                                .h0_public_consequence_hashes
                                .size(),
                    root.stable_id +
                        ": AC1 evidence projection is incomplete");
                for (std::size_t action = 0;
                     action <
                     model.score.decision.actions.size();
                     ++action) {
                    require(
                        model.score.decision.actions[action]
                                .raw_samples.size() ==
                            kSamplesPerAction &&
                            model.score
                                    .h0_public_consequence_hashes
                                    [action]
                                    .size() ==
                                kSamplesPerAction &&
                            std::isfinite(
                                model.score.decision
                                    .actions[action]
                                    .raw_score) &&
                            std::all_of(
                                model.score.decision
                                    .actions[action]
                                    .raw_samples.begin(),
                                model.score.decision
                                    .actions[action]
                                    .raw_samples.end(),
                                [](double sample) {
                                    return std::isfinite(
                                        sample);
                                }) &&
                            std::all_of(
                                model.score
                                    .h0_public_consequence_hashes
                                    [action]
                                    .begin(),
                                model.score
                                    .h0_public_consequence_hashes
                                    [action]
                                    .end(),
                                [](const std::string& hash) {
                                    return audit_common::
                                        is_lower_hex_digest(
                                            hash);
                                }),
                        root.stable_id +
                            ": AC1 evidence raw row is incomplete");
                }
            };
        validate_model(root.c16);
        validate_model(root.oc1);
    }
}

EvidenceBundle serialize_evidence_bundle_impl(
    const RunReport& report) {
    validate_evidence_report(report);
    EvidenceBundle bundle;
    append_tsv_row(
        bundle.bytes,
        {"schema", std::string(kEvidenceSchema)});
    const std::array<std::pair<std::string, std::string>, 5>
        sections = {{
            {"metadata", metadata_section(report)},
            {"manifest", manifest_section(report)},
            {"scores", scores_section(report)},
            {"contrasts", contrasts_section(report)},
            {"integrity", integrity_section(report)},
        }};
    for (const auto& [name, bytes] : sections) {
        bundle.section_names.push_back(name);
        bundle.section_sha256.push_back(
            artifact_integrity::sha256_string(bytes));
        append_tsv_row(
            bundle.bytes, {"section_begin", name});
        bundle.bytes += bytes;
        append_tsv_row(
            bundle.bytes,
            {"section_sha256", name,
             bundle.section_sha256.back()});
    }
    bundle.payload_sha256 =
        artifact_integrity::sha256_string(bundle.bytes);
    append_tsv_row(
        bundle.bytes,
        {"payload_sha256", bundle.payload_sha256});
    return bundle;
}

bool path_exists_without_following(
    const std::filesystem::path& path) {
    std::error_code error;
    const auto status =
        std::filesystem::symlink_status(path, error);
    if (error ==
        std::make_error_code(
            std::errc::no_such_file_or_directory)) {
        return false;
    }
    if (error) {
        throw std::runtime_error(
            "cannot inspect AC1 evidence path '" +
            path.string() + "': " + error.message());
    }
    return status.type() !=
           std::filesystem::file_type::not_found;
}

void write_evidence_atomic_no_replace_impl(
    std::string_view path_text, std::string_view bytes) {
    if (path_text.empty() || bytes.empty() ||
        path_text.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(
            "AC1 evidence path/bytes are invalid");
    }
    const std::filesystem::path target{std::string(path_text)};
    if (target.filename().empty()) {
        throw std::invalid_argument(
            "AC1 evidence path must name a file");
    }
    const std::filesystem::path directory =
        target.has_parent_path()
            ? target.parent_path()
            : std::filesystem::path(".");
    std::error_code create_error;
    std::filesystem::create_directories(
        directory, create_error);
    if (create_error) {
        throw std::runtime_error(
            "cannot create AC1 evidence directory '" +
            directory.string() + "': " +
            create_error.message());
    }
    std::error_code directory_error;
    const auto directory_status =
        std::filesystem::symlink_status(
            directory, directory_error);
    if (directory_error ||
        directory_status.type() !=
            std::filesystem::file_type::directory) {
        throw std::runtime_error(
            "AC1 evidence parent is not a non-symlink directory");
    }
    const std::filesystem::path temporary =
        target.string() + ".tmp";
    if (path_exists_without_following(target) ||
        path_exists_without_following(temporary)) {
        throw std::runtime_error(
            "AC1 evidence destination or temporary already exists");
    }

    int flags = O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int descriptor =
        ::open(temporary.c_str(), flags, 0644);
    if (descriptor < 0) {
        throw std::runtime_error(
            "cannot create AC1 evidence temporary: " +
            std::string(std::strerror(errno)));
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
        const ssize_t written =
            ::write(
                descriptor, bytes.data() + cursor,
                bytes.size() - cursor);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            const std::string detail = std::strerror(errno);
            cleanup();
            throw std::runtime_error(
                "cannot write AC1 evidence temporary: " +
                detail);
        }
        if (written == 0) {
            cleanup();
            throw std::runtime_error(
                "AC1 evidence write made no progress");
        }
        cursor += static_cast<std::size_t>(written);
    }
    if (::fsync(descriptor) != 0) {
        const std::string detail = std::strerror(errno);
        cleanup();
        throw std::runtime_error(
            "cannot sync AC1 evidence temporary: " +
            detail);
    }
    if (::close(descriptor) != 0) {
        const std::string detail = std::strerror(errno);
        descriptor = -1;
        static_cast<void>(::unlink(temporary.c_str()));
        throw std::runtime_error(
            "cannot close AC1 evidence temporary: " +
            detail);
    }
    descriptor = -1;

    const int directory_descriptor =
        ::open(directory.c_str(), O_RDONLY | O_CLOEXEC);
    if (directory_descriptor < 0) {
        const std::string detail = std::strerror(errno);
        static_cast<void>(::unlink(temporary.c_str()));
        throw std::runtime_error(
            "cannot open AC1 evidence directory: " +
            detail);
    }
    if (::link(temporary.c_str(), target.c_str()) != 0) {
        const std::string detail = std::strerror(errno);
        static_cast<void>(::close(directory_descriptor));
        static_cast<void>(::unlink(temporary.c_str()));
        throw std::runtime_error(
            "cannot atomically publish AC1 evidence: " +
            detail);
    }
    // The successful no-replace hard link is the publication commit point.
    // From here the target contains the complete fsynced bytes. Cleanup and
    // directory durability are best effort: throwing after this point would
    // report exit 2 despite a complete published evidence target.
    static_cast<void>(::unlink(temporary.c_str()));
    static_cast<void>(::fsync(directory_descriptor));
    static_cast<void>(::close(directory_descriptor));
}

void emit_text_noexcept(
    std::ostream& output, std::string_view text) noexcept {
    try {
        output.write(
            text.data(),
            static_cast<std::streamsize>(text.size()));
    } catch (...) {
        // Publication is already committed. Output failure cannot recast a
        // complete scientific result as infrastructure exit 2.
    }
}

void publish_evidence_and_emit_noexcept_impl(
    std::string_view path, std::string_view bytes,
    std::string_view summary, std::ostream& output) {
    write_evidence_atomic_no_replace_impl(path, bytes);
    emit_text_noexcept(output, summary);
}

void print_summary(
    std::ostream& output, const RunReport& report) {
    output << std::setprecision(
        std::numeric_limits<double>::max_digits10);
    output
        << "AC1-T0 next-turn all-action teacher audit\n"
        << "  parent: "
        << report.scientific.parent_model_fingerprint
        << '\n'
        << "  candidate: "
        << report.scientific.candidate_model_fingerprint
        << '\n'
        << "  physical/logical Priority roots: "
        << report.scientific.manifest.roots.size()
        << " / "
        << report.scientific.manifest.logical_priority_ids
        << '\n';
    const auto print_contrasts =
        [&](std::string_view model,
            const std::array<PairedContrast, 3>& contrasts) {
            for (const PairedContrast& contrast :
                 contrasts) {
                output
                    << "  " << model << " "
                    << contrast.name << ": mean="
                    << contrast.mean << " lower95="
                    << contrast.lower_95 << " blocks="
                    << contrast.positive_blocks << "/"
                    << contrast.blocks << " "
                    << (contrast.passed ? "PASS"
                                        : "FAIL")
                    << '\n';
            }
        };
    print_contrasts(
        "C16", report.scientific.c16_primary_contrasts);
    print_contrasts(
        "OC1", report.scientific.oc1_primary_contrasts);
    const auto print_model =
        [&](std::string_view name,
            const ModelRootEvidence& model) {
            const auto& score = model.score.decision;
            output << "      " << name << " support={";
            for (std::size_t index = 0;
                 index < score.selected_support.size();
                 ++index) {
                if (index != 0) {
                    output << ',';
                }
                output << score.selected_support[index];
            }
            output
                << "} accounting="
                << score.accounting.sampled_worlds << "/"
                << score.accounting.rollout_evaluations
                << "/"
                << score.accounting.terminal_evaluations
                << "/"
                << score.accounting.bootstrapped_evaluations
                << '\n';
            for (const auto& action : score.actions) {
                output
                    << "        " << action.descriptor
                    << "=" << action.raw_score << '\n';
            }
        };
    for (const RootEvidence& root :
         report.scientific.roots) {
        output
            << "    root " << root.stable_id << " deck="
            << deck_name(root.root_deck) << " ia="
            << root.information_action_fingerprint << '\n';
        print_model("C16", root.c16);
        print_model("OC1", root.oc1);
    }
    output
        << "    logical alias "
        << kDevForceSpikeAlias << " reuses exact rows from "
        << kCanonicalLiveForceSpike << '\n';
    output
        << "  support controls: "
        << report.scientific.passed_support_controls
        << "/"
        << report.scientific.required_support_controls
        << '\n'
        << "  hidden changed/attempted: "
        << report.integrity.hidden.changed << "/"
        << report.integrity.hidden.attempted << '\n'
        << "  evidence: " << report.publication.path
        << " bytes=" << report.publication.byte_size
        << " sha256=" << report.publication.sha256
        << " payload_sha256="
        << report.publication.payload_sha256 << '\n'
        << "  verdict: "
        << (report.gate.passed
                ? "PASS"
                : report.gate.infrastructure_failure
                      ? "INFRASTRUCTURE FAILURE"
                      : "SCIENTIFIC FAILURE")
        << '\n';
    for (const std::string& failure :
         report.gate.failures) {
        output << "    - " << failure << '\n';
    }
}

void print_infrastructure_summary(
    std::ostream& output, const GateReport& gate) {
    output
        << "AC1-T0 infrastructure failure; no scientific "
           "evidence was published.\n";
    for (const std::string& failure : gate.failures) {
        output << "  - " << failure << '\n';
    }
}

} // namespace

Manifest build_manifest() {
    const std::vector<probes::DecisionProbe> dev =
        probes::make_probe_dev_v3();
    const std::vector<probes::DecisionProbe> force =
        probes::make_force_spike_policy_controls_v1();
    const std::vector<probes::DecisionProbe> counters =
        probes::make_counter_composition_controls_v1();
    const std::vector<probes::DecisionProbe> braingeyser =
        probes::make_braingeyser_x_zero_control_v1();
    const std::vector<probes::DecisionProbe> validation =
        probes::make_probe_validation_v1();
    const std::vector<probes::DecisionProbe> field =
        probes::make_field_regressions_v1();

    require_no_errors(
        probes::validate_probe_dev_v3(dev),
        probes::kProbeDevV3);
    require_no_errors(
        probes::validate_force_spike_policy_controls_v1(
            force),
        probes::kForceSpikePolicyControlsV1);
    require_no_errors(
        probes::validate_counter_composition_controls_v1(
            counters),
        probes::kCounterCompositionControlsV1);
    require_no_errors(
        probes::validate_braingeyser_x_zero_control_v1(
            braingeyser),
        probes::kBraingeyserXZeroControlV1);
    require_no_errors(
        probes::validate_probe_validation_v1(validation),
        probes::kProbeValidationV1);
    require_no_errors(
        probes::validate_field_regressions_v1(field),
        probes::kFieldRegressionsV1);

    const auto dev_alias = std::find_if(
        dev.begin(), dev.end(),
        [](const probes::DecisionProbe& probe) {
            return probe.stable_id ==
                   kDevForceSpikeAlias;
        });
    const auto live = std::find_if(
        force.begin(), force.end(),
        [](const probes::DecisionProbe& probe) {
            return probe.stable_id ==
                   kCanonicalLiveForceSpike;
        });
    require(
        dev_alias != dev.end() && live != force.end() &&
            same_physical_root_ignoring_stable_id(
                *dev_alias, *live) &&
            probes::bsr_information_action_fingerprint(
                *dev_alias) ==
                probes::bsr_information_action_fingerprint(
                    *live),
        "AC1 Force Spike alias is not the canonical physical root");

    Manifest manifest{
        .dev_force_spike_alias =
            std::string(kDevForceSpikeAlias),
        .canonical_live_force_spike =
            std::string(kCanonicalLiveForceSpike),
        .attack_stable_id =
            std::string(kDevAttackStableId),
        .attack_information_action_fingerprint =
            std::string(kDevAttackFingerprint),
    };
    const auto append =
        [&](const probes::DecisionProbe& probe,
            bool from_dev_v3) {
            if (probe.decision_kind !=
                probes::DecisionKind::Priority) {
                throw std::runtime_error(
                    probe.stable_id +
                    ": non-Priority root entered AC1 manifest");
            }
            const std::string fingerprint =
                probes::bsr_information_action_fingerprint(
                    probe);
            const std::string contract_fingerprint =
                manifest_root_contract_fingerprint_impl(
                    probe);
            const ExpectedRoot& expected =
                expected_root(probe.stable_id);
            require(
                fingerprint == expected.fingerprint &&
                    probe.root_deck == expected.deck &&
                    contract_fingerprint ==
                        expected
                            .factory_contract_fingerprint,
                probe.stable_id +
                    ": AC1 manifest identity drifted");
            manifest.roots.push_back({
                .probe = probe,
                .information_action_fingerprint =
                    fingerprint,
                .factory_contract_fingerprint =
                    contract_fingerprint,
                .from_dev_v3 = from_dev_v3,
            });
            ++manifest.physical_roots_by_deck[
                deck_index(probe.root_deck)];
        };

    std::size_t dev_priority = 0;
    std::size_t dev_attack = 0;
    for (const probes::DecisionProbe& probe : dev) {
        ++manifest.logical_dev_roots_by_deck[
            deck_index(probe.root_deck)];
        if (probe.decision_kind ==
            probes::DecisionKind::Attack) {
            ++dev_attack;
            require(
                probe.stable_id == kDevAttackStableId &&
                    probes::bsr_information_action_fingerprint(
                        probe) == kDevAttackFingerprint,
                "AC1 dev-v3 Attack census drifted");
            continue;
        }
        ++dev_priority;
        if (probe.stable_id == kDevForceSpikeAlias) {
            continue;
        }
        append(probe, true);
    }
    for (const auto& probe : force) {
        append(
            probe,
            probe.stable_id ==
                kCanonicalLiveForceSpike);
    }
    for (const auto& probe : counters) {
        append(probe, false);
    }
    for (const auto& probe : braingeyser) {
        append(probe, false);
    }
    for (const auto& probe : validation) {
        append(probe, false);
    }
    for (const auto& probe :
         select_field_priority_roots(field)) {
        append(probe, false);
    }

    std::sort(
        manifest.roots.begin(), manifest.roots.end(),
        [](const ManifestRoot& left,
           const ManifestRoot& right) {
            return left.probe.stable_id <
                   right.probe.stable_id;
        });
    std::set<std::string> ids;
    for (const ManifestRoot& root : manifest.roots) {
        require(
            ids.insert(root.probe.stable_id).second,
            root.probe.stable_id +
                ": duplicate AC1 physical root");
    }
    manifest.logical_priority_ids =
        manifest.roots.size() + 1;
    manifest.exact =
        dev.size() == 20 &&
        dev_priority == kDevPriorityRoots &&
        dev_attack == kDevAttackRoots &&
        manifest.roots.size() ==
            kPhysicalPriorityRoots &&
        manifest.logical_priority_ids ==
            kLogicalPriorityIds &&
        manifest.physical_roots_by_deck ==
            kExpectedPhysicalByDeck &&
        manifest.logical_dev_roots_by_deck ==
            kExpectedLogicalDevByDeck &&
        ids.size() == kExpectedRoots.size() &&
        std::all_of(
            kExpectedRoots.begin(), kExpectedRoots.end(),
            [](const ExpectedRoot& expected) {
                return !expected
                            .factory_contract_fingerprint
                            .empty();
            }) &&
        std::all_of(
            kExpectedRoots.begin(), kExpectedRoots.end(),
            [&](const ExpectedRoot& expected) {
                return ids.contains(
                    std::string(expected.stable_id));
            });
    require(manifest.exact, "AC1 manifest census drifted");
    return manifest;
}

PairedContrast paired_contrast(
    std::string name,
    const scoring::DescriptorScore& positive,
    const scoring::DescriptorScore& negative) {
    if (name.empty() ||
        positive.descriptor.empty() ||
        negative.descriptor.empty() ||
        positive.descriptor == negative.descriptor ||
        positive.raw_samples.size() !=
            kSamplesPerAction ||
        negative.raw_samples.size() !=
            kSamplesPerAction) {
        throw std::invalid_argument(
            "AC1 primary contrast is incomplete");
    }
    std::array<double, kSamplesPerAction> differences{};
    double sum = 0.0;
    for (std::size_t index = 0;
         index < differences.size(); ++index) {
        const double first = positive.raw_samples[index];
        const double second = negative.raw_samples[index];
        if (!std::isfinite(first) ||
            !std::isfinite(second)) {
            throw std::runtime_error(
                "AC1 contrast contains a non-finite sample");
        }
        differences[index] = first - second;
        sum += differences[index];
    }
    const double mean =
        sum / static_cast<double>(differences.size());
    double squared = 0.0;
    for (const double difference : differences) {
        const double centered = difference - mean;
        squared += centered * centered;
    }
    const double variance =
        squared /
        static_cast<double>(differences.size() - 1);
    const double standard_error =
        std::sqrt(
            variance /
            static_cast<double>(differences.size()));
    const double lower =
        mean -
        kNormal95CriticalValue * standard_error;
    if (!std::isfinite(mean) ||
        !std::isfinite(variance) ||
        !std::isfinite(standard_error) ||
        !std::isfinite(lower)) {
        throw std::runtime_error(
            "AC1 contrast statistic is non-finite");
    }
    std::size_t positive_blocks = 0;
    for (std::size_t block = 0; block < kBlocks; ++block) {
        const auto begin =
            differences.begin() +
            static_cast<std::ptrdiff_t>(
                block * kSamplesPerBlock);
        const auto end =
            begin +
            static_cast<std::ptrdiff_t>(
                kSamplesPerBlock);
        const double block_sum =
            std::accumulate(begin, end, 0.0);
        if (block_sum /
                static_cast<double>(kSamplesPerBlock) >
            0.0) {
            ++positive_blocks;
        }
    }
    const bool passed =
        mean > 0.0 && lower > 0.0 &&
        positive_blocks >= kMinimumPositiveBlocks;
    return {
        .name = std::move(name),
        .positive_descriptor =
            positive.descriptor,
        .negative_descriptor =
            negative.descriptor,
        .mean = mean,
        .standard_error = standard_error,
        .lower_95 = lower,
        .positive_blocks = positive_blocks,
        .blocks = kBlocks,
        .samples = differences.size(),
        .complete = true,
        .passed = passed,
    };
}

GateReport evaluate_gate(
    const ScientificEvidence& scientific,
    const IntegrityReport& integrity) {
    GateReport gate{
        .integrity_passed = integrity.passed,
        .complete_evidence = scientific.complete,
        .scientific_passed = scientific.passed,
    };
    if (!integrity.passed) {
        gate.infrastructure_failure = true;
        gate.failures.push_back(
            "artifact, hidden, or repeat integrity failed");
    }
    if (!scientific.complete) {
        gate.infrastructure_failure = true;
        gate.failures.push_back(
            "expected complete AC1 evidence is missing");
    }
    if (!scientific.passed &&
        scientific.complete &&
        integrity.passed) {
        if (scientific.passed_primary_contrasts != 3) {
            gate.failures.push_back(
                "one or more OC1 primary contrasts failed");
        }
        if (!scientific.support_controls_passed) {
            gate.failures.push_back(
                "one or more OC1 support controls failed");
        }
    }
    gate.passed =
        !gate.infrastructure_failure &&
        scientific.passed &&
        gate.failures.empty();
    return gate;
}

int exit_code(const GateReport& gate) {
    if (gate.infrastructure_failure ||
        !gate.integrity_passed ||
        !gate.complete_evidence) {
        return 2;
    }
    return gate.passed ? 0 : 1;
}

RunReport run(std::ostream& progress) {
    const std::filesystem::path evidence_path{
        std::string(kEvidencePath)};
    const std::filesystem::path evidence_temporary =
        evidence_path.string() + ".tmp";
    require(
        !path_exists_without_following(evidence_path) &&
            !path_exists_without_following(
                evidence_temporary),
        "AC1 evidence destination or temporary already exists");
    progress
        << "AC1-T0: verifying immutable artifacts and "
           "26-root manifest...\n";
    RunReport report;
    report.integrity.parent_before =
        snapshot_parent();
    report.integrity.candidate_before =
        snapshot_candidate();
    report.integrity.artifact_requirements_match =
        snapshot_matches(
            report.integrity.parent_before,
            ar1::kParentArtifactPath,
            ar1::kParentArtifactBytes,
            ar1::kParentArtifactSha256) &&
        snapshot_matches(
            report.integrity.candidate_before,
            ar1::kCandidateArtifactPath,
            ar1::kCandidateArtifactBytes,
            ar1::kCandidateArtifactSha256);
    require(
        report.integrity.artifact_requirements_match,
        "AC1 immutable artifact requirement failed");

    const Manifest manifest = build_manifest();
    const Manifest repeated_manifest = build_manifest();
    report.integrity.independent_manifest_bit_identical =
        manifest == repeated_manifest;
    require(
        report.integrity.independent_manifest_bit_identical,
        "AC1 independent manifest reconstruction drifted");
    const auto [parent, candidate] = load_models();
    report.integrity.model_identities_match =
        learned_model_fingerprint(parent) ==
            ar1::kParentModelFingerprint &&
        learned_model_fingerprint(candidate) ==
            ar1::kCandidateModelFingerprint;
    require(
        report.integrity.model_identities_match,
        "AC1 model fingerprints drifted");

    progress
        << "AC1-T0: constructing original/hidden/reversed "
           "H0 evidence twice...\n";
    Construction first =
        construct(manifest, parent, candidate);
    Construction repeated =
        construct(repeated_manifest, parent, candidate);
    report.scientific = std::move(first.evidence);
    report.integrity.hidden = first.hidden;
    report.integrity.repeated_construction_bit_identical =
        scientific_evidence_bit_identical_impl(
            report.scientific, repeated.evidence) &&
        first.hidden == repeated.hidden;
    report.integrity.parent_after = snapshot_parent();
    report.integrity.candidate_after =
        snapshot_candidate();
    report.integrity.artifacts_unchanged =
        report.integrity.parent_before ==
            report.integrity.parent_after &&
        report.integrity.candidate_before ==
            report.integrity.candidate_after;
    report.integrity.passed =
        report.integrity.artifact_requirements_match &&
        report.integrity.model_identities_match &&
        report.integrity.artifacts_unchanged &&
        report.integrity
            .independent_manifest_bit_identical &&
        report.integrity
            .repeated_construction_bit_identical &&
        report.integrity.hidden.passed;
    report.gate =
        evaluate_gate(report.scientific, report.integrity);
    if (report.integrity.passed &&
        report.scientific.complete &&
        !report.gate.infrastructure_failure) {
        const EvidenceBundle bundle =
            serialize_evidence_bundle_impl(report);
        report.publication = {
            .path = std::string(kEvidencePath),
            .byte_size = bundle.bytes.size(),
            .sha256 =
                artifact_integrity::sha256_string(
                    bundle.bytes),
            .payload_sha256 =
                bundle.payload_sha256,
            .atomic_no_replace = false,
            .published = false,
        };
        std::ostringstream rendered_summary;
        print_summary(rendered_summary, report);
        const std::string summary_bytes =
            rendered_summary.str();
        write_evidence_atomic_no_replace_impl(
            kEvidencePath, bundle.bytes);
        // The writer cannot throw after its no-replace hard-link commit.
        // Flip the publication witness before the noexcept summary emission.
        report.publication.atomic_no_replace = true;
        report.publication.published = true;
        emit_text_noexcept(progress, summary_bytes);
    } else {
        print_infrastructure_summary(
            progress, report.gate);
    }
    return report;
}

int run_cli(
    int argc, char*[], std::ostream& output,
    std::ostream& error) {
    if (argc != 1) {
        error
            << "Usage: old-school-ac1-teacher-audit\n"
            << "This sealed command accepts no paths, seeds, "
               "recipes, or gate overrides.\n";
        return 2;
    }
    try {
        return exit_code(run(output).gate);
    } catch (const std::exception& failure) {
        error << "AC1-T0 infrastructure failure: "
              << failure.what() << '\n';
        return 2;
    }
}

namespace testing {

CapturedScore score_priority_root(
    const probes::DecisionProbe& probe,
    std::shared_ptr<const LearnedModel> model,
    const scoring::SearchRecipe& recipe) {
    return score_priority_root_impl(
        probe, model, recipe);
}

std::string h0_public_consequence_hash(
    const LearnedPriorityH0Boundary& boundary,
    std::size_t observer) {
    if (observer >= boundary.state.players.size()) {
        throw std::out_of_range(
            "AC1 H0 consequence observer is invalid");
    }
    const PlayerObservation observation =
        observe_game_state(boundary.state, observer);
    ByteWriter writer;
    writer.integer<std::uint64_t>(observer);
    writer.boolean(boundary.terminal);
    writer.boolean(boundary.context.valid);
    writer.integer<std::uint64_t>(
        static_cast<std::uint64_t>(
            boundary.context.phase));
    writer.integer<std::uint64_t>(
        boundary.context.decision_player);
    writer.integer(boundary.context.consecutive_passes);
    writer.boolean(boundary.context.sorcery_actions);
    for (const PublicPlayerState& player :
         observation.players) {
        append_public_player(writer, player);
    }
    writer.sequence(
        observation.hand,
        [](ByteWriter& output, CardId card) {
            append_card(output, card);
        });
    // The observer owns both hidden zones. Opponent library/hand identities
    // remain redacted to the public sizes in PublicPlayerState.
    writer.sequence(
        boundary.state.players[observer].library,
        [](ByteWriter& output, CardId card) {
            append_card(output, card);
        });
    append_stack(writer, observation.stack);
    for (const std::size_t turns :
         observation.extra_turns_pending) {
        writer.integer<std::uint64_t>(turns);
    }
    for (const bool failed : boundary.state.failed_draw) {
        writer.boolean(failed);
    }
    writer.integer<std::uint64_t>(
        observation.active_player);
    writer.integer<std::uint64_t>(
        observation.starting_player);
    writer.integer<std::uint64_t>(
        observation.turn_number);
    writer.integer<std::uint64_t>(
        boundary.state.next_permanent_id);
    writer.integer<std::uint64_t>(
        boundary.state.next_stack_object_id);
    return artifact_integrity::sha256_string(
        writer.data());
}

std::string manifest_root_contract_fingerprint(
    const probes::DecisionProbe& probe) {
    return manifest_root_contract_fingerprint_impl(probe);
}

bool captured_score_bit_identical(
    const CapturedScore& first,
    const CapturedScore& second) {
    return captured_score_bit_identical_impl(
        first, second);
}

bool scientific_evidence_bit_identical(
    const ScientificEvidence& first,
    const ScientificEvidence& second) {
    return scientific_evidence_bit_identical_impl(
        first, second);
}

EvidenceBundle serialize_evidence_bundle(
    const RunReport& report) {
    return serialize_evidence_bundle_impl(report);
}

void write_evidence_atomic_no_replace(
    std::string_view path, std::string_view bytes) {
    write_evidence_atomic_no_replace_impl(path, bytes);
}

void publish_evidence_and_emit_noexcept(
    std::string_view path, std::string_view bytes,
    std::string_view summary, std::ostream& output) {
    publish_evidence_and_emit_noexcept_impl(
        path, bytes, summary, output);
}

bool support_excludes_x_zero(
    const probes::DecisionProbe& probe,
    const scoring::DecisionScore& score) {
    for (const std::string& selected :
         score.selected_support) {
        const auto found = std::find_if(
            probe.candidates.begin(),
            probe.candidates.end(),
            [&](const probes::Candidate& candidate) {
                return candidate.descriptor == selected;
            });
        if (found == probe.candidates.end()) {
            throw std::invalid_argument(
                probe.stable_id +
                ": selected support is not in the root");
        }
        const auto* action =
            std::get_if<PriorityAction>(&found->action);
        if (action == nullptr) {
            throw std::invalid_argument(
                probe.stable_id +
                ": selected support is not Priority");
        }
        const bool x_spell =
            action->kind ==
                PriorityActionKind::CastDisintegrate ||
            action->kind ==
                PriorityActionKind::CastBraingeyser;
        if (x_spell && action->x_value == 0) {
            return false;
        }
    }
    return !score.selected_support.empty();
}

std::vector<std::string> bit_exact_max_support(
    const std::vector<scoring::DescriptorScore>& actions) {
    return bit_exact_max_support_impl(actions);
}

} // namespace testing

} // namespace old_school::ac1_teacher_audit
