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
AUDIT_COMMON_SOURCE := src/audit_common.cpp
ARTIFACT_INTEGRITY_SOURCE := src/artifact_integrity.cpp
TERMINAL_WEIGHT_EVAL_SOURCE := src/terminal_weight_eval.cpp
JOINT_C17_EVAL_SOURCE := src/joint_c17_eval.cpp
JOINT_C17_RUNNER_SOURCE := src/joint_c17_runner.cpp
JOINT_C17_EXECUTION_SOURCE := src/joint_c17_execution.cpp
JOINT_C17_TRAINING_SOURCE := src/joint_c17_training.cpp
JOINT_C17_ORCHESTRATION_SOURCE := src/joint_c17_orchestration.cpp
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
ARTIFACT_INTEGRITY_TEST_RUNNER := $(BUILD_DIR)/old-school-artifact-integrity-tests
TERMINAL_WEIGHT_EVAL_TEST_RUNNER := $(BUILD_DIR)/old-school-terminal-weight-eval-tests
JOINT_C17_EVAL_TEST_RUNNER := $(BUILD_DIR)/old-school-joint-c17-eval-tests
JOINT_C17_RUNNER_TEST_RUNNER := $(BUILD_DIR)/old-school-joint-c17-runner-tests
JOINT_C17_EXECUTION_TEST_RUNNER := $(BUILD_DIR)/old-school-joint-c17-execution-tests
JOINT_C17_TRAINING_TEST_RUNNER := $(BUILD_DIR)/old-school-joint-c17-training-tests
JOINT_C17_ORCHESTRATION_TEST_RUNNER := $(BUILD_DIR)/old-school-joint-c17-orchestration-tests
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

.PHONY: all test test-capture test-certify test-clean-contract test-learned-iteration test-probes test-audit-common test-artifact-integrity test-terminal-weight-eval test-joint-c17-eval test-joint-c17-runner test-joint-c17-execution test-joint-c17-training test-joint-c17-orchestration test-turn-alignment-audit test-target-factorial-audit test-replay-weight-audit test-rb0-mechanical-preflight rb0-mechanical-preflight test-web test-web-ui test-web-rendered web web-target-stack web-interaction web-journey web-delayed-journey web-build benchmark benchmark-deep benchmark-learned benchmark-challenger stability evolve run clean

all: $(SIMULATOR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(SIMULATOR): $(ENGINE_SOURCE) $(INTERACTIVE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) $(JOINT_C17_EVAL_SOURCE) $(JOINT_C17_RUNNER_SOURCE) $(JOINT_C17_EXECUTION_SOURCE) $(JOINT_C17_TRAINING_SOURCE) $(JOINT_C17_ORCHESTRATION_SOURCE) $(TURN_ALIGNMENT_AUDIT_SOURCE) $(TARGET_FACTORIAL_AUDIT_SOURCE) $(REPLAY_WEIGHT_AUDIT_SOURCE) src/main.cpp include/old_school/game.hpp include/old_school/interactive.hpp include/old_school/learned_iteration.hpp include/old_school/probes.hpp include/old_school/probe_eval.hpp include/old_school/probe_runner.hpp include/old_school/audit_common.hpp include/old_school/artifact_integrity.hpp include/old_school/terminal_weight_eval.hpp include/old_school/joint_c17_eval.hpp include/old_school/joint_c17_runner.hpp include/old_school/joint_c17_execution.hpp include/old_school/joint_c17_training.hpp include/old_school/joint_c17_orchestration.hpp include/old_school/turn_alignment_audit.hpp include/old_school/target_factorial_audit.hpp include/old_school/replay_weight_audit.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(INTERACTIVE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) $(JOINT_C17_EVAL_SOURCE) $(JOINT_C17_RUNNER_SOURCE) $(JOINT_C17_EXECUTION_SOURCE) $(JOINT_C17_TRAINING_SOURCE) $(JOINT_C17_ORCHESTRATION_SOURCE) $(TURN_ALIGNMENT_AUDIT_SOURCE) $(TARGET_FACTORIAL_AUDIT_SOURCE) $(REPLAY_WEIGHT_AUDIT_SOURCE) src/main.cpp -o $@

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

$(AUDIT_COMMON_TEST_RUNNER): $(AUDIT_COMMON_SOURCE) tests/test_audit_common.cpp include/old_school/audit_common.hpp include/old_school/learned_iteration.hpp include/old_school/game.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(AUDIT_COMMON_SOURCE) tests/test_audit_common.cpp -o $@

$(ARTIFACT_INTEGRITY_TEST_RUNNER): $(ARTIFACT_INTEGRITY_SOURCE) tests/test_artifact_integrity.cpp include/old_school/artifact_integrity.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ARTIFACT_INTEGRITY_SOURCE) tests/test_artifact_integrity.cpp -o $@

$(TERMINAL_WEIGHT_EVAL_TEST_RUNNER): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) tests/test_terminal_weight_eval.cpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/probes.hpp include/old_school/probe_eval.hpp include/old_school/probe_runner.hpp include/old_school/audit_common.hpp include/old_school/terminal_weight_eval.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) tests/test_terminal_weight_eval.cpp -o $@

$(JOINT_C17_EVAL_TEST_RUNNER): $(PROBE_EVAL_SOURCE) $(JOINT_C17_EVAL_SOURCE) $(AUDIT_COMMON_SOURCE) tests/test_joint_c17_eval.cpp include/old_school/game.hpp include/old_school/probe_eval.hpp include/old_school/probe_runner.hpp include/old_school/audit_common.hpp include/old_school/terminal_weight_eval.hpp include/old_school/joint_c17_eval.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(PROBE_EVAL_SOURCE) $(JOINT_C17_EVAL_SOURCE) $(AUDIT_COMMON_SOURCE) tests/test_joint_c17_eval.cpp -o $@

$(JOINT_C17_RUNNER_TEST_RUNNER): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(PROBE_EVAL_SOURCE) $(AUDIT_COMMON_SOURCE) $(JOINT_C17_EVAL_SOURCE) $(JOINT_C17_RUNNER_SOURCE) tests/test_joint_c17_runner.cpp include/old_school/artifact_integrity.hpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/probe_eval.hpp include/old_school/probe_runner.hpp include/old_school/audit_common.hpp include/old_school/terminal_weight_eval.hpp include/old_school/joint_c17_eval.hpp include/old_school/joint_c17_runner.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(PROBE_EVAL_SOURCE) $(AUDIT_COMMON_SOURCE) $(JOINT_C17_EVAL_SOURCE) $(JOINT_C17_RUNNER_SOURCE) tests/test_joint_c17_runner.cpp -o $@

$(JOINT_C17_EXECUTION_TEST_RUNNER): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) $(JOINT_C17_EVAL_SOURCE) $(JOINT_C17_RUNNER_SOURCE) $(JOINT_C17_EXECUTION_SOURCE) tests/test_joint_c17_execution.cpp include/old_school/artifact_integrity.hpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/probes.hpp include/old_school/probe_eval.hpp include/old_school/probe_runner.hpp include/old_school/audit_common.hpp include/old_school/terminal_weight_eval.hpp include/old_school/joint_c17_eval.hpp include/old_school/joint_c17_runner.hpp include/old_school/joint_c17_execution.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) $(JOINT_C17_EVAL_SOURCE) $(JOINT_C17_RUNNER_SOURCE) $(JOINT_C17_EXECUTION_SOURCE) tests/test_joint_c17_execution.cpp -o $@

$(JOINT_C17_TRAINING_TEST_RUNNER): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_EVAL_SOURCE) $(AUDIT_COMMON_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(JOINT_C17_EVAL_SOURCE) $(JOINT_C17_RUNNER_SOURCE) $(JOINT_C17_TRAINING_SOURCE) tests/test_joint_c17_training.cpp include/old_school/artifact_integrity.hpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/probe_eval.hpp include/old_school/probe_runner.hpp include/old_school/audit_common.hpp include/old_school/terminal_weight_eval.hpp include/old_school/joint_c17_eval.hpp include/old_school/joint_c17_runner.hpp include/old_school/joint_c17_training.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_EVAL_SOURCE) $(AUDIT_COMMON_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(JOINT_C17_EVAL_SOURCE) $(JOINT_C17_RUNNER_SOURCE) $(JOINT_C17_TRAINING_SOURCE) tests/test_joint_c17_training.cpp -o $@

$(JOINT_C17_ORCHESTRATION_TEST_RUNNER): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) $(JOINT_C17_EVAL_SOURCE) $(JOINT_C17_RUNNER_SOURCE) $(JOINT_C17_EXECUTION_SOURCE) $(JOINT_C17_ORCHESTRATION_SOURCE) tests/test_joint_c17_orchestration.cpp include/old_school/artifact_integrity.hpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/probes.hpp include/old_school/probe_eval.hpp include/old_school/probe_runner.hpp include/old_school/audit_common.hpp include/old_school/terminal_weight_eval.hpp include/old_school/joint_c17_eval.hpp include/old_school/joint_c17_runner.hpp include/old_school/joint_c17_execution.hpp include/old_school/joint_c17_orchestration.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) $(JOINT_C17_EVAL_SOURCE) $(JOINT_C17_RUNNER_SOURCE) $(JOINT_C17_EXECUTION_SOURCE) $(JOINT_C17_ORCHESTRATION_SOURCE) tests/test_joint_c17_orchestration.cpp -o $@

$(TURN_ALIGNMENT_AUDIT_TEST_RUNNER): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) $(TURN_ALIGNMENT_AUDIT_SOURCE) tests/test_turn_alignment_audit.cpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/probes.hpp include/old_school/probe_eval.hpp include/old_school/probe_runner.hpp include/old_school/audit_common.hpp include/old_school/terminal_weight_eval.hpp include/old_school/turn_alignment_audit.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) $(TURN_ALIGNMENT_AUDIT_SOURCE) tests/test_turn_alignment_audit.cpp -o $@

$(TARGET_FACTORIAL_AUDIT_TEST_RUNNER): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) $(TURN_ALIGNMENT_AUDIT_SOURCE) $(TARGET_FACTORIAL_AUDIT_SOURCE) tests/test_target_factorial_audit.cpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/probes.hpp include/old_school/probe_eval.hpp include/old_school/probe_runner.hpp include/old_school/audit_common.hpp include/old_school/terminal_weight_eval.hpp include/old_school/turn_alignment_audit.hpp include/old_school/target_factorial_audit.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) $(TURN_ALIGNMENT_AUDIT_SOURCE) $(TARGET_FACTORIAL_AUDIT_SOURCE) tests/test_target_factorial_audit.cpp -o $@

$(REPLAY_WEIGHT_AUDIT_TEST_RUNNER): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) $(REPLAY_WEIGHT_AUDIT_SOURCE) tests/test_replay_weight_audit.cpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/probes.hpp include/old_school/probe_eval.hpp include/old_school/probe_runner.hpp include/old_school/audit_common.hpp include/old_school/terminal_weight_eval.hpp include/old_school/replay_weight_audit.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) $(REPLAY_WEIGHT_AUDIT_SOURCE) tests/test_replay_weight_audit.cpp -o $@

$(RB0_MECHANICAL_PREFLIGHT): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) $(REPLAY_WEIGHT_AUDIT_SOURCE) $(RB0_MECHANICAL_PREFLIGHT_SOURCE) src/rb0_mechanical_preflight_main.cpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/probes.hpp include/old_school/probe_eval.hpp include/old_school/probe_runner.hpp include/old_school/audit_common.hpp include/old_school/terminal_weight_eval.hpp include/old_school/replay_weight_audit.hpp include/old_school/rb0_mechanical_preflight.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) $(REPLAY_WEIGHT_AUDIT_SOURCE) $(RB0_MECHANICAL_PREFLIGHT_SOURCE) src/rb0_mechanical_preflight_main.cpp -o $@

$(RB0_MECHANICAL_PREFLIGHT_TEST_RUNNER): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) $(REPLAY_WEIGHT_AUDIT_SOURCE) $(RB0_MECHANICAL_PREFLIGHT_SOURCE) tests/test_rb0_mechanical_preflight.cpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/probes.hpp include/old_school/probe_eval.hpp include/old_school/probe_runner.hpp include/old_school/audit_common.hpp include/old_school/terminal_weight_eval.hpp include/old_school/replay_weight_audit.hpp include/old_school/rb0_mechanical_preflight.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) $(REPLAY_WEIGHT_AUDIT_SOURCE) $(RB0_MECHANICAL_PREFLIGHT_SOURCE) tests/test_rb0_mechanical_preflight.cpp -o $@

$(WEB_BRIDGE): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(WEB_BRIDGE_SOURCE) src/web_bridge_main.cpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/web_bridge.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(WEB_BRIDGE_SOURCE) src/web_bridge_main.cpp -o $@

$(WEB_BRIDGE_TEST_RUNNER): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(WEB_BRIDGE_SOURCE) tests/test_web_bridge.cpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/web_bridge.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(WEB_BRIDGE_SOURCE) tests/test_web_bridge.cpp -o $@

$(WEB_DEPENDENCIES): web/package.json web/package-lock.json
	npm --prefix web ci --ignore-scripts

test: $(TEST_RUNNER) $(LEARNED_ITERATION_TEST_RUNNER) $(PROBE_TEST_RUNNER) $(PROBE_EVAL_TEST_RUNNER) $(PROBE_RUNNER_TEST_RUNNER) $(AUDIT_COMMON_TEST_RUNNER) $(ARTIFACT_INTEGRITY_TEST_RUNNER) $(TERMINAL_WEIGHT_EVAL_TEST_RUNNER) $(JOINT_C17_EVAL_TEST_RUNNER) $(JOINT_C17_RUNNER_TEST_RUNNER) $(JOINT_C17_EXECUTION_TEST_RUNNER) $(JOINT_C17_TRAINING_TEST_RUNNER) $(JOINT_C17_ORCHESTRATION_TEST_RUNNER) $(TURN_ALIGNMENT_AUDIT_TEST_RUNNER) $(TARGET_FACTORIAL_AUDIT_TEST_RUNNER) $(REPLAY_WEIGHT_AUDIT_TEST_RUNNER) $(RB0_MECHANICAL_PREFLIGHT_TEST_RUNNER) $(WEB_BRIDGE_TEST_RUNNER) $(WEB_BRIDGE) $(WEB_DEPENDENCIES) $(SIMULATOR)
	./$(TEST_RUNNER)
	./$(LEARNED_ITERATION_TEST_RUNNER)
	./$(PROBE_TEST_RUNNER)
	./$(PROBE_EVAL_TEST_RUNNER)
	./$(PROBE_RUNNER_TEST_RUNNER)
	./$(AUDIT_COMMON_TEST_RUNNER)
	./$(ARTIFACT_INTEGRITY_TEST_RUNNER)
	./$(TERMINAL_WEIGHT_EVAL_TEST_RUNNER)
	./$(JOINT_C17_EVAL_TEST_RUNNER)
	./$(JOINT_C17_RUNNER_TEST_RUNNER)
	./$(JOINT_C17_EXECUTION_TEST_RUNNER)
	./$(JOINT_C17_TRAINING_TEST_RUNNER)
	./$(JOINT_C17_ORCHESTRATION_TEST_RUNNER)
	./$(TURN_ALIGNMENT_AUDIT_TEST_RUNNER)
	./$(TARGET_FACTORIAL_AUDIT_TEST_RUNNER)
	./$(REPLAY_WEIGHT_AUDIT_TEST_RUNNER)
	./$(RB0_MECHANICAL_PREFLIGHT_TEST_RUNNER)
	./$(WEB_BRIDGE_TEST_RUNNER)
	sh tests/test_cli.sh ./$(SIMULATOR)
	sh tests/test_capture_once.sh
	sh tests/test_make_clean.sh
	./$(SIMULATOR) --games 5 --seed 1 >/dev/null
	npm --prefix web test
	PYTHONDONTWRITEBYTECODE=1 python3 -m unittest tests/test_certify.py

test-certify:
	PYTHONDONTWRITEBYTECODE=1 python3 -m unittest tests/test_certify.py

test-capture:
	sh tests/test_capture_once.sh

test-clean-contract:
	sh tests/test_make_clean.sh

test-learned-iteration: $(LEARNED_ITERATION_TEST_RUNNER)
	./$(LEARNED_ITERATION_TEST_RUNNER)

test-probes: $(PROBE_TEST_RUNNER) $(PROBE_EVAL_TEST_RUNNER) $(PROBE_RUNNER_TEST_RUNNER)
	./$(PROBE_TEST_RUNNER)
	./$(PROBE_EVAL_TEST_RUNNER)
	./$(PROBE_RUNNER_TEST_RUNNER)

test-audit-common: $(AUDIT_COMMON_TEST_RUNNER)
	./$(AUDIT_COMMON_TEST_RUNNER)

test-artifact-integrity: $(ARTIFACT_INTEGRITY_TEST_RUNNER)
	./$(ARTIFACT_INTEGRITY_TEST_RUNNER)

test-terminal-weight-eval: $(TERMINAL_WEIGHT_EVAL_TEST_RUNNER)
	./$(TERMINAL_WEIGHT_EVAL_TEST_RUNNER)

test-joint-c17-eval: $(JOINT_C17_EVAL_TEST_RUNNER)
	./$(JOINT_C17_EVAL_TEST_RUNNER)

test-joint-c17-runner: $(JOINT_C17_RUNNER_TEST_RUNNER)
	./$(JOINT_C17_RUNNER_TEST_RUNNER)

test-joint-c17-execution: $(JOINT_C17_EXECUTION_TEST_RUNNER)
	./$(JOINT_C17_EXECUTION_TEST_RUNNER)

test-joint-c17-training: $(JOINT_C17_TRAINING_TEST_RUNNER)
	./$(JOINT_C17_TRAINING_TEST_RUNNER)

test-joint-c17-orchestration: $(JOINT_C17_ORCHESTRATION_TEST_RUNNER)
	./$(JOINT_C17_ORCHESTRATION_TEST_RUNNER)

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
	@if [ -d "$(BUILD_DIR)" ]; then \
		find "$(BUILD_DIR)" -mindepth 1 -maxdepth 1 \
			! -name model-cache -exec rm -rf -- {} +; \
	fi
