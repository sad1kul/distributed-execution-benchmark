# Implementation history

## Sprint 1 baseline

- Objective: establish the deterministic integer workload and independent oracle.
- Baseline: Sprint 1 tip `c637330`; merged into `origin/main` by `1a43b6a`.
- Evidence at V1 start: Release build, CTest, and Python anchors passed locally.
- Known limitation: only ARM64 macOS/AppleClang had local evidence.

## Protocol codec — `bf3f8bf`

- Files: `include/protocol.hpp`, `src/protocol.cpp`, `tests/protocol_test.cpp`, CMake.
- Decision: explicit big-endian fields and strict exact-length messages; no raw structs.
- Tests: workload and protocol CTest passed; Python anchors passed.
- Limitation: this stage intentionally contained no sockets.

## Local execution — `b4fde63`

- Files: execution library/tests and targeted workload regression.
- Decision: pre-created lanes, shared release gate, static round robin, expectations
  computed separately from measured work.
- Defect corrected: zero-dimensional multiply/checksum objects were previously
  accepted; both now throw `invalid_argument` with regression coverage.
- Tests: workload, protocol, and execution tests passed.

## TCP worker sessions — `0738e66`

- Files: socket abstraction, remote session/server, network tests.
- Decision: nonblocking sockets with monotonic overall deadlines; loopback default;
  persistent worker listener with session-only SHUTDOWN.
- Test issue: sandboxed CTest initially failed `bind` with socket error 1. The identical
  suite passed when explicitly allowed to use ephemeral localhost sockets.
- Limitation: no external host was contacted.

## Four execution modes — `4ae76d1`

- Files: distributed runner and integration tests.
- Decision: one common gate and fixed lane ownership; persistent teardown occurs after
  the stored batch boundary; fresh teardown remains inside it.
- Tests: all five then-current CTest targets passed over localhost.

## Measurement pipeline — `7a6800f`

- Files: worker/benchmark CLIs, benchmark library/test, functional smoke test.
- Decision: separate authoritative batch rows from task rows, retain measured
  failures, and use `unknown` for unavailable metadata.
- Tests: all four CLI modes produced valid temporary smoke observations; data was
  deleted and was not treated as research evidence.

## Analysis safeguards — `b39071a`

- Files: analysis script/test and expanded raw configuration fields.
- Decision: nearest-rank p95, unclamped penalty, matched baseline requirement, invalid
  and smoke-row exclusion, build compatibility checks.
- Tests: metric fixtures and refusal paths passed.

## Network quality pass — `8e1ba65`

- Files: server shutdown, socket portability, and expanded network tests.
- Defect corrected: a macOS `timeval` narrowing build error was fixed during initial
  networking work. The quality pass then added MSVC socket-length types and prompt
  active-session closure on worker termination.
- Tests: deterministic byte-at-a-time transfer, malformed state transitions, worker
  errors, Release CTest, and the complete AppleClang ASan/UBSan suite passed.

Hosted CI, Windows, Linux, x86-64, and real two-machine execution remain unverified
until those environments actually run the branch.
