# Reference FDTD engine

The `oep::fdtd_engine` target is the deterministic FP64 correctness path for the first electromagnetic implementation. It provides a small conventional Yee solver whose results are judged by the independent golden Maxwell harness.

## Numerical contract

- `YeeGrid1d` stores electric samples on nodes and magnetic samples at cell centers. It supports paired periodic boundaries, PEC ends, and first-order Mur absorbing ends.
- `YeeGrid3d` uses six structure-of-arrays field buffers at the standard staggered Yee locations. Every face is PEC in this reference slice.
- Time steps are calculated from the lossless medium and the one- or three-dimensional Courant limit. User factors must be finite and in `(0, 1]`.
- Sources are explicit soft or hard electric-field samples. Invalid, nonfinite, out-of-grid, and tangential PEC sources are rejected before a step mutates state.
- Probes sample explicit staggered field locations. The caller can also inspect complete read-only field spans.
- The discrete energy reported after a step is the leapfrog invariant pairing the previous and current electric fields with the half-step magnetic field.
- Dispersion helpers expose the numerical plane-wave phase velocity, PEC-cavity eigenfrequency, rectangular-waveguide cutoff, and guided propagation constant of the same stencil.

## Restart contract

Both grids emit versioned binary checkpoints containing the complete FP64 field state, grid/material policy, step index, and most recent discrete energy. Decoding validates dimensionality, configuration bounds, exact field extents, finite values, trailing data, version, and an FNV-1a payload checksum before returning usable state. Continued updates from restored canonical cases are bit-for-bit identical to uninterrupted updates.

The checkpoint is an internal reference-solver format. A future durable simulation artifact will wrap it with job, ASIR, mesh, solver-build, and execution provenance.

## Deliberate limits

This slice supports uniform Cartesian cells and lossless homogeneous isotropic media. The three-dimensional path currently provides PEC faces; the normal-incidence open-boundary check uses the one-dimensional Mur implementation. CPML, heterogeneous material coefficients, conductivity/dispersion, CAD-to-cell mapping, calibrated ports, Fourier monitors, near-to-far transforms, cancellation, parallel kernels, GPU execution, and MPI decomposition remain later panels. These capabilities must retain the FP64 solver and golden cases as the correctness baseline.
