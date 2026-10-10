# ADR 0007: Measured reference-FDTD cost model

**Status:** Accepted
**Date:** October 10, 2026

## Context

The first EM workflow must report cells, time step, steps, memory, runtime, and storage before execution. Cell counts alone are insufficient: the staggered Yee layout has component-specific extents, the reference update temporarily copies all electric fields, checkpointing creates another full serialized field payload, smaller cells reduce the Courant time step, and wall time depends on the hardware and solver build. A generic constant such as “six values per cell” would undercount boundaries and obscure which assumptions changed.

ASIR 0.1 already carries hard cell, memory, runtime, and storage budgets plus an escalation policy. The estimator must assess those limits without silently changing fidelity or discarding an over-budget estimate that a user needs to review.

## Decision

Compile `simulation_cost` as an implemented module with a reference-FDTD execution profile.

The deterministic model calculates the exact six staggered component extents for a uniform three-dimensional grid. FP64 field bytes follow those extents. Peak logical payload memory includes live field arrays, the update's electric-field scratch copies, checkpoint serialization when requested, and explicit fixed/per-cell calibration overhead. The checkpoint forecast uses the exact version-1 three-dimensional reference format after a completed step.

The Courant-limited time step uses the grid spacings, caller-declared maximum wave speed, and Courant factor. An explicit simulated duration rounds up to a whole step. Logical cell updates are the product of cells and steps, so refining all three axes exposes both the cubic cell increase and the additional steps caused by the smaller stable time step.

Runtime comes only from named observations containing grid cells, executed steps, and measured wall time. A calibration records hardware ID, solver-build ID, thread count, cell updates per second, and a relative prediction uncertainty covering both a declared floor and the calibration residuals. Runtime budget assessment uses the upper prediction bound.

Every ASIR resource dimension is assessed independently. An exceeded estimate remains available. `requires_escalation` becomes true only when at least one limit is exceeded and the ASIR policy requires escalation. The estimator never reduces resolution, duration, retained checkpoints, or result storage on its own.

## Alternatives

- Use a fixed bytes-per-cell and seconds-per-cell constant. This would hide staggered boundary extents, transient copies, checkpoint behavior, and hardware/build identity.
- Measure process RSS inside the estimator. RSS includes allocator, runtime, test harness, and operating-system behavior and cannot provide a deterministic portable contract.
- Reject all over-budget estimates. This would prevent the UI and orchestrator from showing which limits failed and by how much.
- Infer simulated duration from the requested frequency band. ASIR 0.1 has not yet frozen observable-based termination, so such an inference would encode an undocumented solver policy.

## Consequences

- Exact cells, step size, step count, FP64 field bytes, checkpoint bytes, and retained storage can be reviewed before allocation.
- Calibration tests compare predicted values with live reference-solver field storage, checkpoints, and wall time on the executing host.
- A uniformly halved cell size over the same domain visibly creates eight times the cells, about twice the steps, and about sixteen times the logical update work.
- Runtime claims are valid only for the named hardware, solver build, thread count, and uncertainty carried by the calibration.
- The current model excludes CPML auxiliaries, heterogeneous/dispersive coefficient arrays, transform monitors, GPU allocation, MPI halos, and durable normalized result formats. Each requires a separately versioned execution profile rather than an untracked multiplier.
