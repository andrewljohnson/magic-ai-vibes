#include "old_school/information_set_puct.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace old_school::information_set_puct {
namespace {

constexpr long double kPriorTolerance = 1.0e-12L;

struct Edge {
    std::string action_key;
    double prior = 0.0;
    std::size_t visits = 0;
    long double actor_value_sum = 0.0L;
    bool expanded = false;
    std::size_t terminal_transitions = 0;
    std::map<std::string, std::size_t> successors;
};

struct Node {
    std::string information_set_key;
    std::string legal_signature;
    std::uint8_t actor = 0;
    double fpu_leaf_value = 0.5;
    std::size_t visits = 0;
    std::vector<Edge> edges;
};

struct PathStep {
    std::size_t node_index = 0;
    std::size_t edge_index = 0;
};

struct CanonicalObservation {
    std::string information_set_key;
    std::string legal_signature;
    std::uint8_t actor = 0;
    std::vector<ActionPrior> actions;
    double fpu_leaf_value = 0.5;
};

[[noreturn]] void fail(
    FailureCode code,
    std::string message) {
    throw SearchFailure(code, std::move(message));
}

CanonicalObservation canonicalize(
    const Observation& observation) {
    if (observation.information_set_key.empty()) {
        fail(
            FailureCode::InvalidObservation,
            "ISP0 observation has an empty information-set key");
    }
    if (observation.legal_signature.empty()) {
        fail(
            FailureCode::InvalidObservation,
            "ISP0 observation has an empty legal signature");
    }
    if (observation.actor > 1) {
        fail(
            FailureCode::InvalidObservation,
            "ISP0 observation actor is not player zero or one");
    }
    if (!std::isfinite(observation.fpu_leaf_value) ||
        observation.fpu_leaf_value < 0.0 ||
        observation.fpu_leaf_value > 1.0) {
        fail(
            FailureCode::InvalidObservation,
            "ISP0 observation FPU/leaf value is invalid");
    }
    if (observation.actions.empty()) {
        fail(
            FailureCode::InvalidObservation,
            "ISP0 observation has no legal action");
    }

    CanonicalObservation result{
        .information_set_key =
            observation.information_set_key,
        .legal_signature = observation.legal_signature,
        .actor = observation.actor,
        .actions = observation.actions,
        .fpu_leaf_value =
            observation.fpu_leaf_value,
    };
    std::sort(
        result.actions.begin(),
        result.actions.end(),
        [](const ActionPrior& left,
           const ActionPrior& right) {
            return left.action_key < right.action_key;
        });

    long double prior_sum = 0.0L;
    std::string_view previous;
    bool have_previous = false;
    for (const ActionPrior& action : result.actions) {
        if (action.action_key.empty()) {
            fail(
                FailureCode::InvalidObservation,
                "ISP0 observation has an empty action key");
        }
        if (have_previous &&
            action.action_key == previous) {
            fail(
                FailureCode::InvalidObservation,
                "ISP0 observation has duplicate action keys");
        }
        if (!std::isfinite(action.prior) ||
            action.prior <= 0.0) {
            fail(
                FailureCode::InvalidObservation,
                "ISP0 action prior is not finite and positive");
        }
        prior_sum +=
            static_cast<long double>(action.prior);
        if (!std::isfinite(prior_sum)) {
            fail(
                FailureCode::InvalidObservation,
                "ISP0 action-prior sum is non-finite");
        }
        previous = action.action_key;
        have_previous = true;
    }
    if (std::abs(prior_sum - 1.0L) >
        kPriorTolerance) {
        fail(
            FailureCode::InvalidObservation,
            "ISP0 action priors are not normalized");
    }
    return result;
}

Node make_node(
    const CanonicalObservation& observation) {
    Node node{
        .information_set_key =
            observation.information_set_key,
        .legal_signature = observation.legal_signature,
        .actor = observation.actor,
        .fpu_leaf_value =
            observation.fpu_leaf_value,
    };
    node.edges.reserve(observation.actions.size());
    for (const ActionPrior& action :
         observation.actions) {
        node.edges.push_back({
            .action_key = action.action_key,
            .prior = action.prior,
        });
    }
    return node;
}

void require_same_observation(
    const Node& node,
    const CanonicalObservation& observation) {
    if (node.actor != observation.actor ||
        node.legal_signature !=
            observation.legal_signature) {
        fail(
            FailureCode::NodeCollision,
            "ISP0 information-set key collided across "
            "actor or legal signature");
    }
    if (node.fpu_leaf_value !=
            observation.fpu_leaf_value ||
        node.edges.size() != observation.actions.size()) {
        fail(
            FailureCode::NodeCollision,
            "ISP0 information-set key collided across "
            "hidden-safe value or legal actions");
    }
    for (std::size_t action = 0;
         action < node.edges.size(); ++action) {
        if (node.edges[action].action_key !=
                observation.actions[action].action_key ||
            node.edges[action].prior !=
                observation.actions[action].prior) {
            fail(
                FailureCode::NodeCollision,
                "ISP0 information-set key collided across "
                "action keys or priors");
        }
    }
}

std::uint64_t fnv_update(
    std::uint64_t hash,
    std::string_view value) {
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

std::uint64_t tie_rank(
    std::uint64_t seed,
    std::string_view domain,
    std::string_view node_key,
    std::string_view candidate_key) {
    std::uint64_t hash =
        UINT64_C(1469598103934665603);
    hash = fnv_update(hash, domain);
    hash = fnv_update(hash, node_key);
    hash = fnv_update(hash, candidate_key);
    return splitmix64(seed ^ hash);
}

bool stable_tie_precedes(
    std::uint64_t seed,
    std::string_view domain,
    std::string_view node_key,
    std::string_view left,
    std::string_view right) {
    const std::uint64_t left_rank =
        tie_rank(seed, domain, node_key, left);
    const std::uint64_t right_rank =
        tie_rank(seed, domain, node_key, right);
    if (left_rank != right_rank) {
        return left_rank < right_rank;
    }
    return left < right;
}

long double edge_actor_q(
    const Node& node,
    const Edge& edge) {
    if (edge.visits == 0) {
        return static_cast<long double>(
            node.fpu_leaf_value);
    }
    return edge.actor_value_sum /
           static_cast<long double>(edge.visits);
}

std::size_t select_puct_edge(
    const Node& node,
    std::uint64_t tie_seed) {
    if (node.edges.empty()) {
        fail(
            FailureCode::InvalidObservation,
            "ISP0 attempted to select an empty node");
    }

    const long double parent_scale = std::sqrt(
        static_cast<long double>(node.visits));
    std::size_t best = 0;
    long double best_score =
        -std::numeric_limits<long double>::infinity();
    for (std::size_t index = 0;
         index < node.edges.size(); ++index) {
        const Edge& edge = node.edges[index];
        const long double exploration =
            static_cast<long double>(
                kExplorationConstant) *
            static_cast<long double>(edge.prior) *
            parent_scale /
            static_cast<long double>(edge.visits + 1);
        const long double score =
            edge_actor_q(node, edge) + exploration;
        if (!std::isfinite(score)) {
            fail(
                FailureCode::InvalidObservation,
                "ISP0 PUCT score is non-finite");
        }
        if (score > best_score ||
            (score == best_score &&
             stable_tie_precedes(
                 tie_seed,
                 "old-school-isp0-puct-edge-v1",
                 node.information_set_key,
                 edge.action_key,
                 node.edges[best].action_key))) {
            best = index;
            best_score = score;
        }
    }
    return best;
}

std::size_t select_evidence_edge(
    const Node& node,
    std::uint64_t tie_seed,
    std::string_view domain) {
    std::size_t best = 0;
    for (std::size_t index = 1;
         index < node.edges.size(); ++index) {
        const Edge& candidate = node.edges[index];
        const Edge& incumbent = node.edges[best];
        const long double candidate_q =
            edge_actor_q(node, candidate);
        const long double incumbent_q =
            edge_actor_q(node, incumbent);
        if (candidate.visits > incumbent.visits ||
            (candidate.visits == incumbent.visits &&
             (candidate_q > incumbent_q ||
              (candidate_q == incumbent_q &&
               stable_tie_precedes(
                   tie_seed,
                   domain,
                   node.information_set_key,
                   candidate.action_key,
                   incumbent.action_key))))) {
            best = index;
        }
    }
    return best;
}

double player_zero_leaf_value(
    const CanonicalObservation& observation) {
    return observation.actor == 0
               ? observation.fpu_leaf_value
               : 1.0 - observation.fpu_leaf_value;
}

double player_zero_terminal_value(
    const Terminal& terminal) {
    if (!terminal.winner.has_value()) {
        return 0.5;
    }
    if (*terminal.winner > 1) {
        fail(
            FailureCode::InvalidTerminal,
            "ISP0 terminal winner is not player zero or one");
    }
    return *terminal.winner == 0 ? 1.0 : 0.0;
}

void backup(
    std::vector<Node>& nodes,
    const std::vector<std::size_t>& visited_nodes,
    const std::vector<PathStep>& path,
    double player_zero_value) {
    if (!std::isfinite(player_zero_value) ||
        player_zero_value < 0.0 ||
        player_zero_value > 1.0) {
        fail(
            FailureCode::InvalidObservation,
            "ISP0 backup value is invalid");
    }
    for (const std::size_t node_index :
         visited_nodes) {
        ++nodes[node_index].visits;
    }
    for (const PathStep& step : path) {
        Node& node = nodes[step.node_index];
        Edge& edge = node.edges[step.edge_index];
        const double actor_value =
            node.actor == 0
                ? player_zero_value
                : 1.0 - player_zero_value;
        ++edge.visits;
        edge.actor_value_sum +=
            static_cast<long double>(actor_value);
    }
}

std::size_t sum_edge_visits(const Node& node) {
    return std::accumulate(
        node.edges.begin(),
        node.edges.end(),
        std::size_t{0},
        [](std::size_t total, const Edge& edge) {
            return total + edge.visits;
        });
}

std::vector<std::string> make_principal_variation(
    const std::vector<Node>& nodes,
    const std::unordered_map<std::string, std::size_t>&
        node_indices,
    std::size_t root_index,
    std::uint64_t tie_seed) {
    std::vector<std::string> result;
    std::unordered_set<std::string> seen;
    std::size_t node_index = root_index;
    for (std::size_t depth = 0;
         depth < kMaximumDecisionPlies; ++depth) {
        const Node& node = nodes[node_index];
        if (!seen.insert(node.information_set_key).second) {
            break;
        }
        const std::size_t edge_index =
            select_evidence_edge(
                node,
                tie_seed,
                depth == 0
                    ? "old-school-isp0-root-choice-v1"
                    : "old-school-isp0-node-choice-v1");
        const Edge& edge = node.edges[edge_index];
        if (edge.visits == 0) {
            break;
        }
        result.push_back(edge.action_key);
        if (edge.successors.empty()) {
            break;
        }

        auto best_successor =
            edge.successors.begin();
        for (auto candidate =
                 std::next(edge.successors.begin());
             candidate != edge.successors.end();
             ++candidate) {
            if (candidate->second >
                    best_successor->second ||
                (candidate->second ==
                     best_successor->second &&
                 stable_tie_precedes(
                     tie_seed,
                     "old-school-isp0-pv-successor-v1",
                     node.information_set_key,
                     candidate->first,
                     best_successor->first))) {
                best_successor = candidate;
            }
        }
        const auto found =
            node_indices.find(best_successor->first);
        if (found == node_indices.end()) {
            break;
        }
        node_index = found->second;
    }
    return result;
}

} // namespace

SearchFailure::SearchFailure(
    FailureCode code,
    std::string message)
    : std::runtime_error(std::move(message)),
      code_(code) {}

FailureCode SearchFailure::code() const noexcept {
    return code_;
}

SearchResult search(
    Environment& environment,
    std::uint64_t tie_seed) {
    return search(
        environment, tie_seed, kSimulationCount);
}

SearchResult search(
    Environment& environment,
    std::uint64_t tie_seed,
    std::size_t simulation_count) {
    if (simulation_count == 0 ||
        simulation_count > kMaximumSimulationCount) {
        fail(
            FailureCode::InvalidSimulationCount,
            "ISP0 simulation count is outside [1, 512]");
    }

    std::vector<Node> nodes;
    nodes.reserve(simulation_count + 1);
    std::unordered_map<std::string, std::size_t>
        node_indices;
    node_indices.reserve(simulation_count + 1);
    Accounting accounting;
    std::optional<std::string> root_key;
    std::size_t root_index = 0;

    const auto merge_observation =
        [&nodes, &node_indices](
            const CanonicalObservation& observation)
        -> std::pair<std::size_t, bool> {
        const auto found = node_indices.find(
            observation.information_set_key);
        if (found != node_indices.end()) {
            require_same_observation(
                nodes[found->second], observation);
            return {found->second, false};
        }
        if (nodes.size() >= kMaximumNodeCount) {
            fail(
                FailureCode::NodeLimit,
                "ISP0 node bound exhausted");
        }
        const std::size_t index = nodes.size();
        nodes.push_back(make_node(observation));
        node_indices.emplace(
            observation.information_set_key, index);
        return {index, true};
    };

    for (std::size_t simulation = 0;
         simulation < simulation_count;
         ++simulation) {
        ++accounting.simulations_started;
        std::unique_ptr<TruthParticle> particle =
            environment.start_simulation(simulation);
        if (!particle) {
            fail(
                FailureCode::InvalidParticle,
                "ISP0 environment returned a null particle");
        }
        const CanonicalObservation root_observation =
            canonicalize(environment.observe(*particle));
        if (!root_key.has_value()) {
            const auto [index, inserted] =
                merge_observation(root_observation);
            if (!inserted) {
                fail(
                    FailureCode::RootCollision,
                    "ISP0 initial root was already present");
            }
            root_index = index;
            root_key =
                root_observation.information_set_key;
        } else {
            if (root_observation.information_set_key !=
                *root_key) {
                fail(
                    FailureCode::RootCollision,
                    "ISP0 simulations disagree on the root "
                    "information set");
            }
            const auto [index, inserted] =
                merge_observation(root_observation);
            if (inserted || index != root_index) {
                fail(
                    FailureCode::RootCollision,
                    "ISP0 root identity changed during search");
            }
        }

        std::size_t current_index = root_index;
        std::size_t depth = 0;
        std::vector<PathStep> path;
        path.reserve(kMaximumDecisionPlies);
        std::vector<std::size_t> visited_nodes{
            root_index};
        visited_nodes.reserve(
            kMaximumDecisionPlies + 1);
        std::unordered_set<std::string>
            simulation_node_keys;
        simulation_node_keys.reserve(
            kMaximumDecisionPlies + 1);
        simulation_node_keys.insert(*root_key);

        while (true) {
            const std::size_t edge_index =
                select_puct_edge(
                    nodes[current_index], tie_seed);
            const bool newly_expanded_edge =
                !nodes[current_index]
                     .edges[edge_index]
                     .expanded;
            if (newly_expanded_edge) {
                if (accounting.expanded_edge_count >=
                    kMaximumExpandedEdgeCount) {
                    fail(
                        FailureCode::EdgeLimit,
                        "ISP0 expanded-edge bound exhausted");
                }
                nodes[current_index]
                    .edges[edge_index]
                    .expanded = true;
                ++accounting.expanded_edge_count;
            }

            const std::string action_key =
                nodes[current_index]
                    .edges[edge_index]
                    .action_key;
            path.push_back({
                .node_index = current_index,
                .edge_index = edge_index,
            });
            Transition transition =
                environment.advance(
                    *particle, action_key);
            ++depth;
            accounting.maximum_depth =
                std::max(
                    accounting.maximum_depth, depth);

            if (const auto* bound =
                    std::get_if<Bound>(&transition)) {
                fail(
                    FailureCode::BoundExhausted,
                    bound->reason.empty()
                        ? "ISP0 environment bound exhausted"
                        : "ISP0 environment bound exhausted: " +
                              bound->reason);
            }
            if (const auto* terminal =
                    std::get_if<Terminal>(&transition)) {
                ++nodes[current_index]
                       .edges[edge_index]
                       .terminal_transitions;
                backup(
                    nodes,
                    visited_nodes,
                    path,
                    player_zero_terminal_value(
                        *terminal));
                ++accounting.terminal_leaves;
                break;
            }

            const CanonicalObservation next =
                canonicalize(
                    std::get<Observation>(transition));
            const auto [next_index, new_node] =
                merge_observation(next);
            ++nodes[current_index]
                   .edges[edge_index]
                   .successors[
                       next.information_set_key];
            if (!simulation_node_keys.insert(
                    next.information_set_key)
                     .second) {
                fail(
                    FailureCode::CyclicObservation,
                    "ISP0 simulation revisited an "
                    "information-set key");
            }
            visited_nodes.push_back(next_index);

            if (depth >= kMaximumDecisionPlies) {
                backup(
                    nodes,
                    visited_nodes,
                    path,
                    player_zero_leaf_value(next));
                ++accounting.depth_leaves;
                break;
            }
            if (newly_expanded_edge || new_node) {
                backup(
                    nodes,
                    visited_nodes,
                    path,
                    player_zero_leaf_value(next));
                ++accounting.observation_leaves;
                break;
            }
            current_index = next_index;
        }
        ++accounting.simulations_completed;
    }

    accounting.node_count = nodes.size();
    if (!root_key.has_value() ||
        accounting.simulations_started !=
            simulation_count ||
        accounting.simulations_completed !=
            simulation_count ||
        nodes[root_index].visits !=
            simulation_count ||
        sum_edge_visits(nodes[root_index]) !=
            simulation_count ||
        accounting.node_count > kMaximumNodeCount ||
        accounting.expanded_edge_count >
            kMaximumExpandedEdgeCount ||
        accounting.maximum_depth >
            kMaximumDecisionPlies ||
        accounting.terminal_leaves +
                accounting.observation_leaves +
                accounting.depth_leaves !=
            simulation_count) {
        fail(
            FailureCode::InvalidObservation,
            "ISP0 internal accounting invariant failed");
    }

    const std::size_t root_choice =
        select_evidence_edge(
            nodes[root_index],
            tie_seed,
            "old-school-isp0-root-choice-v1");
    SearchResult result{
        .tie_seed = tie_seed,
        .root_information_set_key = *root_key,
        .root_actor = nodes[root_index].actor,
        .root_visits = nodes[root_index].visits,
        .selected_action_key =
            nodes[root_index]
                .edges[root_choice]
                .action_key,
        .principal_variation =
            make_principal_variation(
                nodes,
                node_indices,
                root_index,
                tie_seed),
        .accounting = accounting,
    };

    result.nodes.reserve(nodes.size());
    for (std::size_t node_index = 0;
         node_index < nodes.size(); ++node_index) {
        const Node& node = nodes[node_index];
        const std::size_t selected =
            node_index == root_index
                ? root_choice
                : select_evidence_edge(
                      node,
                      tie_seed,
                      "old-school-isp0-node-choice-v1");
        NodeEvidence evidence{
            .information_set_key =
                node.information_set_key,
            .legal_signature = node.legal_signature,
            .actor = node.actor,
            .fpu_leaf_value =
                node.fpu_leaf_value,
            .visits = node.visits,
            .selected_action_key =
                node.edges[selected].action_key,
        };
        evidence.edges.reserve(node.edges.size());
        for (const Edge& edge : node.edges) {
            EdgeEvidence edge_evidence{
                .action_key = edge.action_key,
                .prior = edge.prior,
                .visits = edge.visits,
                .actor_q = static_cast<double>(
                    edge_actor_q(node, edge)),
                .expanded = edge.expanded,
                .terminal_transitions =
                    edge.terminal_transitions,
            };
            edge_evidence.successors.reserve(
                edge.successors.size());
            for (const auto& [key, count] :
                 edge.successors) {
                edge_evidence.successors.push_back({
                    .information_set_key = key,
                    .transition_count = count,
                });
            }
            evidence.edges.push_back(
                std::move(edge_evidence));
        }
        result.nodes.push_back(std::move(evidence));
    }
    std::sort(
        result.nodes.begin(),
        result.nodes.end(),
        [](const NodeEvidence& left,
           const NodeEvidence& right) {
            return left.information_set_key <
                   right.information_set_key;
        });
    return result;
}

} // namespace old_school::information_set_puct
