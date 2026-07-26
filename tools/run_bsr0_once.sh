#!/bin/sh

set -eu

if [ "$#" -ne 1 ]; then
    printf 'usage: sh tools/run_bsr0_once.sh ABSOLUTE_OUTPUT_PREFIX\n' >&2
    exit 2
fi

bsr0_output_prefix=$1
case $bsr0_output_prefix in
    /*) ;;
    *)
        printf 'BSR0 output prefix must be absolute\n' >&2
        exit 2
        ;;
esac

bsr0_script_directory=$(
    CDPATH= cd -- "$(dirname -- "$0")" && pwd
)
bsr0_project_directory=$(dirname -- "$bsr0_script_directory")
cd "$bsr0_project_directory"

exec sh "$bsr0_script_directory/capture_once.sh" \
    "$bsr0_output_prefix" \
    ./build/old-school-sim --audit-v3-blue-stack-regret \
    --train-games 800 --train-seed 424242 \
    --learned-generations 16
