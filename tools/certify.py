#!/usr/bin/env python3
"""Fail-closed certification runner and strict simulator-log parser."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable


TRAINING_GAMES = 800
TRAINING_SEED = 424242
GENERATION = 16
LEARNED_ROLLOUTS = 8
MONTE_CARLO_ROLLOUTS = 2
DEEP_MONTE_CARLO_ROLLOUTS = 8
MAKE_TEST_JOBS = 4
PRIMARY_REPETITIONS = 34
PRIMARY_TOTAL_GAMES = 2040
PRIMARY_EFFECT_OF_INTEREST_PERCENTAGE_POINTS = 3.0
PRIMARY_ALPHA_TWO_SIDED = 0.05
PRIMARY_TARGET_POWER = 0.80
PANEL_REPETITIONS = 5
PANEL_SEEDS = (101, 202, 303, 404, 505, 606, 707, 808)
ARTIFACT_SMOKE_SEED = 919190
DECKS = ("Green", "Red", "Blue", "White", "RU Aggro")
PANEL_PAIRING_REPETITIONS = len(PANEL_SEEDS) * PANEL_REPETITIONS
PANEL_TOTAL_DIRECT_GAMES = PANEL_PAIRING_REPETITIONS * 60
POOLED_DIRECT_REPETITIONS = PRIMARY_REPETITIONS + PANEL_PAIRING_REPETITIONS
POOLED_DIRECT_DIAGONAL_GAMES = 4 * POOLED_DIRECT_REPETITIONS
POOLED_DIRECT_OFF_DIAGONAL_GAMES = 2 * POOLED_DIRECT_REPETITIONS
POOLED_DIRECT_GAMES_PER_DECK = 12 * POOLED_DIRECT_REPETITIONS
POOLED_DIRECT_TOTAL_GAMES = 60 * POOLED_DIRECT_REPETITIONS
POLICIES = (
    "Random",
    "Monte Carlo",
    "Deep Monte Carlo",
    "Handcrafted Policy",
    "Learned Value",
)
OTHER_POLICIES = (
    "Monte Carlo",
    "Deep Monte Carlo",
    "Handcrafted Policy",
)
POOLED_MIXED_GAMES_PER_DECK_POLICY = 640
FINGERPRINT_RE = r"[0-9a-f]{64}"
ARTIFACT_RELATIVE_PATH = Path(
    "build/model-cache/"
    "old-school-value-challenger-v3-c16-t800-s424242.bin"
)
CERTIFICATION_ROOT = Path("certification-runs")
SIMULATOR_SOURCES = (
    "src/game.cpp",
    "src/interactive.cpp",
    "src/learned_iteration.cpp",
    "src/probes.cpp",
    "src/probe_eval.cpp",
    "src/probe_runner.cpp",
    "src/main.cpp",
)
SIMULATOR_HEADERS = (
    "include/old_school/game.hpp",
    "include/old_school/interactive.hpp",
    "include/old_school/learned_iteration.hpp",
    "include/old_school/probes.hpp",
    "include/old_school/probe_eval.hpp",
    "include/old_school/probe_runner.hpp",
)
ALLOWED_SOURCE_OUTPUT_PREFIXES = (
    "build/",
    "web/dist-game/",
    "web/node_modules/",
)
ALLOWED_REVIEW_STATUS = b" M REVIEW.md\0"


class ContractError(RuntimeError):
    """The CLI output is missing or contradicts required evidence."""


class InfrastructureError(RuntimeError):
    """A command or prerequisite failed before a scientific verdict."""


@dataclass(frozen=True)
class Record:
    wins: int
    losses: int
    draws: int

    @property
    def games(self) -> int:
        return self.wins + self.losses + self.draws

    @property
    def win_rate(self) -> float:
        return self.wins / self.games if self.games else 0.0


@dataclass(frozen=True)
class ParsedDeckMatrix:
    cells: dict[str, dict[str, Record]]
    challenger_rows: dict[str, Record]
    baseline_columns: dict[str, Record]
    aggregate: Record


def wilson95(wins: int, games: int) -> tuple[float, float]:
    if games <= 0:
        raise ContractError("Wilson interval requires positive games")
    z = 1.959963984540054
    proportion = wins / games
    denominator = 1.0 + z * z / games
    center = (proportion + z * z / (2.0 * games)) / denominator
    margin = (
        z
        * math.sqrt(
            proportion * (1.0 - proportion) / games
            + z * z / (4.0 * games * games)
        )
        / denominator
    )
    return center - margin, center + margin


def _binomial_upper_tail(games: int, probability: float, wins: int) -> float:
    """Return P[X >= wins] for X ~ Binomial(games, probability)."""
    if games <= 0:
        raise InfrastructureError("binomial power requires positive games")
    if probability <= 0.0 or probability >= 1.0:
        raise InfrastructureError(
            "binomial power probability must be strictly between zero and one"
        )
    if wins <= 0:
        return 1.0
    if wins > games:
        return 0.0
    log_probability = math.log(probability)
    log_complement = math.log1p(-probability)
    log_terms = [
        math.lgamma(games + 1)
        - math.lgamma(value + 1)
        - math.lgamma(games - value + 1)
        + value * log_probability
        + (games - value) * log_complement
        for value in range(wins, games + 1)
    ]
    maximum = max(log_terms)
    return math.exp(maximum) * math.fsum(
        math.exp(value - maximum) for value in log_terms
    )


def primary_power_contract() -> dict[str, Any]:
    """Describe exact-binomial power for the predeclared three-point effect."""
    critical_wins = next(
        wins
        for wins in range(PRIMARY_TOTAL_GAMES + 1)
        if wilson95(wins, PRIMARY_TOTAL_GAMES)[0] > 0.5
    )
    alternative = (
        0.5 + PRIMARY_EFFECT_OF_INTEREST_PERCENTAGE_POINTS / 100.0
    )
    achieved_power = _binomial_upper_tail(
        PRIMARY_TOTAL_GAMES, alternative, critical_wins
    )
    false_positive_probability = _binomial_upper_tail(
        PRIMARY_TOTAL_GAMES, 0.5, critical_wins
    )
    low = 0.5
    high = 1.0
    for _ in range(64):
        midpoint = (low + high) / 2.0
        if (
            _binomial_upper_tail(
                PRIMARY_TOTAL_GAMES, midpoint, critical_wins
            )
            < PRIMARY_TARGET_POWER
        ):
            low = midpoint
        else:
            high = midpoint
    return {
        "method": (
            "exact binomial upper tail at the first win count whose "
            "two-sided 95% Wilson lower bound exceeds 50%; draws count "
            "as non-wins"
        ),
        "sampling_assumption": (
            "independent Bernoulli outright-win indicators at fixed p; "
            "deck heterogeneity or within-pair dependence is not modeled "
            "and can reduce effective power"
        ),
        "null_win_probability": 0.5,
        "alternative_win_probability": alternative,
        "effect_percentage_points":
            PRIMARY_EFFECT_OF_INTEREST_PERCENTAGE_POINTS,
        "target_power": PRIMARY_TARGET_POWER,
        "achieved_power": achieved_power,
        "target_power_met": achieved_power >= PRIMARY_TARGET_POWER,
        "critical_wins": critical_wins,
        "critical_win_rate": critical_wins / PRIMARY_TOTAL_GAMES,
        "actual_null_upper_tail": false_positive_probability,
        "mde_at_target_power_percentage_points": (high - 0.5) * 100.0,
    }


def sha256_path(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def snapshot_entries(root: Path) -> tuple[str, ...]:
    return tuple(
        path.relative_to(root).as_posix()
        for path in sorted(root.rglob("*"), key=lambda item: item.as_posix())
        if path.is_file() or path.is_symlink()
    )


def snapshot_sha256(
    root: Path, entries: Iterable[str] | None = None
) -> str:
    """Hash selected paths, modes, file bytes, and symlink targets."""
    digest = hashlib.sha256()
    selected = snapshot_entries(root) if entries is None else tuple(entries)
    for relative_text in selected:
        path = root / relative_text
        relative = relative_text.encode("utf-8")
        if path.is_symlink():
            digest.update(b"L\0" + relative + b"\0")
            digest.update(
                f"{path.lstat().st_mode & 0o7777:o}".encode("ascii") + b"\0"
            )
            digest.update(os.readlink(path).encode("utf-8"))
            digest.update(b"\0")
        elif path.is_file():
            digest.update(b"F\0" + relative + b"\0")
            digest.update(
                f"{path.stat().st_mode & 0o7777:o}".encode("ascii") + b"\0"
            )
            with path.open("rb") as source:
                for block in iter(lambda: source.read(1024 * 1024), b""):
                    digest.update(block)
            digest.update(b"\0")
        else:
            digest.update(b"M\0" + relative + b"\0")
    return digest.hexdigest()


def unexpected_snapshot_paths(
    root: Path, expected_entries: Iterable[str]
) -> list[str]:
    expected = set(expected_entries)
    return [
        relative
        for relative in snapshot_entries(root)
        if relative not in expected
        and not any(
            relative.startswith(prefix)
            for prefix in ALLOWED_SOURCE_OUTPUT_PREFIXES
        )
    ]


def compiler_argv() -> list[str]:
    command = shlex.split(os.environ.get("CXX", "c++"))
    if not command:
        raise InfrastructureError("CXX resolved to an empty compiler command")
    return command


def resolve_npm_cache(
    npm: Path,
    cwd: Path,
    environment: dict[str, str],
) -> Path:
    """Resolve a preexisting npm cache without allowing network fallback."""
    process = subprocess.run(
        [str(npm), "config", "get", "cache"],
        cwd=cwd,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if process.returncode != 0:
        detail = process.stderr.strip() or process.stdout.strip()
        raise InfrastructureError(
            "could not resolve npm cache"
            + (f": {detail}" if detail else "")
        )
    lines = [line.strip() for line in process.stdout.splitlines() if line.strip()]
    if len(lines) != 1:
        raise InfrastructureError(
            f"npm cache query returned {len(lines)} nonempty lines"
        )
    cache = Path(lines[0])
    if not cache.is_absolute() or not cache.is_dir():
        raise InfrastructureError(
            f"npm cache is not an existing absolute directory: {cache}"
        )
    return cache


def archived_test_environment(
    base: dict[str, str],
    compiler: Iterable[str],
    toolchain_bin: Path,
    npm_cache: Path,
) -> dict[str, str]:
    """Build the archived-tree test environment with offline dependencies."""
    environment = base.copy()
    environment["CXX"] = shlex.join(compiler)
    environment["NPM_CONFIG_AUDIT"] = "false"
    environment["NPM_CONFIG_FUND"] = "false"
    environment["NPM_CONFIG_IGNORE_SCRIPTS"] = "true"
    environment["NPM_CONFIG_OFFLINE"] = "true"
    environment["NPM_CONFIG_CACHE"] = str(npm_cache)
    environment["PATH"] = (
        str(toolchain_bin)
        + os.pathsep
        + environment.get("PATH", "")
    )
    return environment


def resolve_executable(command: str, cwd: Path) -> Path:
    candidate: str | None
    if os.sep in command:
        path = Path(command)
        candidate = str(path if path.is_absolute() else cwd / path)
    else:
        candidate = shutil.which(command)
    if candidate is None:
        raise InfrastructureError(f"executable was not found on PATH: {command}")
    resolved = Path(candidate).resolve()
    if not resolved.is_file() or not os.access(resolved, os.X_OK):
        raise InfrastructureError(
            f"resolved executable is not an executable file: {resolved}"
        )
    return resolved


def _validate_printed_percentage(
    label: str, printed: float, record: Record
) -> None:
    expected = record.win_rate * 100.0
    if abs(printed - expected) > 0.051:
        raise ContractError(
            f"{label} percentage is {printed:.6g}%, "
            f"but counts imply {expected:.6g}%"
        )


def _validate_embedded_record_percentages(
    text: str, context: str
) -> None:
    pattern = re.compile(
        r"(\d+)-(\d+)-(\d+) \(([0-9.]+)%(?: wins|,|\))"
    )
    for ordinal, match in enumerate(pattern.finditer(text), start=1):
        _validate_printed_percentage(
            f"{context} record {ordinal}",
            float(match.group(4)),
            Record(
                int(match.group(1)),
                int(match.group(2)),
                int(match.group(3)),
            ),
        )


def release_build_command(
    compiler: Iterable[str], simulator: Path
) -> list[str]:
    return [
        *compiler,
        "-Iinclude",
        "-std=c++20",
        "-O3",
        "-Wall",
        "-Wextra",
        "-Wpedantic",
        "-Werror",
        *SIMULATOR_SOURCES,
        "-o",
        str(simulator),
    ]


def make_test_command(make: Path) -> list[str]:
    return [str(make), "-B", f"-j{MAKE_TEST_JOBS}", "test"]


def sanitizer_build_command(
    compiler: Iterable[str], output: Path
) -> list[str]:
    return [
        *compiler,
        "-Iinclude",
        "-std=c++20",
        "-O1",
        "-g",
        "-Wall",
        "-Wextra",
        "-Wpedantic",
        "-Werror",
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "src/game.cpp",
        "src/interactive.cpp",
        "src/learned_iteration.cpp",
        "tests/test_game.cpp",
        "-o",
        str(output),
    ]


def artifact_load_command(simulator: Path) -> list[str]:
    return [
        str(simulator),
        "--games",
        "1",
        "--seed",
        str(ARTIFACT_SMOKE_SEED),
        "--bots",
        "learned-value",
        "--train-games",
        str(TRAINING_GAMES),
        "--train-seed",
        str(TRAINING_SEED),
        "--learned-generations",
        str(GENERATION),
        "--learned-rollouts",
        str(LEARNED_ROLLOUTS),
    ]


def primary_benchmark_command(simulator: Path, primary_seed: int) -> list[str]:
    return [
        str(simulator),
        "--benchmark",
        "--games",
        str(PRIMARY_REPETITIONS),
        "--seed",
        str(primary_seed),
        "--train-games",
        str(TRAINING_GAMES),
        "--train-seed",
        str(TRAINING_SEED),
        "--challenger",
        "learned-value-c16",
        "--baseline",
        "handcrafted",
        "--learned-rollouts",
        str(LEARNED_ROLLOUTS),
    ]


def fixed_panel_command(simulator: Path) -> list[str]:
    return [
        str(simulator),
        "--stability",
        "--stability-runs",
        str(len(PANEL_SEEDS)),
        "--games",
        str(PANEL_REPETITIONS),
        "--seed",
        "0",
        "--rollouts",
        str(MONTE_CARLO_ROLLOUTS),
        "--deep-rollouts",
        str(DEEP_MONTE_CARLO_ROLLOUTS),
        "--train-games",
        str(TRAINING_GAMES),
        "--train-seed",
        str(TRAINING_SEED),
        "--learned-generations",
        str(GENERATION),
        "--learned-rollouts",
        str(LEARNED_ROLLOUTS),
    ]


def _single(pattern: str, text: str, label: str, flags: int = 0) -> re.Match[str]:
    matches = list(re.finditer(pattern, text, flags))
    if len(matches) != 1:
        raise ContractError(
            f"expected exactly one {label}, found {len(matches)}"
        )
    return matches[0]


def _model_identity(text: str, expected_fingerprint: str) -> str:
    fingerprint = _single(
        rf"^  Value Challenger C16 fingerprint: ({FINGERPRINT_RE})$",
        text,
        "C16 fingerprint",
        re.MULTILINE,
    ).group(1)
    if fingerprint != expected_fingerprint:
        raise ContractError(
            "C16 fingerprint mismatch: "
            f"expected {expected_fingerprint}, got {fingerprint}"
        )
    cache_path = _single(
        r"^  Value Challenger C16 artifact cache: loaded (.+)$",
        text,
        "loaded C16 artifact",
        re.MULTILINE,
    ).group(1)
    if "Training frozen Value Challenger C16" in text:
        raise ContractError("certification route trained C16")
    if re.search(
        r"^  Value Challenger C16 artifact cache: generated ",
        text,
        re.MULTILINE,
    ):
        raise ContractError("certification route generated C16")
    return cache_path


def parse_artifact_log(text: str, expected_fingerprint: str) -> dict[str, Any]:
    cache_path = _model_identity(text, expected_fingerprint)
    _single(
        r"^Training seed: 424242$",
        text,
        "artifact-route training seed",
        re.MULTILINE,
    )
    _single(
        (
            r"^Frozen learned model: Learned Value Challenger C16, "
            r"seed 424242, 800 training games, K=8$"
        ),
        text,
        "artifact-route frozen model identity",
        re.MULTILINE,
    )
    return {
        "fingerprint": expected_fingerprint,
        "cache_path_reported": cache_path,
        "loaded": True,
    }


def _sum_records(records: Iterable[Record]) -> Record:
    materialized = tuple(records)
    return Record(
        sum(record.wins for record in materialized),
        sum(record.losses for record in materialized),
        sum(record.draws for record in materialized),
    )


def _record_report(record: Record) -> dict[str, int]:
    return {
        "wins": record.wins,
        "losses": record.losses,
        "draws": record.draws,
        "games": record.games,
    }


def _matrix_report(matrix: ParsedDeckMatrix) -> dict[str, Any]:
    return {
        "aggregate": _record_report(matrix.aggregate),
        "challenger_rows": {
            deck: _record_report(matrix.challenger_rows[deck])
            for deck in DECKS
        },
        "baseline_columns": {
            deck: _record_report(matrix.baseline_columns[deck])
            for deck in DECKS
        },
        "cells": {
            challenger: {
                baseline: _record_report(
                    matrix.cells[challenger][baseline]
                )
                for baseline in DECKS
            }
            for challenger in DECKS
        },
    }


def _matrix_from_cells(
    cells: dict[str, dict[str, Record]], label: str
) -> ParsedDeckMatrix:
    if set(cells) != set(DECKS):
        raise ContractError(
            f"{label} challenger rows are incomplete: "
            f"expected {sorted(DECKS)}, got {sorted(cells)}"
        )
    for challenger in DECKS:
        if set(cells[challenger]) != set(DECKS):
            raise ContractError(
                f"{label} baseline columns for {challenger} are incomplete: "
                f"expected {sorted(DECKS)}, "
                f"got {sorted(cells[challenger])}"
            )
        if not all(
            isinstance(cells[challenger][baseline], Record)
            for baseline in DECKS
        ):
            raise ContractError(f"{label} contains a non-record cell")

    challenger_rows = {
        challenger: _sum_records(cells[challenger].values())
        for challenger in DECKS
    }
    baseline_columns = {
        baseline: Record(
            sum(cells[challenger][baseline].losses for challenger in DECKS),
            sum(cells[challenger][baseline].wins for challenger in DECKS),
            sum(cells[challenger][baseline].draws for challenger in DECKS),
        )
        for baseline in DECKS
    }
    aggregate = _sum_records(challenger_rows.values())
    return ParsedDeckMatrix(
        cells=cells,
        challenger_rows=challenger_rows,
        baseline_columns=baseline_columns,
        aggregate=aggregate,
    )


def _record_from_report(value: Any, label: str) -> Record:
    if not isinstance(value, dict):
        raise ContractError(f"{label} is not a record object")
    expected_fields = {"wins", "losses", "draws", "games"}
    if set(value) != expected_fields:
        raise ContractError(
            f"{label} fields are {sorted(value)}, "
            f"expected {sorted(expected_fields)}"
        )
    for field in expected_fields:
        if type(value[field]) is not int or value[field] < 0:
            raise ContractError(f"{label} {field} is not a nonnegative integer")
    record = Record(value["wins"], value["losses"], value["draws"])
    if record.games != value["games"]:
        raise ContractError(
            f"{label} has {record.games} outcomes but reports "
            f"{value['games']} games"
        )
    return record


def _record_from_summary(value: Any, label: str) -> Record:
    if not isinstance(value, dict):
        raise ContractError(f"{label} is not a summary object")
    required_fields = ("wins", "losses", "draws")
    if any(field not in value for field in required_fields):
        raise ContractError(f"{label} is missing outcome counts")
    for field in required_fields:
        if type(value[field]) is not int or value[field] < 0:
            raise ContractError(f"{label} {field} is not a nonnegative integer")
    return Record(value["wins"], value["losses"], value["draws"])


def _matrix_from_report(value: Any, label: str) -> ParsedDeckMatrix:
    if not isinstance(value, dict):
        raise ContractError(f"{label} is not a matrix object")
    expected_fields = {
        "aggregate",
        "challenger_rows",
        "baseline_columns",
        "cells",
    }
    if set(value) != expected_fields:
        raise ContractError(
            f"{label} fields are {sorted(value)}, "
            f"expected {sorted(expected_fields)}"
        )
    raw_cells = value["cells"]
    if not isinstance(raw_cells, dict):
        raise ContractError(f"{label} cells are not an object")
    if set(raw_cells) != set(DECKS):
        raise ContractError(
            f"{label} cell rows are incomplete: "
            f"expected {sorted(DECKS)}, got {sorted(raw_cells)}"
        )
    cells: dict[str, dict[str, Record]] = {}
    for challenger in DECKS:
        raw_row = raw_cells[challenger]
        if not isinstance(raw_row, dict):
            raise ContractError(
                f"{label} cell row {challenger} is not an object"
            )
        if set(raw_row) != set(DECKS):
            raise ContractError(
                f"{label} cell row {challenger} is incomplete: "
                f"expected {sorted(DECKS)}, got {sorted(raw_row)}"
            )
        cells[challenger] = {
            baseline: _record_from_report(
                raw_row[baseline],
                f"{label} cell {challenger} vs {baseline}",
            )
            for baseline in DECKS
        }

    matrix = _matrix_from_cells(cells, label)
    if _matrix_report(matrix) != value:
        raise ContractError(
            f"{label} cells do not reconstruct its exact rows, columns, "
            "and aggregate"
        )
    return matrix


def pool_direct_evidence(
    primary: dict[str, Any], stability: dict[str, Any]
) -> dict[str, Any]:
    try:
        primary_report = primary["exact_deck_matrix"]
        panel_report = stability["pooled_handcrafted"]["exact_deck_matrix"]
    except (KeyError, TypeError) as error:
        raise ContractError(
            "pooled direct evidence is missing an exact source matrix"
        ) from error

    primary_matrix = _matrix_from_report(
        primary_report, "primary exact deck matrix report"
    )
    panel_matrix = _matrix_from_report(
        panel_report, "fixed-panel exact deck matrix report"
    )
    try:
        primary_summary = _record_from_summary(
            primary["record"], "primary parsed aggregate"
        )
        panel_summary = _record_from_summary(
            stability["pooled_handcrafted"],
            "fixed-panel parsed aggregate",
        )
    except (KeyError, TypeError) as error:
        raise ContractError(
            "pooled direct evidence is missing a parsed source aggregate"
        ) from error
    if primary_matrix.aggregate != primary_summary:
        raise ContractError(
            "primary exact deck matrix report disagrees with its "
            "parsed aggregate"
        )
    if panel_matrix.aggregate != panel_summary:
        raise ContractError(
            "fixed-panel exact deck matrix report disagrees with its "
            "parsed aggregate"
        )
    if primary_matrix.aggregate.games != PRIMARY_TOTAL_GAMES:
        raise ContractError(
            "primary exact deck matrix report has "
            f"{primary_matrix.aggregate.games} games, "
            f"expected {PRIMARY_TOTAL_GAMES}"
        )
    if panel_matrix.aggregate.games != PANEL_TOTAL_DIRECT_GAMES:
        raise ContractError(
            "fixed-panel exact deck matrix report has "
            f"{panel_matrix.aggregate.games} games, "
            f"expected {PANEL_TOTAL_DIRECT_GAMES}"
        )

    cells = {
        challenger: {
            baseline: _sum_records(
                (
                    primary_matrix.cells[challenger][baseline],
                    panel_matrix.cells[challenger][baseline],
                )
            )
            for baseline in DECKS
        }
        for challenger in DECKS
    }
    matrix = _matrix_from_cells(cells, "pooled direct exact deck matrix")
    expected_aggregate = _sum_records(
        (primary_matrix.aggregate, panel_matrix.aggregate)
    )
    if matrix.aggregate != expected_aggregate:
        raise ContractError(
            "pooled direct aggregate is not the component-wise sum "
            "of its source aggregates"
        )
    if matrix.aggregate.games != POOLED_DIRECT_TOTAL_GAMES:
        raise ContractError(
            f"pooled direct aggregate has {matrix.aggregate.games} games, "
            f"expected {POOLED_DIRECT_TOTAL_GAMES}"
        )

    for challenger in DECKS:
        for baseline in DECKS:
            record = matrix.cells[challenger][baseline]
            expected_games = (
                POOLED_DIRECT_DIAGONAL_GAMES
                if challenger == baseline
                else POOLED_DIRECT_OFF_DIAGONAL_GAMES
            )
            if record.games != expected_games:
                raise ContractError(
                    f"pooled direct cell {challenger} vs {baseline} has "
                    f"{record.games} games, expected {expected_games}"
                )
        if (
            matrix.challenger_rows[challenger].games
            != POOLED_DIRECT_GAMES_PER_DECK
        ):
            raise ContractError(
                f"pooled direct challenger row {challenger} has "
                f"{matrix.challenger_rows[challenger].games} games, "
                f"expected {POOLED_DIRECT_GAMES_PER_DECK}"
            )
        if (
            matrix.baseline_columns[challenger].games
            != POOLED_DIRECT_GAMES_PER_DECK
        ):
            raise ContractError(
                f"pooled direct baseline column {challenger} has "
                f"{matrix.baseline_columns[challenger].games} games, "
                f"expected {POOLED_DIRECT_GAMES_PER_DECK}"
            )

    challenger_cross_sum = _sum_records(matrix.challenger_rows.values())
    if challenger_cross_sum != matrix.aggregate:
        raise ContractError(
            "pooled direct challenger rows do not sum component-wise "
            "to the aggregate"
        )
    baseline_cross_sum = _sum_records(matrix.baseline_columns.values())
    expected_baseline = Record(
        matrix.aggregate.losses,
        matrix.aggregate.wins,
        matrix.aggregate.draws,
    )
    if baseline_cross_sum != expected_baseline:
        raise ContractError(
            "pooled direct baseline columns do not sum component-wise "
            "to the reciprocal aggregate"
        )

    interval = wilson95(matrix.aggregate.wins, matrix.aggregate.games)
    per_deck = {
        deck: {
            "challenger_wins": matrix.challenger_rows[deck].wins,
            "baseline_wins": matrix.baseline_columns[deck].wins,
            "challenger_record": _record_report(
                matrix.challenger_rows[deck]
            ),
            "baseline_record": _record_report(
                matrix.baseline_columns[deck]
            ),
            "games_per_policy": POOLED_DIRECT_GAMES_PER_DECK,
            "challenger_won": (
                matrix.challenger_rows[deck].wins
                > matrix.baseline_columns[deck].wins
            ),
        }
        for deck in DECKS
    }
    aggregate_over_50 = matrix.aggregate.win_rate > 0.5
    wilson_lower_over_50 = interval[0] > 0.5
    direct_wins_all_decks = all(
        entry["challenger_won"] for entry in per_deck.values()
    )
    return {
        "source_repetitions": {
            "primary": PRIMARY_REPETITIONS,
            "fixed_panel": PANEL_PAIRING_REPETITIONS,
        },
        "repetitions": POOLED_DIRECT_REPETITIONS,
        "total_games": POOLED_DIRECT_TOTAL_GAMES,
        "games_per_deck": POOLED_DIRECT_GAMES_PER_DECK,
        "diagonal_games_per_cell": POOLED_DIRECT_DIAGONAL_GAMES,
        "off_diagonal_games_per_cell":
            POOLED_DIRECT_OFF_DIAGONAL_GAMES,
        "record": {
            **_record_report(matrix.aggregate),
            "win_rate": matrix.aggregate.win_rate,
        },
        "wilson95": {
            "low": interval[0],
            "high": interval[1],
        },
        "per_deck": per_deck,
        "exact_deck_matrix": _matrix_report(matrix),
        "aggregate_over_50": aggregate_over_50,
        "wilson_lower_over_50": wilson_lower_over_50,
        "direct_wins_all_decks": direct_wins_all_decks,
        "gate_pass": (
            aggregate_over_50
            and wilson_lower_over_50
            and direct_wins_all_decks
        ),
    }


def _parse_exact_deck_matrix(
    text: str,
    *,
    header: str,
    diagonal_games: int,
    off_diagonal_games: int,
    label: str,
) -> ParsedDeckMatrix:
    header_match = _single(
        rf"^{re.escape(header)}$",
        text,
        f"{label} header",
        re.MULTILINE,
    )
    deck_pattern = "|".join(re.escape(deck) for deck in DECKS)
    row_pattern = re.compile(
        rf"^  ({deck_pattern}) vs ({deck_pattern}): "
        r"(\d+)-(\d+)-(\d+) \((\d+) games\)$",
        re.MULTILINE,
    )
    matches = list(row_pattern.finditer(text))
    expected_cell_count = len(DECKS) * len(DECKS)
    if len(matches) != expected_cell_count:
        raise ContractError(
            f"{label} has {len(matches)} cells, "
            f"expected {expected_cell_count}"
        )
    if matches[0].start() <= header_match.end():
        raise ContractError(f"{label} rows do not follow its header")
    separators = [
        text[header_match.end():matches[0].start()],
        *[
            text[left.end():right.start()]
            for left, right in zip(matches, matches[1:])
        ],
    ]
    if any(separator not in ("\n", "\r\n") for separator in separators):
        raise ContractError(f"{label} rows are not one contiguous matrix")

    cells: dict[str, dict[str, Record]] = {
        challenger: {} for challenger in DECKS
    }
    for match in matches:
        challenger = match.group(1)
        baseline = match.group(2)
        if baseline in cells[challenger]:
            raise ContractError(
                f"duplicate {label} cell: {challenger} vs {baseline}"
            )
        record = Record(
            int(match.group(3)),
            int(match.group(4)),
            int(match.group(5)),
        )
        printed_games = int(match.group(6))
        if record.games != printed_games:
            raise ContractError(
                f"{label} cell {challenger} vs {baseline} has "
                f"{record.games} outcomes but prints {printed_games} games"
            )
        expected_games = (
            diagonal_games
            if challenger == baseline
            else off_diagonal_games
        )
        if record.games != expected_games:
            raise ContractError(
                f"{label} cell {challenger} vs {baseline} has "
                f"{record.games} games, expected {expected_games}"
            )
        cells[challenger][baseline] = record

    expected_pairs = {
        (challenger, baseline)
        for challenger in DECKS
        for baseline in DECKS
    }
    actual_pairs = {
        (challenger, baseline)
        for challenger, row in cells.items()
        for baseline in row
    }
    if actual_pairs != expected_pairs:
        raise ContractError(
            f"{label} is not an exact 5x5 matrix: "
            f"missing={sorted(expected_pairs - actual_pairs)}, "
            f"extra={sorted(actual_pairs - expected_pairs)}"
        )

    return _matrix_from_cells(cells, label)


def _parse_benchmark_decks(
    text: str, matrix: ParsedDeckMatrix
) -> dict[str, dict[str, Any]]:
    pattern = re.compile(
        r"^  (Green|Red|Blue|White|RU Aggro): challenger "
        r"([0-9.]+)% \((\d+)-(\d+)-(\d+)\), baseline "
        r"([0-9.]+)% \((\d+)-(\d+)-(\d+)\)$",
        re.MULTILINE,
    )
    decks: dict[str, dict[str, Any]] = {}
    for match in pattern.finditer(text):
        name = match.group(1)
        if name in decks:
            raise ContractError(f"duplicate benchmark deck row: {name}")
        challenger = Record(
            int(match.group(3)), int(match.group(4)), int(match.group(5))
        )
        baseline = Record(
            int(match.group(7)), int(match.group(8)), int(match.group(9))
        )
        _validate_printed_percentage(
            f"{name} challenger", float(match.group(2)), challenger
        )
        _validate_printed_percentage(
            f"{name} baseline", float(match.group(6)), baseline
        )
        if challenger.games != PRIMARY_TOTAL_GAMES // len(DECKS):
            raise ContractError(
                f"{name} challenger game count is {challenger.games}, "
                f"expected {PRIMARY_TOTAL_GAMES // len(DECKS)}"
            )
        if baseline.games != PRIMARY_TOTAL_GAMES // len(DECKS):
            raise ContractError(
                f"{name} baseline game count is {baseline.games}, "
                f"expected {PRIMARY_TOTAL_GAMES // len(DECKS)}"
            )
        reconstructed_challenger = matrix.challenger_rows[name]
        reconstructed_baseline = matrix.baseline_columns[name]
        if challenger != reconstructed_challenger:
            raise ContractError(
                f"{name} challenger marginal disagrees with exact matrix"
            )
        if baseline != reconstructed_baseline:
            raise ContractError(
                f"{name} baseline marginal disagrees with exact matrix"
            )
        decks[name] = {
            "challenger": reconstructed_challenger,
            "baseline": reconstructed_baseline,
            "challenger_won": (
                reconstructed_challenger.wins
                > reconstructed_baseline.wins
            ),
        }
    if set(decks) != set(DECKS):
        raise ContractError(
            "benchmark deck rows are incomplete: "
            f"expected {sorted(DECKS)}, got {sorted(decks)}"
        )
    return decks


def parse_benchmark_log(
    text: str, expected_fingerprint: str, expected_seed: int
) -> dict[str, Any]:
    _validate_embedded_record_percentages(text, "benchmark")
    cache_path = _model_identity(text, expected_fingerprint)
    evaluation_seed = int(
        _single(
            r"^Evaluation seed: (\d+)$",
            text,
            "benchmark evaluation seed",
            re.MULTILINE,
        ).group(1)
    )
    if evaluation_seed != expected_seed:
        raise ContractError(
            f"benchmark seed is {evaluation_seed}, expected {expected_seed}"
        )
    _single(
        r"^Training seed: 424242$",
        text,
        "benchmark training seed",
        re.MULTILINE,
    )
    _single(
        r"^Challenger: Learned Value Challenger C16$",
        text,
        "benchmark challenger",
        re.MULTILINE,
    )
    _single(
        r"^Baseline: Handcrafted Policy$",
        text,
        "benchmark baseline",
        re.MULTILINE,
    )
    _single(
        (
            r"^Challenger frozen model: Learned Value Challenger C16, "
            r"seed 424242, 800 training games, K=8$"
        ),
        text,
        "benchmark frozen model identity",
        re.MULTILINE,
    )
    repetitions = int(
        _single(
            r"^Repetitions per unordered deck pairing: (\d+)$",
            text,
            "benchmark repetition count",
            re.MULTILINE,
        ).group(1)
    )
    total_games = int(
        _single(
            r"^Total paired games: (\d+)$",
            text,
            "benchmark total games",
            re.MULTILINE,
        ).group(1)
    )
    if repetitions != PRIMARY_REPETITIONS or total_games != PRIMARY_TOTAL_GAMES:
        raise ContractError(
            "benchmark size mismatch: "
            f"repetitions={repetitions}, total={total_games}"
        )
    record_match = _single(
        (
            r"^  Challenger record: (\d+)-(\d+)-(\d+) "
            r"\(([0-9.]+)% wins\)$"
        ),
        text,
        "benchmark aggregate record",
        re.MULTILINE,
    )
    record = Record(
        int(record_match.group(1)),
        int(record_match.group(2)),
        int(record_match.group(3)),
    )
    if record.games != total_games:
        raise ContractError(
            f"aggregate record has {record.games} games, expected {total_games}"
        )
    _validate_printed_percentage(
        "benchmark aggregate", float(record_match.group(4)), record
    )
    interval_match = _single(
        (
            r"^  Approximate 95% confidence interval: "
            r"([0-9.]+)% to ([0-9.]+)%$"
        ),
        text,
        "benchmark confidence interval",
        re.MULTILINE,
    )
    printed_interval = (
        float(interval_match.group(1)) / 100.0,
        float(interval_match.group(2)) / 100.0,
    )
    computed_interval = wilson95(record.wins, record.games)
    for printed, computed in zip(printed_interval, computed_interval):
        if abs(printed - computed) > 0.00051:
            raise ContractError(
                "printed Wilson interval disagrees with aggregate record"
            )
    matrix = _parse_exact_deck_matrix(
        text,
        header=(
            "Exact challenger-deck x baseline-deck matrix "
            "(challenger perspective)"
        ),
        diagonal_games=4 * PRIMARY_REPETITIONS,
        off_diagonal_games=2 * PRIMARY_REPETITIONS,
        label="primary exact deck matrix",
    )
    if matrix.aggregate != record:
        raise ContractError(
            "primary exact deck matrix does not reconstruct the "
            "aggregate record"
        )
    decks = _parse_benchmark_decks(text, matrix)
    challenger_totals = _sum_records(
        entry["challenger"] for entry in decks.values()
    )
    if challenger_totals != record:
        raise ContractError(
            "reconstructed challenger rows do not sum component-wise "
            "to the aggregate record"
        )
    baseline_totals = _sum_records(
        entry["baseline"] for entry in decks.values()
    )
    expected_baseline = Record(record.losses, record.wins, record.draws)
    if baseline_totals != expected_baseline:
        raise ContractError(
            "reconstructed reciprocal baseline columns do not sum "
            "component-wise to the aggregate record"
        )
    smoke_gate_pass = (
        record.win_rate > 0.5
        and computed_interval[0] > 0.5
    )
    standalone_gate_pass = (
        smoke_gate_pass
        and all(entry["challenger_won"] for entry in decks.values())
    )
    verdict = _single(
        r"^  Verdict: (.+)$", text, "benchmark verdict", re.MULTILINE
    ).group(1)
    cli_pass = verdict.startswith("PASS")
    if cli_pass != standalone_gate_pass:
        raise ContractError(
            "CLI benchmark verdict disagrees with independently parsed "
            "standalone gate"
        )
    return {
        "evaluation_seed": evaluation_seed,
        "training_seed": TRAINING_SEED,
        "repetitions": repetitions,
        "total_games": total_games,
        "record": {
            "wins": record.wins,
            "losses": record.losses,
            "draws": record.draws,
            "win_rate": record.win_rate,
        },
        "wilson95": {
            "low": computed_interval[0],
            "high": computed_interval[1],
        },
        "per_deck": {
            name: {
                "challenger_wins": decks[name]["challenger"].wins,
                "baseline_wins": decks[name]["baseline"].wins,
                "challenger_won": decks[name]["challenger_won"],
            }
            for name in DECKS
        },
        "exact_deck_matrix": _matrix_report(matrix),
        "cache_path_reported": cache_path,
        "smoke_gate_pass": smoke_gate_pass,
        "standalone_gate_pass": standalone_gate_pass,
        "cli_verdict_pass": cli_pass,
    }


def parse_stability_log(
    text: str, expected_fingerprint: str
) -> dict[str, Any]:
    _validate_embedded_record_percentages(text, "stability")
    cache_path = _model_identity(text, expected_fingerprint)
    required_lines = (
        (r"^Runs: 8$", "stability run count"),
        (r"^Evaluation base seed: 0$", "stability base seed"),
        (r"^Training seed: 424242$", "stability training seed"),
        (
            r"^Repetitions per unordered deck pairing per run: 5$",
            "stability repetition count",
        ),
        (r"^Learned model: Challenger C16$", "stability model"),
        (
            r"^Learned search worlds per legal action: 8$",
            "stability rollout count",
        ),
    )
    for pattern, label in required_lines:
        _single(pattern, text, label, re.MULTILINE)

    seed_headers = [
        int(value)
        for value in re.findall(r"^  Evaluation seed (\d+):$", text, re.MULTILINE)
    ]
    if seed_headers != list(PANEL_SEEDS):
        raise ContractError(
            f"stability seeds are {seed_headers}, expected {list(PANEL_SEEDS)}"
        )
    per_seed: dict[str, Any] = {}
    for index, seed in enumerate(PANEL_SEEDS):
        start = text.index(f"  Evaluation seed {seed}:")
        if index + 1 < len(PANEL_SEEDS):
            end = text.index(f"  Evaluation seed {PANEL_SEEDS[index + 1]}:")
        else:
            end = text.index("\nPooled results", start)
        block = text[start:end]
        match = _single(
            (
                r"^    vs Handcrafted Policy: "
                r"(\d+)-(\d+)-(\d+) \(([0-9.]+)%\) (PASS|FAIL)$"
            ),
            block,
            f"Handcrafted record for seed {seed}",
            re.MULTILINE,
        )
        record = Record(
            int(match.group(1)), int(match.group(2)), int(match.group(3))
        )
        _validate_printed_percentage(
            f"seed {seed} Handcrafted", float(match.group(4)), record
        )
        expected_games = PANEL_REPETITIONS * 60
        if record.games != expected_games:
            raise ContractError(
                f"seed {seed} has {record.games} Handcrafted games, "
                f"expected {expected_games}"
            )
        won = record.wins > record.losses
        not_losing = record.wins >= record.losses
        if (match.group(5) == "PASS") != won:
            raise ContractError(
                f"seed {seed} Handcrafted verdict contradicts its record"
            )
        lift_match = _single(
            (
                r"^    mixed-field lift: "
                r"Green=(PASS|FAIL) Red=(PASS|FAIL) "
                r"Blue=(PASS|FAIL) White=(PASS|FAIL) "
                r"RU Aggro=(PASS|FAIL) => (PASS|FAIL)$"
            ),
            block,
            f"mixed-field deck set for seed {seed}",
            re.MULTILINE,
        )
        seed_lift_values = [
            lift_match.group(group) == "PASS" for group in range(1, 6)
        ]
        seed_lift_pass = lift_match.group(6) == "PASS"
        if seed_lift_pass != all(seed_lift_values):
            raise ContractError(
                f"seed {seed} mixed-field verdict contradicts its deck rows"
            )
        per_seed[str(seed)] = {
            "wins": record.wins,
            "losses": record.losses,
            "draws": record.draws,
            "won": won,
            "not_losing": not_losing,
            "mixed_field": {
                name: passed
                for name, passed in zip(DECKS, seed_lift_values)
            },
            "mixed_field_all_five": seed_lift_pass,
        }

    pooled_match = _single(
        (
            r"^  vs Handcrafted Policy: (\d+)-(\d+)-(\d+) "
            r"\(([0-9.]+)%, 95% interval ([0-9.]+)% to ([0-9.]+)%\)$"
        ),
        text,
        "pooled Handcrafted record",
        re.MULTILINE,
    )
    pooled = Record(
        int(pooled_match.group(1)),
        int(pooled_match.group(2)),
        int(pooled_match.group(3)),
    )
    _validate_printed_percentage(
        "pooled Handcrafted", float(pooled_match.group(4)), pooled
    )
    expected_pooled_games = PANEL_TOTAL_DIRECT_GAMES
    if pooled.games != expected_pooled_games:
        raise ContractError(
            f"pooled Handcrafted games are {pooled.games}, "
            f"expected {expected_pooled_games}"
        )
    summed_seed_record = Record(
        sum(entry["wins"] for entry in per_seed.values()),
        sum(entry["losses"] for entry in per_seed.values()),
        sum(entry["draws"] for entry in per_seed.values()),
    )
    if pooled != summed_seed_record:
        raise ContractError(
            "pooled Handcrafted record does not equal the "
            "component-wise sum of all fixed-panel seeds"
        )
    computed_interval = wilson95(pooled.wins, pooled.games)
    printed_low = float(pooled_match.group(5)) / 100.0
    printed_high = float(pooled_match.group(6)) / 100.0
    if (
        abs(printed_low - computed_interval[0]) > 0.00051
        or abs(printed_high - computed_interval[1]) > 0.00051
    ):
        raise ContractError(
            "stability Wilson interval disagrees with pooled record"
        )

    matrix = _parse_exact_deck_matrix(
        text,
        header=(
            "Pooled Handcrafted exact challenger-deck x baseline-deck "
            "matrix (challenger perspective)"
        ),
        diagonal_games=4 * PANEL_PAIRING_REPETITIONS,
        off_diagonal_games=2 * PANEL_PAIRING_REPETITIONS,
        label="pooled Handcrafted exact deck matrix",
    )
    if matrix.aggregate != pooled:
        raise ContractError(
            "pooled Handcrafted exact deck matrix does not reconstruct "
            "the pooled aggregate record"
        )

    pooled_start = pooled_match.end()
    pooled_end = text.index("\nPooled mixed-field lift over Random", pooled_start)
    pooled_block = text[pooled_start:pooled_end]
    deck_pattern = re.compile(
        r"^    (Green|Red|Blue|White|RU Aggro): "
        r"(\d+) vs (\d+) (PASS|FAIL)$",
        re.MULTILINE,
    )
    pooled_decks: dict[str, Any] = {}
    for match in deck_pattern.finditer(pooled_block):
        name = match.group(1)
        if name in pooled_decks:
            raise ContractError(f"duplicate pooled Handcrafted deck: {name}")
        printed_challenger_wins = int(match.group(2))
        printed_baseline_wins = int(match.group(3))
        challenger = matrix.challenger_rows[name]
        baseline = matrix.baseline_columns[name]
        if printed_challenger_wins != challenger.wins:
            raise ContractError(
                f"pooled {name} challenger wins disagree with exact matrix"
            )
        if printed_baseline_wins != baseline.wins:
            raise ContractError(
                f"pooled {name} baseline wins disagree with exact matrix"
            )
        expected_deck_games = expected_pooled_games // len(DECKS)
        if challenger.games != expected_deck_games:
            raise ContractError(
                f"pooled {name} challenger matrix row has "
                f"{challenger.games} games, expected {expected_deck_games}"
            )
        if baseline.games != expected_deck_games:
            raise ContractError(
                f"pooled {name} baseline matrix column has "
                f"{baseline.games} games, expected {expected_deck_games}"
            )
        won = challenger.wins > baseline.wins
        if (match.group(4) == "PASS") != won:
            raise ContractError(
                f"pooled {name} verdict contradicts direct wins"
            )
        pooled_decks[name] = {
            "challenger_wins": challenger.wins,
            "baseline_wins": baseline.wins,
            "challenger_record": _record_report(challenger),
            "baseline_record": _record_report(baseline),
            "games_per_policy": expected_deck_games,
            "challenger_won": won,
        }
    if set(pooled_decks) != set(DECKS):
        raise ContractError(
            "pooled Handcrafted deck rows are incomplete: "
            f"expected {sorted(DECKS)}, got {sorted(pooled_decks)}"
        )
    pooled_challenger_decks = _sum_records(
        matrix.challenger_rows.values()
    )
    pooled_baseline_decks = _sum_records(
        matrix.baseline_columns.values()
    )
    if pooled_challenger_decks != pooled:
        raise ContractError(
            "pooled Handcrafted challenger matrix rows do not sum to "
            "the aggregate record"
        )
    if pooled_baseline_decks != Record(
        pooled.losses, pooled.wins, pooled.draws
    ):
        raise ContractError(
            "pooled Handcrafted reciprocal baseline matrix columns do "
            "not sum to the aggregate record"
        )

    lift_start = text.index("\nPooled mixed-field lift over Random")
    lift_block = text[lift_start:]
    lift_pattern = re.compile(
        r"^  (Green|Red|Blue|White|RU Aggro): "
        r"Learned ([+-][0-9.]+) pp, best other (.+) "
        r"([+-][0-9.]+) pp (PASS|FAIL)$",
        re.MULTILINE,
    )
    printed_lifts: dict[str, Any] = {}
    for match in lift_pattern.finditer(lift_block):
        name = match.group(1)
        if name in printed_lifts:
            raise ContractError(f"duplicate mixed-field lift deck: {name}")
        printed_lifts[name] = {
            "learned_lift": float(match.group(2)),
            "best_other": match.group(3),
            "best_other_lift": float(match.group(4)),
            "learned_is_best": match.group(5) == "PASS",
        }
    if set(printed_lifts) != set(DECKS):
        raise ContractError(
            "mixed-field lift rows are incomplete: "
            f"expected {sorted(DECKS)}, got {sorted(printed_lifts)}"
        )

    _single(
        r"^Pooled mixed-field exact deck-policy counts$",
        text,
        "exact pooled mixed-field count header",
        re.MULTILINE,
    )
    policy_pattern = "|".join(re.escape(policy) for policy in POLICIES)
    count_pattern = re.compile(
        r"^  (Green|Red|Blue|White|RU Aggro) \| "
        rf"({policy_pattern}): "
        r"(\d+)-(\d+)-(\d+) \((\d+) games\)$",
        re.MULTILINE,
    )
    exact_counts: dict[str, dict[str, Record]] = {
        name: {} for name in DECKS
    }
    for match in count_pattern.finditer(text):
        deck = match.group(1)
        policy = match.group(2)
        if policy in exact_counts[deck]:
            raise ContractError(
                f"duplicate exact mixed-field count row: {deck} / {policy}"
            )
        record = Record(
            int(match.group(3)),
            int(match.group(4)),
            int(match.group(5)),
        )
        printed_games = int(match.group(6))
        if record.games != printed_games:
            raise ContractError(
                f"{deck} / {policy} count row has {record.games} "
                f"games but prints {printed_games}"
            )
        if record.games != POOLED_MIXED_GAMES_PER_DECK_POLICY:
            raise ContractError(
                f"{deck} / {policy} has {record.games} games, expected "
                f"{POOLED_MIXED_GAMES_PER_DECK_POLICY}"
            )
        exact_counts[deck][policy] = record
    expected_count_pairs = {
        (deck, policy) for deck in DECKS for policy in POLICIES
    }
    actual_count_pairs = {
        (deck, policy)
        for deck, rows in exact_counts.items()
        for policy in rows
    }
    if actual_count_pairs != expected_count_pairs:
        missing = sorted(expected_count_pairs - actual_count_pairs)
        extra = sorted(actual_count_pairs - expected_count_pairs)
        raise ContractError(
            "exact mixed-field count rows are incomplete: "
            f"missing={missing}, extra={extra}"
        )
    exact_total_wins = sum(
        record.wins
        for rows in exact_counts.values()
        for record in rows.values()
    )
    exact_total_losses = sum(
        record.losses
        for rows in exact_counts.values()
        for record in rows.values()
    )
    if exact_total_wins != exact_total_losses:
        raise ContractError(
            "exact mixed-field seat records do not conserve wins and losses"
        )

    lifts: dict[str, Any] = {}
    for deck in DECKS:
        records = exact_counts[deck]
        random_rate = records["Random"].win_rate * 100.0
        learned_rate = records["Learned Value"].win_rate * 100.0
        best_other = OTHER_POLICIES[0]
        best_other_rate = records[best_other].win_rate * 100.0
        for policy in OTHER_POLICIES[1:]:
            rate = records[policy].win_rate * 100.0
            if rate > best_other_rate:
                best_other = policy
                best_other_rate = rate
        learned_lift = learned_rate - random_rate
        best_other_lift = best_other_rate - random_rate
        learned_is_best = learned_rate + 1.0e-12 >= best_other_rate
        printed = printed_lifts[deck]
        if printed["best_other"] != best_other:
            raise ContractError(
                f"{deck} printed best-other policy is "
                f"{printed['best_other']}, computed {best_other}"
            )
        if (
            abs(printed["learned_lift"] - learned_lift) > 0.051
            or abs(printed["best_other_lift"] - best_other_lift) > 0.051
        ):
            raise ContractError(
                f"{deck} printed lifts disagree with exact count rows"
            )
        if printed["learned_is_best"] != learned_is_best:
            raise ContractError(
                f"{deck} printed lift verdict contradicts exact count rows"
            )
        lifts[deck] = {
            "learned_lift": learned_lift,
            "best_other": best_other,
            "best_other_lift": best_other_lift,
            "learned_is_best": learned_is_best,
            "exact_counts": {
                policy: {
                    "wins": records[policy].wins,
                    "losses": records[policy].losses,
                    "draws": records[policy].draws,
                    "games": records[policy].games,
                }
                for policy in POLICIES
            },
        }

    printed_pooled_lift_gate = (
        _single(
            r"^  Per-deck pooled lift gate: (PASS|FAIL)$",
            text,
            "pooled lift verdict",
            re.MULTILINE,
        ).group(1)
        == "PASS"
    )
    pooled_lift_gate = all(
        entry["learned_is_best"] for entry in lifts.values()
    )
    if printed_pooled_lift_gate != pooled_lift_gate:
        raise ContractError("pooled lift verdict contradicts deck rows")
    overall_pass = (
        _single(
            r"^Overall: (PASS|FAIL)$",
            text,
            "stability overall verdict",
            re.MULTILINE,
        ).group(1)
        == "PASS"
    )
    standalone_gate_pass = (
        all(entry["not_losing"] for entry in per_seed.values())
        and pooled.win_rate > 0.5
        and computed_interval[0] > 0.5
        and all(
            entry["challenger_won"] for entry in pooled_decks.values()
        )
        and pooled_lift_gate
    )
    if overall_pass and not standalone_gate_pass:
        raise ContractError(
            "CLI Overall PASS contradicts the independently parsed "
            "standalone panel verdict"
        )
    no_losing_seed = all(
        entry["not_losing"] for entry in per_seed.values()
    )
    panel_gate_pass = no_losing_seed and pooled_lift_gate
    return {
        "seeds": list(PANEL_SEEDS),
        "per_seed_handcrafted": per_seed,
        "no_losing_seed": no_losing_seed,
        "pooled_handcrafted": {
            "wins": pooled.wins,
            "losses": pooled.losses,
            "draws": pooled.draws,
            "win_rate": pooled.win_rate,
            "wilson95": {
                "low": computed_interval[0],
                "high": computed_interval[1],
            },
            "per_deck": pooled_decks,
            "exact_deck_matrix": _matrix_report(matrix),
        },
        "mixed_field": {
            "per_deck": lifts,
            "all_five": pooled_lift_gate,
        },
        "cache_path_reported": cache_path,
        "standalone_gate_pass": standalone_gate_pass,
        "panel_gate_pass": panel_gate_pass,
        "cli_overall_pass": overall_pass,
    }


def validate_primary_exit_code(exit_code: int, result: dict[str, Any]) -> None:
    expected = 0 if result["cli_verdict_pass"] else 1
    if exit_code != expected:
        raise ContractError(
            "primary benchmark exit code contradicts its CLI verdict: "
            f"got {exit_code}, expected {expected}"
        )


def validate_stability_exit_code(
    exit_code: int, result: dict[str, Any]
) -> None:
    # The simulator's Overall includes standalone direct and additional
    # all-policy gates beyond the v4 certification contract. Its exit code
    # must match that printed status, but a stricter CLI rejection does not
    # reject an otherwise exact v4 panel/pooled-direct pass.
    expected = 0 if result["cli_overall_pass"] else 1
    if exit_code != expected:
        raise ContractError(
            "stability exit code contradicts CLI Overall: "
            f"got {exit_code}, expected {expected}"
        )


def _git(root: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=root,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        raise InfrastructureError(
            f"git {' '.join(args)} failed: {result.stderr.strip()}"
        )
    return result.stdout.strip()


def _git_bytes(root: Path, *args: str) -> bytes:
    result = subprocess.run(
        ["git", *args],
        cwd=root,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        error = result.stderr.decode("utf-8", errors="backslashreplace").strip()
        raise InfrastructureError(f"git {' '.join(args)} failed: {error}")
    return result.stdout


def worktree_status_bytes(root: Path) -> bytes:
    return _git_bytes(
        root,
        "status",
        "--porcelain=v1",
        "-z",
        "--untracked-files=all",
    )


def worktree_status_allowed(raw: bytes) -> bool:
    return raw in (b"", ALLOWED_REVIEW_STATUS)


def worktree_status_report(root: Path, raw: bytes) -> dict[str, Any]:
    allowed = worktree_status_allowed(raw)
    review = root / "REVIEW.md"
    head_review = _git_bytes(root, "show", "HEAD:REVIEW.md")
    worktree_review_sha256 = (
        sha256_path(review) if review.is_file() else None
    )
    head_review_sha256 = hashlib.sha256(head_review).hexdigest()
    return {
        "allowed": allowed,
        "porcelain_v1_z_sha256": hashlib.sha256(raw).hexdigest(),
        "porcelain_v1_z_hex": raw.hex(),
        "display": raw.replace(b"\0", b"\n")
        .decode("utf-8", errors="backslashreplace")
        .rstrip("\n"),
        "tolerated_review_drift": raw == ALLOWED_REVIEW_STATUS,
        "review_worktree_sha256": worktree_review_sha256,
        "review_head_sha256": head_review_sha256,
        "review_matches_head": (
            worktree_review_sha256 == head_review_sha256
        ),
    }


class CertificationRunner:
    def __init__(self, root: Path, primary_seed: int, fingerprint: str):
        self.root = root
        self.primary_seed = primary_seed
        self.fingerprint = fingerprint
        self.started = datetime.now(timezone.utc)
        self.run_dir: Path | None = None
        self.report_path: Path | None = None
        self.source_dir: Path | None = None
        self.runtime_dir: Path | None = None
        self.source_entries: tuple[str, ...] = ()
        self.report: dict[str, Any] = {
            "schema": "learned-value-certification/v4",
            "status": "infrastructure-incomplete",
            "certified": False,
            "started_utc": self.started.isoformat(),
            "recipe": {
                "family": "value-challenger",
                "generation": GENERATION,
                "training_games": TRAINING_GAMES,
                "training_seed": TRAINING_SEED,
                "deployment_k": LEARNED_ROLLOUTS,
                "monte_carlo_k": MONTE_CARLO_ROLLOUTS,
                "deep_monte_carlo_k": DEEP_MONTE_CARLO_ROLLOUTS,
                "primary_repetitions": PRIMARY_REPETITIONS,
                "primary_total_games": PRIMARY_TOTAL_GAMES,
                "fixed_panel_repetitions_per_seed": PANEL_REPETITIONS,
                "fixed_panel_pooled_repetitions":
                    PANEL_PAIRING_REPETITIONS,
                "fixed_panel_total_direct_games":
                    PANEL_TOTAL_DIRECT_GAMES,
                "pooled_direct_repetitions": POOLED_DIRECT_REPETITIONS,
                "pooled_direct_total_games": POOLED_DIRECT_TOTAL_GAMES,
                "pooled_direct_games_per_deck":
                    POOLED_DIRECT_GAMES_PER_DECK,
                "pooled_direct_diagonal_games_per_cell":
                    POOLED_DIRECT_DIAGONAL_GAMES,
                "pooled_direct_off_diagonal_games_per_cell":
                    POOLED_DIRECT_OFF_DIAGONAL_GAMES,
                "primary_effect_of_interest_percentage_points":
                    PRIMARY_EFFECT_OF_INTEREST_PERCENTAGE_POINTS,
                "alpha_two_sided": PRIMARY_ALPHA_TWO_SIDED,
                "confidence_level": 1.0 - PRIMARY_ALPHA_TWO_SIDED,
                "power": primary_power_contract(),
            },
            "seeds": {
                "primary": primary_seed,
                "validation": list(PANEL_SEEDS),
                "artifact_smoke": ARTIFACT_SMOKE_SEED,
            },
            "cli_contract": {
                "native_structured_output": False,
                "parser": "strict-text-v4-pooled-direct-integrity-bound",
                "on_missing_or_changed_evidence": "infrastructure-incomplete",
            },
            "stages": [],
            "integrity_checks": [],
            "source_integrity_checks": [],
            "criteria": {
                "primary_aggregate_over_50": None,
                "primary_wilson_lower_over_50": None,
                "mixed_lift_all_decks": None,
                "no_losing_validation_seed": None,
                "pooled_direct_aggregate_over_50": None,
                "pooled_direct_wilson_lower_over_50": None,
                "pooled_direct_wins_all_decks": None,
                "tests_and_sanitizers": None,
            },
            "probes": {
                "used_for_promotion": False,
                "reason": (
                    "No currently qualified immutable five-deck probe "
                    "contract is available; probe evidence is excluded."
                ),
            },
        }

    def _write_report(self, *, completed: bool = False) -> None:
        if self.report_path is None:
            return
        timestamp = datetime.now(timezone.utc).isoformat()
        self.report["updated_utc"] = timestamp
        if completed:
            self.report["completed_utc"] = timestamp
        temporary = self.report_path.with_suffix(".json.tmp")
        temporary.write_text(
            json.dumps(self.report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        os.replace(temporary, self.report_path)

    def prepare(self) -> None:
        commit = _git(self.root, "rev-parse", "HEAD")
        tree = _git(self.root, "rev-parse", "HEAD^{tree}")
        if len(commit) != 40 or len(tree) != 40:
            raise InfrastructureError("git did not return full source identity")
        state_root = self.root / CERTIFICATION_ROOT
        claims = state_root / "seed-claims"
        runs = state_root / "runs"
        claims.mkdir(parents=True, exist_ok=True)
        runs.mkdir(parents=True, exist_ok=True)
        stamp = self.started.strftime("%Y%m%dT%H%M%SZ")
        self.run_dir = (
            runs / f"{stamp}-{commit[:12]}-s{self.primary_seed}-p{os.getpid()}"
        )
        self.run_dir.mkdir()
        self.report_path = self.run_dir / "report.json"
        self.source_dir = self.run_dir / "source"
        self.runtime_dir = self.run_dir / "runtime"
        self.runtime_dir.mkdir()
        self.report["run"] = {
            "id": self.run_dir.name,
            "directory": str(self.run_dir.relative_to(self.root)),
            "commit_sha": commit,
            "source_tree": tree,
            "dirty": None,
        }
        raw_status = worktree_status_bytes(self.root)
        status = worktree_status_report(self.root, raw_status)
        self.report["run"]["dirty"] = bool(raw_status)
        self.report["run"]["worktree"] = status
        self._write_report()
        if self.primary_seed == TRAINING_SEED:
            raise InfrastructureError(
                "primary evaluation seed equals the training seed"
            )
        if self.primary_seed in PANEL_SEEDS:
            raise InfrastructureError(
                "primary evaluation seed reuses a fixed-panel seed"
            )
        if self.primary_seed == ARTIFACT_SMOKE_SEED:
            raise InfrastructureError(
                "primary evaluation seed reuses the artifact smoke seed"
            )
        if not status["allowed"]:
            raise InfrastructureError(
                "certification permits only a clean worktree or the exact "
                "unstaged status ' M REVIEW.md'; staged REVIEW changes, "
                "other paths, renames, deletes, type changes, and untracked "
                "files are forbidden"
            )
        artifact = self.root / ARTIFACT_RELATIVE_PATH
        if not artifact.is_file():
            raise InfrastructureError(
                f"required preexisting artifact is missing: {artifact}"
            )
        claim = claims / str(self.primary_seed)
        try:
            claim.mkdir()
        except FileExistsError as error:
            raise InfrastructureError(
                f"primary seed {self.primary_seed} was already claimed"
            ) from error
        claim_data = {
            "seed": self.primary_seed,
            "commit_sha": commit,
            "source_tree": tree,
            "expected_fingerprint": self.fingerprint,
            "generation": GENERATION,
            "training_games": TRAINING_GAMES,
            "training_seed": TRAINING_SEED,
            "claimed_utc": self.started.isoformat(),
            "pid": os.getpid(),
        }
        (claim / "claim.json").write_text(
            json.dumps(claim_data, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        self.report["run"]["seed_claim"] = str(claim.relative_to(self.root))
        source_archive = self.run_dir / "source.tar"
        _git(
            self.root,
            "archive",
            "--format=tar",
            f"--output={source_archive}",
            commit,
        )
        self.source_dir.mkdir()
        with tarfile.open(source_archive, "r") as archive:
            source_root = self.source_dir.resolve()
            for member in archive.getmembers():
                target = (self.source_dir / member.name).resolve()
                try:
                    target.relative_to(source_root)
                except ValueError as error:
                    raise InfrastructureError(
                        f"git archive contains an unsafe path: {member.name}"
                    ) from error
            archive.extractall(self.source_dir, filter="data")
        self.source_entries = snapshot_entries(self.source_dir)
        missing_source = [
            relative
            for relative in (*SIMULATOR_SOURCES, *SIMULATOR_HEADERS)
            if not (self.source_dir / relative).is_file()
        ]
        if missing_source:
            raise InfrastructureError(
                f"source snapshot is missing required files: {missing_source}"
            )
        runtime_artifact = self.runtime_dir / ARTIFACT_RELATIVE_PATH
        runtime_artifact.parent.mkdir(parents=True)
        shutil.copyfile(artifact, runtime_artifact)
        artifact_sha256 = sha256_path(artifact)
        runtime_artifact_sha256 = sha256_path(runtime_artifact)
        if runtime_artifact_sha256 != artifact_sha256:
            raise InfrastructureError("runtime artifact copy hash mismatch")
        self.report["source"] = {
            "archive": str(source_archive.relative_to(self.root)),
            "archive_sha256": sha256_path(source_archive),
            "snapshot": str(self.source_dir.relative_to(self.root)),
            "snapshot_sha256": snapshot_sha256(
                self.source_dir, self.source_entries
            ),
            "archived_entry_count": len(self.source_entries),
            "commit_sha": commit,
            "tree_sha": tree,
            "allowed_generated_prefixes": list(
                ALLOWED_SOURCE_OUTPUT_PREFIXES
            ),
        }
        package_lock = self.source_dir / "web/package-lock.json"
        if not package_lock.is_file():
            raise InfrastructureError(
                "source snapshot is missing web/package-lock.json"
            )
        self.report["dependencies"] = {
            "web_install": (
                f"make -B -j{MAKE_TEST_JOBS} test invokes offline npm ci "
                "--ignore-scripts from the archived source tree; "
                "package-lock integrity selects bytes and a preexisting "
                "content-addressed cache supplies transport only"
            ),
            "package_lock": "web/package-lock.json",
            "package_lock_sha256": sha256_path(package_lock),
            "generated_dependency_prefix": "web/node_modules/",
            "network_fallback": False,
        }
        self.report["artifact"] = {
            "path": str(ARTIFACT_RELATIVE_PATH),
            "sha256_before": artifact_sha256,
            "runtime_path": str(runtime_artifact.relative_to(self.root)),
            "runtime_sha256_before": runtime_artifact_sha256,
            "expected_fingerprint": self.fingerprint,
        }
        self._write_report()

    def verify_integrity(self, label: str) -> None:
        if (
            self.run_dir is None
            or self.source_dir is None
            or self.runtime_dir is None
        ):
            raise InfrastructureError("integrity check ran before preparation")
        run = self.report["run"]
        source = self.report["source"]
        artifact = self.report["artifact"]

        def existing_hash(path: Path) -> str | None:
            return sha256_path(path) if path.is_file() else None

        current_commit = _git(self.root, "rev-parse", "HEAD")
        current_tree = _git(self.root, "rev-parse", "HEAD^{tree}")
        raw_status = worktree_status_bytes(self.root)
        status = worktree_status_report(self.root, raw_status)
        source_archive = self.root / source["archive"]
        original_artifact = self.root / ARTIFACT_RELATIVE_PATH
        runtime_artifact = self.root / artifact["runtime_path"]
        simulator = self.runtime_dir / "old-school-sim"
        expected_simulator = run.get("simulator_sha256")
        unexpected_source = unexpected_snapshot_paths(
            self.source_dir, self.source_entries
        )
        compiler_state = self.report.get("toolchain", {}).get("compiler")
        compiler_hash = (
            existing_hash(Path(compiler_state["resolved_path"]))
            if compiler_state is not None
            else None
        )
        toolchain_hashes: dict[str, str | None] = {}
        toolchain_matches: dict[str, bool] = {}
        for name in ("compiler", "make", "npm", "node", "python"):
            state = self.report.get("toolchain", {}).get(name)
            if state is None:
                continue
            current = existing_hash(Path(state["resolved_path"]))
            toolchain_hashes[name] = current
            toolchain_matches[name] = current == state["sha256"]
        checks = {
            "commit_matches": current_commit == run["commit_sha"],
            "tree_matches": current_tree == run["source_tree"],
            "worktree_status_allowed": status["allowed"],
            "source_archive_matches": (
                existing_hash(source_archive) == source["archive_sha256"]
            ),
            "source_snapshot_matches": (
                snapshot_sha256(self.source_dir, self.source_entries)
                == source["snapshot_sha256"]
            ),
            "source_has_no_unexpected_inputs": not unexpected_source,
            "original_artifact_matches": (
                existing_hash(original_artifact) == artifact["sha256_before"]
            ),
            "runtime_artifact_matches": (
                existing_hash(runtime_artifact)
                == artifact["runtime_sha256_before"]
            ),
            "simulator_matches": (
                expected_simulator is None
                or existing_hash(simulator) == expected_simulator
            ),
            "compiler_matches": (
                compiler_state is None
                or compiler_hash == compiler_state["sha256"]
            ),
            "toolchain_matches": all(toolchain_matches.values()),
        }
        entry = {
            "label": label,
            "checked_utc": datetime.now(timezone.utc).isoformat(),
            "checks": checks,
            "commit_sha": current_commit,
            "tree_sha": current_tree,
            "worktree": status,
            "unexpected_source_paths": unexpected_source,
            "source_snapshot_sha256": snapshot_sha256(
                self.source_dir, self.source_entries
            ),
            "compiler_sha256": compiler_hash,
            "toolchain_sha256": toolchain_hashes,
            "simulator_sha256": existing_hash(simulator),
            "runtime_artifact_sha256": existing_hash(runtime_artifact),
        }
        self.report["integrity_checks"].append(entry)
        self._write_report()
        failed = [name for name, passed in checks.items() if not passed]
        if failed:
            raise ContractError(
                f"integrity check '{label}' failed: {', '.join(failed)}"
            )

    def verify_source_snapshot(self, label: str) -> None:
        if self.source_dir is None or not self.source_entries:
            raise InfrastructureError(
                "source snapshot check ran before preparation"
            )
        expected = self.report["source"]["snapshot_sha256"]
        actual = snapshot_sha256(self.source_dir, self.source_entries)
        unexpected = unexpected_snapshot_paths(
            self.source_dir, self.source_entries
        )
        toolchain_hashes: dict[str, str | None] = {}
        toolchain_matches: dict[str, bool] = {}
        for name in ("compiler", "make", "npm", "node", "python"):
            state = self.report.get("toolchain", {}).get(name)
            if state is None:
                continue
            path = Path(state["resolved_path"])
            current = sha256_path(path) if path.is_file() else None
            toolchain_hashes[name] = current
            toolchain_matches[name] = current == state["sha256"]
        shim_matches: dict[str, bool] = {}
        toolchain = self.report.get("toolchain", {})
        prefix = toolchain.get("pinned_path_prefix")
        for name, target in toolchain.get(
            "pinned_path_targets", {}
        ).items():
            shim = self.root / prefix / name
            shim_matches[name] = (
                shim.is_symlink()
                and str(Path(os.readlink(shim)).resolve()) == target
            )
        entry = {
            "label": label,
            "checked_utc": datetime.now(timezone.utc).isoformat(),
            "snapshot_sha256": actual,
            "expected_snapshot_sha256": expected,
            "snapshot_matches": actual == expected,
            "unexpected_source_paths": unexpected,
            "no_unexpected_source_paths": not unexpected,
            "toolchain_sha256": toolchain_hashes,
            "toolchain_matches": toolchain_matches,
            "pinned_path_matches": shim_matches,
        }
        self.report["source_integrity_checks"].append(entry)
        self._write_report()
        failed = []
        if actual != expected:
            failed.append("snapshot_matches")
        if unexpected:
            failed.append("no_unexpected_source_paths")
        if not all(toolchain_matches.values()):
            failed.append("toolchain_matches")
        if not all(shim_matches.values()):
            failed.append("pinned_path_matches")
        if failed:
            raise ContractError(
                f"source snapshot check '{label}' failed: "
                f"{', '.join(failed)}"
            )

    def run_stage(
        self,
        name: str,
        argv: Iterable[str],
        *,
        accepted_codes: tuple[int, ...] = (0,),
        environment: dict[str, str] | None = None,
        cwd: Path | None = None,
    ) -> tuple[int, str]:
        if self.run_dir is None:
            raise InfrastructureError("run directory was not initialized")
        command = [str(value) for value in argv]
        execution_cwd = self.root if cwd is None else cwd
        if not execution_cwd.is_dir():
            raise InfrastructureError(
                f"{name} working directory does not exist: {execution_cwd}"
            )
        log_path = (
            self.run_dir
            / f"{len(self.report['stages']) + 1:02d}-{name}.log"
        )
        started = time.monotonic()
        with log_path.open("wb") as log:
            process = subprocess.run(
                command,
                cwd=execution_cwd,
                stdout=log,
                stderr=subprocess.STDOUT,
                env=environment,
                check=False,
            )
        duration = time.monotonic() - started
        log_text = log_path.read_text(encoding="utf-8", errors="replace")
        stage = {
            "id": name,
            "argv": command,
            "cwd": (
                str(execution_cwd.relative_to(self.root))
                if execution_cwd.is_relative_to(self.root)
                else str(execution_cwd)
            ),
            "exit_code": process.returncode,
            "accepted_exit_codes": list(accepted_codes),
            "duration_seconds": duration,
            "log": str(log_path.relative_to(self.root)),
            "log_sha256": sha256_path(log_path),
            "complete": process.returncode in accepted_codes,
        }
        if environment is not None:
            stage["environment_overrides"] = {
                key: value
                for key, value in environment.items()
                if os.environ.get(key) != value
            }
        self.report["stages"].append(stage)
        self._write_report()
        if process.returncode not in accepted_codes:
            raise InfrastructureError(
                f"{name} exited {process.returncode}; see {log_path}"
            )
        return process.returncode, log_text

    def run_source_stage(
        self,
        name: str,
        argv: Iterable[str],
        *,
        accepted_codes: tuple[int, ...] = (0,),
        environment: dict[str, str] | None = None,
    ) -> tuple[int, str]:
        if self.source_dir is None:
            raise InfrastructureError(
                "source-bound stage ran before preparation"
            )
        self.verify_source_snapshot(f"before-{name}")
        try:
            return self.run_stage(
                name,
                argv,
                accepted_codes=accepted_codes,
                environment=environment,
                cwd=self.source_dir,
            )
        finally:
            self.verify_source_snapshot(f"after-{name}")

    def execute(self) -> int:
        try:
            self.prepare()
            if (
                self.run_dir is None
                or self.source_dir is None
                or self.runtime_dir is None
            ):
                raise InfrastructureError(
                    "preparation did not create runtime paths"
                )
            requested_compiler = compiler_argv()
            compiler_path = resolve_executable(
                requested_compiler[0], self.source_dir
            )
            compiler = [str(compiler_path), *requested_compiler[1:]]
            make_path = resolve_executable("make", self.source_dir)
            npm_path = resolve_executable("npm", self.source_dir)
            node_path = resolve_executable("node", self.source_dir)
            python_path = resolve_executable(sys.executable, self.source_dir)
            self.report["toolchain"] = {
                "compiler": {
                    "requested_argv": requested_compiler,
                    "effective_argv": compiler,
                    "resolved_path": str(compiler_path),
                    "sha256": sha256_path(compiler_path),
                },
                "make": {
                    "resolved_path": str(make_path),
                    "sha256": sha256_path(make_path),
                },
                "npm": {
                    "resolved_path": str(npm_path),
                    "sha256": sha256_path(npm_path),
                },
                "node": {
                    "resolved_path": str(node_path),
                    "sha256": sha256_path(node_path),
                },
                "python": {
                    "resolved_path": str(python_path),
                    "sha256": sha256_path(python_path),
                },
            }
            toolchain_bin = self.runtime_dir / "toolchain-bin"
            toolchain_bin.mkdir()
            for name, path in (
                ("npm", npm_path),
                ("node", node_path),
                ("python3", python_path),
            ):
                os.symlink(path, toolchain_bin / name)
            self.report["toolchain"]["pinned_path_prefix"] = str(
                toolchain_bin.relative_to(self.root)
            )
            self.report["toolchain"]["pinned_path_targets"] = {
                "npm": str(npm_path),
                "node": str(node_path),
                "python3": str(python_path),
            }
            self._write_report()
            parser_test_environment = os.environ.copy()
            parser_test_environment["PYTHONDONTWRITEBYTECODE"] = "1"
            cache_query_environment = parser_test_environment.copy()
            cache_query_environment["PATH"] = (
                str(toolchain_bin)
                + os.pathsep
                + cache_query_environment.get("PATH", "")
            )
            npm_cache = resolve_npm_cache(
                npm_path,
                self.source_dir,
                cache_query_environment,
            )
            self.report["dependencies"]["npm_cache"] = str(npm_cache)
            self._write_report()
            make_test_environment = archived_test_environment(
                parser_test_environment,
                compiler,
                toolchain_bin,
                npm_cache,
            )
            self.run_stage(
                "compiler-version",
                [*compiler, "--version"],
                cwd=self.source_dir,
            )
            self.run_source_stage(
                "certification-self-tests",
                [
                    str(python_path),
                    "-m",
                    "unittest",
                    "tests/test_certify.py",
                ],
                environment=parser_test_environment,
            )
            # -B prevents ignored, future-mtime test binaries from satisfying
            # the test gate without being rebuilt from the clean source tree;
            # fixed parallelism keeps the recorded gate reproducible.
            self.run_source_stage(
                "make-test",
                make_test_command(make_path),
                environment=make_test_environment,
            )
            simulator = self.runtime_dir / "old-school-sim"
            self.run_source_stage(
                "release-build",
                release_build_command(compiler, simulator),
            )
            if not simulator.is_file():
                raise InfrastructureError(
                    "explicit source-snapshot compile did not produce simulator"
                )
            self.report["run"]["simulator_sha256"] = sha256_path(simulator)
            self._write_report()
            sanitizer_binary = self.runtime_dir / "old-school-tests-sanitize"
            self.run_source_stage(
                "asan-ubsan-build",
                sanitizer_build_command(compiler, sanitizer_binary),
            )
            sanitizer_environment = os.environ.copy()
            sanitizer_environment["ASAN_OPTIONS"] = "detect_leaks=0"
            sanitizer_environment["UBSAN_OPTIONS"] = "halt_on_error=1"
            self.run_stage(
                "asan-ubsan-tests",
                [str(sanitizer_binary)],
                environment=sanitizer_environment,
                cwd=self.runtime_dir,
            )
            self.report["criteria"]["tests_and_sanitizers"] = True
            self._write_report()

            self.verify_integrity("before-artifact-load")
            _, artifact_log = self.run_stage(
                "artifact-load",
                artifact_load_command(simulator),
                cwd=self.runtime_dir,
            )
            self.verify_integrity("after-artifact-load")
            artifact_result = parse_artifact_log(
                artifact_log, self.fingerprint
            )
            artifact_path = self.runtime_dir / ARTIFACT_RELATIVE_PATH
            artifact_after = sha256_path(artifact_path)
            if (
                artifact_after
                != self.report["artifact"]["runtime_sha256_before"]
            ):
                raise ContractError("artifact bytes changed during load stage")
            if artifact_result["cache_path_reported"] != str(
                ARTIFACT_RELATIVE_PATH
            ):
                raise ContractError(
                    "CLI reported an unexpected artifact cache path"
                )
            self.report["artifact"].update(artifact_result)
            self.report["artifact"]["runtime_sha256_after_load"] = artifact_after
            self._write_report()

            self.verify_integrity("before-primary-benchmark")
            benchmark_rc, benchmark_log = self.run_stage(
                "primary-benchmark",
                primary_benchmark_command(simulator, self.primary_seed),
                accepted_codes=(0, 1),
                cwd=self.runtime_dir,
            )
            self.verify_integrity("after-primary-benchmark")
            benchmark = parse_benchmark_log(
                benchmark_log, self.fingerprint, self.primary_seed
            )
            validate_primary_exit_code(benchmark_rc, benchmark)
            self.report["primary_benchmark"] = benchmark
            self.report["criteria"].update(
                {
                    "primary_aggregate_over_50": (
                        benchmark["record"]["win_rate"] > 0.5
                    ),
                    "primary_wilson_lower_over_50": (
                        benchmark["wilson95"]["low"] > 0.5
                    ),
                }
            )
            self._write_report()
            if not benchmark["smoke_gate_pass"]:
                self.report["status"] = "not-certified"
                self.report["reason"] = (
                    "primary aggregate/Wilson smoke rejected"
                )
                self._write_report(completed=True)
                print(
                    f"NOT CERTIFIED: primary benchmark; "
                    f"report {self.report_path}"
                )
                return 1

            self.verify_integrity("before-fixed-panel-stability")
            stability_rc, stability_log = self.run_stage(
                "fixed-panel-stability",
                fixed_panel_command(simulator),
                accepted_codes=(0, 1),
                cwd=self.runtime_dir,
            )
            self.verify_integrity("after-fixed-panel-stability")
            stability = parse_stability_log(
                stability_log, self.fingerprint
            )
            validate_stability_exit_code(stability_rc, stability)
            self.report["validation"] = stability
            pooled_direct = pool_direct_evidence(benchmark, stability)
            self.report["pooled_direct"] = pooled_direct
            self.report["criteria"].update(
                {
                    "mixed_lift_all_decks": (
                        stability["mixed_field"]["all_five"]
                    ),
                    "no_losing_validation_seed": (
                        stability["no_losing_seed"]
                    ),
                    "pooled_direct_aggregate_over_50": (
                        pooled_direct["aggregate_over_50"]
                    ),
                    "pooled_direct_wilson_lower_over_50": (
                        pooled_direct["wilson_lower_over_50"]
                    ),
                    "pooled_direct_wins_all_decks": (
                        pooled_direct["direct_wins_all_decks"]
                    ),
                }
            )
            self._write_report()
            if (
                not stability["panel_gate_pass"]
                or not pooled_direct["gate_pass"]
            ):
                self.report["status"] = "not-certified"
                self.report["reason"] = (
                    "fixed-panel or pooled-direct gate rejected"
                )
                self._write_report(completed=True)
                print(
                    f"NOT CERTIFIED: fixed-panel/pooled-direct; "
                    f"report {self.report_path}"
                )
                return 1

            self.verify_integrity("final")
            artifact_final = sha256_path(
                self.runtime_dir / ARTIFACT_RELATIVE_PATH
            )
            self.report["artifact"][
                "runtime_sha256_after_evaluation"
            ] = artifact_final
            self.report["certified"] = all(self.report["criteria"].values())
            self.report["status"] = (
                "certified" if self.report["certified"] else "not-certified"
            )
            self._write_report(completed=True)
            print(
                f"{'CERTIFIED' if self.report['certified'] else 'NOT CERTIFIED'}; "
                f"report {self.report_path}"
            )
            return 0 if self.report["certified"] else 1
        except (ContractError, InfrastructureError) as error:
            self.report["status"] = "infrastructure-incomplete"
            self.report["certified"] = False
            self.report["error"] = str(error)
            self._write_report(completed=True)
            print(f"certification incomplete: {error}", file=sys.stderr)
            if self.report_path is not None:
                print(f"report: {self.report_path}", file=sys.stderr)
            return 2
        except Exception as error:  # pragma: no cover - last-resort fail closed
            self.report["status"] = "infrastructure-incomplete"
            self.report["certified"] = False
            self.report["error"] = (
                f"unexpected {type(error).__name__}: {error}"
            )
            self._write_report(completed=True)
            print(
                f"certification incomplete: unexpected failure: {error}",
                file=sys.stderr,
            )
            if self.report_path is not None:
                print(f"report: {self.report_path}", file=sys.stderr)
            return 3


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Certify the pinned C16/T800/seed424242/K8 policy. "
            "Exit 0 means certified, 1 means scientific rejection, "
            "and 2+ means incomplete infrastructure/evidence."
        )
    )
    parser.add_argument("primary_seed", type=int)
    parser.add_argument(
        "expected_fingerprint",
        help="required full 64-character lowercase model fingerprint",
    )
    args = parser.parse_args(argv)
    if args.primary_seed < 0 or args.primary_seed > (1 << 64) - 1:
        parser.error("primary_seed must fit uint64")
    if not re.fullmatch(FINGERPRINT_RE, args.expected_fingerprint):
        parser.error(
            "expected_fingerprint must be 64 lowercase hexadecimal characters"
        )
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    root = Path(__file__).resolve().parent.parent
    return CertificationRunner(
        root, args.primary_seed, args.expected_fingerprint
    ).execute()


if __name__ == "__main__":
    raise SystemExit(main())
