# Test suites

- `unit/`: mathematical and module-level contracts.
- `integration/`: process, persistence, and end-to-end workflows.
- `golden/`: versioned CAD and EM reference cases with declared tolerances.
- `conformance/`: schema, CPU/GPU/MPI, response-basis, and restart equivalence.
- `fuzz/`: package, translator, IPC, and public API fuzz targets.
- `performance/`: named-hardware budgets and regression scenarios.

The bootstrap smoke tests in `CMakeLists.txt` only verify that each process target links and starts. Functional tests should be added with the first implementation slice.
