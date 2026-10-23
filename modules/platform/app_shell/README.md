# Native application shell

<!--
SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
SPDX-License-Identifier: MIT
-->

`oep::app_shell` is the shared desktop host for CAD Studio and EM Studio. The application entry points provide only an initial workspace, product name, and version. With `OEP_ENABLE_QT_SHELL=ON`, the host loads the Qt Quick shell; the default build keeps a small headless entry point so engine development and CI do not require a window system.

The shell owns application chrome, workspace switching, focus behavior, accessibility metadata, visual tokens, panel layout persistence, and native viewport presentation. Document mutation, CAD evaluation, tessellation, ASIR validation, meshing, and simulation remain in their compiled modules and worker boundaries.

The Qt allowlist is exact Qt 6.4.2 Core, Gui, Qml, Quick, and Quick Controls 2. The QML runtime uses only Qml, Models, WorkerScript, Quick, Window, Controls, Templates, and Layouts. `Theme.js` centralizes the visual tokens, and `ShellController` stores panel geometry and visibility under the `shell/layout-v1` settings group.

On Ubuntu 24.04:

```sh
tools/bootstrap_qt_ubuntu_noble.sh
tools/bootstrap_occt_ubuntu_noble.sh
cmake --preset qt-cad
cmake --build --preset qt-cad
ctest --preset qt-cad
tools/run_qt_shell.sh cad
tools/run_qt_shell.sh em
```

The two smoke tests use Qt's offscreen platform. They load the complete QML resource and assert the named navigation and accessibility contract before exiting. `lint.qml_app_shell` resolves the real QML modules and the typed `ShellController` metadata. `unit.app_shell_state` verifies workspace selection, bounded panel sizes, persisted visibility, and reset behavior in an isolated settings file.

The shell controller can attach a host-owned `document::Document`. The host must
detach before destroying it and call `refreshDocument()` after external changes.
QML reads revision/geometry status, feature states, and diagnostics; parameter
edits carry a decimal expected revision and go through `Document::apply`. Undo
and redo use the document timeline. The default host has no document attached.
With the `qt-cad` preset, File provides **New CAD example**, **Open CAD
checkpoint**, and **Save CAD checkpoint**. The owned session uses OCCT and the
Reel 2 CAD evaluator. Loading validates the document and explicitly hydrates
kernel state, including failed revisions, before replacing the active session.
An unreadable, corrupt, or incompatible checkpoint leaves the active document
intact. Saves use the existing atomic replacement implementation. The UI states
that opening/creating replaces the current document and saving replaces the
chosen file; it does not autosave.

An evaluated `qt-cad` document publishes an immutable scene assembly derived
from the final OCCT body. Instances support shared mesh LODs, affine transforms,
materials, visibility, and occurrence-qualified selection. The center viewport draws shaded faces and edge curves;
left drag orbits, middle/right drag pans, the wheel zooms, **Fit** reframes, and
**Clip** moves a model-Z section plane. Hover and click highlights use the same
persistent face IDs exposed to EM setup. Failed current regeneration clears the
scene instead of presenting last-good geometry as current.

The renderer-neutral projection corpus represents 1,048,576 source triangles
through 256 shared-mesh instances and budget-selected display LODs. Its
120-frame strict-debug gate is 16.667 ms at 1280×720; final verification averaged
approximately 0.82 ms. This qualifies the core CPU projection boundary, not GPU buffers,
ten-million-triangle models, or every Qt Canvas workload.

Select a feature in the CAD browser to edit its declared parameters. Apply uses
the editor's revision, engine diagnostics explain failed regeneration, and
Edit → Undo/Redo follows the persisted timeline. Example dimensions use model
units (mm). Checkpoints use the internal recovery format, not the planned public
native package. The path popup accepts a local filesystem path.

The `qt-shell` preset still builds without OCCT and disables creation/loading;
use `OEP_SHELL_PRESET=qt-shell tools/run_qt_shell.sh cad` for that build. EM Studio now offers **Prepare EM draft** after creating/opening an evaluated
CAD example. It snapshots the exact geometry authority and CAD dimensions in SI,
displays ASIR counts and validator issues, and edits a study-local frequency
band in Hz. The fingerprint guards frequency edits against stale study commands.
Changing CAD preserves the old draft and reports stale authority; failed CAD
blocks preparation, and Undo can restore the draft's CAD revision. **Rebuild EM
draft** explicitly replaces its settings with defaults. Use **Save/Open CAD + EM project** to retain drafts and CAD history together.
New/CAD-only document replacement clears the active draft; CAD-only checkpoints
do not store EM settings. The inspector lists final-body resolved faces with persistent feature/local IDs
and available semantic labels. Assign **Air / vacuum**, **Keep out**, **Conductor**, **Dielectric**, or **Ground** roles, choosing a catalog material for each material-bearing role, and author a discrete port by choosing its face, reference
plane face, inward orientation and reference impedance in ohms. Port defaults
are matched termination, mode 0, 1 V amplitude, 0 rad phase and zero de-embedding.
Use the same definition ID to replace it or **Remove role/port** to delete it.
These edits use compiled commands and fail atomically on invalid definitions,
stale fingerprints, unresolved topology or stale/failed CAD. Missing setup
remains visible in preflight. Structural validation does not calibrate the port
or enable a solve.

The **Nondispersive material** editor creates or replaces a material by ID.
Select an existing catalog entry to load its stored values before editing.
Relative permittivity/permeability must be positive; conductivity (S/m) and
loss tangent must be nonnegative. Enter a validity band in Hz covering the
study's frequency band and a source/assumption in the provenance field.
**Apply material** validates the typed definition before mutation. **Remove
material** rejects a material referenced by a role or boundary. Reassign or
remove those references first. Properties are user-supplied; the UI does not
infer a physical material from a CAD face or offer a dispersive model. If a
frequency-band change makes several materials invalid, repair their bands one
at a time; preflight retains each outstanding validity issue.

The **Boundary policy** editor supports open PML and PEC with an explicit
background material and padding in metres. An open antenna study still requires
at least one PML policy; adding only PEC keeps that diagnostic visible. Padding
must be nonnegative and the background must exist in the material catalog.

The **Requested observable** editor supports S-parameter and impedance monitors.
Choose an authored port and a frequency band in Hz contained in the study band.
Reuse an ID to replace a boundary/monitor or remove it explicitly. Removing the
last PML policy or observable returns the missing-setup diagnostic. A monitor
protects its referenced port from deletion; reassign/remove the monitor first.
When the study band narrows, monitor bands can be repaired individually while
preflight retains the outstanding issues.

These controls store ASIR setup. 3-D CPML, calibrated solver ports, periodic
and symmetry boundary editors, and field/far-field monitor editors remain
unfinished.

The EM output dock includes a **Worker** tab. Once structural setup is complete,
**Estimate** binds the reference solver build and execution provenance, runs the
real cost estimator, and prepares a bounded `12 × 10 × 8` homogeneous preview
job. **Launch** reports worker-authoritative progress. **Cancel** reaches an
explicit terminal state; **Checkpoint** and **Restart** resume exact state in a
fresh worker object; **Collect** returns a raw checkpoint artifact with study
and solver-build provenance. CAD or study mutation invalidates live execution
state. **Save CAD + EM project** stores an available verified job/checkpoint in
project-checkpoint v2; Open validates and resumes it in a fresh desktop session
before activating the project. The preview does not consume general heterogeneous
CAD-to-grid output.

General feature creation tools, GPU-resident rendering, adaptive LOD generation,
heterogeneous worker preparation, and solved-field visualization remain unfinished.

The native presentation follows the web interaction reference with compact controls, a working browser filter, activity-rail navigation, grid and status-overlay toggles, and a tabbed output dock. The CAD feature-history tab selects live document features; Diagnostics displays document errors, and EM Preflight displays the draft validator report. The grid backs the native evaluated-geometry viewport. Unsupported modeling and solve toolbar tools remain disabled.

The EM inspector groups Frequency, Materials, Persistent face roles, Discrete ports, Boundary policies, and Requested observables into independently collapsible sections. Editors stay instantiated while collapsed, preserving unsaved entries. Expansion starts open and is session-only; it is not part of layout persistence or study content.

EM browser selection expands and scrolls to the corresponding inspector section, revealing the inspector if hidden. ASIR/geometry summary rows lead to Frequency. Filtering retains original row indices. Stale drafts remain navigable, with editor content disabled while section headers stay available.

Selecting an EM browser row focuses its section header. Left collapses and Right expands the focused section. Workspace switching clears browser filtering and selection. Offscreen acceptance checks keyboard events and confirms stale CAD disables editors while headers remain available; Undo restores draft edit authority.

CAD parameter rows show feature/revision identity and compact editors. Enter and Apply share the commit path. Unchanged and nonnumeric entries disable Apply; compiled commands still enforce authority and regeneration. Values remain in model units (millimeters for the supported example).

Compound project recovery uses the versioned, bounded internal [project checkpoint contract](../../em/simulation_ir/include/oep/simulation_ir/project_checkpoint.hpp). It preserves all ASIR fields and document identity, including incomplete/stale drafts and failed CAD history. Version 2 optionally carries a nested checksummed prepared-job/checkpoint record. Open validates history, exact-kernel hydration, worker protocol, canonical ASIR identity, solver build, and exact resume before replacing the active session. Errors preserve the active project. Unsaved field entries, collected result databases, and layout are outside this checkpoint.
