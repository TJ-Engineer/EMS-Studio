# Software Design Descriptions

**Baseline:** October 12, 2026 · working-tree implementation · design revision 7  
**Status:** Maintained implementation descriptions; not a release certification.

These SDDs describe the current EMS Studio design for maintainers, reviewers, and support engineers. SDD means **Software Design Description**. The documents describe responsibilities, data structures, interfaces, processing, ownership, failure behavior, verification, and extension constraints. They are organized by subsystem rather than by individual class so that cross-module invariants stay visible.

The baseline includes uncommitted work. File links identify implementation locations, not an immutable release tag. Before distributing a release-specific design package, record the release commit and update its verification evidence.

## Reading map

| Document | Questions answered |
|---|---|
| [System design](SYSTEM_SDD.md) | What runs where, which components own authority, and which integrations exist? |
| [Platform design](PLATFORM_SDD.md) | How are revisions, transactions, recovery files, and IPC protected? |
| [CAD design](CAD_SDD.md) | How do sketches, exact geometry, topology identities, and restart hydration interact? |
| [Native viewport design](VIEWPORT_SDD.md) | How does exact geometry become an immutable scene, and how do interactions return persistent IDs? |
| [EM design](EM_SDD.md) | What do ASIR, geometry preparation, meshing, cost estimation, reference solving, and workers guarantee? |
| [Desktop design](DESKTOP_SDD.md) | How does Qt expose document/study commands and preserve layout and honest status? |
| [Worker experience](WORKER_EXPERIENCE_SDD.md) | How does the desktop prepare, run, checkpoint, restart, and collect a reference job? |
| [Verification and support](VERIFICATION_AND_SUPPORT.md) | Which tests support each contract, how are failures diagnosed, and what changes require coordinated updates? |

## Design authority and maintenance

The C++ contracts and tests establish implemented behavior. These SDDs explain that behavior; numbered [architecture decisions](../decisions/README.md) record the decisions behind it. The [development plan](../../implemention_plans/Fusio360_Vive_CAD_Architecture_and_Development_Plan.md) describes the larger intended architecture. The [storyboard](../../DEVELOPMENT_STORYBOARD.md) tracks outcome evidence and unfinished work.

When an interface, invariant, format, lifecycle, dependency, or supported numerical capability changes, update its SDD in the same change. Record material design changes in an ADR. Update verification links and the baseline date. Do not upgrade a planned integration to implemented solely because both endpoints exist.

## Vocabulary

| Term | Meaning |
|---|---|
| Semantic revision | Authoritative parameter and feature-graph state after an accepted edit |
| Evaluated revision | Revision associated with successful geometry evaluation |
| Current geometry | Geometry whose authority matches the active semantic revision |
| Last-good geometry | Historical successful snapshot; does not grant current authority after failure |
| Persistent topology ID | Feature/local identity reconciled across geometry regeneration |
| Kernel handle | Transient exact-body identity owned by a kernel instance |
| ASIR | Antenna Simulation Intermediate Representation, schema version 0.1 |
| Study fingerprint | Canonical SHA-256 identity of ASIR content |
| Recovery checkpoint | Internal versioned restart data, distinct from a public native document package |
| Preflight | Structural validation against live authority; not proof of solver capability or calibration |
| Worker checkpoint | Exact solver state bound to job, study, build, grid, and progress authority |

## Common design rules

1. Reject stale authority before mutation or simulation preparation.
2. Preserve diagnostics and provenance instead of hiding failure with cached success.
3. Keep numerical/kernel details behind compiled contracts, outside presentation code.
4. Bound decoded data before allocating from its declared lengths.
5. Preserve the reference numerical path and independent analytic tests when adding optimized execution.
6. Label unsupported and session-only capabilities in the UI and documentation.
