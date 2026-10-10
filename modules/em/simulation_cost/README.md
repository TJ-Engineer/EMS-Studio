# Reference FDTD cost estimator

The compiled `oep::simulation_cost` module turns a uniform Cartesian grid and an explicit simulated duration into a reviewable FP64 reference-solver estimate before field arrays are allocated.

The deterministic part of the model uses the exact staggered Yee extents from the reference engine. It reports logical grid cells, electric and magnetic field values, Courant-limited time step, step count, cell updates, field and transient-scratch bytes, serialized checkpoint bytes, peak payload memory, and retained checkpoint/result storage. The checkpoint estimate matches the version-1 three-dimensional reference checkpoint after at least one time step.

Runtime is deliberately hardware-specific. `calibrate_reference_runtime` consumes named hardware/build observations and calculates measured cell updates per second plus a prediction interval that covers the calibration residuals and a caller-supplied uncertainty floor. Estimates preserve both hardware and solver-build IDs. They are not portable performance claims.

ASIR cell, memory, runtime, and storage budgets are assessed independently. Runtime uses the conservative upper prediction bound. An exceedance remains visible in the returned estimate, and `requires_escalation` follows the ASIR escalation policy; an over-budget request is never silently rewritten to lower fidelity.

## Deliberate limits

- The model describes the current single-process FP64 reference Yee implementation. CPML auxiliary arrays, dispersive coefficients, heterogeneous material updates, monitor transforms, GPU storage, MPI halos, and normalized result artifacts require separately versioned execution profiles.
- Memory values count solver payloads and configured calibration overhead rather than allocator bookkeeping or whole-process RSS.
- Runtime is proportional to logical cell updates. New kernels or compiler/hardware changes require new calibration observations.
- The workload duration is explicit because ASIR 0.1 does not yet carry a finalized observable-based termination contract.
