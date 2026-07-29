#include "old_school/information_set_puct_preflight.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <ostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace old_school::information_set_puct_preflight {
namespace {

constexpr std::string_view kRedundantCounterId =
    "control.blue.counter-redundant-same-target.v1";
constexpr std::string_view kInterveningCounterId =
    "control.blue.counter-same-target-after-intervening-counter.v1";
constexpr std::string_view kBraingeyserId =
    "control.blue.braingeyser-x0.v1";
constexpr std::string_view kAncestralId =
    "field.blue.ancestral-opponent-seed24.aq0.v1";
constexpr std::string_view kSickBearGrowthId =
    "field.green.second-main-sick-bear-growth.v1";
constexpr std::string_view kOpponentGrowthId =
    "field.green.begin-combat-growth-tapped-air.v1";
constexpr std::string_view kLiveForceSpikeId =
    "control.blue.force-spike-live-gray-ogre.v1";
constexpr std::string_view kFiveOpenForceSpikeId =
    "control.blue.force-spike-payable-five-open-gray-ogre.aq0.v1";
constexpr std::string_view kLife20BlockId =
    "field.ru.life20-flying-men-chump-air.v1";
constexpr std::string_view kLife4BlockId =
    "field.ru.life4-flying-men-chump-air.v1";
constexpr std::string_view kAttackId =
    "diagnostic.ru.life20-flying-men-attack-air.v1";

std::size_t g_engine_search_invocations = 0;

std::uint64_t splitmix64(std::uint64_t value) {
    value += UINT64_C(0x9e3779b97f4a7c15);
    value =
        (value ^ (value >> 30)) *
        UINT64_C(0xbf58476d1ce4e5b9);
    value =
        (value ^ (value >> 27)) *
        UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}

std::uint64_t fnv_string(std::string_view value) {
    std::uint64_t hash = UINT64_C(1469598103934665603);
    constexpr std::uint64_t kPrime =
        UINT64_C(1099511628211);
    for (const unsigned char byte : value) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= kPrime;
    }
    return hash;
}

std::uint64_t indexed_seed(
    std::uint64_t base, std::uint64_t domain,
    std::size_t first = 0, std::size_t second = 0) {
    std::uint64_t value = splitmix64(base ^ domain);
    value = splitmix64(
        value ^ static_cast<std::uint64_t>(first));
    return splitmix64(
        value ^ static_cast<std::uint64_t>(second));
}

std::uint64_t root_search_seed(
    const aq5::PreparedRoot& root) {
    return splitmix64(
        kPreflightSeed ^
        UINT64_C(0x49535030524f4f54) ^
        fnv_string(root.stable_id));
}

std::uint64_t root_tie_seed(
    const aq5::PreparedRoot& root) {
    return splitmix64(
        root_search_seed(root) ^
        UINT64_C(0x4953503054494542));
}

LearnedGenerativePosition make_root_position(
    const aq5::PreparedRoot& root) {
    switch (root.family) {
    case aq5::DecisionFamily::Priority:
        return make_learned_generative_priority_position(
            root.state, root.original_decks, root.actor,
            {
                .valid = true,
                .phase = root.phase,
                .decision_player = root.actor,
                .consecutive_passes =
                    root.consecutive_passes,
                .sorcery_actions =
                    root.sorcery_actions,
            });
    case aq5::DecisionFamily::Attack:
        return make_learned_generative_attack_position(
            root.state, root.original_decks, root.actor,
            root.actor, root.selected_attackers,
            root.subject, root.remaining_attackers);
    case aq5::DecisionFamily::Block:
        return make_learned_generative_block_position(
            root.state, root.original_decks, root.actor,
            1 - root.actor, root.attackers,
            root.selected_blocks, root.subject_blocker,
            root.remaining_blockers);
    }
    throw std::invalid_argument(
        "ISP0 root has an invalid decision family");
}

bool payload_matches(
    const probes::CandidateAction& candidate,
    const LearnedGenerativeActionPayload& payload) {
    if (const auto* priority =
            std::get_if<PriorityAction>(&candidate)) {
        const auto* action =
            std::get_if<PriorityAction>(&payload);
        return action != nullptr && *action == *priority;
    }
    if (const auto* attack =
            std::get_if<probes::BinaryAttackDecision>(
                &candidate)) {
        const auto* action =
            std::get_if<LearnedGenerativeAttackAction>(
                &payload);
        return action != nullptr &&
               action->subject == attack->attacker &&
               action->include == attack->include;
    }
    const auto& block =
        std::get<probes::BinaryBlockDecision>(candidate);
    const auto* action =
        std::get_if<LearnedGenerativeBlockAction>(
            &payload);
    return action != nullptr &&
           action->blocker == block.blocker &&
           action->attacker ==
               (block.include
                    ? std::optional<PermanentId>(
                          block.attacker)
                    : std::nullopt);
}

struct RootActionMap {
    std::string fixture_key;
    LearnedGenerativeActionPrior engine;

    bool operator==(const RootActionMap&) const = default;
};

std::vector<RootActionMap> map_root_actions(
    const aq5::PreparedRoot& root,
    const LearnedGenerativeObservation& observation) {
    if (root.candidate_keys.size() !=
        root.candidates.size()) {
        throw std::invalid_argument(
            "ISP0 fixture candidate metadata is ragged");
    }
    std::vector<RootActionMap> result;
    result.reserve(root.candidates.size());
    std::set<std::string> used_engine_keys;
    for (std::size_t index = 0;
         index < root.candidates.size(); ++index) {
        const auto found = std::find_if(
            observation.actions.begin(),
            observation.actions.end(),
            [&](const LearnedGenerativeActionPrior& action) {
                return payload_matches(
                    root.candidates[index],
                    action.action.payload);
            });
        if (found == observation.actions.end() ||
            !used_engine_keys
                 .insert(found->action.stable_key)
                 .second) {
            throw std::logic_error(
                "ISP0 fixture action did not map uniquely "
                "to an engine action");
        }
        result.push_back({
            .fixture_key = root.candidate_keys[index],
            .engine = *found,
        });
    }
    if (result.size() != observation.actions.size()) {
        throw std::logic_error(
            "ISP0 fixture does not cover every legal root "
            "action");
    }
    return result;
}

puct::Observation to_puct_observation(
    const LearnedGenerativeObservation& observation,
    const std::vector<RootActionMap>* root_actions) {
    if (observation.actor > 1) {
        throw std::logic_error(
            "ISP0 engine observation has an invalid actor");
    }
    puct::Observation result{
        .information_set_key =
            observation.information_set_key,
        .legal_signature =
            observation.legal_signature,
        .actor =
            static_cast<std::uint8_t>(observation.actor),
        .fpu_leaf_value =
            observation.fpu_leaf_value,
    };
    if (root_actions != nullptr) {
        result.actions.reserve(root_actions->size());
        for (const RootActionMap& action :
             *root_actions) {
            result.actions.push_back({
                .action_key = action.fixture_key,
                .prior = action.engine.prior,
            });
        }
    } else {
        result.actions.reserve(observation.actions.size());
        for (const auto& action : observation.actions) {
            result.actions.push_back({
                .action_key = action.action.stable_key,
                .prior = action.prior,
            });
        }
    }
    return result;
}

struct SafeTraceStep {
    std::size_t simulation = 0;
    std::size_t depth = 0;
    std::string action_key;
    std::string engine_action_key;
    LearnedGenerativeDisposition disposition =
        LearnedGenerativeDisposition::Bound;
    LearnedGenerativeBound exhausted_bound =
        LearnedGenerativeBound::None;
    LearnedGenerativeWitness witness;
    std::optional<int> terminal_winner;
    std::optional<std::string> successor_information_set_key;
    std::optional<LearnedGenerativeLeafEvaluation>
        successor_leaf_evaluation;
    std::size_t actions_applied = 0;
    std::size_t phase_transitions = 0;
    std::size_t turn_advances = 0;

    bool operator==(const SafeTraceStep&) const = default;
};

struct TraceStep {
    SafeTraceStep safe;
    GameState truth_before;
};

struct EngineParticle final : puct::TruthParticle {
    LearnedGenerativePosition position;
    std::size_t simulation = 0;
    std::size_t depth = 0;
    std::map<std::string, std::string> action_map;
};

class EngineEnvironment final : public puct::Environment {
public:
    EngineEnvironment(
        aq5::PreparedRoot root,
        std::shared_ptr<const LearnedModel> parent,
        std::uint64_t search_seed)
        : root_(std::move(root)),
          parent_(std::move(parent)),
          search_seed_(search_seed),
          observation_seed_(indexed_seed(
              search_seed_,
              UINT64_C(0x4e4f44454f425356))),
          root_position_(make_root_position(root_)),
          root_engine_observation_(
              observe_learned_generative_position(
                  root_position_, parent_,
                  observation_seed_)),
          root_actions_(
              map_root_actions(
                  root_, root_engine_observation_)),
          root_observation_(
              to_puct_observation(
                  root_engine_observation_,
                  &root_actions_)) {
        if (!parent_) {
            throw std::invalid_argument(
                "ISP0 environment requires a parent");
        }
        traces_.resize(puct::kSimulationCount);
    }

    std::unique_ptr<puct::TruthParticle>
    start_simulation(
        std::size_t simulation_index) override {
        if (simulation_index >= puct::kSimulationCount) {
            throw std::out_of_range(
                "ISP0 simulation index is outside the "
                "sealed budget");
        }
        auto particle =
            std::make_unique<EngineParticle>();
        particle->position = root_position_;
        particle->position.truth =
            learned_generative_actor_determinization(
                root_position_,
                indexed_seed(
                    search_seed_,
                    UINT64_C(0x53494d554c415445),
                    simulation_index));
        particle->simulation = simulation_index;
        particle->action_map = root_action_map();
        traces_[simulation_index].clear();
        return particle;
    }

    puct::Observation observe(
        const puct::TruthParticle& base) override {
        const auto* particle =
            dynamic_cast<const EngineParticle*>(&base);
        if (particle == nullptr || particle->depth != 0 ||
            learned_generative_actor(particle->position) !=
                root_.actor) {
            throw puct::SearchFailure(
                puct::FailureCode::InvalidParticle,
                "ISP0 root observation received an invalid "
                "engine particle");
        }
        return root_observation_;
    }

    puct::Transition advance(
        puct::TruthParticle& base,
        std::string_view action_key) override {
        auto* particle =
            dynamic_cast<EngineParticle*>(&base);
        if (particle == nullptr ||
            particle->simulation >= traces_.size()) {
            throw puct::SearchFailure(
                puct::FailureCode::InvalidParticle,
                "ISP0 advance received an invalid engine "
                "particle");
        }
        const auto mapped =
            particle->action_map.find(
                std::string(action_key));
        if (mapped == particle->action_map.end()) {
            throw std::invalid_argument(
                "ISP0 selected action is absent from the "
                "engine mapping");
        }

        const GameState truth_before =
            particle->position.truth;
        const std::string external_key(action_key);
        const std::string engine_key = mapped->second;
        const LearnedGenerativeTransition transition =
            advance_learned_generative_position(
                particle->position, engine_key, parent_,
                transition_seed(
                    search_seed_,
                    particle->simulation,
                    particle->depth),
                true);

        SafeTraceStep safe{
            .simulation = particle->simulation,
            .depth = particle->depth,
            .action_key = external_key,
            .engine_action_key = engine_key,
            .disposition = transition.disposition,
            .exhausted_bound =
                transition.exhausted_bound,
            .witness = transition.witness,
            .actions_applied =
                transition.actions_applied,
            .phase_transitions =
                transition.phase_transitions,
            .turn_advances =
                transition.turn_advances,
        };
        if (transition.terminal_result.has_value()) {
            safe.terminal_winner =
                transition.terminal_result->winner;
        }

        ++particle->depth;
        bounded_macro_accounting_ =
            bounded_macro_accounting_ &&
            transition.actions_applied <=
                kLearnedPriorityMacroActionBound &&
            transition.phase_transitions <=
                kLearnedPriorityMacroPhaseTransitionBound &&
            transition.turn_advances <=
                kLearnedPriorityMacroTurnAdvanceBound;
        opponent_action_accounting_consistent_ =
            opponent_action_accounting_consistent_ &&
            transition.witness
                    .opponent_decisions_applied ==
                transition.witness
                    .opponent_decisions.size() &&
            transition.witness
                    .opponent_decisions_applied <=
                transition.actions_applied;
        for (const auto& opponent :
             transition.witness.opponent_decisions) {
            const bool selected_present =
                std::any_of(
                    opponent.actions.begin(),
                    opponent.actions.end(),
                    [&](const auto& action) {
                        return action.action.stable_key ==
                                   opponent
                                       .selected_stable_key &&
                               std::isfinite(
                                   action
                                       .successor_value);
                    });
            opponent_action_accounting_consistent_ =
                opponent_action_accounting_consistent_ &&
                !opponent.actions.empty() &&
                !opponent.selected_stable_key.empty() &&
                selected_present;
        }
        if (transition.disposition ==
                LearnedGenerativeDisposition::Bound &&
            transition.exhausted_bound ==
                LearnedGenerativeBound::ExactCombat) {
            no_combat_bound_fallback_ = false;
        }
        for (const auto& opponent :
             transition.witness.opponent_decisions) {
            opponent_information_set_keys_.insert(
                opponent.information_set_key);
        }

        if (transition.disposition ==
            LearnedGenerativeDisposition::Bound) {
            traces_[particle->simulation].push_back({
                .safe = std::move(safe),
                .truth_before = truth_before,
            });
            return puct::Bound{
                .reason = "engine generative transition "
                          "exhausted a bound",
            };
        }
        if (transition.disposition ==
            LearnedGenerativeDisposition::Terminal) {
            traces_[particle->simulation].push_back({
                .safe = std::move(safe),
                .truth_before = truth_before,
            });
            if (!transition.terminal_result.has_value()) {
                throw std::logic_error(
                    "ISP0 terminal transition has no result");
            }
            const int winner =
                transition.terminal_result->winner;
            if (winner < 0) {
                return puct::Terminal{
                    .winner = std::nullopt,
                };
            }
            if (winner > 1) {
                throw std::logic_error(
                    "ISP0 terminal winner is invalid");
            }
            return puct::Terminal{
                .winner =
                    static_cast<std::uint8_t>(winner),
            };
        }
        if (!transition.position.has_value()) {
            throw std::logic_error(
                "ISP0 decision transition has no position");
        }

        particle->position = *transition.position;
        const LearnedGenerativeObservation engine_observation =
            observe_learned_generative_position(
                particle->position, parent_,
                observation_seed_);
        const bool recurrent_root =
            engine_observation.information_set_key ==
            root_engine_observation_.information_set_key;
        const puct::Observation observation =
            to_puct_observation(
                engine_observation,
                recurrent_root ? &root_actions_ : nullptr);
        safe.successor_information_set_key =
            observation.information_set_key;
        safe.successor_leaf_evaluation =
            evaluate_learned_generative_leaf(
                particle->position, root_.actor, parent_,
                indexed_seed(
                    observation_seed_,
                    UINT64_C(0x534944454c454146),
                    particle->simulation,
                    particle->depth));
        traces_[particle->simulation].push_back({
            .safe = std::move(safe),
            .truth_before = truth_before,
        });
        particle->action_map.clear();
        if (recurrent_root) {
            particle->action_map = root_action_map();
        } else {
            for (const auto& action :
                 engine_observation.actions) {
                particle->action_map.emplace(
                    action.action.stable_key,
                    action.action.stable_key);
            }
        }
        return observation;
    }

    const LearnedGenerativePosition& root_position() const {
        return root_position_;
    }

    const LearnedGenerativeObservation&
    root_engine_observation() const {
        return root_engine_observation_;
    }

    const puct::Observation& root_observation() const {
        return root_observation_;
    }

    const std::vector<RootActionMap>& root_actions() const {
        return root_actions_;
    }

    const std::vector<std::vector<TraceStep>>& traces() const {
        return traces_;
    }

    std::vector<std::vector<SafeTraceStep>>
    safe_traces() const {
        std::vector<std::vector<SafeTraceStep>> result;
        result.reserve(traces_.size());
        for (const auto& simulation : traces_) {
            std::vector<SafeTraceStep> safe;
            safe.reserve(simulation.size());
            for (const TraceStep& step : simulation) {
                safe.push_back(step.safe);
            }
            result.push_back(std::move(safe));
        }
        return result;
    }

    bool bounded_macro_accounting() const {
        return bounded_macro_accounting_;
    }

    bool no_combat_bound_fallback() const {
        return no_combat_bound_fallback_;
    }

    bool opponent_action_accounting_consistent() const {
        return opponent_action_accounting_consistent_;
    }

    const std::set<std::string>&
    opponent_information_set_keys() const {
        return opponent_information_set_keys_;
    }

private:
    std::map<std::string, std::string>
    root_action_map() const {
        std::map<std::string, std::string> result;
        for (const RootActionMap& action : root_actions_) {
            result.emplace(
                action.fixture_key,
                action.engine.action.stable_key);
        }
        return result;
    }

    aq5::PreparedRoot root_;
    std::shared_ptr<const LearnedModel> parent_;
    std::uint64_t search_seed_ = 0;
    std::uint64_t observation_seed_ = 0;
    LearnedGenerativePosition root_position_;
    LearnedGenerativeObservation
        root_engine_observation_;
    std::vector<RootActionMap> root_actions_;
    puct::Observation root_observation_;
    std::vector<std::vector<TraceStep>> traces_;
    bool bounded_macro_accounting_ = true;
    bool opponent_action_accounting_consistent_ = true;
    bool no_combat_bound_fallback_ = true;
    std::set<std::string>
        opponent_information_set_keys_;
};

const puct::NodeEvidence& root_node(
    const puct::SearchResult& result) {
    const auto found = std::find_if(
        result.nodes.begin(), result.nodes.end(),
        [&](const puct::NodeEvidence& node) {
            return node.information_set_key ==
                   result.root_information_set_key;
        });
    if (found == result.nodes.end()) {
        throw std::logic_error(
            "ISP0 result omitted its root node");
    }
    return *found;
}

const puct::EdgeEvidence& edge_for(
    const puct::NodeEvidence& node,
    std::string_view key) {
    const auto found = std::find_if(
        node.edges.begin(), node.edges.end(),
        [&](const puct::EdgeEvidence& edge) {
            return edge.action_key == key;
        });
    if (found == node.edges.end()) {
        throw std::logic_error(
            "ISP0 result omitted a requested root edge");
    }
    return *found;
}

bool is_counter_fixture(std::string_view stable_id) {
    return stable_id == kRedundantCounterId ||
           stable_id == kInterveningCounterId;
}

bool is_combat_family(aq5::DecisionFamily family) {
    return family == aq5::DecisionFamily::Attack ||
           family == aq5::DecisionFamily::Block;
}

bool successor_value_prior_formula_holds(
    const std::vector<RootActionEvidence>& actions);

bool is_own_counter_target(
    const GameState& state, std::size_t actor,
    const PriorityAction& action) {
    if (action.kind !=
            PriorityActionKind::CastCounterspell ||
        !action.spell_target.has_value()) {
        return false;
    }
    const auto found = std::find_if(
        state.stack.begin(), state.stack.end(),
        [&](const StackObject& object) {
            return object.id == *action.spell_target;
        });
    return found != state.stack.end() &&
           found->controller == actor &&
           found->card == CardId::Counterspell;
}

std::uint64_t puct_fingerprint_update(
    std::uint64_t hash, std::string_view value) {
    constexpr std::uint64_t kPrime =
        UINT64_C(1099511628211);
    const std::uint64_t length =
        static_cast<std::uint64_t>(value.size());
    for (unsigned int shift = 0; shift < 64;
         shift += 8) {
        hash ^=
            (length >> shift) & UINT64_C(0xff);
        hash *= kPrime;
    }
    for (const unsigned char byte : value) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= kPrime;
    }
    return hash;
}

std::uint64_t puct_tie_rank(
    std::uint64_t seed, std::string_view domain,
    std::string_view node_key,
    std::string_view candidate_key) {
    std::uint64_t hash =
        UINT64_C(1469598103934665603);
    hash = puct_fingerprint_update(hash, domain);
    hash = puct_fingerprint_update(hash, node_key);
    hash =
        puct_fingerprint_update(hash, candidate_key);
    return splitmix64(seed ^ hash);
}

bool puct_tie_precedes(
    std::uint64_t seed, std::string_view domain,
    std::string_view node_key, std::string_view left,
    std::string_view right) {
    const std::uint64_t left_rank =
        puct_tie_rank(
            seed, domain, node_key, left);
    const std::uint64_t right_rank =
        puct_tie_rank(
            seed, domain, node_key, right);
    return left_rank != right_rank
               ? left_rank < right_rank
               : left < right;
}

struct PrincipalVariationPath {
    std::vector<std::string> node_keys;
    std::vector<std::optional<std::string>>
        successor_keys;
};

PrincipalVariationPath principal_variation_path(
    const puct::SearchResult& search) {
    PrincipalVariationPath result;
    if (search.principal_variation.empty()) {
        return result;
    }
    std::string node_key =
        search.root_information_set_key;
    result.node_keys.push_back(node_key);
    result.successor_keys.reserve(
        search.principal_variation.size());
    for (std::size_t depth = 0;
         depth < search.principal_variation.size();
         ++depth) {
        const auto node = std::find_if(
            search.nodes.begin(), search.nodes.end(),
            [&](const puct::NodeEvidence& candidate) {
                return candidate.information_set_key ==
                       node_key;
            });
        if (node == search.nodes.end() ||
            node->selected_action_key !=
                search.principal_variation[depth]) {
            throw std::logic_error(
                "ISP0 principal variation does not follow "
                "the selected tree edges");
        }
        const puct::EdgeEvidence& edge =
            edge_for(
                *node,
                search.principal_variation[depth]);
        if (edge.successors.empty()) {
            result.successor_keys.push_back(
                std::nullopt);
            if (depth + 1 <
                search.principal_variation.size()) {
                throw std::logic_error(
                    "ISP0 principal variation continues "
                    "after an edge without a successor");
            }
            continue;
        }
        auto best = edge.successors.begin();
        for (auto candidate =
                 std::next(edge.successors.begin());
             candidate != edge.successors.end();
             ++candidate) {
            if (candidate->transition_count >
                    best->transition_count ||
                (candidate->transition_count ==
                     best->transition_count &&
                 puct_tie_precedes(
                     search.tie_seed,
                     "old-school-isp0-pv-successor-v1",
                     node->information_set_key,
                     candidate->information_set_key,
                     best->information_set_key))) {
                best = candidate;
            }
        }
        node_key = best->information_set_key;
        result.successor_keys.push_back(node_key);
        if (depth + 1 <
            search.principal_variation.size()) {
            result.node_keys.push_back(node_key);
        }
    }
    if (result.node_keys.size() !=
            search.principal_variation.size() ||
        result.successor_keys.size() !=
            search.principal_variation.size()) {
        throw std::logic_error(
            "ISP0 principal variation node path is ragged");
    }
    return result;
}

PrincipalVariationWitness make_pv_witness(
    const puct::SearchResult& search,
    const std::vector<std::vector<TraceStep>>& traces,
    std::size_t root_actor) {
    PrincipalVariationWitness best{
        .actions = search.principal_variation,
        .searched_depth =
            search.principal_variation.size(),
    };
    const PrincipalVariationPath pv_path =
        principal_variation_path(search);
    int best_rank = -1;
    for (const auto& simulation : traces) {
        if (simulation.size() <
            search.principal_variation.size()) {
            continue;
        }
        bool prefix = true;
        for (std::size_t index = 0;
             index < search.principal_variation.size();
             ++index) {
            prefix =
                prefix &&
                simulation[index].safe.action_key ==
                    search.principal_variation[index];
            if (pv_path.successor_keys[index]
                    .has_value()) {
                prefix =
                    prefix &&
                    simulation[index]
                            .safe
                            .successor_information_set_key ==
                        pv_path.successor_keys[index];
            } else {
                prefix =
                    prefix &&
                    !simulation[index]
                         .safe
                         .successor_information_set_key
                         .has_value();
            }
        }
        if (!prefix) {
            continue;
        }
        PrincipalVariationWitness candidate{
            .actions = search.principal_variation,
            .completed_trace = true,
            .searched_depth =
                search.principal_variation.size(),
        };
        for (std::size_t index = 0;
             index < search.principal_variation.size();
             ++index) {
            const TraceStep& step = simulation[index];
            const auto& witness = step.safe.witness;
            if (index + 1 ==
                search.principal_variation.size()) {
                candidate.stack_settled =
                    witness.stack_settled;
            }
            if (witness.exact_combat_completed) {
                candidate.exact_combat_completed = true;
                candidate.exact_combat_completed_plan_count +=
                    witness
                        .exact_combat_completed_plan_count;
                candidate.exact_combat_contains_pure_chump =
                    candidate
                        .exact_combat_contains_pure_chump ||
                    witness
                        .exact_combat_contains_pure_chump;
                candidate.completed_damage_ordered_blocks =
                    witness.completed_damage_ordered_blocks;
            }
            const bool actual_leaf =
                index + 1 ==
                    search.principal_variation.size() &&
                simulation.size() ==
                    search.principal_variation.size();
            if (actual_leaf &&
                step.safe.successor_leaf_evaluation
                    .has_value() &&
                step.safe.successor_leaf_evaluation
                    ->exact_combat_completed) {
                const auto& leaf =
                    *step.safe.successor_leaf_evaluation;
                candidate.exact_combat_completed = true;
                candidate.exact_combat_completed_plan_count +=
                    leaf.exact_combat_completed_plan_count;
                candidate.exact_combat_contains_pure_chump =
                    candidate
                        .exact_combat_contains_pure_chump ||
                    leaf.exact_combat_contains_pure_chump;
                candidate.completed_damage_ordered_blocks =
                    leaf.completed_damage_ordered_blocks;
            }
            if (witness.applied_priority_action.has_value() &&
                witness.applied_priority_action->kind ==
                    PriorityActionKind::CastCounterspell) {
                ++candidate.root_actor_counterspells;
                if (is_own_counter_target(
                        step.truth_before, root_actor,
                        *witness.applied_priority_action)) {
                    ++candidate
                          .root_actor_counters_targeting_own_counter;
                }
            }
        }
        const int rank =
            (candidate.stack_settled ? 4 : 0) +
            (candidate.exact_combat_completed ? 2 : 0) +
            (candidate.completed_trace ? 1 : 0);
        if (rank > best_rank) {
            best = std::move(candidate);
            best_rank = rank;
        }
    }
    return best;
}

bool has_shared_selected_successor(
    const puct::SearchResult& search) {
    const puct::NodeEvidence& root = root_node(search);
    const puct::EdgeEvidence& selected =
        edge_for(root, search.selected_action_key);
    return std::any_of(
        selected.successors.begin(),
        selected.successors.end(),
        [&](const puct::SuccessorEvidence& successor) {
            return successor.transition_count >= 2 &&
                   std::any_of(
                       search.nodes.begin(),
                       search.nodes.end(),
                       [&](const puct::NodeEvidence& node) {
                           return node.information_set_key ==
                                  successor
                                      .information_set_key;
                       });
        });
}

bool exact_root_visit_accounting(
    const puct::SearchResult& search) {
    const puct::NodeEvidence& root = root_node(search);
    const std::size_t edge_visits = std::accumulate(
        root.edges.begin(), root.edges.end(),
        std::size_t{0},
        [](std::size_t sum,
           const puct::EdgeEvidence& edge) {
            return sum + edge.visits;
        });
    return search.root_visits == puct::kSimulationCount &&
           root.visits == puct::kSimulationCount &&
           edge_visits == puct::kSimulationCount &&
           search.accounting.simulations_started ==
               puct::kSimulationCount &&
           search.accounting.simulations_completed ==
               puct::kSimulationCount;
}

bool bounded_tree_accounting(
    const puct::SearchResult& search) {
    const auto& accounting = search.accounting;
    return accounting.node_count == search.nodes.size() &&
           accounting.node_count <=
               puct::kMaximumNodeCount &&
           accounting.expanded_edge_count <=
               puct::kMaximumExpandedEdgeCount &&
           accounting.maximum_depth <=
               puct::kMaximumDecisionPlies &&
           accounting.terminal_leaves +
                   accounting.observation_leaves +
                   accounting.depth_leaves ==
               puct::kSimulationCount;
}

bool finite_positive_normalized(
    const puct::NodeEvidence& root) {
    long double sum = 0.0L;
    for (const auto& edge : root.edges) {
        if (!std::isfinite(edge.prior) ||
            edge.prior <= 0.0 ||
            !std::isfinite(edge.actor_q) ||
            edge.actor_q < 0.0 ||
            edge.actor_q > 1.0) {
            return false;
        }
        sum += static_cast<long double>(edge.prior);
    }
    return std::abs(sum - 1.0L) <= 1.0e-12L;
}

bool root_coverage(
    const aq5::PreparedRoot& root,
    const puct::NodeEvidence& node) {
    std::vector<std::string> expected =
        root.candidate_keys;
    std::vector<std::string> observed;
    observed.reserve(node.edges.size());
    for (const auto& edge : node.edges) {
        observed.push_back(edge.action_key);
    }
    std::sort(expected.begin(), expected.end());
    std::sort(observed.begin(), observed.end());
    return expected == observed &&
           std::adjacent_find(
               observed.begin(), observed.end()) ==
               observed.end();
}

bool nodes_belong_only_to_root_observer(
    const puct::SearchResult& search,
    std::size_t root_actor) {
    return root_actor < 2 &&
           std::all_of(
               search.nodes.begin(), search.nodes.end(),
               [&](const puct::NodeEvidence& node) {
                   return node.actor == root_actor;
               });
}

bool opponent_nodes_absent(
    const puct::SearchResult& search,
    const EngineEnvironment& environment) {
    return std::none_of(
        search.nodes.begin(), search.nodes.end(),
        [&](const puct::NodeEvidence& node) {
            return environment
                .opponent_information_set_keys()
                .contains(node.information_set_key);
        });
}

bool completed_expected_blue_block(
    const PrincipalVariationWitness& witness) {
    return witness.exact_combat_completed &&
           witness.exact_combat_completed_plan_count > 0 &&
           !witness.exact_combat_contains_pure_chump &&
           !witness.completed_damage_ordered_blocks.empty();
}

RootReport run_engine_root(
    const aq5::PreparedRoot& root,
    const std::shared_ptr<const LearnedModel>& parent) {
    ++g_engine_search_invocations;
    const std::uint64_t search_seed =
        root_search_seed(root);
    const std::uint64_t tie_seed =
        root_tie_seed(root);
    EngineEnvironment direct(
        root, parent, search_seed);
    const puct::SearchResult direct_search =
        puct::search(direct, tie_seed);

    EngineEnvironment replay(
        root, parent, search_seed);
    const puct::SearchResult replay_search =
        puct::search(replay, tie_seed);

    const aq5::PreparedRoot reversed_root =
        aq5::reverse_candidate_order(root);
    EngineEnvironment reversed(
        reversed_root, parent, search_seed);
    const puct::SearchResult reversed_search =
        puct::search(reversed, tie_seed);

    const aq5::PreparedRoot hidden_root =
        aq5::make_hidden_repartition_clone(root);
    EngineEnvironment hidden(
        hidden_root, parent, search_seed);
    const puct::SearchResult hidden_search =
        puct::search(hidden, tie_seed);

    const puct::NodeEvidence& direct_root =
        root_node(direct_search);
    RootReport report{
        .stable_id = root.stable_id,
        .family = root.family,
        .expected_key =
            expected_fixture_choice(root.stable_id),
        .selected_key =
            direct_search.selected_action_key,
        .accounting = direct_search.accounting,
        .principal_variation =
            make_pv_witness(
                direct_search, direct.traces(),
                root.actor),
    };
    report.actions.reserve(
        direct.root_actions().size());
    for (const RootActionMap& action :
         direct.root_actions()) {
        const puct::EdgeEvidence& edge =
            edge_for(
                direct_root, action.fixture_key);
        report.actions.push_back({
            .fixture_key = action.fixture_key,
            .engine_key =
                action.engine.action.stable_key,
            .successor_value =
                action.engine.successor_value,
            .prior = edge.prior,
            .visits = edge.visits,
            .actor_q = edge.actor_q,
        });
    }

    report.strategic_direction_passed =
        fixture_direction_passed(
            root.stable_id, report.selected_key);
    report.complete_legal_choice_coverage =
        root_coverage(root, direct_root);
    report.finite_positive_normalized_priors =
        finite_positive_normalized(direct_root);
    report.exact_successor_value_prior_formula =
        successor_value_prior_formula_holds(
            report.actions);
    report.root_visit_accounting_exact =
        exact_root_visit_accounting(direct_search);
    report.bounded_tree_accounting =
        bounded_tree_accounting(direct_search);
    report.bounded_macro_accounting =
        direct.bounded_macro_accounting();
    report.opponent_action_accounting_consistent =
        direct.opponent_action_accounting_consistent();
    report.no_combat_bound_fallback =
        direct.no_combat_bound_fallback();
    const std::uint64_t first_transition_seed =
        transition_seed(search_seed, 0, 0);
    const std::uint64_t replay_transition_seed =
        transition_seed(search_seed, 0, 0);
    report.transition_seed_candidate_independent =
        first_transition_seed ==
            replay_transition_seed &&
        first_transition_seed !=
            transition_seed(search_seed, 0, 1) &&
        first_transition_seed !=
            transition_seed(search_seed, 1, 0);
    report.deterministic_replay_bit_identical =
        direct_search == replay_search &&
        direct.safe_traces() == replay.safe_traces();
    report.reversed_input_full_evidence_bit_identical =
        direct_search == reversed_search &&
        direct.safe_traces() == reversed.safe_traces();
    report.hidden_repartition_nonvacuous =
        root.state != hidden_root.state &&
        observe_game_state(root.state, root.actor) ==
            observe_game_state(
                hidden_root.state, root.actor);
    report.hidden_root_observation_bit_identical =
        direct.root_observation() ==
        hidden.root_observation();
    report.hidden_full_evidence_bit_identical =
        direct_search == hidden_search &&
        direct.safe_traces() == hidden.safe_traces();
    report.root_observer_only_nodes =
        nodes_belong_only_to_root_observer(
            direct_search, root.actor);
    report.opponent_nodes_absent =
        opponent_nodes_absent(direct_search, direct);

    const LearnedGenerativePosition original_position =
        make_root_position(root);
    const GameState before_truth =
        original_position.truth;
    const GameState redeterminized =
        learned_generative_actor_determinization(
            original_position,
            indexed_seed(
                search_seed,
                UINT64_C(0x524544455445524d)));
    report.actor_hand_preserved =
        redeterminized.players[root.actor].hand ==
        before_truth.players[root.actor].hand;
    report.public_state_preserved =
        observe_game_state(redeterminized, root.actor) ==
        observe_game_state(before_truth, root.actor);
    report.truth_immutable_during_redeterminization =
        original_position.truth == before_truth;
    LearnedGenerativePosition sampled_position =
        original_position;
    sampled_position.truth = redeterminized;
    const auto sampled_observation =
        observe_learned_generative_position(
            sampled_position, parent,
            indexed_seed(
                search_seed,
                UINT64_C(0x4c4547414c534947)));
    const auto original_observation =
        observe_learned_generative_position(
            original_position, parent,
            indexed_seed(
                search_seed,
                UINT64_C(0x4c4547414c534947)));
    report.legal_signature_preserved =
        sampled_observation.legal_signature ==
            original_observation.legal_signature &&
        sampled_observation.information_set_key ==
            original_observation.information_set_key;

    report.required_shared_successor =
        !is_counter_fixture(root.stable_id) ||
        (direct_search.accounting.maximum_depth >= 2 &&
         has_shared_selected_successor(direct_search));
    report.required_counter_principal_variation =
        root.stable_id != kInterveningCounterId ||
        (report.principal_variation.completed_trace &&
         report.principal_variation.searched_depth >= 2 &&
         report.principal_variation
                 .root_actor_counterspells == 1 &&
         report.principal_variation
                 .root_actor_counters_targeting_own_counter ==
             0 &&
         report.principal_variation.stack_settled);
    report.required_exact_combat_completion =
        !is_combat_family(root.family) ||
        (root.stable_id ==
                 aq5::kNewBlueBlockFixtureId
             ? completed_expected_blue_block(
                   report.principal_variation)
             : report.principal_variation
                       .exact_combat_completed &&
                   report.principal_variation
                           .exact_combat_completed_plan_count >
                       0);
    if (is_combat_family(root.family)) {
        const auto leaf =
            evaluate_learned_generative_leaf(
                original_position, root.actor, parent,
                indexed_seed(
                    search_seed,
                    UINT64_C(0x5041525449414c4c)));
        report.partial_combat_leaf_completed_exactly =
            leaf.exact_combat_completed &&
            leaf.exact_combat_completed_plan_count > 0;
    } else {
        report.partial_combat_leaf_completed_exactly =
            true;
    }
    return report;
}

bool swap_private_hand_and_library_card(
    PlayerState& player) {
    for (std::size_t hand = 0;
         hand < player.hand.size(); ++hand) {
        for (std::size_t library = 0;
             library < player.library.size(); ++library) {
            if (player.hand[hand] ==
                player.library[library]) {
                continue;
            }
            std::swap(
                player.hand[hand],
                player.library[library]);
            return true;
        }
    }
    return false;
}

std::string pass_engine_key(
    const aq5::PreparedRoot& root,
    const LearnedGenerativeObservation& observation) {
    const auto mapping =
        map_root_actions(root, observation);
    const auto pass = std::find_if(
        mapping.begin(), mapping.end(),
        [](const RootActionMap& action) {
            return action.fixture_key == "pass";
        });
    if (pass == mapping.end()) {
        throw std::logic_error(
            "ISP0 noninterference control has no Pass");
    }
    return pass->engine.action.stable_key;
}

OpponentNoninterferenceReport
check_engine_opponent_noninterference(
    const std::vector<aq5::PreparedRoot>& roots,
    const std::shared_ptr<const LearnedModel>& parent) {
    const auto selected_root = std::find_if(
        roots.begin(), roots.end(),
        [](const aq5::PreparedRoot& root) {
            return root.stable_id ==
                   kRedundantCounterId;
        });
    if (selected_root == roots.end()) {
        throw std::logic_error(
            "ISP0 opponent control root is absent");
    }
    const aq5::PreparedRoot& root = *selected_root;
    LearnedGenerativePosition original =
        make_root_position(root);
    LearnedGenerativePosition carrier = original;
    const std::size_t private_player = root.actor;
    if (!swap_private_hand_and_library_card(
            carrier.truth.players[private_player])) {
        throw std::logic_error(
            "ISP0 opponent carrier repartition is vacuous");
    }
    const std::size_t opponent = 1 - private_player;
    const bool opponent_observation_identity =
        observe_game_state(
            original.truth, opponent) ==
        observe_game_state(
            carrier.truth, opponent);
    const std::uint64_t seed =
        indexed_seed(
            root_search_seed(root),
            UINT64_C(0x4f50504e4f4e494e));
    const auto root_observation =
        observe_learned_generative_position(
            original, parent,
            indexed_seed(
                seed, UINT64_C(0x524f4f544f425356)));
    const std::string pass =
        pass_engine_key(root, root_observation);
    const LearnedGenerativeTransition first =
        advance_learned_generative_position(
            original, pass, parent, seed, true);
    const LearnedGenerativeTransition second =
        advance_learned_generative_position(
            carrier, pass, parent, seed, true);
    if (first.witness.opponent_decisions.empty() ||
        second.witness.opponent_decisions.empty()) {
        throw std::logic_error(
            "ISP0 opponent control exposed no opponent "
            "decision");
    }
    const auto& left =
        first.witness.opponent_decisions.front();
    const auto& right =
        second.witness.opponent_decisions.front();
    const bool finite_same_width_scores =
        !left.actions.empty() &&
        left.actions.size() == right.actions.size() &&
        std::all_of(
            left.actions.begin(), left.actions.end(),
            [](const auto& action) {
                return std::isfinite(
                    action.successor_value);
            }) &&
        std::all_of(
            right.actions.begin(), right.actions.end(),
            [](const auto& action) {
                return std::isfinite(
                    action.successor_value);
            });
    const auto selected_count =
        [](const auto& witness) {
            return static_cast<std::size_t>(
                std::count_if(
                    witness.actions.begin(),
                    witness.actions.end(),
                    [&](const auto& action) {
                        return action.action.stable_key ==
                               witness
                                   .selected_stable_key;
                    }));
        };
    const bool selected_is_real =
        !left.selected_stable_key.empty() &&
        !right.selected_stable_key.empty() &&
        selected_count(left) == 1 &&
        selected_count(right) == 1;
    OpponentNoninterferenceReport report{
        .fixture_id = root.stable_id,
        .scored_action_count = left.actions.size(),
        .selected_action_membership_count =
            selected_count(left),
        .nonvacuous_private_repartition =
            original.truth != carrier.truth,
        .opponent_observation_bit_identical =
            opponent_observation_identity &&
            left.information_set_key ==
                right.information_set_key,
        .legal_signature_bit_identical =
            left.legal_signature ==
            right.legal_signature,
        .c16_scores_bit_identical =
            finite_same_width_scores &&
            left.actions == right.actions,
        .selected_action_bit_identical =
            selected_is_real &&
            left.selected_stable_key ==
                right.selected_stable_key &&
            left.search_seed == right.search_seed &&
            left.tie_seed == right.tie_seed,
        .local_accounting_bit_identical =
            first.actions_applied ==
                second.actions_applied &&
            first.phase_transitions ==
                second.phase_transitions &&
            first.turn_advances ==
                second.turn_advances &&
            first.witness.opponent_decisions_applied ==
                second.witness
                    .opponent_decisions_applied,
        // The generative seam rolls this decision outside the
        // Environment/Puct boundary. assemble_preflight also ANDs
        // this with every root's explicit tree-node exclusion.
        .no_shared_opponent_node_or_q_update = true,
    };
    return report;
}

struct FixedGameWitness {
    GameResult result;
    std::vector<GameState> trace;
    GameState final_state;

    bool operator==(const FixedGameWitness&) const = default;
};

FixedGameWitness run_default_off_fixed_game(
    const std::shared_ptr<const LearnedModel>& parent) {
    BotConfig c16{
        .kind = BotKind::Learned,
        .learned_variant =
            LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = 2,
        .exploration_rate = 0.0,
        .value_continuation_epsilon = 0.0,
        .value_priority_residual_weight = 0.0,
        .value_pass_dominance = false,
        .value_resolved_shallow_prior_weight = 0.0,
        .value_adversarial_blocks = false,
        .value_actor_local_search = false,
        .value_recursive_policy_improvement = false,
        .value_continuation_controller =
            LearnedContinuationController::Legacy,
        .training_games = 800,
        .learned_model = parent,
    };
    GameConfig config{
        .max_turns = 12,
        .starting_player = 0,
        .bots = {c16, c16},
        .learned_training_seed = 424242,
        .learned_model = parent,
    };
    Game game(
        ru_aggro_deck(), ru_aggro_deck(),
        UINT64_C(0x495350304f464630), config);
    FixedGameWitness witness;
    witness.result =
        game.run_with_trace(witness.trace);
    witness.final_state = game.state();
    return witness;
}

std::string_view family_name(
    aq5::DecisionFamily family) {
    switch (family) {
    case aq5::DecisionFamily::Priority:
        return "Priority";
    case aq5::DecisionFamily::Attack:
        return "Attack";
    case aq5::DecisionFamily::Block:
        return "Block";
    }
    return "Invalid";
}

bool successor_value_prior_formula_holds(
    const std::vector<RootActionEvidence>& actions) {
    if (actions.empty()) {
        return false;
    }
    double maximum =
        -std::numeric_limits<double>::infinity();
    for (const auto& action : actions) {
        if (!std::isfinite(action.successor_value) ||
            action.successor_value < 0.0 ||
            action.successor_value > 1.0) {
            return false;
        }
        maximum =
            std::max(
                maximum, action.successor_value);
    }
    std::vector<double> weights;
    weights.reserve(actions.size());
    double weight_sum = 0.0;
    for (const auto& action : actions) {
        const double weight =
            1.0e-6 +
            std::exp(
                action.successor_value - maximum);
        if (!std::isfinite(weight) || weight <= 0.0) {
            return false;
        }
        weights.push_back(weight);
        weight_sum += weight;
    }
    if (!std::isfinite(weight_sum) ||
        weight_sum <= 0.0) {
        return false;
    }
    const double tolerance =
        64.0 * std::numeric_limits<double>::epsilon();
    for (std::size_t index = 0;
         index < actions.size(); ++index) {
        const double expected =
            weights[index] / weight_sum;
        if (!std::isfinite(actions[index].prior) ||
            std::abs(actions[index].prior - expected) >
                tolerance) {
            return false;
        }
    }
    return true;
}

bool valid_root_action_evidence(
    const RootReport& report) {
    if (report.actions.empty()) {
        return false;
    }
    std::vector<std::string> keys;
    keys.reserve(report.actions.size());
    std::size_t visit_sum = 0;
    long double prior_sum = 0.0L;
    std::size_t selected_count = 0;
    for (const RootActionEvidence& action :
         report.actions) {
        if (action.fixture_key.empty() ||
            action.engine_key.empty() ||
            !std::isfinite(action.successor_value) ||
            action.successor_value < 0.0 ||
            action.successor_value > 1.0 ||
            !std::isfinite(action.prior) ||
            action.prior <= 0.0 ||
            !std::isfinite(action.actor_q) ||
            action.actor_q < 0.0 ||
            action.actor_q > 1.0) {
            return false;
        }
        keys.push_back(action.fixture_key);
        visit_sum += action.visits;
        prior_sum +=
            static_cast<long double>(action.prior);
        if (action.fixture_key ==
            report.selected_key) {
            ++selected_count;
        }
    }
    std::sort(keys.begin(), keys.end());
    return selected_count == 1 &&
           std::adjacent_find(
               keys.begin(), keys.end()) == keys.end() &&
           visit_sum == puct::kSimulationCount &&
           std::abs(prior_sum - 1.0L) <= 1.0e-12L &&
           successor_value_prior_formula_holds(
               report.actions) &&
           !report.principal_variation.actions.empty() &&
           report.principal_variation.actions.front() ==
               report.selected_key;
}

} // namespace

bool RootReport::gate_passed() const {
    return !stable_id.empty() &&
           !expected_key.empty() &&
           selected_key == expected_key &&
           valid_root_action_evidence(*this) &&
           strategic_direction_passed &&
           complete_legal_choice_coverage &&
           finite_positive_normalized_priors &&
           exact_successor_value_prior_formula &&
           root_visit_accounting_exact &&
           bounded_tree_accounting &&
           bounded_macro_accounting &&
           opponent_action_accounting_consistent &&
           no_combat_bound_fallback &&
           transition_seed_candidate_independent &&
           deterministic_replay_bit_identical &&
           reversed_input_full_evidence_bit_identical &&
           hidden_repartition_nonvacuous &&
           hidden_root_observation_bit_identical &&
           hidden_full_evidence_bit_identical &&
           root_observer_only_nodes &&
           opponent_nodes_absent &&
           actor_hand_preserved &&
           public_state_preserved &&
           truth_immutable_during_redeterminization &&
           legal_signature_preserved &&
           required_shared_successor &&
           required_counter_principal_variation &&
           required_exact_combat_completion &&
           partial_combat_leaf_completed_exactly;
}

bool OpponentNoninterferenceReport::gate_passed() const {
    return !fixture_id.empty() &&
           scored_action_count > 0 &&
           selected_action_membership_count == 1 &&
           nonvacuous_private_repartition &&
           opponent_observation_bit_identical &&
           legal_signature_bit_identical &&
           c16_scores_bit_identical &&
           selected_action_bit_identical &&
           local_accounting_bit_identical &&
           no_shared_opponent_node_or_q_update;
}

bool IsolationReport::gate_passed() const {
    return exact_parent &&
           exact_configuration &&
           evaluation_only_default_off &&
           default_off_fixed_game_bit_identical;
}

bool PreflightReport::gate_passed() const {
    return seed == kPreflightSeed &&
           parent_fingerprint ==
               kRequiredParentFingerprint &&
           roots.size() == aq5::kFixtureRootCount &&
           all_twelve_roots_present_once &&
           all_three_decision_families_present &&
           std::all_of(
               roots.begin(), roots.end(),
               [](const RootReport& root) {
                   return root.gate_passed();
               }) &&
           opponent_noninterference.gate_passed() &&
           isolation.gate_passed() &&
           hypothesis_passed;
}

std::string expected_fixture_choice(
    std::string_view stable_id) {
    if (stable_id == kRedundantCounterId ||
        stable_id == kSickBearGrowthId ||
        stable_id == kFiveOpenForceSpikeId) {
        return "pass";
    }
    if (stable_id == kInterveningCounterId) {
        return "counter-opponent-counterspell";
    }
    if (stable_id == kBraingeyserId) {
        return "braingeyser-x1-self";
    }
    if (stable_id == kAncestralId) {
        return "ancestral-self";
    }
    if (stable_id == kOpponentGrowthId) {
        return "growth-own-ironroot-treefolk";
    }
    if (stable_id == kLiveForceSpikeId) {
        return "force-spike-gray-ogre";
    }
    if (stable_id == kLife20BlockId) {
        return "no-blocks";
    }
    if (stable_id == kLife4BlockId) {
        return "block-air-elemental-with-flying-men";
    }
    if (stable_id == kAttackId) {
        return "no-attack";
    }
    if (stable_id ==
        aq5::kNewBlueBlockFixtureId) {
        return "no-block";
    }
    throw std::invalid_argument(
        "ISP0 fixture has no predeclared expected choice");
}

bool fixture_direction_passed(
    std::string_view stable_id,
    std::string_view selected_key) {
    return selected_key ==
           expected_fixture_choice(stable_id);
}

std::uint64_t transition_seed(
    std::uint64_t root_seed,
    std::size_t simulation_index,
    std::size_t searched_decision_ply) {
    return indexed_seed(
        root_seed, UINT64_C(0x454447455452414e),
        simulation_index, searched_decision_ply);
}

PreflightReport assemble_preflight(
    std::string parent_fingerprint,
    const PreflightApi& api) {
    if (!api.run_root ||
        !api.check_opponent_noninterference ||
        !api.check_default_off_identity) {
        throw std::invalid_argument(
            "ISP0 preflight API is incomplete");
    }
    const std::vector<aq5::PreparedRoot> fixtures =
        aq5::build_fixture_roots();
    PreflightReport report{
        .parent_fingerprint =
            std::move(parent_fingerprint),
    };
    report.roots.reserve(fixtures.size());
    for (const aq5::PreparedRoot& fixture : fixtures) {
        RootReport root = api.run_root(fixture);
        if (root.stable_id != fixture.stable_id ||
            root.family != fixture.family ||
            root.expected_key !=
                expected_fixture_choice(
                    fixture.stable_id)) {
            throw std::logic_error(
                "ISP0 root runner changed fixture identity");
        }
        report.roots.push_back(std::move(root));
    }
    report.opponent_noninterference =
        api.check_opponent_noninterference(fixtures);
    report.opponent_noninterference
        .no_shared_opponent_node_or_q_update =
        report.opponent_noninterference
            .no_shared_opponent_node_or_q_update &&
        std::all_of(
            report.roots.begin(), report.roots.end(),
            [](const RootReport& root) {
                return root.root_observer_only_nodes &&
                       root.opponent_nodes_absent;
            });
    const bool default_off_identity =
        api.check_default_off_identity();
    report.isolation = {
        .exact_parent =
            report.parent_fingerprint ==
            kRequiredParentFingerprint,
        .exact_configuration =
            puct::kSimulationCount == 64 &&
            puct::kMaximumDecisionPlies == 8 &&
            puct::kMaximumNodeCount == 513 &&
            puct::kMaximumExpandedEdgeCount == 512 &&
            puct::kExplorationConstant == 1.0,
        .evaluation_only_default_off =
            default_off_identity,
        .default_off_fixed_game_bit_identical =
            default_off_identity,
    };

    std::set<std::string> ids;
    std::array<bool, 3> families{};
    for (const RootReport& root : report.roots) {
        ids.insert(root.stable_id);
        const std::size_t family =
            static_cast<std::size_t>(root.family);
        if (family < families.size()) {
            families[family] = true;
        }
    }
    report.all_twelve_roots_present_once =
        report.roots.size() ==
            aq5::kFixtureRootCount &&
        ids.size() == aq5::kFixtureRootCount;
    report.all_three_decision_families_present =
        std::all_of(
            families.begin(), families.end(),
            [](bool present) { return present; });
    report.hypothesis_passed =
        std::all_of(
            report.roots.begin(), report.roots.end(),
            [](const RootReport& root) {
                return root.strategic_direction_passed;
            });
    return report;
}

PreflightReport run_preflight(
    std::shared_ptr<const LearnedModel> parent) {
    if (!parent ||
        learned_model_fingerprint(parent) !=
            kRequiredParentFingerprint) {
        throw std::invalid_argument(
            "ISP0 preflight requires exact frozen C16");
    }
    const FixedGameWitness default_off_before =
        run_default_off_fixed_game(parent);
    PreflightApi api{
        .run_root =
            [parent](
                const aq5::PreparedRoot& root) {
                return run_engine_root(root, parent);
            },
        .check_opponent_noninterference =
            [parent](
                const std::vector<aq5::PreparedRoot>&
                    roots) {
                return check_engine_opponent_noninterference(
                    roots, parent);
            },
        .check_default_off_identity =
            [parent, default_off_before]() {
                const std::size_t invocations_before_game =
                    g_engine_search_invocations;
                const FixedGameWitness after =
                    run_default_off_fixed_game(parent);
                return after == default_off_before &&
                       g_engine_search_invocations ==
                           invocations_before_game;
            },
    };
    return assemble_preflight(
        learned_model_fingerprint(parent), api);
}

void print_report(
    const PreflightReport& report,
    std::ostream& output) {
    output << std::fixed << std::setprecision(6);
    output
        << "AQ7-ISP0 information-set PUCT preflight\n"
        << "  seed: " << report.seed << '\n'
        << "  parent: " << report.parent_fingerprint
        << '\n';
    for (const RootReport& root : report.roots) {
        output
            << "  " << family_name(root.family)
            << ' ' << root.stable_id
            << " selected=" << root.selected_key
            << " expected=" << root.expected_key
            << " direction="
            << (root.strategic_direction_passed
                    ? "PASS"
                    : "FAIL")
            << " gate="
            << (root.gate_passed() ? "PASS" : "FAIL")
            << " nodes=" << root.accounting.node_count
            << " edges="
            << root.accounting.expanded_edge_count
            << " depth="
            << root.accounting.maximum_depth
            << " pv=" << root.principal_variation.actions.size()
            << '\n';
        for (const RootActionEvidence& action :
             root.actions) {
            output
                << "    " << action.fixture_key
                << " visits=" << action.visits
                << " q=" << action.actor_q
                << " prior=" << action.prior
                << " successor="
                << action.successor_value << '\n';
        }
        if (!root.gate_passed()) {
            output
                << "    invariants"
                << " coverage="
                << root.complete_legal_choice_coverage
                << " prior="
                << root.finite_positive_normalized_priors
                << " accounting="
                << root.root_visit_accounting_exact
                << " replay="
                << root.deterministic_replay_bit_identical
                << " reverse="
                << root
                       .reversed_input_full_evidence_bit_identical
                << " hidden="
                << root.hidden_full_evidence_bit_identical
                << " successor="
                << root.required_shared_successor
                << " counter-pv="
                << root.required_counter_principal_variation
                << " combat="
                << root.required_exact_combat_completion
                << '\n';
        }
    }
    output
        << "  opponent-noninterference: "
        << (report.opponent_noninterference.gate_passed()
                ? "PASS"
                : "FAIL")
        << '\n'
        << "  isolation: "
        << (report.isolation.gate_passed()
                ? "PASS"
                : "FAIL")
        << '\n'
        << "result="
        << (report.gate_passed() ? "PASS" : "REJECT")
        << " hypothesis_passed="
        << (report.hypothesis_passed ? 1 : 0)
        << " web_licensed=0 artifact_published=0\n";
}

} // namespace old_school::information_set_puct_preflight
