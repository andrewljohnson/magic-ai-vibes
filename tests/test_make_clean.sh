#!/bin/sh
set -eu

clean_plan=$(make --no-print-directory -n clean)

case $clean_plan in
    *'rm -rf build'*)
        printf 'make clean still removes the complete build tree\n' >&2
        exit 1
        ;;
esac

case $clean_plan in
    *'find "build" -mindepth 1 -maxdepth 1'*\
*'! -name model-cache'*)
        ;;
    *)
        printf 'make clean does not explicitly preserve model-cache\n' >&2
        printf '%s\n' "$clean_plan" >&2
        exit 1
        ;;
esac

printf 'make clean preserves frozen model artifacts\n'
