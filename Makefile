CXX ?= clang++
CXXFLAGS ?= -std=c++20 -O3 -Wall -Wextra -Wpedantic -Werror
CPPFLAGS ?= -Iinclude

BUILD_DIR := build
ENGINE_SOURCE := src/game.cpp
PROBE_SOURCE := src/probes.cpp
PROBE_EVAL_SOURCE := src/probe_eval.cpp
SIMULATOR := $(BUILD_DIR)/alpha-sim
TEST_RUNNER := $(BUILD_DIR)/alpha-tests
PROBE_TEST_RUNNER := $(BUILD_DIR)/alpha-probe-tests
PROBE_EVAL_TEST_RUNNER := $(BUILD_DIR)/alpha-probe-eval-tests

.PHONY: all test test-probes benchmark benchmark-deep benchmark-learned stability evolve run clean

all: $(SIMULATOR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(SIMULATOR): $(ENGINE_SOURCE) src/main.cpp include/alpha/game.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) src/main.cpp -o $@

$(TEST_RUNNER): $(ENGINE_SOURCE) tests/test_game.cpp include/alpha/game.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) tests/test_game.cpp -o $@

$(PROBE_TEST_RUNNER): $(ENGINE_SOURCE) $(PROBE_SOURCE) tests/test_probes.cpp include/alpha/game.hpp include/alpha/probes.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(PROBE_SOURCE) tests/test_probes.cpp -o $@

$(PROBE_EVAL_TEST_RUNNER): $(PROBE_EVAL_SOURCE) tests/test_probe_eval.cpp include/alpha/game.hpp include/alpha/probe_eval.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(PROBE_EVAL_SOURCE) tests/test_probe_eval.cpp -o $@

test: $(TEST_RUNNER) $(PROBE_TEST_RUNNER) $(PROBE_EVAL_TEST_RUNNER) $(SIMULATOR)
	./$(TEST_RUNNER)
	./$(PROBE_TEST_RUNNER)
	./$(PROBE_EVAL_TEST_RUNNER)
	./$(SIMULATOR) --games 5 --seed 1 >/dev/null

test-probes: $(PROBE_TEST_RUNNER) $(PROBE_EVAL_TEST_RUNNER)
	./$(PROBE_TEST_RUNNER)
	./$(PROBE_EVAL_TEST_RUNNER)

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
	./$(SIMULATOR)

clean:
	rm -rf $(BUILD_DIR)
