# 0011: Immutable persistent-ID viewport scene packets

**Status:** Accepted  
**Date:** October 12, 2026

## Context

The Qt shell previously displayed a staging grid while the exact OCCT body remained inside the CAD evaluator. Rendering kernel objects directly would couple UI lifetime and threading to OCCT and would expose transient kernel enumeration IDs as selections. Retaining a last-good display after failed regeneration would also misrepresent current document authority.

## Decision

The geometry-kernel boundary supplies disposable tessellation with transient kernel face and edge IDs. The `tessellation` module immediately translates those IDs through the final feature artifact's topology bindings and emits a renderer-neutral scene packet containing mesh vertices, triangles, edge polylines, bounds, geometry identity, and document-persistent selection IDs.

The shell publishes immutable packets through a `SceneAssembly`. An instance carries a persistent occurrence ID, affine transform, material, visibility, and shared ordered mesh LODs. Camera, clipping, selection, and preselection are separate presentation state. The first renderer uses deterministic orthographic CPU projection and Qt Quick Canvas drawing. Hit testing returns an instance-qualified persistent topology ID. Failed or unavailable current geometry clears the scene; the viewport never promotes last-good recovery geometry to current display.

## Alternatives considered

- Passing OCCT shapes into QML was rejected because it crosses the kernel boundary and cannot provide durable document identities.
- Selecting by triangle index was rejected because tessellation density and ordering are disposable.
- Rendering last-good geometry without stale treatment was rejected because it conflicts with document authority.
- Binding the first implementation to private Qt RHI APIs was deferred. The stable scene-packet contract permits a public-API GPU renderer without changing CAD ownership.

## Consequences

Exact evaluated geometry is now visible and interactive in `qt-cad`, and face selection round-trips to the same persistent IDs used by ASIR. Kernel adapters that do not implement tessellation report a typed unsupported-operation diagnostic.

The CPU/Canvas renderer remains the first integration implementation. Shared immutable instancing, transforms, materials, visibility, and budget-selected LODs now establish the storyboard's measured one-million-source-triangle boundary: the strict-debug acceptance projects a 1,048,576-source-triangle assembly below the 16.667 ms core-projection gate. The measurement uses coarse display LODs and is a regression fixture, not a portable GPU claim. GPU resource management, ten-million-triangle qualification, adaptive LOD generation, and manual visual review remain later performance work.
