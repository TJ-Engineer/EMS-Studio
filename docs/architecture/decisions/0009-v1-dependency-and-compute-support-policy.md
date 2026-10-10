# ADR 0009: V1 dependency and compute support policy

**Status:** Accepted
**Date:** October 10, 2026

## Context

The architecture names Qt, OCCT, Kokkos, MPI, VTK, HDF5, MFEM, PETSc, hypre, and candidate mesh technology, but naming a library is not a distribution or support decision. License obligations, transitive packages, proprietary toolchains, hardware availability, and numerical conformance differ materially. Adding all named libraries early would enlarge the build and legal surface before their owning product slices exist.

The repository currently uses only the C++ build toolchain and an optional OCCT/oneTBB path. Its FP64 single-threaded reference solver is the only qualified compute backend.

## Decision

Adopt a gated dependency allowlist and a tiered compute support matrix.

The open V1 path accepts dynamically linked, version-verified LGPL components where their obligations can be met, and permissively licensed scientific libraries behind narrow adapters. GPL-only libraries are not linked into or bundled with the open V1 baseline. Proprietary CAD components remain separate licensed builds. Proprietary GPU toolchains are external prerequisites whose redistributable runtime set must be reviewed for each pinned release.

Use the in-house structured Cartesian grid for V1. Accept Qt's verified LGPL modules for the native shell, Kokkos for production kernels, Open MPI for the first Linux distributed implementation, NVIDIA CUDA for the first GPU target, VTK for result visualization, and HDF5 for durable field storage. Defer MFEM, PETSc, and hypre to the later FEM program. Exclude Gmsh from the linked/bundled open V1 baseline under its GPL terms; it may remain an independent research tool or return through a separate commercial-license decision.

Retain the portable FP64 single-threaded solver as the correctness authority. Qualify Kokkos OpenMP CPU, Kokkos CUDA NVIDIA, and Open MPI Linux profiles separately. AMD HIP, Intel SYCL, Apple GPU, mixed precision, and unqualified multi-GPU execution remain deferred or experimental until they pass the same numerical, restart, provenance, and cost contracts.

Exact native workstation and cluster hardware must be recorded before performance claims. WSL2 evidence remains functional evidence and cannot qualify GPU or release performance.

## Alternatives

- Add every planned library to the bootstrap build. This would create unused transitive and packaging obligations before adapter contracts exist.
- Use Gmsh as the default V1 mesher. Structured FDTD does not require an unstructured mesh, and the open Gmsh terms would impose a distribution decision unrelated to current capability.
- Target CUDA, HIP, SYCL, and Apple GPU concurrently. Separate toolchains and hardware matrices would dilute the first accelerator conformance effort.
- Replace the reference solver with Kokkos immediately. This would remove the simple independent authority needed to diagnose backend divergence.

## Consequences

- Each dependency has an owner, scope, license classification, packaging rule, and adoption checklist before it enters CMake.
- The native UI may begin with an LGPL-verified Qt module allowlist while preserving a separate commercial packaging option.
- NVIDIA CUDA is the first production GPU target; CPU-only builds remain complete and usable.
- Future backends and FEM libraries remain architecturally reachable without being presented as supported V1 capability.
- Artifact-level SBOMs, notices, exact versions, vulnerability review, and authorized license review remain release gates rather than claims made by this planning record.
