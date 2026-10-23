# Native Viewport Software Design Description

**Baseline:** October 12, 2026 · revision 2

## Responsibility and authority

The viewport displays current evaluated CAD geometry and returns persistent topology identities. It does not own semantic parameters, exact B-rep authority, topology reconciliation, or ASIR definitions. Display meshes are disposable products derived from the exact body.

```mermaid
flowchart LR
    DOC[Current document evaluation] --> ART[Final CAD artifact]
    ART --> BODY[Exact kernel body]
    BODY --> KT[Kernel tessellation]
    ART --> BIND[Persistent topology bindings]
    KT --> PACKET[Immutable mesh packet]
    BIND --> PACKET
    PACKET --> INST[Assembly instances and LODs]
    INST --> PROJ[Camera, clipping, and projection]
    PROJ --> QT[Qt Quick Canvas]
    QT --> PICK[Instance-qualified persistent ID]
```

If the semantic head has no current evaluated geometry, the scene is empty. A last-good body may exist for recovery but is not rendered as current authority.

## Kernel tessellation boundary

`GeometryKernel::tessellate` accepts an exact-body handle and positive linear/angular deflections. The default adapter implementation returns `unsupported_operation`, preserving compatibility for non-rendering kernels. The OCCT implementation runs `BRepMesh_IncrementalMesh`, emits flat-normal triangles for faces, samples exact edge curves, checks cancellation, and returns typed invalid-handle/input/kernel diagnostics.

Each triangle retains its kernel face ID and each polyline its kernel edge ID. These IDs are adapter-local and must not escape as UI selection authority.

## Mesh packets and assembly scenes

`build_scene_packet` translates kernel IDs by exact kind-and-ID match against the final artifact's `TopologyBinding` values. Render primitives receive the canonical `feature:topology:local` persistent ID string. The packet also retains the evaluated geometry hash and exact-body bounds.

`viewport::SceneAssembly` adds the renderer-facing assembly boundary. Each `SceneInstance` owns a persistent instance ID, column-major affine transform, material, visibility flag, and an ordered set of `shared_ptr<const ScenePacket>` LODs. Repeated occurrences share immutable mesh storage. Assembly bounds are computed from transformed packet bounds, and source triangle count includes every visible occurrence at its finest LOD.

The projection policy applies a display triangle budget. When an assembly exceeds it, projection selects the coarsest available LOD for each instance; otherwise it uses the finest packet. Projected primitives carry the instance material. Picking qualifies topology IDs with the instance ID so identical shared geometry remains distinguishable by occurrence.

The controller stores a `shared_ptr<const SceneAssembly>`. Rebuild is replacement-only: parameter edit, Undo/Redo, new/open, and project recovery either publish a complete scene or clear it and retain a diagnostic. Selection and preselection clear whenever geometry authority changes.

## Camera, clipping, and hit testing

`viewport` owns renderer-neutral camera math. The orthographic camera has yaw, pitch, zoom, screen pan, and a normalized Z section fraction. Orbit clamps pitch to avoid a singular view; zoom is bounded; Fit resets pan and zoom while retaining orientation.

Projection transforms instance geometry, sorts triangles and edges by view depth for painter ordering, and clips against the assembly Z range. Face hit testing walks displayed triangles from front to back and returns the first nonempty instance-qualified persistent ID containing the screen point.

The Qt controller exposes projected frame values to QML. Canvas draws material-colored shaded faces and exact-curve overlays; persistent hover and selection IDs change highlight colors. Left drag orbits, middle/right drag pans, the wheel zooms, Fit reframes, the Clip slider moves the section plane, hover preselects, and a left click selects.

## Failure and concurrency behavior

| Condition | Behavior |
|---|---|
| No document or Qt-only build | Empty scene with honest status |
| Failed current regeneration | Clear current scene; retain document diagnostics |
| Invalid/stale kernel handle | Reject scene build with typed diagnostic |
| Invalid tessellation deflection | Reject without partial scene publication |
| Missing topology binding | Primitive remains drawable but cannot become a selectable document identity |
| Hidden instance | Excluded from bounds, counts, projection, and picking |
| Document replacement | Build against candidate session after hydration; failed load preserves active scene |

Evaluation, scene creation, CPU projection, and Canvas drawing are synchronous on the Qt owning thread. Immutable packets allow a future background builder or public-API GPU renderer to publish complete replacement scenes without changing CAD authority.

## Verification and performance boundary

`viewport_projection_test` checks deterministic projection, orbit, pan, zoom, Fit, section clipping, transforms, materials, visibility, shared LOD selection, instance-qualified picking, and persistent IDs. Its representative assembly has 256 visible occurrences sharing a 4,096-triangle fine mesh and a one-triangle display LOD: 1,048,576 source triangles project as 256 display triangles. A 120-frame 1280×720 strict-debug run must average below 16.667 ms; the final October 12 verification measured approximately 0.82 ms.

`app_shell_state_test` verifies live OCCT tessellation, visible frame data, matching preselection/selection IDs, clipping, clearing on failed regeneration, and recovery through Undo. QML lint and both offscreen shell smoke tests verify the complete view loads.

This completes the native viewport panel's measured one-million-source-triangle/60 Hz core projection boundary. It is a CPU projection and Qt Canvas implementation, and the measurement is a regression fixture rather than a portable hardware claim. GPU-resident buffers, ten-million-triangle qualification, frustum/occlusion acceleration, adaptive LOD generation, edge-picking tolerance, clip caps, solved-field rendering, and manual visual/accessibility review remain future work.

## Implementation inventory

- [`kernel_adapter.hpp`](../../../modules/cad/modeler/include/oep/modeler/kernel_adapter.hpp) — kernel-neutral tessellation request/result
- [`occt_geometry_kernel.cpp`](../../../modules/cad/modeler/src/occt_geometry_kernel.cpp) — exact-body tessellation
- [`scene_packet.hpp`](../../../modules/cad/tessellation/include/oep/tessellation/scene_packet.hpp) and [`scene_packet.cpp`](../../../modules/cad/tessellation/src/scene_packet.cpp) — persistent-ID packet construction
- [`viewport.hpp`](../../../modules/cad/viewport/include/oep/viewport/viewport.hpp) and [`viewport.cpp`](../../../modules/cad/viewport/src/viewport.cpp) — assembly, camera, projection, LOD, clipping, and picking
- [`viewport_projection_test.cpp`](../../../tests/unit/viewport_projection_test.cpp) — deterministic and measured acceptance corpus
- [`shell_controller.cpp`](../../../modules/platform/app_shell/src/shell_controller.cpp) — document lifecycle integration
- [`Main.qml`](../../../modules/platform/app_shell/qml/Main.qml) — native Qt drawing and input
