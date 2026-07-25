CXX ?= clang++
CXXFLAGS ?= -std=c++20 -O3 -Wall -Wextra -Wpedantic -Werror
CPPFLAGS ?= -Iinclude

BUILD_DIR := build
ENGINE_SOURCE := src/game.cpp
SIMULATOR := $(BUILD_DIR)/alpha-sim
TEST_RUNNER := $(BUILD_DIR)/alpha-tests

.PHONY: all test benchmark benchmark-deep benchmark-learned run clean

all: $(SIMULATOR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(SIMULATOR): $(ENGINE_SOURCE) src/main.cpp include/alpha/game.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) src/main.cpp -o $@

$(TEST_RUNNER): $(ENGINE_SOURCE) tests/test_game.cpp include/alpha/game.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) tests/test_game.cpp -o $@

test: $(TEST_RUNNER) $(SIMULATOR)
	./$(TEST_RUNNER)
	./$(SIMULATOR) --games 5 --seed 1 >/dev/null

benchmark: $(SIMULATOR)
	./$(SIMULATOR) --benchmark --games 20 --seed 424242 --challenger strategic --baseline monte-carlo --rollouts 2

benchmark-deep: $(SIMULATOR)
	./$(SIMULATOR) --benchmark --games 20 --seed 424242 --challenger strategic --baseline deep-monte-carlo --deep-rollouts 8

benchmark-learned: $(SIMULATOR)
	./$(SIMULATOR) --benchmark --games 20 --seed 424242 --challenger learned --baseline monte-carlo --rollouts 2 --train-games 200

run: $(SIMULATOR)
	./$(SIMULATOR)

clean:
	rm -rf $(BUILD_DIR)
