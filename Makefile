CXX ?= clang++
CXXFLAGS ?= -std=c++20 -O3 -Wall -Wextra -Wpedantic -Werror
CPPFLAGS ?= -Iinclude

BUILD_DIR := build
ENGINE_SOURCE := src/game.cpp
SIMULATOR := $(BUILD_DIR)/alpha-sim
TEST_RUNNER := $(BUILD_DIR)/alpha-tests

.PHONY: all test run clean

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

run: $(SIMULATOR)
	./$(SIMULATOR)

clean:
	rm -rf $(BUILD_DIR)
