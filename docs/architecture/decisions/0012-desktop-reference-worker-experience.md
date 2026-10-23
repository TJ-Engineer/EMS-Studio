# 0012: In-process reference worker for the first desktop execution experience

**Status:** Accepted  
**Date:** October 12, 2026

## Context

The solver-neutral worker contract, calibrated estimator, and process protocol already prove execution and restart independently, while the desktop stopped at structural ASIR setup. Connecting the first UI directly to a production service would require supervision, durable job ownership, disconnect/retry behavior, and general CAD-to-grid preparation that do not yet exist.

## Decision

Add a Worker tab backed by an in-process `ReferenceSimulationWorker`. Preparation accepts a current study whose only missing preflight fields are solver-build and execution provenance. It records the compiled reference build, checksum, host, timestamp, approval, and generated operation, validates the resulting ASIR, and creates an immutable job from a real cost estimate.

Use a bounded homogeneous `12 × 10 × 8` preview grid derived from the supported example dimensions. Label it as a reference preview and do not imply that authored materials, ports, or PML have entered the numerical model. Drive execution through bounded worker slices and expose worker-owned status, cancellation, checkpoint, fresh-worker resume, and raw artifact collection.

Keep live job, checkpoint, and artifacts in the shell session and reset them whenever CAD or study authority changes. Durable project recovery is governed separately by [ADR 0013](0013-atomic-worker-recovery-in-project-checkpoint.md).

## Alternatives considered

- Synthetic QML progress was rejected because it would create a second, non-authoritative lifecycle.
- Launching the batch protocol for every timer slice was rejected because it is a test harness rather than a supervised service and would obscure process ownership.
- Feeding the axis-aligned CAD-to-grid spike into the worker was deferred because its material grid is not yet the worker's homogeneous solver input contract.
- Persisting opaque session state in QSettings was rejected because job/checkpoint integrity and project authority require a versioned bounded format.

## Consequences

Users can now estimate and run the real reference lifecycle from visible Qt controls, including cancellation, exact checkpoint resume in a fresh worker object, and provenance-bearing artifact collection. The UI and controller use the same lifecycle contracts as the independent worker tests.

Execution is synchronous in short slices on the Qt thread. Project-checkpoint v2 now restores exact progress in a fresh desktop session. External worker supervision, crash/disconnect retry, normalized result browsing, calibrated ports, general heterogeneous preparation, and production backends remain open. See the [Worker Experience SDD](../sdd/WORKER_EXPERIENCE_SDD.md).
