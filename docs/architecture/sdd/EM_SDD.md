# Electromagnetic Software Design Description

**Baseline:** October 12, 2026 · revision 3

## Scope and component interfaces

| Module | Input | Output/authority |
|---|---|---|
| `simulation_ir` | Study values and live CAD validation context | Typed validation report and canonical study fingerprint |
| `cad_study_draft` | Current example document or guarded study edit | Incomplete snapshot/draft or atomic edit result |
| `em_geometry` | Validated study and resolved matching CAD primitives | Traceable computational geometry or diagnostics |
| `em_mesh` | Computational geometry and Cartesian grid policy | Dominant materials, subcell/surface/port mappings, risk reports |
| `simulation_cost` | Reference workload, calibration, ASIR budget | Exact payload counts and bounded runtime prediction |
| `fdtd_engine` | Homogeneous lossless grid, sources/probes | FP64 staggered fields, energy, checkpoint state |
| `em_solver_api` | Prepared job and worker value contracts | Lifecycle state, diagnostics, checkpoints, raw artifacts |
| `simulation_orchestrator` | Prepared jobs or typed protocol commands | Validated execution, cancellation, restart, collection |

These are implemented foundations with separate acceptance paths. Arbitrary CAD geometry, heterogeneous mapped materials, authored ports/PML, and Qt study setup are not yet an integrated simulation pipeline.

## ASIR data design

ASIR 0.1 represents CAD snapshot identity; SI dimensions; coordinate frames; geometry references; electromagnetic roles; materials; ports; boundary policies; solver/mesh policy; resource budgets; monitors/goals; and execution provenance. Persistent references include expected topology kind. Materials retain explicit provenance and validity bands.

`ValidationContext` carries live document identity, semantic/evaluated revisions, current geometry hash, parameter authority, and resolved topology supplied by the caller. The validator compares study authority to this context and checks identifiers, dimensions, finiteness, physical ranges, references, bands, and required setup. A valid structure does not prove a solver can execute every declared policy.

`canonical_fingerprint` produces an `asir-0.1-sha256:` identity from canonical content. Equivalent ordering of identity-bearing collections must not create a different study. Encoding changes require fingerprint/schema coordination and tests. The [schema](../../../schemas/simulation_ir/asir-0.1.md) and [ADR 0002](../decisions/0002-validated-asir-boundary.md) define the structural boundary.

## Incremental draft commands

The example factory requires current evaluated geometry and the supported five-feature/parameter model. It captures exact geometry authority and converts millimeters to meters. Initial setup includes an explicit ideal-vacuum material and preview solver/mesh/budget policy. It deliberately lacks roles, ports, boundaries, requested observables, solver build, and execution provenance, so full preflight reports missing setup.

Every guarded definition edit compares expected canonical fingerprint and live CAD authority. It edits a copy, validates applicable collection/definition diagnostics, and replaces the study only on success. Material, boundary, and monitor repairs can be performed individually while full preflight retains problems elsewhere. A successful draft edit is not a globally valid execution request.

Deletion preserves dependencies: role/boundary references protect materials, monitor references protect ports, and paired boundaries protect their partners. Removing an otherwise unreferenced required item can deliberately return the draft to incomplete state. No command silently repairs dependencies or invents geometry.

## Geometry preparation

`CadGeometrySnapshot` supplies one evaluated hash and resolved persistent entities. Primitives are world/study-space axis-aligned boxes or planar patches in meters. Component transforms must be applied by the supplying CAD adapter. Planar tangential coordinates follow the other axes in ascending axis order; inward-normal sign is exactly ±1.

Preparation checks hash equality, unique nonempty persistent IDs, finite nondegenerate primitives, entity kinds, declared materials, and reference resolution. One resolved air/vacuum volume defines background/domain authority. Material-bearing volumes and surface roles remain separate records. Ports require planar faces, matching inward orientation, and parallel consistently oriented reference planes.

Preparation accumulates diagnostics and publishes no computational geometry when any error exists. Successful material/role/port records are sorted by stable ID and retain study fingerprint, geometry hash, source IDs, reference-plane identity, mode, impedance, de-embedding, and termination. Curved/rotated B-rep intersection, healing/defeaturing, coatings, and thin-sheet/wire models remain unfinished. See [ADR 0006](../decisions/0006-traceable-axis-aligned-cad-grid-spike.md).

## Cartesian mapping algorithm

Grid policy supplies origin, counts, spacing, dominant-material threshold (default 0.5), and hard cell cap. Counts/spacing and arithmetic are checked before allocation. Boxes are intersected analytically with cells to retain exact occupancy fractions. Planar patches map to normal layers and tangential cell area fractions; port face and reference plane map separately.

One dominant material is recorded per cell; ties follow stable role-ID order. Separate mappings preserve subcell occupancy even when a material is not dominant. Out-of-domain geometry, overlapping non-background material volumes, invalid policy, and unmappable surfaces are typed failures.

Risk reports compare exact measure, integrated subcell measure, binary center-sample measure/error, minimum width in cells, and cut-cell count. Mixed/under-resolved cells produce warnings. These diagnostics make staircasing visible; they do not implement a conformal numerical treatment. The reference solver does not consume these heterogeneous records yet.

## FP64 reference numerical design

`YeeGrid1d` stores E at nodes and H at cell centers. It supports paired periodic boundaries, PEC ends, and first-order Mur absorbing ends. `YeeGrid3d` has six structure-of-arrays buffers at staggered Yee locations and currently supports PEC faces only. Both use lossless homogeneous isotropic media and uniform spacing.

For medium wave speed `v`, the time step is the Courant factor times `dx/v` in 1-D or `1 / (v * sqrt(1/dx² + 1/dy² + 1/dz²))` in 3-D. Factors must be finite and in `(0,1]`. H and E advance in leapfrog order. Explicit hard/soft electric sources are validated before mutation, including domain and tangential PEC restrictions. Probes address actual staggered component locations.

The discrete energy pairs previous/current electric fields with half-step magnetic fields. Analytic helpers expose plane-wave numerical dispersion, PEC cavity frequencies, and waveguide cutoff/dispersion. Independent Maxwell tests supply acceptance tolerances; implementation self-comparison cannot replace them. See [ADR 0004](../decisions/0004-fp64-yee-reference-solver.md).

Solver checkpoints store complete FP64 field/configuration/time-step state and recent energy. Version, kind, extents, bounds, finite values, checksum, and trailing bytes are validated before usable restore. Canonical restart cases continue bit-for-bit. This internal state is wrapped in worker authority before job resume.

## Port contract versus execution

The independent RMS power-wave harness fixes normalized wave conventions, deterministic phase anchors, orientation, reference planes, passivity/reciprocity tests, and linear reconstruction semantics. See [ADR 0005](../decisions/0005-versioned-rms-port-contract.md). This is evidence for a port contract, not an implemented calibrated port in the current homogeneous reference grid.

UI discrete-port values and S-parameter monitor requests remain ASIR setup. 3-D CPML, calibrated ports, Fourier monitors, near-to-far transforms, conductivity/dispersion, and heterogeneous coefficients are unfinished.

## Cost and budgets

Reference estimates count exact staggered E/H values, FP64 storage, electric scratch, checkpoint payload, retained storage, time step, steps, and logical cell updates. Checked arithmetic rejects overflow. Estimates describe payloads plus explicit overhead, not whole-process RSS.

Calibration identifies hardware, solver build, thread count, measured cell updates/second, observations, and uncertainty. Runtime intervals account for residuals and a caller-supplied uncertainty floor. Cells, memory, runtime upper bound, and storage are assessed independently against budgets. Exceedance is reviewable and escalation follows policy; fidelity is never silently reduced. Hardware/compiler/kernel changes require recalibration. See [ADR 0007](../decisions/0007-measured-reference-fdtd-cost-model.md).

## Worker processing and restart

A `PreparedSimulationJob` includes job ID, study fingerprint, homogeneous grid, total steps, resource snapshot, and optional approval ID. The worker validates identity, grid, estimate agreement, calibration/build identity, and required approval before allocation. Lifecycle phases are queued, running, cancelling, cancelled, succeeded, and failed; terminal phases cannot be treated as running work.

Bounded `run_slice` advances exact step progress. Cancellation is idempotent and terminal. Only successful jobs expose raw final solver checkpoint artifacts. Checkpoint wrappers bind job ID, study fingerprint, build, completed steps, content identity, and payload. Resume verifies wrapper and decoded grid/state against the prepared job before accepting a fresh worker.

Protocol commands are submit, resume, status, cancel, checkpoint, and collect. Payload version is `oep-simulation-worker-1.0`, independent of frame version. Nested limits include 1 MiB text, 64 MiB blobs, and 4096 diagnostics. The batch process returns correlated replies and offers deterministic lifecycle acceptance. It is not a supervised, persistent queue or long-lived discovery/heartbeat/retry service. See [ADR 0008](../decisions/0008-versioned-simulation-worker-lifecycle.md).

The desktop adapts a structurally complete study into a bounded homogeneous reference job. Preparation records the compiled worker build and execution provenance and uses the real reference cost estimator. Project-checkpoint v2 may persist the prepared job and exact checkpoint as an opaque nested protocol frame; fresh-session restore verifies current CAD, canonical ASIR, solver build, grid, and solver payload before activation. This path demonstrates lifecycle and durability integration but does not consume the heterogeneous CAD-to-grid mapping or implement calibrated ports/PML. See the [Worker Experience SDD](WORKER_EXPERIENCE_SDD.md), [ADR 0012](../decisions/0012-desktop-reference-worker-experience.md), and [ADR 0013](../decisions/0013-atomic-worker-recovery-in-project-checkpoint.md).

## Project recovery

The [project checkpoint API](../../../modules/em/simulation_ir/include/oep/simulation_ir/project_checkpoint.hpp) bundles CAD recovery state, document identity, optional complete ASIR, and canonical fingerprint. The explicit ASIR field tuples define a versioned binary schema; payload/string/aggregate collection bounds, enum/boolean validation, integrity, and historical snapshot checks precede activation. Incomplete/stale drafts remain valid recovery content. Qt loading restores history and hydrates exact geometry separately. See [ADR 0010](../decisions/0010-atomic-cad-study-project-recovery.md). The helper links platform persistence and IPC; no Qt types are serialized.

## Extension constraints

Do not label an ASIR PML policy as working CPML or raw checkpoints as normalized results. New numerical policies need independent analytic/convergence evidence and an updated cost profile. New geometry paths must preserve persistent IDs and authority hashes. New worker services must specify crash/retry/idempotency, ownership, transport authentication where relevant, and durable job policy before being exposed as product capabilities.

## Implementation reference inventory

### `modules/em/simulation_ir`

- [cad_study_draft.hpp](../../../modules/em/simulation_ir/include/oep/simulation_ir/cad_study_draft.hpp) — `modules/em/simulation_ir/include/oep/simulation_ir`
- [simulation_ir.hpp](../../../modules/em/simulation_ir/include/oep/simulation_ir/simulation_ir.hpp) — `modules/em/simulation_ir/include/oep/simulation_ir`
- [cad_study_draft.cpp](../../../modules/em/simulation_ir/src/cad_study_draft.cpp) — `modules/em/simulation_ir/src`
- [simulation_ir.cpp](../../../modules/em/simulation_ir/src/simulation_ir.cpp) — `modules/em/simulation_ir/src`

### `modules/em/em_geometry`

- [README.md](../../../modules/em/em_geometry/README.md) — `modules/em/em_geometry`
- [computational_geometry.hpp](../../../modules/em/em_geometry/include/oep/em_geometry/computational_geometry.hpp) — `modules/em/em_geometry/include/oep/em_geometry`
- [computational_geometry.cpp](../../../modules/em/em_geometry/src/computational_geometry.cpp) — `modules/em/em_geometry/src`

### `modules/em/em_mesh`

- [README.md](../../../modules/em/em_mesh/README.md) — `modules/em/em_mesh`
- [cartesian_grid.hpp](../../../modules/em/em_mesh/include/oep/em_mesh/cartesian_grid.hpp) — `modules/em/em_mesh/include/oep/em_mesh`
- [cartesian_grid.cpp](../../../modules/em/em_mesh/src/cartesian_grid.cpp) — `modules/em/em_mesh/src`

### `modules/em/simulation_cost`

- [README.md](../../../modules/em/simulation_cost/README.md) — `modules/em/simulation_cost`
- [simulation_cost.hpp](../../../modules/em/simulation_cost/include/oep/simulation_cost/simulation_cost.hpp) — `modules/em/simulation_cost/include/oep/simulation_cost`
- [simulation_cost.cpp](../../../modules/em/simulation_cost/src/simulation_cost.cpp) — `modules/em/simulation_cost/src`

### `modules/em/fdtd_engine`

- [README.md](../../../modules/em/fdtd_engine/README.md) — `modules/em/fdtd_engine`
- [reference_fdtd.hpp](../../../modules/em/fdtd_engine/include/oep/fdtd/reference_fdtd.hpp) — `modules/em/fdtd_engine/include/oep/fdtd`
- [reference_fdtd.cpp](../../../modules/em/fdtd_engine/src/reference_fdtd.cpp) — `modules/em/fdtd_engine/src`

### `modules/em/em_solver_api`

- [README.md](../../../modules/em/em_solver_api/README.md) — `modules/em/em_solver_api`
- [solver_api.hpp](../../../modules/em/em_solver_api/include/oep/em_solver_api/solver_api.hpp) — `modules/em/em_solver_api/include/oep/em_solver_api`

### `modules/em/simulation_orchestrator`

- [README.md](../../../modules/em/simulation_orchestrator/README.md) — `modules/em/simulation_orchestrator`
- [reference_worker.hpp](../../../modules/em/simulation_orchestrator/include/oep/simulation_orchestrator/reference_worker.hpp) — `modules/em/simulation_orchestrator/include/oep/simulation_orchestrator`
- [worker_protocol.hpp](../../../modules/em/simulation_orchestrator/include/oep/simulation_orchestrator/worker_protocol.hpp) — `modules/em/simulation_orchestrator/include/oep/simulation_orchestrator`
- [reference_worker.cpp](../../../modules/em/simulation_orchestrator/src/reference_worker.cpp) — `modules/em/simulation_orchestrator/src`
- [worker_protocol.cpp](../../../modules/em/simulation_orchestrator/src/worker_protocol.cpp) — `modules/em/simulation_orchestrator/src`


### Project recovery implementation

- [project_checkpoint.hpp](../../../modules/em/simulation_ir/include/oep/simulation_ir/project_checkpoint.hpp) — compound recovery API and value state
- [project_checkpoint.cpp](../../../modules/em/simulation_ir/src/project_checkpoint.cpp) — explicit schema, bounded archive, identity checks, and atomic file path
