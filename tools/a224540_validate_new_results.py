#!/usr/bin/env python3
"""Independent artifact-level validation for A224540 new results.

This checker intentionally does not invoke the production binary. It parses the
campaign TSV files, recomputes powers, segment coverage, and aggregate sums.
"""

from __future__ import annotations

import csv
import hashlib
from pathlib import Path
from typing import Dict, Iterable, List, Tuple


ROOT = Path(__file__).resolve().parents[1]

EXPECTED_NEW = {
    22: 12406199367,
    23: 37216010685,
    24: 111651745638,
    25: 334951946950,
    26: 1004855123675,
    27: 3014552251123,
    28: 9043690122807,
}

EXPECTED_ALL = {
    0: 1,
    1: 2,
    2: 4,
    3: 12,
    4: 36,
    5: 106,
    6: 249,
    7: 613,
    8: 1732,
    9: 8028,
    10: 23348,
    11: 69370,
    12: 210807,
    13: 634839,
    14: 1893582,
    15: 5686389,
    16: 17031777,
    17: 51073675,
    18: 153185957,
    19: 459516225,
    20: 1378707224,
    21: 4135278456,
    **EXPECTED_NEW,
}

RANGE_PATH = ROOT / "validation/campaigns/a22_a25/a22_a25.tsv"
AGGREGATES = {
    26: ROOT / "validation/campaigns/a26/aggregate_n26.tsv",
    27: ROOT / "validation/campaigns/a27/aggregate_n27.tsv",
    28: ROOT / "validation/campaigns/a28/aggregate_n28.tsv",
}
SEGMENT_GLOBS = {
    26: "validation/campaigns/a26/segment_n26_*.tsv",
    27: "validation/campaigns/a27/segment_n27_*.tsv",
    28: "validation/campaigns/a28/segment_n28_*.tsv",
}
BFILE_PATH = ROOT / "data/b224540.txt"


def read_single_row_tsv(path: Path) -> Dict[str, str]:
    with path.open(newline="") as f:
        rows = list(csv.DictReader(f, delimiter="\t"))
    if len(rows) != 1:
        raise ValueError(f"{path}: expected exactly one data row, got {len(rows)}")
    return rows[0]


def read_tsv(path: Path) -> List[Dict[str, str]]:
    with path.open(newline="") as f:
        return list(csv.DictReader(f, delimiter="\t"))


def as_int(row: Dict[str, str], key: str, path: Path) -> int:
    try:
        return int(row[key])
    except (KeyError, ValueError) as exc:
        raise ValueError(f"{path}: invalid integer field {key!r}: {row.get(key)!r}") from exc


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def check_pow3(n: int, m: int, path: Path) -> None:
    expected = 3 ** n
    if m != expected:
        raise ValueError(f"{path}: M mismatch for n={n}: got {m}, expected {expected}")


def valid_children(x: int, m: int) -> List[int]:
    children: List[int] = []
    if x <= m // 2:
        children.append(2 * x)
    if x % 3 == 1:
        y = (x - 1) // 3
        if y > 1 and y % 2 == 1 and y <= m:
            children.append(y)
    return children


def prefix_summary(m: int, depth: int) -> Dict[str, int]:
    stack: List[Tuple[int, int]] = [(1, 0)]
    prefix_count = 0
    frontier_count = 0
    double_pushes = 0
    odd_pushes = 0
    max_stack_len = 1
    max_value = 1

    while stack:
        x, d = stack.pop()
        if d == depth:
            frontier_count += 1
            continue

        prefix_count += 1
        max_value = max(max_value, x)
        children = valid_children(x, m)
        for child_index, child in enumerate(children):
            if child_index == 0 and x <= m // 2:
                double_pushes += 1
            else:
                odd_pushes += 1
            stack.append((child, d + 1))
            max_stack_len = max(max_stack_len, len(stack))

    return {
        "prefix_count": prefix_count,
        "frontier_count": frontier_count,
        "double_pushes": double_pushes,
        "odd_pushes": odd_pushes,
        "max_stack_len": max_stack_len,
        "max_value": max_value,
    }


def validate_range() -> List[Tuple[int, int]]:
    rows = read_tsv(RANGE_PATH)
    if len(rows) != 4:
        raise ValueError(f"{RANGE_PATH}: expected four rows for n=22..25")
    values = []
    seen = []
    for row in rows:
        n = as_int(row, "n", RANGE_PATH)
        m = as_int(row, "M", RANGE_PATH)
        count = as_int(row, "count", RANGE_PATH)
        seen.append(n)
        check_pow3(n, m, RANGE_PATH)
        if row.get("mode") != "range":
            raise ValueError(f"{RANGE_PATH}: n={n} mode is not range")
        if row.get("status") != "COMPUTED":
            raise ValueError(f"{RANGE_PATH}: n={n} status is not COMPUTED")
        if EXPECTED_NEW[n] != count:
            raise ValueError(f"{RANGE_PATH}: n={n} count mismatch")
        values.append((n, count))
    if seen != [22, 23, 24, 25]:
        raise ValueError(f"{RANGE_PATH}: unexpected n sequence {seen}")
    return values


def validate_bfile() -> None:
    rows: Dict[int, int] = {}
    with BFILE_PATH.open(encoding="utf-8") as f:
        for line_no, raw in enumerate(f, 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) != 2:
                raise ValueError(f"{BFILE_PATH}:{line_no}: expected exactly two fields")
            n = int(parts[0])
            value = int(parts[1])
            if n in rows:
                raise ValueError(f"{BFILE_PATH}:{line_no}: duplicate n={n}")
            rows[n] = value
    expected_keys = list(range(0, 29))
    if sorted(rows) != expected_keys:
        raise ValueError(f"{BFILE_PATH}: expected contiguous n=0..28")
    for n in expected_keys:
        if rows[n] != EXPECTED_ALL[n]:
            raise ValueError(f"{BFILE_PATH}: value mismatch at n={n}")


def validate_segments(n: int) -> Tuple[int, Dict[str, int]]:
    aggregate_path = AGGREGATES[n]
    aggregate = read_single_row_tsv(aggregate_path)
    if aggregate.get("mode") != "aggregate":
        raise ValueError(f"{aggregate_path}: mode is not aggregate")
    if aggregate.get("status") != "AGGREGATED_NO_ORACLE":
        raise ValueError(f"{aggregate_path}: status is not AGGREGATED_NO_ORACLE")

    agg_n = as_int(aggregate, "n", aggregate_path)
    if agg_n != n:
        raise ValueError(f"{aggregate_path}: aggregate n mismatch")
    m = as_int(aggregate, "M", aggregate_path)
    check_pow3(n, m, aggregate_path)
    depth = as_int(aggregate, "depth", aggregate_path)
    prefix = as_int(aggregate, "prefix_count", aggregate_path)
    frontier = as_int(aggregate, "frontier_count", aggregate_path)
    agg_count = as_int(aggregate, "count", aggregate_path)
    agg_roots = as_int(aggregate, "roots_processed", aggregate_path)
    agg_segments = as_int(aggregate, "segments", aggregate_path)
    agg_double = as_int(aggregate, "double_pushes", aggregate_path)
    agg_odd = as_int(aggregate, "odd_pushes", aggregate_path)
    agg_max_stack = as_int(aggregate, "max_stack_len", aggregate_path)
    agg_max_value = as_int(aggregate, "max_value", aggregate_path)
    prefix_check = prefix_summary(m, depth)
    if prefix_check["prefix_count"] != prefix:
        raise ValueError(f"n={n}: independent prefix_count mismatch")
    if prefix_check["frontier_count"] != frontier:
        raise ValueError(f"n={n}: independent frontier_count mismatch")

    segment_paths = sorted(ROOT.glob(SEGMENT_GLOBS[n]))
    if len(segment_paths) != agg_segments:
        raise ValueError(
            f"n={n}: aggregate says {agg_segments} segments, found {len(segment_paths)}"
        )

    intervals = []
    segment_sum = 0
    roots_sum = 0
    double_sum = prefix_check["double_pushes"]
    odd_sum = prefix_check["odd_pushes"]
    max_stack = prefix_check["max_stack_len"]
    max_value = prefix_check["max_value"]
    for path in segment_paths:
        row = read_single_row_tsv(path)
        if row.get("mode") != "segment" or row.get("status") != "SEGMENT":
            raise ValueError(f"{path}: invalid segment mode/status")
        if as_int(row, "n", path) != n:
            raise ValueError(f"{path}: n mismatch")
        if as_int(row, "M", path) != m:
            raise ValueError(f"{path}: M mismatch")
        if as_int(row, "depth", path) != depth:
            raise ValueError(f"{path}: depth mismatch")
        if as_int(row, "prefix_count", path) != prefix:
            raise ValueError(f"{path}: prefix_count mismatch")
        if as_int(row, "frontier_count", path) != frontier:
            raise ValueError(f"{path}: frontier_count mismatch")
        first = as_int(row, "first_index", path)
        last = as_int(row, "last_index", path)
        roots = as_int(row, "roots_processed", path)
        if roots != last - first + 1:
            raise ValueError(f"{path}: roots_processed does not match interval size")
        intervals.append((first, last, path))
        segment_sum += as_int(row, "count", path)
        roots_sum += roots
        double_sum += as_int(row, "double_pushes", path)
        odd_sum += as_int(row, "odd_pushes", path)
        max_stack = max(max_stack, as_int(row, "max_stack_len", path))
        max_value = max(max_value, as_int(row, "max_value", path))

    intervals.sort()
    expected_first = 0
    for first, last, path in intervals:
        if first != expected_first:
            raise ValueError(
                f"n={n}: coverage gap/overlap before {path.name}; expected {expected_first}, got {first}"
            )
        expected_first = last + 1
    if expected_first != frontier:
        raise ValueError(f"n={n}: coverage ended at {expected_first - 1}, frontier={frontier}")

    total = prefix + segment_sum
    if roots_sum != agg_roots or roots_sum != frontier:
        raise ValueError(f"n={n}: roots_processed mismatch")
    if total != agg_count:
        raise ValueError(f"n={n}: aggregate count mismatch: {total} != {agg_count}")
    if agg_count != EXPECTED_NEW[n]:
        raise ValueError(f"n={n}: expected-new ledger mismatch")
    if double_sum != agg_double:
        raise ValueError(f"n={n}: double_pushes mismatch")
    if odd_sum != agg_odd:
        raise ValueError(f"n={n}: odd_pushes mismatch")
    if max_stack != agg_max_stack:
        raise ValueError(f"n={n}: max_stack_len mismatch")
    if max_value != agg_max_value:
        raise ValueError(f"n={n}: max_value mismatch")

    detail = {
        "depth": depth,
        "prefix": prefix,
        "frontier": frontier,
        "segments": agg_segments,
        "segment_sum": segment_sum,
        "roots": roots_sum,
        "max_stack": max_stack,
        "max_value": max_value,
    }
    return agg_count, detail


def format_table(rows: Iterable[Tuple[int, int, str]]) -> str:
    out = ["| n | value | source |", "|---:|---:|---|"]
    for n, value, source in rows:
        out.append(f"| {n} | {value} | {source} |")
    return "\n".join(out)


def main() -> int:
    validate_bfile()
    found: List[Tuple[int, int, str]] = []
    for n, value in validate_range():
        found.append((n, value, "range TSV"))
    segment_details = {}
    for n in (26, 27, 28):
        value, detail = validate_segments(n)
        found.append((n, value, "independent segment aggregation"))
        segment_details[n] = detail

    output_path = ROOT / "validation/A224540_new_results_independent_validation.md"
    lines = [
        "# A224540 New Results Independent Validation",
        "",
        "## Verdict",
        "",
        "PASS. The new-result campaign artifacts for n=22..28 are internally consistent.",
        "This checker did not invoke the production binary; it independently parsed TSV files,",
        "validated the extended b-file, recomputed powers 3^n, verified segment coverage,",
        "and recomputed aggregate sums.",
        "",
        "## Certified Values From Artifacts",
        "",
        format_table(found),
        "",
        "## Segment Campaign Checks",
        "",
        "| n | depth | prefix | frontier | segments | segment_sum | total |",
        "|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for n in (26, 27, 28):
        d = segment_details[n]
        lines.append(
            f"| {n} | {d['depth']} | {d['prefix']} | {d['frontier']} | "
            f"{d['segments']} | {d['segment_sum']} | {EXPECTED_NEW[n]} |"
        )
    lines.extend(
        [
            "",
            "## Artifact Hashes",
            "",
            "| path | sha256 |",
            "|---|---|",
        ]
    )
    hash_paths = [
        BFILE_PATH,
        RANGE_PATH,
        AGGREGATES[26],
        AGGREGATES[27],
        AGGREGATES[28],
        ROOT / "src/a224540.c",
        ROOT / "src/Makefile",
    ]
    for path in hash_paths:
        lines.append(f"| `{path.relative_to(ROOT)}` | `{sha256(path)}` |")
    lines.extend(
        [
            "",
            "## Scope Note",
            "",
            "This is an independent artifact-level validation, not a full independent",
            "recomputation of every inverse-Collatz subtree. A full recomputation of",
            "n=28 would require another production-scale campaign.",
            "",
        ]
    )
    output_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"PASS wrote {output_path.relative_to(ROOT)}")
    for n, value, source in found:
        print(f"a({n}) = {value} [{source}]")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
