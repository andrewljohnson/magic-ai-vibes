#!/bin/sh

set -eu

script_directory=$(CDPATH= cd -P -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -P -- "$script_directory/.." && pwd)
cd "$project_root"

test_directory=$(
    mktemp -d "${TMPDIR:-/tmp}/old-school-make-incremental.XXXXXX"
)
cleanup_make_incremental() {
    rm -rf "$test_directory"
}
trap cleanup_make_incremental EXIT HUP INT TERM

build_directory=$test_directory/build
probe_eval_target=$build_directory/old-school-probe-eval-tests
action_eval_target=$build_directory/old-school-oc1-action-eval-tests

base_cxx=${CXX:-c++}
base_cppflags=-Iinclude
base_cxxflags='-std=c++20 -O0 -Wall -Wextra -Wpedantic -Werror'

run_build() {
    output_path=$1
    build_cxx=$2
    build_cppflags=$3
    build_cxxflags=$4
    shift 4
    MAKEFLAGS= MFLAGS= make --no-print-directory -j4 \
        "BUILD_DIR=$build_directory" \
        "CXX=$build_cxx" \
        "CPPFLAGS=$build_cppflags" \
        "CXXFLAGS=$build_cxxflags" \
        "$@" "$probe_eval_target" "$action_eval_target" \
        >"$output_path" 2>&1
}

run_one() {
    output_path=$1
    build_cxx=$2
    build_cppflags=$3
    build_cxxflags=$4
    build_target=$5
    MAKEFLAGS= MFLAGS= make --no-print-directory -j4 \
        "BUILD_DIR=$build_directory" \
        "CXX=$build_cxx" \
        "CPPFLAGS=$build_cppflags" \
        "CXXFLAGS=$build_cxxflags" \
        "$build_target" >"$output_path" 2>&1
}

count_fixed() {
    pattern=$1
    input_path=$2
    grep -F -c -- "$pattern" "$input_path" 2>/dev/null || true
}

require_count() {
    expected=$1
    pattern=$2
    input_path=$3
    actual=$(count_fixed "$pattern" "$input_path")
    if [ "$actual" -ne "$expected" ]; then
        printf 'expected %s occurrences of %s, found %s in %s\n' \
            "$expected" "$pattern" "$actual" "$input_path" >&2
        cat "$input_path" >&2
        exit 1
    fi
}

initial_log=$test_directory/initial.log
run_build "$initial_log" "$base_cxx" "$base_cppflags" "$base_cxxflags"
for source in \
    src/probe_eval.cpp \
    tests/test_probe_eval.cpp \
    src/oc1_action_eval.cpp \
    tests/test_oc1_action_eval.cpp
do
    require_count 1 "-c \"$source\"" "$initial_log"
done
require_count 1 "-o $probe_eval_target" "$initial_log"
require_count 1 "-o $action_eval_target" "$initial_log"

noop_log=$test_directory/noop.log
run_build "$noop_log" "$base_cxx" "$base_cppflags" "$base_cxxflags"
require_count 0 ' -c "' "$noop_log"
require_count 0 "-o $probe_eval_target" "$noop_log"
require_count 0 "-o $action_eval_target" "$noop_log"

# Changing one program's ordered link-source list in the Makefile must relink
# that program even when the source set, objects, and compiler configuration
# are unchanged. It must neither rebuild shared objects nor disturb a sibling.
link_order_log=$test_directory/link-order.log
reordered_action_sources='src/oc1_action_eval.cpp src/probe_eval.cpp'
run_build \
    "$link_order_log" "$base_cxx" "$base_cppflags" "$base_cxxflags" \
    "OC1_ACTION_EVAL_LINK_SOURCES=$reordered_action_sources"
require_count 0 ' -c "' "$link_order_log"
require_count 0 "-o $probe_eval_target" "$link_order_log"
require_count 1 "-o $action_eval_target" "$link_order_log"

link_order_noop_log=$test_directory/link-order-noop.log
run_build \
    "$link_order_noop_log" "$base_cxx" "$base_cppflags" "$base_cxxflags" \
    "OC1_ACTION_EVAL_LINK_SOURCES=$reordered_action_sources"
require_count 0 ' -c "' "$link_order_noop_log"
require_count 0 "-o $probe_eval_target" "$link_order_noop_log"
require_count 0 "-o $action_eval_target" "$link_order_noop_log"

link_order_restore_log=$test_directory/link-order-restore.log
run_build \
    "$link_order_restore_log" "$base_cxx" "$base_cppflags" "$base_cxxflags"
require_count 0 ' -c "' "$link_order_restore_log"
require_count 0 "-o $probe_eval_target" "$link_order_restore_log"
require_count 1 "-o $action_eval_target" "$link_order_restore_log"

cxxflags_log=$test_directory/cxxflags.log
changed_cxxflags="$base_cxxflags -DOLD_SCHOOL_CXXFLAGS_CONFIG=1"
run_build \
    "$cxxflags_log" "$base_cxx" "$base_cppflags" "$changed_cxxflags"
for source in \
    src/probe_eval.cpp \
    tests/test_probe_eval.cpp \
    src/oc1_action_eval.cpp \
    tests/test_oc1_action_eval.cpp
do
    require_count 1 "-c \"$source\"" "$cxxflags_log"
done
require_count 1 "-o $probe_eval_target" "$cxxflags_log"
require_count 1 "-o $action_eval_target" "$cxxflags_log"

cppflags_log=$test_directory/cppflags.log
changed_cppflags="$base_cppflags -DOLD_SCHOOL_CPPFLAGS_CONFIG=1"
run_build \
    "$cppflags_log" "$base_cxx" "$changed_cppflags" "$changed_cxxflags"
for source in \
    src/probe_eval.cpp \
    tests/test_probe_eval.cpp \
    src/oc1_action_eval.cpp \
    tests/test_oc1_action_eval.cpp
do
    require_count 1 "-c \"$source\"" "$cppflags_log"
done
require_count 1 "-o $probe_eval_target" "$cppflags_log"
require_count 1 "-o $action_eval_target" "$cppflags_log"

cxx_log=$test_directory/cxx.log
changed_cxx="$base_cxx -DOLD_SCHOOL_CXX_CONFIG=1"
run_build \
    "$cxx_log" "$changed_cxx" "$changed_cppflags" "$changed_cxxflags"
for source in \
    src/probe_eval.cpp \
    tests/test_probe_eval.cpp \
    src/oc1_action_eval.cpp \
    tests/test_oc1_action_eval.cpp
do
    require_count 1 "-c \"$source\"" "$cxx_log"
done
require_count 1 "-o $probe_eval_target" "$cxx_log"
require_count 1 "-o $action_eval_target" "$cxx_log"

# Returning to the first configuration reuses its objects but must relink the
# stable program paths, which currently contain the most recent configuration.
switch_back_log=$test_directory/switch-back.log
run_build \
    "$switch_back_log" "$base_cxx" "$base_cppflags" "$base_cxxflags"
require_count 0 ' -c "' "$switch_back_log"
require_count 1 "-o $probe_eval_target" "$switch_back_log"
require_count 1 "-o $action_eval_target" "$switch_back_log"

# Per-program sidecars must not let one target's configuration claim make a
# differently configured sibling look current. Cache the changed objects for
# both, switch only action-eval back to the base configuration, select the
# already-current changed probe-eval target, then require action-eval to relink
# when it returns to the changed configuration.
stagger_changed_log=$test_directory/stagger-changed.log
run_build \
    "$stagger_changed_log" "$base_cxx" "$base_cppflags" "$changed_cxxflags"
require_count 0 ' -c "' "$stagger_changed_log"
require_count 1 "-o $probe_eval_target" "$stagger_changed_log"
require_count 1 "-o $action_eval_target" "$stagger_changed_log"

stagger_action_base_log=$test_directory/stagger-action-base.log
run_one \
    "$stagger_action_base_log" "$base_cxx" "$base_cppflags" \
    "$base_cxxflags" "$action_eval_target"
require_count 0 ' -c "' "$stagger_action_base_log"
require_count 0 "-o $probe_eval_target" "$stagger_action_base_log"
require_count 1 "-o $action_eval_target" "$stagger_action_base_log"

stagger_probe_changed_log=$test_directory/stagger-probe-changed.log
run_one \
    "$stagger_probe_changed_log" "$base_cxx" "$base_cppflags" \
    "$changed_cxxflags" "$probe_eval_target"
require_count 0 ' -c "' "$stagger_probe_changed_log"
require_count 0 "-o $probe_eval_target" "$stagger_probe_changed_log"
require_count 0 "-o $action_eval_target" "$stagger_probe_changed_log"

stagger_action_changed_log=$test_directory/stagger-action-changed.log
run_one \
    "$stagger_action_changed_log" "$base_cxx" "$base_cppflags" \
    "$changed_cxxflags" "$action_eval_target"
require_count 0 ' -c "' "$stagger_action_changed_log"
require_count 0 "-o $probe_eval_target" "$stagger_action_changed_log"
require_count 1 "-o $action_eval_target" "$stagger_action_changed_log"

# Compiler depfiles must still rebuild a shared source exactly once and relink
# both parallel consumers when one of its headers changes.
# GNU Make 3.81 compares these freshly written files at one-second resolution,
# so cross a timestamp tick before using -W's non-mutating header simulation.
sleep 1
header_log=$test_directory/header.log
run_build \
    "$header_log" "$base_cxx" "$base_cppflags" "$base_cxxflags" \
    -W include/old_school/probe_eval.hpp
require_count 1 '-c "src/probe_eval.cpp"' "$header_log"
require_count 1 "-o $probe_eval_target" "$header_log"
require_count 1 "-o $action_eval_target" "$header_log"

printf 'shared-object incremental build tests passed\n'
