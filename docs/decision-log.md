# Decision log

## Sprint 1: deterministic workload

### Decisions

- Use unsigned fixed-width integers throughout the workload and checksum. Unsigned
  wraparound defines the required modulo-2^64 behavior without platform-specific
  code or floating-point arithmetic.
- Generate A and B from separate SplitMix64 states. The B-seed XOR appears only in
  `generate_inputs`, which makes accidental stream continuation easier to detect.
- Store matrices in flat row-major vectors and retain the naive `i`, `j`, `k` loop
  order. Blocking, transposition, threads, intrinsics, and explicit SIMD would change
  the frozen workload rather than merely implement it.
- Serialize the dimension and result elements byte by byte in little-endian order
  before FNV-1a hashing. Hashing object memory would make padding and native byte
  order part of the result.
- Keep the Python oracle independent of the C++ implementation. Generated fixtures
  serve interchange and test convenience; immutable anchors remain separately
  encoded in the test executable.
- Use a small dependency-free test program whose checks continue after failure.
  This keeps the research artifact portable and reports multiple discrepancies in
  one run.

### Validation and allocation policy

Public generation rejects zero dimensions, dimensions above the supplied limit, and
element-count or byte-size overflow before allocation. Multiplication rejects unequal
dimensions and malformed backing storage before allocating its result. Checksum
rejects malformed backing storage and checks element-count and byte-size arithmetic.
`std::invalid_argument` represents invalid dimensions or shapes;
`std::overflow_error` represents `size_t` arithmetic overflow. Allocation failures
are not translated and propagate to the caller.

### Alternatives not selected

- Standard random engines and distributions were excluded because their mapping to
  generated elements would not be the frozen SplitMix64 sequence.
- Raw-memory hashing was excluded because it would not define a portable byte stream.
- Test frameworks and numeric libraries were excluded because Sprint 1 needs no
  third-party dependency.
- Optimized multiplication techniques were excluded because this sprint establishes
  a simple reproducible baseline, not a performance result.

### Deviations

- No methodology deviation was made. At pre-flight, the requested
  `docs/experimental-protocol.md` and `docs/implementation-prompt.md` files were absent,
  so no repository-specific methodology text was available for comparison. Their
  contents were not inferred or recreated.

### Known limitations

- Local verification covers ARM64 macOS with AppleClang only. The CI workflow defines
  Ubuntu GCC, Ubuntu Clang, macOS 14 Clang, and an Ubuntu sanitizer job, but those jobs
  have not been observed running in this sprint workspace.
- Windows/MSVC is unverified even though the build defines `/W4` for MSVC.
- Cross-architecture determinism is not claimed until both ARM64 and x86-64 runs pass
  the same checked-in vectors.
- Sprint 1 has no networking, coordinator, worker, protocol, timing, concurrency,
  CSV output, charts, or distributed execution modes.
