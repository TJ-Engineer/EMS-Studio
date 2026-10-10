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

The future reference solver will report measured observations to `assess`. The harness remains in the golden test tree and does not link to solver internals.
