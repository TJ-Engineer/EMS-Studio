# Electromagnetic solver lifecycle contract

The header-only `oep::em_solver_api` boundary defines the solver-neutral types shared by the orchestrator, worker process, and future UI adapter. Its versioned contract covers:

- a prepared three-dimensional structured-grid job and its ASIR study fingerprint;
- the immutable resource-estimate snapshot and any explicit budget approval;
- queued, running, cancelling, cancelled, succeeded, and failed job states;
- typed validation, transition, solver, checkpoint, and protocol diagnostics;
- restart checkpoints and raw solver artifacts carrying job, study, solver-build, and progress provenance.

The UI does not own these states. A worker response is authoritative, and successful raw artifacts are available only after the job reaches `succeeded`.

## Deliberate limits

This first contract carries the reference FDTD grid directly because a general computational-model manifest is not implemented yet. Capability discovery, heterogeneous materials, execution profiles, logs, partial results, normalized immutable result sets, and remote scheduling require later compatible contract revisions.
