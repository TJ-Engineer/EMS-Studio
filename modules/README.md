# Module boundaries

Each directory below is an independently owned boundary. Public headers belong in the module's `include/` directory and private implementation belongs in `src/`. Cross-module use goes through the `oep::<module>` CMake target; source files must not reach into another module's `src/` directory.

## Platform

| Module | Responsibility |
|---|---|
| `app_shell` | Window, workspace, and application command coordination |
| `interaction` | Tool state machines, snapping requests, and selection intent |
| `document` | Stable IDs, semantic revisions, evaluation state, feature DAG, and transactions |
| `ipc` | Versioned local process contracts and transport adapters |
| `persistence` | Native package, journal, checkpoints, and migrations |
| `automation` | Stable automation object model and Python-host boundary |
| `sync` | Immutable versions, blobs, metadata, and conflict states |

## CAD

| Module | Responsibility |
|---|---|
| `sketch` | Sketch entities, constraints, and diagnostics |
| `modeler` | Feature definitions, regeneration, and kernel adapter |
| `topology_id` | Persistent topology provenance and reconciliation |
| `assembly` | Components, occurrences, joints, and transforms |
| `tessellation` | Display meshes, levels of detail, and selection mapping |
| `viewport` | GPU scene resources, camera, rendering, and hit testing |
| `exchange` | Import/export adapters, healing, and conversion reports |

## Electromagnetics

| Module | Responsibility |
|---|---|
| `simulation_ir` | ASIR schema model, validation, units, and study fingerprints |
| `em_geometry` | Computational regions, role transfer, healing, and defeaturing |
| `em_mesh` | Structured grids and mesh quality metadata |
| `em_solver_api` | Solver lifecycle, capabilities, resource estimates, and result contract |
| `fdtd_engine` | FDTD time stepping, boundaries, sources, monitors, and decomposition |
| `fem_engine` | Deferred frequency-domain Maxwell engine boundary |
| `simulation_cost` | Fidelity profiles, estimates, budgets, and calibration |
| `simulation_cache` | Content-addressed grids, calibrations, checkpoints, and bases |
| `em_optimizer` | Isolated candidates, search, sensitivities, and guarded promotion |
| `basis_library` | Versioned multiport and embedded-pattern response bases |
| `simulation_orchestrator` | Jobs, workers, scheduling, cancellation, and artifacts |
| `simulation_results` | Immutable normalized results and provenance |
