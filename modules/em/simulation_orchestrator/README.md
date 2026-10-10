# Reference simulation worker orchestration

The compiled `oep::simulation_orchestrator` module implements the first authoritative simulation lifecycle for the FP64 reference FDTD solver.

`ReferenceSimulationWorker` validates prepared jobs before allocation. It checks identity, grid validity, time-step and resource-estimate agreement, calibration build identity, and required budget approval. Accepted jobs advance in bounded solver slices and expose typed status. Cancellation is idempotent and terminal. Only successful jobs expose raw artifacts.

Checkpoints wrap the complete reference-solver checkpoint with a content identity plus job ID, ASIR fingerprint, solver build, and exact completed step count. Resume verifies all wrapper fields against the decoded solver state before a fresh worker accepts the job. This makes restart explicit and prevents a checkpoint from being attached to another study, job, build, or grid.

`WorkerProtocolSession` maps typed submit, resume, status, cancel, checkpoint, and collect commands onto versioned `oep::ipc` frames. Every payload declares `oep-simulation-worker-1.0` independently of the generic frame version, and unsupported contract versions fail before command decoding. The `simulation-worker --batch` entry point processes a bounded request stream in a separate process and returns one correlated reply per request. The process integration test checkpoints a running job, exits the worker, resumes it in a new process, and verifies final artifact provenance.

## Deliberate limits

- The batch entry point is a deterministic process-boundary harness. A long-lived socket or pipe server, queue persistence, discovery, retries, heartbeats, and crash supervision remain future orchestration work.
- Execution is single-process, single-threaded reference FDTD with a small fixed scheduling quantum.
- Collection returns the raw final solver checkpoint. Normalization into immutable S-parameter, field, and pattern results belongs to `simulation-results`.
- The current prepared job embeds a homogeneous reference grid. It will later reference a content-addressed computational model produced from validated ASIR.
