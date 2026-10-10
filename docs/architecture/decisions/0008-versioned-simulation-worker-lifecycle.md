# ADR 0008: Versioned simulation-worker lifecycle across the process boundary

**Status:** Accepted
**Date:** October 10, 2026

## Context

The reference FDTD engine, traceable CAD-to-grid spike, and measured cost estimator establish what can run and what it should cost. Running that work in the UI process would still make solver state, cancellation, and recovery depend on presentation lifetime. The architecture requires an authoritative worker with typed validation, bounded messages, explicit resource approval, checkpoint recovery, and result provenance.

The first implementation also needs a testable process seam without prematurely selecting a platform socket library, daemon supervisor, durable queue, or remote scheduler.

## Decision

Define the common job, resource snapshot, state, diagnostic, checkpoint, and raw-artifact types in `em-solver-api`. A prepared job carries its ASIR fingerprint, complete reference-grid request, immutable resource estimate, calibration solver-build identity, and optional approval ID. The worker rejects malformed, mismatched, or unapproved work before solver allocation.

Implement the authoritative state machine in `simulation-orchestrator`. Jobs begin queued, advance through bounded reference-FDTD slices, and finish as succeeded, failed, or cancelled. Cancellation is idempotent. Raw artifacts are available only after success.

Wrap solver checkpoints with job ID, study fingerprint, solver build, exact completed step count, and a content identity. Resume in a fresh worker verifies the wrapper, decodes the checksummed solver checkpoint, and compares its configuration and progress with the prepared job before accepting it.

Use the compiled `ipc` module for a generic version-1 local frame containing a message type, correlation ID, bounded payload, and corruption checksum. Independently declare `oep-simulation-worker-1.0` in every typed payload so worker-contract evolution does not depend on the generic envelope version. The typed worker protocol maps submit, resume, status, cancel, checkpoint, and collect to those frames. The initial `simulation-worker --batch` interface reads and writes bounded frame streams, which gives tests a real process boundary while leaving the eventual pipe or socket transport open.

## Alternatives

- Call the solver directly from the UI. This would make UI lifetime authoritative for progress and recovery and would let a solver fault terminate the application process.
- Use ad hoc command-line arguments or untyped JSON files. This would weaken enum, numeric, provenance, size-limit, and correlation guarantees at the boundary.
- Select a production daemon and durable scheduler now. Queue retention, discovery, security, remote execution, and supervision policy are not yet defined and are unnecessary for proving the lifecycle contract.
- Recover an interrupted job from worker memory. Process memory cannot survive the crash or restart that the acceptance case must cover.

## Consequences

- Validation, resource approval, progress, cancellation, checkpointing, restart, and raw artifact collection now cross a real process boundary with typed correlated replies.
- A reference job can resume in a new worker process from an exact solver checkpoint while retaining ASIR and solver-build provenance.
- Malformed or oversized IPC fails before the domain command is dispatched, and cancelled jobs cannot be presented as successful results.
- The current transport is a bounded batch harness rather than a long-lived service. Queue durability, incremental transport, crash supervision, normalized results, and UI binding remain explicit later work.
