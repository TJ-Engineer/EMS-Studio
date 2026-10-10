# Test suites

- `unit/`: mathematical and module-level contracts.
- `integration/`: process, persistence, and end-to-end workflows.
- `golden/`: versioned CAD and EM reference cases with declared tolerances.
- `conformance/`: schema, CPU/GPU/MPI, response-basis, and restart equivalence.
- `fuzz/`: package, translator, IPC, and public API fuzz targets.
- `performance/`: named-hardware budgets and regression scenarios.

The bootstrap smoke tests in `CMakeLists.txt` only verify that each process target links and starts. Reel 1 platform behavior and ASIR validation are covered under `unit/`; the CAD golden suites run the kernel contract, topology mutations, and the complete restart-safe Reel 2 modeling thread against deterministic and OCCT adapters. The [EM golden harness](golden/em/README.md) fixes analytic plane-wave, PEC-cavity, and rectangular-waveguide references plus the convergence, energy, boundary-reflection, and restart criteria for the reference FDTD engine.
