# ADR 0004: FP64 Yee grid as the electromagnetic reference path

**Status:** Accepted
**Date:** October 10, 2026

## Context

The production FDTD architecture will eventually need heterogeneous materials, CPML, CPU/GPU portability, MPI decomposition, calibrated ports, Fourier monitors, and interruption recovery. Implementing those concerns together would make early Maxwell failures difficult to localize. The existing golden harness already fixes analytic values and case-specific tolerances for a plane wave, a PEC cavity, and a rectangular waveguide.

## Decision

Maintain a deterministic double-precision CPU Yee implementation as the numerical reference path. One-dimensional and three-dimensional grids expose their staggered fields directly, calculate time steps from the applicable Courant limit, reject invalid sources before mutation, and provide explicit probe sampling.

The one-dimensional grid supports paired periodic, PEC, and first-order Mur boundaries. The three-dimensional verification grid uses PEC faces. Numerical-dispersion functions are derived from the same discrete update stencil and are checked alongside time-stepped field shape, energy, and restart behavior.

Restart state is a versioned checksummed binary record containing the complete FP64 fields and numerical configuration. Canonical runs restored at an intermediate step must continue bit-for-bit identically.

The independent Maxwell harness remains in the golden test tree and links to the solver only in the acceptance executable. Solver implementation code does not define or relax its tolerances.

## Alternatives

- Begin with the portable production kernel layer. Backend abstractions, device storage, and asynchronous execution would obscure the first correctness failures.
- Validate only closed-form dispersion equations. That would not exercise field storage, curl updates, boundaries, sources, probes, energy, or restart.
- Adopt an external FDTD engine as the reference. That would not establish an independently controlled native update and checkpoint contract.
- Implement CPML before a normal-incidence open-boundary baseline. The larger auxiliary-state system would add risk before the core update is verified.

## Consequences

- The reference implementation favors clarity and deterministic evidence over performance.
- Production CPU/GPU/MPI paths must remain comparable with this FP64 path.
- CPML and heterogeneous materials can be added as independently tested modules after the core stencil.
- The current open-boundary evidence is one-dimensional normal incidence; three-dimensional CPML remains unfinished.
- The canonical plane-wave, TM111 cavity, and WR90 TE10 cases now satisfy their declared accuracy, convergence, conservation, reflection, and restart requirements.
