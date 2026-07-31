CXX ?= clang++
CXXFLAGS ?= -std=c++20 -O3 -Wall -Wextra -Wpedantic -Werror
CPPFLAGS ?= -Iinclude

BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj

ENGINE_SOURCE := src/game.cpp
SELFPLAY_ZERO_SOURCE := src/selfplay_zero.cpp
WEB_BRIDGE_SOURCE := src/web_bridge.cpp src/artifact_integrity.cpp

SELFPLAY_ZERO := $(BUILD_DIR)/selfplay-zero
AUDIT_CHAMPION := $(BUILD_DIR)/audit-champion
MATCHUP_MATRIX := $(BUILD_DIR)/matchup-matrix
WEB_BRIDGE := $(BUILD_DIR)/old-school-web-bridge
GAME_TESTS := $(BUILD_DIR)/old-school-tests
SELFPLAY_ZERO_TESTS := $(BUILD_DIR)/test-selfplay-zero
WEB_BRIDGE_TESTS := $(BUILD_DIR)/old-school-web-bridge-tests
ARTIFACT_INTEGRITY_TESTS := $(BUILD_DIR)/old-school-artifact-integrity-tests
WEB_DEPENDENCIES := web/node_modules/.package-lock.json

source_objects = $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(1))

all: $(SELFPLAY_ZERO) $(WEB_BRIDGE)

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p "$(@D)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c "$<" -o "$@"

define link_program
$(1): $(call source_objects,$(2))
	@mkdir -p $(BUILD_DIR)
	$$(CXX) $$(CPPFLAGS) $$(CXXFLAGS) $(call source_objects,$(2)) -o $$@
endef

$(eval $(call link_program,$(SELFPLAY_ZERO),$(ENGINE_SOURCE) $(SELFPLAY_ZERO_SOURCE) src/selfplay_zero_main.cpp))
$(eval $(call link_program,$(AUDIT_CHAMPION),$(ENGINE_SOURCE) $(SELFPLAY_ZERO_SOURCE) src/audit_champion_main.cpp))
$(eval $(call link_program,$(MATCHUP_MATRIX),$(ENGINE_SOURCE) $(SELFPLAY_ZERO_SOURCE) src/matchup_matrix_main.cpp))
$(eval $(call link_program,$(WEB_BRIDGE),$(ENGINE_SOURCE) $(SELFPLAY_ZERO_SOURCE) $(WEB_BRIDGE_SOURCE) src/web_bridge_main.cpp))
$(eval $(call link_program,$(GAME_TESTS),$(ENGINE_SOURCE) src/interactive.cpp tests/test_game.cpp))
$(eval $(call link_program,$(SELFPLAY_ZERO_TESTS),$(ENGINE_SOURCE) $(SELFPLAY_ZERO_SOURCE) tests/test_selfplay_zero.cpp))
$(eval $(call link_program,$(WEB_BRIDGE_TESTS),$(ENGINE_SOURCE) $(SELFPLAY_ZERO_SOURCE) $(WEB_BRIDGE_SOURCE) tests/test_web_bridge.cpp))
$(eval $(call link_program,$(ARTIFACT_INTEGRITY_TESTS),src/artifact_integrity.cpp tests/test_artifact_integrity.cpp))

$(WEB_DEPENDENCIES): web/package.json web/package-lock.json
	npm --prefix web ci
	@touch $(WEB_DEPENDENCIES)

.PHONY: all test test-selfplay-zero test-web test-web-ui test-web-rendered selfplay-zero audit-champion matchup-matrix web-target-stack web-interaction web-journey web-delayed-journey web-build web clean

audit-champion: $(AUDIT_CHAMPION)
	./$(AUDIT_CHAMPION) --self-test
	./$(AUDIT_CHAMPION) 16

selfplay-zero: $(SELFPLAY_ZERO)

matchup-matrix: $(MATCHUP_MATRIX)

test: $(GAME_TESTS) $(SELFPLAY_ZERO_TESTS) $(WEB_BRIDGE_TESTS) $(ARTIFACT_INTEGRITY_TESTS) $(WEB_BRIDGE) $(WEB_DEPENDENCIES)
	./$(GAME_TESTS)
	./$(SELFPLAY_ZERO_TESTS)
	./$(WEB_BRIDGE_TESTS)
	./$(ARTIFACT_INTEGRITY_TESTS)
	npm --prefix web test

test-selfplay-zero: $(SELFPLAY_ZERO_TESTS)
	./$(SELFPLAY_ZERO_TESTS)

test-web: $(WEB_BRIDGE_TESTS) $(WEB_BRIDGE) $(WEB_DEPENDENCIES)
	./$(WEB_BRIDGE_TESTS)
	npm --prefix web test

test-web-ui: $(WEB_DEPENDENCIES)
	npm --prefix web run test:ui

test-web-rendered: $(WEB_BRIDGE) $(WEB_DEPENDENCIES)
	npm --prefix web run test:rendered:target-stack

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

web-build: $(WEB_BRIDGE) $(WEB_DEPENDENCIES)
	npm --prefix web run build

web: web-build
	npm --prefix web start

clean:
	@if [ -d "$(BUILD_DIR)" ]; then \
		find "$(BUILD_DIR)" -mindepth 1 -maxdepth 1 \
			! -name model-cache -exec rm -rf -- {} +; \
	fi

-include $(shell find $(OBJ_DIR) -name '*.d' 2>/dev/null)
