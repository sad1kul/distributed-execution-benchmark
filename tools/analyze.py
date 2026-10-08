#!/usr/bin/env python3
"""Summarize controlled batch observations without plotting dependencies."""

from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path
import statistics
import sys
from typing import Any

MATCH_FIELDS = (
    "matrix_size",
    "task_count",
    "base_seed",
    "workload_version",
    "connection_mode",
    "connect_timeout_ms",
    "message_timeout_ms",
)
BUILD_FIELDS = ("compiler", "compiler_version", "build_configuration", "compiler_flags")
TWO_WORKER_MODES = {"local-2", "local-remote"}


def nearest_rank_p95(values: list[int]) -> int:
    if not values:
        raise ValueError("p95 requires at least one observation")
    ordered = sorted(values)
    return ordered[math.ceil(0.95 * len(ordered)) - 1]


def load_directory(directory: Path) -> tuple[list[dict[str, str]], dict[str, Any]]:
    batch_path = directory / "batch_observations.csv"
    environment_path = directory / "environment.json"
    if not batch_path.is_file() or not environment_path.is_file():
        raise ValueError(f"{directory}: batch_observations.csv and environment.json are required")
    with batch_path.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    with environment_path.open(encoding="utf-8") as handle:
        environment = json.load(handle)
    for row in rows:
        missing = [field for field in (*MATCH_FIELDS, "mode", "total_batch_ns", "batch_valid")
                   if field not in row]
        if missing:
            raise ValueError(f"{batch_path}: missing columns {', '.join(missing)}")
        row["_source"] = str(batch_path)
    return rows, environment


def build_compatibility(environments: list[dict[str, Any]]) -> tuple[bool, str]:
    if not environments:
        return False, "no environment records"
    differences = []
    for field in BUILD_FIELDS:
        values = {str(environment.get(field, "unknown")) for environment in environments}
        if "unknown" in values:
            differences.append(f"{field} is unknown")
        elif len(values) != 1:
            differences.append(f"{field} differs: {sorted(values)}")
    architectures = sorted({str(item.get("coordinator_architecture", "unknown"))
                            for item in environments})
    systems = sorted({str(item.get("operating_system", "unknown")) for item in environments})
    context = f"coordinator_architectures={architectures}; operating_systems={systems}"
    if differences:
        return False, "; ".join(differences) + "; " + context
    return True, context


def summarize(directories: list[Path]) -> tuple[list[dict[str, Any]], list[dict[str, str]], str]:
    rows: list[dict[str, str]] = []
    environments = []
    for directory in directories:
        loaded_rows, environment = load_directory(directory)
        rows.extend(loaded_rows)
        environments.append(environment)
    compatible, environment_note = build_compatibility(environments)
    if not compatible:
        raise ValueError("comparison refused: build environments are not compatible: " + environment_note)

    excluded = []
    valid = []
    for row in rows:
        reason = ""
        if row["batch_valid"].lower() != "true":
            reason = row.get("failure_reason") or "invalid batch"
        elif row.get("experiment_id", "").startswith("smoke-"):
            reason = "functional smoke-test data is excluded from performance summaries"
        else:
            try:
                duration = int(row["total_batch_ns"])
                count = int(row["task_count"])
                if duration <= 0 or count <= 0:
                    reason = "nonpositive duration or task count"
            except ValueError:
                reason = "invalid numeric observation"
        if reason:
            rejected = dict(row)
            rejected["exclusion_reason"] = reason
            excluded.append(rejected)
        else:
            valid.append(row)

    grouped: dict[tuple[str, ...], dict[str, list[dict[str, str]]]] = {}
    for row in valid:
        key = tuple(row[field] for field in MATCH_FIELDS)
        grouped.setdefault(key, {}).setdefault(row["mode"], []).append(row)

    summaries = []
    for key, modes in sorted(grouped.items()):
        if "local-1" not in modes:
            raise ValueError(f"comparison refused for {key}: matched local-1 baseline is missing")
        baseline_values = [int(row["total_batch_ns"]) for row in modes["local-1"]]
        baseline_median = statistics.median(baseline_values)
        medians: dict[str, float] = {}
        for mode, mode_rows in modes.items():
            durations = [int(row["total_batch_ns"]) for row in mode_rows]
            median_ns = statistics.median(durations)
            medians[mode] = median_ns
            task_count = int(key[MATCH_FIELDS.index("task_count")])
            throughput_samples = [task_count / (duration / 1_000_000_000) for duration in durations]
            speedup = baseline_median / median_ns
            summary = {field: value for field, value in zip(MATCH_FIELDS, key)}
            summary.update({
                "mode": mode,
                "valid_batches": len(durations),
                "median_batch_ns": median_ns,
                "p95_batch_ns": nearest_rank_p95(durations),
                "median_throughput_tasks_per_second": statistics.median(throughput_samples),
                "speedup_vs_local_1": speedup,
                "nominal_two_worker_efficiency": speedup / 2 if mode in TWO_WORKER_MODES else "",
                "relative_machine_boundary_penalty": "",
                "environment_note": environment_note,
            })
            summaries.append(summary)
        if "local-remote" in medians and "local-2" in medians:
            penalty = medians["local-remote"] / medians["local-2"] - 1
            for summary in summaries:
                if (summary["mode"] == "local-remote" and
                        tuple(str(summary[field]) for field in MATCH_FIELDS) == key):
                    summary["relative_machine_boundary_penalty"] = penalty
    return summaries, excluded, environment_note


def write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input-dir", action="append", required=True, type=Path)
    parser.add_argument("--summary", required=True, type=Path)
    parser.add_argument("--excluded", required=True, type=Path)
    args = parser.parse_args()
    try:
        summaries, excluded, note = summarize(args.input_dir)
        summary_fields = [
            *MATCH_FIELDS,
            "mode",
            "valid_batches",
            "median_batch_ns",
            "p95_batch_ns",
            "median_throughput_tasks_per_second",
            "speedup_vs_local_1",
            "nominal_two_worker_efficiency",
            "relative_machine_boundary_penalty",
            "environment_note",
        ]
        excluded_fields = [
            "experiment_id", "run_id", "repetition", "mode", *MATCH_FIELDS,
            "total_batch_ns", "batch_valid", "failure_reason", "exclusion_reason", "_source",
        ]
        write_csv(args.summary, summaries, summary_fields)
        write_csv(args.excluded, excluded, excluded_fields)
        print(f"summary_rows={len(summaries)}")
        print(f"excluded_rows={len(excluded)}")
        print(f"environment={note}")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
