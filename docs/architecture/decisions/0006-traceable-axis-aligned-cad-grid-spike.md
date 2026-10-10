# ADR 0006: Traceable axis-aligned CAD-to-grid spike

**Status:** Accepted
**Date:** October 10, 2026

## Context

The FDTD path needs a derived computational model, but exact CAD must remain authoritative. A grid that stores only material numbers loses the persistent topology identities needed to explain a port, diagnose a changed role, invalidate a cache, or compare results after regeneration. Beginning with unrestricted B-rep voxelization would combine topology resolution, component transforms, geometric intersection, material precedence, subcell policy, and solver coupling before those contracts are observable.

## Decision

Introduce two compiled boundaries. `em-geometry` resolves validated ASIR roles and ports against a geometry snapshot with the exact evaluated geometry hash. `em-mesh` converts that computational geometry into a uniform Cartesian grid.

The initial snapshot supports world-space axis-aligned volume boxes and axis-aligned planar patches. Every derived volume, surface, material cell, port face, and reference plane retains its ASIR role or port ID and persistent CAD feature/local topology ID. The prepared model also retains the canonical ASIR fingerprint, geometry hash, material catalog, port mode, orientation, impedance, de-embedding distance, and termination.

ASIR collections are canonicalized by stable ID before preparation output is produced. Snapshot order and ASIR collection order therefore cannot alter grid precedence or mappings.

Box-cell and patch-cell intersection measures are calculated analytically. Region and surface mappings retain exact subcell occupancy fractions. A solver-facing material cell also records a dominant material using an explicit threshold from the grid specification; the default is `0.5`. Overlapping non-background material volumes fail until an explicit physical precedence rule exists. Distinct regions sharing a coarse cell remain visible as mixed-cell warnings and separate subcell mappings.

Each mapped feature records exact measure, subcell-integrated measure, binary cell-center measure, relative binary error, minimum feature width in cells, cut-cell count, and a staircasing risk classification. Stale geometry, missing identities, kind mismatches, invalid primitives, out-of-domain geometry, hard cell limits, and unmappable surfaces fail with typed diagnostics.

## Alternatives

- Pass live kernel handles into the mesh. This would cross the process/schema boundary and make restart and provenance dependent on transient state.
- Store only a material index per cell. That would discard partial occupancy and the CAD identity needed for explanation and invalidation.
- Resolve ties from input order. ASIR keyed collections are order-independent, so equivalent studies could generate different grids.
- Silently prioritize conductor or later-listed materials in overlaps. Physical precedence must be an explicit policy with validation evidence.
- Implement the general B-rep intersector immediately. It would obscure whether failures came from identity transfer, geometry approximation, or intersection robustness.

## Consequences

- The project now has an executable, deterministic CAD-role-to-cell contract with audit paths back to persistent topology IDs.
- Cost estimation can consume exact cell counts and observed cut-cell/tiny-feature pressure from this grid representation.
- The spike is limited to pretransformed axis-aligned boxes and patches. Curved or rotated B-reps, healing, defeaturing, conformal cells, coatings, thin sheets/wires, and production port calibration remain unfinished.
- Subcell metadata is diagnostic today; the homogeneous reference FDTD path does not yet apply heterogeneous coefficients from this grid.
- A future exact B-rep adapter can replace the primitive source while preserving the computational-geometry and grid evidence contracts.
