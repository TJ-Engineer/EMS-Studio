# Desktop Software Design Description

**Baseline:** October 12, 2026 · revision 9

## Responsibilities and technology boundary

CAD Studio and EM Studio use one `app_shell` host with explicit initial workspace. The Qt host loads `Main.qml` and supplies a native `ShellController`. QML owns presentation and interaction; compiled document, CAD, and ASIR modules own state transitions and validation. The headless host reports application identity for non-Qt builds.

The supported shell uses exact Qt 6.4.2 Core, Gui, Qml, Quick, and Quick Controls 2. `Theme.js` centralizes tokens; `ShellButton.qml` supplies consistent focus/selection styling. The [web prototype](../../../apps/ui_wireframe/index.html) is a design reference, not a second engine client.

## Ownership and lifetime

The Qt host retains controller and QML engine for the application lifetime. `ShellController` can borrow a host document or own an OCCT CAD session. Hosts must detach borrowed documents before destruction and call `refreshDocument()` after external changes. The owned session groups kernel, evaluator, and document so their dependency lifetimes align.

Replacing a document resets selected feature and EM draft authority and establishes a new document-session identity. Drafts are controller-owned optional ASIR values, not persistent document members. QSettings owns layout persistence independently of document recovery.

## Presentation projections

| Projection | Source of truth | Consumer |
|---|---|---|
| Document status/revision | Active semantic/evaluated geometry access | Header, footer, viewport status, dock |
| Feature rows and parameter rows | Evaluation/feature definitions and semantic values | Browser, feature strip, CAD inspector |
| Diagnostics/file message | Engine command/evaluation and file outcomes | Diagnostics dock and inspector |
| Undo/redo enabled state | Document timeline | Edit actions |
| Study status/fingerprint/report | Draft plus live validation context | EM browser, preflight, inspector |
| Faces | Final-body resolved persistent topology | Role and port selectors |
| Material/port/setup catalogs | Stored draft definitions | EM forms and summaries |
| Viewport scene and selection | Current final exact-body artifact plus persistent topology bindings | Native Canvas, overlay, future authoring bridge |
| Worker estimate/state/checkpoint/artifacts | Prepared job and reference worker value contracts | EM Worker tab |

Qt revision strings retain integer precision when passing expected revisions through QML. Numeric editors check usability, but compiled commands remain authoritative. A disabled UI action does not replace validation at the command boundary.

## Document workflows

New creates the supported OCCT example. Open decodes/restores/hydrates a candidate session before replacing active state. Failures preserve the current session. Save uses atomic internal CAD recovery files. The path popup accepts local paths and replacement is explicit; no autosave or public native package is implied.

Select a browser row or history card to load that feature's declared parameters. Apply submits parameter ID/value/expected revision. Regeneration diagnostics and current-geometry status reflect the committed engine result. Undo/redo navigate the document timeline. A failed edit must remain visible, not be replaced with a synthetic successful revision label.

The CAD inspector includes a plain-text selected-feature/revision identity card and compact value/Apply rows. Enter and Apply use the same presentation guard: nonempty finite values differing numerically from the projected value. The engine remains responsible for revision checks and regeneration. Unit labels remain model units; the supported example uses millimeters. Offscreen acceptance checks unchanged/nonnumeric disabling and the Enter accepted signal followed by failed regeneration and Undo.

## EM workflows

Prepare requires current evaluated supported CAD geometry. Rebuild requires explicit acknowledgement that material/frequency/role/port/boundary/monitor setup resets. CAD changes preserve the old draft and expose stale authority. Failed CAD blocks new preparation. Undo to the original authority can restore edit eligibility.

The inspector supports study-local frequency bands; nondispersive material creation/loading/replacement with SI properties, provenance and validity bands; air/vacuum, keep-out, conductor, dielectric and ground face roles; discrete ports with resolved face/reference-plane selectors and impedance/orientation; open-PML/PEC policies with background material/padding; and network/impedance monitor requests with declared ports and bands.

Every command carries the expected fingerprint and validates live CAD authority. Invalid edits leave the draft unchanged. Material/port deletion reports dependent role/boundary/monitor constraints. Incremental band repairs preserve remaining preflight issues. Structural setup does not calibrate an observable.

The Worker tab becomes available when only solver-build and provenance remain missing. Estimate binds those fields, validates the complete ASIR, displays a real resource forecast, and prepares the bounded homogeneous reference job. Launch, progress, cancellation, checkpoint, fresh-worker restart, and raw artifact collection use `ReferenceSimulationWorker`; QML contains no lifecycle rules. CAD or study mutation invalidates the job. See the [Worker Experience SDD](WORKER_EXPERIENCE_SDD.md).

CAD-only checkpoint saves exclude EM drafts. **Save/Open CAD + EM project** persists complete ASIR values with CAD history and document identity through the bounded compound recovery contract. Project v2 optionally adds a nested prepared-job/checkpoint record. Loading validates history, hydrates a separate exact-kernel candidate, verifies current CAD/ASIR/solver authority, and resumes in a fresh worker before activation. Project v1 remains readable without worker state. Stale/failed CAD and incomplete drafts remain recoverable when no worker record is present. Unsaved editor text, collected results, and layout are outside recovery.

## Layout and navigation

Horizontal/vertical split views provide resizable project browser, center viewport/output, and inspector regions. Dimensions and visibility persist under `shell/layout-v1` with bounds on restored sizes. Each executable's initial workspace remains authoritative rather than being overridden by the last UI mode.

The browser filter hides nonmatching rows without changing underlying feature indices. Activity controls toggle browser/diagnostics, focus the viewport, and switch to EM. The CAD history tab selects live feature rows; Diagnostics displays document errors. EM Preflight exposes the real validator report.

Grid/overlay toggles control viewport decoration and status text. In `qt-cad`, the current evaluated exact body is tessellated into an immutable assembly scene and rendered with material-aware face shading and edge curves. Scene instances support shared mesh LODs, transforms, visibility, and occurrence-qualified persistent selection. Left drag orbits, middle/right drag pans, the wheel zooms, Fit reframes, Clip sections by model Z, hover preselects, and click selects. Failed current regeneration clears the scene. The Qt-only profile starts with no CAD scene. GPU buffers, solved fields, clip caps, full web visual parity, and manual review remain unfinished.

The reusable `InspectorSection.qml` groups EM authoring into six independently collapsible sections. Its default content property retains editor instances while the body is hidden, so entered values and signal bindings survive collapse. All sections initially expand; expansion is session-only, changes no study authority, and is not stored in QSettings. Offscreen tests toggle each section and verify unsaved material values and canonical fingerprint remain unchanged.

EM browser rows now select and expand the matching section, reveal the inspector, focus its header, and scroll it into view. ASIR/geometry summary rows lead to Frequency; material, role, port, boundary, and monitor rows lead to their respective editors. Filtering preserves original indices. Stale CAD disables section content while leaving collapse/navigation available for inspection. Offscreen tests exercise all seven row routes and verify study content remains unchanged.

Browser navigation focuses the actionable section header. Left collapses and Right expands that section; header focus stays available when CAD authority disables the editors. Switching workspace clears the previous browser filter and selection. Offscreen tests deliver actual Left/Right key events, verify header focus, and exercise stale-draft navigation and Undo recovery.

## Keyboard and accessibility

`Ctrl+1`/`Ctrl+2` select CAD/EM, `Ctrl+K` opens commands, `Ctrl+J` toggles diagnostics, and Escape closes command discovery. Focusable controls expose accessible names/descriptions and selection/focus styling. Stable object names support offscreen interaction checks.

Offscreen smoke mode loads both complete shells and invokes actual actions for navigation, palette/shortcuts, viewport controls, CAD editing/recovery, EM setup, and the visible Worker controls where OCCT is enabled. Unit acceptance separately verifies exact OCCT scene generation, persistent-ID picking, one-million-source-triangle assembly projection, and worker lifecycle rejection paths. These checks do not establish pixel parity or assistive-technology usability.

## Support and extension constraints

Missing CAD creation/loading on `qt-shell` is a build capability boundary; use `qt-cad` for the OCCT workflow. Empty startup is expected. The two application processes do not share a loaded document automatically. The desktop worker uses an in-process homogeneous reference preview. Its verified checkpoint survives compound project Save/Open and resumes in a fresh desktop session. It does not yet consume the general CAD-to-grid output or represent a supervised external service.

Keep domain validation out of QML. Future native dialogs, renderer, persistence, and worker status UI must preserve command authority and typed errors. Qt UI mutation remains on the Qt owning thread; any future asynchronous evaluation/worker callbacks require explicit dispatch and lifetime handling.

## Implementation reference inventory

### `modules/platform/app_shell`

- [README.md](../../../modules/platform/app_shell/README.md) — `modules/platform/app_shell`
- [application.hpp](../../../modules/platform/app_shell/include/oep/app_shell/application.hpp) — `modules/platform/app_shell/include/oep/app_shell`
- [shell_controller.hpp](../../../modules/platform/app_shell/include/oep/app_shell/shell_controller.hpp) — `modules/platform/app_shell/include/oep/app_shell`
- [InspectorSection.qml](../../../modules/platform/app_shell/qml/InspectorSection.qml) — collapsible presentation component
- [Main.qml](../../../modules/platform/app_shell/qml/Main.qml) — `modules/platform/app_shell/qml`
- [ShellButton.qml](../../../modules/platform/app_shell/qml/ShellButton.qml) — `modules/platform/app_shell/qml`
- [application_headless.cpp](../../../modules/platform/app_shell/src/application_headless.cpp) — `modules/platform/app_shell/src`
- [application_qt.cpp](../../../modules/platform/app_shell/src/application_qt.cpp) — `modules/platform/app_shell/src`
- [shell_controller.cpp](../../../modules/platform/app_shell/src/shell_controller.cpp) — `modules/platform/app_shell/src`

