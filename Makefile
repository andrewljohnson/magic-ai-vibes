CXX ?= clang++
CXXFLAGS ?= -std=c++20 -O3 -Wall -Wextra -Wpedantic -Werror
CPPFLAGS ?= -Iinclude

BUILD_DIR := build

# Shell-quote an effective make value without evaluating its contents. The
# resulting SHA keeps shared objects from crossing compiler/flag
# configurations while retaining each configuration's warm cache.
shell_quote = '$(subst ','"'"',$(1))'
BUILD_CONFIG_ID := $(shell \
	{ printf '%s\0' \
		$(call shell_quote,$(CXX)) \
		$(call shell_quote,$(CPPFLAGS)) \
		$(call shell_quote,$(CXXFLAGS)); } | \
	{ if command -v shasum >/dev/null 2>&1; then \
		shasum -a 256 | awk '{ print $$1 }'; \
	elif command -v sha256sum >/dev/null 2>&1; then \
		sha256sum | awk '{ print $$1 }'; \
	elif command -v openssl >/dev/null 2>&1; then \
		openssl dgst -sha256 | awk '{ print $$NF }'; \
	fi; })
ifeq ($(strip $(BUILD_CONFIG_ID)),)
$(error a SHA-256 implementation (shasum, sha256sum, or openssl) is required)
endif

OBJ_DIR := $(BUILD_DIR)/obj/$(BUILD_CONFIG_ID)
program_config_sidecar = $(1).compile-config.mk
program_config_variable = OLD_SCHOOL_PROGRAM_CONFIG_$(1)
program_link_objects_variable = OLD_SCHOOL_PROGRAM_LINK_OBJECTS_$(1)
PROGRAM_CONFIG_SIDECARS := $(wildcard $(BUILD_DIR)/*.compile-config.mk)
-include $(PROGRAM_CONFIG_SIDECARS)
recorded_program_config = $($(call program_config_variable,$(1)))
# Every program has at least one link object. Mutual substring containment is
# therefore an exact, order-sensitive equality check without a parse-time
# hashing subprocess.
same_link_objects = $(and \
	$(findstring $(strip $(1)),$(strip $(2))),\
	$(findstring $(strip $(2)),$(strip $(1))))
recorded_program_link_objects = $($(call program_link_objects_variable,$(1)))
program_config_relink = $(if \
	$(and \
		$(filter $(BUILD_CONFIG_ID),$(call recorded_program_config,$(1))),\
		$(call same_link_objects,$(2),\
			$(call recorded_program_link_objects,$(1)))),\
	,FORCE)
# The engine's public search/evaluation seams call the exact public combat
# transition, so every engine consumer links that implementation as one unit.
LEARNED_PRIORITY_BILINEAR_SOURCE := src/learned_priority_bilinear.cpp
LEARNED_PRIORITY_BILINEAR_ARTIFACT_SOURCE := src/learned_priority_bilinear_artifact.cpp
LEARNED_PRIORITY_SPARSE_CROSS_SOURCE := src/learned_priority_sparse_cross.cpp
ENGINE_SOURCE := src/game.cpp src/exact_combat_subgame.cpp $(LEARNED_PRIORITY_BILINEAR_SOURCE) $(LEARNED_PRIORITY_SPARSE_CROSS_SOURCE)
INTERACTIVE_SOURCE := src/interactive.cpp
LEARNED_ITERATION_SOURCE := src/learned_iteration.cpp
PROBE_SOURCE := src/probes.cpp src/dvr1_replay.cpp
PROBE_EVAL_SOURCE := src/probe_eval.cpp
PROBE_RUNNER_SOURCE := src/probe_runner.cpp
AUDIT_COMMON_SOURCE := src/audit_common.cpp
ARTIFACT_INTEGRITY_SOURCE := src/artifact_integrity.cpp
FQ0_RUSAGE_GUARD_SOURCE := src/fq0_rusage_guard.cpp
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
FQ0_SEQUENCE_PROJECTION_SOURCE := src/fq0_sequence_projection.cpp
FQ0_CAUSAL_QUOTIENT_SOURCE := src/fq0_causal_quotient.cpp
FQ4_PRIORITY_MATH_SOURCE := src/fq4_priority_math.cpp
FQ4_PARENT_CLASSIFICATION_SOURCE := src/fq4_parent_classification.cpp
FQ4_PRIORITY_COLLECTION_SOURCE := src/fq4_priority_collection.cpp
FQ4_DEV_BUNDLE_SOURCE := src/fq4_dev_bundle.cpp
FQ4_DEV_GENERATOR_SOURCE := src/fq4_dev_generator.cpp
FQ4_DEV_EVALUATOR_SOURCE := src/fq4_dev_evaluator.cpp
FQ4_DEV_CANDIDATE_ARTIFACT_SOURCE := src/fq4_dev_candidate_artifact.cpp
FQ4_DEV_CANDIDATE_PUBLISHER_SOURCE := src/fq4_dev_candidate_publisher.cpp
FQ4_DEV1_GAMEPLAY_SOURCE := src/fq4_dev1_gameplay.cpp
FQ4_DEV5_GAMEPLAY_SOURCE := src/fq4_dev5_gameplay.cpp
FQ4_BLEND_EXPLORE_SOURCE := src/fq4_blend_explore.cpp
ACTION_Q_EXPLORE_SOURCE := src/action_q_explore.cpp
ACTION_Q_FIELD_GATE_SOURCE := src/action_q_field_gate.cpp
ACTION_Q_OFFLINE_GATE_SOURCE := src/action_q_offline_gate.cpp
ACTION_Q_BELLMAN_TEACHER_SOURCE := src/action_q_bellman_teacher.cpp
ACTION_Q_BELLMAN_EXPLORE_SOURCE := src/action_q_bellman_explore.cpp
ACTION_Q_MULTISCALE_TEACHER_SOURCE := src/action_q_multiscale_teacher.cpp
ACTION_Q_MULTISCALE_EXPLORE_SOURCE := src/action_q_multiscale_explore.cpp
ACTION_Q_LONG_HORIZON_DIAGNOSTIC_SOURCE := src/action_q_long_horizon_diagnostic.cpp
ACTION_Q_NESTED_ACTOR_DIAGNOSTIC_SOURCE := src/action_q_nested_actor_diagnostic.cpp
ACTION_Q_NESTED_ACTOR_DISTILL_SOURCE := src/action_q_nested_actor_distill.cpp
ACTION_Q_NESTED_ACTOR_EARLY_STOP_SOURCE := src/action_q_nested_actor_early_stop.cpp
ACTION_Q_NESTED_ACTOR_ANCHOR_SOURCE := src/action_q_nested_actor_anchor.cpp
ACTION_Q_NESTED_ACTOR_BROAD_DISTILL_SOURCE := src/action_q_nested_actor_broad_distill.cpp
DECISION_BOUNDARY_CRITIC_SOURCE := src/decision_boundary_critic.cpp
DECISION_BOUNDARY_CRITIC_GATE_SOURCE := src/decision_boundary_critic_gate.cpp
DECISION_BOUNDARY_RANK_DIRECT_SOURCE := src/decision_boundary_rank_direct.cpp
DECISION_BOUNDARY_RANK_HIDDEN_SOURCE := src/decision_boundary_rank_hidden.cpp
DECISION_BOUNDARY_ACTION_PAIR_SOURCE := src/decision_boundary_action_pair.cpp
DECISION_BOUNDARY_ADAPTIVE_TRUNK_SOURCE := src/decision_boundary_adaptive_trunk.cpp
DECISION_DENSITY_CENSUS_SOURCE := src/decision_density_census.cpp
DECISION_DENSITY_PRIORITY_SOURCE := src/decision_density_priority.cpp
DECISION_DENSITY_LABELS_SOURCE := src/decision_density_labels.cpp
DECISION_DENSITY_BILINEAR_SOURCE := src/decision_density_bilinear.cpp
DECISION_DENSITY_SPARSE_SUPPORT_SOURCE := src/decision_density_sparse_support.cpp
DECISION_DENSITY_SPARSE_CROSS_SOURCE := src/decision_density_sparse_cross.cpp
ACTION_Q_ON_POLICY_SUCCESSOR_SOURCE := src/action_q_on_policy_successor.cpp
ACTION_Q_PRIORITY_TRUST_REGION_SOURCE := src/action_q_priority_trust_region.cpp
ACTION_Q_RECURSIVE_POLICY_IMPROVEMENT_SOURCE := src/action_q_recursive_policy_improvement.cpp
CONSERVATIVE_POLICY_IMPROVEMENT_SOURCE := src/conservative_policy_improvement.cpp
INFORMATION_SET_PUCT_SOURCE := src/information_set_puct.cpp
INFORMATION_SET_PUCT_PREFLIGHT_SOURCE := src/information_set_puct_preflight.cpp
INFORMATION_SET_PUCT_BUDGET_DIAGNOSTIC_SOURCE := src/information_set_puct_budget_diagnostic.cpp
FQ4_DEV_BACKGROUND_DIAGNOSTIC_SOURCE := src/fq4_dev_background_diagnostic.cpp
FQ4_DEV_COVERAGE_CENSUS_SOURCE := src/fq4_dev_coverage_census.cpp
FQ4_NEUTRAL_SUPPLEMENT_SOURCE := src/fq4_neutral_supplement.cpp
FQ4_WORK0_CACHE_SOURCE := src/fq4_work0_cache.cpp
FQ4_NEUTRAL_PUBLISHER_SOURCE := src/fq4_neutral_publisher.cpp
FQ4_NEUTRAL_EVALUATOR_SOURCE := src/fq4_neutral_evaluator.cpp
FQ4_NEUTRAL_EVALUATOR_RUNNER_SOURCE := src/fq4_neutral_evaluator_runner.cpp
FQ4_NEUTRAL_CANDIDATE_PUBLISHER_SOURCE := src/fq4_neutral_candidate_publisher.cpp
FQ4_PRIORITY_FIT_SOURCE := src/fq4_priority_fit.cpp
FQ4_D1_FIELD_GATE_SOURCE := src/fq4_d1_field_gate.cpp
FQ4_D1_TREATMENT_SOURCE := src/fq4_d1_treatment.cpp
FQ4_D1_TREATMENT_PRODUCTION_SOURCE := src/fq4_d1_treatment_production.cpp
FQ4_DEV_SCHEDULE_SOURCE := src/fq4_dev_schedule.cpp
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
FQ0_RUSAGE_GUARD := $(BUILD_DIR)/old-school-fq0-quarantine-supervisor
FQ0_RUSAGE_GUARD_TEST_RUNNER := $(BUILD_DIR)/old-school-fq0-rusage-guard-tests
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
FQ0_SEQUENCE_PROJECTION_TEST_RUNNER := $(BUILD_DIR)/old-school-fq0-sequence-projection-tests
FQ0_CAUSAL_QUOTIENT_TEST_RUNNER := $(BUILD_DIR)/old-school-fq0-causal-quotient-tests
FQ0_CAUSAL_QUOTIENT := $(BUILD_DIR)/old-school-fq0-causal-quotient
FQ4_PRIORITY_MATH_TEST_RUNNER := $(BUILD_DIR)/old-school-fq4-priority-math-tests
FQ4_PRIORITY_COLLECTION_TEST_RUNNER := $(BUILD_DIR)/old-school-fq4-priority-collection-tests
FQ4_WORK0_CACHE_TEST_RUNNER := $(BUILD_DIR)/old-school-fq4-work0-cache-tests
FQ4_DEV_BUNDLE_TEST_RUNNER := $(BUILD_DIR)/old-school-fq4-dev-bundle-tests
FQ4_DEV_GENERATOR_TEST_RUNNER := $(BUILD_DIR)/old-school-fq4-dev-generator-tests
FQ4_DEV_GENERATOR := $(BUILD_DIR)/old-school-fq4-priority-dev-generate
FQ4_DEV_EVALUATOR_TEST_RUNNER := $(BUILD_DIR)/old-school-fq4-dev-evaluator-tests
FQ4_DEV_EVALUATOR := $(BUILD_DIR)/old-school-fq4-priority-dev-evaluate
FQ4_DEV_CANDIDATE_ARTIFACT_TEST_RUNNER := $(BUILD_DIR)/old-school-fq4-dev-candidate-artifact-tests
FQ4_DEV_CANDIDATE_PUBLISHER_TEST_RUNNER := $(BUILD_DIR)/old-school-fq4-dev-candidate-publisher-tests
FQ4_DEV_CANDIDATE_PUBLISHER := $(BUILD_DIR)/old-school-fq4-dev1-candidate-publish
FQ4_DEV1_GAMEPLAY_TEST_RUNNER := $(BUILD_DIR)/old-school-fq4-dev1-gameplay-tests
FQ4_DEV1_GAMEPLAY := $(BUILD_DIR)/old-school-fq4-dev1-gameplay
FQ4_DEV5_GAMEPLAY_TEST_RUNNER := $(BUILD_DIR)/old-school-fq4-dev5-gameplay-tests
FQ4_DEV5_GAMEPLAY := $(BUILD_DIR)/old-school-fq4-dev5-gameplay
FQ4_BLEND_EXPLORE_TEST_RUNNER := $(BUILD_DIR)/old-school-fq4-blend-explore-tests
FQ4_BLEND_EXPLORE := $(BUILD_DIR)/old-school-fq4-blend-explore
ACTION_Q_EXPLORE_TEST_RUNNER := $(BUILD_DIR)/old-school-action-q-explore-tests
ACTION_Q_FIELD_GATE_TEST_RUNNER := $(BUILD_DIR)/old-school-action-q-field-gate-tests
ACTION_Q_OFFLINE_GATE_TEST_RUNNER := $(BUILD_DIR)/old-school-action-q-offline-gate-tests
ACTION_Q_EXPLORE := $(BUILD_DIR)/old-school-action-q-explore
ACTION_Q_BELLMAN_TEACHER_TEST_RUNNER := $(BUILD_DIR)/old-school-action-q-bellman-teacher-tests
ACTION_Q_BELLMAN_EXPLORE_TEST_RUNNER := $(BUILD_DIR)/old-school-action-q-bellman-explore-tests
ACTION_Q_BELLMAN_EXPLORE := $(BUILD_DIR)/old-school-action-q-bellman-explore
ACTION_Q_MULTISCALE_TEACHER_TEST_RUNNER := $(BUILD_DIR)/old-school-action-q-multiscale-teacher-tests
ACTION_Q_MULTISCALE_EXPLORE_TEST_RUNNER := $(BUILD_DIR)/old-school-action-q-multiscale-explore-tests
ACTION_Q_MULTISCALE_EXPLORE := $(BUILD_DIR)/old-school-action-q-multiscale-explore
ACTION_Q_LONG_HORIZON_DIAGNOSTIC_TEST_RUNNER := $(BUILD_DIR)/old-school-action-q-long-horizon-diagnostic-tests
ACTION_Q_LONG_HORIZON_DIAGNOSTIC := $(BUILD_DIR)/old-school-action-q-long-horizon-diagnostic
ACTION_Q_NESTED_ACTOR_DIAGNOSTIC_TEST_RUNNER := $(BUILD_DIR)/old-school-action-q-nested-actor-diagnostic-tests
ACTION_Q_NESTED_ACTOR_DIAGNOSTIC := $(BUILD_DIR)/old-school-action-q-nested-actor-diagnostic
ACTION_Q_NESTED_ACTOR_DISTILL_TEST_RUNNER := $(BUILD_DIR)/old-school-action-q-nested-actor-distill-tests
ACTION_Q_NESTED_ACTOR_DISTILL := $(BUILD_DIR)/old-school-action-q-nested-actor-distill
ACTION_Q_NESTED_ACTOR_EARLY_STOP_TEST_RUNNER := $(BUILD_DIR)/old-school-action-q-nested-actor-early-stop-tests
ACTION_Q_NESTED_ACTOR_EARLY_STOP := $(BUILD_DIR)/old-school-action-q-nested-actor-early-stop
ACTION_Q_NESTED_ACTOR_ANCHOR_TEST_RUNNER := $(BUILD_DIR)/old-school-action-q-nested-actor-anchor-tests
ACTION_Q_NESTED_ACTOR_ANCHOR := $(BUILD_DIR)/old-school-action-q-nested-actor-anchor
ACTION_Q_NESTED_ACTOR_BROAD_DISTILL_TEST_RUNNER := $(BUILD_DIR)/old-school-action-q-broad-distill-tests
ACTION_Q_NESTED_ACTOR_BROAD_DISTILL := $(BUILD_DIR)/old-school-action-q-broad-distill
DECISION_BOUNDARY_CRITIC_TEST_RUNNER := $(BUILD_DIR)/old-school-decision-boundary-critic-tests
DECISION_BOUNDARY_CRITIC := $(BUILD_DIR)/old-school-decision-boundary-critic
DECISION_BOUNDARY_RANK_DIRECT_TEST_RUNNER := $(BUILD_DIR)/old-school-decision-boundary-rank-direct-tests
DECISION_BOUNDARY_RANK_DIRECT := $(BUILD_DIR)/old-school-decision-boundary-rank-direct
DECISION_BOUNDARY_RANK_HIDDEN_TEST_RUNNER := $(BUILD_DIR)/old-school-decision-boundary-rank-hidden-tests
DECISION_BOUNDARY_RANK_HIDDEN := $(BUILD_DIR)/old-school-decision-boundary-rank-hidden
DECISION_BOUNDARY_ACTION_PAIR_TEST_RUNNER := $(BUILD_DIR)/old-school-decision-boundary-action-pair-tests
DECISION_BOUNDARY_ACTION_PAIR := $(BUILD_DIR)/old-school-decision-boundary-action-pair
DECISION_BOUNDARY_ADAPTIVE_TRUNK_TEST_RUNNER := $(BUILD_DIR)/old-school-decision-boundary-adaptive-trunk-tests
DECISION_BOUNDARY_ADAPTIVE_TRUNK := $(BUILD_DIR)/old-school-decision-boundary-adaptive-trunk
DECISION_DENSITY_CENSUS_TEST_RUNNER := $(BUILD_DIR)/old-school-decision-density-census-tests
DECISION_DENSITY_CENSUS := $(BUILD_DIR)/old-school-decision-density-census
DECISION_DENSITY_PRIORITY_TEST_RUNNER := $(BUILD_DIR)/old-school-decision-density-priority-tests
DECISION_DENSITY_PRIORITY := $(BUILD_DIR)/old-school-decision-density-priority
DECISION_DENSITY_LABELS_TEST_RUNNER := $(BUILD_DIR)/old-school-decision-density-labels-tests
DECISION_DENSITY_LABELS := $(BUILD_DIR)/old-school-decision-density-labels
LEARNED_PRIORITY_BILINEAR_TEST_RUNNER := $(BUILD_DIR)/old-school-learned-priority-bilinear-tests
LEARNED_PRIORITY_BILINEAR_ARTIFACT_TEST_RUNNER := $(BUILD_DIR)/old-school-learned-priority-bilinear-artifact-tests
LEARNED_PRIORITY_SPARSE_CROSS_TEST_RUNNER := $(BUILD_DIR)/old-school-learned-priority-sparse-cross-tests
DECISION_DENSITY_BILINEAR_TEST_RUNNER := $(BUILD_DIR)/old-school-decision-density-bilinear-tests
DECISION_DENSITY_BILINEAR := $(BUILD_DIR)/old-school-decision-density-bilinear
DECISION_DENSITY_BILINEAR_ARTIFACT := $(BUILD_DIR)/old-school-decision-density-bilinear-artifact
DECISION_DENSITY_SPARSE_SUPPORT_TEST_RUNNER := $(BUILD_DIR)/old-school-decision-density-sparse-support-tests
DECISION_DENSITY_SPARSE_SUPPORT := $(BUILD_DIR)/old-school-decision-density-sparse-support
DECISION_DENSITY_SPARSE_CROSS_TEST_RUNNER := $(BUILD_DIR)/old-school-decision-density-sparse-cross-tests
DECISION_DENSITY_SPARSE_CROSS := $(BUILD_DIR)/old-school-decision-density-sparse-cross
ACTION_Q_ON_POLICY_SUCCESSOR_TEST_RUNNER := $(BUILD_DIR)/old-school-action-q-on-policy-successor-tests
ACTION_Q_ON_POLICY_SUCCESSOR := $(BUILD_DIR)/old-school-action-q-on-policy-successor
ACTION_Q_PRIORITY_TRUST_REGION_TEST_RUNNER := $(BUILD_DIR)/old-school-action-q-priority-trust-region-tests
ACTION_Q_PRIORITY_TRUST_REGION := $(BUILD_DIR)/old-school-action-q-priority-trust-region
ACTION_Q_RECURSIVE_POLICY_IMPROVEMENT_TEST_RUNNER := $(BUILD_DIR)/old-school-action-q-recursive-policy-improvement-tests
ACTION_Q_RECURSIVE_POLICY_IMPROVEMENT := $(BUILD_DIR)/old-school-action-q-recursive-policy-improvement
CONSERVATIVE_POLICY_IMPROVEMENT_TEST_RUNNER := $(BUILD_DIR)/old-school-conservative-policy-improvement-tests
EXACT_COMBAT_SUBGAME_TEST_RUNNER := $(BUILD_DIR)/old-school-exact-combat-subgame-tests
INFORMATION_SET_PUCT_TEST_RUNNER := $(BUILD_DIR)/old-school-information-set-puct-tests
LEARNED_GENERATIVE_SEARCH_TEST_RUNNER := $(BUILD_DIR)/old-school-learned-generative-search-tests
INFORMATION_SET_PUCT_PREFLIGHT_TEST_RUNNER := $(BUILD_DIR)/old-school-information-set-puct-preflight-tests
INFORMATION_SET_PUCT_PREFLIGHT := $(BUILD_DIR)/old-school-information-set-puct-preflight
INFORMATION_SET_PUCT_BUDGET_DIAGNOSTIC_TEST_RUNNER := $(BUILD_DIR)/old-school-information-set-puct-budget-diagnostic-tests
INFORMATION_SET_PUCT_BUDGET_DIAGNOSTIC := $(BUILD_DIR)/old-school-information-set-puct-budget-diagnostic
FQ4_DEV_BACKGROUND_DIAGNOSTIC_TEST_RUNNER := $(BUILD_DIR)/old-school-fq4-dev2-background-diagnostic-tests
FQ4_DEV_BACKGROUND_DIAGNOSTIC := $(BUILD_DIR)/old-school-fq4-dev2-background-diagnostic
FQ4_DEV_COVERAGE_CENSUS_TEST_RUNNER := $(BUILD_DIR)/old-school-fq4-dev4-coverage-census-tests
FQ4_DEV_COVERAGE_CENSUS := $(BUILD_DIR)/old-school-fq4-dev4-coverage-census
FQ4_NEUTRAL_SUPPLEMENT_TEST_RUNNER := $(BUILD_DIR)/old-school-fq4-dev5-neutral-supplement-tests
FQ4_NEUTRAL_PUBLISHER_TEST_RUNNER := $(BUILD_DIR)/old-school-fq4-dev5-neutral-publisher-tests
FQ4_NEUTRAL_PUBLISHER := $(BUILD_DIR)/old-school-fq4-dev5-neutral-publish
FQ4_NEUTRAL_EVALUATOR_TEST_RUNNER := $(BUILD_DIR)/old-school-fq4-dev5-neutral-evaluator-tests
FQ4_NEUTRAL_EVALUATOR_RUNNER_TEST_RUNNER := $(BUILD_DIR)/old-school-fq4-dev5-neutral-evaluator-runner-tests
FQ4_NEUTRAL_EVALUATOR := $(BUILD_DIR)/old-school-fq4-dev5-neutral-evaluate
FQ4_NEUTRAL_CANDIDATE_PUBLISHER_TEST_RUNNER := $(BUILD_DIR)/old-school-fq4-dev5-candidate-publisher-tests
FQ4_NEUTRAL_CANDIDATE_PUBLISHER := $(BUILD_DIR)/old-school-fq4-dev5-candidate-publish
FQ4_PRIORITY_FIT_TEST_RUNNER := $(BUILD_DIR)/old-school-fq4-priority-fit-tests
FQ4_PRIORITY_FIT := $(BUILD_DIR)/old-school-fq4-priority-fit
FQ4_PRIORITY_FIT_D0B := $(BUILD_DIR)/old-school-fq4-priority-fit-d0b
FQ4_D1_FIELD_GATE_TEST_RUNNER := $(BUILD_DIR)/old-school-fq4-d1-field-gate-tests
FQ4_D1_CENSUS := $(BUILD_DIR)/old-school-fq4-priority-fit-d1-census
FQ4_D1_TREATMENT_TEST_RUNNER := $(BUILD_DIR)/old-school-fq4-d1-treatment-tests
FQ4_D1_TREATMENT := $(BUILD_DIR)/old-school-fq4-priority-fit-d1
FQ4_DEV_SCHEDULE_TEST_RUNNER := $(BUILD_DIR)/old-school-fq4-dev-schedule-tests
FQ4_DEV_SCHEDULE := $(BUILD_DIR)/old-school-fq4-priority-dev-schedule
WEB_BRIDGE := $(BUILD_DIR)/old-school-web-bridge
WEB_BRIDGE_TEST_RUNNER := $(BUILD_DIR)/old-school-web-bridge-tests
WEB_DEPENDENCIES := web/node_modules/.package-lock.json
LEARNED_ROLLOUTS ?= 2
LEARNED_GENERATIONS ?= 0
CHALLENGER_GENERATIONS ?= 1

ALL_CPP := $(wildcard src/*.cpp tests/*.cpp)
source_objects = $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(1))
DEPFILES := $(patsubst %.o,%.d,$(call source_objects,$(ALL_CPP)))
FQ4_DEV_PRODUCER_COMMIT := $(shell git rev-parse --verify HEAD 2>/dev/null)
ifeq ($(strip $(FQ4_DEV_PRODUCER_COMMIT)),)
$(error a Git HEAD is required to build the FQ4 development generator)
endif
FQ4_DEV_GENERATOR_MAIN_OBJECT := $(OBJ_DIR)/src/fq4_dev_generator_main.$(FQ4_DEV_PRODUCER_COMMIT).o
FQ4_DEV_GENERATOR_MAIN_DEPFILE := $(FQ4_DEV_GENERATOR_MAIN_OBJECT:.o=.d)
FQ4_NEUTRAL_PUBLISHER_MAIN_OBJECT := $(OBJ_DIR)/src/fq4_neutral_publisher_main.$(FQ4_DEV_PRODUCER_COMMIT).o
FQ4_NEUTRAL_PUBLISHER_MAIN_DEPFILE := $(FQ4_NEUTRAL_PUBLISHER_MAIN_OBJECT:.o=.d)
FQ4_NEUTRAL_CANDIDATE_PUBLISHER_MAIN_OBJECT := $(OBJ_DIR)/src/fq4_neutral_candidate_publisher_main.$(FQ4_DEV_PRODUCER_COMMIT).o
FQ4_NEUTRAL_CANDIDATE_PUBLISHER_MAIN_DEPFILE := $(FQ4_NEUTRAL_CANDIDATE_PUBLISHER_MAIN_OBJECT:.o=.d)

.PHONY: FORCE all test test-build-graph test-capture test-certify test-clean-contract test-learned-iteration test-probes attack-regression test-audit-common test-artifact-integrity test-fq0-rusage-guard fq0-quarantine-supervisor test-terminal-weight-eval test-joint-c17-eval test-joint-c17-runner test-joint-c17-execution test-joint-c17-training test-joint-c17-orchestration test-turn-alignment-audit test-target-factorial-audit test-replay-weight-audit test-rb0-mechanical-preflight rb0-mechanical-preflight test-dvr2-harvest dvr2-harvest test-dvr2-replay-bundle test-output-calibration test-output-calibration-artifact test-output-calibration-runner output-calibration test-oc1-action-eval test-oc1-action-scoring test-oc1-action-regression oc1-action-regression test-ac1-teacher-audit ac1-teacher-audit test-fq0 test-fq0-information-set test-fq0-bellman test-fq0-dominance test-fq0-dominance-transition test-fq0-bellman-science test-fq0-bellman-audit test-fq0-bellman-run fq0-bellman-audit test-fq0-sequence-projection test-fq0-causal-quotient test-fq0-causal-quotient-production fq0-causal-quotient test-fq4-priority-math test-fq4-priority-collection test-fq4-dev-bundle test-fq4-dev-generator test-fq4-dev-evaluator fq4-dev-evaluator test-fq4-dev-candidate-artifact test-fq4-dev-candidate-publisher fq4-dev-candidate-publish test-fq4-dev1-gameplay test-fq4-dev5-gameplay fq4-dev5-gameplay-smoke test-fq4-blend-explore fq4-blend-explore test-fq4-dev-background-diagnostic fq4-dev-background-diagnostic test-fq4-dev-coverage-census fq4-dev-coverage-census test-fq4-neutral-candidate-publisher fq4-neutral-candidate-publish test-fq4-priority-fit fq4-priority-fit test-fq4-priority-fit-d0b fq4-priority-fit-d0b test-fq4-d1-field-gate fq4-d1-census test-fq4-d1-treatment fq4-d1-treatment test-fq4-dev-schedule fq4-dev-schedule test-web test-web-ui test-web-rendered web web-target-stack web-interaction web-journey web-delayed-journey web-build benchmark benchmark-deep benchmark-learned benchmark-challenger stability evolve run clean
.PHONY: test-fq4-work0-cache test-fq4-work0-firewall
.PHONY: test-action-q-explore test-action-q-field-gate test-action-q-offline-gate action-q-census action-q-run
.PHONY: test-action-q-bellman-teacher test-action-q-bellman-explore action-q-bellman-census action-q-bellman-run
.PHONY: test-action-q-multiscale-teacher test-action-q-multiscale-explore action-q-multiscale-census action-q-multiscale-run
.PHONY: test-action-q-long-horizon-diagnostic action-q-long-horizon-diagnose
.PHONY: test-action-q-nested-actor-diagnostic action-q-nested-actor-diagnose
.PHONY: test-action-q-nested-actor-distill action-q-nested-actor-distill-census action-q-nested-actor-distill-run
.PHONY: test-action-q-nested-actor-early-stop action-q-nested-actor-early-stop-run
.PHONY: test-action-q-nested-actor-anchor action-q-nested-actor-anchor-run
.PHONY: test-action-q-broad-distill action-q-broad-distill-preflight action-q-broad-distill-census action-q-broad-distill-run
.PHONY: test-decision-boundary-critic old-school-decision-boundary-critic decision-boundary-critic-census decision-boundary-critic-run decision-boundary-critic-cache
.PHONY: test-decision-boundary-rank-direct decision-boundary-rank-direct-run
.PHONY: test-decision-boundary-rank-hidden decision-boundary-rank-hidden-run
.PHONY: test-decision-boundary-action-pair decision-boundary-action-pair-run
.PHONY: test-decision-boundary-adaptive-trunk decision-boundary-adaptive-trunk-run
.PHONY: test-decision-density-census old-school-decision-density-census decision-density-census
.PHONY: test-decision-density-priority old-school-decision-density-priority decision-density-priority
.PHONY: test-decision-density-labels old-school-decision-density-labels decision-density-labels-publish
.PHONY: test-learned-priority-bilinear test-learned-priority-bilinear-artifact test-decision-density-bilinear decision-density-bilinear-run decision-density-bilinear-offline-report decision-density-bilinear-artifact-publish
.PHONY: test-learned-priority-sparse-cross test-decision-density-sparse-support decision-density-sparse-support-census
.PHONY: test-decision-density-sparse-cross decision-density-sparse-cross-offline-report
.PHONY: test-action-q-on-policy-successor action-q-on-policy-successor-census action-q-on-policy-successor-run
.PHONY: test-action-q-priority-trust-region action-q-priority-trust-region-run
.PHONY: test-action-q-recursive-policy-improvement action-q-recursive-policy-improvement-run
.PHONY: test-conservative-policy-improvement test-exact-combat-subgame test-information-set-puct test-learned-generative-search test-information-set-puct-preflight test-information-set-puct-budget-diagnostic

all: $(SIMULATOR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

FORCE:

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p "$(@D)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP \
		-MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

# Keep each program's exact ordered translation-unit list at its declaration.
# The same source/configuration maps to the same object, so parallel builds
# compile it once and every consumer waits on that shared prerequisite. Each
# stable program path records its own configuration and ordered link objects
# only after a successful link; a mismatched or missing sidecar adds FORCE for
# that program alone.
define link_program
$(1): $(call source_objects,$(2)) $(call program_config_relink,$(1),$(call source_objects,$(2))) | $(BUILD_DIR)
	@rm -f -- "$$@.compile-config.mk"
	$$(CXX) $$(CPPFLAGS) $$(CXXFLAGS) $(call source_objects,$(2)) -o $$@
	@temporary="$$@.compile-config.mk.tmp.$$$$$$$$"; \
	printf '%s := %s\n%s := %s\n' \
		"$(call program_config_variable,$(1))" \
		"$(BUILD_CONFIG_ID)" \
		"$(call program_link_objects_variable,$(1))" \
		"$(call source_objects,$(2))" >"$$$$temporary" && \
	mv -f -- "$$$$temporary" "$$@.compile-config.mk"
endef

SIMULATOR_LINK_SOURCES := $(ENGINE_SOURCE) $(INTERACTIVE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) $(JOINT_C17_EVAL_SOURCE) $(JOINT_C17_RUNNER_SOURCE) $(JOINT_C17_EXECUTION_SOURCE) $(JOINT_C17_TRAINING_SOURCE) $(JOINT_C17_ORCHESTRATION_SOURCE) $(TURN_ALIGNMENT_AUDIT_SOURCE) $(TARGET_FACTORIAL_AUDIT_SOURCE) $(REPLAY_WEIGHT_AUDIT_SOURCE) src/main.cpp

$(eval $(call link_program,$(SIMULATOR),$(SIMULATOR_LINK_SOURCES)))

$(eval $(call link_program,$(TEST_RUNNER),$(ENGINE_SOURCE) $(INTERACTIVE_SOURCE) $(LEARNED_ITERATION_SOURCE) tests/test_game.cpp))

$(eval $(call link_program,$(LEARNED_ITERATION_TEST_RUNNER),$(LEARNED_ITERATION_SOURCE) tests/test_learned_iteration.cpp))

$(eval $(call link_program,$(PROBE_TEST_RUNNER),$(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) tests/test_probes.cpp))

$(eval $(call link_program,$(PROBE_EVAL_TEST_RUNNER),$(PROBE_EVAL_SOURCE) tests/test_probe_eval.cpp))

$(eval $(call link_program,$(PROBE_RUNNER_TEST_RUNNER),$(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) tests/test_probe_runner.cpp))

$(eval $(call link_program,$(ATTACK_REGRESSION),$(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) src/attack_regression_main.cpp))

$(eval $(call link_program,$(AUDIT_COMMON_TEST_RUNNER),$(AUDIT_COMMON_SOURCE) tests/test_audit_common.cpp))

$(eval $(call link_program,$(ARTIFACT_INTEGRITY_TEST_RUNNER),$(ARTIFACT_INTEGRITY_SOURCE) tests/test_artifact_integrity.cpp))

$(eval $(call link_program,$(FQ0_RUSAGE_GUARD),$(ARTIFACT_INTEGRITY_SOURCE) $(FQ0_RUSAGE_GUARD_SOURCE) src/fq0_rusage_guard_main.cpp))

$(eval $(call link_program,$(FQ0_RUSAGE_GUARD_TEST_RUNNER),$(ARTIFACT_INTEGRITY_SOURCE) $(FQ0_RUSAGE_GUARD_SOURCE) tests/test_fq0_rusage_guard.cpp))

$(eval $(call link_program,$(TERMINAL_WEIGHT_EVAL_TEST_RUNNER),$(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) tests/test_terminal_weight_eval.cpp))

$(eval $(call link_program,$(JOINT_C17_EVAL_TEST_RUNNER),$(PROBE_EVAL_SOURCE) $(JOINT_C17_EVAL_SOURCE) $(AUDIT_COMMON_SOURCE) tests/test_joint_c17_eval.cpp))

$(eval $(call link_program,$(JOINT_C17_RUNNER_TEST_RUNNER),$(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(PROBE_EVAL_SOURCE) $(AUDIT_COMMON_SOURCE) $(JOINT_C17_EVAL_SOURCE) $(JOINT_C17_RUNNER_SOURCE) tests/test_joint_c17_runner.cpp))

$(eval $(call link_program,$(JOINT_C17_EXECUTION_TEST_RUNNER),$(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) $(JOINT_C17_EVAL_SOURCE) $(JOINT_C17_RUNNER_SOURCE) $(JOINT_C17_EXECUTION_SOURCE) tests/test_joint_c17_execution.cpp))

$(eval $(call link_program,$(JOINT_C17_TRAINING_TEST_RUNNER),$(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_EVAL_SOURCE) $(AUDIT_COMMON_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(JOINT_C17_EVAL_SOURCE) $(JOINT_C17_RUNNER_SOURCE) $(JOINT_C17_TRAINING_SOURCE) tests/test_joint_c17_training.cpp))

$(eval $(call link_program,$(JOINT_C17_ORCHESTRATION_TEST_RUNNER),$(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) $(JOINT_C17_EVAL_SOURCE) $(JOINT_C17_RUNNER_SOURCE) $(JOINT_C17_EXECUTION_SOURCE) $(JOINT_C17_ORCHESTRATION_SOURCE) tests/test_joint_c17_orchestration.cpp))

$(eval $(call link_program,$(TURN_ALIGNMENT_AUDIT_TEST_RUNNER),$(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) $(TURN_ALIGNMENT_AUDIT_SOURCE) tests/test_turn_alignment_audit.cpp))

$(eval $(call link_program,$(TARGET_FACTORIAL_AUDIT_TEST_RUNNER),$(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) $(TURN_ALIGNMENT_AUDIT_SOURCE) $(TARGET_FACTORIAL_AUDIT_SOURCE) tests/test_target_factorial_audit.cpp))

$(eval $(call link_program,$(REPLAY_WEIGHT_AUDIT_TEST_RUNNER),$(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) $(REPLAY_WEIGHT_AUDIT_SOURCE) tests/test_replay_weight_audit.cpp))

$(eval $(call link_program,$(RB0_MECHANICAL_PREFLIGHT),$(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) $(REPLAY_WEIGHT_AUDIT_SOURCE) $(RB0_MECHANICAL_PREFLIGHT_SOURCE) src/rb0_mechanical_preflight_main.cpp))

$(eval $(call link_program,$(RB0_MECHANICAL_PREFLIGHT_TEST_RUNNER),$(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(TERMINAL_WEIGHT_EVAL_SOURCE) $(REPLAY_WEIGHT_AUDIT_SOURCE) $(RB0_MECHANICAL_PREFLIGHT_SOURCE) tests/test_rb0_mechanical_preflight.cpp))

$(eval $(call link_program,$(DVR2_HARVEST),$(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(DVR2_HARVEST_SOURCE) src/dvr2_harvest_main.cpp))

$(eval $(call link_program,$(DVR2_HARVEST_TEST_RUNNER),$(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(DVR2_HARVEST_SOURCE) tests/test_dvr2_harvest.cpp))

$(eval $(call link_program,$(DVR2_REPLAY_BUNDLE_TEST_RUNNER),$(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(DVR2_REPLAY_BUNDLE_SOURCE) tests/test_dvr2_replay_bundle.cpp))

$(eval $(call link_program,$(OUTPUT_CALIBRATION_TEST_RUNNER),$(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(AUDIT_COMMON_SOURCE) $(OUTPUT_CALIBRATION_SOURCE) tests/test_output_calibration.cpp))

$(eval $(call link_program,$(OUTPUT_CALIBRATION_ARTIFACT_TEST_RUNNER),$(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(AUDIT_COMMON_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(OUTPUT_CALIBRATION_SOURCE) $(OUTPUT_CALIBRATION_ARTIFACT_SOURCE) tests/test_output_calibration_artifact.cpp))

$(eval $(call link_program,$(OUTPUT_CALIBRATION),$(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(AUDIT_COMMON_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(OUTPUT_CALIBRATION_SOURCE) $(OUTPUT_CALIBRATION_ARTIFACT_SOURCE) $(OUTPUT_CALIBRATION_RUNNER_SOURCE) src/output_calibration_main.cpp))

$(eval $(call link_program,$(OUTPUT_CALIBRATION_RUNNER_TEST_RUNNER),$(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(AUDIT_COMMON_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(OUTPUT_CALIBRATION_SOURCE) $(OUTPUT_CALIBRATION_ARTIFACT_SOURCE) $(OUTPUT_CALIBRATION_RUNNER_SOURCE) tests/test_output_calibration_runner.cpp))

OC1_ACTION_EVAL_LINK_SOURCES := $(PROBE_EVAL_SOURCE) $(OC1_ACTION_EVAL_SOURCE)

$(eval $(call link_program,$(OC1_ACTION_EVAL_TEST_RUNNER),$(OC1_ACTION_EVAL_LINK_SOURCES) tests/test_oc1_action_eval.cpp))

$(eval $(call link_program,$(OC1_ACTION_SCORING_TEST_RUNNER),$(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(OC1_ACTION_SCORING_SOURCE) tests/test_oc1_action_scoring.cpp))

OC1_ACTION_REGRESSION_LINK_SOURCES := $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(DVR2_REPLAY_BUNDLE_SOURCE) $(OUTPUT_CALIBRATION_SOURCE) $(OUTPUT_CALIBRATION_ARTIFACT_SOURCE) $(OC1_ACTION_EVAL_SOURCE) $(OC1_ACTION_SCORING_SOURCE) $(OC1_ACTION_REGRESSION_SOURCE)

$(eval $(call link_program,$(OC1_ACTION_REGRESSION_TEST_RUNNER),$(OC1_ACTION_REGRESSION_LINK_SOURCES) tests/test_oc1_action_regression.cpp))

$(eval $(call link_program,$(OC1_ACTION_REGRESSION),$(OC1_ACTION_REGRESSION_LINK_SOURCES) src/oc1_action_regression_main.cpp))

AC1_TEACHER_AUDIT_LINK_SOURCES := $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(AUDIT_COMMON_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(OUTPUT_CALIBRATION_SOURCE) $(OUTPUT_CALIBRATION_ARTIFACT_SOURCE) $(OC1_ACTION_SCORING_SOURCE) $(AC1_TEACHER_AUDIT_SOURCE)
FQ0_INFORMATION_SET_LINK_SOURCES := $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(FQ0_INFORMATION_SET_SOURCE)
FQ0_DOMINANCE_LINK_SOURCES := $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(FQ0_DOMINANCE_SOURCE) $(FQ0_DOMINANCE_TRANSITION_SOURCE)
FQ0_BELLMAN_SCIENCE_LINK_SOURCES := $(AC1_TEACHER_AUDIT_LINK_SOURCES) $(FQ0_INFORMATION_SET_SOURCE) $(FQ0_BELLMAN_SOURCE) $(FQ0_BELLMAN_SCIENCE_SOURCE)
FQ0_BELLMAN_AUDIT_LINK_SOURCES := $(AC1_TEACHER_AUDIT_LINK_SOURCES) $(FQ0_INFORMATION_SET_SOURCE) $(FQ0_BELLMAN_SOURCE) $(FQ0_DOMINANCE_SOURCE) $(FQ0_BELLMAN_AUDIT_SOURCE)
FQ0_BELLMAN_RUN_LINK_SOURCES := $(AC1_TEACHER_AUDIT_LINK_SOURCES) $(FQ0_INFORMATION_SET_SOURCE) $(FQ0_BELLMAN_SOURCE) $(FQ0_DOMINANCE_SOURCE) $(FQ0_DOMINANCE_TRANSITION_SOURCE) $(FQ0_BELLMAN_SCIENCE_SOURCE) $(FQ0_BELLMAN_AUDIT_SOURCE) $(FQ0_BELLMAN_RUN_SOURCE)
FQ0_SEQUENCE_PROJECTION_LINK_SOURCES := $(FQ0_INFORMATION_SET_LINK_SOURCES) $(FQ0_SEQUENCE_PROJECTION_SOURCE)
FQ0_CAUSAL_QUOTIENT_LINK_SOURCES := $(FQ0_SEQUENCE_PROJECTION_LINK_SOURCES) $(FQ0_CAUSAL_QUOTIENT_SOURCE)
FQ4_PRIORITY_COLLECTION_LINK_SOURCES := $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(FQ0_INFORMATION_SET_SOURCE) $(FQ0_DOMINANCE_SOURCE) $(FQ0_DOMINANCE_TRANSITION_SOURCE) $(FQ4_PARENT_CLASSIFICATION_SOURCE) $(FQ4_PRIORITY_COLLECTION_SOURCE)
FQ4_DEV_BUNDLE_LINK_SOURCES := $(ARTIFACT_INTEGRITY_SOURCE) $(FQ4_DEV_BUNDLE_SOURCE)
FQ4_WORK0_CACHE_LINK_SOURCES := $(FQ4_PRIORITY_COLLECTION_LINK_SOURCES) $(FQ4_DEV_SCHEDULE_SOURCE) $(FQ4_DEV_BUNDLE_SOURCE) $(FQ4_NEUTRAL_SUPPLEMENT_SOURCE) $(FQ4_WORK0_CACHE_SOURCE)
FQ4_DEV_GENERATOR_LINK_SOURCES := $(FQ4_PRIORITY_COLLECTION_LINK_SOURCES) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(OC1_ACTION_SCORING_SOURCE) $(FQ4_PRIORITY_MATH_SOURCE) $(FQ4_DEV_SCHEDULE_SOURCE) $(FQ4_DEV_BUNDLE_SOURCE) $(FQ4_DEV_GENERATOR_SOURCE) $(FQ4_DEV_COVERAGE_CENSUS_SOURCE) $(FQ4_NEUTRAL_SUPPLEMENT_SOURCE) $(FQ4_WORK0_CACHE_SOURCE)
FQ4_DEV_EVALUATOR_LINK_SOURCES := $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(FQ4_PARENT_CLASSIFICATION_SOURCE) $(FQ4_PRIORITY_MATH_SOURCE) $(FQ4_DEV_BUNDLE_SOURCE) $(FQ4_DEV_EVALUATOR_SOURCE)
FQ4_DEV_CANDIDATE_ARTIFACT_LINK_SOURCES := $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(FQ4_DEV_CANDIDATE_ARTIFACT_SOURCE)
FQ4_DEV_CANDIDATE_PUBLISHER_LINK_SOURCES := $(FQ4_DEV_EVALUATOR_LINK_SOURCES) $(FQ4_DEV_CANDIDATE_ARTIFACT_SOURCE) $(FQ4_DEV_CANDIDATE_PUBLISHER_SOURCE)
FQ4_DEV1_GAMEPLAY_LINK_SOURCES := $(FQ4_DEV_CANDIDATE_ARTIFACT_LINK_SOURCES) $(FQ4_DEV1_GAMEPLAY_SOURCE)
FQ4_DEV5_GAMEPLAY_LINK_SOURCES := $(FQ4_DEV_CANDIDATE_ARTIFACT_LINK_SOURCES) $(FQ4_DEV5_GAMEPLAY_SOURCE)
FQ4_BLEND_EXPLORE_LINK_SOURCES := $(FQ4_DEV5_GAMEPLAY_LINK_SOURCES) $(FQ4_BLEND_EXPLORE_SOURCE)
ACTION_Q_EXPLORE_LINK_SOURCES := $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(ACTION_Q_EXPLORE_SOURCE) $(ACTION_Q_FIELD_GATE_SOURCE)
ACTION_Q_OFFLINE_GATE_LINK_SOURCES := $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(ACTION_Q_EXPLORE_SOURCE) $(ACTION_Q_FIELD_GATE_SOURCE) $(ACTION_Q_OFFLINE_GATE_SOURCE)
ACTION_Q_BELLMAN_TEACHER_LINK_SOURCES := $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(FQ0_INFORMATION_SET_SOURCE) $(FQ0_BELLMAN_SOURCE) $(ACTION_Q_FIELD_GATE_SOURCE) $(ACTION_Q_BELLMAN_TEACHER_SOURCE)
ACTION_Q_BELLMAN_EXPLORE_LINK_SOURCES := $(ACTION_Q_OFFLINE_GATE_LINK_SOURCES) $(FQ0_INFORMATION_SET_SOURCE) $(FQ0_BELLMAN_SOURCE) $(ACTION_Q_BELLMAN_TEACHER_SOURCE) $(ACTION_Q_BELLMAN_EXPLORE_SOURCE)
ACTION_Q_MULTISCALE_TEACHER_LINK_SOURCES := $(ACTION_Q_BELLMAN_TEACHER_LINK_SOURCES) $(ACTION_Q_MULTISCALE_TEACHER_SOURCE)
ACTION_Q_MULTISCALE_EXPLORE_LINK_SOURCES := $(ACTION_Q_OFFLINE_GATE_LINK_SOURCES) $(FQ0_INFORMATION_SET_SOURCE) $(FQ0_BELLMAN_SOURCE) $(ACTION_Q_BELLMAN_TEACHER_SOURCE) $(ACTION_Q_MULTISCALE_TEACHER_SOURCE) $(ACTION_Q_MULTISCALE_EXPLORE_SOURCE)
ACTION_Q_LONG_HORIZON_DIAGNOSTIC_LINK_SOURCES := $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(ACTION_Q_LONG_HORIZON_DIAGNOSTIC_SOURCE)
ACTION_Q_NESTED_ACTOR_DIAGNOSTIC_LINK_SOURCES := $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(ACTION_Q_NESTED_ACTOR_DIAGNOSTIC_SOURCE)
ACTION_Q_NESTED_ACTOR_DISTILL_LINK_SOURCES := $(ACTION_Q_OFFLINE_GATE_LINK_SOURCES) $(ACTION_Q_NESTED_ACTOR_DIAGNOSTIC_SOURCE) $(ACTION_Q_NESTED_ACTOR_DISTILL_SOURCE)
ACTION_Q_NESTED_ACTOR_EARLY_STOP_LINK_SOURCES := $(ACTION_Q_NESTED_ACTOR_DISTILL_LINK_SOURCES) $(ACTION_Q_NESTED_ACTOR_EARLY_STOP_SOURCE)
FQ4_DEV_BACKGROUND_DIAGNOSTIC_LINK_SOURCES := $(FQ4_DEV_EVALUATOR_LINK_SOURCES) $(FQ4_DEV_BACKGROUND_DIAGNOSTIC_SOURCE)
FQ4_DEV_COVERAGE_CENSUS_LINK_SOURCES := $(FQ4_DEV_GENERATOR_LINK_SOURCES)
FQ4_NEUTRAL_SUPPLEMENT_LINK_SOURCES := $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(FQ4_DEV_BUNDLE_LINK_SOURCES) $(FQ4_DEV_SCHEDULE_SOURCE) $(FQ4_NEUTRAL_SUPPLEMENT_SOURCE)
FQ4_NEUTRAL_PUBLISHER_LINK_SOURCES := $(FQ4_DEV_COVERAGE_CENSUS_LINK_SOURCES) $(FQ4_NEUTRAL_PUBLISHER_SOURCE)
FQ4_NEUTRAL_EVALUATOR_LINK_SOURCES := $(FQ4_DEV_EVALUATOR_LINK_SOURCES) $(FQ4_DEV_CANDIDATE_ARTIFACT_SOURCE) $(FQ4_DEV_SCHEDULE_SOURCE) $(FQ4_NEUTRAL_SUPPLEMENT_SOURCE) $(FQ4_NEUTRAL_EVALUATOR_SOURCE)
FQ4_NEUTRAL_EVALUATOR_RUNNER_LINK_SOURCES := $(FQ4_NEUTRAL_EVALUATOR_LINK_SOURCES) $(FQ4_NEUTRAL_EVALUATOR_RUNNER_SOURCE)
FQ4_NEUTRAL_CANDIDATE_PUBLISHER_LINK_SOURCES := $(FQ4_NEUTRAL_EVALUATOR_LINK_SOURCES) $(FQ4_NEUTRAL_CANDIDATE_PUBLISHER_SOURCE)
ACTION_Q_NESTED_ACTOR_ANCHOR_LINK_SOURCES := $(ACTION_Q_NESTED_ACTOR_EARLY_STOP_LINK_SOURCES) $(filter-out $(ACTION_Q_NESTED_ACTOR_EARLY_STOP_LINK_SOURCES),$(FQ4_NEUTRAL_EVALUATOR_LINK_SOURCES)) $(ACTION_Q_NESTED_ACTOR_ANCHOR_SOURCE)
ACTION_Q_NESTED_ACTOR_BROAD_DISTILL_LINK_SOURCES := $(ACTION_Q_NESTED_ACTOR_DISTILL_LINK_SOURCES) $(ACTION_Q_NESTED_ACTOR_BROAD_DISTILL_SOURCE)
ACTION_Q_ON_POLICY_SUCCESSOR_LINK_SOURCES := $(ACTION_Q_NESTED_ACTOR_BROAD_DISTILL_LINK_SOURCES) $(ACTION_Q_ON_POLICY_SUCCESSOR_SOURCE)
ACTION_Q_PRIORITY_TRUST_REGION_LINK_SOURCES := $(ACTION_Q_ON_POLICY_SUCCESSOR_LINK_SOURCES) $(ACTION_Q_PRIORITY_TRUST_REGION_SOURCE)
ACTION_Q_RECURSIVE_POLICY_IMPROVEMENT_LINK_SOURCES := $(ACTION_Q_OFFLINE_GATE_LINK_SOURCES) $(ACTION_Q_RECURSIVE_POLICY_IMPROVEMENT_SOURCE) $(CONSERVATIVE_POLICY_IMPROVEMENT_SOURCE)
INFORMATION_SET_PUCT_PREFLIGHT_LINK_SOURCES := $(ACTION_Q_RECURSIVE_POLICY_IMPROVEMENT_LINK_SOURCES) $(INFORMATION_SET_PUCT_SOURCE) $(INFORMATION_SET_PUCT_PREFLIGHT_SOURCE)
INFORMATION_SET_PUCT_BUDGET_DIAGNOSTIC_LINK_SOURCES := $(INFORMATION_SET_PUCT_PREFLIGHT_LINK_SOURCES) $(INFORMATION_SET_PUCT_BUDGET_DIAGNOSTIC_SOURCE)
DECISION_BOUNDARY_CRITIC_LINK_SOURCES := $(ACTION_Q_NESTED_ACTOR_BROAD_DISTILL_LINK_SOURCES) $(filter-out $(ACTION_Q_NESTED_ACTOR_BROAD_DISTILL_LINK_SOURCES),$(INFORMATION_SET_PUCT_BUDGET_DIAGNOSTIC_LINK_SOURCES)) $(DECISION_BOUNDARY_CRITIC_SOURCE) $(DECISION_BOUNDARY_CRITIC_GATE_SOURCE)
DECISION_BOUNDARY_RANK_DIRECT_LINK_SOURCES := $(DECISION_BOUNDARY_CRITIC_LINK_SOURCES) $(DECISION_BOUNDARY_RANK_DIRECT_SOURCE)
DECISION_BOUNDARY_RANK_HIDDEN_LINK_SOURCES := $(DECISION_BOUNDARY_RANK_DIRECT_LINK_SOURCES) $(DECISION_BOUNDARY_RANK_HIDDEN_SOURCE)
DECISION_BOUNDARY_ACTION_PAIR_LINK_SOURCES := $(DECISION_BOUNDARY_RANK_DIRECT_LINK_SOURCES) $(DECISION_BOUNDARY_ACTION_PAIR_SOURCE)
DECISION_BOUNDARY_ADAPTIVE_TRUNK_LINK_SOURCES := $(DECISION_BOUNDARY_ACTION_PAIR_LINK_SOURCES) $(DECISION_BOUNDARY_ADAPTIVE_TRUNK_SOURCE)
DECISION_DENSITY_CENSUS_LINK_SOURCES := $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(DECISION_DENSITY_CENSUS_SOURCE)
DECISION_DENSITY_PRIORITY_LINK_SOURCES := $(DECISION_DENSITY_CENSUS_LINK_SOURCES) $(DECISION_DENSITY_PRIORITY_SOURCE)
DECISION_DENSITY_LABELS_LINK_SOURCES := $(ACTION_Q_NESTED_ACTOR_BROAD_DISTILL_LINK_SOURCES) $(filter-out $(ACTION_Q_NESTED_ACTOR_BROAD_DISTILL_LINK_SOURCES),$(DECISION_DENSITY_PRIORITY_LINK_SOURCES)) $(DECISION_DENSITY_LABELS_SOURCE)
DECISION_DENSITY_BILINEAR_LINK_SOURCES := $(DECISION_DENSITY_LABELS_LINK_SOURCES) $(filter-out $(DECISION_DENSITY_LABELS_LINK_SOURCES),$(DECISION_BOUNDARY_ACTION_PAIR_LINK_SOURCES)) $(DECISION_DENSITY_BILINEAR_SOURCE)
DECISION_DENSITY_BILINEAR_ARTIFACT_LINK_SOURCES := $(DECISION_DENSITY_BILINEAR_LINK_SOURCES) $(LEARNED_PRIORITY_BILINEAR_ARTIFACT_SOURCE)
DECISION_DENSITY_SPARSE_SUPPORT_LINK_SOURCES := $(DECISION_DENSITY_BILINEAR_LINK_SOURCES) $(DECISION_DENSITY_SPARSE_SUPPORT_SOURCE)
DECISION_DENSITY_SPARSE_CROSS_LINK_SOURCES := $(DECISION_DENSITY_SPARSE_SUPPORT_LINK_SOURCES) $(DECISION_DENSITY_SPARSE_CROSS_SOURCE)
FQ4_PRIORITY_FIT_LINK_SOURCES := $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(FQ0_INFORMATION_SET_SOURCE) $(FQ0_DOMINANCE_SOURCE) $(FQ0_DOMINANCE_TRANSITION_SOURCE) $(OC1_ACTION_SCORING_SOURCE) $(FQ4_PRIORITY_MATH_SOURCE) $(FQ4_PRIORITY_FIT_SOURCE)
FQ4_D1_FIELD_GATE_LINK_SOURCES := $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(PROBE_SOURCE) $(PROBE_EVAL_SOURCE) $(PROBE_RUNNER_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(FQ0_INFORMATION_SET_SOURCE) $(FQ0_DOMINANCE_SOURCE) $(FQ0_DOMINANCE_TRANSITION_SOURCE) $(OC1_ACTION_SCORING_SOURCE) $(FQ4_PARENT_CLASSIFICATION_SOURCE) $(FQ4_PRIORITY_COLLECTION_SOURCE) $(FQ4_D1_FIELD_GATE_SOURCE)
FQ4_D1_TREATMENT_LINK_SOURCES := $(FQ4_D1_FIELD_GATE_LINK_SOURCES) $(FQ4_PRIORITY_MATH_SOURCE) $(FQ4_PRIORITY_FIT_SOURCE) $(FQ4_D1_TREATMENT_SOURCE) $(FQ4_D1_TREATMENT_PRODUCTION_SOURCE)

$(eval $(call link_program,$(AC1_TEACHER_AUDIT_TEST_RUNNER),$(AC1_TEACHER_AUDIT_LINK_SOURCES) tests/test_ac1_teacher_audit.cpp))

$(eval $(call link_program,$(AC1_TEACHER_AUDIT),$(AC1_TEACHER_AUDIT_LINK_SOURCES) src/ac1_teacher_audit_main.cpp))

$(eval $(call link_program,$(FQ0_INFORMATION_SET_TEST_RUNNER),$(FQ0_INFORMATION_SET_LINK_SOURCES) tests/test_fq0_information_set.cpp))

$(eval $(call link_program,$(FQ0_BELLMAN_TEST_RUNNER),$(FQ0_BELLMAN_SOURCE) tests/test_fq0_bellman.cpp))

$(eval $(call link_program,$(FQ0_DOMINANCE_TEST_RUNNER),$(FQ0_DOMINANCE_LINK_SOURCES) tests/test_fq0_dominance.cpp))

$(eval $(call link_program,$(FQ0_DOMINANCE_TRANSITION_TEST_RUNNER),$(FQ0_DOMINANCE_LINK_SOURCES) tests/test_fq0_dominance_transition.cpp))

$(eval $(call link_program,$(FQ0_BELLMAN_SCIENCE_TEST_RUNNER),$(FQ0_BELLMAN_SCIENCE_LINK_SOURCES) tests/test_fq0_bellman_science.cpp))

$(eval $(call link_program,$(FQ0_BELLMAN_AUDIT_TEST_RUNNER),$(FQ0_BELLMAN_AUDIT_LINK_SOURCES) tests/test_fq0_bellman_audit.cpp))

$(eval $(call link_program,$(FQ0_BELLMAN_RUN_TEST_RUNNER),$(FQ0_BELLMAN_RUN_LINK_SOURCES) tests/test_fq0_bellman_run.cpp))

$(eval $(call link_program,$(FQ0_BELLMAN_AUDIT),$(FQ0_BELLMAN_RUN_LINK_SOURCES) src/fq0_bellman_audit_main.cpp))

$(eval $(call link_program,$(FQ0_SEQUENCE_PROJECTION_TEST_RUNNER),$(FQ0_SEQUENCE_PROJECTION_LINK_SOURCES) tests/test_fq0_sequence_projection.cpp))

$(eval $(call link_program,$(FQ0_CAUSAL_QUOTIENT_TEST_RUNNER),$(FQ0_CAUSAL_QUOTIENT_LINK_SOURCES) tests/test_fq0_causal_quotient.cpp))

$(eval $(call link_program,$(FQ0_CAUSAL_QUOTIENT),$(FQ0_CAUSAL_QUOTIENT_LINK_SOURCES) src/fq0_causal_quotient_main.cpp))

$(eval $(call link_program,$(FQ4_PRIORITY_MATH_TEST_RUNNER),$(FQ4_PRIORITY_MATH_SOURCE) tests/test_fq4_priority_math.cpp))

$(eval $(call link_program,$(FQ4_PRIORITY_COLLECTION_TEST_RUNNER),$(FQ4_PRIORITY_COLLECTION_LINK_SOURCES) tests/test_fq4_priority_collection.cpp))

$(eval $(call link_program,$(FQ4_WORK0_CACHE_TEST_RUNNER),$(FQ4_WORK0_CACHE_LINK_SOURCES) tests/test_fq4_work0_cache.cpp))

$(eval $(call link_program,$(FQ4_DEV_BUNDLE_TEST_RUNNER),$(FQ4_DEV_BUNDLE_LINK_SOURCES) tests/test_fq4_dev_bundle.cpp))

$(eval $(call link_program,$(FQ4_DEV_GENERATOR_TEST_RUNNER),$(FQ4_DEV_GENERATOR_LINK_SOURCES) tests/test_fq4_dev_generator.cpp))

$(eval $(call link_program,$(FQ4_DEV_EVALUATOR_TEST_RUNNER),$(FQ4_DEV_EVALUATOR_LINK_SOURCES) tests/test_fq4_dev_evaluator.cpp))

$(eval $(call link_program,$(FQ4_DEV_EVALUATOR),$(FQ4_DEV_EVALUATOR_LINK_SOURCES) src/fq4_dev_evaluator_main.cpp))

$(eval $(call link_program,$(FQ4_DEV_CANDIDATE_ARTIFACT_TEST_RUNNER),$(FQ4_DEV_CANDIDATE_ARTIFACT_LINK_SOURCES) tests/test_fq4_dev_candidate_artifact.cpp))

$(eval $(call link_program,$(FQ4_DEV_CANDIDATE_PUBLISHER_TEST_RUNNER),$(FQ4_DEV_CANDIDATE_PUBLISHER_LINK_SOURCES) tests/test_fq4_dev_candidate_publisher.cpp))

$(eval $(call link_program,$(FQ4_DEV_CANDIDATE_PUBLISHER),$(FQ4_DEV_CANDIDATE_PUBLISHER_LINK_SOURCES) src/fq4_dev_candidate_publisher_main.cpp))

$(eval $(call link_program,$(FQ4_DEV1_GAMEPLAY_TEST_RUNNER),$(FQ4_DEV1_GAMEPLAY_LINK_SOURCES) tests/test_fq4_dev1_gameplay.cpp))

$(eval $(call link_program,$(FQ4_DEV1_GAMEPLAY),$(FQ4_DEV1_GAMEPLAY_LINK_SOURCES) src/fq4_dev1_gameplay_main.cpp))

$(eval $(call link_program,$(FQ4_DEV5_GAMEPLAY_TEST_RUNNER),$(FQ4_DEV5_GAMEPLAY_LINK_SOURCES) tests/test_fq4_dev5_gameplay.cpp))

$(eval $(call link_program,$(FQ4_DEV5_GAMEPLAY),$(FQ4_DEV5_GAMEPLAY_LINK_SOURCES) src/fq4_dev5_gameplay_main.cpp))

$(eval $(call link_program,$(FQ4_BLEND_EXPLORE_TEST_RUNNER),$(FQ4_BLEND_EXPLORE_LINK_SOURCES) tests/test_fq4_blend_explore.cpp))

$(eval $(call link_program,$(FQ4_BLEND_EXPLORE),$(FQ4_BLEND_EXPLORE_LINK_SOURCES) src/fq4_blend_explore_main.cpp))

$(eval $(call link_program,$(ACTION_Q_EXPLORE_TEST_RUNNER),$(ACTION_Q_EXPLORE_LINK_SOURCES) tests/test_action_q_explore.cpp))

$(eval $(call link_program,$(ACTION_Q_FIELD_GATE_TEST_RUNNER),$(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(ACTION_Q_FIELD_GATE_SOURCE) tests/test_action_q_field_gate.cpp))

$(eval $(call link_program,$(ACTION_Q_OFFLINE_GATE_TEST_RUNNER),$(ACTION_Q_OFFLINE_GATE_LINK_SOURCES) tests/test_action_q_offline_gate.cpp))

$(eval $(call link_program,$(ACTION_Q_EXPLORE),$(ACTION_Q_OFFLINE_GATE_LINK_SOURCES) src/action_q_explore_main.cpp))

$(eval $(call link_program,$(ACTION_Q_BELLMAN_TEACHER_TEST_RUNNER),$(ACTION_Q_BELLMAN_TEACHER_LINK_SOURCES) tests/test_action_q_bellman_teacher.cpp))

$(eval $(call link_program,$(ACTION_Q_BELLMAN_EXPLORE_TEST_RUNNER),$(ACTION_Q_BELLMAN_EXPLORE_LINK_SOURCES) tests/test_action_q_bellman_explore.cpp))

$(eval $(call link_program,$(ACTION_Q_BELLMAN_EXPLORE),$(ACTION_Q_BELLMAN_EXPLORE_LINK_SOURCES) src/action_q_bellman_explore_main.cpp))

$(eval $(call link_program,$(ACTION_Q_MULTISCALE_TEACHER_TEST_RUNNER),$(ACTION_Q_MULTISCALE_TEACHER_LINK_SOURCES) tests/test_action_q_multiscale_teacher.cpp))

$(eval $(call link_program,$(ACTION_Q_MULTISCALE_EXPLORE_TEST_RUNNER),$(ACTION_Q_MULTISCALE_EXPLORE_LINK_SOURCES) tests/test_action_q_multiscale_explore.cpp))

$(eval $(call link_program,$(ACTION_Q_MULTISCALE_EXPLORE),$(ACTION_Q_MULTISCALE_EXPLORE_LINK_SOURCES) src/action_q_multiscale_explore_main.cpp))

$(eval $(call link_program,$(ACTION_Q_LONG_HORIZON_DIAGNOSTIC_TEST_RUNNER),$(ACTION_Q_LONG_HORIZON_DIAGNOSTIC_LINK_SOURCES) tests/test_action_q_long_horizon_diagnostic.cpp))

$(eval $(call link_program,$(ACTION_Q_LONG_HORIZON_DIAGNOSTIC),$(ACTION_Q_LONG_HORIZON_DIAGNOSTIC_LINK_SOURCES) src/action_q_long_horizon_diagnostic_main.cpp))

$(eval $(call link_program,$(ACTION_Q_NESTED_ACTOR_DIAGNOSTIC_TEST_RUNNER),$(ACTION_Q_NESTED_ACTOR_DIAGNOSTIC_LINK_SOURCES) tests/test_action_q_nested_actor_diagnostic.cpp))

$(eval $(call link_program,$(ACTION_Q_NESTED_ACTOR_DIAGNOSTIC),$(ACTION_Q_NESTED_ACTOR_DIAGNOSTIC_LINK_SOURCES) src/action_q_nested_actor_diagnostic_main.cpp))

$(eval $(call link_program,$(ACTION_Q_NESTED_ACTOR_DISTILL_TEST_RUNNER),$(ACTION_Q_NESTED_ACTOR_DISTILL_LINK_SOURCES) tests/test_action_q_nested_actor_distill.cpp))

$(eval $(call link_program,$(ACTION_Q_NESTED_ACTOR_DISTILL),$(ACTION_Q_NESTED_ACTOR_DISTILL_LINK_SOURCES) src/action_q_nested_actor_distill_main.cpp))

$(eval $(call link_program,$(ACTION_Q_NESTED_ACTOR_EARLY_STOP_TEST_RUNNER),$(ACTION_Q_NESTED_ACTOR_EARLY_STOP_LINK_SOURCES) tests/test_action_q_nested_actor_early_stop.cpp))

$(eval $(call link_program,$(ACTION_Q_NESTED_ACTOR_EARLY_STOP),$(ACTION_Q_NESTED_ACTOR_EARLY_STOP_LINK_SOURCES) src/action_q_nested_actor_early_stop_main.cpp))

$(eval $(call link_program,$(ACTION_Q_NESTED_ACTOR_ANCHOR_TEST_RUNNER),$(ACTION_Q_NESTED_ACTOR_ANCHOR_LINK_SOURCES) tests/test_action_q_nested_actor_anchor.cpp))

$(eval $(call link_program,$(ACTION_Q_NESTED_ACTOR_ANCHOR),$(ACTION_Q_NESTED_ACTOR_ANCHOR_LINK_SOURCES) src/action_q_nested_actor_anchor_main.cpp))

$(eval $(call link_program,$(ACTION_Q_NESTED_ACTOR_BROAD_DISTILL_TEST_RUNNER),$(ACTION_Q_NESTED_ACTOR_BROAD_DISTILL_LINK_SOURCES) tests/test_action_q_nested_actor_broad_distill.cpp))

$(eval $(call link_program,$(ACTION_Q_NESTED_ACTOR_BROAD_DISTILL),$(ACTION_Q_NESTED_ACTOR_BROAD_DISTILL_LINK_SOURCES) src/action_q_nested_actor_broad_distill_main.cpp))

$(eval $(call link_program,$(DECISION_BOUNDARY_CRITIC_TEST_RUNNER),$(DECISION_BOUNDARY_CRITIC_LINK_SOURCES) tests/test_decision_boundary_critic.cpp))

$(eval $(call link_program,$(DECISION_BOUNDARY_CRITIC),$(DECISION_BOUNDARY_CRITIC_LINK_SOURCES) src/decision_boundary_critic_main.cpp))

$(eval $(call link_program,$(DECISION_BOUNDARY_RANK_DIRECT_TEST_RUNNER),$(DECISION_BOUNDARY_RANK_DIRECT_LINK_SOURCES) tests/test_decision_boundary_rank_direct.cpp))

$(eval $(call link_program,$(DECISION_BOUNDARY_RANK_DIRECT),$(DECISION_BOUNDARY_RANK_DIRECT_LINK_SOURCES) src/decision_boundary_rank_direct_main.cpp))

$(eval $(call link_program,$(DECISION_BOUNDARY_RANK_HIDDEN_TEST_RUNNER),$(DECISION_BOUNDARY_RANK_HIDDEN_LINK_SOURCES) tests/test_decision_boundary_rank_hidden.cpp))

$(eval $(call link_program,$(DECISION_BOUNDARY_RANK_HIDDEN),$(DECISION_BOUNDARY_RANK_HIDDEN_LINK_SOURCES) src/decision_boundary_rank_hidden_main.cpp))

$(eval $(call link_program,$(DECISION_BOUNDARY_ACTION_PAIR_TEST_RUNNER),$(DECISION_BOUNDARY_ACTION_PAIR_LINK_SOURCES) tests/test_decision_boundary_action_pair.cpp))

$(eval $(call link_program,$(DECISION_BOUNDARY_ACTION_PAIR),$(DECISION_BOUNDARY_ACTION_PAIR_LINK_SOURCES) src/decision_boundary_action_pair_main.cpp))

$(eval $(call link_program,$(DECISION_BOUNDARY_ADAPTIVE_TRUNK_TEST_RUNNER),$(DECISION_BOUNDARY_ADAPTIVE_TRUNK_LINK_SOURCES) tests/test_decision_boundary_adaptive_trunk.cpp))

$(eval $(call link_program,$(DECISION_BOUNDARY_ADAPTIVE_TRUNK),$(DECISION_BOUNDARY_ADAPTIVE_TRUNK_LINK_SOURCES) src/decision_boundary_adaptive_trunk_main.cpp))

$(eval $(call link_program,$(DECISION_DENSITY_CENSUS_TEST_RUNNER),$(DECISION_DENSITY_CENSUS_LINK_SOURCES) tests/test_decision_density_census.cpp))

$(eval $(call link_program,$(DECISION_DENSITY_CENSUS),$(DECISION_DENSITY_CENSUS_LINK_SOURCES) src/decision_density_census_main.cpp))

$(eval $(call link_program,$(DECISION_DENSITY_PRIORITY_TEST_RUNNER),$(DECISION_DENSITY_PRIORITY_LINK_SOURCES) tests/test_decision_density_priority.cpp))

$(eval $(call link_program,$(DECISION_DENSITY_PRIORITY),$(DECISION_DENSITY_PRIORITY_LINK_SOURCES) src/decision_density_priority_main.cpp))

$(eval $(call link_program,$(DECISION_DENSITY_LABELS_TEST_RUNNER),$(DECISION_DENSITY_LABELS_LINK_SOURCES) tests/test_decision_density_labels.cpp))

$(eval $(call link_program,$(DECISION_DENSITY_LABELS),$(DECISION_DENSITY_LABELS_LINK_SOURCES) src/decision_density_labels_main.cpp))

$(eval $(call link_program,$(LEARNED_PRIORITY_BILINEAR_TEST_RUNNER),$(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) tests/test_learned_priority_bilinear.cpp))

$(eval $(call link_program,$(LEARNED_PRIORITY_BILINEAR_ARTIFACT_TEST_RUNNER),$(LEARNED_PRIORITY_BILINEAR_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(LEARNED_PRIORITY_BILINEAR_ARTIFACT_SOURCE) tests/test_learned_priority_bilinear_artifact.cpp))

$(eval $(call link_program,$(LEARNED_PRIORITY_SPARSE_CROSS_TEST_RUNNER),$(LEARNED_PRIORITY_SPARSE_CROSS_SOURCE) tests/test_learned_priority_sparse_cross.cpp))

$(eval $(call link_program,$(DECISION_DENSITY_BILINEAR_TEST_RUNNER),$(DECISION_DENSITY_BILINEAR_LINK_SOURCES) tests/test_decision_density_bilinear.cpp))

$(eval $(call link_program,$(DECISION_DENSITY_BILINEAR),$(DECISION_DENSITY_BILINEAR_LINK_SOURCES) src/decision_density_bilinear_main.cpp))

$(eval $(call link_program,$(DECISION_DENSITY_BILINEAR_ARTIFACT),$(DECISION_DENSITY_BILINEAR_ARTIFACT_LINK_SOURCES) src/decision_density_bilinear_artifact_main.cpp))

$(eval $(call link_program,$(DECISION_DENSITY_SPARSE_SUPPORT_TEST_RUNNER),$(DECISION_DENSITY_SPARSE_SUPPORT_LINK_SOURCES) tests/test_decision_density_sparse_support.cpp))

$(eval $(call link_program,$(DECISION_DENSITY_SPARSE_SUPPORT),$(DECISION_DENSITY_SPARSE_SUPPORT_LINK_SOURCES) src/decision_density_sparse_support_main.cpp))

$(eval $(call link_program,$(DECISION_DENSITY_SPARSE_CROSS_TEST_RUNNER),$(DECISION_DENSITY_SPARSE_CROSS_LINK_SOURCES) tests/test_decision_density_sparse_cross.cpp))

$(eval $(call link_program,$(DECISION_DENSITY_SPARSE_CROSS),$(DECISION_DENSITY_SPARSE_CROSS_LINK_SOURCES) src/decision_density_sparse_cross_main.cpp))

$(eval $(call link_program,$(ACTION_Q_ON_POLICY_SUCCESSOR_TEST_RUNNER),$(ACTION_Q_ON_POLICY_SUCCESSOR_LINK_SOURCES) tests/test_action_q_on_policy_successor.cpp))

$(eval $(call link_program,$(ACTION_Q_ON_POLICY_SUCCESSOR),$(ACTION_Q_ON_POLICY_SUCCESSOR_LINK_SOURCES) src/action_q_on_policy_successor_main.cpp))

$(eval $(call link_program,$(ACTION_Q_PRIORITY_TRUST_REGION_TEST_RUNNER),$(ACTION_Q_PRIORITY_TRUST_REGION_LINK_SOURCES) tests/test_action_q_priority_trust_region.cpp))

$(eval $(call link_program,$(ACTION_Q_PRIORITY_TRUST_REGION),$(ACTION_Q_PRIORITY_TRUST_REGION_LINK_SOURCES) $(FQ4_DEV_CANDIDATE_ARTIFACT_SOURCE) src/action_q_priority_trust_region_main.cpp))

$(eval $(call link_program,$(ACTION_Q_RECURSIVE_POLICY_IMPROVEMENT_TEST_RUNNER),$(ACTION_Q_RECURSIVE_POLICY_IMPROVEMENT_LINK_SOURCES) tests/test_action_q_recursive_policy_improvement.cpp))

$(eval $(call link_program,$(ACTION_Q_RECURSIVE_POLICY_IMPROVEMENT),$(ACTION_Q_RECURSIVE_POLICY_IMPROVEMENT_LINK_SOURCES) src/action_q_recursive_policy_improvement_main.cpp))

$(eval $(call link_program,$(CONSERVATIVE_POLICY_IMPROVEMENT_TEST_RUNNER),$(CONSERVATIVE_POLICY_IMPROVEMENT_SOURCE) tests/test_conservative_policy_improvement.cpp))

$(eval $(call link_program,$(EXACT_COMBAT_SUBGAME_TEST_RUNNER),$(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) tests/test_exact_combat_subgame.cpp))

$(eval $(call link_program,$(INFORMATION_SET_PUCT_TEST_RUNNER),$(INFORMATION_SET_PUCT_SOURCE) tests/test_information_set_puct.cpp))

$(eval $(call link_program,$(LEARNED_GENERATIVE_SEARCH_TEST_RUNNER),$(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) tests/test_learned_generative_search.cpp))

$(eval $(call link_program,$(INFORMATION_SET_PUCT_PREFLIGHT_TEST_RUNNER),$(INFORMATION_SET_PUCT_PREFLIGHT_LINK_SOURCES) tests/test_information_set_puct_preflight.cpp))

$(eval $(call link_program,$(INFORMATION_SET_PUCT_PREFLIGHT),$(INFORMATION_SET_PUCT_PREFLIGHT_LINK_SOURCES) src/information_set_puct_preflight_main.cpp))

$(eval $(call link_program,$(INFORMATION_SET_PUCT_BUDGET_DIAGNOSTIC_TEST_RUNNER),$(INFORMATION_SET_PUCT_BUDGET_DIAGNOSTIC_LINK_SOURCES) tests/test_information_set_puct_budget_diagnostic.cpp))

$(eval $(call link_program,$(INFORMATION_SET_PUCT_BUDGET_DIAGNOSTIC),$(INFORMATION_SET_PUCT_BUDGET_DIAGNOSTIC_LINK_SOURCES) src/information_set_puct_budget_diagnostic_main.cpp))

$(eval $(call link_program,$(FQ4_DEV_BACKGROUND_DIAGNOSTIC_TEST_RUNNER),$(FQ4_DEV_BACKGROUND_DIAGNOSTIC_LINK_SOURCES) tests/test_fq4_dev_background_diagnostic.cpp))

$(eval $(call link_program,$(FQ4_DEV_BACKGROUND_DIAGNOSTIC),$(FQ4_DEV_BACKGROUND_DIAGNOSTIC_LINK_SOURCES) src/fq4_dev_background_diagnostic_main.cpp))

$(eval $(call link_program,$(FQ4_DEV_COVERAGE_CENSUS_TEST_RUNNER),$(FQ4_DEV_COVERAGE_CENSUS_LINK_SOURCES) tests/test_fq4_dev_coverage_census.cpp))

$(eval $(call link_program,$(FQ4_DEV_COVERAGE_CENSUS),$(FQ4_DEV_COVERAGE_CENSUS_LINK_SOURCES) src/fq4_dev_coverage_census_main.cpp))

$(eval $(call link_program,$(FQ4_NEUTRAL_SUPPLEMENT_TEST_RUNNER),$(FQ4_NEUTRAL_SUPPLEMENT_LINK_SOURCES) tests/test_fq4_neutral_supplement.cpp))

$(eval $(call link_program,$(FQ4_NEUTRAL_PUBLISHER_TEST_RUNNER),$(FQ4_NEUTRAL_PUBLISHER_LINK_SOURCES) tests/test_fq4_neutral_publisher.cpp))

$(eval $(call link_program,$(FQ4_NEUTRAL_EVALUATOR_TEST_RUNNER),$(FQ4_NEUTRAL_EVALUATOR_LINK_SOURCES) tests/test_fq4_neutral_evaluator.cpp))

$(eval $(call link_program,$(FQ4_NEUTRAL_EVALUATOR_RUNNER_TEST_RUNNER),$(FQ4_NEUTRAL_EVALUATOR_RUNNER_LINK_SOURCES) tests/test_fq4_neutral_evaluator_runner.cpp))

$(eval $(call link_program,$(FQ4_NEUTRAL_EVALUATOR),$(FQ4_NEUTRAL_EVALUATOR_RUNNER_LINK_SOURCES) src/fq4_neutral_evaluator_main.cpp))

$(eval $(call link_program,$(FQ4_NEUTRAL_CANDIDATE_PUBLISHER_TEST_RUNNER),$(FQ4_NEUTRAL_CANDIDATE_PUBLISHER_LINK_SOURCES) tests/test_fq4_neutral_candidate_publisher.cpp))

$(FQ4_DEV_GENERATOR_MAIN_OBJECT): src/fq4_dev_generator_main.cpp
	@mkdir -p "$(@D)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) \
		-DOLD_SCHOOL_FQ4_PRODUCER_COMMIT=\"$(FQ4_DEV_PRODUCER_COMMIT)\" \
		-MMD -MP -MF "$(FQ4_DEV_GENERATOR_MAIN_DEPFILE)" \
		-MT "$@" -c "$<" -o "$@"

FQ4_DEV_GENERATOR_LINK_OBJECTS := $(call source_objects,$(FQ4_DEV_GENERATOR_LINK_SOURCES)) $(FQ4_DEV_GENERATOR_MAIN_OBJECT)

$(FQ4_DEV_GENERATOR): $(FQ4_DEV_GENERATOR_LINK_OBJECTS) $(call program_config_relink,$(FQ4_DEV_GENERATOR),$(FQ4_DEV_GENERATOR_LINK_OBJECTS)) | $(BUILD_DIR)
	@rm -f -- "$@.compile-config.mk"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) \
		$(call source_objects,$(FQ4_DEV_GENERATOR_LINK_SOURCES)) \
		$(FQ4_DEV_GENERATOR_MAIN_OBJECT) -o "$@"
	@temporary="$@.compile-config.mk.tmp.$$$$$$$$"; \
	printf '%s := %s\n%s := %s\n' \
		"$(call program_config_variable,$(FQ4_DEV_GENERATOR))" \
		"$(BUILD_CONFIG_ID)" \
		"$(call program_link_objects_variable,$(FQ4_DEV_GENERATOR))" \
		"$(FQ4_DEV_GENERATOR_LINK_OBJECTS)" >"$$temporary" && \
	mv -f -- "$$temporary" "$@.compile-config.mk"

$(FQ4_NEUTRAL_PUBLISHER_MAIN_OBJECT): src/fq4_neutral_publisher_main.cpp
	@mkdir -p "$(@D)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) \
		-DOLD_SCHOOL_FQ4_DEV5_PRODUCER_COMMIT=\"$(FQ4_DEV_PRODUCER_COMMIT)\" \
		-MMD -MP -MF "$(FQ4_NEUTRAL_PUBLISHER_MAIN_DEPFILE)" \
		-MT "$@" -c "$<" -o "$@"

FQ4_NEUTRAL_PUBLISHER_LINK_OBJECTS := $(call source_objects,$(FQ4_NEUTRAL_PUBLISHER_LINK_SOURCES)) $(FQ4_NEUTRAL_PUBLISHER_MAIN_OBJECT)

$(FQ4_NEUTRAL_PUBLISHER): $(FQ4_NEUTRAL_PUBLISHER_LINK_OBJECTS) $(call program_config_relink,$(FQ4_NEUTRAL_PUBLISHER),$(FQ4_NEUTRAL_PUBLISHER_LINK_OBJECTS)) | $(BUILD_DIR)
	@rm -f -- "$@.compile-config.mk"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) \
		$(call source_objects,$(FQ4_NEUTRAL_PUBLISHER_LINK_SOURCES)) \
		$(FQ4_NEUTRAL_PUBLISHER_MAIN_OBJECT) -o "$@"
	@temporary="$@.compile-config.mk.tmp.$$$$$$$$"; \
	printf '%s := %s\n%s := %s\n' \
		"$(call program_config_variable,$(FQ4_NEUTRAL_PUBLISHER))" \
		"$(BUILD_CONFIG_ID)" \
		"$(call program_link_objects_variable,$(FQ4_NEUTRAL_PUBLISHER))" \
	"$(FQ4_NEUTRAL_PUBLISHER_LINK_OBJECTS)" >"$$temporary" && \
	mv -f -- "$$temporary" "$@.compile-config.mk"

$(FQ4_NEUTRAL_CANDIDATE_PUBLISHER_MAIN_OBJECT): src/fq4_neutral_candidate_publisher_main.cpp
	@mkdir -p "$(@D)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) \
		-DOLD_SCHOOL_FQ4_DEV5_PRODUCER_COMMIT=\"$(FQ4_DEV_PRODUCER_COMMIT)\" \
		-MMD -MP -MF "$(FQ4_NEUTRAL_CANDIDATE_PUBLISHER_MAIN_DEPFILE)" \
		-MT "$@" -c "$<" -o "$@"

FQ4_NEUTRAL_CANDIDATE_PUBLISHER_LINK_OBJECTS := $(call source_objects,$(FQ4_NEUTRAL_CANDIDATE_PUBLISHER_LINK_SOURCES)) $(FQ4_NEUTRAL_CANDIDATE_PUBLISHER_MAIN_OBJECT)

$(FQ4_NEUTRAL_CANDIDATE_PUBLISHER): $(FQ4_NEUTRAL_CANDIDATE_PUBLISHER_LINK_OBJECTS) $(call program_config_relink,$(FQ4_NEUTRAL_CANDIDATE_PUBLISHER),$(FQ4_NEUTRAL_CANDIDATE_PUBLISHER_LINK_OBJECTS)) | $(BUILD_DIR)
	@rm -f -- "$@.compile-config.mk"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) \
		$(call source_objects,$(FQ4_NEUTRAL_CANDIDATE_PUBLISHER_LINK_SOURCES)) \
		$(FQ4_NEUTRAL_CANDIDATE_PUBLISHER_MAIN_OBJECT) -o "$@"
	@temporary="$@.compile-config.mk.tmp.$$$$$$$$"; \
	printf '%s := %s\n%s := %s\n' \
		"$(call program_config_variable,$(FQ4_NEUTRAL_CANDIDATE_PUBLISHER))" \
		"$(BUILD_CONFIG_ID)" \
		"$(call program_link_objects_variable,$(FQ4_NEUTRAL_CANDIDATE_PUBLISHER))" \
		"$(FQ4_NEUTRAL_CANDIDATE_PUBLISHER_LINK_OBJECTS)" >"$$temporary" && \
	mv -f -- "$$temporary" "$@.compile-config.mk"

$(eval $(call link_program,$(FQ4_PRIORITY_FIT_TEST_RUNNER),$(FQ4_PRIORITY_FIT_LINK_SOURCES) tests/test_fq4_priority_fit.cpp))

$(eval $(call link_program,$(FQ4_PRIORITY_FIT),$(FQ4_PRIORITY_FIT_LINK_SOURCES) src/fq4_priority_fit_main.cpp))

$(eval $(call link_program,$(FQ4_PRIORITY_FIT_D0B),$(FQ4_PRIORITY_FIT_LINK_SOURCES) src/fq4_priority_fit_d0b_main.cpp))

$(eval $(call link_program,$(FQ4_D1_FIELD_GATE_TEST_RUNNER),$(FQ4_D1_FIELD_GATE_LINK_SOURCES) tests/test_fq4_d1_field_gate.cpp))

$(eval $(call link_program,$(FQ4_D1_CENSUS),$(FQ4_D1_FIELD_GATE_LINK_SOURCES) src/fq4_d1_census_main.cpp))

$(eval $(call link_program,$(FQ4_D1_TREATMENT_TEST_RUNNER),$(FQ4_D1_TREATMENT_LINK_SOURCES) tests/test_fq4_d1_treatment.cpp))

$(eval $(call link_program,$(FQ4_D1_TREATMENT),$(FQ4_D1_TREATMENT_LINK_SOURCES) src/fq4_d1_treatment_main.cpp))

$(eval $(call link_program,$(FQ4_DEV_SCHEDULE_TEST_RUNNER),$(LEARNED_ITERATION_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(FQ4_DEV_SCHEDULE_SOURCE) tests/test_fq4_dev_schedule.cpp))

$(eval $(call link_program,$(FQ4_DEV_SCHEDULE),$(LEARNED_ITERATION_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(FQ4_DEV_SCHEDULE_SOURCE) src/fq4_dev_schedule_main.cpp))

WEB_BRIDGE_CORE_SOURCES := $(ENGINE_SOURCE) $(LEARNED_ITERATION_SOURCE) $(ARTIFACT_INTEGRITY_SOURCE) $(LEARNED_PRIORITY_BILINEAR_ARTIFACT_SOURCE) $(WEB_BRIDGE_SOURCE)
WEB_BRIDGE_LINK_SOURCES := $(WEB_BRIDGE_CORE_SOURCES) src/web_bridge_main.cpp

$(eval $(call link_program,$(WEB_BRIDGE),$(WEB_BRIDGE_LINK_SOURCES)))

$(eval $(call link_program,$(WEB_BRIDGE_TEST_RUNNER),$(WEB_BRIDGE_CORE_SOURCES) tests/test_web_bridge.cpp))

-include $(DEPFILES) $(FQ4_DEV_GENERATOR_MAIN_DEPFILE) $(FQ4_NEUTRAL_PUBLISHER_MAIN_DEPFILE) $(FQ4_NEUTRAL_CANDIDATE_PUBLISHER_MAIN_DEPFILE)

$(WEB_DEPENDENCIES): web/package.json web/package-lock.json
	npm --prefix web ci --ignore-scripts

test: $(FQ4_DEV5_GAMEPLAY_TEST_RUNNER) $(FQ4_DEV5_GAMEPLAY)
test: $(FQ4_BLEND_EXPLORE_TEST_RUNNER) $(FQ4_BLEND_EXPLORE)
test: $(ACTION_Q_EXPLORE_TEST_RUNNER) $(ACTION_Q_FIELD_GATE_TEST_RUNNER) $(ACTION_Q_OFFLINE_GATE_TEST_RUNNER) $(ACTION_Q_EXPLORE)
test: $(ACTION_Q_BELLMAN_TEACHER_TEST_RUNNER) $(ACTION_Q_BELLMAN_EXPLORE_TEST_RUNNER) $(ACTION_Q_BELLMAN_EXPLORE)
test: $(ACTION_Q_MULTISCALE_TEACHER_TEST_RUNNER) $(ACTION_Q_MULTISCALE_EXPLORE_TEST_RUNNER) $(ACTION_Q_MULTISCALE_EXPLORE)
test: $(ACTION_Q_LONG_HORIZON_DIAGNOSTIC_TEST_RUNNER) $(ACTION_Q_LONG_HORIZON_DIAGNOSTIC)
test: $(ACTION_Q_NESTED_ACTOR_DIAGNOSTIC_TEST_RUNNER) $(ACTION_Q_NESTED_ACTOR_DIAGNOSTIC)
test: $(ACTION_Q_NESTED_ACTOR_DISTILL_TEST_RUNNER) $(ACTION_Q_NESTED_ACTOR_DISTILL)
test: $(ACTION_Q_NESTED_ACTOR_EARLY_STOP_TEST_RUNNER) $(ACTION_Q_NESTED_ACTOR_EARLY_STOP)
test: $(ACTION_Q_NESTED_ACTOR_ANCHOR_TEST_RUNNER) $(ACTION_Q_NESTED_ACTOR_ANCHOR)
test: $(ACTION_Q_NESTED_ACTOR_BROAD_DISTILL_TEST_RUNNER) $(ACTION_Q_NESTED_ACTOR_BROAD_DISTILL)
test: $(DECISION_BOUNDARY_CRITIC_TEST_RUNNER) $(DECISION_BOUNDARY_CRITIC)
test: $(DECISION_BOUNDARY_RANK_DIRECT_TEST_RUNNER) $(DECISION_BOUNDARY_RANK_DIRECT)
test: $(DECISION_BOUNDARY_RANK_HIDDEN_TEST_RUNNER) $(DECISION_BOUNDARY_RANK_HIDDEN)
test: $(DECISION_BOUNDARY_ACTION_PAIR_TEST_RUNNER) $(DECISION_BOUNDARY_ACTION_PAIR)
test: $(DECISION_BOUNDARY_ADAPTIVE_TRUNK_TEST_RUNNER) $(DECISION_BOUNDARY_ADAPTIVE_TRUNK)
test: $(DECISION_DENSITY_CENSUS_TEST_RUNNER) $(DECISION_DENSITY_CENSUS)
test: $(DECISION_DENSITY_PRIORITY_TEST_RUNNER) $(DECISION_DENSITY_PRIORITY)
test: $(DECISION_DENSITY_LABELS_TEST_RUNNER) $(DECISION_DENSITY_LABELS)
test: $(LEARNED_PRIORITY_BILINEAR_TEST_RUNNER)
test: $(LEARNED_PRIORITY_BILINEAR_ARTIFACT_TEST_RUNNER) $(DECISION_DENSITY_BILINEAR_TEST_RUNNER) $(DECISION_DENSITY_BILINEAR) $(DECISION_DENSITY_BILINEAR_ARTIFACT)
test: $(DECISION_DENSITY_SPARSE_SUPPORT_TEST_RUNNER) $(DECISION_DENSITY_SPARSE_SUPPORT)
test: $(LEARNED_PRIORITY_SPARSE_CROSS_TEST_RUNNER) $(DECISION_DENSITY_SPARSE_CROSS_TEST_RUNNER) $(DECISION_DENSITY_SPARSE_CROSS)
test: $(ACTION_Q_ON_POLICY_SUCCESSOR_TEST_RUNNER) $(ACTION_Q_ON_POLICY_SUCCESSOR)
test: $(ACTION_Q_PRIORITY_TRUST_REGION_TEST_RUNNER) $(ACTION_Q_PRIORITY_TRUST_REGION)
test: $(ACTION_Q_RECURSIVE_POLICY_IMPROVEMENT_TEST_RUNNER) $(ACTION_Q_RECURSIVE_POLICY_IMPROVEMENT)
test: $(CONSERVATIVE_POLICY_IMPROVEMENT_TEST_RUNNER) $(EXACT_COMBAT_SUBGAME_TEST_RUNNER) $(INFORMATION_SET_PUCT_TEST_RUNNER) $(LEARNED_GENERATIVE_SEARCH_TEST_RUNNER) $(INFORMATION_SET_PUCT_PREFLIGHT_TEST_RUNNER) $(INFORMATION_SET_PUCT_PREFLIGHT) $(INFORMATION_SET_PUCT_BUDGET_DIAGNOSTIC_TEST_RUNNER) $(INFORMATION_SET_PUCT_BUDGET_DIAGNOSTIC)
test: $(FQ4_WORK0_CACHE_TEST_RUNNER)
test: test-fq4-work0-firewall

test: $(TEST_RUNNER) $(LEARNED_ITERATION_TEST_RUNNER) $(PROBE_TEST_RUNNER) $(PROBE_EVAL_TEST_RUNNER) $(PROBE_RUNNER_TEST_RUNNER) $(AUDIT_COMMON_TEST_RUNNER) $(ARTIFACT_INTEGRITY_TEST_RUNNER) $(FQ0_RUSAGE_GUARD_TEST_RUNNER) $(TERMINAL_WEIGHT_EVAL_TEST_RUNNER) $(JOINT_C17_EVAL_TEST_RUNNER) $(JOINT_C17_RUNNER_TEST_RUNNER) $(JOINT_C17_EXECUTION_TEST_RUNNER) $(JOINT_C17_TRAINING_TEST_RUNNER) $(JOINT_C17_ORCHESTRATION_TEST_RUNNER) $(TURN_ALIGNMENT_AUDIT_TEST_RUNNER) $(TARGET_FACTORIAL_AUDIT_TEST_RUNNER) $(REPLAY_WEIGHT_AUDIT_TEST_RUNNER) $(RB0_MECHANICAL_PREFLIGHT_TEST_RUNNER) $(DVR2_HARVEST_TEST_RUNNER) $(DVR2_REPLAY_BUNDLE_TEST_RUNNER) $(DVR2_HARVEST) $(OUTPUT_CALIBRATION_TEST_RUNNER) $(OUTPUT_CALIBRATION_ARTIFACT_TEST_RUNNER) $(OUTPUT_CALIBRATION_RUNNER_TEST_RUNNER) $(OUTPUT_CALIBRATION) $(OC1_ACTION_EVAL_TEST_RUNNER) $(OC1_ACTION_SCORING_TEST_RUNNER) $(OC1_ACTION_REGRESSION_TEST_RUNNER) $(OC1_ACTION_REGRESSION) $(AC1_TEACHER_AUDIT_TEST_RUNNER) $(AC1_TEACHER_AUDIT) $(FQ0_INFORMATION_SET_TEST_RUNNER) $(FQ0_BELLMAN_TEST_RUNNER) $(FQ0_DOMINANCE_TEST_RUNNER) $(FQ0_DOMINANCE_TRANSITION_TEST_RUNNER) $(FQ0_BELLMAN_SCIENCE_TEST_RUNNER) $(FQ0_BELLMAN_AUDIT_TEST_RUNNER) $(FQ0_BELLMAN_RUN_TEST_RUNNER) $(FQ0_BELLMAN_AUDIT) $(FQ0_SEQUENCE_PROJECTION_TEST_RUNNER) $(FQ0_CAUSAL_QUOTIENT_TEST_RUNNER) $(FQ0_CAUSAL_QUOTIENT) $(FQ4_PRIORITY_MATH_TEST_RUNNER) $(FQ4_PRIORITY_COLLECTION_TEST_RUNNER) $(FQ4_DEV_BUNDLE_TEST_RUNNER) $(FQ4_DEV_GENERATOR_TEST_RUNNER) $(FQ4_DEV_GENERATOR) $(FQ4_DEV_EVALUATOR_TEST_RUNNER) $(FQ4_DEV_EVALUATOR) $(FQ4_DEV_CANDIDATE_ARTIFACT_TEST_RUNNER) $(FQ4_DEV_CANDIDATE_PUBLISHER_TEST_RUNNER) $(FQ4_DEV_CANDIDATE_PUBLISHER) $(FQ4_DEV1_GAMEPLAY_TEST_RUNNER) $(FQ4_DEV1_GAMEPLAY) $(FQ4_DEV_BACKGROUND_DIAGNOSTIC_TEST_RUNNER) $(FQ4_DEV_BACKGROUND_DIAGNOSTIC) $(FQ4_DEV_COVERAGE_CENSUS_TEST_RUNNER) $(FQ4_DEV_COVERAGE_CENSUS) $(FQ4_NEUTRAL_CANDIDATE_PUBLISHER_TEST_RUNNER) $(FQ4_NEUTRAL_CANDIDATE_PUBLISHER) $(FQ4_PRIORITY_FIT_TEST_RUNNER) $(FQ4_PRIORITY_FIT) $(FQ4_PRIORITY_FIT_D0B) $(FQ4_D1_FIELD_GATE_TEST_RUNNER) $(FQ4_D1_CENSUS) $(FQ4_D1_TREATMENT_TEST_RUNNER) $(FQ4_D1_TREATMENT) $(FQ4_DEV_SCHEDULE_TEST_RUNNER) $(FQ4_DEV_SCHEDULE) $(WEB_BRIDGE_TEST_RUNNER) $(WEB_BRIDGE) $(WEB_DEPENDENCIES) $(SIMULATOR)
	./$(TEST_RUNNER)
	./$(LEARNED_ITERATION_TEST_RUNNER)
	./$(PROBE_TEST_RUNNER)
	./$(PROBE_EVAL_TEST_RUNNER)
	./$(PROBE_RUNNER_TEST_RUNNER)
	./$(AUDIT_COMMON_TEST_RUNNER)
	./$(ARTIFACT_INTEGRITY_TEST_RUNNER)
	./$(FQ0_RUSAGE_GUARD_TEST_RUNNER)
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
	./$(FQ0_SEQUENCE_PROJECTION_TEST_RUNNER)
	./$(FQ0_CAUSAL_QUOTIENT_TEST_RUNNER)
	./$(FQ4_PRIORITY_MATH_TEST_RUNNER)
	./$(FQ4_PRIORITY_COLLECTION_TEST_RUNNER)
	./$(FQ4_WORK0_CACHE_TEST_RUNNER)
	./$(FQ4_DEV_BUNDLE_TEST_RUNNER)
	./$(FQ4_DEV_GENERATOR_TEST_RUNNER)
	./$(FQ4_DEV_EVALUATOR_TEST_RUNNER)
	./$(FQ4_DEV_CANDIDATE_ARTIFACT_TEST_RUNNER)
	./$(FQ4_DEV_CANDIDATE_PUBLISHER_TEST_RUNNER)
	./$(FQ4_DEV1_GAMEPLAY_TEST_RUNNER)
	./$(FQ4_DEV5_GAMEPLAY_TEST_RUNNER)
	./$(FQ4_BLEND_EXPLORE_TEST_RUNNER)
	./$(ACTION_Q_EXPLORE_TEST_RUNNER)
	./$(ACTION_Q_FIELD_GATE_TEST_RUNNER)
	./$(ACTION_Q_OFFLINE_GATE_TEST_RUNNER)
	./$(ACTION_Q_BELLMAN_TEACHER_TEST_RUNNER)
	./$(ACTION_Q_BELLMAN_EXPLORE_TEST_RUNNER)
	./$(ACTION_Q_MULTISCALE_TEACHER_TEST_RUNNER)
	./$(ACTION_Q_MULTISCALE_EXPLORE_TEST_RUNNER)
	./$(ACTION_Q_LONG_HORIZON_DIAGNOSTIC_TEST_RUNNER)
	@set +e; output=`./$(ACTION_Q_LONG_HORIZON_DIAGNOSTIC) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ3-D0 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-action-q-long-horizon-diagnostic --diagnose' >/dev/null
	./$(ACTION_Q_NESTED_ACTOR_DIAGNOSTIC_TEST_RUNNER)
	@set +e; output=`./$(ACTION_Q_NESTED_ACTOR_DIAGNOSTIC) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ4-D1 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-action-q-nested-actor-diagnostic --diagnose' >/dev/null
	./$(ACTION_Q_NESTED_ACTOR_DISTILL_TEST_RUNNER)
	@set +e; output=`./$(ACTION_Q_NESTED_ACTOR_DISTILL) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ4-G1 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-action-q-nested-actor-distill (--census|--run)' >/dev/null
	./$(ACTION_Q_NESTED_ACTOR_EARLY_STOP_TEST_RUNNER)
	@set +e; output=`./$(ACTION_Q_NESTED_ACTOR_EARLY_STOP) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ4-G2 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-action-q-nested-actor-early-stop --run' >/dev/null
	./$(ACTION_Q_NESTED_ACTOR_ANCHOR_TEST_RUNNER)
	@set +e; output=`./$(ACTION_Q_NESTED_ACTOR_ANCHOR) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ4-G3 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-action-q-nested-actor-anchor --run' >/dev/null
	./$(ACTION_Q_NESTED_ACTOR_BROAD_DISTILL_TEST_RUNNER)
	@set +e; output=`./$(ACTION_Q_NESTED_ACTOR_BROAD_DISTILL) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ4-G4B accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-action-q-broad-distill (--preflight|--census|--run)' >/dev/null
	./$(DECISION_BOUNDARY_CRITIC_TEST_RUNNER)
	@set +e; output=`./$(DECISION_BOUNDARY_CRITIC) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ10-DBC0 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-decision-boundary-critic (--census|--run|--cache)' >/dev/null
	./$(DECISION_BOUNDARY_RANK_DIRECT_TEST_RUNNER)
	@set +e; output=`./$(DECISION_BOUNDARY_RANK_DIRECT) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ11-DBC2 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-decision-boundary-rank-direct --run' >/dev/null
	./$(DECISION_BOUNDARY_RANK_HIDDEN_TEST_RUNNER)
	@set +e; output=`./$(DECISION_BOUNDARY_RANK_HIDDEN) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ12-DBC3 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-decision-boundary-rank-hidden --run' >/dev/null
	./$(DECISION_BOUNDARY_ACTION_PAIR_TEST_RUNNER)
	@set +e; output=`./$(DECISION_BOUNDARY_ACTION_PAIR) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ13-DBC4 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-decision-boundary-action-pair --run' >/dev/null
	./$(DECISION_BOUNDARY_ADAPTIVE_TRUNK_TEST_RUNNER)
	@set +e; output=`./$(DECISION_BOUNDARY_ADAPTIVE_TRUNK) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ14-DBC5 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-decision-boundary-adaptive-trunk --run' >/dev/null
	./$(DECISION_DENSITY_CENSUS_TEST_RUNNER)
	@set +e; output=`./$(DECISION_DENSITY_CENSUS) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ16-DBC6 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-decision-density-census --census' >/dev/null
	./$(DECISION_DENSITY_PRIORITY_TEST_RUNNER)
	@set +e; output=`./$(DECISION_DENSITY_PRIORITY) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ17-DBC6 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-decision-density-priority --select' >/dev/null
	./$(DECISION_DENSITY_LABELS_TEST_RUNNER)
	@set +e; output=`./$(DECISION_DENSITY_LABELS) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ18-DBC6 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-decision-density-labels --publish' >/dev/null
	./$(LEARNED_PRIORITY_BILINEAR_TEST_RUNNER)
	./$(LEARNED_PRIORITY_BILINEAR_ARTIFACT_TEST_RUNNER)
	./$(DECISION_DENSITY_BILINEAR_TEST_RUNNER)
	@set +e; output=`./$(DECISION_DENSITY_BILINEAR) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ19-DBC6 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-decision-density-bilinear (--run|--offline-report)' >/dev/null
	@set +e; output=`./$(DECISION_DENSITY_BILINEAR_ARTIFACT) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ19 artifact publisher accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-decision-density-bilinear-artifact --publish' >/dev/null
	./$(DECISION_DENSITY_SPARSE_SUPPORT_TEST_RUNNER)
	@set +e; output=`./$(DECISION_DENSITY_SPARSE_SUPPORT) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ20 support census accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-decision-density-sparse-support --census' >/dev/null
	./$(LEARNED_PRIORITY_SPARSE_CROSS_TEST_RUNNER)
	./$(DECISION_DENSITY_SPARSE_CROSS_TEST_RUNNER)
	@set +e; output=`./$(DECISION_DENSITY_SPARSE_CROSS) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ20 sparse cross accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-decision-density-sparse-cross --offline-report' >/dev/null
	./$(ACTION_Q_ON_POLICY_SUCCESSOR_TEST_RUNNER)
	@set +e; output=`./$(ACTION_Q_ON_POLICY_SUCCESSOR) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ4-OP1 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-action-q-on-policy-successor (--census|--run)' >/dev/null
	./$(ACTION_Q_PRIORITY_TRUST_REGION_TEST_RUNNER)
	@set +e; output=`./$(ACTION_Q_PRIORITY_TRUST_REGION) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ4-OP2 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-action-q-priority-trust-region --run' >/dev/null
	./$(ACTION_Q_RECURSIVE_POLICY_IMPROVEMENT_TEST_RUNNER)
	./$(CONSERVATIVE_POLICY_IMPROVEMENT_TEST_RUNNER)
	./$(EXACT_COMBAT_SUBGAME_TEST_RUNNER)
	./$(INFORMATION_SET_PUCT_TEST_RUNNER)
	./$(LEARNED_GENERATIVE_SEARCH_TEST_RUNNER)
	./$(INFORMATION_SET_PUCT_PREFLIGHT_TEST_RUNNER)
	@set +e; output=`./$(INFORMATION_SET_PUCT_PREFLIGHT) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'ISP0 preflight accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null
	./$(INFORMATION_SET_PUCT_BUDGET_DIAGNOSTIC_TEST_RUNNER)
	@set +e; output=`./$(INFORMATION_SET_PUCT_BUDGET_DIAGNOSTIC) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'ISP1 budget diagnostic accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null
	@set +e; output=`./$(ACTION_Q_RECURSIVE_POLICY_IMPROVEMENT) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ5-RPI0 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null
	@set +e; output=`./$(ACTION_Q_MULTISCALE_EXPLORE) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ2 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-action-q-multiscale-explore (--census|--run)' >/dev/null
	@set +e; output=`./$(ACTION_Q_BELLMAN_EXPLORE) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ1 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-action-q-bellman-explore (--census|--run|--diagnose-teacher)' >/dev/null
	@set +e; output=`./$(ACTION_Q_EXPLORE) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ0 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-action-q-explore (--census|--run)' >/dev/null
	@set +e; output=`./$(FQ4_DEV5_GAMEPLAY) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ4-DEV5 gameplay accepted an arbitrary mode\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage: old-school-fq4-dev5-gameplay --smoke' >/dev/null
	./$(FQ4_DEV_BACKGROUND_DIAGNOSTIC_TEST_RUNNER)
	./$(FQ4_DEV_COVERAGE_CENSUS_TEST_RUNNER)
	./$(FQ4_NEUTRAL_SUPPLEMENT_TEST_RUNNER)
	./$(FQ4_NEUTRAL_PUBLISHER_TEST_RUNNER)
	./$(FQ4_NEUTRAL_EVALUATOR_TEST_RUNNER)
	./$(FQ4_NEUTRAL_EVALUATOR_RUNNER_TEST_RUNNER)
	./$(FQ4_NEUTRAL_CANDIDATE_PUBLISHER_TEST_RUNNER)
	@set +e; output=`./$(FQ4_NEUTRAL_CANDIDATE_PUBLISHER) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ4-DEV5 anchored candidate publisher accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage: old-school-fq4-dev5-candidate-publish' >/dev/null
	./$(FQ4_PRIORITY_FIT_TEST_RUNNER)
	@set +e; output=`./$(FQ0_BELLMAN_AUDIT) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ0-T0 CLI accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage: old-school-fq0-bellman-audit' >/dev/null
	@set +e; output=`./$(FQ4_PRIORITY_FIT) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ4-D0 CLI accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null
	@set +e; output=`./$(FQ4_DEV_GENERATOR) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ4-DEV1 generator CLI accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null
	@if rg -n 'setpgid|killpg|::kill\(-' \
		src/fq4_dev_generator_main.cpp >/dev/null; then \
		printf 'FQ4-DEV1 watchdog detached or group-signaled its worker\n' >&2; \
		exit 1; \
	fi
	@if rg -n 'error=|\.what\(\)' \
		src/fq4_dev_generator_main.cpp >/dev/null; then \
		printf 'FQ4-DEV1 production CLI can disclose exception details\n' >&2; \
		exit 1; \
	fi
	@set +e; output=`./$(FQ4_DEV_EVALUATOR) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ4-DEV1 evaluator CLI accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null
	@case " $(FQ4_DEV_EVALUATOR_LINK_SOURCES) " in \
		*" src/fq4_dev_generator.cpp "*|\
		*" src/fq4_d1_field_gate.cpp "*|\
		*" src/fq4_d1_treatment.cpp "*|\
		*" src/fq4_d1_treatment_production.cpp "*|\
		*" src/fq4_priority_fit.cpp "*|\
		*" src/fq4_d1_census_main.cpp "*) \
			printf 'FQ4-DEV1 evaluator link graph crossed a generator/held-out firewall\n' >&2; \
			exit 1;; \
	esac
	@if rg -n \
		'#include "old_school/(fq4_dev_generator|fq4_d1_field_gate|fq4_d1_treatment|fq4_priority_fit)\.hpp"|GameState|\\bGame\\s*[({]|score_production|determinize_|rollout_game' \
		include/old_school/fq4_dev_evaluator.hpp \
		src/fq4_dev_evaluator.cpp \
		src/fq4_dev_evaluator_main.cpp >/dev/null; then \
		printf 'FQ4-DEV1 evaluator source crossed a game/generator/held-out firewall\n' >&2; \
		exit 1; \
	fi
	@if rg -n 'error=|\.what\(\)' \
		src/fq4_dev_evaluator_main.cpp >/dev/null; then \
		printf 'FQ4-DEV1 evaluator CLI can disclose exception details\n' >&2; \
		exit 1; \
	fi
	@set +e; output=`./$(FQ4_DEV_CANDIDATE_PUBLISHER) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ4-DEV1 candidate publisher accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null
	@set +e; output=`./$(FQ4_DEV1_GAMEPLAY) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ4-DEV1 gameplay accepted an arbitrary mode\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null
	@case " $(FQ4_DEV1_GAMEPLAY_LINK_SOURCES) " in \
		*" src/fq4_dev_bundle.cpp "*|\
		*" src/fq4_dev_evaluator.cpp "*|\
		*" src/fq4_dev_generator.cpp "*|\
		*" src/fq4_priority_fit.cpp "*|\
		*" src/fq4_d1_field_gate.cpp "*|\
		*" src/fq4_d1_treatment.cpp "*|\
		*" src/fq4_d1_treatment_production.cpp "*|\
		*" src/fq0_information_set.cpp "*|\
		*" src/fq0_dominance.cpp "*|\
		*" src/fq0_dominance_transition.cpp "*) \
			printf 'FQ4-DEV1 gameplay link graph crossed its load-only firewall\n' >&2; \
			exit 1;; \
	esac
	@if rg -n \
		'#include "old_school/(fq4_dev_bundle|fq4_dev_evaluator|fq4_dev_generator|fq4_priority_fit|fq4_d1_field_gate|fq4_d1_treatment|fq0_information_set|fq0_dominance|fq0_dominance_transition)\.hpp"' \
		include/old_school/fq4_dev_candidate_artifact.hpp \
		include/old_school/fq4_dev1_gameplay.hpp \
		src/fq4_dev_candidate_artifact.cpp \
		src/fq4_dev1_gameplay.cpp \
		src/fq4_dev1_gameplay_main.cpp >/dev/null; then \
		printf 'FQ4-DEV1 load-only source crossed a training/evaluator firewall\n' >&2; \
		exit 1; \
	fi
	@if nm "$(FQ4_DEV1_GAMEPLAY)" | \
		rg -q 'fq4_dev_(bundle|evaluator|generator)|fq4_priority_fit|fq4_d1_|fq0_(information_set|dominance)'; then \
		printf 'FQ4-DEV1 gameplay binary contains forbidden research symbols\n' >&2; \
		exit 1; \
	fi
	@set +e; output=`./$(FQ4_DEV_BACKGROUND_DIAGNOSTIC) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ4-DEV2 background diagnostic accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null
	@set +e; output=`./$(FQ4_DEV_COVERAGE_CENSUS) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ4-DEV4 coverage census accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null
	@if rg -n 'error=|\.what\(\)' \
		src/fq4_dev_coverage_census_main.cpp >/dev/null; then \
		printf 'FQ4-DEV4 production CLI can disclose exception details\n' >&2; \
		exit 1; \
	fi
	@set +e; output=`./$(FQ4_PRIORITY_FIT_D0B) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ4-D0b CLI accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null
	./$(FQ4_D1_FIELD_GATE_TEST_RUNNER)
	@set +e; output=`./$(FQ4_D1_CENSUS) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ4-D1-P0 CLI accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null
	@test -z "$(filter $(FQ4_PRIORITY_FIT_SOURCE) $(FQ4_D1_TREATMENT_SOURCE) $(FQ4_D1_TREATMENT_PRODUCTION_SOURCE),$(FQ4_D1_FIELD_GATE_LINK_SOURCES))" || { \
		printf 'FQ4-D1-P0 link graph contains treatment source\n' >&2; \
		exit 1; \
	}
	@if nm "$(FQ4_D1_CENSUS)" | rg -q 'fq4_priority_fit|fq4_d1_treatment'; then \
		printf 'FQ4-D1-P0 binary contains treatment symbols\n' >&2; \
		exit 1; \
	fi
	@if strings "$(FQ4_D1_CENSUS)" | rg -q '81ad05d2c32bea9b17ca4c89cbbf7a9be105ad130897f79fa4d8a29a5ea1105e'; then \
		printf 'FQ4-D1-P0 binary contains treatment fingerprint\n' >&2; \
		exit 1; \
	fi
	./$(FQ4_D1_TREATMENT_TEST_RUNNER)
	@set +e; output=`./$(FQ4_D1_TREATMENT) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ4-D1-T0 CLI accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null
	@test -z "$(filter $(FQ4_D1_TREATMENT_SOURCE) $(FQ4_D1_TREATMENT_PRODUCTION_SOURCE),$(FQ4_D1_FIELD_GATE_LINK_SOURCES))" || { \
		printf 'FQ4-D1-P0 link graph contains D1 treatment source\n' >&2; \
		exit 1; \
	}
	@if nm "$(FQ4_D1_CENSUS)" | rg -q 'fq4_d1_treatment'; then \
		printf 'FQ4-D1-P0 binary contains D1 treatment symbols\n' >&2; \
		exit 1; \
	fi
	@if nm "$(call source_objects,$(FQ4_D1_TREATMENT_SOURCE))" | rg -q 'score_production'; then \
		printf 'FQ4-D1 tensor evaluator directly references production search\n' >&2; \
		exit 1; \
	fi
	@if nm -u "$(call source_objects,$(FQ4_D1_TREATMENT_SOURCE))" | rg -q 'run_parent_census|fit_d0b_production|score_production|sample_determinization|physical_game_id'; then \
		printf 'FQ4-D1 tensor evaluator directly references census, fit, search, or source helpers\n' >&2; \
		exit 1; \
	fi
	@if rg -n 'run_parent_census|fit_d0b_production|score_production|sample_determinization|field::physical_game_id' "$(FQ4_D1_TREATMENT_SOURCE)" >/dev/null; then \
		printf 'FQ4-D1 tensor evaluator source contains census, fit, search, or source helpers\n' >&2; \
		exit 1; \
	fi
	./$(FQ4_DEV_SCHEDULE_TEST_RUNNER)
	@set +e; output=`./$(FQ4_DEV_SCHEDULE) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ4-DEV1 schedule CLI accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null
	./$(WEB_BRIDGE_TEST_RUNNER)
	sh tests/test_cli.sh ./$(SIMULATOR)
	sh tests/test_capture_once.sh
	sh tests/test_make_clean.sh
	sh tests/test_make_incremental.sh
	./$(SIMULATOR) --games 5 --seed 1 >/dev/null
	npm --prefix web test
	PYTHONDONTWRITEBYTECODE=1 python3 -m unittest tests/test_certify.py

test-build-graph:
	sh tests/test_make_incremental.sh

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

fq0-quarantine-supervisor: $(FQ0_RUSAGE_GUARD)

test-fq0-rusage-guard: $(FQ0_RUSAGE_GUARD_TEST_RUNNER) $(FQ0_RUSAGE_GUARD)
	./$(FQ0_RUSAGE_GUARD_TEST_RUNNER)
	@set +e; output=`./$(FQ0_RUSAGE_GUARD) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 95 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ0 rusage supervisor accepted invalid CLI syntax\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage: old-school-fq0-quarantine-supervisor' >/dev/null

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

test-fq0: test-fq0-information-set test-fq0-bellman test-fq0-dominance test-fq0-dominance-transition test-fq0-bellman-science test-fq0-bellman-audit test-fq0-bellman-run test-fq0-sequence-projection test-fq0-causal-quotient

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

test-fq0-sequence-projection: $(FQ0_SEQUENCE_PROJECTION_TEST_RUNNER)
	./$(FQ0_SEQUENCE_PROJECTION_TEST_RUNNER)

test-fq0-causal-quotient: $(FQ0_CAUSAL_QUOTIENT_TEST_RUNNER) $(FQ0_CAUSAL_QUOTIENT)
	./$(FQ0_CAUSAL_QUOTIENT_TEST_RUNNER)
	@set +e; output=`./$(FQ0_CAUSAL_QUOTIENT) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FR1 causal-quotient CLI accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null

test-fq0-causal-quotient-production: $(FQ0_CAUSAL_QUOTIENT)
	@set +e; output=`./$(FQ0_CAUSAL_QUOTIENT) 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 0 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FR3 production gate did not return its registered pass\n' >&2; \
		exit 1; \
	fi; \
	require_line() { \
		printf '%s\n' "$$output" | grep -Fx "$$1" >/dev/null || { \
			printf 'FR3 production output missing: %s\n' "$$1" >&2; \
			exit 1; \
		}; \
	}; \
	require_line 'direct_blue=1/2 direct_white=1/4 life_control=1'; \
	require_line 'root_macros=448 incomplete=0'; \
	require_line 'pairs=44 blue_pairs=38 white_pairs=6'; \
	require_line 'legacy_rows=177 blue_rows=163 white_rows=14'; \
	require_line 'row_identity_sha256=564d2a185c6d591b9848a33b7f19c669893c7e4c85aba0b7054e931f1533745c'; \
	require_line 'reconstructed=44 graveyard_only=39/167 additional_public_difference=5/10 equivalent=39'; \
	require_line 'fr1_verdict=REJECT'; \
	require_line 'fr2_pairs=5 contrasts=15 action_comparisons=30'; \
	require_line 'fr2_equal graveyards=5 observer_hand=5 combined=5'; \
	require_line 'fr2_controls wrong_masks=4/4 life=1'; \
	require_line 'repeat_bit_identical=1'; \
	require_line 'fr2_verdict=PASS'; \
	require_line 'fr3_pairs=44 paired_actions=177 source_instances=354'; \
	require_line 'fr3_identity exact_legacy=44 quotient_equal=44 feature_bit_identical=177/177'; \
	require_line 'fr3_consequences legacy_conflicts=177 residual_quotient_conflicts=0'; \
	require_line 'fr3_leaf legacy_conflicts=44 residual_quotient_conflicts=0'; \
	require_line 'fr3_controls graveyard_multiset=1 life=1 hidden_repartition=1'; \
	require_line 'fr3_catalog_sha256=ffe52f04973793f49d5a841384b8992fc650682f99b7b4e1c8107948582cb7e8'; \
	require_line 'fr3_verdict=PASS'

fq0-causal-quotient: $(FQ0_CAUSAL_QUOTIENT)
	./$(FQ0_CAUSAL_QUOTIENT)

test-fq4-priority-math: $(FQ4_PRIORITY_MATH_TEST_RUNNER)
	./$(FQ4_PRIORITY_MATH_TEST_RUNNER)

test-fq4-priority-collection: $(FQ4_PRIORITY_COLLECTION_TEST_RUNNER)
	./$(FQ4_PRIORITY_COLLECTION_TEST_RUNNER)

test-fq4-work0-cache: $(FQ4_WORK0_CACHE_TEST_RUNNER)
	./$(FQ4_WORK0_CACHE_TEST_RUNNER)

FQ4_WORK0_FORBIDDEN_LINK_SOURCES := $(FQ4_DEV_GENERATOR_SOURCE) $(OC1_ACTION_SCORING_SOURCE) $(FQ4_PRIORITY_FIT_SOURCE) $(FQ4_DEV_CANDIDATE_ARTIFACT_SOURCE) $(FQ4_DEV_CANDIDATE_PUBLISHER_SOURCE) $(FQ4_NEUTRAL_CANDIDATE_PUBLISHER_SOURCE) $(FQ4_DEV1_GAMEPLAY_SOURCE) $(FQ4_DEV5_GAMEPLAY_SOURCE)

test-fq4-work0-firewall:
	@if test -n "$(filter $(FQ4_WORK0_CACHE_SOURCE) $(FQ4_DEV_GENERATOR_SOURCE),$(SIMULATOR_LINK_SOURCES))"; then \
		printf 'FQ4 WORK0 research source leaked into simulator link graph\n' >&2; \
		exit 1; \
	fi
	@if test -n "$(filter $(FQ4_WORK0_CACHE_SOURCE) $(FQ4_DEV_GENERATOR_SOURCE),$(WEB_BRIDGE_LINK_SOURCES))"; then \
		printf 'FQ4 WORK0 research source leaked into web link graph\n' >&2; \
		exit 1; \
	fi
	@if test -n "$(filter $(FQ4_WORK0_FORBIDDEN_LINK_SOURCES),$(FQ4_WORK0_CACHE_LINK_SOURCES))"; then \
		printf 'FQ4 WORK0 cache test graph gained a research producer/scorer\n' >&2; \
		exit 1; \
	fi
	@if rg -n 'fq4_dev_generator' \
		include/old_school/fq4_work0_cache.hpp \
		src/fq4_work0_cache.cpp; then \
		printf 'FQ4 WORK0 cache source includes its producer\n' >&2; \
		exit 1; \
	fi
	@if rg -n '(publish_atomic|generate_and_publish|write_cache|save_cache)[[:space:]]*\(' \
		include/old_school/fq4_work0_cache.hpp; then \
		printf 'FQ4 WORK0 cache seam exposes publication/write authority\n' >&2; \
		exit 1; \
	fi

test-fq4-dev-bundle: $(FQ4_DEV_BUNDLE_TEST_RUNNER)
	./$(FQ4_DEV_BUNDLE_TEST_RUNNER)

test-fq4-dev-generator: $(FQ4_DEV_GENERATOR_TEST_RUNNER) $(FQ4_DEV_GENERATOR)
	./$(FQ4_DEV_GENERATOR_TEST_RUNNER)
	@set +e; output=`./$(FQ4_DEV_GENERATOR) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ4-DEV1 generator CLI accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null
	@if ! /usr/bin/strings -a "$(FQ4_DEV_GENERATOR)" | \
		grep -Fx "$(FQ4_DEV_PRODUCER_COMMIT)" >/dev/null; then \
		printf 'FQ4-DEV1 generator lost its exact producer commit\n' >&2; \
		exit 1; \
	fi
	@if /usr/bin/strings -a "$(FQ4_DEV_GENERATOR)" | \
		grep -F 'built without OLD_SCHOOL_FQ4_PRODUCER_COMMIT' >/dev/null; then \
		printf 'FQ4-DEV1 generator retained the unconfigured fallback\n' >&2; \
		exit 1; \
	fi
	@case " $(FQ4_DEV_GENERATOR_LINK_SOURCES) " in \
		*" src/fq4_d1_field_gate.cpp "*|\
		*" src/fq4_d1_treatment.cpp "*|\
		*" src/fq4_d1_treatment_production.cpp "*|\
		*" src/fq4_priority_fit.cpp "*|\
		*" src/fq4_d1_census_main.cpp "*) \
			printf 'FQ4-DEV1 generator link graph crossed a held-out firewall\n' >&2; \
			exit 1;; \
	esac
	@if rg -n \
		'#include "old_school/(fq4_d1_field_gate|fq4_d1_treatment|fq4_priority_fit)\.hpp"' \
		include/old_school/fq4_dev_generator.hpp \
		src/fq4_dev_generator.cpp \
		src/fq4_dev_generator_main.cpp >/dev/null; then \
		printf 'FQ4-DEV1 generator included a held-out/treatment module\n' >&2; \
		exit 1; \
	fi
	@if nm "$(FQ4_DEV_GENERATOR)" | \
		grep -E 'run_parent_census|fq4_d1_(field_gate|treatment)|fq4_priority_fit' >/dev/null; then \
		printf 'FQ4-DEV1 generator binary crossed a held-out symbol firewall\n' >&2; \
		exit 1; \
	fi
	@if rg -n 'setpgid|killpg|::kill\(-' \
		src/fq4_dev_generator_main.cpp >/dev/null; then \
		printf 'FQ4-DEV1 watchdog detached or group-signaled its worker\n' >&2; \
		exit 1; \
	fi
	@if rg -n 'error=|\.what\(\)' \
		src/fq4_dev_generator_main.cpp >/dev/null; then \
		printf 'FQ4-DEV1 production CLI can disclose exception details\n' >&2; \
		exit 1; \
	fi

test-fq4-dev-evaluator: $(FQ4_DEV_EVALUATOR_TEST_RUNNER) $(FQ4_DEV_EVALUATOR)
	./$(FQ4_DEV_EVALUATOR_TEST_RUNNER)
	@set +e; output=`./$(FQ4_DEV_EVALUATOR) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ4-DEV1 evaluator CLI accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null
	@case " $(FQ4_DEV_EVALUATOR_LINK_SOURCES) " in \
		*" src/fq4_dev_generator.cpp "*|\
		*" src/fq4_d1_field_gate.cpp "*|\
		*" src/fq4_d1_treatment.cpp "*|\
		*" src/fq4_d1_treatment_production.cpp "*|\
		*" src/fq4_priority_fit.cpp "*|\
		*" src/fq4_d1_census_main.cpp "*) \
			printf 'FQ4-DEV1 evaluator link graph crossed a generator/held-out firewall\n' >&2; \
			exit 1;; \
	esac
	@if rg -n \
		'#include "old_school/(fq4_dev_generator|fq4_d1_field_gate|fq4_d1_treatment|fq4_priority_fit)\.hpp"|GameState|\\bGame\\s*[({]|score_production|determinize_|rollout_game' \
		include/old_school/fq4_dev_evaluator.hpp \
		src/fq4_dev_evaluator.cpp \
		src/fq4_dev_evaluator_main.cpp >/dev/null; then \
		printf 'FQ4-DEV1 evaluator source crossed a game/generator/held-out firewall\n' >&2; \
		exit 1; \
	fi
	@if rg -n 'error=|\.what\(\)' \
		src/fq4_dev_evaluator_main.cpp >/dev/null; then \
		printf 'FQ4-DEV1 evaluator CLI can disclose exception details\n' >&2; \
		exit 1; \
	fi

fq4-dev-evaluator: $(FQ4_DEV_EVALUATOR)
	./$(FQ4_DEV_EVALUATOR) --evaluate-parent

test-fq4-dev-candidate-artifact: $(FQ4_DEV_CANDIDATE_ARTIFACT_TEST_RUNNER)
	./$(FQ4_DEV_CANDIDATE_ARTIFACT_TEST_RUNNER)

test-fq4-dev-candidate-publisher: $(FQ4_DEV_CANDIDATE_PUBLISHER_TEST_RUNNER) $(FQ4_DEV_CANDIDATE_PUBLISHER)
	./$(FQ4_DEV_CANDIDATE_PUBLISHER_TEST_RUNNER)
	@set +e; output=`./$(FQ4_DEV_CANDIDATE_PUBLISHER) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ4-DEV1 candidate publisher accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null

fq4-dev-candidate-publish: $(FQ4_DEV_CANDIDATE_PUBLISHER)
	./$(FQ4_DEV_CANDIDATE_PUBLISHER)

test-fq4-dev1-gameplay: $(FQ4_DEV1_GAMEPLAY_TEST_RUNNER) $(FQ4_DEV1_GAMEPLAY)
	./$(FQ4_DEV1_GAMEPLAY_TEST_RUNNER)
	@set +e; output=`./$(FQ4_DEV1_GAMEPLAY) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ4-DEV1 gameplay accepted an arbitrary mode\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null
	@case " $(FQ4_DEV1_GAMEPLAY_LINK_SOURCES) " in \
		*" src/fq4_dev_bundle.cpp "*|\
		*" src/fq4_dev_evaluator.cpp "*|\
		*" src/fq4_dev_generator.cpp "*|\
		*" src/fq4_priority_fit.cpp "*|\
		*" src/fq4_d1_field_gate.cpp "*|\
		*" src/fq4_d1_treatment.cpp "*|\
		*" src/fq4_d1_treatment_production.cpp "*|\
		*" src/fq0_information_set.cpp "*|\
		*" src/fq0_dominance.cpp "*|\
		*" src/fq0_dominance_transition.cpp "*) \
			printf 'FQ4-DEV1 gameplay link graph crossed its load-only firewall\n' >&2; \
			exit 1;; \
	esac
	@if nm "$(FQ4_DEV1_GAMEPLAY)" | \
		rg -q 'fq4_dev_(bundle|evaluator|generator)|fq4_priority_fit|fq4_d1_|fq0_(information_set|dominance)'; then \
		printf 'FQ4-DEV1 gameplay binary contains forbidden research symbols\n' >&2; \
		exit 1; \
	fi

test-fq4-dev5-gameplay: $(FQ4_DEV5_GAMEPLAY_TEST_RUNNER) $(FQ4_DEV5_GAMEPLAY)
	./$(FQ4_DEV5_GAMEPLAY_TEST_RUNNER)
	@set +e; output=`./$(FQ4_DEV5_GAMEPLAY) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ4-DEV5 gameplay accepted an arbitrary mode\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage: old-school-fq4-dev5-gameplay --smoke' >/dev/null
	@case " $(FQ4_DEV5_GAMEPLAY_LINK_SOURCES) " in \
		*" src/fq4_dev1_gameplay.cpp "*|\
		*" src/fq4_dev_bundle.cpp "*|\
		*" src/fq4_dev_evaluator.cpp "*|\
		*" src/fq4_dev_generator.cpp "*|\
		*" src/fq4_dev_candidate_publisher.cpp "*|\
		*" src/fq4_dev_coverage_census.cpp "*|\
		*" src/fq4_neutral_supplement.cpp "*|\
		*" src/fq4_neutral_evaluator.cpp "*|\
		*" src/fq4_neutral_evaluator_runner.cpp "*|\
		*" src/fq4_neutral_candidate_publisher.cpp "*|\
		*" src/fq4_priority_collection.cpp "*|\
		*" src/fq4_priority_fit.cpp "*|\
		*" src/fq4_d1_field_gate.cpp "*|\
		*" src/fq4_d1_treatment.cpp "*|\
		*" src/fq4_d1_treatment_production.cpp "*|\
		*" src/fq0_information_set.cpp "*|\
		*" src/fq0_dominance.cpp "*|\
		*" src/fq0_dominance_transition.cpp "*) \
			printf 'FQ4-DEV5 gameplay link graph crossed its load-only firewall\n' >&2; \
			exit 1;; \
	esac
	@if rg -n \
		'#include "old_school/(fq4_dev1_gameplay|fq4_dev_bundle|fq4_dev_evaluator|fq4_dev_generator|fq4_dev_candidate_publisher|fq4_dev_coverage_census|fq4_neutral_supplement|fq4_neutral_evaluator|fq4_neutral_candidate_publisher|fq4_priority_collection|fq4_priority_fit|fq4_d1_field_gate|fq4_d1_treatment|fq0_information_set|fq0_dominance|fq0_dominance_transition)\.hpp"|\bproduction_contract[[:space:]]*\(' \
		include/old_school/fq4_dev5_gameplay.hpp \
		src/fq4_dev5_gameplay.cpp \
		src/fq4_dev5_gameplay_main.cpp >/dev/null; then \
		printf 'FQ4-DEV5 gameplay source crossed its load-only contract firewall\n' >&2; \
		exit 1; \
	fi
	@if nm "$(call source_objects,$(FQ4_DEV5_GAMEPLAY_SOURCE))" | c++filt | \
		rg -q 'fq4_dev_candidate_artifact::production_contract'; then \
		printf 'FQ4-DEV5 gameplay object references the generic production contract\n' >&2; \
		exit 1; \
	fi
	@if nm "$(FQ4_DEV5_GAMEPLAY)" | c++filt | \
		rg -q 'old_school::fq4_dev1_gameplay|old_school::fq4_(dev_bundle|dev_evaluator|dev_generator|dev_candidate_publisher|dev_coverage_census|neutral_|priority_collection|priority_fit|d1_)|old_school::fq0_'; then \
		printf 'FQ4-DEV5 gameplay binary contains forbidden research symbols\n' >&2; \
		exit 1; \
	fi

fq4-dev5-gameplay-smoke: $(FQ4_DEV5_GAMEPLAY)
	./$(FQ4_DEV5_GAMEPLAY) --smoke

test-fq4-blend-explore: $(FQ4_BLEND_EXPLORE_TEST_RUNNER) $(FQ4_BLEND_EXPLORE)
	./$(FQ4_BLEND_EXPLORE_TEST_RUNNER)
	@set +e; output=`./$(FQ4_BLEND_EXPLORE) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ4 blend explorer accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage: old-school-fq4-blend-explore' >/dev/null

fq4-blend-explore: $(FQ4_BLEND_EXPLORE)
	./$(FQ4_BLEND_EXPLORE)

test-action-q-explore: $(ACTION_Q_EXPLORE_TEST_RUNNER) $(ACTION_Q_EXPLORE)
	./$(ACTION_Q_EXPLORE_TEST_RUNNER)
	@set +e; output=`./$(ACTION_Q_EXPLORE) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ0 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-action-q-explore (--census|--run)' >/dev/null

test-action-q-field-gate: $(ACTION_Q_FIELD_GATE_TEST_RUNNER)
	./$(ACTION_Q_FIELD_GATE_TEST_RUNNER)

test-action-q-offline-gate: $(ACTION_Q_OFFLINE_GATE_TEST_RUNNER)
	./$(ACTION_Q_OFFLINE_GATE_TEST_RUNNER)

action-q-census: $(ACTION_Q_EXPLORE)
	./$(ACTION_Q_EXPLORE) --census

action-q-run: $(ACTION_Q_EXPLORE)
	./$(ACTION_Q_EXPLORE) --run

test-action-q-bellman-teacher: $(ACTION_Q_BELLMAN_TEACHER_TEST_RUNNER)
	./$(ACTION_Q_BELLMAN_TEACHER_TEST_RUNNER)

test-action-q-bellman-explore: $(ACTION_Q_BELLMAN_EXPLORE_TEST_RUNNER) $(ACTION_Q_BELLMAN_EXPLORE)
	./$(ACTION_Q_BELLMAN_EXPLORE_TEST_RUNNER)
	@set +e; output=`./$(ACTION_Q_BELLMAN_EXPLORE) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ1 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-action-q-bellman-explore (--census|--run|--diagnose-teacher)' >/dev/null

action-q-bellman-census: $(ACTION_Q_BELLMAN_EXPLORE)
	./$(ACTION_Q_BELLMAN_EXPLORE) --census

action-q-bellman-run: $(ACTION_Q_BELLMAN_EXPLORE)
	./$(ACTION_Q_BELLMAN_EXPLORE) --run

test-action-q-multiscale-teacher: $(ACTION_Q_MULTISCALE_TEACHER_TEST_RUNNER)
	./$(ACTION_Q_MULTISCALE_TEACHER_TEST_RUNNER)

test-action-q-multiscale-explore: $(ACTION_Q_MULTISCALE_EXPLORE_TEST_RUNNER) $(ACTION_Q_MULTISCALE_EXPLORE)
	./$(ACTION_Q_MULTISCALE_EXPLORE_TEST_RUNNER)
	@set +e; output=`./$(ACTION_Q_MULTISCALE_EXPLORE) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ2 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-action-q-multiscale-explore (--census|--run)' >/dev/null

action-q-multiscale-census: $(ACTION_Q_MULTISCALE_EXPLORE)
	./$(ACTION_Q_MULTISCALE_EXPLORE) --census

action-q-multiscale-run: $(ACTION_Q_MULTISCALE_EXPLORE)
	./$(ACTION_Q_MULTISCALE_EXPLORE) --run

test-action-q-long-horizon-diagnostic: $(ACTION_Q_LONG_HORIZON_DIAGNOSTIC_TEST_RUNNER) $(ACTION_Q_LONG_HORIZON_DIAGNOSTIC)
	./$(ACTION_Q_LONG_HORIZON_DIAGNOSTIC_TEST_RUNNER)
	@set +e; output=`./$(ACTION_Q_LONG_HORIZON_DIAGNOSTIC) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ3-D0 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-action-q-long-horizon-diagnostic --diagnose' >/dev/null

action-q-long-horizon-diagnose: $(ACTION_Q_LONG_HORIZON_DIAGNOSTIC)
	./$(ACTION_Q_LONG_HORIZON_DIAGNOSTIC) --diagnose

test-action-q-nested-actor-diagnostic: $(ACTION_Q_NESTED_ACTOR_DIAGNOSTIC_TEST_RUNNER) $(ACTION_Q_NESTED_ACTOR_DIAGNOSTIC)
	./$(ACTION_Q_NESTED_ACTOR_DIAGNOSTIC_TEST_RUNNER)
	@set +e; output=`./$(ACTION_Q_NESTED_ACTOR_DIAGNOSTIC) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ4-D1 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-action-q-nested-actor-diagnostic --diagnose' >/dev/null

action-q-nested-actor-diagnose: $(ACTION_Q_NESTED_ACTOR_DIAGNOSTIC)
	./$(ACTION_Q_NESTED_ACTOR_DIAGNOSTIC) --diagnose

test-action-q-nested-actor-distill: $(ACTION_Q_NESTED_ACTOR_DISTILL_TEST_RUNNER) $(ACTION_Q_NESTED_ACTOR_DISTILL)
	./$(ACTION_Q_NESTED_ACTOR_DISTILL_TEST_RUNNER)
	@set +e; output=`./$(ACTION_Q_NESTED_ACTOR_DISTILL) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ4-G1 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-action-q-nested-actor-distill (--census|--run)' >/dev/null

action-q-nested-actor-distill-census: $(ACTION_Q_NESTED_ACTOR_DISTILL)
	./$(ACTION_Q_NESTED_ACTOR_DISTILL) --census

action-q-nested-actor-distill-run: $(ACTION_Q_NESTED_ACTOR_DISTILL)
	./$(ACTION_Q_NESTED_ACTOR_DISTILL) --run

test-action-q-nested-actor-early-stop: $(ACTION_Q_NESTED_ACTOR_EARLY_STOP_TEST_RUNNER) $(ACTION_Q_NESTED_ACTOR_EARLY_STOP)
	./$(ACTION_Q_NESTED_ACTOR_EARLY_STOP_TEST_RUNNER)
	@set +e; output=`./$(ACTION_Q_NESTED_ACTOR_EARLY_STOP) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ4-G2 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-action-q-nested-actor-early-stop --run' >/dev/null

action-q-nested-actor-early-stop-run: $(ACTION_Q_NESTED_ACTOR_EARLY_STOP)
	./$(ACTION_Q_NESTED_ACTOR_EARLY_STOP) --run

test-action-q-nested-actor-anchor: $(ACTION_Q_NESTED_ACTOR_ANCHOR_TEST_RUNNER) $(ACTION_Q_NESTED_ACTOR_ANCHOR)
	./$(ACTION_Q_NESTED_ACTOR_ANCHOR_TEST_RUNNER)
	@set +e; output=`./$(ACTION_Q_NESTED_ACTOR_ANCHOR) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ4-G3 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-action-q-nested-actor-anchor --run' >/dev/null

action-q-nested-actor-anchor-run: $(ACTION_Q_NESTED_ACTOR_ANCHOR)
	./$(ACTION_Q_NESTED_ACTOR_ANCHOR) --run

test-action-q-broad-distill: $(ACTION_Q_NESTED_ACTOR_BROAD_DISTILL_TEST_RUNNER) $(ACTION_Q_NESTED_ACTOR_BROAD_DISTILL)
	./$(ACTION_Q_NESTED_ACTOR_BROAD_DISTILL_TEST_RUNNER)
	@set +e; output=`./$(ACTION_Q_NESTED_ACTOR_BROAD_DISTILL) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ4-G4B accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-action-q-broad-distill (--preflight|--census|--run)' >/dev/null

action-q-broad-distill-preflight: $(ACTION_Q_NESTED_ACTOR_BROAD_DISTILL)
	./$(ACTION_Q_NESTED_ACTOR_BROAD_DISTILL) --preflight

action-q-broad-distill-census: $(ACTION_Q_NESTED_ACTOR_BROAD_DISTILL)
	./$(ACTION_Q_NESTED_ACTOR_BROAD_DISTILL) --census

action-q-broad-distill-run: $(ACTION_Q_NESTED_ACTOR_BROAD_DISTILL)
	./$(ACTION_Q_NESTED_ACTOR_BROAD_DISTILL) --run

test-decision-boundary-critic: $(DECISION_BOUNDARY_CRITIC_TEST_RUNNER) $(DECISION_BOUNDARY_CRITIC)
	./$(DECISION_BOUNDARY_CRITIC_TEST_RUNNER)
	@set +e; output=`./$(DECISION_BOUNDARY_CRITIC) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ10-DBC0 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-decision-boundary-critic (--census|--run|--cache)' >/dev/null

old-school-decision-boundary-critic: $(DECISION_BOUNDARY_CRITIC)

decision-boundary-critic-census: $(DECISION_BOUNDARY_CRITIC)
	./$(DECISION_BOUNDARY_CRITIC) --census

decision-boundary-critic-run: $(DECISION_BOUNDARY_CRITIC)
	./$(DECISION_BOUNDARY_CRITIC) --run

decision-boundary-critic-cache: $(DECISION_BOUNDARY_CRITIC)
	./$(DECISION_BOUNDARY_CRITIC) --cache

test-decision-boundary-rank-direct: $(DECISION_BOUNDARY_RANK_DIRECT_TEST_RUNNER) $(DECISION_BOUNDARY_RANK_DIRECT)
	./$(DECISION_BOUNDARY_RANK_DIRECT_TEST_RUNNER)
	@set +e; output=`./$(DECISION_BOUNDARY_RANK_DIRECT) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ11-DBC2 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-decision-boundary-rank-direct --run' >/dev/null

decision-boundary-rank-direct-run: $(DECISION_BOUNDARY_RANK_DIRECT)
	./$(DECISION_BOUNDARY_RANK_DIRECT) --run

test-decision-boundary-rank-hidden: $(DECISION_BOUNDARY_RANK_HIDDEN_TEST_RUNNER) $(DECISION_BOUNDARY_RANK_HIDDEN)
	./$(DECISION_BOUNDARY_RANK_HIDDEN_TEST_RUNNER)
	@set +e; output=`./$(DECISION_BOUNDARY_RANK_HIDDEN) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ12-DBC3 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-decision-boundary-rank-hidden --run' >/dev/null

decision-boundary-rank-hidden-run: $(DECISION_BOUNDARY_RANK_HIDDEN)
	./$(DECISION_BOUNDARY_RANK_HIDDEN) --run

test-decision-boundary-action-pair: $(DECISION_BOUNDARY_ACTION_PAIR_TEST_RUNNER) $(DECISION_BOUNDARY_ACTION_PAIR)
	./$(DECISION_BOUNDARY_ACTION_PAIR_TEST_RUNNER)
	@set +e; output=`./$(DECISION_BOUNDARY_ACTION_PAIR) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ13-DBC4 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-decision-boundary-action-pair --run' >/dev/null

decision-boundary-action-pair-run: $(DECISION_BOUNDARY_ACTION_PAIR)
	./$(DECISION_BOUNDARY_ACTION_PAIR) --run

test-decision-boundary-adaptive-trunk: $(DECISION_BOUNDARY_ADAPTIVE_TRUNK_TEST_RUNNER) $(DECISION_BOUNDARY_ADAPTIVE_TRUNK)
	./$(DECISION_BOUNDARY_ADAPTIVE_TRUNK_TEST_RUNNER)
	@set +e; output=`./$(DECISION_BOUNDARY_ADAPTIVE_TRUNK) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ14-DBC5 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-decision-boundary-adaptive-trunk --run' >/dev/null

decision-boundary-adaptive-trunk-run: $(DECISION_BOUNDARY_ADAPTIVE_TRUNK)
	./$(DECISION_BOUNDARY_ADAPTIVE_TRUNK) --run

test-decision-density-census: $(DECISION_DENSITY_CENSUS_TEST_RUNNER) $(DECISION_DENSITY_CENSUS)
	./$(DECISION_DENSITY_CENSUS_TEST_RUNNER)
	@set +e; output=`./$(DECISION_DENSITY_CENSUS) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ16-DBC6 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-decision-density-census --census' >/dev/null

old-school-decision-density-census: $(DECISION_DENSITY_CENSUS)

decision-density-census: $(DECISION_DENSITY_CENSUS)
	./$(DECISION_DENSITY_CENSUS) --census

test-decision-density-priority: $(DECISION_DENSITY_PRIORITY_TEST_RUNNER) $(DECISION_DENSITY_PRIORITY)
	./$(DECISION_DENSITY_PRIORITY_TEST_RUNNER)
	@set +e; output=`./$(DECISION_DENSITY_PRIORITY) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ17-DBC6 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-decision-density-priority --select' >/dev/null

old-school-decision-density-priority: $(DECISION_DENSITY_PRIORITY)

decision-density-priority: $(DECISION_DENSITY_PRIORITY)
	./$(DECISION_DENSITY_PRIORITY) --select

test-decision-density-labels: $(DECISION_DENSITY_LABELS_TEST_RUNNER) $(DECISION_DENSITY_LABELS)
	./$(DECISION_DENSITY_LABELS_TEST_RUNNER)
	@set +e; output=`./$(DECISION_DENSITY_LABELS) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ18-DBC6 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-decision-density-labels --publish' >/dev/null

old-school-decision-density-labels: $(DECISION_DENSITY_LABELS)

decision-density-labels-publish: $(DECISION_DENSITY_LABELS)
	./$(DECISION_DENSITY_LABELS) --publish

test-learned-priority-bilinear: $(LEARNED_PRIORITY_BILINEAR_TEST_RUNNER)
	./$(LEARNED_PRIORITY_BILINEAR_TEST_RUNNER)

test-learned-priority-bilinear-artifact: $(LEARNED_PRIORITY_BILINEAR_ARTIFACT_TEST_RUNNER)
	./$(LEARNED_PRIORITY_BILINEAR_ARTIFACT_TEST_RUNNER)

test-learned-priority-sparse-cross: $(LEARNED_PRIORITY_SPARSE_CROSS_TEST_RUNNER)
	./$(LEARNED_PRIORITY_SPARSE_CROSS_TEST_RUNNER)

test-decision-density-bilinear: $(DECISION_DENSITY_BILINEAR_TEST_RUNNER) $(DECISION_DENSITY_BILINEAR)
	./$(DECISION_DENSITY_BILINEAR_TEST_RUNNER)
	@set +e; output=`./$(DECISION_DENSITY_BILINEAR) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ19-DBC6 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-decision-density-bilinear (--run|--offline-report)' >/dev/null

decision-density-bilinear-run: $(DECISION_DENSITY_BILINEAR)
	./$(DECISION_DENSITY_BILINEAR) --run

decision-density-bilinear-offline-report: $(DECISION_DENSITY_BILINEAR)
	./$(DECISION_DENSITY_BILINEAR) --offline-report

decision-density-bilinear-artifact-publish: $(DECISION_DENSITY_BILINEAR_ARTIFACT)
	./$(DECISION_DENSITY_BILINEAR_ARTIFACT) --publish

test-decision-density-sparse-support: $(DECISION_DENSITY_SPARSE_SUPPORT_TEST_RUNNER) $(DECISION_DENSITY_SPARSE_SUPPORT)
	./$(DECISION_DENSITY_SPARSE_SUPPORT_TEST_RUNNER)
	@set +e; output=`./$(DECISION_DENSITY_SPARSE_SUPPORT) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ20 support census accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-decision-density-sparse-support --census' >/dev/null

decision-density-sparse-support-census: $(DECISION_DENSITY_SPARSE_SUPPORT)
	./$(DECISION_DENSITY_SPARSE_SUPPORT) --census

test-decision-density-sparse-cross: $(DECISION_DENSITY_SPARSE_CROSS_TEST_RUNNER) $(DECISION_DENSITY_SPARSE_CROSS)
	./$(DECISION_DENSITY_SPARSE_CROSS_TEST_RUNNER)
	@set +e; output=`./$(DECISION_DENSITY_SPARSE_CROSS) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ20 sparse cross accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-decision-density-sparse-cross --offline-report' >/dev/null

decision-density-sparse-cross-offline-report: $(DECISION_DENSITY_SPARSE_CROSS)
	./$(DECISION_DENSITY_SPARSE_CROSS) --offline-report

test-action-q-on-policy-successor: $(ACTION_Q_ON_POLICY_SUCCESSOR_TEST_RUNNER) $(ACTION_Q_ON_POLICY_SUCCESSOR)
	./$(ACTION_Q_ON_POLICY_SUCCESSOR_TEST_RUNNER)
	@set +e; output=`./$(ACTION_Q_ON_POLICY_SUCCESSOR) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ4-OP1 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-action-q-on-policy-successor (--census|--run)' >/dev/null

action-q-on-policy-successor-census: $(ACTION_Q_ON_POLICY_SUCCESSOR)
	./$(ACTION_Q_ON_POLICY_SUCCESSOR) --census

action-q-on-policy-successor-run: $(ACTION_Q_ON_POLICY_SUCCESSOR)
	./$(ACTION_Q_ON_POLICY_SUCCESSOR) --run

test-action-q-priority-trust-region: $(ACTION_Q_PRIORITY_TRUST_REGION_TEST_RUNNER) $(ACTION_Q_PRIORITY_TRUST_REGION)
	./$(ACTION_Q_PRIORITY_TRUST_REGION_TEST_RUNNER)
	@set +e; output=`./$(ACTION_Q_PRIORITY_TRUST_REGION) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ4-OP2 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage: old-school-action-q-priority-trust-region --run' >/dev/null

action-q-priority-trust-region-run: $(ACTION_Q_PRIORITY_TRUST_REGION)
	./$(ACTION_Q_PRIORITY_TRUST_REGION) --run

test-action-q-recursive-policy-improvement: $(ACTION_Q_RECURSIVE_POLICY_IMPROVEMENT_TEST_RUNNER) $(ACTION_Q_RECURSIVE_POLICY_IMPROVEMENT)
	./$(ACTION_Q_RECURSIVE_POLICY_IMPROVEMENT_TEST_RUNNER)
	@set +e; output=`./$(ACTION_Q_RECURSIVE_POLICY_IMPROVEMENT) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'AQ5-RPI0 accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null

action-q-recursive-policy-improvement-run: $(ACTION_Q_RECURSIVE_POLICY_IMPROVEMENT)
	./$(ACTION_Q_RECURSIVE_POLICY_IMPROVEMENT) --run

test-conservative-policy-improvement: $(CONSERVATIVE_POLICY_IMPROVEMENT_TEST_RUNNER)
	./$(CONSERVATIVE_POLICY_IMPROVEMENT_TEST_RUNNER)

test-exact-combat-subgame: $(EXACT_COMBAT_SUBGAME_TEST_RUNNER)
	./$(EXACT_COMBAT_SUBGAME_TEST_RUNNER)

test-information-set-puct: $(INFORMATION_SET_PUCT_TEST_RUNNER)
	./$(INFORMATION_SET_PUCT_TEST_RUNNER)

test-learned-generative-search: $(LEARNED_GENERATIVE_SEARCH_TEST_RUNNER)
	./$(LEARNED_GENERATIVE_SEARCH_TEST_RUNNER)

test-information-set-puct-preflight: $(INFORMATION_SET_PUCT_PREFLIGHT_TEST_RUNNER) $(INFORMATION_SET_PUCT_PREFLIGHT)
	./$(INFORMATION_SET_PUCT_PREFLIGHT_TEST_RUNNER)
	@set +e; output=`./$(INFORMATION_SET_PUCT_PREFLIGHT) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'ISP0 preflight accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null

test-information-set-puct-budget-diagnostic: $(INFORMATION_SET_PUCT_BUDGET_DIAGNOSTIC_TEST_RUNNER) $(INFORMATION_SET_PUCT_BUDGET_DIAGNOSTIC)
	./$(INFORMATION_SET_PUCT_BUDGET_DIAGNOSTIC_TEST_RUNNER)
	@set +e; output=`./$(INFORMATION_SET_PUCT_BUDGET_DIAGNOSTIC) unexpected 2>&1`; status=$$?; set -e; \
		if [ $$status -ne 2 ]; then \
			printf '%s\n' "$$output"; \
			printf 'ISP1 budget diagnostic accepted an arbitrary mode\n' >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null

test-fq4-dev-background-diagnostic: $(FQ4_DEV_BACKGROUND_DIAGNOSTIC_TEST_RUNNER) $(FQ4_DEV_BACKGROUND_DIAGNOSTIC)
	./$(FQ4_DEV_BACKGROUND_DIAGNOSTIC_TEST_RUNNER)
	@set +e; output=`./$(FQ4_DEV_BACKGROUND_DIAGNOSTIC) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ4-DEV2 background diagnostic accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null
	@case " $(FQ4_DEV_BACKGROUND_DIAGNOSTIC_LINK_SOURCES) " in \
		*" src/fq4_dev_generator.cpp "*|\
		*" src/fq4_d1_field_gate.cpp "*|\
		*" src/fq4_d1_treatment.cpp "*|\
		*" src/fq4_d1_treatment_production.cpp "*|\
		*" src/fq4_priority_fit.cpp "*|\
		*" src/fq4_d1_census_main.cpp "*) \
			printf 'FQ4-DEV2 background diagnostic link graph crossed a generator/held-out firewall\n' >&2; \
			exit 1;; \
	esac
	@if rg -n \
		'#include "old_school/(fq4_dev_generator|fq4_d1_field_gate|fq4_d1_treatment|fq4_priority_fit)\.hpp"|GameState|\\bGame\\s*[({]|score_production|determinize_|rollout_game' \
		include/old_school/fq4_dev_background_diagnostic.hpp \
		src/fq4_dev_background_diagnostic.cpp \
		src/fq4_dev_background_diagnostic_main.cpp >/dev/null; then \
		printf 'FQ4-DEV2 background diagnostic source crossed a game/generator/held-out firewall\n' >&2; \
		exit 1; \
	fi
	@if rg -n 'error=|\.what\(\)' \
		src/fq4_dev_background_diagnostic_main.cpp >/dev/null; then \
		printf 'FQ4-DEV2 background diagnostic CLI can disclose exception details\n' >&2; \
		exit 1; \
	fi

fq4-dev-background-diagnostic: $(FQ4_DEV_BACKGROUND_DIAGNOSTIC)
	./$(FQ4_DEV_BACKGROUND_DIAGNOSTIC)

test-fq4-dev-coverage-census: $(FQ4_DEV_COVERAGE_CENSUS_TEST_RUNNER) $(FQ4_DEV_COVERAGE_CENSUS)
	./$(FQ4_DEV_COVERAGE_CENSUS_TEST_RUNNER)
	@set +e; output=`./$(FQ4_DEV_COVERAGE_CENSUS) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ4-DEV4 coverage census accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null
	@if rg -n 'error=|\.what\(\)' \
		src/fq4_dev_coverage_census_main.cpp >/dev/null; then \
		printf 'FQ4-DEV4 production CLI can disclose exception details\n' >&2; \
		exit 1; \
	fi

fq4-dev-coverage-census: $(FQ4_DEV_COVERAGE_CENSUS)
	./$(FQ4_DEV_COVERAGE_CENSUS)

.PHONY: test-fq4-neutral-supplement test-fq4-neutral-publisher fq4-neutral-publish test-fq4-neutral-evaluator fq4-neutral-evaluate
test: $(FQ4_NEUTRAL_SUPPLEMENT_TEST_RUNNER) $(FQ4_NEUTRAL_PUBLISHER_TEST_RUNNER) $(FQ4_NEUTRAL_PUBLISHER) $(FQ4_NEUTRAL_EVALUATOR_TEST_RUNNER) $(FQ4_NEUTRAL_EVALUATOR_RUNNER_TEST_RUNNER) $(FQ4_NEUTRAL_EVALUATOR)

test-fq4-neutral-supplement: $(FQ4_NEUTRAL_SUPPLEMENT_TEST_RUNNER)
	./$(FQ4_NEUTRAL_SUPPLEMENT_TEST_RUNNER)

test-fq4-neutral-publisher: $(FQ4_NEUTRAL_PUBLISHER_TEST_RUNNER) $(FQ4_NEUTRAL_PUBLISHER)
	./$(FQ4_NEUTRAL_PUBLISHER_TEST_RUNNER)
	@set +e; output=`./$(FQ4_NEUTRAL_PUBLISHER) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ4-DEV5 neutral publisher accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage: old-school-fq4-dev5-neutral-publish' >/dev/null
	@if ! /usr/bin/strings -a "$(FQ4_NEUTRAL_PUBLISHER)" | \
		grep -Fx "$(FQ4_DEV_PRODUCER_COMMIT)" >/dev/null; then \
		printf 'FQ4-DEV5 neutral publisher lost its exact producer commit\n' >&2; \
		exit 1; \
	fi
	@if rg -n 'error=|\.what\(\)' \
		src/fq4_neutral_publisher_main.cpp >/dev/null; then \
		printf 'FQ4-DEV5 neutral publisher CLI can disclose exception details\n' >&2; \
		exit 1; \
	fi
	@case " $(FQ4_NEUTRAL_PUBLISHER_LINK_SOURCES) " in \
		*" src/fq4_d1_field_gate.cpp "*|\
		*" src/fq4_d1_treatment.cpp "*|\
		*" src/fq4_d1_treatment_production.cpp "*|\
		*" src/fq4_priority_fit.cpp "*|\
		*" src/fq4_d1_census_main.cpp "*) \
			printf 'FQ4-DEV5 neutral publisher crossed a held-out firewall\n' >&2; \
			exit 1;; \
	esac

fq4-neutral-publish: $(FQ4_NEUTRAL_PUBLISHER)
	./$(FQ4_NEUTRAL_PUBLISHER)

test-fq4-neutral-evaluator: $(FQ4_NEUTRAL_EVALUATOR_TEST_RUNNER) $(FQ4_NEUTRAL_EVALUATOR_RUNNER_TEST_RUNNER) $(FQ4_NEUTRAL_EVALUATOR)
	./$(FQ4_NEUTRAL_EVALUATOR_TEST_RUNNER)
	./$(FQ4_NEUTRAL_EVALUATOR_RUNNER_TEST_RUNNER)
	@set +e; output=`./$(FQ4_NEUTRAL_EVALUATOR) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ4-DEV5 neutral evaluator accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage: old-school-fq4-neutral-evaluate' >/dev/null
	@if rg -n 'error=|\.what\(\)' \
		src/fq4_neutral_evaluator_main.cpp >/dev/null; then \
		printf 'FQ4-DEV5 evaluator CLI can disclose exception details\n' >&2; \
		exit 1; \
	fi
	@case " $(FQ4_NEUTRAL_EVALUATOR_RUNNER_LINK_SOURCES) " in \
		*" src/fq4_dev_generator.cpp "*|\
		*" src/fq4_dev_coverage_census.cpp "*|\
		*" src/fq4_neutral_publisher.cpp "*|\
		*" src/fq4_priority_collection.cpp "*|\
		*" src/fq4_dev1_gameplay.cpp "*|\
		*" src/fq4_d1_field_gate.cpp "*|\
		*" src/fq4_d1_treatment.cpp "*|\
		*" src/fq4_d1_treatment_production.cpp "*) \
			printf 'FQ4-DEV5 evaluator crossed a source/held-out firewall\n' >&2; \
			exit 1;; \
	esac

fq4-neutral-evaluate: $(FQ4_NEUTRAL_EVALUATOR)
	./$(FQ4_NEUTRAL_EVALUATOR)

test-fq4-neutral-candidate-publisher: $(FQ4_NEUTRAL_CANDIDATE_PUBLISHER_TEST_RUNNER) $(FQ4_NEUTRAL_CANDIDATE_PUBLISHER)
	./$(FQ4_NEUTRAL_CANDIDATE_PUBLISHER_TEST_RUNNER)
	@set +e; output=`./$(FQ4_NEUTRAL_CANDIDATE_PUBLISHER) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ4-DEV5 anchored candidate publisher accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage: old-school-fq4-dev5-candidate-publish' >/dev/null
	@if ! /usr/bin/strings -a "$(FQ4_NEUTRAL_CANDIDATE_PUBLISHER)" | \
		grep -Fx "$(FQ4_DEV_PRODUCER_COMMIT)" >/dev/null; then \
		printf 'FQ4-DEV5 anchored candidate publisher lost its exact producer commit\n' >&2; \
		exit 1; \
	fi
	@if rg -n 'error=|\.what\(\)' \
		src/fq4_neutral_candidate_publisher_main.cpp >/dev/null; then \
		printf 'FQ4-DEV5 anchored candidate publisher CLI can disclose exception details\n' >&2; \
		exit 1; \
	fi
	@case " $(FQ4_NEUTRAL_CANDIDATE_PUBLISHER_LINK_SOURCES) " in \
		*" src/fq4_priority_collection.cpp "*|\
		*" src/fq4_dev_generator.cpp "*|\
		*" src/fq4_dev_coverage_census.cpp "*|\
		*" src/fq4_neutral_publisher.cpp "*|\
		*" src/fq4_neutral_evaluator_runner.cpp "*|\
		*" src/fq4_dev_candidate_publisher.cpp "*|\
		*" src/fq4_dev1_gameplay.cpp "*|\
		*" src/fq4_priority_fit.cpp "*|\
		*" src/fq4_d1_field_gate.cpp "*|\
		*" src/fq4_d1_treatment.cpp "*|\
		*" src/fq4_d1_treatment_production.cpp "*) \
			printf 'FQ4-DEV5 anchored candidate publisher crossed a source/gameplay/held-out firewall\n' >&2; \
			exit 1;; \
	esac
	@if nm -gU "$(FQ4_NEUTRAL_CANDIDATE_PUBLISHER)" | c++filt | \
		rg -q 'build_canonical_root|evaluate_robust_dominance|make_hidden_clone|replay_exact|select_development_rows|run_fq4_dev1_gameplay'; then \
		printf 'FQ4-DEV5 anchored candidate publisher contains forbidden source-game symbols\n' >&2; \
		exit 1; \
	fi

fq4-neutral-candidate-publish: $(FQ4_NEUTRAL_CANDIDATE_PUBLISHER)
	./$(FQ4_NEUTRAL_CANDIDATE_PUBLISHER)

test-fq4-priority-fit: $(FQ4_PRIORITY_FIT_TEST_RUNNER) $(FQ4_PRIORITY_FIT)
	./$(FQ4_PRIORITY_FIT_TEST_RUNNER)
	@set +e; output=`./$(FQ4_PRIORITY_FIT) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ4-D0 CLI accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null

fq4-priority-fit: $(FQ4_PRIORITY_FIT)
	./$(FQ4_PRIORITY_FIT)

test-fq4-priority-fit-d0b: $(FQ4_PRIORITY_FIT_TEST_RUNNER) $(FQ4_PRIORITY_FIT_D0B)
	./$(FQ4_PRIORITY_FIT_TEST_RUNNER)
	@set +e; output=`./$(FQ4_PRIORITY_FIT_D0B) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ4-D0b CLI accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null

fq4-priority-fit-d0b: $(FQ4_PRIORITY_FIT_D0B)
	./$(FQ4_PRIORITY_FIT_D0B)

test-fq4-d1-field-gate: $(FQ4_D1_FIELD_GATE_TEST_RUNNER) $(FQ4_D1_CENSUS)
	./$(FQ4_D1_FIELD_GATE_TEST_RUNNER)
	@set +e; output=`./$(FQ4_D1_CENSUS) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ4-D1-P0 CLI accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null
	@test -z "$(filter $(FQ4_PRIORITY_FIT_SOURCE) $(FQ4_D1_TREATMENT_SOURCE) $(FQ4_D1_TREATMENT_PRODUCTION_SOURCE),$(FQ4_D1_FIELD_GATE_LINK_SOURCES))" || { \
		printf 'FQ4-D1-P0 link graph contains treatment source\n' >&2; \
		exit 1; \
	}
	@if nm "$(FQ4_D1_CENSUS)" | rg -q 'fq4_priority_fit|fq4_d1_treatment'; then \
		printf 'FQ4-D1-P0 binary contains treatment symbols\n' >&2; \
		exit 1; \
	fi
	@if strings "$(FQ4_D1_CENSUS)" | rg -q '81ad05d2c32bea9b17ca4c89cbbf7a9be105ad130897f79fa4d8a29a5ea1105e'; then \
		printf 'FQ4-D1-P0 binary contains treatment fingerprint\n' >&2; \
		exit 1; \
	fi

fq4-d1-census: $(FQ4_D1_CENSUS)
	./$(FQ4_D1_CENSUS)

test-fq4-d1-treatment: $(FQ4_D1_TREATMENT_TEST_RUNNER) $(FQ4_D1_TREATMENT) $(FQ4_D1_CENSUS)
	./$(FQ4_D1_TREATMENT_TEST_RUNNER)
	@set +e; output=`./$(FQ4_D1_TREATMENT) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ4-D1-T0 CLI accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null
	@test -z "$(filter $(FQ4_PRIORITY_FIT_SOURCE) $(FQ4_D1_TREATMENT_SOURCE) $(FQ4_D1_TREATMENT_PRODUCTION_SOURCE),$(FQ4_D1_FIELD_GATE_LINK_SOURCES))" || { \
		printf 'FQ4-D1-P0 link graph contains treatment source\n' >&2; \
		exit 1; \
	}
	@if nm "$(FQ4_D1_CENSUS)" | rg -q 'fq4_priority_fit|fq4_d1_treatment'; then \
		printf 'FQ4-D1-P0 binary contains treatment symbols\n' >&2; \
		exit 1; \
	fi
	@if strings "$(FQ4_D1_CENSUS)" | rg -q '81ad05d2c32bea9b17ca4c89cbbf7a9be105ad130897f79fa4d8a29a5ea1105e'; then \
		printf 'FQ4-D1-P0 binary contains treatment fingerprint\n' >&2; \
		exit 1; \
	fi
	@if nm "$(call source_objects,$(FQ4_D1_TREATMENT_SOURCE))" | rg -q 'score_production'; then \
		printf 'FQ4-D1 tensor evaluator directly references production search\n' >&2; \
		exit 1; \
	fi
	@if nm -u "$(call source_objects,$(FQ4_D1_TREATMENT_SOURCE))" | rg -q 'run_parent_census|fit_d0b_production|score_production|sample_determinization|physical_game_id'; then \
		printf 'FQ4-D1 tensor evaluator directly references census, fit, search, or source helpers\n' >&2; \
		exit 1; \
	fi
	@if rg -n 'run_parent_census|fit_d0b_production|score_production|sample_determinization|field::physical_game_id' "$(FQ4_D1_TREATMENT_SOURCE)" >/dev/null; then \
		printf 'FQ4-D1 tensor evaluator source contains census, fit, search, or source helpers\n' >&2; \
		exit 1; \
	fi

fq4-d1-treatment: $(FQ4_D1_TREATMENT)
	./$(FQ4_D1_TREATMENT)

test-fq4-dev-schedule: $(FQ4_DEV_SCHEDULE_TEST_RUNNER) $(FQ4_DEV_SCHEDULE)
	./$(FQ4_DEV_SCHEDULE_TEST_RUNNER)
	@set +e; output=`./$(FQ4_DEV_SCHEDULE) unexpected 2>&1`; status=$$?; set -e; \
	if [ $$status -ne 2 ]; then \
		printf '%s\n' "$$output"; \
		printf 'FQ4-DEV1 schedule CLI accepted an argument\n' >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null

fq4-dev-schedule: $(FQ4_DEV_SCHEDULE)
	./$(FQ4_DEV_SCHEDULE)

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
