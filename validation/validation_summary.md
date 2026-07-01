# Validation Summary

## Date And Scope

This GitHub/Zenodo release package validates the extension of A224540 through
`a(28)`.

## Certified Values

| n | a(n) | certification |
|---:|---:|---|
| 22 | 12406199367 | range campaign |
| 23 | 37216010685 | range campaign |
| 24 | 111651745638 | range campaign |
| 25 | 334951946950 | range campaign |
| 26 | 1004855123675 | segmented aggregate |
| 27 | 3014552251123 | segmented aggregate |
| 28 | 9043690122807 | segmented aggregate |

## Commands

Build and regression:

```bash
make -C src all
make -C src test
make -C src sanitize
```

Independent artifact-level validation:

```bash
python3 tools/a224540_validate_new_results.py
```

## Campaign Summary

| scope | mode | depth | segment files | elapsed seconds | status |
|---|---|---:|---:|---:|---|
| a(22)..a(25) | range | n/a | 0 | 1634.339440650 | COMPUTED |
| a(26) | segmented | 18 | 1 | 3021.577599397 | AGGREGATED_NO_ORACLE |
| a(27) | segmented | 20 | 2 | 9039.147276653 | AGGREGATED_NO_ORACLE |
| a(28) | segmented | 24 | 14 | 26987.874523042 | AGGREGATED_NO_ORACLE |

## Data Checks

- `data/b224540.txt` is contiguous for `n = 0..28`.
- The b-file ends with a real final blank line.
- Extracting `(n, a(n))` from `results/A224540_terms.tsv` matches
  `data/b224540.txt`.
- The independent validator reports `PASS`.

## Checksums

See `validation/checksums.sha256` for the final package checksum sweep.
