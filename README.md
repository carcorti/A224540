# A224540: Computational extension through a(28)

This repository contains code, data, selected campaign and validation artifacts, and an accompanying paper supporting a computational extension of OEIS sequence [A224540](https://oeis.org/A224540) through `a(28)`.

A224540 counts the positive integers `k` whose Collatz trajectory, stopped at the first occurrence of `1`, never exceeds `3^n`. Equivalently,

```text
a(n) = #{ k >= 1 : max of the stopped Collatz trajectory of k is <= 3^n }.
```

The bound is inclusive. The stopped trajectory of `1` is `[1]`, so `1` is counted for every `n >= 0`.

## Publication metadata

This repository is the public GitHub/Zenodo release package for version `v1.0`.

```text
Repository: https://github.com/carcorti/A224540
Zenodo DOI: 10.5281/zenodo.21096334
```

## Main results

The local OEIS snapshot used for this project contained terms through `a(21)`. This repository extends the b-file contiguously through `a(28)`.

| n   | a(n)          | Source in this repository |
| ---:| -------------:| ------------------------- |
| 22  | 12406199367   | range campaign            |
| 23  | 37216010685   | range campaign            |
| 24  | 111651745638  | range campaign            |
| 25  | 334951946950  | range campaign            |
| 26  | 1004855123675 | segmented aggregate       |
| 27  | 3014552251123 | segmented aggregate       |
| 28  | 9043690122807 | segmented aggregate       |

The canonical OEIS-style b-file in this repository is:

```text
data/b224540.txt
```

Additional result and certification tables are provided for cross-checking; they are not competing b-files.

## Repository structure

```text
.
├── data
│   ├── b224540.txt
│   └── certified_terms.tsv
├── .gitignore
├── LICENSE
├── CITATION.cff
├── paper
│   ├── A224540.pdf
│   └── A224540.tex
├── README.md
├── results
│   └── A224540_terms.tsv
├── src
│   ├── a224540.c
│   └── Makefile
├── tools
│   ├── a224540_small_topdown.py
│   └── a224540_validate_new_results.py
└── validation
    ├── A224540_campaign_and_validation_summary.md
    ├── A224540_final_ledger.md
    ├── A224540_new_results_independent_validation.md
    ├── campaigns/
    ├── checksums.sha256
    ├── run_manifest.tsv
    ├── validation_notes.md
    └── validation_summary.md
```

## Method

The computation uses the inverse Collatz tree rooted at the terminal value `1`, truncated by the inclusive bound `M = 3^n`.

For a node `x`, the inverse children used by the production traversal are:

- `2x`, when `2x <= M`;
- `(x - 1) / 3`, when this is a valid odd positive predecessor under the stopped-at-1 convention.

The production implementation is the C17 program:

```text
src/a224540.c
```

It performs an iterative depth-first traversal and counts generated nodes. For the largest terms, the traversal is split at a fixed frontier depth into segmented subtree campaigns, then recombined by aggregate checks.

## Production campaigns

| n      | M = 3^n        | Mode      | Depth | Segments | Elapsed seconds |
| ------:| --------------:| --------- | -----:| --------:| ---------------:|
| 22--25 | 3^22--3^25     | range     | —     | 0        | 1634.34         |
| 26     | 2541865828329  | segmented | 18    | 1        | 3021.58         |
| 27     | 7625597484987  | segmented | 20    | 2        | 9039.15         |
| 28     | 22876792454961 | segmented | 24    | 14       | 26987.87        |

The elapsed times are wall-clock measurements recorded in the campaign manifests and summarized in the paper.

## Validation boundary

The repository provides selected validation artifacts, not a claim of a second complete production-scale traversal by an independent implementation.

The validation record includes:

- replay of the known OEIS prefix `a(0)` through `a(21)`;
- C implementation checks and small-scale validation targets;
- a small independent top-down Python enumerator through `n = 14`;
- segmented-versus-monolithic checks on small instances;
- sanitizer smoke validation;
- independent artifact-level validation by `tools/a224540_validate_new_results.py`;
- checks of b-file contiguity, powers `3^n`, segment coverage, and aggregate sums.

The independent validator does not call the production binary and does not repeat a full traversal for `n = 28`. Its role is to verify artifact consistency and aggregation.

## Build and validation

The project is built from the `src/` directory using the supplied Makefile.

```bash
make -C src all
```

The validation target recorded in the paper is:

```bash
make -C src all validate sanitize RUN_ID=20260630Tbfile_extended_final
```

The public Makefile also provides:

```bash
make -C src validate
make -C src test
make -C src sanitize
```

Here `test` is an alias for `validate`. Exact production and validation command records are preserved in:

```text
validation/run_manifest.tsv
validation/campaigns/
```

## Hardware and software environment

The recorded local production campaign used:

```text
CPU: AMD Ryzen 9 7940HS
Logical CPUs: 16
RAM: 64 GB DDR5
OS: Linux Mint 22.3 on x86-64
Compiler: GCC 13.3.0
Language standard: C17
```

The release Makefile uses optimized native builds and the GCC/Clang `__uint128_t` extension for checked intermediate arithmetic.

## Data and reproducibility artifacts

Essential reproducibility artifacts include:

```text
src/a224540.c
src/Makefile
data/b224540.txt
data/certified_terms.tsv
results/A224540_terms.tsv
validation/run_manifest.tsv
validation/campaigns/
tools/a224540_validate_new_results.py
```

The validation summaries and ledgers under `validation/` document the certification boundary and aggregate checks.

## Paper

The accompanying paper is:

```text
paper/A224540.tex
paper/A224540.pdf
```

Title:

```text
Computational Extension of OEIS Sequence A224540:
New Terms Through a(28) via a Truncated Inverse Collatz Tree
```

## Citation

Please cite the repository metadata in `CITATION.cff` and the accompanying paper.

Citation metadata:

```text
Carlo Corti, A224540: Computational extension through a(28) by truncated inverse Collatz-tree traversal, 2026.
Repository: https://github.com/carcorti/A224540
Zenodo DOI: 10.5281/zenodo.21096334
```

## License

This repository is released under the MIT License. See `LICENSE`.

## References

- OEIS Foundation Inc., Sequence A224540, The On-Line Encyclopedia of Integer Sequences, https://oeis.org/A224540.
- J. C. Lagarias, *The 3x+1 problem and its generalizations*, American Mathematical Monthly 92 (1985), no. 1, 3--23.
- J. C. Lagarias, editor, *The Ultimate Challenge: The 3x+1 Problem*, American Mathematical Society, 2010.
