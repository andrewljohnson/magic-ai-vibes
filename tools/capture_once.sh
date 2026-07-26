#!/bin/sh

set -eu

if [ "$#" -lt 2 ]; then
    printf 'usage: sh tools/capture_once.sh ABSOLUTE_OUTPUT_PREFIX COMMAND [ARG ...]\n' >&2
    exit 2
fi

capture_once_prefix=$1
shift
case $capture_once_prefix in
    /*) ;;
    *)
        printf 'capture output prefix must be absolute\n' >&2
        exit 2
        ;;
esac

capture_once_complete=${capture_once_prefix}.complete.txt
capture_once_exit=${capture_once_prefix}.exit.txt
capture_once_sha=${capture_once_prefix}.sha256.txt
for capture_once_target in \
    "$capture_once_complete" \
    "$capture_once_exit" \
    "$capture_once_sha"
do
    if [ -e "$capture_once_target" ]; then
        printf 'refusing to overwrite one-shot capture artifact: %s\n' \
            "$capture_once_target" >&2
        exit 2
    fi
done

set +e
/usr/bin/time -p "$@" >"$capture_once_complete" 2>&1
capture_once_process_exit=$?
set -e
printf '%s\n' "$capture_once_process_exit" >"$capture_once_exit"

if command -v shasum >/dev/null 2>&1; then
    capture_once_complete_sha=$(
        shasum -a 256 "$capture_once_complete" |
            awk '{print $1}'
    )
elif command -v sha256sum >/dev/null 2>&1; then
    capture_once_complete_sha=$(
        sha256sum "$capture_once_complete" |
            awk '{print $1}'
    )
else
    printf 'no SHA-256 utility available\n' >&2
    exit 2
fi
printf '%s  %s\n' \
    "$capture_once_complete_sha" "$capture_once_complete" \
    >"$capture_once_sha"

cat "$capture_once_complete"
printf 'Complete-capture file: %s\n' \
    "$capture_once_complete"
printf 'Complete-capture SHA-256: %s\n' \
    "$capture_once_complete_sha"
printf 'Process exit: %s\n' "$capture_once_process_exit"

exit "$capture_once_process_exit"
