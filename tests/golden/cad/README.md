# CAD kernel golden corpus

Every geometry-kernel candidate runs through `run_kernel_corpus`. The runner emits the same case IDs, categories, normalized body summaries, topology-provenance checks, typed failures, and cancellation checks regardless of the backend.

| Case ID | Category | Contract evidence |
|---|---|---|
| `extrude-rectangular-prism` | Valid | Volume, bounds, solid topology, semantic cap and edge provenance |
| `extrude-zero-area-profile` | Degenerate | No body and a typed `degenerate_geometry` diagnostic |
| `cut-through-rectangular-prism` | Topology-changing | Immutable input, changed topology, generated and modified provenance |
| `fillet-valid-and-oversized` | Topology-changing | Valid edge fillet plus classified oversized-radius failure |
| `linear-pattern-three-instances` | Valid | Instance count, aggregate volume, and bounds |
| `resolve-extrude-end-cap-plane` | Attached sketch | Stable semantic face role and recovered planar frame |
| `extrude-depth-parameter-sweep` | Parameter sweep | Monotonic volume and repeatable content hash |
| `heal-and-exact-snapshot-roundtrip` | Recovery | Healing preserves a valid body; exact snapshots are deterministic and restorable |
| `pre-cancelled-extrusion` | Cancellation | No body and a typed `cancelled` diagnostic |

The fake adapter is deterministic scaffolding for the contract. It does not claim exact B-rep capability. Exact-kernel adapters must execute this same runner, then add backend-specific robustness cases and native artifact capture.

## Topology mutation corpus

`run_topology_mutation_corpus` drives the persistent-topology service with both the fake and OCCT adapters. Kernel topology IDs are deliberately treated as transient; application references use feature-owned `PersistentTopologyId` values.

| Case ID | Evidence |
|---|---|
| `inspect-topology-signatures` | Surface/curve class, measure, centroid, orientation, parameter bounds, and adjacency are available for reconciliation |
| `extrude-sweep-preserves-attached-face` | A changed extrusion keeps the end-cap persistent ID and its semantic sketch frame |
| `cut-preserves-modified-cap` | Kernel `modified` provenance carries a cap reference through a topology-changing through cut |
| `fillet-classifies-source-and-generated-topology` | A source edge is preserved or explicitly diagnosed, the generated fillet face receives a new ID, and a persistent planar result face remains sketchable |
| `pattern-spacing-preserves-instance-ids` | Pattern instance IDs remain bound to their semantic roles when spacing changes |

The unit mutation suite adds adversarial deletion, split, equal-signature, conflicting-provenance, and many-to-one fixtures. These cases must remain unresolved with typed diagnostics; transient ordering is never used to choose a winner.

## Reel 2 modeling-thread corpus

`run_modeling_thread_corpus` drives the complete sketch → extrude → fillet → attached sketch → cut story against fresh kernel processes. Both the fake adapter and OCCT must pass the same sequence.

| Case ID | Evidence |
|---|---|
| `sketch-extrude-fillet-attached-sketch-cut` | The initial model and first-dimension change regenerate successfully, alter the exact body, and retain the attached sketch's persistent support-face ID |
| `classified-failure-denies-stale-geometry` | An oversized fillet commits as semantic state, leaves clean ancestors untouched, blocks descendants, emits a typed failure, denies stale geometry, and saves atomically |
| `reopen-failed-repair-undo-redo` | A fresh evaluator hydrates and verifies the failed checkpoint; repair, undo, and redo reproduce the correct failed and valid states |
| `repaired-save-terminate-reopen` | A second process restart reproduces parameter values, feature outputs, topology diagnostics, persistent support ID, and exact body hash |

Document restore remains validation-only. The corpus calls explicit CAD evaluator hydration first, as recorded in [ADR 0001](../../../docs/architecture/decisions/0001-explicit-cad-evaluator-hydration.md).
