# Beyond One Machine: Deterministic Workload

This repository contains an independent experiment about computation-communication
trade-offs. It is inspired by a question from the DEX paper (ICDCS 2020), but it is
not a DEX implementation or reproduction.

## Status

Sprint 1 implements only the deterministic integer matrix workload. It includes an
independent Python reference, generated golden vectors, a portable C++20 library,
and correctness tests T01-T31. Networking, distributed execution, timing, and the
multi-mode experiment harness are not implemented.

Verified locally:

- macOS on ARM64 with AppleClang 21.0.0, Release build and CTest.
- macOS on ARM64 with AppleClang 21.0.0, AddressSanitizer and UndefinedBehaviorSanitizer.
- Python 3.14.7 reference anchor verification.

Configured in CI but not yet verified by an executed workflow:

- Ubuntu x86-64 with GCC.
- Ubuntu x86-64 with Clang.
- macOS 14 ARM64 with Clang.
- Ubuntu x86-64 with Clang AddressSanitizer and UndefinedBehaviorSanitizer.

Windows and MSVC are unverified. Cross-architecture determinism is not yet claimed.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The Release build uses C++20. GCC and Clang targets receive `-O2 -Wall -Wextra
-Wpedantic`; MSVC receives `/W4`. Compiler auto-optimization is permitted and the
configured flags remain visible in the build system.

## Test

```sh
ctest --test-dir build --output-on-failure
python3 tools/reference.py --check
```

CTest runs the dependency-free C++ test executable. The Python command independently
checks every frozen anchor without invoking the C++ library.

## Golden vectors

`tools/reference.py` uses only the Python standard library and masks reference
arithmetic to 64 bits. After changing the reference intentionally, regenerate both
checked-in formats with:

```sh
python3 tools/reference.py --write
git diff -- tests/golden_vectors.json tests/golden_vectors.inc
```

CI regenerates these files and fails if they differ from the checked-in versions.
The immutable anchors are also written separately in the C++ tests so the generator
cannot validate itself silently.
