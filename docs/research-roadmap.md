# Research roadmap

## Implemented

- Frozen deterministic workload and independent golden vectors.
- BOMX protocol version 1 and bounded cross-platform TCP abstraction.
- Listening worker with persistent and fresh-per-task sessions.
- local-1, remote-1, local-2, and local-remote execution modes.
- Static deterministic scheduling and post-batch correctness verification.
- Warm-ups, measured repetitions, task/batch CSVs, and environment records.
- Guarded standard-library analysis metrics.
- Unit, protocol, localhost integration, sanitizer, and functional smoke coverage.

## Human validation next

1. Review the code and raw schema.
2. Run hosted CI on the feature branch and record actual runner architectures.
3. Verify a real two-host trusted-network connection.
4. Complete manual environment fields for both hosts.
5. Calibrate workloads without selecting for a desired result.
6. Freeze a controlled experiment plan and repetition counts.
7. Collect the first two-machine raw dataset.
8. Review validity and exclusions before generating summaries.

## Explicitly outside V1

Multiple remote workers, dynamic scheduling, authentication, TLS, cloud orchestration,
containers, databases, dashboards, energy measurement, GPUs, distributed shared
memory, and execution migration are not planned for this version.

No performance campaign or conclusion has been completed.
