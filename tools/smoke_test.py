#!/usr/bin/env python3
"""Functional localhost smoke test; generated data is temporary, not research evidence."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path
import queue
import subprocess
import tempfile
import threading


def read_ready(process: subprocess.Popen[str], timeout: float) -> str:
    lines: queue.Queue[str] = queue.Queue()

    def reader() -> None:
        assert process.stdout is not None
        lines.put(process.stdout.readline())

    thread = threading.Thread(target=reader, daemon=True)
    thread.start()
    try:
        line = lines.get(timeout=timeout).strip()
    except queue.Empty as error:
        raise RuntimeError("worker did not signal readiness") from error
    if not line.startswith("READY 127.0.0.1:"):
        raise RuntimeError(f"unexpected worker readiness output: {line!r}")
    return line


def verify_output(directory: Path, task_count: int) -> None:
    with (directory / "task_observations.csv").open(newline="", encoding="utf-8") as handle:
        tasks = list(csv.DictReader(handle))
    with (directory / "batch_observations.csv").open(newline="", encoding="utf-8") as handle:
        batches = list(csv.DictReader(handle))
    if len(tasks) != task_count or any(row["valid"] != "true" for row in tasks):
        raise RuntimeError("smoke task observations are missing or invalid")
    if len(batches) != 1 or batches[0]["batch_valid"] != "true":
        raise RuntimeError("smoke batch observation is missing or invalid")
    if not (directory / "environment.json").is_file():
        raise RuntimeError("smoke environment record is missing")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--worker", required=True)
    parser.add_argument("--benchmark", required=True)
    args = parser.parse_args()

    worker = subprocess.Popen(
        [args.worker, "--bind", "127.0.0.1", "--port", "0", "--message-timeout-ms", "5000"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        ready = read_ready(worker, 5.0)
        port = ready.rsplit(":", 1)[1]
        with tempfile.TemporaryDirectory(prefix="bom-smoke-") as temporary:
            root = Path(temporary)
            for mode in ("local-1", "local-2", "remote-1", "local-remote"):
                directory = root / mode
                command = [
                    args.benchmark,
                    "--mode", mode,
                    "--dimension", "3",
                    "--task-count", "3",
                    "--base-seed", "42",
                    "--worker-host", "127.0.0.1",
                    "--worker-port", port,
                    "--connection-mode", "persistent",
                    "--warmup-count", "0",
                    "--repetitions", "1",
                    "--connect-timeout-ms", "1000",
                    "--message-timeout-ms", "5000",
                    "--output-dir", str(directory),
                    "--experiment-id", f"smoke-{mode}",
                ]
                completed = subprocess.run(command, capture_output=True, text=True, timeout=20)
                if completed.returncode != 0:
                    raise RuntimeError(
                        f"{mode} smoke command failed ({completed.returncode}): "
                        f"{completed.stdout}{completed.stderr}"
                    )
                verify_output(directory, 3)
        print("functional smoke test passed for all four modes")
        return 0
    finally:
        if worker.poll() is None:
            worker.terminate()
            try:
                worker.wait(timeout=5)
            except subprocess.TimeoutExpired:
                worker.kill()
                worker.wait(timeout=5)
        if worker.returncode not in (0, -15, 15):
            stderr = worker.stderr.read() if worker.stderr is not None else ""
            if stderr:
                print(f"worker stderr: {stderr}")


if __name__ == "__main__":
    raise SystemExit(main())
