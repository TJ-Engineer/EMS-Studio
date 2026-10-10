# Maxwell verification harness

This directory contains the solver-independent oracle for the reference FDTD path. The harness fixes the analytic values, metric definitions, and acceptance thresholds before solver output is available, so a solver cannot define its own test.

## Analytic references

The compiled reference functions cover three lossless cases:

- `plane-wave-1d-vacuum` uses \(v=1/\sqrt{\mu\epsilon}\), \(\lambda=v/f\), \(\eta=\sqrt{\mu/\epsilon}\), and \(k=2\pi/\lambda\).
- `pec-cavity-tm111` uses \(f_{mnp}=v/2\sqrt{(m/a)^2+(n/b)^2+(p/d)^2}\). TM modes require nonzero transverse indices; TE modes require a nonzero longitudinal index and at least one nonzero transverse index.
- `wr90-waveguide-te10` uses the rectangular-waveguide cutoff, propagation constant, guide wavelength, phase/group velocity, and TE/TM impedance equations. Requests at or below cutoff return a typed diagnostic rather than a propagating value.

All inputs are SI values. Each canonical case retains both its physical problem definition and its calculated reference, so a solver can execute the case without duplicating hidden constants. The references reject nonfinite values, nonpositive dimensions or frequencies, invalid lossless material constants, and unsupported mode indices.

## Metrics and thresholds

The harness defines scalar relative error, relative field L2 error, absolute L-infinity error, observed convergence order, and normalized energy residual. Every canonical case requires one observation for every declared metric; missing, duplicate, nonfinite, negative error, and out-of-tolerance observations fail with typed assessment issues.

| Case | Primary accuracy limits | Shared evidence |
|---|---|---|
| 1-D plane wave | phase velocity <= 1%; field L2 <= 2%; open-boundary reflection <= 0.1% | energy residual <= 1%; observed order >= 1.8; restart L-infinity <= 1e-12 |
| PEC TM111 cavity | resonance <= 1%; field L2 <= 3% | energy residual <= 0.5%; observed order >= 1.8; restart L-infinity <= 1e-12 |
| WR90 TE10 waveguide | cutoff <= 0.5%; propagation constant <= 1%; field L2 <= 3% | energy residual <= 1%; observed order >= 1.8; restart L-infinity <= 1e-12 |

The accuracy limits allow coarse verification grids to expose the expected second-order trend without hiding dispersion, boundary, or field-shape defects. The restart limit is intentionally much tighter: an FP64 checkpoint taken on the same implementation path must reproduce the uninterrupted state to roundoff rather than merely converge to the same physical answer.

The harness remains in the golden test tree and does not link to solver internals. The acceptance executable links both sides and reports measured observations to `assess`.

## Reference solver evidence

The [FP64 Yee acceptance test](reference_fdtd_maxwell_test.cpp) drives the compiled [reference engine](../../../modules/em/fdtd_engine/README.md). Field errors, energy residuals, open-boundary reflection, and restart differences come from time-stepped field states. Frequency and propagation values use the numerical dispersion relation of the same Yee stencil at two grid refinements; the cavity test also verifies its measured zero-crossing frequency against that prediction.

| Case | Accepted fine-grid observations |
|---|---|
| 1-D plane wave | phase velocity error `5.46e-4`; field L2 `3.47e-3`; Mur reflection `1.27e-5`; energy residual `1.54e-16`; order `2.024`; restart L-infinity `0` |
| PEC TM111 cavity | frequency error `2.69e-3`; field L2 `3.28e-5`; energy residual `3.14e-15`; order `2.047`; restart L-infinity `0` |
| WR90 TE10 waveguide | cutoff error `2.47e-3`; propagation error `1.60e-3`; field L2 `5.07e-15`; energy residual `4.10e-15`; order `2.002`; restart L-infinity `0` |

These values are deterministic properties of the declared grids and build, not replacement tolerances. The limits earlier in this document remain the acceptance authority.

## Reference cost calibration

The [cost calibration acceptance test](simulation_cost_calibration_test.cpp) measures two single-threaded three-dimensional reference-FDTD workloads on the executing host, constructs a named hardware/build calibration, and predicts a third holdout workload. The holdout must remain within an explicit `0.35×` to `2.85×` predicted/actual regression band. This intentionally broad cross-host limit detects order-of-magnitude model failures without treating shared CI timing as a stable benchmark.

The same holdout proves that the deterministic forecast matches the reference grid's allocated staggered FP64 field bytes and serialized version-1 checkpoint bytes exactly. Unit evidence separately fixes hard-budget behavior and the cell, memory, step-count, and runtime pressure created when a small feature halves uniform grid spacing.

## Port normalization contract

The independent [port contract API](include/oep/test/port_contract_harness.hpp) freezes `oep-rms-power-wave-1.0`. It uses RMS phasors with `Re{sqrt(2) x exp(+j omega t)}`, voltage in the declared modal direction, and current directed into the computational domain. For finite positive-real impedance `Z0`,

\[
a=\frac{V+Z_0I}{2\sqrt{Z_0}},\qquad
b=\frac{V-Z_0I}{2\sqrt{Z_0}}.
\]

`a` and `b` have units of `sqrt(W)`, and `|a|² - |b|²` equals RMS terminal power. The harness rejects cutoff or evanescent modes, non-positive impedance, non-unit orientation, weak phase anchors, and near-degenerate modes without an explicitly tracked orthogonal basis.

Each basis run drives exactly one port with `1 sqrt(W)` and terminates every inactive port with zero incident wave. The [compiled fixture](port_contract_harness.cpp) assembles those runs into a four-port scattering matrix and a normalized far-field basis. Two stored complex simultaneous drives reproduce their independently recorded outgoing waves and three far-field samples within `1e-12`.

The canonical reciprocal fixture has maximum power gain `0.1726874826`, safely below the passive limit of one. Its reciprocity error is zero. A positive reference-plane distance moves the plane inward and applies

\[
S'_{pq}=S_{pq}\exp\{+j(\beta_p d_p+\beta_q d_q)\}.
\]

The [contract test](port_contract_harness_test.cpp) verifies the shift element by element, reverses it, and confirms that lossless phase shifts preserve passivity and reciprocity. It also fixes metric denominators: radiated power for directivity, accepted power for gain, and total incident power for realized gain. [ADR 0005](../../../docs/architecture/decisions/0005-versioned-rms-port-contract.md) records the complete convention and deferrals.
