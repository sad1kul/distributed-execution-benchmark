#!/usr/bin/env python3
"""Independent reference for the deterministic integer workload."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

MASK64 = (1 << 64) - 1
SEED_B_XOR = 0xA5A5A5A5A5A5A5A5
FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
ROOT = Path(__file__).resolve().parents[1]


def splitmix64_next(state: int) -> tuple[int, int]:
    state = (state + 0x9E3779B97F4A7C15) & MASK64
    z = state
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK64
    return state, (z ^ (z >> 31)) & MASK64


def generate_matrix(n: int, seed: int, max_dimension: int = 2048) -> list[int]:
    if n == 0 or n > max_dimension:
        raise ValueError("dimension is outside the permitted range")
    state = seed & MASK64
    result = []
    for _ in range(n * n):
        state, value = splitmix64_next(state)
        result.append(value % 16)
    return result


def generate_inputs(n: int, seed: int) -> tuple[list[int], list[int]]:
    return generate_matrix(n, seed), generate_matrix(n, seed ^ SEED_B_XOR)


def multiply(n: int, a: list[int], b: list[int]) -> list[int]:
    if len(a) != n * n or len(b) != n * n:
        raise ValueError("matrix storage does not match its dimension")
    result = [0] * (n * n)
    for i in range(n):
        for j in range(n):
            total = 0
            for k in range(n):
                total = (total + ((a[i * n + k] * b[k * n + j]) & MASK64)) & MASK64
            result[i * n + j] = total
    return result


def fnv1a64_byte(hash_value: int, byte: int) -> int:
    return ((hash_value ^ (byte & 0xFF)) * FNV_PRIME) & MASK64


def fnv1a64_le64(hash_value: int, value: int) -> int:
    for shift in range(0, 64, 8):
        hash_value = fnv1a64_byte(hash_value, (value >> shift) & 0xFF)
    return hash_value


def checksum(n: int, values: list[int]) -> int:
    if len(values) != n * n:
        raise ValueError("matrix storage does not match its dimension")
    result = fnv1a64_le64(FNV_OFFSET, n)
    for value in values:
        result = fnv1a64_le64(result, value)
    return result


CASES = [
    (1, 0, [15], [1], [15], 0x11FD7B309A75530B),
    (2, 0, [15, 4, 15, 12], [1, 1, 10, 5], [55, 35, 135, 75], 0x5C62EFBFD523391F),
    (3, 0, [15, 4, 15, 12, 11, 10, 1, 12, 3], [1, 1, 10, 5, 6, 3, 14, 2, 2], [245, 69, 192, 207, 98, 173, 103, 79, 52], 0x48635283F1E3FCCA),
    (2, 0xDEADBEEFCAFEF00D, [11, 7, 4, 2], [0, 7, 10, 3], [70, 98, 20, 34], 0x4FD41AA3FB32B415),
    (3, 0xDEADBEEFCAFEF00D, [11, 7, 4, 2, 12, 4, 11, 6, 5], [0, 7, 10, 3, 3, 8, 1, 10, 0], [25, 138, 166, 40, 90, 116, 23, 145, 158], 0x80BE2BEEE8D3018D),
    (2, 1, [1, 7, 14, 11], [4, 11, 9, 3], [67, 32, 155, 187], 0x8A8006D9A5534DE4),
    (3, 1, [1, 7, 14, 11, 9, 0, 5, 5, 8], [4, 11, 9, 3, 5, 8, 13, 5, 3], [207, 116, 107, 71, 166, 171, 139, 120, 109], 0xB4FC51D19E142BA2),
    (4, 12345, [0, 13, 13, 10, 11, 14, 2, 12, 7, 15, 5, 6, 4, 5, 0, 6], [6, 5, 7, 2, 4, 1, 0, 6, 8, 9, 14, 9, 9, 1, 14, 8], [246, 140, 322, 275, 246, 99, 273, 220, 196, 101, 203, 197, 98, 31, 112, 86], 0x84DC679E1B21A719),
]

SPLITMIX = {
    0: [0xE220A8397B1DCDAF, 0x6E789E6AA1B965F4, 0x06C45D188009454F],
    1: [0x910A2DEC89025CC1, 0xBEEB8DA1658EEC67, 0xF893A2EEFB32555E],
    42: [0xBDD732262FEB6E95, 0x28EFE333B266F103, 0x47526757130F9F52],
    0xDEADBEEFCAFEF00D: [0x901D4F652FB472CB, 0xA7CE246440F74527, 0x19B40BBBB9380D34],
}


def check_anchors() -> list[str]:
    errors = []
    for seed, expected in SPLITMIX.items():
        state = seed
        actual = []
        for _ in range(3):
            state, value = splitmix64_next(state)
            actual.append(value)
        if actual != expected:
            errors.append(f"SplitMix64 seed {seed:#x}: {actual!r} != {expected!r}")
    for n, seed, expected_a, expected_b, expected_c, expected_ck in CASES:
        a, b = generate_inputs(n, seed)
        c = multiply(n, a, b)
        for label, actual, expected in (("A", a, expected_a), ("B", b, expected_b), ("C", c, expected_c)):
            if actual != expected:
                errors.append(f"n={n} seed={seed:#x} {label}: {actual!r} != {expected!r}")
        actual_ck = checksum(n, c)
        if actual_ck != expected_ck:
            errors.append(f"n={n} seed={seed:#x} checksum: {actual_ck:#018x} != {expected_ck:#018x}")
    byte_vectors = [(b"", FNV_OFFSET), (b"a", 0xAF63DC4C8601EC8C), (b"foobar", 0x85944171F73967E8)]
    for data, expected in byte_vectors:
        actual = FNV_OFFSET
        for byte in data:
            actual = fnv1a64_byte(actual, byte)
        if actual != expected:
            errors.append(f"FNV bytes {data!r}: {actual:#018x} != {expected:#018x}")
    for value, expected in ((3, 0xC7C2BF3B330983E6), (0x0102030405060708, 0x0C6D4496E17859D5)):
        actual = fnv1a64_le64(FNV_OFFSET, value)
        if actual != expected:
            errors.append(f"FNV le64 {value:#x}: {actual:#018x} != {expected:#018x}")
    wrap = [0xFFFFFFFC00000002, 0, 0, 0]
    if checksum(2, wrap) != 0x5C82B77744BD5F52:
        errors.append("wrap checksum differs")
    products = [
        (1, [7], [6], [42]),
        (2, [1, 2, 3, 4], [5, 6, 7, 8], [19, 22, 43, 50]),
        (3, list(range(1, 10)), list(range(9, 0, -1)), [30, 24, 18, 84, 69, 54, 138, 114, 90]),
        (2, [0xFFFFFFFF, 0xFFFFFFFF, 0, 0], [0xFFFFFFFF, 0, 0xFFFFFFFF, 0], wrap),
    ]
    for n, a, b, expected in products:
        if multiply(n, a, b) != expected:
            errors.append(f"hand-built {n}x{n} product differs")
    return errors


def generated_data() -> dict[str, object]:
    cases = []
    for n, seed, *_ in CASES:
        a, b = generate_inputs(n, seed)
        c = multiply(n, a, b)
        cases.append({"n": n, "seed": f"0x{seed:016x}", "a": a, "b": b,
                      "c": [f"0x{x:016x}" for x in c], "checksum": f"0x{checksum(n, c):016x}"})
    return {"format_version": 1, "cases": cases}


def write_outputs() -> None:
    data = generated_data()
    json_path = ROOT / "tests" / "golden_vectors.json"
    inc_path = ROOT / "tests" / "golden_vectors.inc"
    json_path.parent.mkdir(parents=True, exist_ok=True)
    json_path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    lines = ["// Generated by tools/reference.py --write. Do not edit.", "const std::vector<GoldenCase> kGoldenCases = {"]
    for case in data["cases"]:
        a = ", ".join(f"{x}U" for x in case["a"])
        b = ", ".join(f"{x}U" for x in case["b"])
        c = ", ".join(f"{x}ULL" for x in case["c"])
        lines.append(f"    {{{case['n']}U, {case['seed']}ULL, {{{a}}}, {{{b}}}, {{{c}}}, {case['checksum']}ULL}},")
    lines.extend(["};", ""])
    inc_path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--check", action="store_true")
    group.add_argument("--write", action="store_true")
    args = parser.parse_args()
    errors = check_anchors()
    if errors:
        print("anchor verification failed:", file=sys.stderr)
        for error in errors:
            print(f"  {error}", file=sys.stderr)
        return 1
    if args.write:
        write_outputs()
        print("anchors verified; golden vectors written")
    else:
        print("all anchor vectors verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
