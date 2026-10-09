# Open Engineering Platform

This repository is the clean-sheet implementation workspace for the coordinated CAD Studio and EM Studio applications described in the architecture plan.

## Build

The bootstrap build requires CMake 3.28+, Ninja, and a C++20 compiler.

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Build products stay under `build/` and are not source-controlled.

## Source layout

- `apps/`: thin desktop application entry points.
- `workers/`: isolated model-engine and simulation-worker processes.
- `modules/platform/`: application-neutral document, command, IPC, persistence, automation, and sync infrastructure.
- `modules/cad/`: sketching, feature modeling, topology identity, assembly, tessellation, viewport, and exchange modules.
- `modules/em/`: study IR, computational geometry, meshing, solvers, optimization, orchestration, caching, bases, and results.
- `schemas/`: versioned wire and persistence contracts; generated code belongs in the build tree.
- `tests/`: unit, integration, golden-model, conformance, fuzz, and performance suites.
- `tools/`: corpus, schema-migration, and benchmark utilities.
- `packaging/`: platform-specific installers and release metadata.
- `docs/`: architecture, decision records, and implementation plans.

Module directories intentionally begin with only boundary documentation. A module receives a compiled target when its first implementation slice is added.
