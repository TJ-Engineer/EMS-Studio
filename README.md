# Open Engineering Platform

This repository is the clean-sheet implementation workspace for the coordinated CAD Studio and EM Studio applications described in the architecture plan.

## Development progress

The [development storyboard](docs/DEVELOPMENT_STORYBOARD.md) records the completed platform and first end-to-end CAD reels, validated ASIR boundary, Maxwell harness, FP64 reference Yee solver, RMS multiport contract, traceable CAD-to-grid spike, measured resource-cost estimator, versioned simulation-worker lifecycle, and first interactive CAD/EM workflow wireframe.

The [dependency and license matrix](docs/product/DEPENDENCY_LICENSE_MATRIX.md) and [compute support policy](docs/product/COMPUTE_SUPPORT_POLICY.md) define which planned libraries and CPU/GPU/MPI profiles may enter the V1 baseline and the evidence required before they are supported or distributed.

## UI wireframe

The dependency-free [interactive wireframe](apps/ui_wireframe/README.md) makes the shared CAD Studio and EM Studio workflow available while the native Qt 6/QML shell is being prepared:

```sh
python3 -m http.server 8080 --directory apps/ui_wireframe
```

Open <http://localhost:8080> to switch studios, inspect model and study entities, review the feature history and EM preflight, toggle engineering overlays, and exercise the simulated solve flow. The wireframe contains representative presentation state only; compiled engine modules remain authoritative.

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

- `apps/`: thin desktop application entry points and the interactive UI workflow wireframe.
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
