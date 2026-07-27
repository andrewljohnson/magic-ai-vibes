CXX ?= clang++
CXXFLAGS ?= -std=c++20 -O3 -Wall -Wextra -Wpedantic -Werror
CPPFLAGS ?= -Iinclude

BUILD_DIR := build
ENGINE_SOURCE := src/game.cpp
INTERACTIVE_SOURCE := src/interactive.cpp
LEARNED_ITERATION_SOURCE := src/learned_iteration.cpp
PROBE_SOURCE := src/probes.cpp src/dvr1_replay.cpp
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
DVR2_HARVEST_SOURCE := src/dvr2_harvest.cpp
DVR2_REPLAY_BUNDLE_SOURCE := src/dvr2_replay_bundle.cpp
OUTPUT_CALIBRATION_SOURCE := src/output_calibration.cpp
OUTPUT_CALIBRATION_ARTIFACT_SOURCE := src/output_calibration_artifact.cpp
OUTPUT_CALIBRATION_RUNNER_SOURCE := src/output_calibration_runner.cpp
OC1_ACTION_EVAL_SOURCE := src/oc1_action_eval.cpp
OC1_ACTION_SCORING_SOURCE := src/oc1_action_scoring.cpp
OC1_ACTION_REGRESSION_SOURCE := src/oc1_action_regression.cpp
AC1_TEACHER_AUDIT_SOURCE := src/ac1_teacher_audit.cpp
FQ0_INFORMATION_SET_SOURCE := src/fq0_information_set.cpp
FQ0_BELLMAN_SOURCE := src/fq0_bellman.cpp
FQ0_DOMINANCE_SOURCE := src/fq0_dominance.cpp
FQ0_DOMINANCE_TRANSITION_SOURCE := src/fq0_dominance_transition.cpp
FQ0_BELLMAN_SCIENCE_SOURCE := src/fq0_bellman_science.cpp
FQ0_BELLMAN_AUDIT_SOURCE := src/fq0_bellman_audit.cpp
FQ0_BELLMAN_RUN_SOURCE := src/fq0_bellman_run.cpp
WEB_BRIDGE_SOURCE := src/web_bridge.cpp
SIMULATOR := $(BUILD_DIR)/old-school-sim
TEST_RUNNER := $(BUILD_DIR)/old-school-tests
LEARNED_ITERATION_TEST_RUNNER := $(BUILD_DIR)/old-school-learned-iteration-tests
PROBE_TEST_RUNNER := $(BUILD_DIR)/old-school-probe-tests
PROBE_EVAL_TEST_RUNNER := $(BUILD_DIR)/old-school-probe-eval-tests
PROBE_RUNNER_TEST_RUNNER := $(BUILD_DIR)/old-school-probe-runner-tests
ATTACK_REGRESSION := $(BUILD_DIR)/old-school-attack-regression
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
DVR2_HARVEST := $(BUILD_DIR)/old-school-dvr2-harvest
DVR2_HARVEST_TEST_RUNNER := $(BUILD_DIR)/old-school-dvr2-harvest-tests
DVR2_REPLAY_BUNDLE_TEST_RUNNER := $(BUILD_DIR)/old-school-dvr2-replay-bundle-tests
OUTPUT_CALIBRATION_TEST_RUNNER := $(BUILD_DIR)/old-school-output-calibration-tests
OUTPUT_CALIBRATION_ARTIFACT_TEST_RUNNER := $(BUILD_DIR)/old-school-output-calibration-artifact-tests
OUTPUT_CALIBRATION_RUNNER_TEST_RUNNER := $(BUILD_DIR)/old-school-output-calibration-runner-tests
OUTPUT_CALIBRATION := $(BUILD_DIR)/old-school-output-calibration
OC1_ACTION_EVAL_TEST_RUNNER := $(BUILD_DIR)/old-school-oc1-action-eval-tests
OC1_ACTION_SCORING_TEST_RUNNER := $(BUILD_DIR)/old-school-oc1-action-scoring-tests
OC1_ACTION_REGRESSION_TEST_RUNNER := $(BUILD_DIR)/old-school-oc1-action-regression-tests
OC1_ACTION_REGRESSION := $(BUILD_DIR)/old-school-oc1-action-regression
AC1_TEACHER_AUDIT_TEST_RUNNER := $(BUILD_DIR)/old-school-ac1-teacher-audit-tests
AC1_TEACHER_AUDIT := $(BUILD_DIR)/old-school-ac1-teacher-audit
FQ0_INFORMATION_SET_TEST_RUNNER := $(BUILD_DIR)/old-school-fq0-information-set-tests
FQ0_BELLMAN_TEST_RUNNER := $(BUILD_DIR)/old-school-fq0-bellman-tests
FQ0_DOMINANCE_TEST_RUNNER := $(BUILD_DIR)/old-school-fq0-dominance-tests
FQ0_DOMINANCE_TRANSITION_TEST_RUNNER := $(BUILD_DIR)/old-school-fq0-dominance-transition-tests
FQ0_BELLMAN_SCIENCE_TEST_RUNNER := $(BUILD_DIR)/old-school-fq0-bellman-science-tests
FQ0_BELLMAN_AUDIT_TEST_RUNNER := $(BUILD_DIR)/old-school-fq0-bellman-audit-tests
FQ0_BELLMAN_RUN_TEST_RUNNER := $(BUILD_DIR)/old-school-fq0-bellman-run-tests
FQ0_BELLMAN_AUDIT := $(BUILD_DIR)/old-school-fq0-bellman-audit
WEB_BRIDGE := $(BUILD_DIR)/old-school-web-bridge
WEB_BRIDGE_TEST_RUNNER := $(BUILD_DIR)/old-school-web-bridge-tests
PROBE_HEADER_DEPENDENTS := \
	$(SIMULATOR) \
	$(PROBE_TEST_RUNNER) \
	$(PROBE_RUNNER_TEST_RUNNER) \
	$(ATTACK_REGRESSION) \
	$(TERMINAL_WEIGHT_EVAL_TEST_RUNNER) \
	$(JOINT_C17_EXECUTION_TEST_RUNNER) \
	$(JOINT_C17_ORCHESTRATION_TEST_RUNNER) \
	$(TURN_ALIGNMENT_AUDIT_TEST_RUNNER) \
	$(TARGET_FACTORIAL_AUDIT_TEST_RUNNER) \
	$(REPLAY_WEIGHT_AUDIT_TEST_RUNNER) \
	$(RB0_MECHANICAL_PREFLIGHT) \
	$(RB0_MECHANICAL_PREFLIGHT_TEST_RUNNER)
WEB_DEPENDENCIES := web/node_modules/.package-lock.json
LEARNED_ROLLOUTS ?= 2
LEARNED_GENERATIONS ?= 0
CHALLENGER_GENERATIONS ?= 1

.PHONY: all test test-capture test-certify test-clean-contract test-learned-iteration test-probes attack-regression test-audit-common test-artifact-integrity test-terminal-weight-eval test-joint-c17-eval test-joint-c17-runner test-joint-c17-execution test-joint-c17-training test-joint-c17-orchestration test-turn-alignment-audit test-target-factorial-audit test-replay-weight-audit test-rb0-mechanical-preflight rb0-mechanical-preflight test-dvr2-harvest dvr2-harvest test-dvr2-replay-bundle test-output-calibration test-output-calibration-artifact test-output-calibration-runner output-calibration test-oc1-action-eval test-oc1-action-scoring test-oc1-action-regression oc1-action-regression test-ac1-teacher-audit ac1-teacher-audit test-fq0 test-fq0-information-set test-fq0-bellman test-fq0-dominance test-fq0-dominance-transition test-fq0-bellman-science test-fq0-bellman-audit test-fq0-bellman-run fq0-bellman-audit test-web test-web-ui test-web-rendered web web-target-stack web-interaction web-journey web-delayed-journey web-build benchmark benchmark-deep benchmark-learned benchmark-challenger stability evolve run clean

all: $(SIMULATOR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(PROBE_HEADER_DEPENDENTS): include/old_school/dvr1_replay.hpp

$(SIMULATOR): $(ENGINE_SOURCE) $(INTERACTIVE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) $(JOINT_C17_EVAL_SOURCE) $(JOINT_C17_RUNNER_SOURCE) $(JOINT_C17_EXECUTION_SOURCE) $(JOINT_C17_TRAINING_SOURCE) $(JOINT_C17_ORCHESTRATION_SOURCE) $(TURN_ALIGNMENT_AUDIT_SOURCE) $(TARGET_FACTORIAL_AUDIT_SOURCE) $(REPLAY_WEIGHT_AUDIT_SOURCE) src/main.cpp include/old_school/game.hpp include/old_school/interactive.hpp include/old_school/learned_iteration.hpp include/old_school/probes.hpp include/old_school/probe_eval.hpp include/old_school/probe_runner.hpp include/old_school/audit_common.hpp include/old_school/artifact_integrity.hpp include/old_school/terminal_weight_eval.hpp include/old_school/joint_c17_eval.hpp include/old_school/joint_c17_runner.hpp include/old_school/joint_c17_execution.hpp include/old_school/joint_c17_training.hpp include/old_school/joint_c17_orchestration.hpp include/old_school/turn_alignment_audit.hpp include/old_school/target_factorial_audit.hpp include/old_school/replay_weight_audit.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(INTERACTIVE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) $(JOINT_C17_EVAL_SOURCE) $(JOINT_C17_RUNNER_SOURCE) $(JOINT_C17_EXECUTION_SOURCE) $(JOINT_C17_TRAINING_SOURCE) $(JOINT_C17_ORCHESTRATION_SOURCE) $(TURN_ALIGNMENT_AUDIT_SOURCE) $(TARGET_FACTORIAL_AUDIT_SOURCE) $(REPLAY_WEIGHT_AUDIT_SOURCE) src/main.cpp -o $@

$(TEST_RUNNER): $(ENGINE_SOURCE) $(INTERACTIVE_SOURCE) $(LEARNED_ITERATION_SOURCE) tests/test_game.cpp include/old_school/game.hpp include/old_school/interactive.hpp include/old_school/learned_iteration.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(INTERACTIVE_SOURCE) $(LEARNED_ITERATION_SOURCE) tests/test_game.cpp -o $@

$(LEARNED_ITERATION_TEST_RUNNER): $(LEARNED_ITERATION_SOURCE) tests/test_learned_iteration.cpp include/old_school/game.hpp include/old_school/learned_iteration.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LEARNED_ITERATION_SOURCE) tests/test_learned_iteration.cpp -o $@

$(PROBE_TEST_RUNNER): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) tests/test_probes.cpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/probes.hpp include/old_school/dvr1_replay.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) tests/test_probes.cpp -o $@

$(PROBE_EVAL_TEST_RUNNER): $(PROBE_EVAL_SOURCE) tests/test_probe_eval.cpp include/old_school/game.hpp include/old_school/probe_eval.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(PROBE_EVAL_SOURCE) tests/test_probe_eval.cpp -o $@

$(PROBE_RUNNER_TEST_RUNNER): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) tests/test_probe_runner.cpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/probes.hpp include/old_school/probe_eval.hpp include/old_school/probe_runner.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) tests/test_probe_runner.cpp -o $@

$(ATTACK_REGRESSION): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) src/attack_regression_main.cpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/probes.hpp include/old_school/probe_eval.hpp include/old_school/probe_runner.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) src/attack_regression_main.cpp -o $@

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

$(DVR2_HARVEST): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(DVR2_HARVEST_SOURCE) src/dvr2_harvest_main.cpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/probes.hpp include/old_school/dvr1_replay.hpp include/old_school/artifact_integrity.hpp include/old_school/dvr2_harvest.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(DVR2_HARVEST_SOURCE) src/dvr2_harvest_main.cpp -o $@

$(DVR2_HARVEST_TEST_RUNNER): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(DVR2_HARVEST_SOURCE) tests/test_dvr2_harvest.cpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/probes.hpp include/old_school/dvr1_replay.hpp include/old_school/artifact_integrity.hpp include/old_school/dvr2_harvest.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(DVR2_HARVEST_SOURCE) tests/test_dvr2_harvest.cpp -o $@

$(DVR2_REPLAY_BUNDLE_TEST_RUNNER): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(DVR2_REPLAY_BUNDLE_SOURCE) tests/test_dvr2_replay_bundle.cpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/probes.hpp include/old_school/dvr1_replay.hpp include/old_school/artifact_integrity.hpp include/old_school/dvr2_replay_bundle.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(DVR2_REPLAY_BUNDLE_SOURCE) tests/test_dvr2_replay_bundle.cpp -o $@

$(OUTPUT_CALIBRATION_TEST_RUNNER): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(AUDIT_COMMON_SOURCE) $(OUTPUT_CALIBRATION_SOURCE) tests/test_output_calibration.cpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/audit_common.hpp include/old_school/output_calibration.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(AUDIT_COMMON_SOURCE) $(OUTPUT_CALIBRATION_SOURCE) tests/test_output_calibration.cpp -o $@

$(OUTPUT_CALIBRATION_ARTIFACT_TEST_RUNNER): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(AUDIT_COMMON_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(OUTPUT_CALIBRATION_SOURCE) $(OUTPUT_CALIBRATION_ARTIFACT_SOURCE) tests/test_output_calibration_artifact.cpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/audit_common.hpp include/old_school/artifact_integrity.hpp include/old_school/output_calibration.hpp include/old_school/output_calibration_artifact.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(AUDIT_COMMON_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(OUTPUT_CALIBRATION_SOURCE) $(OUTPUT_CALIBRATION_ARTIFACT_SOURCE) tests/test_output_calibration_artifact.cpp -o $@

$(OUTPUT_CALIBRATION): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(AUDIT_COMMON_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(OUTPUT_CALIBRATION_SOURCE) $(OUTPUT_CALIBRATION_ARTIFACT_SOURCE) $(OUTPUT_CALIBRATION_RUNNER_SOURCE) src/output_calibration_main.cpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/audit_common.hpp include/old_school/artifact_integrity.hpp include/old_school/output_calibration.hpp include/old_school/output_calibration_artifact.hpp include/old_school/output_calibration_runner.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(AUDIT_COMMON_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(OUTPUT_CALIBRATION_SOURCE) $(OUTPUT_CALIBRATION_ARTIFACT_SOURCE) $(OUTPUT_CALIBRATION_RUNNER_SOURCE) src/output_calibration_main.cpp -o $@

$(OUTPUT_CALIBRATION_RUNNER_TEST_RUNNER): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(AUDIT_COMMON_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(OUTPUT_CALIBRATION_SOURCE) $(OUTPUT_CALIBRATION_ARTIFACT_SOURCE) $(OUTPUT_CALIBRATION_RUNNER_SOURCE) tests/test_output_calibration_runner.cpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/audit_common.hpp include/old_school/artifact_integrity.hpp include/old_school/output_calibration.hpp include/old_school/output_calibration_artifact.hpp include/old_school/output_calibration_runner.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(AUDIT_COMMON_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(OUTPUT_CALIBRATION_SOURCE) $(OUTPUT_CALIBRATION_ARTIFACT_SOURCE) $(OUTPUT_CALIBRATION_RUNNER_SOURCE) tests/test_output_calibration_runner.cpp -o $@

$(OC1_ACTION_EVAL_TEST_RUNNER): $(PROBE_EVAL_SOURCE) $(OC1_ACTION_EVAL_SOURCE) tests/test_oc1_action_eval.cpp include/old_school/game.hpp include/old_school/probe_eval.hpp include/old_school/oc1_action_eval.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(PROBE_EVAL_SOURCE) $(OC1_ACTION_EVAL_SOURCE) tests/test_oc1_action_eval.cpp -o $@

$(OC1_ACTION_SCORING_TEST_RUNNER): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(OC1_ACTION_SCORING_SOURCE) tests/test_oc1_action_scoring.cpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/probes.hpp include/old_school/probe_eval.hpp include/old_school/probe_runner.hpp include/old_school/oc1_action_scoring.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(OC1_ACTION_SCORING_SOURCE) tests/test_oc1_action_scoring.cpp -o $@

OC1_ACTION_REGRESSION_LINK_SOURCES := $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(DVR2_REPLAY_BUNDLE_SOURCE) $(OUTPUT_CALIBRATION_SOURCE) $(OUTPUT_CALIBRATION_ARTIFACT_SOURCE) $(OC1_ACTION_EVAL_SOURCE) $(OC1_ACTION_SCORING_SOURCE) $(OC1_ACTION_REGRESSION_SOURCE)
OC1_ACTION_REGRESSION_HEADERS := include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/probes.hpp include/old_school/probe_eval.hpp include/old_school/probe_runner.hpp include/old_school/audit_common.hpp include/old_school/artifact_integrity.hpp include/old_school/dvr1_replay.hpp include/old_school/dvr2_replay_bundle.hpp include/old_school/output_calibration.hpp include/old_school/output_calibration_artifact.hpp include/old_school/oc1_action_eval.hpp include/old_school/oc1_action_scoring.hpp include/old_school/oc1_action_regression.hpp

$(OC1_ACTION_REGRESSION_TEST_RUNNER): $(OC1_ACTION_REGRESSION_LINK_SOURCES) tests/test_oc1_action_regression.cpp $(OC1_ACTION_REGRESSION_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(OC1_ACTION_REGRESSION_LINK_SOURCES) tests/test_oc1_action_regression.cpp -o $@

$(OC1_ACTION_REGRESSION): $(OC1_ACTION_REGRESSION_LINK_SOURCES) src/oc1_action_regression_main.cpp $(OC1_ACTION_REGRESSION_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(OC1_ACTION_REGRESSION_LINK_SOURCES) src/oc1_action_regression_main.cpp -o $@

AC1_TEACHER_AUDIT_LINK_SOURCES := $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(OUTPUT_CALIBRATION_SOURCE) $(OUTPUT_CALIBRATION_ARTIFACT_SOURCE) $(OC1_ACTION_SCORING_SOURCE) $(AC1_TEACHER_AUDIT_SOURCE)
AC1_TEACHER_AUDIT_HEADERS := include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/probes.hpp include/old_school/dvr1_replay.hpp include/old_school/probe_eval.hpp include/old_school/probe_runner.hpp include/old_school/audit_common.hpp include/old_school/artifact_integrity.hpp include/old_school/output_calibration.hpp include/old_school/output_calibration_artifact.hpp include/old_school/oc1_action_scoring.hpp include/old_school/oc1_action_regression.hpp include/old_school/ac1_teacher_audit.hpp
FQ0_INFORMATION_SET_LINK_SOURCES := $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(FQ0_INFORMATION_SET_SOURCE)
FQ0_DOMINANCE_LINK_SOURCES := $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(FQ0_DOMINANCE_SOURCE) $(FQ0_DOMINANCE_TRANSITION_SOURCE)
FQ0_BELLMAN_SCIENCE_LINK_SOURCES := $(AC1_TEACHER_AUDIT_LINK_SOURCES) $(FQ0_INFORMATION_SET_SOURCE) $(FQ0_BELLMAN_SOURCE) $(FQ0_BELLMAN_SCIENCE_SOURCE)
FQ0_BELLMAN_AUDIT_LINK_SOURCES := $(AC1_TEACHER_AUDIT_LINK_SOURCES) $(FQ0_INFORMATION_SET_SOURCE) $(FQ0_BELLMAN_SOURCE) $(FQ0_DOMINANCE_SOURCE) $(FQ0_BELLMAN_AUDIT_SOURCE)
FQ0_BELLMAN_RUN_LINK_SOURCES := $(AC1_TEACHER_AUDIT_LINK_SOURCES) $(FQ0_INFORMATION_SET_SOURCE) $(FQ0_BELLMAN_SOURCE) $(FQ0_DOMINANCE_SOURCE) $(FQ0_DOMINANCE_TRANSITION_SOURCE) $(FQ0_BELLMAN_SCIENCE_SOURCE) $(FQ0_BELLMAN_AUDIT_SOURCE) $(FQ0_BELLMAN_RUN_SOURCE)
FQ0_HEADERS := include/old_school/fq0_information_set.hpp include/old_school/fq0_bellman.hpp include/old_school/fq0_dominance.hpp include/old_school/fq0_dominance_transition.hpp include/old_school/fq0_bellman_science.hpp include/old_school/fq0_bellman_audit.hpp

$(AC1_TEACHER_AUDIT_TEST_RUNNER): $(AC1_TEACHER_AUDIT_LINK_SOURCES) tests/test_ac1_teacher_audit.cpp $(AC1_TEACHER_AUDIT_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(AC1_TEACHER_AUDIT_LINK_SOURCES) tests/test_ac1_teacher_audit.cpp -o $@

$(AC1_TEACHER_AUDIT): $(AC1_TEACHER_AUDIT_LINK_SOURCES) src/ac1_teacher_audit_main.cpp $(AC1_TEACHER_AUDIT_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(AC1_TEACHER_AUDIT_LINK_SOURCES) src/ac1_teacher_audit_main.cpp -o $@

$(FQ0_INFORMATION_SET_TEST_RUNNER): $(FQ0_INFORMATION_SET_LINK_SOURCES) tests/test_fq0_information_set.cpp include/old_school/fq0_information_set.hpp $(AC1_TEACHER_AUDIT_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(FQ0_INFORMATION_SET_LINK_SOURCES) tests/test_fq0_information_set.cpp -o $@

$(FQ0_BELLMAN_TEST_RUNNER): $(FQ0_BELLMAN_SOURCE) tests/test_fq0_bellman.cpp include/old_school/fq0_bellman.hpp include/old_school/game.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(FQ0_BELLMAN_SOURCE) tests/test_fq0_bellman.cpp -o $@

$(FQ0_DOMINANCE_TEST_RUNNER): $(FQ0_DOMINANCE_LINK_SOURCES) tests/test_fq0_dominance.cpp include/old_school/fq0_dominance.hpp include/old_school/fq0_dominance_transition.hpp $(AC1_TEACHER_AUDIT_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(FQ0_DOMINANCE_LINK_SOURCES) tests/test_fq0_dominance.cpp -o $@

$(FQ0_DOMINANCE_TRANSITION_TEST_RUNNER): $(FQ0_DOMINANCE_LINK_SOURCES) tests/test_fq0_dominance_transition.cpp include/old_school/fq0_dominance.hpp include/old_school/fq0_dominance_transition.hpp $(AC1_TEACHER_AUDIT_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(FQ0_DOMINANCE_LINK_SOURCES) tests/test_fq0_dominance_transition.cpp -o $@

$(FQ0_BELLMAN_SCIENCE_TEST_RUNNER): $(FQ0_BELLMAN_SCIENCE_LINK_SOURCES) tests/test_fq0_bellman_science.cpp $(AC1_TEACHER_AUDIT_HEADERS) $(FQ0_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(FQ0_BELLMAN_SCIENCE_LINK_SOURCES) tests/test_fq0_bellman_science.cpp -o $@

$(FQ0_BELLMAN_AUDIT_TEST_RUNNER): $(FQ0_BELLMAN_AUDIT_LINK_SOURCES) tests/test_fq0_bellman_audit.cpp $(AC1_TEACHER_AUDIT_HEADERS) $(FQ0_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(FQ0_BELLMAN_AUDIT_LINK_SOURCES) tests/test_fq0_bellman_audit.cpp -o $@

$(FQ0_BELLMAN_RUN_TEST_RUNNER): $(FQ0_BELLMAN_RUN_LINK_SOURCES) tests/test_fq0_bellman_run.cpp $(AC1_TEACHER_AUDIT_HEADERS) $(FQ0_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(FQ0_BELLMAN_RUN_LINK_SOURCES) tests/test_fq0_bellman_run.cpp -o $@

$(FQ0_BELLMAN_AUDIT): $(FQ0_BELLMAN_RUN_LINK_SOURCES) src/fq0_bellman_audit_main.cpp $(AC1_TEACHER_AUDIT_HEADERS) $(FQ0_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(FQ0_BELLMAN_RUN_LINK_SOURCES) src/fq0_bellman_audit_main.cpp -o $@

$(WEB_BRIDGE): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(WEB_BRIDGE_SOURCE) src/web_bridge_main.cpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/web_bridge.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(WEB_BRIDGE_SOURCE) src/web_bridge_main.cpp -o $@

$(WEB_BRIDGE_TEST_RUNNER): $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(WEB_BRIDGE_SOURCE) tests/test_web_bridge.cpp include/old_school/game.hpp include/old_school/learned_iteration.hpp include/old_school/web_bridge.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(WEB_BRIDGE_SOURCE) tests/test_web_bridge.cpp -o $@

$(WEB_DEPENDENCIES): web/package.json web/package-lock.json
	npm --prefix web ci --ignore-scripts

test: $(TEST_RUNNER) $(LEARNED_ITERATION_TEST_RUNNER) $(PROBE_TEST_RUNNER) $(PROBE_EVAL_TEST_RUNNER) $(PROBE_RUNNER_TEST_RUNNER) $(AUDIT_COMMON_TEST_RUNNER) $(ARTIFACT_INTEGRITY_TEST_RUNNER) $(TERMINAL_WEIGHT_EVAL_TEST_RUNNER) $(JOINT_C17_EVAL_TEST_RUNNER) $(JOINT_C17_RUNNER_TEST_RUNNER) $(JOINT_C17_EXECUTION_TEST_RUNNER) $(JOINT_C17_TRAINING_TEST_RUNNER) $(JOINT_C17_ORCHESTRATION_TEST_RUNNER) $(TURN_ALIGNMENT_AUDIT_TEST_RUNNER) $(TARGET_FACTORIAL_AUDIT_TEST_RUNNER) $(REPLAY_WEIGHT_AUDIT_TEST_RUNNER) $(RB0_MECHANICAL_PREFLIGHT_TEST_RUNNER) $(DVR2_HARVEST_TEST_RUNNER) $(DVR2_REPLAY_BUNDLE_TEST_RUNNER) $(DVR2_HARVEST) $(OUTPUT_CALIBRATION_TEST_RUNNER) $(OUTPUT_CALIBRATION_ARTIFACT_TEST_RUNNER) $(OUTPUT_CALIBRATION_RUNNER_TEST_RUNNER) $(OUTPUT_CALIBRATION) $(OC1_ACTION_EVAL_TEST_RUNNER) $(OC1_ACTION_SCORING_TEST_RUNNER) $(OC1_ACTION_REGRESSION_TEST_RUNNER) $(OC1_ACTION_REGRESSION) $(AC1_TEACHER_AUDIT_TEST_RUNNER) $(AC1_TEACHER_AUDIT) $(FQ0_INFORMATION_SET_TEST_RUNNER) $(FQ0_BELLMAN_TEST_RUNNER) $(FQ0_DOMINANCE_TEST_RUNNER) $(FQ0_DOMINANCE_TRANSITION_TEST_RUNNER) $(FQ0_BELLMAN_SCIENCE_TEST_RUNNER) $(FQ0_BELLMAN_AUDIT_TEST_RUNNER) $(FQ0_BELLMAN_RUN_TEST_RUNNER) $(FQ0_BELLMAN_AUDIT) $(WEB_BRIDGE_TEST_RUNNER) $(WEB_BRIDGE) $(WEB_DEPENDENCIES) $(SIMULATOR)
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
	./$(DVR2_HARVEST_TEST_RUNNER)
	./$(DVR2_REPLAY_BUNDLE_TEST_RUNNER)
	sh tests/test_dvr2_once.sh
	./$(OUTPUT_CALIBRATION_TEST_RUNNER)
	./$(OUTPUT_CALIBRATION_ARTIFACT_TEST_RUNNER)
	./$(OUTPUT_CALIBRATION_RUNNER_TEST_RUNNER)
	./$(OC1_ACTION_EVAL_TEST_RUNNER)
	./$(OC1_ACTION_SCORING_TEST_RUNNER)
	./$(OC1_ACTION_REGRESSION_TEST_RUNNER)
	./$(AC1_TEACHER_AUDIT_TEST_RUNNER)
	./$(FQ0_INFORMATION_SET_TEST_RUNNER)
	./$(FQ0_BELLMAN_TEST_RUNNER)
	./$(FQ0_DOMINANCE_TEST_RUNNER)
	./$(FQ0_DOMINANCE_TRANSITION_TEST_RUNNER)
	./$(FQ0_BELLMAN_SCIENCE_TEST_RUNNER)
	./$(FQ0_BELLMAN_AUDIT_TEST_RUNNER)
	./$(FQ0_BELLMAN_RUN_TEST_RUNNER)
	@set +e; output=`./$(FQ0_BELLMAN_AUDIT) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ0-T0 CLI accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage: old-school-fq0-bellman-audit' >/dev/null
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

attack-regression: $(ATTACK_REGRESSION)
	./$(ATTACK_REGRESSION)

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

dvr2-harvest: $(DVR2_HARVEST)

test-dvr2-harvest: $(DVR2_HARVEST_TEST_RUNNER) $(DVR2_HARVEST)
	./$(DVR2_HARVEST_TEST_RUNNER)
	sh tests/test_dvr2_once.sh
	@set +e; output=`./$(DVR2_HARVEST) --seed 4242 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'DVR2 executable accepted a non-output option\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'accepts only its new evidence output path' >/dev/null

test-dvr2-replay-bundle: $(DVR2_REPLAY_BUNDLE_TEST_RUNNER)
	./$(DVR2_REPLAY_BUNDLE_TEST_RUNNER)

test-output-calibration: $(OUTPUT_CALIBRATION_TEST_RUNNER)
	./$(OUTPUT_CALIBRATION_TEST_RUNNER)

test-output-calibration-artifact: $(OUTPUT_CALIBRATION_ARTIFACT_TEST_RUNNER)
	./$(OUTPUT_CALIBRATION_ARTIFACT_TEST_RUNNER)

output-calibration: $(OUTPUT_CALIBRATION)

test-output-calibration-runner: $(OUTPUT_CALIBRATION_RUNNER_TEST_RUNNER) $(OUTPUT_CALIBRATION)
	./$(OUTPUT_CALIBRATION_RUNNER_TEST_RUNNER)
	@set +e; output=`./$(OUTPUT_CALIBRATION) unexpected extra 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'OC1 CLI did not reject extra arguments\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage: old-school-output-calibration' >/dev/null

test-oc1-action-eval: $(OC1_ACTION_EVAL_TEST_RUNNER)
	./$(OC1_ACTION_EVAL_TEST_RUNNER)

test-oc1-action-scoring: $(OC1_ACTION_SCORING_TEST_RUNNER)
	./$(OC1_ACTION_SCORING_TEST_RUNNER)

oc1-action-regression: $(OC1_ACTION_REGRESSION)

test-oc1-action-regression: $(OC1_ACTION_REGRESSION_TEST_RUNNER) $(OC1_ACTION_REGRESSION)
	./$(OC1_ACTION_REGRESSION_TEST_RUNNER)
	@set +e; output=`./$(OC1_ACTION_REGRESSION) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'OC1-AR1 CLI accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage: old-school-oc1-action-regression' >/dev/null

ac1-teacher-audit: $(AC1_TEACHER_AUDIT)

test-ac1-teacher-audit: $(AC1_TEACHER_AUDIT_TEST_RUNNER) $(AC1_TEACHER_AUDIT)
	./$(AC1_TEACHER_AUDIT_TEST_RUNNER)
	@set +e; output=`./$(AC1_TEACHER_AUDIT) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'AC1-T0 CLI accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage: old-school-ac1-teacher-audit' >/dev/null

test-fq0: test-fq0-information-set test-fq0-bellman test-fq0-dominance test-fq0-dominance-transition test-fq0-bellman-science test-fq0-bellman-audit test-fq0-bellman-run

test-fq0-information-set: $(FQ0_INFORMATION_SET_TEST_RUNNER)
	./$(FQ0_INFORMATION_SET_TEST_RUNNER)

test-fq0-bellman: $(FQ0_BELLMAN_TEST_RUNNER)
	./$(FQ0_BELLMAN_TEST_RUNNER)

test-fq0-dominance: $(FQ0_DOMINANCE_TEST_RUNNER)
	./$(FQ0_DOMINANCE_TEST_RUNNER)

test-fq0-dominance-transition: $(FQ0_DOMINANCE_TRANSITION_TEST_RUNNER)
	./$(FQ0_DOMINANCE_TRANSITION_TEST_RUNNER)

test-fq0-bellman-science: $(FQ0_BELLMAN_SCIENCE_TEST_RUNNER)
	./$(FQ0_BELLMAN_SCIENCE_TEST_RUNNER)

test-fq0-bellman-audit: $(FQ0_BELLMAN_AUDIT_TEST_RUNNER)
	./$(FQ0_BELLMAN_AUDIT_TEST_RUNNER)

fq0-bellman-audit: $(FQ0_BELLMAN_AUDIT)

test-fq0-bellman-run: $(FQ0_BELLMAN_RUN_TEST_RUNNER) $(FQ0_BELLMAN_AUDIT)
	./$(FQ0_BELLMAN_RUN_TEST_RUNNER)
	@set +e; output=`./$(FQ0_BELLMAN_AUDIT) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ0-T0 CLI accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage: old-school-fq0-bellman-audit' >/dev/null

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
