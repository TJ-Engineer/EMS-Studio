# Open Engineering Platform

This repository is the clean-sheet implementation workspace for the coordinated CAD Studio and EM Studio applications described in the architecture plan.

## Development progress

The [development storyboard](docs/DEVELOPMENT_STORYBOARD.md) records the completed platform and first end-to-end CAD reels, validated ASIR boundary, Maxwell harness, FP64 reference Yee solver, RMS multiport contract, and traceable CAD-to-grid spike, along with the next cost-model frame on the path to the first EM milestone.

## Build

The bootstrap build requires CMake 3.28+, Ninja, and a C++20 compiler.

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

To build the optional OCCT exact B-rep adapter on Ubuntu 24.04 without changing the system installation:

```sh
tools/bootstrap_occt_ubuntu_noble.sh
cmake --preset occt-local
cmake --build --preset occt-local
ctest --preset occt-local
```

The bootstrap downloads the matching Ubuntu OCCT and TBB packages into the ignored `build/` tree and verifies the OCCT source archive before recovering a header omitted by the binary development package.

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

## License

Project-owned software and documentation are available under the [MIT License](LICENSE). Third-party dependencies and imported artifacts retain their own licenses and notices.
