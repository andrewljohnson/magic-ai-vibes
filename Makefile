CXX ?= clang++
CXXFLAGS ?= -std=c++20 -O3 -Wall -Wextra -Wpedantic -Werror
CPPFLAGS ?= -Iinclude

BUILD_DIR := build
ENGINE_SOURCE := src/game.cpp
INTERACTIVE_SOURCE := src/interactive.cpp
LEARNED_ITERATION_SOURCE := src/learned_iteration.cpp
PROBE_SOURCE := src/probes.cpp
PROBE_EVAL_SOURCE := src/probe_eval.cpp
PROBE_RUNNER_SOURCE := src/probe_runner.cpp
TERMINAL_WEIGHT_EVAL_SOURCE := src/terminal_weight_eval.cpp
WEB_BRIDGE_SOURCE := src/web_bridge.cpp
SIMULATOR := $(BUILD_DIR)/old-school-sim
TEST_RUNNER := $(BUILD_DIR)/old-school-tests
LEARNED_ITERATION_TEST_RUNNER := $(BUILD_DIR)/old-school-learned-iteration-tests
PROBE_TEST_RUNNER := $(BUILD_DIR)/old-school-probe-tests
PROBE_EVAL_TEST_RUNNER := $(BUILD_DIR)/old-school-probe-eval-tests
PROBE_RUNNER_TEST_RUNNER := $(BUILD_DIR)/old-school-probe-runner-tests
TERMINAL_WEIGHT_EVAL_TEST_RUNNER := $(BUILD_DIR)/old-school-terminal-weight-eval-tests
WEB_BRIDGE := $(BUILD_DIR)/old-school-web-bridge
WEB_BRIDGE_TEST_RUNNER := $(BUILD_DIR)/old-school-web-bridge-tests
WEB_DEPENDENCIES := web/node_modules/.package-lock.json
LEARNED_ROLLOUTS ?= 2
LEARNED_GENERATIONS ?= 0
CHALLENGER_GENERATIONS ?= 1

.PHONY: all test test-capture test-certify test-learned-iteration test-probes test-terminal-weight-eval test-web test-web-ui test-web-rendered web web-target-stack web-interaction web-journey web-delayed-journey web-build benchmark benchmark-deep benchmark-learned benchmark-challenger stability evolve run clean

all: $(SIMULATOR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(SIMULATOR): $(ENGINE_SOURCE) $(INTERACTIVE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) src/main.cpp include/old_school/game.hpp include/old_school/interactive.hpp include/old_school/learned_iteration.hpp include/old_school/probes.hpp include/old_school/probe_eval.hpp include/old_school/probe_runner.hpp include/old_school/terminal_weight_eval.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(INTERACTIVE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) src/main.cpp -o $@

$(TEST_RUNNER): $(ENGINE_SOURCE) $(INTERACTIVE_SOURCE) $(LEARNED_ITERATION_SOURCE) tests/test_game.cpp include/old_school/game.hpp include/old_school/interactive.hpp include/old_school/learned_iteration.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(INTERACTIVE_SOURCE) $(LEARNED_ITERATION_SOURCE) tests/test_game.cpp -o $@

$(LEARNED_ITERATION_TEST_RUNNER): $(LEARNED_ITERATION_SOURCE) tests/test_learned_iteration.cpp include/old_school/game.hpp include/old_school/learned_iteration.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LEARNED_ITERATION_SOURCE) tests/test_learned_iteration.cpp -o $@

$(PROBE_TEST_RUNNER): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) tests/test_probes.cpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/probes.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) tests/test_probes.cpp -o $@

$(PROBE_EVAL_TEST_RUNNER): $(PROBE_EVAL_SOURCE) tests/test_probe_eval.cpp include/old_school/game.hpp include/old_school/probe_eval.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(PROBE_EVAL_SOURCE) tests/test_probe_eval.cpp -o $@

$(PROBE_RUNNER_TEST_RUNNER): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) tests/test_probe_runner.cpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/probes.hpp include/old_school/probe_eval.hpp include/old_school/probe_runner.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) tests/test_probe_runner.cpp -o $@

$(TERMINAL_WEIGHT_EVAL_TEST_RUNNER): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) tests/test_terminal_weight_eval.cpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/probes.hpp include/old_school/probe_eval.hpp include/old_school/probe_runner.hpp include/old_school/terminal_weight_eval.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) tests/test_terminal_weight_eval.cpp -o $@

$(WEB_BRIDGE): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(WEB_BRIDGE_SOURCE) src/web_bridge_main.cpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/web_bridge.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(WEB_BRIDGE_SOURCE) src/web_bridge_main.cpp -o $@

$(WEB_BRIDGE_TEST_RUNNER): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(WEB_BRIDGE_SOURCE) tests/test_web_bridge.cpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/web_bridge.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(WEB_BRIDGE_SOURCE) tests/test_web_bridge.cpp -o $@

$(WEB_DEPENDENCIES): web/package.json web/package-lock.json
	npm --prefix web ci --ignore-scripts

test: $(TEST_RUNNER) $(LEARNED_ITERATION_TEST_RUNNER) $(PROBE_TEST_RUNNER) $(PROBE_EVAL_TEST_RUNNER) $(PROBE_RUNNER_TEST_RUNNER) $(TERMINAL_WEIGHT_EVAL_TEST_RUNNER) $(WEB_BRIDGE_TEST_RUNNER) $(WEB_BRIDGE) $(WEB_DEPENDENCIES) $(SIMULATOR)
	./$(TEST_RUNNER)
	./$(LEARNED_ITERATION_TEST_RUNNER)
	./$(PROBE_TEST_RUNNER)
	./$(PROBE_EVAL_TEST_RUNNER)
	./$(PROBE_RUNNER_TEST_RUNNER)
	./$(TERMINAL_WEIGHT_EVAL_TEST_RUNNER)
	./$(WEB_BRIDGE_TEST_RUNNER)
	sh tests/test_cli.sh ./$(SIMULATOR)
	sh tests/test_capture_once.sh
	./$(SIMULATOR) --games 5 --seed 1 >/dev/null
	npm --prefix web test
	PYTHONDONTWRITEBYTECODE=1 python3 -m unittest tests/test_certify.py

test-certify:
	PYTHONDONTWRITEBYTECODE=1 python3 -m unittest tests/test_certify.py

test-capture:
	sh tests/test_capture_once.sh

test-learned-iteration: $(LEARNED_ITERATION_TEST_RUNNER)
	./$(LEARNED_ITERATION_TEST_RUNNER)

test-probes: $(PROBE_TEST_RUNNER) $(PROBE_EVAL_TEST_RUNNER) $(PROBE_RUNNER_TEST_RUNNER)
	./$(PROBE_TEST_RUNNER)
	./$(PROBE_EVAL_TEST_RUNNER)
	./$(PROBE_RUNNER_TEST_RUNNER)

test-terminal-weight-eval: $(TERMINAL_WEIGHT_EVAL_TEST_RUNNER)
	./$(TERMINAL_WEIGHT_EVAL_TEST_RUNNER)

test-web: $(WEB_BRIDGE_TEST_RUNNER) $(WEB_BRIDGE) $(WEB_DEPENDENCIES)
	./$(WEB_BRIDGE_TEST_RUNNER)
	npm --prefix web test

test-web-ui: $(WEB_DEPENDENCIES)
	npm --prefix web run test:ui

test-web-rendered: $(WEB_BRIDGE) $(WEB_DEPENDENCIES)
	npm --prefix web run test:rendered:target-stack

web-build: $(WEB_BRIDGE) $(WEB_DEPENDENCIES)
	npm --prefix web run build

web: web-build
	npm --prefix web start

web-target-stack: $(WEB_DEPENDENCIES)
	npm --prefix web run build
	npm --prefix web run fixture:target-stack

web-interaction: $(WEB_DEPENDENCIES)
	npm --prefix web run build
	npm --prefix web run fixture:interaction

web-journey: $(WEB_DEPENDENCIES)
	npm --prefix web run build
	npm --prefix web run fixture:journey

web-delayed-journey: $(WEB_DEPENDENCIES)
	npm --prefix web run build
	npm --prefix web run fixture:delayed-journey

benchmark: $(SIMULATOR)
	./$(SIMULATOR) --benchmark --games 20 --seed 424242 --challenger handcrafted --baseline monte-carlo --rollouts 2

benchmark-deep: $(SIMULATOR)
	./$(SIMULATOR) --benchmark --games 20 --seed 424242 --challenger handcrafted --baseline deep-monte-carlo --deep-rollouts 8

benchmark-learned: $(SIMULATOR)
	./$(SIMULATOR) --benchmark --games 20 --seed 424242 --challenger learned --baseline handcrafted --learned-rollouts $(LEARNED_ROLLOUTS) --train-games 800

benchmark-challenger: $(SIMULATOR)
	./$(SIMULATOR) --benchmark --games 20 --seed 424242 --challenger learned-value-c$(CHALLENGER_GENERATIONS) --baseline learned-value-g0 --learned-rollouts $(LEARNED_ROLLOUTS) --train-games 800

stability: $(SIMULATOR)
	./$(SIMULATOR) --stability --stability-runs 8 --games 5 --seed 0 --rollouts 2 --deep-rollouts 8 --learned-rollouts $(LEARNED_ROLLOUTS) --learned-generations $(LEARNED_GENERATIONS) --train-games 800

evolve: $(SIMULATOR)
	./$(SIMULATOR) --evolve-deck --generations 10 --population 16 --games 4

run: $(SIMULATOR)
	./$(SIMULATOR) --seed 4242 --learned-rollouts $(LEARNED_ROLLOUTS) --learned-generations $(LEARNED_GENERATIONS)

clean:
	rm -rf $(BUILD_DIR)
