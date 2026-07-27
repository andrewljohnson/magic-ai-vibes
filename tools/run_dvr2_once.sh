#!/bin/sh

set -eu

if [ "$#" -ne 1 ]; then
    printf 'usage: sh tools/run_dvr2_once.sh ABSOLUTE_NEW_OUTPUT_PATH\n' >&2
    exit 2
fi

dvr2_output_path=$1
case $dvr2_output_path in
    /*) ;;
    *)
        printf 'DVR2 output path must be absolute\n' >&2
        exit 2
        ;;
esac

if [ -e "$dvr2_output_path" ] || [ -L "$dvr2_output_path" ]; then
    printf 'refusing to overwrite DVR2 evidence path: %s\n' \
        "$dvr2_output_path" >&2
    exit 2
fi

dvr2_script_directory=$(
    CDPATH= cd -- "$(dirname -- "$0")" && pwd
)
dvr2_project_directory=$(dirname -- "$dvr2_script_directory")
cd "$dvr2_project_directory"

if [ ! -x ./build/old-school-dvr2-harvest ]; then
    printf 'DVR2 runner is not built; run make dvr2-harvest first\n' >&2
    exit 2
fi

exec ./build/old-school-dvr2-harvest \
    --output "$dvr2_output_path"
