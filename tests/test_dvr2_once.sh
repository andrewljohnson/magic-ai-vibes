#!/bin/sh

set -eu

dvr2_script=${1:-tools/run_dvr2_once.sh}
case $dvr2_script in
    /*) ;;
    *) dvr2_script=$(pwd)/${dvr2_script#./} ;;
esac

sh -n "$dvr2_script"
if ! grep -F -- \
    'exec ./build/old-school-dvr2-harvest' \
    "$dvr2_script" >/dev/null ||
    ! grep -F -- '--output "$dvr2_output_path"' \
        "$dvr2_script" >/dev/null; then
    printf 'DVR2 wrapper lost its exact fixed command\n' >&2
    exit 1
fi

set +e
dvr2_missing_output=$(sh "$dvr2_script" 2>&1)
dvr2_missing_status=$?
dvr2_relative_output=$(
    sh "$dvr2_script" relative.dvr2 2>&1
)
dvr2_relative_status=$?
set -e
if [ "$dvr2_missing_status" -ne 2 ] ||
    [ "$dvr2_relative_status" -ne 2 ]; then
    printf 'DVR2 wrapper accepted a missing/relative output path\n%s\n%s\n' \
        "$dvr2_missing_output" "$dvr2_relative_output" >&2
    exit 1
fi

dvr2_test_directory=$(
    mktemp -d "${TMPDIR:-/tmp}/old-school-dvr2-once.XXXXXX"
)
cleanup_dvr2_once() {
    rm -rf "$dvr2_test_directory"
}
trap cleanup_dvr2_once EXIT HUP INT TERM

dvr2_existing=$dvr2_test_directory/existing.dvr2
: >"$dvr2_existing"
set +e
dvr2_existing_output=$(
    sh "$dvr2_script" "$dvr2_existing" 2>&1
)
dvr2_existing_status=$?
set -e
if [ "$dvr2_existing_status" -ne 2 ]; then
    printf 'DVR2 wrapper accepted an existing output path\n%s\n' \
        "$dvr2_existing_output" >&2
    exit 1
fi

dvr2_symlink=$dvr2_test_directory/symlink.dvr2
ln -s "$dvr2_test_directory/missing-target" "$dvr2_symlink"
set +e
dvr2_symlink_output=$(
    sh "$dvr2_script" "$dvr2_symlink" 2>&1
)
dvr2_symlink_status=$?
set -e
if [ "$dvr2_symlink_status" -ne 2 ]; then
    printf 'DVR2 wrapper accepted a dangling output symlink\n%s\n' \
        "$dvr2_symlink_output" >&2
    exit 1
fi

printf 'DVR2 one-shot wrapper tests passed\n'
