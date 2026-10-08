#!/usr/bin/env python3
"""Unit tests for metric definitions and comparison safeguards."""

from __future__ import annotations

import csv
import json
from pathlib import Path
import tempfile

import analyze


FIELDS = [
    "experiment_id", "run_id", "repetition", "mode", "matrix_size", "task_count",
    "base_seed", "workload_version", "connection_mode", "connect_timeout_ms",
    "message_timeout_ms", "total_batch_ns", "connection_setup_ns", "batch_valid",
    "completed_tasks", "failed_tasks", "failure_reason",
]


def write_run(root: Path, mode: str, durations: list[int], *, invalid: bool = False) -> Path:
    directory = root / mode
    directory.mkdir()
    with (directory / "batch_observations.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDS)
        writer.writeheader()
        for repetition, duration in enumerate(durations):
            writer.writerow({
                "experiment_id": mode,
                "run_id": mode + "-run",
                "repetition": repetition,
                "mode": mode,
                "matrix_size": 4,
                "task_count": 4,
                "base_seed": 1,
                "workload_version": 1,
                "connection_mode": "persistent",
                "connect_timeout_ms": 5000,
                "message_timeout_ms": 300000,
                "total_batch_ns": duration,
                "connection_setup_ns": 0,
                "batch_valid": "false" if invalid else "true",
                "completed_tasks": 4,
                "failed_tasks": 0,
                "failure_reason": "injected" if invalid else "",
            })
    environment = {
        "compiler": "Clang",
        "compiler_version": "1",
        "build_configuration": "Release",
        "compiler_flags": "-O2",
        "coordinator_architecture": "arm64",
        "coordinator_cpu_model": "test-cpu",
        "operating_system": "test",
        "kernel_or_os_version": "test-kernel",
        "worker_host": "192.0.2.1" if mode in {"remote-1", "local-remote"} else "unknown",
        "worker_architecture": "arm64" if mode in {"remote-1", "local-remote"} else "unknown",
        "worker_cpu_model": "test-worker" if mode in {"remote-1", "local-remote"} else "unknown",
        "worker_cpu_allocation": "2" if mode in {"remote-1", "local-remote"} else "unknown",
        "network_environment": "controlled-test" if mode in {"remote-1", "local-remote"} else "unknown",
    }
    (directory / "environment.json").write_text(json.dumps(environment), encoding="utf-8")
    return directory


def main() -> int:
    assert analyze.nearest_rank_p95([1, 2, 3, 4]) == 4
    assert analyze.nearest_rank_p95(list(range(1, 21))) == 19
    with tempfile.TemporaryDirectory(prefix="bom-analysis-test-") as temporary:
        root = Path(temporary)
        directories = [
            write_run(root, "local-1", [100, 200]),
            write_run(root, "local-2", [50, 100]),
            write_run(root, "local-remote", [75, 150]),
            write_run(root, "remote-1", [300], invalid=True),
        ]
        summaries, excluded, _ = analyze.summarize(directories)
        by_mode = {row["mode"]: row for row in summaries}
        assert by_mode["local-1"]["median_batch_ns"] == 150
        assert by_mode["local-2"]["speedup_vs_local_1"] == 2
        assert by_mode["local-2"]["nominal_two_worker_efficiency"] == 1
        assert by_mode["local-remote"]["relative_machine_boundary_penalty"] == 0.5
        assert len(excluded) == 1 and excluded[0]["exclusion_reason"] == "injected"
        try:
            analyze.summarize([directories[1]])
            raise AssertionError("missing baseline was accepted")
        except ValueError:
            pass
        try:
            analyze.summarize([directories[0], directories[0]])
            raise AssertionError("duplicate observations were accepted")
        except ValueError:
            pass
        loopback = write_run(root, "remote-loopback", [100])
        batch_path = loopback / "batch_observations.csv"
        rows = list(csv.DictReader(batch_path.open(newline="", encoding="utf-8")))
        rows[0]["mode"] = "remote-1"
        rows[0]["experiment_id"] = "manual-localhost"
        with batch_path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=FIELDS)
            writer.writeheader()
            writer.writerows(rows)
        environment_path = loopback / "environment.json"
        environment = json.loads(environment_path.read_text(encoding="utf-8"))
        environment["worker_host"] = "127.0.0.1"
        environment_path.write_text(json.dumps(environment), encoding="utf-8")
        summaries, loopback_excluded, _ = analyze.summarize([directories[0], loopback])
        assert {row["mode"] for row in summaries} == {"local-1"}
        assert "loopback" in loopback_excluded[0]["exclusion_reason"]
    print("analysis tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
