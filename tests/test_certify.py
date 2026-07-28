#!/usr/bin/env python3
"""Fast synthetic-contract tests for tools/certify.py."""

from __future__ import annotations

import copy
import importlib.util
import re
import subprocess
import sys
import tempfile
import unittest
from datetime import timedelta
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location(
    "certify", ROOT / "tools" / "certify.py"
)
assert SPEC is not None and SPEC.loader is not None
certify = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = certify
SPEC.loader.exec_module(certify)

FINGERPRINT = "a" * 64
CACHE = (
    "build/model-cache/"
    "old-school-value-challenger-v3-c16-t800-s424242.bin"
)


def interval(wins: int, games: int) -> tuple[float, float]:
    low, high = certify.wilson95(wins, games)
    return round(low * 100, 1), round(high * 100, 1)


def identity() -> str:
    return (
        "Loading immutable Value Challenger C16 artifact\n"
        f"  Value Challenger C16 fingerprint: {FINGERPRINT}\n"
        f"  Value Challenger C16 artifact cache: loaded {CACHE}\n"
    )


def artifact_log() -> str:
    return (
        identity()
        + "Training seed: 424242\n"
        + "Frozen learned model: Learned Value Challenger C16, "
        + "seed 424242, 800 training games, K=8\n"
    )


def exact_deck_matrix_rows(
    header: str,
    row_wins: dict[str, int],
    repetitions: int,
) -> list[str]:
    deck_count = len(certify.DECKS)
    source = 0
    row_offset = 1
    column_offset = row_offset + deck_count
    sink = column_offset + deck_count
    size = sink + 1
    capacity = [[0 for _ in range(size)] for _ in range(size)]

    def add_edge(first: int, second: int, amount: int) -> None:
        capacity[first][second] = amount

    for index, deck in enumerate(certify.DECKS):
        add_edge(source, row_offset + index, row_wins[deck])
        add_edge(column_offset + index, sink, row_wins[deck])
    cell_games: dict[tuple[int, int], int] = {}
    for row in range(deck_count):
        for column in range(deck_count):
            games = repetitions * (4 if row == column else 2)
            cell_games[(row, column)] = games
            add_edge(row_offset + row, column_offset + column, games)

    residual = [row[:] for row in capacity]
    total_flow = 0
    while True:
        parent = [-1] * size
        parent[source] = source
        queue = [source]
        cursor = 0
        while cursor < len(queue) and parent[sink] == -1:
            node = queue[cursor]
            cursor += 1
            for neighbor, available in enumerate(residual[node]):
                if available > 0 and parent[neighbor] == -1:
                    parent[neighbor] = node
                    queue.append(neighbor)
        if parent[sink] == -1:
            break
        amount = max(row_wins.values())
        node = sink
        while node != source:
            amount = min(amount, residual[parent[node]][node])
            node = parent[node]
        node = sink
        while node != source:
            previous = parent[node]
            residual[previous][node] -= amount
            residual[node][previous] += amount
            node = previous
        total_flow += amount
    if total_flow != sum(row_wins.values()):
        raise AssertionError("synthetic deck matrix is infeasible")

    lines = [header]
    for row, challenger in enumerate(certify.DECKS):
        for column, baseline in enumerate(certify.DECKS):
            games = cell_games[(row, column)]
            wins = (
                capacity[row_offset + row][column_offset + column]
                - residual[row_offset + row][column_offset + column]
            )
            lines.append(
                f"  {challenger} vs {baseline}: "
                f"{wins}-{games - wins}-0 ({games} games)"
            )
    return lines


def benchmark_log(
    *,
    passing: bool = True,
    duplicate_ru: bool = False,
    row_wins: dict[str, int] | None = None,
) -> str:
    if row_wins is None:
        row_wins = (
            {
                "Green": 220,
                "Red": 225,
                "Blue": 215,
                "White": 230,
                "RU Aggro": 230,
            }
            if passing
            else {
                "Green": 190,
                "Red": 195,
                "Blue": 200,
                "White": 205,
                "RU Aggro": 210,
            }
        )
    if set(row_wins) != set(certify.DECKS):
        raise AssertionError("benchmark fixture deck set is incomplete")
    games_per_deck = certify.PRIMARY_TOTAL_GAMES // len(certify.DECKS)
    records = {
        name: (row_wins[name], games_per_deck - row_wins[name])
        for name in certify.DECKS
    }
    wins = sum(row[0] for row in records.values())
    losses = sum(row[1] for row in records.values())
    low, high = interval(wins, 2040)
    exact_low, _ = certify.wilson95(wins, certify.PRIMARY_TOTAL_GAMES)
    cli_pass = exact_low > 0.5 and all(
        challenger_wins > baseline_wins
        for challenger_wins, baseline_wins in records.values()
    )
    verdict = (
        "PASS — aggregate lower bound exceeds 50% and challenger wins "
        "on all five decks"
        if cli_pass
        else "FAIL — baseline is better at 95% confidence"
    )
    lines = [
        identity().rstrip(),
        "Old School Magic Bot Benchmark",
        "Evaluation seed: 314159",
        "Training seed: 424242",
        "Challenger: Learned Value Challenger C16",
        "Baseline: Handcrafted Policy",
        (
            "Challenger frozen model: Learned Value Challenger C16, "
            "seed 424242, 800 training games, K=8"
        ),
        "Repetitions per unordered deck pairing: 34",
        "Total paired games: 2040",
        f"  Challenger record: {wins}-{losses}-0 ({wins / 20.4:.1f}% wins)",
        f"  Approximate 95% confidence interval: {low:.1f}% to {high:.1f}%",
        f"  Verdict: {verdict}",
        "By challenger deck",
    ]
    for name in certify.DECKS:
        challenger_wins, baseline_wins = records[name]
        lines.append(
            f"  {name}: challenger {challenger_wins / 4.08:.1f}% "
            f"({challenger_wins}-{baseline_wins}-0), baseline "
            f"{baseline_wins / 4.08:.1f}% "
            f"({baseline_wins}-{challenger_wins}-0)"
        )
    if duplicate_ru:
        lines.append(lines[-1])
    lines.extend(
        exact_deck_matrix_rows(
            (
                "Exact challenger-deck x baseline-deck matrix "
                "(challenger perspective)"
            ),
            row_wins,
            certify.PRIMARY_REPETITIONS,
        )
    )
    return "\n".join(lines) + "\n"


def stability_log(
    *,
    missing_seed: bool = False,
    duplicate_ru: bool = False,
    omit_exact_row: bool = False,
    overall_pass: bool = True,
    row_wins: dict[str, int] | None = None,
    seed_wins: int = 160,
) -> str:
    if row_wins is None:
        row_wins = {name: 256 for name in certify.DECKS}
    if set(row_wins) != set(certify.DECKS):
        raise AssertionError("stability fixture deck set is incomplete")
    games_per_seed = certify.PANEL_REPETITIONS * 60
    seed_losses = games_per_seed - seed_wins
    pooled_wins = seed_wins * len(certify.PANEL_SEEDS)
    pooled_losses = seed_losses * len(certify.PANEL_SEEDS)
    if sum(row_wins.values()) != pooled_wins:
        raise AssertionError(
            "stability fixture rows do not sum to its per-seed pool"
        )
    low, high = interval(pooled_wins, certify.PANEL_TOTAL_DIRECT_GAMES)
    seeds = certify.PANEL_SEEDS[:-1] if missing_seed else certify.PANEL_SEEDS
    lines = [
        identity().rstrip(),
        "Learned Value Challenger All-Policy Stability Panel",
        "Runs: 8",
        "Evaluation base seed: 0",
        "Training seed: 424242",
        "Repetitions per unordered deck pairing per run: 5",
        "Learned model: Challenger C16",
        "Learned search worlds per legal action: 8",
    ]
    for seed in seeds:
        lines.extend(
            [
                f"  Evaluation seed {seed}:",
                "    vs Random: 200-100-0 (66.7%) PASS",
                "    vs Monte Carlo: 190-110-0 (63.3%) PASS",
                "    vs Deep Monte Carlo: 180-120-0 (60.0%) PASS",
                (
                    f"    vs Handcrafted Policy: "
                    f"{seed_wins}-{seed_losses}-0 "
                    f"({seed_wins / games_per_seed * 100.0:.1f}%) "
                    f"{'PASS' if seed_wins > seed_losses else 'FAIL'}"
                ),
                (
                    "    mixed-field lift: Green=PASS Red=PASS "
                    "Blue=PASS White=PASS RU Aggro=PASS => PASS"
                ),
            ]
        )
    lines.extend(
        [
            "Pooled results",
            (
                f"  vs Handcrafted Policy: {pooled_wins}-{pooled_losses}-0 "
                f"({pooled_wins / certify.PANEL_TOTAL_DIRECT_GAMES * 100.0:.1f}%, "
                f"95% interval {low:.1f}% to {high:.1f}%)"
            ),
        ]
    )
    for name in certify.DECKS:
        challenger_wins = row_wins[name]
        baseline_wins = (
            certify.PANEL_TOTAL_DIRECT_GAMES // len(certify.DECKS)
            - challenger_wins
        )
        lines.append(
            f"    {name}: {challenger_wins} vs {baseline_wins} "
            f"{'PASS' if challenger_wins > baseline_wins else 'FAIL'}"
        )
    lines.append("    Seeds 8/8, confidence PASS, decks PASS => PASS")
    lines.extend(
        exact_deck_matrix_rows(
            (
                "Pooled Handcrafted exact challenger-deck x baseline-deck "
                "matrix (challenger perspective)"
            ),
            row_wins,
            len(certify.PANEL_SEEDS) * certify.PANEL_REPETITIONS,
        )
    )
    lines.append("Pooled mixed-field exact deck-policy counts")
    exact_records = {
        "Random": (224, 416),
        "Monte Carlo": (288, 352),
        "Deep Monte Carlo": (320, 320),
        "Handcrafted Policy": (352, 288),
        "Learned Value": (416, 224),
    }
    exact_rows: list[str] = []
    for name in certify.DECKS:
        for policy in certify.POLICIES:
            wins, losses = exact_records[policy]
            exact_rows.append(
                f"  {name} | {policy}: {wins}-{losses}-0 (640 games)"
            )
    if omit_exact_row:
        exact_rows.pop()
    lines.extend(exact_rows)
    lines.append("Pooled mixed-field lift over Random")
    for name in certify.DECKS:
        lines.append(
            f"  {name}: Learned +30.0 pp, best other Handcrafted Policy "
            "+20.0 pp PASS"
        )
    if duplicate_ru:
        lines.append(lines[-1])
    lines.extend(
        [
            "  Mixed-field seeds: 8/8",
            "  Per-deck pooled lift gate: PASS",
            "All-policy seed verdict: 8/8",
            f"Overall: {'PASS' if overall_pass else 'FAIL'}",
        ]
    )
    return "\n".join(lines) + "\n"


class CertificationParserTests(unittest.TestCase):
    def test_commands_pin_the_candidate_and_sample_sizes(self) -> None:
        simulator = Path("/tmp/old-school-sim")
        artifact = certify.artifact_load_command(simulator)
        benchmark = certify.primary_benchmark_command(simulator, 314159)
        stability = certify.fixed_panel_command(simulator)
        self.assertEqual(
            certify.make_test_command(Path("/tmp/make")),
            ["/tmp/make", "-B", "-j4", "test"],
        )
        self.assertEqual(
            artifact[artifact.index("--learned-generations") + 1], "16"
        )
        self.assertEqual(
            stability[stability.index("--learned-generations") + 1], "16"
        )
        self.assertEqual(
            benchmark[benchmark.index("--challenger") + 1],
            "learned-value-c16",
        )
        self.assertEqual(benchmark[benchmark.index("--games") + 1], "34")
        self.assertEqual(
            stability[stability.index("--stability-runs") + 1], "8"
        )
        self.assertEqual(stability[stability.index("--games") + 1], "5")
        self.assertEqual(stability[stability.index("--seed") + 1], "0")
        for command in (artifact, benchmark, stability):
            self.assertEqual(
                command[command.index("--train-games") + 1], "800"
            )
            self.assertEqual(
                command[command.index("--train-seed") + 1], "424242"
            )
            self.assertEqual(
                command[command.index("--learned-rollouts") + 1], "8"
            )
        self.assertEqual(
            stability[stability.index("--rollouts") + 1], "2"
        )
        self.assertEqual(
            stability[stability.index("--deep-rollouts") + 1], "8"
        )
        release = certify.release_build_command(
            ["clang++"], Path("/tmp/runtime/old-school-sim")
        )
        self.assertIn("src/main.cpp", release)
        self.assertIn("-Werror", release)
        self.assertEqual(release[-2:], ["-o", "/tmp/runtime/old-school-sim"])

    def test_archived_tests_use_a_preexisting_cache_strictly_offline(
        self,
    ) -> None:
        environment = certify.archived_test_environment(
            {
                "PATH": "/usr/bin",
                "PRESERVED": "yes",
                "NPM_CONFIG_CACHE": "/discarded-empty-cache",
            },
            ["clang++", "-O3"],
            Path("/pinned-tools"),
            Path("/existing-npm-cache"),
        )
        self.assertEqual(environment["PRESERVED"], "yes")
        self.assertEqual(environment["CXX"], "clang++ -O3")
        self.assertEqual(
            environment["PATH"], "/pinned-tools:/usr/bin"
        )
        self.assertEqual(
            environment["NPM_CONFIG_CACHE"], "/existing-npm-cache"
        )
        self.assertEqual(environment["NPM_CONFIG_OFFLINE"], "true")
        self.assertEqual(environment["NPM_CONFIG_IGNORE_SCRIPTS"], "true")
        self.assertEqual(environment["NPM_CONFIG_AUDIT"], "false")
        self.assertEqual(environment["NPM_CONFIG_FUND"], "false")

    def test_npm_cache_resolution_requires_one_existing_absolute_path(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cache = root / "cache"
            cache.mkdir()
            npm = root / "npm"
            npm.write_text(
                "#!/bin/sh\nprintf '%s\\n' \"$CERTIFY_TEST_CACHE\"\n",
                encoding="utf-8",
            )
            npm.chmod(0o755)
            environment = {"CERTIFY_TEST_CACHE": str(cache), "PATH": "/usr/bin"}
            self.assertEqual(
                certify.resolve_npm_cache(npm, root, environment),
                cache,
            )
            environment["CERTIFY_TEST_CACHE"] = "relative-cache"
            with self.assertRaises(certify.InfrastructureError):
                certify.resolve_npm_cache(npm, root, environment)

    def test_artifact_requires_loaded_expected_fingerprint(self) -> None:
        result = certify.parse_artifact_log(artifact_log(), FINGERPRINT)
        self.assertTrue(result["loaded"])
        with self.assertRaises(certify.ContractError):
            certify.parse_artifact_log(
                artifact_log().replace("loaded", "generated"), FINGERPRINT
            )

    def test_primary_benchmark_pass(self) -> None:
        result = certify.parse_benchmark_log(
            benchmark_log(), FINGERPRINT, 314159
        )
        self.assertTrue(result["smoke_gate_pass"])
        self.assertTrue(result["standalone_gate_pass"])
        self.assertTrue(result["cli_verdict_pass"])
        self.assertEqual(set(result["per_deck"]), set(certify.DECKS))
        self.assertEqual(result["total_games"], 2040)
        matrix = result["exact_deck_matrix"]
        self.assertEqual(
            matrix["challenger_rows"]["Green"],
            {"wins": 220, "losses": 188, "draws": 0, "games": 408},
        )
        self.assertEqual(
            matrix["baseline_columns"]["Green"],
            {"wins": 188, "losses": 220, "draws": 0, "games": 408},
        )
        self.assertEqual(
            matrix["aggregate"],
            {"wins": 1120, "losses": 920, "draws": 0, "games": 2040},
        )
        self.assertEqual(
            matrix["cells"]["Green"]["Green"]["games"],
            4 * certify.PRIMARY_REPETITIONS,
        )
        self.assertEqual(
            matrix["cells"]["Green"]["Red"]["games"],
            2 * certify.PRIMARY_REPETITIONS,
        )

    def test_primary_requires_exactly_one_complete_matrix(self) -> None:
        header = (
            "Exact challenger-deck x baseline-deck matrix "
            "(challenger perspective)"
        )
        rows = exact_deck_matrix_rows(
            header,
            {
                "Green": 220,
                "Red": 225,
                "Blue": 215,
                "White": 230,
                "RU Aggro": 230,
            },
            certify.PRIMARY_REPETITIONS,
        )
        missing = benchmark_log().replace("\n".join(rows), "")
        with self.assertRaises(certify.ContractError):
            certify.parse_benchmark_log(missing, FINGERPRINT, 314159)
        duplicate_header = benchmark_log().replace(
            header, f"{header}\n{header}", 1
        )
        with self.assertRaises(certify.ContractError):
            certify.parse_benchmark_log(
                duplicate_header, FINGERPRINT, 314159
            )
        duplicate_cell = benchmark_log().replace(
            "  Green vs Red:", "  Red vs Green:", 1
        )
        with self.assertRaises(certify.ContractError):
            certify.parse_benchmark_log(
                duplicate_cell, FINGERPRINT, 314159
            )

    def test_primary_rejects_matrix_cell_game_contract_mismatch(self) -> None:
        malformed_outcome_total = benchmark_log().replace(
            "(68 games)", "(67 games)", 1
        )
        with self.assertRaisesRegex(
            certify.ContractError, "outcomes"
        ):
            certify.parse_benchmark_log(
                malformed_outcome_total, FINGERPRINT, 314159
            )
        row = next(
            line
            for line in benchmark_log().splitlines()
            if line.startswith("  Green vs Blue:")
        )
        match = re.fullmatch(
            r"  Green vs Blue: (\d+)-(\d+)-0 \(68 games\)", row
        )
        assert match is not None
        wins = int(match.group(1))
        losses = int(match.group(2))
        malformed_expected_size = benchmark_log().replace(
            row,
            (
                f"  Green vs Blue: {wins}-{losses - 1}-0 "
                "(67 games)"
            ),
            1,
        )
        with self.assertRaisesRegex(
            certify.ContractError, "expected 68"
        ):
            certify.parse_benchmark_log(
                malformed_expected_size, FINGERPRINT, 314159
            )

    def test_primary_rejects_structurally_impossible_marginals(self) -> None:
        malformed = benchmark_log()
        malformed = malformed.replace(
            "Green: challenger 53.9% (220-188-0), baseline "
            "46.1% (188-220-0)",
            "Green: challenger 54.2% (221-187-0), baseline "
            "46.1% (188-220-0)",
        )
        malformed = malformed.replace(
            "Red: challenger 55.1% (225-183-0), baseline "
            "44.9% (183-225-0)",
            "Red: challenger 54.9% (224-184-0), baseline "
            "44.9% (183-225-0)",
        )
        with self.assertRaisesRegex(
            certify.ContractError, "marginal disagrees with exact matrix"
        ):
            certify.parse_benchmark_log(
                malformed, FINGERPRINT, 314159
            )

    def test_primary_rejects_percentage_that_contradicts_counts(self) -> None:
        malformed = benchmark_log().replace(
            "Challenger record: 1120-920-0 (54.9% wins)",
            "Challenger record: 1120-920-0 (99.9% wins)",
        )
        with self.assertRaisesRegex(
            certify.ContractError, "percentage"
        ):
            certify.parse_benchmark_log(
                malformed, FINGERPRINT, 314159
            )

    def test_primary_rejects_deck_percentage_that_contradicts_counts(
        self,
    ) -> None:
        malformed = benchmark_log().replace(
            "Green: challenger 53.9%",
            "Green: challenger 99.9%",
        )
        with self.assertRaisesRegex(
            certify.ContractError, "percentage"
        ):
            certify.parse_benchmark_log(
                malformed, FINGERPRINT, 314159
            )

    def test_primary_scientific_rejection_is_valid_evidence(self) -> None:
        result = certify.parse_benchmark_log(
            benchmark_log(passing=False), FINGERPRINT, 314159
        )
        self.assertFalse(result["smoke_gate_pass"])
        self.assertFalse(result["standalone_gate_pass"])
        self.assertFalse(result["cli_verdict_pass"])

    def test_primary_rejects_duplicate_deck(self) -> None:
        with self.assertRaises(certify.ContractError):
            certify.parse_benchmark_log(
                benchmark_log(duplicate_ru=True), FINGERPRINT, 314159
            )

    def test_primary_rejects_deck_component_total_mismatch(self) -> None:
        malformed = benchmark_log().replace(
            "Green: challenger 53.9% (220-188-0), baseline "
            "46.1% (188-220-0)",
            "Green: challenger 53.9% (220-0-188), baseline "
            "46.1% (188-0-220)",
        )
        with self.assertRaises(certify.ContractError):
            certify.parse_benchmark_log(
                malformed, FINGERPRINT, 314159
            )

    def test_stability_pass(self) -> None:
        result = certify.parse_stability_log(
            stability_log(), FINGERPRINT
        )
        self.assertTrue(result["standalone_gate_pass"])
        self.assertTrue(result["panel_gate_pass"])
        self.assertEqual(result["seeds"], list(certify.PANEL_SEEDS))
        self.assertTrue(result["mixed_field"]["all_five"])
        self.assertEqual(
            result["mixed_field"]["per_deck"]["Green"]["best_other"],
            "Handcrafted Policy",
        )
        matrix = result["pooled_handcrafted"]["exact_deck_matrix"]
        self.assertEqual(
            matrix["challenger_rows"]["Blue"],
            {"wins": 256, "losses": 224, "draws": 0, "games": 480},
        )
        self.assertEqual(
            matrix["baseline_columns"]["Blue"],
            {"wins": 224, "losses": 256, "draws": 0, "games": 480},
        )
        self.assertEqual(
            matrix["aggregate"],
            {"wins": 1280, "losses": 1120, "draws": 0, "games": 2400},
        )

    def test_stability_requires_exactly_one_complete_matrix(self) -> None:
        header = (
            "Pooled Handcrafted exact challenger-deck x baseline-deck "
            "matrix (challenger perspective)"
        )
        rows = exact_deck_matrix_rows(
            header,
            {name: 256 for name in certify.DECKS},
            len(certify.PANEL_SEEDS) * certify.PANEL_REPETITIONS,
        )
        missing = stability_log().replace("\n".join(rows), "")
        with self.assertRaises(certify.ContractError):
            certify.parse_stability_log(missing, FINGERPRINT)
        duplicate_header = stability_log().replace(
            header, f"{header}\n{header}", 1
        )
        with self.assertRaises(certify.ContractError):
            certify.parse_stability_log(duplicate_header, FINGERPRINT)

    def test_stability_rejects_structurally_impossible_marginals(
        self,
    ) -> None:
        malformed = stability_log().replace(
            "    Green: 256 vs 224 PASS",
            "    Green: 257 vs 224 PASS",
        ).replace(
            "    Red: 256 vs 224 PASS",
            "    Red: 255 vs 224 PASS",
        )
        with self.assertRaisesRegex(
            certify.ContractError, "wins disagree with exact matrix"
        ):
            certify.parse_stability_log(malformed, FINGERPRINT)

    def test_stability_rejects_percentage_that_contradicts_counts(
        self,
    ) -> None:
        malformed = stability_log().replace(
            "vs Random: 200-100-0 (66.7%) PASS",
            "vs Random: 200-100-0 (99.9%) PASS",
            1,
        )
        with self.assertRaisesRegex(
            certify.ContractError, "percentage"
        ):
            certify.parse_stability_log(malformed, FINGERPRINT)

    def test_stability_rejects_missing_seed(self) -> None:
        with self.assertRaises(certify.ContractError):
            certify.parse_stability_log(
                stability_log(missing_seed=True), FINGERPRINT
            )

    def test_stability_rejects_duplicate_lift_deck(self) -> None:
        with self.assertRaises(certify.ContractError):
            certify.parse_stability_log(
                stability_log(duplicate_ru=True), FINGERPRINT
            )

    def test_stability_requires_every_exact_count_row(self) -> None:
        with self.assertRaises(certify.ContractError):
            certify.parse_stability_log(
                stability_log(omit_exact_row=True), FINGERPRINT
            )

    def test_stability_rejects_lift_contradicting_exact_counts(self) -> None:
        malformed = stability_log().replace(
            "Learned +30.0 pp, best other Handcrafted Policy +20.0 pp PASS",
            "Learned +1.0 pp, best other Bogus +99.0 pp PASS",
        )
        with self.assertRaises(certify.ContractError):
            certify.parse_stability_log(malformed, FINGERPRINT)

    def test_stability_accepts_one_decimal_lift_rounding(self) -> None:
        rounded = stability_log()
        replacements = {
            "Green | Random: 224-416-0": "Green | Random: 221-419-0",
            "Green | Monte Carlo: 288-352-0":
                "Green | Monte Carlo: 289-351-0",
            "Green | Deep Monte Carlo: 320-320-0":
                "Green | Deep Monte Carlo: 319-321-0",
            "Green | Handcrafted Policy: 352-288-0":
                "Green | Handcrafted Policy: 353-287-0",
            "Green | Learned Value: 416-224-0":
                "Green | Learned Value: 418-222-0",
            "Green: Learned +30.0 pp, best other Handcrafted Policy "
            "+20.0 pp PASS":
                "Green: Learned +30.8 pp, best other Handcrafted Policy "
                "+20.6 pp PASS",
        }
        for original, replacement in replacements.items():
            rounded = rounded.replace(original, replacement)
        result = certify.parse_stability_log(rounded, FINGERPRINT)
        self.assertTrue(result["standalone_gate_pass"])
        self.assertTrue(result["panel_gate_pass"])

    def test_stability_rejects_seed_pool_cross_accounting_mismatch(self) -> None:
        malformed = stability_log().replace(
            "vs Handcrafted Policy: 160-140-0 (53.3%) PASS",
            "vs Handcrafted Policy: 151-149-0 (50.3%) PASS",
        )
        with self.assertRaises(certify.ContractError):
            certify.parse_stability_log(malformed, FINGERPRINT)

    def test_stability_rejects_deck_pool_cross_accounting_mismatch(self) -> None:
        malformed = stability_log().replace(
            "    Green: 256 vs 224 PASS",
            "    Green: 1 vs 0 PASS",
        )
        with self.assertRaises(certify.ContractError):
            certify.parse_stability_log(malformed, FINGERPRINT)

    def test_stricter_cli_overall_failure_is_descriptive(self) -> None:
        result = certify.parse_stability_log(
            stability_log(overall_pass=False), FINGERPRINT
        )
        self.assertTrue(result["standalone_gate_pass"])
        self.assertTrue(result["panel_gate_pass"])
        self.assertFalse(result["cli_overall_pass"])
        certify.validate_stability_exit_code(1, result)

    def test_tied_validation_seed_is_not_a_losing_seed(self) -> None:
        tied = stability_log(overall_pass=False).replace(
            "vs Handcrafted Policy: 160-140-0 (53.3%) PASS",
            "vs Handcrafted Policy: 150-150-0 (50.0%) FAIL",
            1,
        )
        old_low, old_high = interval(1280, 2400)
        new_low, new_high = interval(1270, 2400)
        tied = tied.replace(
            "vs Handcrafted Policy: 1280-1120-0 "
            f"(53.3%, 95% interval {old_low:.1f}% to {old_high:.1f}%)",
            "vs Handcrafted Policy: 1270-1130-0 "
            f"(52.9%, 95% interval {new_low:.1f}% to {new_high:.1f}%)",
        )
        tied = tied.replace(
            "    Green: 256 vs 224 PASS",
            "    Green: 246 vs 234 PASS",
        )
        matrix_header = (
            "Pooled Handcrafted exact challenger-deck x baseline-deck "
            "matrix (challenger perspective)"
        )
        old_matrix = "\n".join(
            exact_deck_matrix_rows(
                matrix_header,
                {name: 256 for name in certify.DECKS},
                len(certify.PANEL_SEEDS) * certify.PANEL_REPETITIONS,
            )
        )
        new_matrix = "\n".join(
            exact_deck_matrix_rows(
                matrix_header,
                {
                    name: 246 if name == "Green" else 256
                    for name in certify.DECKS
                },
                len(certify.PANEL_SEEDS) * certify.PANEL_REPETITIONS,
            )
        )
        tied = tied.replace(old_matrix, new_matrix)
        tied = tied.replace(
            "    Seeds 8/8, confidence PASS, decks PASS => PASS",
            "    Seeds 7/8, confidence PASS, decks PASS => FAIL",
        )
        tied = tied.replace(
            "All-policy seed verdict: 8/8",
            "All-policy seed verdict: 7/8",
        )
        result = certify.parse_stability_log(tied, FINGERPRINT)
        self.assertTrue(result["no_losing_seed"])
        self.assertTrue(result["standalone_gate_pass"])
        self.assertTrue(result["panel_gate_pass"])
        self.assertFalse(result["cli_overall_pass"])

    def test_pooled_direct_exact_component_cross_sums(self) -> None:
        primary = certify.parse_benchmark_log(
            benchmark_log(), FINGERPRINT, 314159
        )
        stability = certify.parse_stability_log(
            stability_log(), FINGERPRINT
        )
        pooled = certify.pool_direct_evidence(primary, stability)

        self.assertEqual(
            pooled["source_repetitions"],
            {"primary": 34, "fixed_panel": 40},
        )
        self.assertEqual(pooled["repetitions"], 74)
        self.assertEqual(pooled["total_games"], 4440)
        self.assertEqual(pooled["games_per_deck"], 888)
        self.assertEqual(pooled["diagonal_games_per_cell"], 296)
        self.assertEqual(pooled["off_diagonal_games_per_cell"], 148)
        self.assertEqual(
            pooled["record"],
            {
                "wins": 2400,
                "losses": 2040,
                "draws": 0,
                "games": 4440,
                "win_rate": 2400 / 4440,
            },
        )
        self.assertTrue(pooled["gate_pass"])

        primary_matrix = primary["exact_deck_matrix"]
        panel_matrix = stability["pooled_handcrafted"][
            "exact_deck_matrix"
        ]
        matrix = pooled["exact_deck_matrix"]
        for challenger in certify.DECKS:
            self.assertEqual(
                matrix["challenger_rows"][challenger]["games"], 888
            )
            self.assertEqual(
                matrix["baseline_columns"][challenger]["games"], 888
            )
            for baseline in certify.DECKS:
                pooled_cell = matrix["cells"][challenger][baseline]
                primary_cell = primary_matrix["cells"][challenger][baseline]
                panel_cell = panel_matrix["cells"][challenger][baseline]
                for field in ("wins", "losses", "draws", "games"):
                    self.assertEqual(
                        pooled_cell[field],
                        primary_cell[field] + panel_cell[field],
                    )
                expected_games = 296 if challenger == baseline else 148
                self.assertEqual(pooled_cell["games"], expected_games)

        challenger_totals = {
            field: sum(
                matrix["challenger_rows"][deck][field]
                for deck in certify.DECKS
            )
            for field in ("wins", "losses", "draws", "games")
        }
        self.assertEqual(
            challenger_totals,
            {
                field: matrix["aggregate"][field]
                for field in ("wins", "losses", "draws", "games")
            },
        )
        baseline_totals = {
            field: sum(
                matrix["baseline_columns"][deck][field]
                for deck in certify.DECKS
            )
            for field in ("wins", "losses", "draws", "games")
        }
        self.assertEqual(baseline_totals["wins"], matrix["aggregate"]["losses"])
        self.assertEqual(
            baseline_totals["losses"], matrix["aggregate"]["wins"]
        )
        self.assertEqual(baseline_totals["draws"], matrix["aggregate"]["draws"])
        self.assertEqual(baseline_totals["games"], matrix["aggregate"]["games"])

    def test_primary_weak_slice_does_not_veto_repaired_pool(self) -> None:
        primary = certify.parse_benchmark_log(
            benchmark_log(
                row_wins={
                    "Green": 190,
                    "Red": 255,
                    "Blue": 215,
                    "White": 230,
                    "RU Aggro": 230,
                }
            ),
            FINGERPRINT,
            314159,
        )
        self.assertTrue(primary["smoke_gate_pass"])
        self.assertFalse(primary["standalone_gate_pass"])
        self.assertFalse(primary["cli_verdict_pass"])
        certify.validate_primary_exit_code(1, primary)
        with self.assertRaises(certify.ContractError):
            certify.validate_primary_exit_code(0, primary)

        stability = certify.parse_stability_log(
            stability_log(), FINGERPRINT
        )
        pooled = certify.pool_direct_evidence(primary, stability)
        self.assertEqual(pooled["per_deck"]["Green"]["challenger_wins"], 446)
        self.assertEqual(pooled["per_deck"]["Green"]["baseline_wins"], 442)
        self.assertTrue(pooled["direct_wins_all_decks"])
        self.assertTrue(pooled["gate_pass"])

    def test_panel_weak_slice_does_not_veto_repaired_pool(self) -> None:
        stability = certify.parse_stability_log(
            stability_log(
                overall_pass=False,
                row_wins={
                    "Green": 230,
                    "Red": 282,
                    "Blue": 256,
                    "White": 256,
                    "RU Aggro": 256,
                },
            ),
            FINGERPRINT,
        )
        self.assertFalse(stability["standalone_gate_pass"])
        self.assertTrue(stability["panel_gate_pass"])
        self.assertFalse(stability["cli_overall_pass"])
        certify.validate_stability_exit_code(1, stability)
        with self.assertRaises(certify.ContractError):
            certify.validate_stability_exit_code(0, stability)

        primary = certify.parse_benchmark_log(
            benchmark_log(), FINGERPRINT, 314159
        )
        pooled = certify.pool_direct_evidence(primary, stability)
        self.assertEqual(pooled["per_deck"]["Green"]["challenger_wins"], 450)
        self.assertEqual(pooled["per_deck"]["Green"]["baseline_wins"], 438)
        self.assertTrue(pooled["direct_wins_all_decks"])
        self.assertTrue(pooled["gate_pass"])

    def test_pooled_direct_strictly_rejects_deck_ties_and_losses(self) -> None:
        stability = certify.parse_stability_log(
            stability_log(), FINGERPRINT
        )
        for green_wins, red_wins, expected in (
            (188, 257, (444, 444)),
            (187, 258, (443, 445)),
        ):
            with self.subTest(green_wins=green_wins):
                primary = certify.parse_benchmark_log(
                    benchmark_log(
                        row_wins={
                            "Green": green_wins,
                            "Red": red_wins,
                            "Blue": 215,
                            "White": 230,
                            "RU Aggro": 230,
                        }
                    ),
                    FINGERPRINT,
                    314159,
                )
                self.assertTrue(primary["smoke_gate_pass"])
                self.assertFalse(primary["standalone_gate_pass"])
                pooled = certify.pool_direct_evidence(primary, stability)
                self.assertEqual(
                    (
                        pooled["per_deck"]["Green"]["challenger_wins"],
                        pooled["per_deck"]["Green"]["baseline_wins"],
                    ),
                    expected,
                )
                self.assertTrue(pooled["aggregate_over_50"])
                self.assertTrue(pooled["wilson_lower_over_50"])
                self.assertFalse(pooled["direct_wins_all_decks"])
                self.assertFalse(pooled["gate_pass"])

    def test_pooled_direct_wilson_failure_is_a_final_veto(self) -> None:
        primary = certify.parse_benchmark_log(
            benchmark_log(
                row_wins={name: 213 for name in certify.DECKS}
            ),
            FINGERPRINT,
            314159,
        )
        self.assertTrue(primary["smoke_gate_pass"])
        self.assertTrue(primary["standalone_gate_pass"])

        stability = certify.parse_stability_log(
            stability_log(
                overall_pass=False,
                row_wins={name: 240 for name in certify.DECKS},
                seed_wins=150,
            ),
            FINGERPRINT,
        )
        self.assertTrue(stability["no_losing_seed"])
        self.assertTrue(stability["panel_gate_pass"])
        self.assertFalse(stability["standalone_gate_pass"])
        pooled = certify.pool_direct_evidence(primary, stability)
        self.assertEqual(pooled["record"]["wins"], 2265)
        self.assertTrue(pooled["aggregate_over_50"])
        self.assertFalse(pooled["wilson_lower_over_50"])
        self.assertTrue(pooled["direct_wins_all_decks"])
        self.assertFalse(pooled["gate_pass"])

    def test_pooled_direct_rejects_tampered_source_cross_sums(self) -> None:
        primary = certify.parse_benchmark_log(
            benchmark_log(), FINGERPRINT, 314159
        )
        stability = certify.parse_stability_log(
            stability_log(), FINGERPRINT
        )

        tampered_cell = copy.deepcopy(primary)
        cell = tampered_cell["exact_deck_matrix"]["cells"]["Green"]["Red"]
        cell["wins"] -= 1
        cell["losses"] += 1
        with self.assertRaisesRegex(
            certify.ContractError, "do not reconstruct"
        ):
            certify.pool_direct_evidence(tampered_cell, stability)

        tampered_row = copy.deepcopy(primary)
        row = tampered_row["exact_deck_matrix"]["challenger_rows"]["Blue"]
        row["wins"] += 1
        row["losses"] -= 1
        with self.assertRaisesRegex(
            certify.ContractError, "do not reconstruct"
        ):
            certify.pool_direct_evidence(tampered_row, stability)

        tampered_aggregate = copy.deepcopy(stability)
        aggregate = tampered_aggregate["pooled_handcrafted"][
            "exact_deck_matrix"
        ]["aggregate"]
        aggregate["wins"] += 1
        aggregate["losses"] -= 1
        with self.assertRaisesRegex(
            certify.ContractError, "do not reconstruct"
        ):
            certify.pool_direct_evidence(primary, tampered_aggregate)

        tampered_consistent = copy.deepcopy(primary)
        source = certify._matrix_from_report(
            tampered_consistent["exact_deck_matrix"], "test source"
        )
        cells = {
            challenger: dict(source.cells[challenger])
            for challenger in certify.DECKS
        }
        original = cells["Green"]["Red"]
        cells["Green"]["Red"] = certify.Record(
            original.wins - 1,
            original.losses + 1,
            original.draws,
        )
        tampered_consistent["exact_deck_matrix"] = certify._matrix_report(
            certify._matrix_from_cells(cells, "tampered test matrix")
        )
        with self.assertRaisesRegex(
            certify.ContractError, "disagrees with its parsed aggregate"
        ):
            certify.pool_direct_evidence(tampered_consistent, stability)

    def test_exit_code_disagreement_is_incomplete_evidence(self) -> None:
        benchmark = certify.parse_benchmark_log(
            benchmark_log(), FINGERPRINT, 314159
        )
        with self.assertRaises(certify.ContractError):
            certify.validate_primary_exit_code(1, benchmark)
        stability = certify.parse_stability_log(
            stability_log(), FINGERPRINT
        )
        with self.assertRaises(certify.ContractError):
            certify.validate_stability_exit_code(1, stability)


class CertificationRunnerLifecycleTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self._git("init", "-q")
        self._git("config", "user.name", "Certification Test")
        self._git("config", "user.email", "certification@example.test")
        (self.root / ".gitignore").write_text(
            "build/\ncertification-runs/\n", encoding="utf-8"
        )
        (self.root / "tracked.txt").write_text("clean\n", encoding="utf-8")
        (self.root / "REVIEW.md").write_text(
            "# Independent review\n", encoding="utf-8"
        )
        for relative in (
            *certify.SIMULATOR_SOURCES,
            *certify.SIMULATOR_HEADERS,
            "tests/test_game.cpp",
        ):
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(f"// {relative}\n", encoding="utf-8")
        package_lock = self.root / "web/package-lock.json"
        package_lock.parent.mkdir(parents=True, exist_ok=True)
        package_lock.write_text(
            '{"lockfileVersion": 3}\n', encoding="utf-8"
        )
        self._git("add", ".")
        self._git("commit", "-q", "-m", "fixture")
        artifact = self.root / certify.ARTIFACT_RELATIVE_PATH
        artifact.parent.mkdir(parents=True)
        artifact.write_bytes(b"immutable artifact fixture")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _git(self, *args: str) -> str:
        result = subprocess.run(
            ["git", *args],
            cwd=self.root,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=True,
        )
        return result.stdout.strip()

    def _runner(self, seed: int = 987654321) -> object:
        return certify.CertificationRunner(self.root, seed, FINGERPRINT)

    def test_prepare_creates_durable_snapshot_and_runtime_artifact(self) -> None:
        runner = self._runner()
        runner.prepare()
        self.assertIsNotNone(runner.run_dir)
        self.assertIsNotNone(runner.source_dir)
        self.assertIsNotNone(runner.runtime_dir)
        assert runner.run_dir is not None
        assert runner.source_dir is not None
        assert runner.runtime_dir is not None
        self.assertEqual(
            runner.run_dir.parts[-3],
            certify.CERTIFICATION_ROOT.name,
        )
        self.assertTrue((runner.run_dir / "source.tar").is_file())
        self.assertTrue((runner.source_dir / "src/main.cpp").is_file())
        runtime_artifact = (
            runner.runtime_dir / certify.ARTIFACT_RELATIVE_PATH
        )
        self.assertTrue(runtime_artifact.is_file())
        self.assertEqual(
            certify.sha256_path(runtime_artifact),
            certify.sha256_path(
                self.root / certify.ARTIFACT_RELATIVE_PATH
            ),
        )
        self.assertIn(
            "make -B -j4 test",
            runner.report["dependencies"]["web_install"],
        )
        self.assertEqual(self._git("status", "--porcelain"), "")

    def test_prepare_tolerates_only_unstaged_review_drift(self) -> None:
        review = self.root / "REVIEW.md"
        review.write_text(
            "# Independent review\n\nnew external entry\n",
            encoding="utf-8",
        )
        runner = self._runner()
        runner.prepare()
        state = runner.report["run"]["worktree"]
        self.assertTrue(state["allowed"])
        self.assertTrue(state["tolerated_review_drift"])
        self.assertFalse(state["review_matches_head"])
        self.assertEqual(
            state["review_worktree_sha256"],
            certify.sha256_path(review),
        )

    def test_prepare_rejects_dirty_worktree(self) -> None:
        (self.root / "tracked.txt").write_text("dirty\n", encoding="utf-8")
        with self.assertRaises(certify.InfrastructureError):
            self._runner().prepare()

    def test_prepare_rejects_staged_review_change(self) -> None:
        (self.root / "REVIEW.md").write_text(
            "# staged review\n", encoding="utf-8"
        )
        self._git("add", "REVIEW.md")
        with self.assertRaises(certify.InfrastructureError):
            self._runner().prepare()

    def test_prepare_rejects_deleted_review(self) -> None:
        (self.root / "REVIEW.md").unlink()
        with self.assertRaises(certify.InfrastructureError):
            self._runner().prepare()

    def test_prepare_rejects_renamed_review(self) -> None:
        self._git("mv", "REVIEW.md", "REVIEW-MOVED.md")
        with self.assertRaises(certify.InfrastructureError):
            self._runner().prepare()

    def test_prepare_rejects_untracked_file(self) -> None:
        (self.root / "untracked.txt").write_text(
            "untracked\n", encoding="utf-8"
        )
        with self.assertRaises(certify.InfrastructureError):
            self._runner().prepare()

    def test_primary_seed_claim_collision_is_fail_closed(self) -> None:
        first = self._runner()
        first.prepare()
        second = self._runner()
        second.started = first.started + timedelta(seconds=1)
        with self.assertRaisesRegex(
            certify.InfrastructureError, "already claimed"
        ):
            second.prepare()

    def test_integrity_detects_runtime_artifact_mutation(self) -> None:
        runner = self._runner()
        runner.prepare()
        assert runner.runtime_dir is not None
        runtime_artifact = (
            runner.runtime_dir / certify.ARTIFACT_RELATIVE_PATH
        )
        runtime_artifact.write_bytes(b"mutated")
        with self.assertRaisesRegex(
            certify.ContractError, "runtime_artifact_matches"
        ):
            runner.verify_integrity("mutated-artifact")

    def test_integrity_detects_runtime_binary_mutation(self) -> None:
        runner = self._runner()
        runner.prepare()
        assert runner.runtime_dir is not None
        simulator = runner.runtime_dir / "old-school-sim"
        simulator.write_bytes(b"first binary")
        runner.report["run"]["simulator_sha256"] = certify.sha256_path(
            simulator
        )
        runner.verify_integrity("known-binary")
        simulator.write_bytes(b"different binary")
        with self.assertRaisesRegex(
            certify.ContractError, "simulator_matches"
        ):
            runner.verify_integrity("mutated-binary")

    def test_recipe_records_effect_power_and_actual_mde(self) -> None:
        runner = self._runner()
        self.assertEqual(
            runner.report["schema"], "learned-value-certification/v4"
        )
        recipe = runner.report["recipe"]
        self.assertEqual(
            recipe["primary_effect_of_interest_percentage_points"], 3.0
        )
        self.assertEqual(recipe["fixed_panel_repetitions_per_seed"], 5)
        self.assertEqual(recipe["fixed_panel_pooled_repetitions"], 40)
        self.assertEqual(recipe["fixed_panel_total_direct_games"], 2400)
        self.assertEqual(recipe["pooled_direct_repetitions"], 74)
        self.assertEqual(recipe["pooled_direct_total_games"], 4440)
        self.assertEqual(recipe["pooled_direct_games_per_deck"], 888)
        self.assertEqual(
            recipe["pooled_direct_diagonal_games_per_cell"], 296
        )
        self.assertEqual(
            recipe["pooled_direct_off_diagonal_games_per_cell"], 148
        )
        self.assertEqual(
            set(runner.report["criteria"]),
            {
                "primary_aggregate_over_50",
                "primary_wilson_lower_over_50",
                "mixed_lift_all_decks",
                "no_losing_validation_seed",
                "pooled_direct_aggregate_over_50",
                "pooled_direct_wilson_lower_over_50",
                "pooled_direct_wins_all_decks",
                "tests_and_sanitizers",
            },
        )
        self.assertEqual(recipe["alpha_two_sided"], 0.05)
        power = recipe["power"]
        self.assertEqual(power["target_power"], 0.80)
        self.assertAlmostEqual(power["achieved_power"], 0.7706562312)
        self.assertFalse(power["target_power_met"])
        self.assertEqual(power["critical_wins"], 1065)
        self.assertAlmostEqual(
            power["mde_at_target_power_percentage_points"],
            3.1111200727,
        )

    def test_source_snapshot_hash_ignores_only_declared_outputs(self) -> None:
        runner = self._runner()
        runner.prepare()
        assert runner.source_dir is not None
        generated = runner.source_dir / "build/generated"
        generated.parent.mkdir(parents=True, exist_ok=True)
        generated.write_text("derived\n", encoding="utf-8")
        runner.verify_source_snapshot("declared-output")
        injected = runner.source_dir / "src/injected.cpp"
        injected.write_text("unexpected\n", encoding="utf-8")
        with self.assertRaisesRegex(
            certify.ContractError, "no_unexpected_source_paths"
        ):
            runner.verify_source_snapshot("unexpected-input")

    def test_source_bound_stage_brackets_archived_tree(self) -> None:
        runner = self._runner()
        runner.prepare()
        assert runner.source_dir is not None
        runner.run_source_stage(
            "derived-output",
            [
                sys.executable,
                "-c",
                (
                    "from pathlib import Path; "
                    "Path('build').mkdir(); "
                    "Path('build/derived').write_text('ok')"
                ),
            ],
        )
        self.assertEqual(
            [
                entry["label"]
                for entry in runner.report["source_integrity_checks"]
            ],
            ["before-derived-output", "after-derived-output"],
        )
        self.assertEqual(
            runner.report["stages"][-1]["cwd"],
            str(runner.source_dir.relative_to(self.root)),
        )

    def test_source_bound_stage_rejects_input_mutation(self) -> None:
        runner = self._runner()
        runner.prepare()
        with self.assertRaisesRegex(
            certify.ContractError, "snapshot_matches"
        ):
            runner.run_source_stage(
                "mutate-input",
                [
                    sys.executable,
                    "-c",
                    "from pathlib import Path; "
                    "Path('tracked.txt').write_text('mutated')",
                ],
            )


class WorktreeStatusContractTests(unittest.TestCase):
    def test_only_clean_or_exact_unstaged_review_is_allowed(self) -> None:
        self.assertTrue(certify.worktree_status_allowed(b""))
        self.assertTrue(
            certify.worktree_status_allowed(b" M REVIEW.md\0")
        )
        forbidden = (
            b"M  REVIEW.md\0",
            b"MM REVIEW.md\0",
            b" D REVIEW.md\0",
            b"T  REVIEW.md\0",
            b"R  REVIEW-MOVED.md\0REVIEW.md\0",
            b"?? REVIEW.md\0",
            b" M tracked.txt\0",
            b"?? untracked.txt\0",
            b" M REVIEW.md\0?? untracked.txt\0",
            b" M REVIEW.md",
        )
        for raw in forbidden:
            with self.subTest(raw=raw):
                self.assertFalse(certify.worktree_status_allowed(raw))


if __name__ == "__main__":
    unittest.main()
