#!/usr/bin/env python3
"""Independent small-range top-down checker for OEIS A224540."""

from __future__ import annotations

import argparse
import sys
import time


def read_bfile(path: str) -> dict[int, int]:
    terms: dict[int, int] = {}
    with open(path, "r", encoding="ascii") as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            n_text, value_text = line.split()[:2]
            terms[int(n_text)] = int(value_text)
    return terms


def count_top_down(limit: int) -> tuple[int, int]:
    # State values: 0 unknown, 1 accepted, 2 rejected for this fixed limit.
    state = bytearray(limit + 1)
    state[1] = 1
    max_seen = 1

    for start in range(1, limit + 1):
        if state[start] != 0:
            continue
        path: list[int] = []
        x = start
        accepted = False
        while True:
            if x > max_seen and x <= limit:
                max_seen = x
            if x > limit:
                accepted = False
                break
            if state[x] == 1:
                accepted = True
                break
            if state[x] == 2:
                accepted = False
                break
            path.append(x)
            if x == 1:
                accepted = True
                break
            if x & 1:
                x = 3 * x + 1
            else:
                x //= 2
        mark = 1 if accepted else 2
        for y in path:
            if y <= limit:
                state[y] = mark

    return state.count(1), max_seen


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bfile", required=True)
    parser.add_argument("--max-n", type=int, default=14)
    parser.add_argument("--tsv", required=True)
    args = parser.parse_args()

    if args.max_n > 14:
        print("error: this checker is intentionally capped at n<=14", file=sys.stderr)
        return 2

    expected = read_bfile(args.bfile)
    with open(args.tsv, "w", encoding="ascii") as out:
        out.write("mode\tn\tM\tcount\texpected\tstatus\telapsed_sec\tmax_seen\n")
        for n in range(args.max_n + 1):
            limit = 3**n
            t0 = time.perf_counter()
            count, max_seen = count_top_down(limit)
            elapsed = time.perf_counter() - t0
            want = expected[n]
            status = "PASS" if count == want else "FAIL"
            out.write(
                f"small_topdown\t{n}\t{limit}\t{count}\t{want}\t"
                f"{status}\t{elapsed:.9f}\t{max_seen}\n"
            )
            out.flush()
            if status != "PASS":
                return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
