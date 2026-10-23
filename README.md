# Open Engineering Platform

This repository is the clean-sheet implementation workspace for the coordinated CAD Studio and EM Studio applications described in the architecture plan.

## Development progress

The [development storyboard](docs/DEVELOPMENT_STORYBOARD.md) records the completed platform and first end-to-end CAD reels, validated ASIR boundary, Maxwell harness, FP64 reference Yee solver, RMS multiport contract, traceable CAD-to-grid spike, measured resource-cost estimator, versioned simulation-worker lifecycle, interactive workflow wireframe, native Qt application shell, completed native viewport and Worker Experience panels, and active heterogeneous CAD-to-worker preparation.

The [dependency and license matrix](docs/product/DEPENDENCY_LICENSE_MATRIX.md) and [compute support policy](docs/product/COMPUTE_SUPPORT_POLICY.md) define which planned libraries and CPU/GPU/MPI profiles may enter the V1 baseline and the evidence required before they are supported or distributed.

## Desktop UI

The dependency-free [interactive wireframe](apps/ui_wireframe/README.md) remains available for quick browser review:

```sh
python3 -m http.server 8080 --directory apps/ui_wireframe
```

Open <http://localhost:8080> to switch studios, inspect model and study entities, review the feature history and EM preflight, toggle engineering overlays, and exercise the simulated solve flow. The wireframe contains representative presentation state only; compiled engine modules remain authoritative.

The compiled [Qt application shell](modules/platform/app_shell/README.md) now carries the shared CAD/EM chrome, resizable panels, persisted layout, menus, command palette, keyboard navigation, and accessibility metadata. It remains presentation-only until the next engine-binding slice.

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

To build and launch the optional native shell on Ubuntu 24.04 without changing the system installation:

```sh
tools/bootstrap_qt_ubuntu_noble.sh
tools/bootstrap_occt_ubuntu_noble.sh
cmake --preset qt-cad
cmake --build --preset qt-cad
ctest --preset qt-cad
tools/run_qt_shell.sh cad
```

Use `tools/run_qt_shell.sh em` to start in EM Studio. The bootstrap verifies the complete locked Ubuntu package set before extracting Qt 6.4.2 under the ignored `build/` tree.

Choose **File → New CAD example → Create**, select `base-sketch`, edit `width` or `height`, and click **Apply**. Try a negative width to inspect failed regeneration, then **Edit → Undo** to recover. **File → Save CAD checkpoint** and **Open CAD checkpoint** accept a local path and preserve the timeline. The native viewport renders the current exact OCCT body with orbit, pan, zoom, Fit, section clipping, and persistent face picking; failed regeneration clears it honestly. Switch to **EM Studio → Prepare EM draft** to inspect its ASIR snapshot, edit the frequency band in Hz, and read preflight diagnostics. Return to CAD and change a dimension to see the draft become stale; Undo restores its original authority. In the EM inspector, use resolved face IDs to apply air/vacuum, keep-out, conductor, dielectric, or ground roles and discrete ports, with editable inward orientation and impedance. Reusing an ID replaces a definition; Remove deletes it. The material editor supports nondispersive property values, explicit validity bands and provenance, and loading existing values for editing. Referenced materials cannot be removed until their roles/boundaries are reassigned. The boundary editor supports open-PML/PEC policies with background material and padding in metres. Monitor requests support S parameters and impedance with explicit ports and frequency bands in Hz; referenced ports are protected from deletion. **File → Save/Open CAD + EM project** preserves CAD history and all authored EM setup atomically, including stale or incomplete drafts. Project-checkpoint v2 also restores an available verified worker checkpoint at exact progress in a fresh desktop session; CAD-only checkpoints still omit EM and worker state. The EM **Worker** tab estimates and runs a bounded homogeneous reference preview with authoritative progress, cancellation, checkpoint/restart, and raw artifact collection. Heterogeneous CAD-to-grid execution, calibrated ports, and 3-D CPML remain unfinished. The lighter `qt-shell` preset keeps shell-only support; launch it with `OEP_SHELL_PRESET=qt-shell tools/run_qt_shell.sh cad`.

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
