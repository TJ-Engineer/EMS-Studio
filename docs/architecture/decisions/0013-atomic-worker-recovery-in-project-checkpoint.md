# 0013: Atomic worker recovery inside the compound project checkpoint

**Status:** Accepted  
**Date:** October 12, 2026

## Context

The first desktop Worker Experience could checkpoint and resume exact reference-solver state in a fresh worker object, but CAD+ASIR project recovery explicitly omitted the prepared job and checkpoint. Closing the desktop therefore discarded the only UI route back to that execution, even though the worker contract already had bounded versioned resume messages.

Durability must not permit a checkpoint to reattach to different CAD, ASIR, grid, solver build, or resource authority. Loading a valid document first and discovering bad worker state afterward would also create a partial project transition.

## Decision

Version the compound format as `oep-project-checkpoint-2` and append an optional opaque worker-recovery blob. The blob is an IPC-framed worker-protocol `ResumeCommand` with correlation ID zero. It carries the immutable `PreparedSimulationJob` and exact `CheckpointArtifact`; the nested frame supplies its own version, checksum, text/blob bounds, and resume-only decoding policy.

Keep the blob opaque in `simulation_ir` so the CAD+ASIR recovery codec does not depend on `simulation_orchestrator`. Project v1 remains readable and produces no worker state. A worker record requires an ASIR study.

On save, the desktop requests a current checkpoint for running work when possible and writes CAD, ASIR, and worker bytes through the existing durable atomic replacement. On open, it stages document restore, exact-kernel hydration, current CAD authority, nested decode, canonical ASIR fingerprint, supported solver build, and fresh-worker `resume` before replacing any active session.

## Alternatives considered

- A QSettings checkpoint path was rejected because settings are neither project-authoritative nor an atomic CAD+ASIR transaction.
- A separate sidecar file was rejected because independently advanced files can combine incompatible document and worker identities after interruption or copying.
- Serializing worker C++ objects was rejected because object layout, pointers, and implementation lifetimes are not a persistence contract.
- Letting project load succeed while dropping invalid worker bytes was rejected because it hides corruption and breaks the user's expectation that the saved execution is part of the project.

## Consequences

A verified reference job now resumes at exact saved progress in a fresh desktop controller and can complete and collect artifacts. Corrupt nested data, changed ASIR content, unsupported solver identity, and stale current CAD reject before activation, preserving the previously open project.

The format stores a restart point, not a live OS process. External worker discovery, ownership leases, heartbeats, disconnect/retry semantics, and durable queues require a separate supervision design. Completed normalized results also remain a separate immutable result model.
