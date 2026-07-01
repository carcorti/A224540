# A224540 New Results Independent Validation

## Verdict

PASS. The new-result campaign artifacts for n=22..28 are internally consistent.
This checker did not invoke the production binary; it independently parsed TSV files,
validated the extended b-file, recomputed powers 3^n, verified segment coverage,
and recomputed aggregate sums.

## Certified Values From Artifacts

| n | value | source |
|---:|---:|---|
| 22 | 12406199367 | range TSV |
| 23 | 37216010685 | range TSV |
| 24 | 111651745638 | range TSV |
| 25 | 334951946950 | range TSV |
| 26 | 1004855123675 | independent segment aggregation |
| 27 | 3014552251123 | independent segment aggregation |
| 28 | 9043690122807 | independent segment aggregation |

## Segment Campaign Checks

| n | depth | prefix | frontier | segments | segment_sum | total |
|---:|---:|---:|---:|---:|---:|---:|
| 26 | 18 | 168 | 44 | 1 | 1004855123507 | 1004855123675 |
| 27 | 20 | 270 | 72 | 2 | 3014552250853 | 3014552251123 |
| 28 | 24 | 689 | 179 | 14 | 9043690122118 | 9043690122807 |

## Artifact Hashes

| path | sha256 |
|---|---|
| `data/b224540.txt` | `dee6553788e53f66b60f0196f1697b73d0eae1f15a85bd1ca044544c639c1b3d` |
| `validation/campaigns/a22_a25/a22_a25.tsv` | `b2604cae9e3df952d11f24a19f29b78a1a7b26f326a6acbdb8f10379a7f7887c` |
| `validation/campaigns/a26/aggregate_n26.tsv` | `eb14929de58076e6f07ab838aaa1b735a51d006e60f0491873f331cc37f9ae8a` |
| `validation/campaigns/a27/aggregate_n27.tsv` | `9ac77c2a1349f7faf79597ec9dd04d3f283b46b8ab5aaff04185f44a946063e9` |
| `validation/campaigns/a28/aggregate_n28.tsv` | `9e5d7c692c1adca4e0adcb51202376d173f7633b7177fe2b63294bab466d588d` |
| `src/a224540.c` | `8035683f7ba325158ffcbcc034c92411b563dd36a365679e827de770e3047c6c` |
| `src/Makefile` | `707a997bf47abe5ec76ca2949db400f6372da69af574860c002c01b6c071c807` |

## Scope Note

This is an independent artifact-level validation, not a full independent
recomputation of every inverse-Collatz subtree. A full recomputation of
n=28 would require another production-scale campaign.
