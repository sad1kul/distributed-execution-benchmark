# Experimental protocol

## Research question

Under what combinations of task granularity, parallelism, and communication overhead
does adding compute capacity across a machine boundary improve throughput compared
with single-node execution?

This is a user-space distributed-execution experiment inspired by one performance
question raised by DEX. It is not DEX, thread migration, process migration,
distributed shared memory, or a production computing framework.

## Frozen numerical workload

Workload version 1 is the Sprint 1 unsigned-integer matrix workload. Task `i` has
`task_id = i` and `seed = base_seed + uint64_t(i)` using modulo-2^64 unsigned
addition. Task ID is metadata and never affects matrix generation. The checked-in
Python anchors establish independent numerical evidence; C++ expected checksums
establish consistency among execution modes.

## Modes and allocation

| Mode | Lanes | Purpose |
|---|---|---|
| local-1 | one local | single-worker reference |
| remote-1 | one TCP worker | remote-dispatch cost |
| local-2 | two local | local parallel comparison |
| local-remote | one local, one TCP worker | added remote compute comparison |

Two-lane modes use static round robin: `task_index % 2`. There is no work stealing or
adaptive balancing. Static allocation can create load imbalance on unequal machines.
At least two tasks are required before a two-worker run can be treated as scale-out
evidence. Single-task runs are permitted only for correctness and protocol checks.

## Batch procedure

For one configuration:

1. Validate all CLI inputs.
2. Construct the task set once.
3. Compute and retain every expected checksum outside all warm-up and measured timing.
4. Run the configured number of warm-up batches with the same task set and connection
   lifecycle. Abort if a warm-up is incorrect.
5. Run each measured batch without automatic retry.
6. Stop the stored batch timer.
7. Verify task IDs, uniqueness, completeness, status, and checksums.
8. After all repetitions, write raw task and batch observations plus environment data.

Failed measured batches remain in the batch CSV with a reason and are excluded by the
analysis tool. No failed computation is silently retried.

## Timing boundaries

`std::chrono::steady_clock` supplies all durations. The batch timer includes remote
connection and HELLO setup, dispatch, matrix preparation, multiplication, checksum,
result transfer and receipt, and fresh-per-task teardown. Local execution threads are
created before the timer and released through one shared gate.

Expected-checksum preparation, worker launch, local thread creation, post-batch
verification, CSV writing, environment collection, console output, analysis, and
persistent teardown are excluded. Persistent setup duration is recorded separately
but remains part of total batch time. Worker timing fields are durations; absolute
steady-clock values are never compared across machines.

## Raw observations

`task_observations.csv` contains one row per returned task observation. Dispatch and
completion are coordinator-relative nanoseconds; worker preparation, computation, and
checksum fields are durations. `batch_observations.csv` is the authoritative source
for batch-level timing. Repeating a batch duration on task rows is deliberately
avoided. Units are encoded in field names.

Environment JSON records automatically observable coordinator and build fields.
Worker hardware, allocation, network characterization, virtualization, and power mode
must be completed manually when automatic detection reports `unknown`.
Each invocation receives a distinct run ID even when an experiment label is reused.

## Analysis definitions

- Throughput: `task_count / batch_duration_seconds`.
- Speedup: median local-1 batch duration divided by the selected mode median.
- Nominal two-worker efficiency: speedup divided by two for local-2 and local-remote.
- Relative machine-boundary penalty: local-remote median divided by local-2 median,
  minus one. Negative values are retained. This is not isolated network overhead.
- Median uses the standard middle-value definition and averages the middle pair for
  even sample counts.
- p95 uses nearest rank: `ceil(0.95 * count) - 1` in a sorted zero-based array.

Analysis rejects mismatched workload/configuration keys, missing local-1 baselines,
duplicate batch identities, incompatible coordinator/build records, invalid rows,
one-task two-worker rows, loopback remote rows, and smoke-test experiment IDs.
Hardware and network differences remain visible in the environment note.

## Controlled-study boundary

Localhost exercises correctness and instrumentation only. It does not demonstrate
multi-machine performance. Controlled measurements require characterized hosts,
network conditions, compatible builds, suitable workload calibration, warm-ups, and
repetition counts chosen without regard to a desired outcome. Two comparable ARM64
Linux hosts remain the preferred initial environment. That study is pending human
review.
