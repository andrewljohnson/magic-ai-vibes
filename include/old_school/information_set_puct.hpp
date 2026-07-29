#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace old_school::information_set_puct {

inline constexpr std::size_t kSimulationCount = 64;
inline constexpr std::size_t kMaximumSimulationCount = 512;
inline constexpr std::size_t kMaximumDecisionPlies = 8;
inline constexpr std::size_t kMaximumNodeCount = 513;
inline constexpr std::size_t kMaximumExpandedEdgeCount = 512;
inline constexpr double kExplorationConstant = 1.0;

struct ActionPrior {
    std::string action_key;
    double prior = 0.0;

    bool operator==(const ActionPrior&) const = default;
};

// Every field in an Observation must be a function of the acting player's
// information set. In particular, neither an opponent hand nor a library
// order may influence the key, priors, or value.
struct Observation {
    std::string information_set_key;
    std::string legal_signature;
    std::uint8_t actor = 0;
    std::vector<ActionPrior> actions;
    // Frozen-C16 contextual value, from actor's perspective. It is used both
    // as first-play urgency and as the value of a newly reached leaf.
    double fpu_leaf_value = 0.5;

    bool operator==(const Observation&) const = default;
};

struct Terminal {
    // No winner is a draw. Otherwise the only valid players are zero and one.
    std::optional<std::uint8_t> winner;
    // When present, this is the terminal utility backed up by the tree in
    // absolute player-zero perspective. The winner remains independently
    // validated and is retained as the raw exact-outcome witness.
    std::optional<double> player_zero_utility;

    bool operator==(const Terminal&) const = default;
};

struct Bound {
    std::string reason;

    bool operator==(const Bound&) const = default;
};

using Transition = std::variant<Observation, Terminal, Bound>;

// The core deliberately cannot inspect a truth particle. An engine adapter
// owns its complete hidden state and is responsible for actor-local
// re-determinization and engine-authoritative transitions.
class TruthParticle {
public:
    virtual ~TruthParticle() = default;
};

class Environment {
public:
    virtual ~Environment() = default;

    // Called exactly once for each simulation index in
    // [0, requested_simulation_count). The returned particle is mutable and
    // private to that simulation.
    virtual std::unique_ptr<TruthParticle> start_simulation(
        std::size_t simulation_index) = 0;

    virtual Observation observe(
        const TruthParticle& particle) = 0;

    // Advance by a stable action key. The adapter may resolve forced or
    // opponent-controlled work before returning the next searched
    // observation. Any exhausted engine/search bound must return Bound.
    virtual Transition advance(
        TruthParticle& particle,
        std::string_view action_key) = 0;
};

enum class FailureCode {
    InvalidObservation,
    NodeCollision,
    RootCollision,
    InvalidTerminal,
    BoundExhausted,
    NodeLimit,
    EdgeLimit,
    CyclicObservation,
    InvalidParticle,
    InvalidSimulationCount,
};

class SearchFailure : public std::runtime_error {
public:
    SearchFailure(FailureCode code, std::string message);

    [[nodiscard]] FailureCode code() const noexcept;

private:
    FailureCode code_;
};

struct SuccessorEvidence {
    std::string information_set_key;
    std::size_t transition_count = 0;

    bool operator==(const SuccessorEvidence&) const = default;
};

struct EdgeEvidence {
    std::string action_key;
    double prior = 0.0;
    std::size_t visits = 0;
    // Mean from the owning node actor's perspective. For an unvisited edge,
    // this is exactly the node's frozen FPU value.
    double actor_q = 0.5;
    bool expanded = false;
    // Terminal transitions reached directly by this edge. This preserves the
    // original ISP0 evidence semantics.
    std::size_t terminal_transitions = 0;
    // Terminal backups whose selected path contains this edge. Root-edge
    // totals therefore partition every terminal leaf by root action.
    std::size_t terminal_path_backups = 0;
    double terminal_player_zero_utility_sum = 0.0;
    double terminal_exact_player_zero_utility_sum = 0.0;
    double terminal_absolute_utility_delta_sum = 0.0;
    std::vector<SuccessorEvidence> successors;

    bool operator==(const EdgeEvidence&) const = default;
};

struct NodeEvidence {
    std::string information_set_key;
    std::string legal_signature;
    std::uint8_t actor = 0;
    double fpu_leaf_value = 0.5;
    std::size_t visits = 0;
    std::string selected_action_key;
    std::vector<EdgeEvidence> edges;

    bool operator==(const NodeEvidence&) const = default;
};

struct Accounting {
    std::size_t simulations_started = 0;
    std::size_t simulations_completed = 0;
    std::size_t node_count = 0;
    std::size_t expanded_edge_count = 0;
    std::size_t maximum_depth = 0;
    std::size_t terminal_leaves = 0;
    std::size_t observation_leaves = 0;
    std::size_t depth_leaves = 0;

    bool operator==(const Accounting&) const = default;
};

struct SearchResult {
    std::uint64_t tie_seed = 0;
    std::string root_information_set_key;
    std::uint8_t root_actor = 0;
    std::size_t root_visits = 0;
    std::string selected_action_key;
    std::vector<std::string> principal_variation;
    // Canonically sorted by information_set_key. Each node's edges and
    // successor evidence are also canonically sorted.
    std::vector<NodeEvidence> nodes;
    Accounting accounting;

    bool operator==(const SearchResult&) const = default;
};

// Run the sealed one-thread ISP0 search. This legacy entry point always uses
// exactly kSimulationCount simulations; depth, allocation bounds, and PUCT
// constant are fixed by the constants above.
SearchResult search(
    Environment& environment,
    std::uint64_t tie_seed);

// Run the same one-thread search with an explicitly requested simulation
// count in [1, kMaximumSimulationCount]. No other search coordinate changes.
SearchResult search(
    Environment& environment,
    std::uint64_t tie_seed,
    std::size_t simulation_count);

} // namespace old_school::information_set_puct
