CXX ?= clang++
CXXFLAGS ?= -std=c++20 -O3 -Wall -Wextra -Wpedantic -Werror
CPPFLAGS ?= -Iinclude

BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
HEADERS := $(wildcard include/old_school/*.hpp)
ENGINE_SOURCE := src/game.cpp
INTERACTIVE_SOURCE := src/interactive.cpp
LEARNED_ITERATION_SOURCE := src/learned_iteration.cpp
PROBE_SOURCE := src/probes.cpp
PROBE_EVAL_SOURCE := src/probe_eval.cpp
PROBE_RUNNER_SOURCE := src/probe_runner.cpp
AUDIT_COMMON_SOURCE := src/audit_common.cpp
TERMINAL_WEIGHT_EVAL_SOURCE := src/terminal_weight_eval.cpp
TURN_ALIGNMENT_AUDIT_SOURCE := src/turn_alignment_audit.cpp
TARGET_FACTORIAL_AUDIT_SOURCE := src/target_factorial_audit.cpp
REPLAY_WEIGHT_AUDIT_SOURCE := src/replay_weight_audit.cpp
RB0_MECHANICAL_PREFLIGHT_SOURCE := src/rb0_mechanical_preflight.cpp
WEB_BRIDGE_SOURCE := src/web_bridge.cpp
SIMULATOR := $(BUILD_DIR)/old-school-sim
TEST_RUNNER := $(BUILD_DIR)/old-school-tests
LEARNED_ITERATION_TEST_RUNNER := $(BUILD_DIR)/old-school-learned-iteration-tests
PROBE_TEST_RUNNER := $(BUILD_DIR)/old-school-probe-tests
PROBE_EVAL_TEST_RUNNER := $(BUILD_DIR)/old-school-probe-eval-tests
PROBE_RUNNER_TEST_RUNNER := $(BUILD_DIR)/old-school-probe-runner-tests
AUDIT_COMMON_TEST_RUNNER := $(BUILD_DIR)/old-school-audit-common-tests
TERMINAL_WEIGHT_EVAL_TEST_RUNNER := $(BUILD_DIR)/old-school-terminal-weight-eval-tests
TURN_ALIGNMENT_AUDIT_TEST_RUNNER := $(BUILD_DIR)/old-school-turn-alignment-audit-tests
TARGET_FACTORIAL_AUDIT_TEST_RUNNER := $(BUILD_DIR)/old-school-target-factorial-audit-tests
REPLAY_WEIGHT_AUDIT_TEST_RUNNER := $(BUILD_DIR)/old-school-replay-weight-audit-tests
RB0_MECHANICAL_PREFLIGHT := $(BUILD_DIR)/rb0-mechanical-preflight
RB0_MECHANICAL_PREFLIGHT_TEST_RUNNER := $(BUILD_DIR)/old-school-rb0-mechanical-preflight-tests
WEB_BRIDGE := $(BUILD_DIR)/old-school-web-bridge
WEB_BRIDGE_TEST_RUNNER := $(BUILD_DIR)/old-school-web-bridge-tests
WEB_DEPENDENCIES := web/node_modules/.package-lock.json
LEARNED_ROLLOUTS ?= 2
LEARNED_GENERATIONS ?= 0
CHALLENGER_GENERATIONS ?= 1

# Every translation unit compiles exactly once into $(OBJ_DIR); binaries are
# links over the shared objects. All objects depend on every public header so
# a header edit rebuilds everything a monolithic compile would have rebuilt.
ENGINE_OBJECT := $(OBJ_DIR)/game.o
INTERACTIVE_OBJECT := $(OBJ_DIR)/interactive.o
LEARNED_ITERATION_OBJECT := $(OBJ_DIR)/learned_iteration.o
PROBE_OBJECT := $(OBJ_DIR)/probes.o
PROBE_EVAL_OBJECT := $(OBJ_DIR)/probe_eval.o
PROBE_RUNNER_OBJECT := $(OBJ_DIR)/probe_runner.o
AUDIT_COMMON_OBJECT := $(OBJ_DIR)/audit_common.o
TERMINAL_WEIGHT_EVAL_OBJECT := $(OBJ_DIR)/terminal_weight_eval.o
TURN_ALIGNMENT_AUDIT_OBJECT := $(OBJ_DIR)/turn_alignment_audit.o
TARGET_FACTORIAL_AUDIT_OBJECT := $(OBJ_DIR)/target_factorial_audit.o
REPLAY_WEIGHT_AUDIT_OBJECT := $(OBJ_DIR)/replay_weight_audit.o
RB0_MECHANICAL_PREFLIGHT_OBJECT := $(OBJ_DIR)/rb0_mechanical_preflight.o
RB0_MECHANICAL_PREFLIGHT_MAIN_OBJECT := $(OBJ_DIR)/rb0_mechanical_preflight_main.o
WEB_BRIDGE_OBJECT := $(OBJ_DIR)/web_bridge.o
WEB_BRIDGE_MAIN_OBJECT := $(OBJ_DIR)/web_bridge_main.o
MAIN_OBJECT := $(OBJ_DIR)/main.o

.PHONY: all test test-capture test-certify test-learned-iteration test-probes test-audit-common test-terminal-weight-eval test-turn-alignment-audit test-target-factorial-audit test-replay-weight-audit test-rb0-mechanical-preflight rb0-mechanical-preflight test-web test-web-ui test-web-rendered web web-target-stack web-interaction web-journey web-delayed-journey web-build benchmark benchmark-deep benchmark-learned benchmark-challenger stability evolve run clean

all: $(SIMULATOR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/tests:
	mkdir -p $(OBJ_DIR)/tests

$(OBJ_DIR)/%.o: src/%.cpp $(HEADERS) | $(OBJ_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/tests/%.o: tests/%.cpp $(HEADERS) | $(OBJ_DIR)/tests
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

SIMULATOR_OBJECTS := $(ENGINE_OBJECT) $(INTERACTIVE_OBJECT) $(LEARNED_ITERATION_OBJECT) $(PROBE_OBJECT) $(PROBE_EVAL_OBJECT) $(PROBE_RUNNER_OBJECT) $(AUDIT_COMMON_OBJECT) $(TERMINAL_WEIGHT_EVAL_OBJECT) $(TURN_ALIGNMENT_AUDIT_OBJECT) $(TARGET_FACTORIAL_AUDIT_OBJECT) $(REPLAY_WEIGHT_AUDIT_OBJECT) $(MAIN_OBJECT)

$(SIMULATOR): $(SIMULATOR_OBJECTS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(SIMULATOR_OBJECTS) -o $@

TEST_RUNNER_OBJECTS := $(ENGINE_OBJECT) $(INTERACTIVE_OBJECT) $(LEARNED_ITERATION_OBJECT) $(OBJ_DIR)/tests/test_game.o

$(TEST_RUNNER): $(TEST_RUNNER_OBJECTS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(TEST_RUNNER_OBJECTS) -o $@

LEARNED_ITERATION_TEST_OBJECTS := $(LEARNED_ITERATION_OBJECT) $(OBJ_DIR)/tests/test_learned_iteration.o

$(LEARNED_ITERATION_TEST_RUNNER): $(LEARNED_ITERATION_TEST_OBJECTS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(LEARNED_ITERATION_TEST_OBJECTS) -o $@

PROBE_TEST_OBJECTS := $(ENGINE_OBJECT) $(LEARNED_ITERATION_OBJECT) $(PROBE_OBJECT) $(OBJ_DIR)/tests/test_probes.o

$(PROBE_TEST_RUNNER): $(PROBE_TEST_OBJECTS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(PROBE_TEST_OBJECTS) -o $@

PROBE_EVAL_TEST_OBJECTS := $(PROBE_EVAL_OBJECT) $(OBJ_DIR)/tests/test_probe_eval.o

$(PROBE_EVAL_TEST_RUNNER): $(PROBE_EVAL_TEST_OBJECTS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(PROBE_EVAL_TEST_OBJECTS) -o $@

PROBE_RUNNER_TEST_OBJECTS := $(ENGINE_OBJECT) $(LEARNED_ITERATION_OBJECT) $(PROBE_OBJECT) $(PROBE_EVAL_OBJECT) $(PROBE_RUNNER_OBJECT) $(OBJ_DIR)/tests/test_probe_runner.o

$(PROBE_RUNNER_TEST_RUNNER): $(PROBE_RUNNER_TEST_OBJECTS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(PROBE_RUNNER_TEST_OBJECTS) -o $@

AUDIT_COMMON_TEST_OBJECTS := $(AUDIT_COMMON_OBJECT) $(OBJ_DIR)/tests/test_audit_common.o

$(AUDIT_COMMON_TEST_RUNNER): $(AUDIT_COMMON_TEST_OBJECTS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(AUDIT_COMMON_TEST_OBJECTS) -o $@

TERMINAL_WEIGHT_EVAL_TEST_OBJECTS := $(ENGINE_OBJECT) $(INTERACTIVE_OBJECT) $(LEARNED_ITERATION_OBJECT) $(PROBE_OBJECT) $(PROBE_EVAL_OBJECT) $(PROBE_RUNNER_OBJECT) $(AUDIT_COMMON_OBJECT) $(TERMINAL_WEIGHT_EVAL_OBJECT) $(OBJ_DIR)/tests/test_terminal_weight_eval.o

$(TERMINAL_WEIGHT_EVAL_TEST_RUNNER): $(TERMINAL_WEIGHT_EVAL_TEST_OBJECTS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(TERMINAL_WEIGHT_EVAL_TEST_OBJECTS) -o $@

TURN_ALIGNMENT_AUDIT_TEST_OBJECTS := $(ENGINE_OBJECT) $(INTERACTIVE_OBJECT) $(LEARNED_ITERATION_OBJECT) $(PROBE_OBJECT) $(PROBE_EVAL_OBJECT) $(PROBE_RUNNER_OBJECT) $(AUDIT_COMMON_OBJECT) $(TERMINAL_WEIGHT_EVAL_OBJECT) $(TURN_ALIGNMENT_AUDIT_OBJECT) $(OBJ_DIR)/tests/test_turn_alignment_audit.o

$(TURN_ALIGNMENT_AUDIT_TEST_RUNNER): $(TURN_ALIGNMENT_AUDIT_TEST_OBJECTS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(TURN_ALIGNMENT_AUDIT_TEST_OBJECTS) -o $@

TARGET_FACTORIAL_AUDIT_TEST_OBJECTS := $(ENGINE_OBJECT) $(INTERACTIVE_OBJECT) $(LEARNED_ITERATION_OBJECT) $(PROBE_OBJECT) $(PROBE_EVAL_OBJECT) $(PROBE_RUNNER_OBJECT) $(AUDIT_COMMON_OBJECT) $(TERMINAL_WEIGHT_EVAL_OBJECT) $(TURN_ALIGNMENT_AUDIT_OBJECT) $(TARGET_FACTORIAL_AUDIT_OBJECT) $(OBJ_DIR)/tests/test_target_factorial_audit.o

$(TARGET_FACTORIAL_AUDIT_TEST_RUNNER): $(TARGET_FACTORIAL_AUDIT_TEST_OBJECTS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(TARGET_FACTORIAL_AUDIT_TEST_OBJECTS) -o $@

REPLAY_WEIGHT_AUDIT_TEST_OBJECTS := $(ENGINE_OBJECT) $(INTERACTIVE_OBJECT) $(LEARNED_ITERATION_OBJECT) $(PROBE_OBJECT) $(PROBE_EVAL_OBJECT) $(PROBE_RUNNER_OBJECT) $(AUDIT_COMMON_OBJECT) $(TERMINAL_WEIGHT_EVAL_OBJECT) $(REPLAY_WEIGHT_AUDIT_OBJECT) $(OBJ_DIR)/tests/test_replay_weight_audit.o

$(REPLAY_WEIGHT_AUDIT_TEST_RUNNER): $(REPLAY_WEIGHT_AUDIT_TEST_OBJECTS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(REPLAY_WEIGHT_AUDIT_TEST_OBJECTS) -o $@

RB0_MECHANICAL_PREFLIGHT_OBJECTS := $(ENGINE_OBJECT) $(INTERACTIVE_OBJECT) $(LEARNED_ITERATION_OBJECT) $(PROBE_OBJECT) $(PROBE_EVAL_OBJECT) $(PROBE_RUNNER_OBJECT) $(AUDIT_COMMON_OBJECT) $(TERMINAL_WEIGHT_EVAL_OBJECT) $(REPLAY_WEIGHT_AUDIT_OBJECT) $(RB0_MECHANICAL_PREFLIGHT_OBJECT) $(RB0_MECHANICAL_PREFLIGHT_MAIN_OBJECT)

$(RB0_MECHANICAL_PREFLIGHT): $(RB0_MECHANICAL_PREFLIGHT_OBJECTS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(RB0_MECHANICAL_PREFLIGHT_OBJECTS) -o $@

RB0_MECHANICAL_PREFLIGHT_TEST_OBJECTS := $(ENGINE_OBJECT) $(INTERACTIVE_OBJECT) $(LEARNED_ITERATION_OBJECT) $(PROBE_OBJECT) $(PROBE_EVAL_OBJECT) $(PROBE_RUNNER_OBJECT) $(AUDIT_COMMON_OBJECT) $(TERMINAL_WEIGHT_EVAL_OBJECT) $(REPLAY_WEIGHT_AUDIT_OBJECT) $(RB0_MECHANICAL_PREFLIGHT_OBJECT) $(OBJ_DIR)/tests/test_rb0_mechanical_preflight.o

$(RB0_MECHANICAL_PREFLIGHT_TEST_RUNNER): $(RB0_MECHANICAL_PREFLIGHT_TEST_OBJECTS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(RB0_MECHANICAL_PREFLIGHT_TEST_OBJECTS) -o $@

WEB_BRIDGE_OBJECTS := $(ENGINE_OBJECT) $(INTERACTIVE_OBJECT) $(LEARNED_ITERATION_OBJECT) $(WEB_BRIDGE_OBJECT) $(WEB_BRIDGE_MAIN_OBJECT)

$(WEB_BRIDGE): $(WEB_BRIDGE_OBJECTS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(WEB_BRIDGE_OBJECTS) -o $@

WEB_BRIDGE_TEST_OBJECTS := $(ENGINE_OBJECT) $(INTERACTIVE_OBJECT) $(LEARNED_ITERATION_OBJECT) $(WEB_BRIDGE_OBJECT) $(OBJ_DIR)/tests/test_web_bridge.o

$(WEB_BRIDGE_TEST_RUNNER): $(WEB_BRIDGE_TEST_OBJECTS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(WEB_BRIDGE_TEST_OBJECTS) -o $@

$(WEB_DEPENDENCIES): web/package.json web/package-lock.json
	npm --prefix web ci --ignore-scripts

test: $(TEST_RUNNER) $(LEARNED_ITERATION_TEST_RUNNER) $(PROBE_TEST_RUNNER) $(PROBE_EVAL_TEST_RUNNER) $(PROBE_RUNNER_TEST_RUNNER) $(AUDIT_COMMON_TEST_RUNNER) $(TERMINAL_WEIGHT_EVAL_TEST_RUNNER) $(TURN_ALIGNMENT_AUDIT_TEST_RUNNER) $(TARGET_FACTORIAL_AUDIT_TEST_RUNNER) $(REPLAY_WEIGHT_AUDIT_TEST_RUNNER) $(RB0_MECHANICAL_PREFLIGHT_TEST_RUNNER) $(WEB_BRIDGE_TEST_RUNNER) $(WEB_BRIDGE) $(WEB_DEPENDENCIES) $(SIMULATOR)
	./$(TEST_RUNNER)
	./$(LEARNED_ITERATION_TEST_RUNNER)
	./$(PROBE_TEST_RUNNER)
	./$(PROBE_EVAL_TEST_RUNNER)
	./$(PROBE_RUNNER_TEST_RUNNER)
	./$(AUDIT_COMMON_TEST_RUNNER)
	./$(TERMINAL_WEIGHT_EVAL_TEST_RUNNER)
	./$(TURN_ALIGNMENT_AUDIT_TEST_RUNNER)
	./$(TARGET_FACTORIAL_AUDIT_TEST_RUNNER)
	./$(REPLAY_WEIGHT_AUDIT_TEST_RUNNER)
	./$(RB0_MECHANICAL_PREFLIGHT_TEST_RUNNER)
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

test-audit-common: $(AUDIT_COMMON_TEST_RUNNER)
	./$(AUDIT_COMMON_TEST_RUNNER)

test-terminal-weight-eval: $(TERMINAL_WEIGHT_EVAL_TEST_RUNNER)
	./$(TERMINAL_WEIGHT_EVAL_TEST_RUNNER)

test-turn-alignment-audit: $(TURN_ALIGNMENT_AUDIT_TEST_RUNNER)
	./$(TURN_ALIGNMENT_AUDIT_TEST_RUNNER)

test-target-factorial-audit: $(TARGET_FACTORIAL_AUDIT_TEST_RUNNER)
	./$(TARGET_FACTORIAL_AUDIT_TEST_RUNNER)

test-replay-weight-audit: $(REPLAY_WEIGHT_AUDIT_TEST_RUNNER)
	./$(REPLAY_WEIGHT_AUDIT_TEST_RUNNER)

rb0-mechanical-preflight: $(RB0_MECHANICAL_PREFLIGHT)

test-rb0-mechanical-preflight: $(RB0_MECHANICAL_PREFLIGHT_TEST_RUNNER) $(RB0_MECHANICAL_PREFLIGHT)
	./$(RB0_MECHANICAL_PREFLIGHT_TEST_RUNNER)
	@set +e; output=`./$(RB0_MECHANICAL_PREFLIGHT) --seed 202607260731 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'RB0-E1 executable did not reject quarantined seed\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'rejects quarantined RB0-0 seed 202607260731' >/dev/null
	@set +e; output=`./$(RB0_MECHANICAL_PREFLIGHT) --seed 202607261047 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'RB0-E1 executable did not reject reserved seed\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'rejects reserved RB0-0 seed 202607261047' >/dev/null

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
