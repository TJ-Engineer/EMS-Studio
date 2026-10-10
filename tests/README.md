# Test suites

- `unit/`: mathematical and module-level contracts.
- `integration/`: process, persistence, and end-to-end workflows.
- `golden/`: versioned CAD and EM reference cases with declared tolerances.
- `conformance/`: schema, CPU/GPU/MPI, response-basis, and restart equivalence.
- `fuzz/`: package, translator, IPC, and public API fuzz targets.
- `performance/`: named-hardware budgets and regression scenarios.

The bootstrap smoke tests in `CMakeLists.txt` only verify that each process target links and starts. Reel 1 platform behavior and ASIR validation are covered under `unit/`; the CAD golden suites run the kernel contract, topology mutations, and the complete restart-safe Reel 2 modeling thread against deterministic and OCCT adapters. The [EM golden harness](golden/em/README.md) fixes analytic plane-wave, PEC-cavity, and rectangular-waveguide references plus the convergence, energy, boundary-reflection, and restart criteria; the compiled FP64 Yee solver is exercised by focused unit and golden acceptance suites against that independent contract. The same golden tree freezes the versioned RMS multiport normalization, modal phase-anchor, response reconstruction, passivity, reciprocity, metric-denominator, and reference-plane conventions. The CAD-to-grid unit suite verifies persistent role/material/port identity transfer, exact subcell occupancy, deterministic ordering, hard grid limits, conflict rejection, and explicit staircasing diagnostics. The IPC and worker lifecycle unit suites verify framing bounds, resource approval, state transitions, cancellation, checkpoint identity, and raw-artifact gating. The process integration suite starts the real simulation worker twice to prove correlated commands, terminal cancellation, exact checkpoint resume after process exit, and final artifact provenance.
