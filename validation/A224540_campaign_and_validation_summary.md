# A224540 Campaign and Validation Summary

## Production Artifacts

| artifact | path | sha256 |
|---|---|---|
| campaign C source | `src/a224540.c` | `8ed5f93d41eca9ef53defad5d8266a3ff13b592eab8f57f307d2f491deadcf46` |
| current C source | `src/a224540.c` | `8035683f7ba325158ffcbcc034c92411b563dd36a365679e827de770e3047c6c` |
| Makefile | `src/Makefile` | `707a997bf47abe5ec76ca2949db400f6372da69af574860c002c01b6c071c807` |
| extended b-file | `data/b224540.txt` | `dee6553788e53f66b60f0196f1697b73d0eae1f15a85bd1ca044544c639c1b3d` |
| independent validator | `tools/a224540_validate_new_results.py` | `cd33a4bd2a121117cd75657aec1030b7e9543ba904ff46e8e6700d705b8eab81` |

## New-Term Campaigns

| n range | mode | depth | segment files | elapsed, seconds | result status |
|---|---|---:|---:|---:|---|
| 22..25 | range | n/a | n/a | 1634.339440650 | `COMPUTED` |
| 26 | segmented | 18 | 1 | 3021.577599397 | `AGGREGATED_NO_ORACLE` |
| 27 | segmented | 20 | 2 | 9039.147276653 | `AGGREGATED_NO_ORACLE` |
| 28 | segmented | 24 | 14 | 26987.874523042 | `AGGREGATED_NO_ORACLE` |

## Segment Campaign Details

| n | M = 3^n | depth | prefix_count | frontier_count | roots_processed | segment_sum | total |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 26 | 2541865828329 | 18 | 168 | 44 | 44 | 1004855123507 | 1004855123675 |
| 27 | 7625597484987 | 20 | 270 | 72 | 72 | 3014552250853 | 3014552251123 |
| 28 | 22876792454961 | 24 | 689 | 179 | 179 | 9043690122118 | 9043690122807 |

## Verification Status

The new-term campaigns were computed with source hash
`8ed5f93d41eca9ef53defad5d8266a3ff13b592eab8f57f307d2f491deadcf46`, recorded
in the campaign checksum files. After the b-file extension, the replay-bfile
handling was adjusted so that rapid validation replays the compiled oracle
prefix while accepting the extended b-file structurally. The inverse-tree
counting, segmentation, and aggregation logic were not changed by this
post-campaign adjustment. The current source was validated with:

```text
make -C src all validate sanitize RUN_ID=20260630Tbfile_extended_final
```

The independent artifact-level validator was run with:

```text
python3 tools/a224540_validate_new_results.py
```

It independently parses TSV files, validates the extended b-file, recomputes
powers 3^n, reconstructs the finite frontier prefixes for segmented campaigns,
checks segment coverage, and recomputes aggregate sums.

## Paper Scope Note

The validation record supports a computational paper extending A224540 through
n = 28. The independent validation is artifact-level: it verifies the b-file,
the segment coverage, and the aggregate arithmetic without invoking the
production binary. It is not a second full production-scale traversal of every
subtree for n = 28.

Suggested paper wording:

```text
The segmented inverse-tree traversal was used for resumability and auditability
rather than load balancing. Equal-size intervals of the depth frontier can have
highly unequal computational costs, reflecting the irregular branching structure
of the inverse Collatz tree under the bound x <= 3^n. Segment-level telemetry
was retained as part of the certification data. An independent artifact-level
validator checked the extended b-file, frontier coverage, and aggregate sums;
it did not constitute a second full traversal of all n = 28 subtrees.
```
