#!/bin/sh

set -eu

capture_script=${1:-tools/capture_once.sh}
case $capture_script in
    /*) ;;
    *) capture_script=$(pwd)/${capture_script#./} ;;
esac
sh -n "$capture_script"
capture_tool_directory=$(dirname -- "$capture_script")
bsr0_script=$capture_tool_directory/run_bsr0_once.sh
sh -n "$bsr0_script"
for bsr0_exact_fragment in \
    './build/old-school-sim --audit-v3-blue-stack-regret' \
    '--train-games 800 --train-seed 424242' \
    '--learned-generations 16'
do
    if ! grep -F -- "$bsr0_exact_fragment" "$bsr0_script" \
        >/dev/null; then
        printf 'BSR0 wrapper lost exact command fragment: %s\n' \
            "$bsr0_exact_fragment" >&2
        exit 1
    fi
done

set +e
bsr0_usage_output=$(sh "$bsr0_script" 2>&1)
bsr0_usage_status=$?
set -e
if [ "$bsr0_usage_status" -ne 2 ]; then
    printf 'BSR0 wrapper did not reject missing capture prefix\n%s\n' \
        "$bsr0_usage_output" >&2
    exit 1
fi

capture_test_directory=$(
    mktemp -d "${TMPDIR:-/tmp}/old-school-capture-test.XXXXXX"
)
cleanup_capture_test() {
    rm -rf "$capture_test_directory"
}
trap cleanup_capture_test EXIT HUP INT TERM
capture_test_prefix=$capture_test_directory/run

set +e
capture_test_output=$(
    sh "$capture_script" "$capture_test_prefix" \
        /bin/sh -c \
        'printf "capture-stdout\n"; printf "capture-stderr\n" >&2; exit 7' \
        2>&1
)
capture_test_status=$?
set -e
if [ "$capture_test_status" -ne 7 ]; then
    printf 'capture helper lost process exit 7 (got %s)\n%s\n' \
        "$capture_test_status" "$capture_test_output" >&2
    exit 1
fi

capture_test_complete=${capture_test_prefix}.complete.txt
capture_test_exit=${capture_test_prefix}.exit.txt
capture_test_sha=${capture_test_prefix}.sha256.txt
if [ "$(cat "$capture_test_exit")" != 7 ]; then
    printf 'capture helper exit artifact is incorrect\n' >&2
    exit 1
fi
case $(cat "$capture_test_complete") in
    *"capture-stdout"*"capture-stderr"*"real "*"user "*"sys "*) ;;
    *)
        printf 'complete capture omitted stdout, stderr, or timing\n' >&2
        exit 1
        ;;
esac

if command -v shasum >/dev/null 2>&1; then
    capture_test_expected_sha=$(
        shasum -a 256 "$capture_test_complete" |
            awk '{print $1}'
    )
else
    capture_test_expected_sha=$(
        sha256sum "$capture_test_complete" |
            awk '{print $1}'
    )
fi
capture_test_recorded_sha=$(awk '{print $1}' "$capture_test_sha")
if [ "$capture_test_recorded_sha" != "$capture_test_expected_sha" ]; then
    printf 'complete-capture SHA-256 is incorrect\n' >&2
    exit 1
fi
case $capture_test_output in
    *"Complete-capture SHA-256: $capture_test_expected_sha"*\
"Process exit: 7"*) ;;
    *)
        printf 'capture helper did not report digest and exit\n%s\n' \
            "$capture_test_output" >&2
        exit 1
        ;;
esac

set +e
capture_test_repeat=$(
    sh "$capture_script" "$capture_test_prefix" \
        /bin/sh -c 'exit 0' 2>&1
)
capture_test_repeat_status=$?
set -e
if [ "$capture_test_repeat_status" -ne 2 ]; then
    printf 'capture helper did not reject an overwrite\n%s\n' \
        "$capture_test_repeat" >&2
    exit 1
fi

printf 'capture-once tests passed\n'
