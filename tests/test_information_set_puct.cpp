#include "old_school/information_set_puct.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace puct = old_school::information_set_puct;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Function>
void expect_failure(
    puct::FailureCode expected,
    Function&& function,
    std::string_view message) {
    try {
        std::forward<Function>(function)();
    } catch (const puct::SearchFailure& error) {
        if (error.code() == expected) {
            return;
        }
        throw std::runtime_error(
            std::string(message) +
            ": wrong failure code");
    }
    throw std::runtime_error(std::string(message));
}

struct GraphParticle final : puct::TruthParticle {
    explicit GraphParticle(
        std::string initial,
        std::size_t index = 0)
        : state(std::move(initial)),
          simulation_index(index) {}

    std::string state;
    std::size_t simulation_index = 0;
};

using GraphOutcome =
    std::variant<std::string, puct::Terminal, puct::Bound>;

struct GraphNode {
    std::string legal_signature;
    std::uint8_t actor = 0;
    double value = 0.5;
    std::vector<puct::ActionPrior> actions;
    std::map<std::string, GraphOutcome> outcomes;
};

class GraphEnvironment : public puct::Environment {
public:
    GraphEnvironment(
        std::string root,
        std::map<std::string, GraphNode> nodes,
        bool reverse_actions = false)
        : root_(std::move(root)),
          nodes_(std::move(nodes)),
          reverse_actions_(reverse_actions) {}

    std::unique_ptr<puct::TruthParticle>
    start_simulation(
        std::size_t simulation_index) override {
        return std::make_unique<GraphParticle>(
            root_, simulation_index);
    }

    puct::Observation observe(
        const puct::TruthParticle& particle) override {
        const auto& graph_particle =
            dynamic_cast<const GraphParticle&>(particle);
        const GraphNode& node =
            nodes_.at(graph_particle.state);
        std::vector<puct::ActionPrior> actions =
            node.actions;
        if (reverse_actions_) {
            std::reverse(actions.begin(), actions.end());
        }
        return {
            .information_set_key =
                graph_particle.state,
            .legal_signature =
                node.legal_signature,
            .actor = node.actor,
            .actions = std::move(actions),
            .fpu_leaf_value = node.value,
        };
    }

    puct::Transition advance(
        puct::TruthParticle& particle,
        std::string_view action_key) override {
        auto& graph_particle =
            dynamic_cast<GraphParticle&>(particle);
        const GraphNode& node =
            nodes_.at(graph_particle.state);
        const auto found =
            node.outcomes.find(std::string(action_key));
        if (found == node.outcomes.end()) {
            throw std::runtime_error(
                "test graph received an unknown action");
        }
        if (const auto* successor =
                std::get_if<std::string>(
                    &found->second)) {
            graph_particle.state = *successor;
            return observe(graph_particle);
        }
        if (const auto* terminal =
                std::get_if<puct::Terminal>(
                    &found->second)) {
            return *terminal;
        }
        return std::get<puct::Bound>(
            found->second);
    }

private:
    std::string root_;
    std::map<std::string, GraphNode> nodes_;
    bool reverse_actions_ = false;
};

const puct::NodeEvidence& find_node(
    const puct::SearchResult& result,
    std::string_view key) {
    const auto found = std::find_if(
        result.nodes.begin(),
        result.nodes.end(),
        [key](const puct::NodeEvidence& node) {
            return node.information_set_key == key;
        });
    if (found == result.nodes.end()) {
        throw std::runtime_error(
            "expected node evidence was absent");
    }
    return *found;
}

const puct::EdgeEvidence& find_edge(
    const puct::NodeEvidence& node,
    std::string_view key) {
    const auto found = std::find_if(
        node.edges.begin(),
        node.edges.end(),
        [key](const puct::EdgeEvidence& edge) {
            return edge.action_key == key;
        });
    if (found == node.edges.end()) {
        throw std::runtime_error(
            "expected edge evidence was absent");
    }
    return *found;
}

std::size_t total_edge_visits(
    const puct::NodeEvidence& node) {
    std::size_t result = 0;
    for (const puct::EdgeEvidence& edge :
         node.edges) {
        result += edge.visits;
    }
    return result;
}

std::map<std::string, GraphNode>
terminal_root_graph() {
    return {
        {
            "root",
            {
                .legal_signature = "root/win-draw-lose",
                .actor = 0,
                .value = 0.5,
                .actions = {
                    {"win", 0.45},
                    {"draw", 0.10},
                    {"lose", 0.45},
                },
                .outcomes = {
                    {"win", puct::Terminal{
                                .winner =
                                    std::uint8_t{0}}},
                    {"draw", puct::Terminal{
                                 .winner =
                                     std::nullopt}},
                    {"lose", puct::Terminal{
                                 .winner =
                                     std::uint8_t{1}}},
                },
            },
        },
    };
}

void test_fixed_accounting_draw_and_replay() {
    GraphEnvironment first(
        "root", terminal_root_graph());
    GraphEnvironment second(
        "root", terminal_root_graph());
    const puct::SearchResult a =
        puct::search(first, UINT64_C(112233));
    const puct::SearchResult b =
        puct::search(second, UINT64_C(112233));

    expect(a == b, "fixed-seed replay was not exact");
    expect(
        a.selected_action_key == "win",
        "terminal winner was not selected");
    expect(
        a.root_visits == puct::kSimulationCount &&
            a.accounting.simulations_started ==
                puct::kSimulationCount &&
            a.accounting.simulations_completed ==
                puct::kSimulationCount,
        "sealed simulation accounting was not 64/64");
    const puct::NodeEvidence& root =
        find_node(a, "root");
    expect(
        total_edge_visits(root) ==
            puct::kSimulationCount,
        "root edge visits did not total 64");
    expect(
        root.selected_action_key ==
                a.selected_action_key &&
            !a.principal_variation.empty() &&
            a.principal_variation.front() ==
                a.selected_action_key,
        "root choice and principal variation disagree");
    expect(
        a.accounting.node_count == 1 &&
            a.accounting.expanded_edge_count == 3 &&
            a.accounting.maximum_depth == 1 &&
            a.accounting.terminal_leaves ==
                puct::kSimulationCount,
        "terminal-tree accounting was not exact");
    const puct::EdgeEvidence& draw =
        find_edge(root, "draw");
    expect(
        draw.visits > 0 && draw.actor_q == 0.5 &&
            draw.terminal_transitions == draw.visits &&
            draw.terminal_path_backups == draw.visits &&
            draw.terminal_player_zero_utility_sum ==
                0.5 *
                    static_cast<double>(draw.visits) &&
            draw.terminal_exact_player_zero_utility_sum ==
                draw.terminal_player_zero_utility_sum &&
            draw.terminal_absolute_utility_delta_sum == 0.0,
        "terminal draw did not back up exact one-half");
}

void test_explicit_terminal_utility_and_evidence() {
    std::map<std::string, GraphNode> graph = {
        {
            "root",
            {
                .legal_signature =
                    "root/exact-win-soft-loss",
                .actor = 0,
                .value = 0.5,
                .actions = {
                    {"exact-win", 0.5},
                    {"exact-loss", 0.5},
                },
                .outcomes = {
                    {"exact-win",
                     puct::Terminal{
                         .winner = std::uint8_t{0},
                         .player_zero_utility = 0.2,
                     }},
                    {"exact-loss",
                     puct::Terminal{
                         .winner = std::uint8_t{1},
                         .player_zero_utility = 0.8,
                     }},
                },
            },
        },
    };
    GraphEnvironment environment(
        "root", std::move(graph));
    const puct::SearchResult result =
        puct::search(environment, UINT64_C(713902));
    const puct::NodeEvidence& root =
        find_node(result, "root");
    const puct::EdgeEvidence& soft_win =
        find_edge(root, "exact-loss");
    const puct::EdgeEvidence& soft_loss =
        find_edge(root, "exact-win");

    expect(
        result.selected_action_key == "exact-loss" &&
            std::abs(soft_win.actor_q - 0.8) <
                1.0e-15 &&
            std::abs(soft_loss.actor_q - 0.2) <
                1.0e-15,
        "explicit player-zero terminal utility was not "
        "backed up");
    expect(
        soft_win.terminal_transitions ==
                soft_win.visits &&
            soft_win.terminal_path_backups ==
                soft_win.visits &&
            soft_loss.terminal_transitions ==
                soft_loss.visits &&
            soft_loss.terminal_path_backups ==
                soft_loss.visits &&
            std::abs(
                soft_win
                        .terminal_player_zero_utility_sum -
                    0.8 * static_cast<double>(
                              soft_win.visits)) <
                1.0e-12 &&
            soft_win
                    .terminal_exact_player_zero_utility_sum ==
                0.0 &&
            std::abs(
                soft_loss
                        .terminal_player_zero_utility_sum -
                    0.2 * static_cast<double>(
                              soft_loss.visits)) <
                1.0e-12 &&
            soft_loss
                    .terminal_exact_player_zero_utility_sum ==
                static_cast<double>(soft_loss.visits) &&
            std::abs(
                soft_win
                        .terminal_absolute_utility_delta_sum -
                    0.8 * static_cast<double>(
                              soft_win.visits)) <
                1.0e-12 &&
            std::abs(
                soft_loss
                        .terminal_absolute_utility_delta_sum -
                    0.8 * static_cast<double>(
                              soft_loss.visits)) <
                1.0e-12,
        "terminal evidence did not separate used and exact "
        "player-zero utility");
}

void test_explicit_terminal_matches_equal_critic_leaf() {
    std::map<std::string, GraphNode> graph = {
        {
            "root",
            {
                .legal_signature = "root/terminal-critic",
                .actor = 0,
                .value = 0.5,
                .actions = {
                    {"terminal", 0.5},
                    {"critic", 0.5},
                },
                .outcomes = {
                    {"terminal",
                     puct::Terminal{
                         .winner = std::uint8_t{0},
                         .player_zero_utility = 0.7,
                     }},
                    {"critic", std::string("critic-leaf")},
                },
            },
        },
        {
            "critic-leaf",
            {
                .legal_signature = "critic-leaf/finish",
                .actor = 0,
                .value = 0.7,
                .actions = {{"finish", 1.0}},
                .outcomes = {
                    {"finish",
                     puct::Terminal{
                         .winner = std::uint8_t{0},
                         .player_zero_utility = 0.7,
                     }},
                },
            },
        },
    };
    GraphEnvironment environment(
        "root", std::move(graph));
    const puct::SearchResult result =
        puct::search(environment, UINT64_C(559104));
    const puct::NodeEvidence& root =
        find_node(result, "root");
    const puct::EdgeEvidence& terminal =
        find_edge(root, "terminal");
    const puct::EdgeEvidence& critic =
        find_edge(root, "critic");
    const puct::NodeEvidence& critic_node =
        find_node(result, "critic-leaf");
    const puct::EdgeEvidence& finish =
        find_edge(critic_node, "finish");
    expect(
        terminal.visits > 0 && critic.visits > 0 &&
            std::abs(terminal.actor_q - 0.7) <
                1.0e-15 &&
            std::abs(critic.actor_q - 0.7) <
                1.0e-15 &&
            terminal.actor_q == critic.actor_q &&
            critic.terminal_transitions == 0 &&
            critic.terminal_path_backups > 0 &&
            critic.terminal_path_backups ==
                finish.terminal_transitions &&
            finish.terminal_transitions ==
                finish.terminal_path_backups &&
            critic
                    .terminal_absolute_utility_delta_sum >
                0.0,
        "equal explicit terminal and critic-leaf values "
        "backed up different Q or lost descendant terminal "
        "evidence");
}

void test_explicit_64_is_legacy_bit_identical() {
    GraphEnvironment legacy(
        "root", terminal_root_graph());
    GraphEnvironment explicit_64(
        "root", terminal_root_graph());
    const puct::SearchResult legacy_result =
        puct::search(legacy, UINT64_C(934764));
    const puct::SearchResult explicit_result =
        puct::search(
            explicit_64,
            UINT64_C(934764),
            puct::kSimulationCount);

    expect(
        explicit_result == legacy_result,
        "explicit 64 simulations changed legacy ISP0 evidence");
}

void test_explicit_larger_budget_replay_and_accounting() {
    GraphEnvironment first(
        "root", terminal_root_graph());
    GraphEnvironment second(
        "root", terminal_root_graph());
    const puct::SearchResult a =
        puct::search(
            first,
            UINT64_C(556677),
            puct::kMaximumSimulationCount);
    const puct::SearchResult b =
        puct::search(
            second,
            UINT64_C(556677),
            puct::kMaximumSimulationCount);

    expect(
        a == b,
        "explicit 512-simulation replay was not exact");
    expect(
        a.root_visits == puct::kMaximumSimulationCount &&
            a.accounting.simulations_started ==
                puct::kMaximumSimulationCount &&
            a.accounting.simulations_completed ==
                puct::kMaximumSimulationCount,
        "explicit 512-simulation accounting was not exact");
    expect(
        total_edge_visits(find_node(a, "root")) ==
                puct::kMaximumSimulationCount &&
            a.accounting.terminal_leaves ==
                puct::kMaximumSimulationCount &&
            a.accounting.observation_leaves == 0 &&
            a.accounting.depth_leaves == 0,
        "explicit 512-simulation leaves or visits were not exact");
}

void test_invalid_simulation_budgets_fail_closed() {
    for (const std::size_t simulation_count :
         {std::size_t{0},
          puct::kMaximumSimulationCount + 1}) {
        GraphEnvironment environment(
            "root", terminal_root_graph());
        expect_failure(
            puct::FailureCode::InvalidSimulationCount,
            [&environment, simulation_count]() {
                (void)puct::search(
                    environment,
                    UINT64_C(881122),
                    simulation_count);
            },
            "out-of-range simulation count was accepted");
    }
}

std::map<std::string, GraphNode>
adversarial_graph() {
    return {
        {
            "root",
            {
                .legal_signature = "root/trap-safe",
                .actor = 0,
                .value = 0.5,
                .actions = {
                    {"trap", 0.80},
                    {"safe", 0.20},
                },
                .outcomes = {
                    {"trap", std::string("opponent")},
                    {"safe", puct::Terminal{
                                 .winner =
                                     std::uint8_t{0}}},
                },
            },
        },
        {
            "opponent",
            {
                .legal_signature =
                    "opponent/punish-blunder",
                .actor = 1,
                .value = 0.5,
                .actions = {
                    {"punish", 0.70},
                    {"blunder", 0.30},
                },
                .outcomes = {
                    {"punish", puct::Terminal{
                                   .winner =
                                       std::uint8_t{1}}},
                    {"blunder", puct::Terminal{
                                    .winner =
                                        std::uint8_t{0}}},
                },
            },
        },
    };
}

void test_deeper_adversarial_lookahead() {
    GraphEnvironment environment(
        "root", adversarial_graph());
    const puct::SearchResult result =
        puct::search(environment, UINT64_C(9911));
    const puct::NodeEvidence& root =
        find_node(result, "root");
    const puct::NodeEvidence& opponent =
        find_node(result, "opponent");

    expect(
        find_edge(root, "trap").prior >
            find_edge(root, "safe").prior,
        "test fixture did not favor trap one step");
    expect(
        result.selected_action_key == "safe",
        "multi-ply search did not reverse its "
        "one-step-prior favorite");
    expect(
        opponent.selected_action_key == "punish",
        "actor-one node did not optimize actor one's "
        "outcome");
    expect(
        find_edge(opponent, "punish").actor_q == 1.0,
        "actor-one terminal win was not backed up in "
        "actor-one perspective");
    expect(
        result.accounting.maximum_depth >= 2 &&
            result.accounting.node_count >= 2,
        "adversarial test did not exercise a deeper node");
}

std::map<std::string, GraphNode>
same_actor_graph() {
    return {
        {
            "same/root",
            {
                .legal_signature = "same/root",
                .actor = 0,
                .value = 0.5,
                .actions = {
                    {"continue", 0.60},
                    {"draw", 0.40},
                },
                .outcomes = {
                    {"continue", std::string("same/child")},
                    {"draw", puct::Terminal{
                                 .winner =
                                     std::nullopt}},
                },
            },
        },
        {
            "same/child",
            {
                .legal_signature = "same/child",
                .actor = 0,
                .value = 0.5,
                .actions = {
                    {"win", 0.75},
                    {"lose", 0.25},
                },
                .outcomes = {
                    {"win", puct::Terminal{
                                .winner =
                                    std::uint8_t{0}}},
                    {"lose", puct::Terminal{
                                 .winner =
                                     std::uint8_t{1}}},
                },
            },
        },
    };
}

void test_consecutive_same_actor_does_not_flip() {
    GraphEnvironment environment(
        "same/root", same_actor_graph());
    const puct::SearchResult result =
        puct::search(environment, UINT64_C(7788));
    const puct::NodeEvidence& root =
        find_node(result, "same/root");
    const puct::NodeEvidence& child =
        find_node(result, "same/child");

    expect(
        result.selected_action_key == "continue" &&
            child.selected_action_key == "win",
        "consecutive actor-zero plies were treated as "
        "alternating players");
    expect(
        find_edge(child, "win").actor_q == 1.0 &&
            find_edge(root, "continue").actor_q > 0.5,
        "same-actor absolute-value backup was inverted");
    expect(
        result.principal_variation.size() >= 2 &&
            result.principal_variation[0] ==
                "continue" &&
            result.principal_variation[1] == "win",
        "principal variation did not expose the "
        "multi-ply same-actor line");
}

void test_input_order_identity() {
    GraphEnvironment normal(
        "root", adversarial_graph(), false);
    GraphEnvironment reversed(
        "root", adversarial_graph(), true);
    const puct::SearchResult normal_result =
        puct::search(normal, UINT64_C(4444));
    const puct::SearchResult reversed_result =
        puct::search(reversed, UINT64_C(4444));
    expect(
        normal_result == reversed_result,
        "reversing every action list changed ISP0 "
        "evidence");
}

class CollisionEnvironment final
    : public puct::Environment {
public:
    enum class Kind {
        Actor,
        LegalSignature,
        Prior,
        Fpu,
    };

    explicit CollisionEnvironment(Kind kind)
        : kind_(kind) {}

    std::unique_ptr<puct::TruthParticle>
    start_simulation(
        std::size_t simulation_index) override {
        return std::make_unique<GraphParticle>(
            "root", simulation_index);
    }

    puct::Observation observe(
        const puct::TruthParticle& particle) override {
        const auto& graph_particle =
            dynamic_cast<const GraphParticle&>(particle);
        if (graph_particle.state == "root") {
            return {
                .information_set_key = "root",
                .legal_signature = "root/only",
                .actor = 0,
                .actions = {{"only", 1.0}},
                .fpu_leaf_value = 0.5,
            };
        }
        const bool second =
            graph_particle.simulation_index != 0;
        return {
            .information_set_key = "collision",
            .legal_signature =
                kind_ == Kind::LegalSignature && second
                    ? "collision/b"
                    : "collision/a",
            .actor = static_cast<std::uint8_t>(
                kind_ == Kind::Actor && second ? 1 : 0),
            .actions = {
                {
                    "finish/a",
                    kind_ == Kind::Prior && second
                        ? 0.75
                        : 0.5,
                },
                {
                    "finish/b",
                    kind_ == Kind::Prior && second
                        ? 0.25
                        : 0.5,
                },
            },
            .fpu_leaf_value =
                kind_ == Kind::Fpu && second
                    ? 0.6
                    : 0.5,
        };
    }

    puct::Transition advance(
        puct::TruthParticle& particle,
        std::string_view action_key) override {
        auto& graph_particle =
            dynamic_cast<GraphParticle&>(particle);
        if (graph_particle.state == "root" &&
            action_key == "only") {
            graph_particle.state = "collision";
            return observe(graph_particle);
        }
        return puct::Terminal{
            .winner = std::uint8_t{0},
        };
    }

private:
    Kind kind_;
};

void test_node_key_collisions_fail_closed() {
    for (const CollisionEnvironment::Kind kind : {
             CollisionEnvironment::Kind::Actor,
             CollisionEnvironment::Kind::LegalSignature,
             CollisionEnvironment::Kind::Prior,
             CollisionEnvironment::Kind::Fpu,
         }) {
        CollisionEnvironment environment(kind);
        expect_failure(
            puct::FailureCode::NodeCollision,
            [&environment]() {
                (void)puct::search(
                    environment, UINT64_C(1));
            },
            "same-key observation collision was accepted");
    }
}

std::map<std::string, GraphNode>
transposition_graph() {
    return {
        {
            "transpose/root",
            {
                .legal_signature =
                    "transpose/root/left-right",
                .actor = 0,
                .value = 0.5,
                .actions = {
                    {"left", 0.5},
                    {"right", 0.5},
                },
                .outcomes = {
                    {"left", std::string("transpose/shared")},
                    {"right", std::string("transpose/shared")},
                },
            },
        },
        {
            "transpose/shared",
            {
                .legal_signature =
                    "transpose/shared/finish",
                .actor = 0,
                .value = 0.5,
                .actions = {{"finish", 1.0}},
                .outcomes = {
                    {"finish", puct::Terminal{
                                   .winner =
                                       std::uint8_t{0}}},
                },
            },
        },
    };
}

void test_new_edge_to_existing_node_is_one_expansion() {
    GraphEnvironment environment(
        "transpose/root", transposition_graph());
    const puct::SearchResult result =
        puct::search(environment, UINT64_C(123456));
    const puct::NodeEvidence& root =
        find_node(result, "transpose/root");
    const puct::EdgeEvidence& left =
        find_edge(root, "left");
    const puct::EdgeEvidence& right =
        find_edge(root, "right");
    expect(
        left.expanded && right.expanded &&
            left.successors.size() == 1 &&
            right.successors.size() == 1 &&
            left.successors.front()
                    .information_set_key ==
                "transpose/shared" &&
            right.successors.front()
                    .information_set_key ==
                "transpose/shared",
        "two action edges did not share the canonical "
        "successor node");
    expect(
        result.accounting.node_count == 2 &&
            result.accounting.expanded_edge_count == 3 &&
            result.accounting.observation_leaves >= 2,
        "new edge to an existing node did not stop at "
        "one expansion");
}

class SingleObservationEnvironment final
    : public puct::Environment {
public:
    SingleObservationEnvironment(
        puct::Observation observation,
        puct::Transition transition)
        : observation_(std::move(observation)),
          transition_(std::move(transition)) {}

    std::unique_ptr<puct::TruthParticle>
    start_simulation(std::size_t) override {
        return std::make_unique<GraphParticle>("single");
    }

    puct::Observation observe(
        const puct::TruthParticle&) override {
        return observation_;
    }

    puct::Transition advance(
        puct::TruthParticle&,
        std::string_view) override {
        return transition_;
    }

private:
    puct::Observation observation_;
    puct::Transition transition_;
};

puct::Observation valid_single_observation() {
    return {
        .information_set_key = "single",
        .legal_signature = "single/only",
        .actor = 0,
        .actions = {{"only", 1.0}},
        .fpu_leaf_value = 0.5,
    };
}

void test_malformed_inputs_and_bounds_fail_closed() {
    {
        puct::Observation malformed =
            valid_single_observation();
        malformed.actions.front().prior = 0.0;
        SingleObservationEnvironment environment(
            malformed,
            puct::Terminal{
                .winner = std::uint8_t{0}});
        expect_failure(
            puct::FailureCode::InvalidObservation,
            [&environment]() {
                (void)puct::search(environment, 0);
            },
            "zero prior was accepted");
    }
    {
        puct::Observation malformed =
            valid_single_observation();
        malformed.actions.front().prior =
            std::numeric_limits<double>::quiet_NaN();
        SingleObservationEnvironment environment(
            malformed,
            puct::Terminal{
                .winner = std::uint8_t{0}});
        expect_failure(
            puct::FailureCode::InvalidObservation,
            [&environment]() {
                (void)puct::search(environment, 0);
            },
            "non-finite prior was accepted");
    }
    {
        puct::Observation malformed =
            valid_single_observation();
        malformed.fpu_leaf_value =
            std::numeric_limits<double>::infinity();
        SingleObservationEnvironment environment(
            malformed,
            puct::Terminal{
                .winner = std::uint8_t{0}});
        expect_failure(
            puct::FailureCode::InvalidObservation,
            [&environment]() {
                (void)puct::search(environment, 0);
            },
            "non-finite leaf value was accepted");
    }
    {
        SingleObservationEnvironment environment(
            valid_single_observation(),
            puct::Bound{.reason = "macro"});
        expect_failure(
            puct::FailureCode::BoundExhausted,
            [&environment]() {
                (void)puct::search(environment, 0);
            },
            "environment bound was accepted");
    }
    {
        SingleObservationEnvironment environment(
            valid_single_observation(),
            puct::Terminal{
                .winner = std::uint8_t{2}});
        expect_failure(
            puct::FailureCode::InvalidTerminal,
            [&environment]() {
                (void)puct::search(environment, 0);
            },
            "invalid terminal winner was accepted");
    }
    for (const double utility :
         {
             -0.01,
             1.01,
             std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::quiet_NaN(),
         }) {
        SingleObservationEnvironment environment(
            valid_single_observation(),
            puct::Terminal{
                .winner = std::uint8_t{0},
                .player_zero_utility = utility,
            });
        expect_failure(
            puct::FailureCode::InvalidTerminal,
            [&environment]() {
                (void)puct::search(environment, 0);
            },
            "invalid explicit terminal utility was accepted");
    }
    {
        SingleObservationEnvironment environment(
            valid_single_observation(),
            puct::Terminal{
                .winner = std::uint8_t{2},
                .player_zero_utility = 0.5,
            });
        expect_failure(
            puct::FailureCode::InvalidTerminal,
            [&environment]() {
                (void)puct::search(environment, 0);
            },
            "explicit utility bypassed winner validation");
    }
}

class CycleEnvironment final : public puct::Environment {
public:
    std::unique_ptr<puct::TruthParticle>
    start_simulation(std::size_t) override {
        return std::make_unique<GraphParticle>("cycle");
    }

    puct::Observation observe(
        const puct::TruthParticle&) override {
        return {
            .information_set_key = "cycle",
            .legal_signature = "cycle/again",
            .actor = 0,
            .actions = {{"again", 1.0}},
            .fpu_leaf_value = 0.5,
        };
    }

    puct::Transition advance(
        puct::TruthParticle& particle,
        std::string_view) override {
        return observe(particle);
    }
};

void test_cycle_fails_closed() {
    CycleEnvironment environment;
    expect_failure(
        puct::FailureCode::CyclicObservation,
        [&environment]() {
            (void)puct::search(
                environment, UINT64_C(3));
        },
        "revisited information-set key was accepted");
}

std::map<std::string, GraphNode> long_chain_graph() {
    std::map<std::string, GraphNode> result;
    for (std::size_t index = 0; index < 10; ++index) {
        const std::string key =
            "chain/" + std::to_string(index);
        const std::string next =
            "chain/" + std::to_string(index + 1);
        result.emplace(
            key,
            GraphNode{
                .legal_signature = key + "/only",
                .actor = static_cast<std::uint8_t>(
                    index % 3 == 0 ? 1 : 0),
                .value = 0.5,
                .actions = {{"next", 1.0}},
                .outcomes = {
                    {"next", next},
                },
            });
    }
    result.emplace(
        "chain/10",
        GraphNode{
            .legal_signature = "chain/10/only",
            .actor = 0,
            .value = 0.5,
            .actions = {{"finish", 1.0}},
            .outcomes = {
                {"finish", puct::Terminal{
                               .winner =
                                   std::uint8_t{0}}},
            },
        });
    return result;
}

void test_sealed_depth_bound() {
    GraphEnvironment environment(
        "chain/0", long_chain_graph());
    const puct::SearchResult result =
        puct::search(environment, UINT64_C(98765));
    expect(
        result.accounting.maximum_depth ==
                puct::kMaximumDecisionPlies &&
            result.accounting.depth_leaves > 0 &&
            result.accounting.node_count == 9 &&
            result.accounting.expanded_edge_count == 8,
        "sealed eight-ply boundary was not exact");
    expect(
        result.accounting.node_count <=
                puct::kMaximumNodeCount &&
            result.accounting.expanded_edge_count <=
                puct::kMaximumExpandedEdgeCount,
        "sealed allocation bounds were exceeded");
}

} // namespace

int main() {
    try {
        test_fixed_accounting_draw_and_replay();
        test_explicit_terminal_utility_and_evidence();
        test_explicit_terminal_matches_equal_critic_leaf();
        test_explicit_64_is_legacy_bit_identical();
        test_explicit_larger_budget_replay_and_accounting();
        test_invalid_simulation_budgets_fail_closed();
        test_deeper_adversarial_lookahead();
        test_consecutive_same_actor_does_not_flip();
        test_input_order_identity();
        test_node_key_collisions_fail_closed();
        test_new_edge_to_existing_node_is_one_expansion();
        test_malformed_inputs_and_bounds_fail_closed();
        test_cycle_fails_closed();
        test_sealed_depth_bound();
    } catch (const std::exception& error) {
        std::cerr
            << "information-set PUCT tests failed: "
            << error.what() << '\n';
        return 1;
    }
    std::cout
        << "information-set PUCT tests passed: 14/14 groups\n";
    return 0;
}
