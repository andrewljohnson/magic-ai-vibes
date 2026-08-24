#!/usr/bin/env bash
set -euo pipefail

# Statically checks the shell scripts and GitHub Actions workflows.
#
# Neither linter ships with a Rust or Node toolchain, so requiring them
# unconditionally would make the repository's own `make check` fail on a
# machine that is otherwise ready to work. The default aggregate is strict, for
# CI; `available` is the explicitly best-effort local mode that skips a linter
# it cannot find and says so.
mode="${1:-all}"
if [[ $# -gt 1 ]]; then
    echo "usage: $0 [all|available]" >&2
    exit 2
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

missing=0

run_linter() {
    local name="$1"
    shift
    if command -v "$name" >/dev/null 2>&1; then
        echo "== $name =="
        "$@"
        return 0
    fi
    if [[ "$mode" == available ]]; then
        echo "$name not found; skipping (install it, or run 'make doctor')"
        missing=$((missing + 1))
        return 0
    fi
    echo "$name is required for the infrastructure lint; see 'make doctor'" >&2
    return 127
}

run_linter shellcheck shellcheck scripts/*.sh
run_linter actionlint actionlint

if [[ "$missing" -gt 0 ]]; then
    echo "skipped $missing infrastructure linter(s); CI runs both strictly"
fi
