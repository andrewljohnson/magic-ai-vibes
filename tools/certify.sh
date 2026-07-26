#!/bin/sh
# Fail-closed entry point for the Learned Value C16 certification harness.
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec python3 "$SCRIPT_DIR/certify.py" "$@"
