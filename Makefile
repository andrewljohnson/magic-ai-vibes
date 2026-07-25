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
SIMULATOR := $(BUILD_DIR)/old-school-sim
TEST_RUNNER := $(BUILD_DIR)/old-school-tests
LEARNED_ITERATION_TEST_RUNNER := $(BUILD_DIR)/old-school-learned-iteration-tests
PROBE_TEST_RUNNER := $(BUILD_DIR)/old-school-probe-tests
PROBE_EVAL_TEST_RUNNER := $(BUILD_DIR)/old-school-probe-eval-tests
PROBE_RUNNER_TEST_RUNNER := $(BUILD_DIR)/old-school-probe-runner-tests

.PHONY: all test test-learned-iteration test-probes benchmark benchmark-deep benchmark-learned stability evolve run clean

all: $(SIMULATOR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(SIMULATOR): $(ENGINE_SOURCE) $(INTERACTIVE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) src/main.cpp include/old_school/game.hpp include/old_school/interactive.hpp include/old_school/learned_iteration.hpp include/old_school/probes.hpp include/old_school/probe_eval.hpp include/old_school/probe_runner.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(INTERACTIVE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) src/main.cpp -o $@

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

test: $(TEST_RUNNER) $(LEARNED_ITERATION_TEST_RUNNER) $(PROBE_TEST_RUNNER) $(PROBE_EVAL_TEST_RUNNER) $(PROBE_RUNNER_TEST_RUNNER) $(SIMULATOR)
	./$(TEST_RUNNER)
	./$(LEARNED_ITERATION_TEST_RUNNER)
	./$(PROBE_TEST_RUNNER)
	./$(PROBE_EVAL_TEST_RUNNER)
	./$(PROBE_RUNNER_TEST_RUNNER)
	sh tests/test_cli.sh ./$(SIMULATOR)
	./$(SIMULATOR) --games 5 --seed 1 >/dev/null

test-learned-iteration: $(LEARNED_ITERATION_TEST_RUNNER)
	./$(LEARNED_ITERATION_TEST_RUNNER)

test-probes: $(PROBE_TEST_RUNNER) $(PROBE_EVAL_TEST_RUNNER) $(PROBE_RUNNER_TEST_RUNNER)
	./$(PROBE_TEST_RUNNER)
	./$(PROBE_EVAL_TEST_RUNNER)
	./$(PROBE_RUNNER_TEST_RUNNER)

benchmark: $(SIMULATOR)
	./$(SIMULATOR) --benchmark --games 20 --seed 424242 --challenger handcrafted --baseline monte-carlo --rollouts 2

benchmark-deep: $(SIMULATOR)
	./$(SIMULATOR) --benchmark --games 20 --seed 424242 --challenger handcrafted --baseline deep-monte-carlo --deep-rollouts 8

benchmark-learned: $(SIMULATOR)
	./$(SIMULATOR) --benchmark --games 20 --seed 424242 --challenger learned --baseline handcrafted --rollouts 2 --train-games 800

stability: $(SIMULATOR)
	./$(SIMULATOR) --stability --stability-runs 8 --games 5 --seed 0 --rollouts 2 --deep-rollouts 8 --train-games 800

evolve: $(SIMULATOR)
	./$(SIMULATOR) --evolve-deck --generations 10 --population 16 --games 4

run: $(SIMULATOR)
	./$(SIMULATOR) --seed 4242

clean:
	rm -rf $(BUILD_DIR)
