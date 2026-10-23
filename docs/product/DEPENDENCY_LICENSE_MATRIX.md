# Dependency and license matrix

**Status date:** October 10, 2026

This register controls which third-party components may enter the open V1 baseline. “Accepted” authorizes an implementation spike behind the named adapter; it does not authorize downloading an unpinned build or distributing binaries without the adoption and release checks below.

## Decision legend

| Decision | Meaning |
|---|---|
| In use | Present in an existing optional or default build path |
| Accepted | May be introduced for the stated V1 role after its adoption gate passes |
| Deferred | Technically and license-compatible in principle, but outside V1 scope |
| Excluded | Must not be linked into or bundled with the open V1 baseline under the listed terms |

## Current and V1 dependencies

| Component | Intended role | Decision | Upstream license classification | Integration and distribution policy |
|---|---|---|---|---|
| Open CASCADE Technology (OCCT) | Open exact B-rep kernel | In use, optional | [LGPL-2.1 with the Open CASCADE exception](https://github.com/Open-Cascade-SAS/OCCT/wiki/license) | Keep behind `modeler`; dynamically link packaged libraries in distributions; retain license, source-access instructions, exception notice, version, and kernel provenance. The current Ubuntu bootstrap pins OCCT 7.6.3 source and binary packages. |
| oneTBB | OCCT runtime dependency in the Ubuntu bootstrap | In use, transitive | [Apache-2.0](https://github.com/uxlfoundation/oneTBB/blob/master/LICENSE.txt) | Record the exact package and include its license/NOTICE material whenever bundled. It is not a public OEP API. |
| Qt 6.4.2 Core, Gui, Qml, Quick, Quick Controls 2, and accessibility support | Native desktop shell | In use, optional | [Commercial or LGPL-3.0/GPL-3.0, depending on module and file](https://doc.qt.io/qt-6/licensing.html) | CMake requires the exact release and only the named compiled modules. The Ubuntu development path also locks the QML, Models, WorkerScript, Quick, Window, Controls, Templates, and Layouts runtime packages plus their transitive package checksums. Keep distribution dynamically linked and include notices, corresponding-source access, and relinking instructions. GPL-only add-ons remain outside the allowlist; a commercial Qt build is a separate packaging choice. See the [adoption record](QT_ADOPTION_RECORD.md). |
| Kokkos Core | Production CPU/GPU kernel portability | Accepted | [Apache-2.0 with LLVM exceptions](https://github.com/kokkos/kokkos/blob/develop/LICENSE) | Keep the FP64 reference solver independent. Add Kokkos only behind the production-kernel adapter. Build Serial/OpenMP and CUDA as separate execution profiles because Kokkos permits one host and one device backend in a configuration. |
| OpenMP runtime supplied by the selected compiler | Multicore CPU backend | Accepted | Toolchain-specific | Do not copy a runtime from an unrecorded compiler installation. Pin the compiler/runtime pair and record its redistribution terms per packaged platform. |
| Open MPI | Linux distributed-memory reference implementation | Accepted | [Permissive project license](https://www.open-mpi.org/community/license.php) | Use the MPI standard at the solver boundary and Open MPI as the first Linux conformance implementation. Prefer cluster/system modules initially; bundling requires review of Open MPI and its selected support libraries. |
| NVIDIA CUDA toolkit/runtime | First production GPU backend | Accepted with proprietary-toolchain gate | [NVIDIA CUDA SDK EULA](https://docs.nvidia.com/cuda/eula/index.html) | The toolkit is an external build prerequisite. Distribute only components expressly listed as redistributable for the pinned version, with required notices, after release review. A CPU build must remain fully usable without CUDA. |
| VTK | Scientific result and field visualization | Accepted | [BSD-3-Clause](https://github.com/Kitware/VTK/blob/master/Copyright.txt) | Introduce behind `simulation-results`/visualization adapters. Pin enabled modules and record their third-party notices; avoid turning VTK data objects into the durable result contract. |
| HDF5 | Chunked durable field/result storage | Accepted | [BSD-3-Clause](https://github.com/HDFGroup/hdf5/blob/develop/LICENSE) | Introduce behind the result-storage adapter. Pin filters and plugins explicitly, disable unneeded dynamic plugin loading in the default reader, and treat untrusted result files as parser input requiring bounds tests and fuzzing. XDMF metadata remains a documented interchange layer. |
| In-house structured Cartesian grid | V1 FDTD mesh | Accepted V1 baseline | Project MIT | This is the product mesh path for V1. It avoids introducing an unstructured-mesher license into the antenna-FDTD release and preserves the traceability already tested by `em-mesh`. |

## Deferred and excluded candidates

| Component | Possible role | Decision | Upstream license classification | Reason |
|---|---|---|---|---|
| MFEM | Later frequency-domain FEM | Deferred to V2 | [BSD-3-Clause](https://github.com/mfem/mfem/blob/master/LICENSE) | License-compatible, but FEM is a separate solver family with its own meshing and verification program. |
| PETSc | Later distributed sparse solvers | Deferred to V2 | [BSD-2-Clause](https://gitlab.com/petsc/petsc/-/blob/main/LICENSE) | License-compatible; adopt only with the FEM solver and a pinned scalar/index/GPU configuration. |
| hypre | Later multigrid/preconditioning | Deferred to V2 | [MIT](https://github.com/hypre-space/hypre/blob/master/LICENSE-MIT) or [Apache-2.0](https://github.com/hypre-space/hypre/blob/master/LICENSE-APACHE) | License-compatible; adopt only through MFEM/PETSc configuration evidence and backend conformance tests. |
| Gmsh | General unstructured mesh generation | Excluded from linked or bundled open V1 baseline under GPL terms | [GPL-2.0-or-later with an exception, or a separate commercial license](https://gmsh.info/#Licensing) | V1 uses the in-house structured grid. Gmsh may remain an independent research/validation tool; product integration requires a commercial-license decision or a separately reviewed GPL distribution model. |
| FreeCAD PlaneGCS | More complete sketch constraints | Deferred | [LGPL-2.1-or-later](https://github.com/FreeCAD/FreeCAD/blob/main/src/Mod/Sketcher/App/planegcs/GCS.h) | The current deterministic solver is project-owned. Any source reuse needs a dedicated adapter, exact-source provenance, modification policy, and release review. |
| AMD HIP/ROCm and Intel SYCL toolchains | Additional Kokkos device backends | Deferred until the NVIDIA profile passes | Vendor/open-component mix varies by release | Kokkos keeps these technically reachable, but each needs its own toolchain inventory, hardware CI, numerical conformance, checkpoint tests, and runtime calibration. |
| Parasolid, D-Cubed, and HOOPS | Optional commercial CAD components | Separate licensed builds only | Proprietary | No source, headers, test data, or behavior from an evaluation may enter the open baseline without a signed agreement and clean adapter boundary. |

## Adoption gate

Before a dependency is added to CMake or a distribution, its change must include:

1. a role and owning adapter with no leakage into durable OEP schemas or unrelated public APIs;
2. an immutable upstream version or commit, download origin, archive checksum, and supported build configuration;
3. the exact license expression, copyright notices, NOTICE files, transitive libraries, optional plugins/codecs, and source-offer or relinking obligations;
4. a packaging decision for each supported platform: system prerequisite, dynamically linked bundle, separately installed toolchain, or optional commercial build;
5. an SBOM entry and third-party notice entry generated from the artifacts actually shipped;
6. vulnerability review, parser/input threat assessment, and an update owner;
7. adapter contract tests plus ABI, restart, numerical, or rendering conformance appropriate to the dependency;
8. explicit approval for any GPL-only, proprietary, source-available, or license-exception case.

## Release rule

The repository MIT license covers project-owned work only. A release candidate is not distributable until its artifact-level dependency manifest and notices match the binaries being packaged. This register is the engineering allowlist; counsel or another authorized license reviewer must resolve ambiguous obligations before public distribution.
