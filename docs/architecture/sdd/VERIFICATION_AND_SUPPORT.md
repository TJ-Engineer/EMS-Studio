# SDD Verification and Support Guide

**Baseline:** October 12, 2026 · revision 4

## Evidence interpretation

Tests establish the contracts listed below. They do not certify a complete product or every deployment. All 29 `qt-cad`, 26 `qt-shell`, 27 `occt-local`, and 24 `dev` CTest entries pass. Acceptance includes deterministic camera/clipping/picking, live OCCT scene generation, the measured one-million-source-triangle LOD corpus, and the desktop reference-worker lifecycle. Link and whitespace checks also pass.

## Acceptance traceability

| Design contract | Primary evidence |
|---|---|
| Revision authority, rejection/commit, failed geometry, undo/redo | [Document tests](../../../tests/unit/document_state_machine_test.cpp) |
| DAG dirtiness, independent branches, cycles, blocked descendants | [Feature graph tests](../../../tests/unit/feature_dependency_graph_test.cpp) |
| Recovery bounds, corruption, atomic path behavior | [Persistence tests](../../../tests/unit/persistence_checkpoint_test.cpp) |
| Exact kernel contracts across fake/OCCT | [Fake corpus test](../../../tests/golden/cad/kernel_adapter_contract_test.cpp), [OCCT corpus test](../../../tests/golden/cad/occt_kernel_adapter_contract_test.cpp) |
| Persistent topology and adversarial mutations | [Identity tests](../../../tests/unit/topology_identity_test.cpp), [Mutation corpus](../../../tests/golden/cad/topology_mutation_corpus.cpp) |
| Native camera, clipping, transforms/materials/visibility, shared LODs, one-million-source-triangle projection, and persistent-ID picking | [Viewport projection tests](../../../tests/unit/viewport_projection_test.cpp) |
| Sketch classification and stable identity | [Sketch tests](../../../tests/unit/sketch_solver_test.cpp) |
| Modeling thread, hydration, failed-head restart and repair | [Modeling corpus](../../../tests/golden/cad/modeling_thread_corpus.cpp) |
| ASIR authority, units, canonical content, guarded draft edits, project-v1 compatibility, project-v2 worker bytes, and decoding rejection | [ASIR tests](../../../tests/unit/simulation_ir_test.cpp) |
| Traceable geometry/grid conservation and rejection | [CAD-to-grid tests](../../../tests/unit/cad_to_grid_test.cpp) |
| Yee updates/checkpoints and independent Maxwell tolerances | [Reference unit tests](../../../tests/unit/reference_fdtd_test.cpp), [Maxwell acceptance](../../../tests/golden/em/reference_fdtd_maxwell_test.cpp) |
| RMS port normalization/phase/reference-plane contract | [Port acceptance](../../../tests/golden/em/port_contract_harness_test.cpp) |
| Checked estimates, independent budgets, measured holdout | [Cost tests](../../../tests/unit/simulation_cost_test.cpp), [Calibration acceptance](../../../tests/golden/em/simulation_cost_calibration_test.cpp) |
| Worker lifecycle, cancellation, checkpoint authority, durable nested record, checksum, and resume-only policy | [Worker tests](../../../tests/unit/simulation_worker_lifecycle_test.cpp) |
| Bounded generic frame decoding | [IPC tests](../../../tests/unit/ipc_frame_test.cpp) |
| Fresh-process worker resume and artifact provenance | [Process integration](../../../tests/integration/simulation_worker_process_test.cpp) |
| Native layout/controller/authoring state, fresh-session CAD+study+worker recovery, corrupt/mismatched load atomicity, exact-body scene lifecycle, selection, and desktop worker lifecycle | [Shell tests](../../../tests/unit/app_shell_state_test.cpp) |
| Real QML actions, navigation, accessibility, native scene/fit controls, Worker estimate/launch/checkpoint/restart/collect, and actual File Save/Open worker recovery | [Offscreen host acceptance](../../../modules/platform/app_shell/src/application_qt.cpp), `lint.qml_app_shell` |

## Build and runtime profiles

| Preset | Capability | Baseline test inventory |
|---|---|---|
| `dev` | Headless development without optional Qt/OCCT | 24 |
| `occt-local` | Exact CAD foundation | 27 |
| `qt-shell` | Qt layout/navigation with OCCT create/load disabled | 26 |
| `qt-cad` | Qt plus OCCT document, native viewport, and study setup | 29 |

Inventory counts can change; they are not coverage percentages. See [presets](../../../CMakePresets.json) for current SDK paths and test environments. The launcher defaults to `qt-cad`:

```sh
cmake --preset qt-cad
cmake --build --preset qt-cad
ctest --preset qt-cad
tools/run_qt_shell.sh cad
```

Local SDK bootstrap requires the repository's documented host prerequisites. Offscreen tests use software Qt rendering and isolated settings where applicable. Performance qualification requires named native hardware and calibration; WSL2 functional evidence is not a portable performance claim.

## Troubleshooting

| Observation | First inspection | Resolution boundary |
|---|---|---|
| No document at startup | File actions/build preset | Create/open the example with OCCT; expected empty state |
| Qt viewport is empty | Active preset, current geometry, viewport diagnostic | Use `qt-cad`; repair/Undo failed CAD; `qt-shell` has no OCCT scene |
| Viewport interaction is slow | Source/display triangles, instance count, LOD selection, and scene build time | Capture the model and hardware; the one-million-source core-projection corpus does not qualify every Canvas workload |
| Parameter Apply rejected | Expected revision and command diagnostic | Refresh stale editor, correct invalid value, submit deliberately |
| Edit committed but geometry missing | Failed feature and blocked descendants | Repair parameters or Undo; do not consume last-good as current |
| Checkpoint open fails | Format limits, restore error, hydration diagnostic | Preserve active session; inspect compatibility/corruption |
| EM draft becomes stale | Document ID, revisions, geometry hash | Undo to matching authority or explicitly rebuild/reset |
| Valid definition still has preflight errors | Full ASIR report | Complete independent missing setup/provenance; no solve implied |
| Material/port cannot be removed | Role/boundary/monitor reference IDs | Reassign or remove dependents first |
| Geometry prep rejects air face | Background target primitive/kind | Preparation needs a volume; UI face roles are not general volume extraction |
| Grid fails or warns | Domain bounds, overlap, budget, width-in-cells | Correct model/policy; retain staircasing diagnostics |
| Worker refuses execution | Estimate/build/approval diagnostics | Correct prepared-job authority; do not bypass resource gates |
| Worker controls reset | CAD revision, study fingerprint, recent edit/load/Undo | Re-estimate; authority changes deliberately invalidate session jobs |
| Saved worker does not resume | Current CAD authority, nested checksum, ASIR fingerprint, solver build, checkpoint diagnostic | Open rejects atomically; use the matching project/checkpoint or prepare a new job |
| Resume refused | Wrapper identity, decoded grid, build, steps | Restore matching checkpoint/job or report incompatibility |
| Artifact collection denied | Job terminal phase | Only succeeded jobs return raw artifacts |

Support records should capture build/preset, relevant document revision/hash or study fingerprint, diagnostic paths/codes, and the command that failed. Avoid substituting screenshots for authority data. Local file paths, model content, and material provenance may contain project information; collect only what the investigation needs.

## Change-impact checklist

| Change | Coordinated updates |
|---|---|
| Document state/transaction semantics | Platform SDD, history/graph tests, checkpoint version/migration if encoding changes |
| CAD feature/kernel/tolerance | CAD SDD, both adapter corpora, topology/hydration expectations |
| ASIR field/canonical encoding | EM SDD, schema, fingerprint/validation tests, downstream identity consumers |
| Grid material mapping | EM SDD, conservation/risk tests, cost profile and future solver adapter |
| Numerical stencil/boundary/source | EM SDD, analytic/convergence/energy/restart tests, calibration/build identity |
| Worker payload/lifecycle | EM/platform SDDs, protocol version, corruption/lifecycle/process tests |
| Shell commands/projections | Desktop, viewport, and worker-experience SDDs; typed lint metadata; controller and offscreen actions |
| Optional dependency/build profile | System SDD, adoption/license records, presets and smoke checks |

## Outstanding verification

Manual native visual review, assistive-technology review, GPU/ten-million-triangle viewport qualification, end-to-end heterogeneous authored-study solve, external worker supervision, general B-rep mapping, calibrated ports, 3-D CPML, normalized result storage, and production parallel execution need their own acceptance evidence. Do not infer these from the existing foundation checks.
