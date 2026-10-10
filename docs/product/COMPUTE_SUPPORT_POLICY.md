# Compute and platform support policy

**Status date:** October 10, 2026

This policy fixes the first solver execution targets without turning planned backends into performance claims. A backend becomes supported only after it passes the same physics, restart, cancellation, provenance, and resource-estimate contracts as the FP64 reference path on named hardware.

## Support levels

| Level | Meaning |
|---|---|
| Reference | Correctness authority; required in every development and release environment |
| V1 required | Release target; must have continuous build and conformance evidence before V1 |
| Experimental | May compile or run, but carries no compatibility or performance promise |
| Deferred | Outside the V1 release criteria |

## Execution profiles

| Profile | Platform/backend | Precision and role | Level | Qualification required |
|---|---|---|---|---|
| `reference-cpu-fp64` | Portable C++20, one CPU thread | Deterministic numerical oracle and checkpoint authority | Reference; implemented | All Maxwell/golden tests, bit-exact restart within one build, sanitizer runs, and named cost calibration |
| `production-cpu-openmp-fp64` | Kokkos OpenMP on x86-64 Linux and Windows | Multicore local solves | V1 required; planned | Reference comparison, thread-count matrix, race sanitizer where supported, cancellation/checkpoint recovery, memory/runtime calibration |
| `production-nvidia-cuda-fp64` | Kokkos CUDA on native x86-64 Linux with NVIDIA GPU | First accelerated solver | V1 required; planned | Device-memory budget, CPU/GPU tolerance matrix, deterministic metadata, checkpoint interchange, driver/toolkit matrix, native hardware CI |
| `distributed-cpu-openmpi-fp64` | Kokkos OpenMP plus Open MPI on Linux x86-64 | First multi-node conformance path | V1 required for cluster execution; planned | One- and multi-rank equivalence, partition/halo tests, rank-failure policy, restart with changed rank count policy, cluster calibration |
| `distributed-nvidia-openmpi-fp64` | CUDA plus Open MPI on Linux x86-64 | Multi-GPU cluster path | Experimental until single-GPU and CPU MPI pass | GPU-aware/staged-transfer matrix, halo overlap checks, multi-rank numerical and checkpoint evidence |
| `production-amd-hip` | Kokkos HIP/ROCm | AMD GPU path | Deferred | Dedicated AMD hardware CI and full backend qualification |
| `production-intel-sycl` | Kokkos SYCL | Intel GPU path | Deferred | Dedicated Intel hardware CI and full backend qualification |
| `production-apple-gpu` | No selected kernel backend | Apple GPU path | Deferred | A supported Kokkos or separate kernel strategy and conformance program |

Kokkos configurations use one device backend per build, so CUDA, HIP, and SYCL artifacts are separate packages rather than runtime switches inside one worker binary. Every worker advertises its execution profile, precision, solver build, accelerator, and hardware calibration ID.

FP32 and mixed-precision field updates are unsupported until benchmark-specific phase, energy, S-parameter, and restart tolerances are approved. Sensitive accumulations remain FP64 unless evidence supports a different policy.

## Desktop and host platforms

| Host | V1 intent | Current evidence | Boundary |
|---|---|---|---|
| Ubuntu 24.04 x86-64 | Primary development, CPU reference, OCCT, native NVIDIA, and cluster client | Strict GCC 13 and sanitizer suites pass; optional OCCT suite passes | Current evidence was collected under WSL2, so it does not qualify native GPU or performance behavior |
| Windows 11 x86-64 | Native CAD/EM desktop and local CPU solver | Planned | Requires MSVC/clang-cl CI, Qt/OCCT package validation, installer and restart tests |
| Native Linux x86-64 cluster | Open MPI CPU and NVIDIA execution | Planned | Exact distribution, interconnect, MPI, driver, and GPU SKUs must be recorded before performance or scale claims |
| WSL2 Ubuntu x86-64 | Developer convenience | Current development host | Not a release performance reference; GPU access is currently unavailable in the recorded environment |
| macOS arm64 | Possible CAD/EM desktop with CPU solver | Experimental after Qt shell | No V1 GPU commitment and no release support until CI and kernel packaging pass |

## Hardware qualification records

Performance evidence must name a stable hardware profile rather than labels such as “workstation” or “GPU.” Each record includes:

- CPU/GPU model, usable memory, accelerator architecture, interconnect, and storage class;
- operating system/kernel, compiler, standard library, Kokkos, MPI, CUDA/driver, and solver build IDs;
- enabled backend, thread/rank/device counts, affinity and deterministic-mode settings;
- calibration workload hashes, cell updates per second, prediction interval, checkpoint throughput, and peak-memory method;
- date, thermal/power policy, and whether the result came from native hardware, a VM, WSL, or a scheduler allocation.

The current WSL2 CPU environment is valid for functional Gate 0 evidence. It is not the V1 reference workstation or cluster. Exact native workstation and cluster SKUs remain procurement choices and must be added before Gate 2 performance acceptance.

## Backend promotion gate

A planned backend becomes supported only when:

1. its dependency and toolchain entries pass the dependency adoption gate;
2. every solver capability is advertised explicitly and unsupported features fail with typed diagnostics;
3. the Maxwell, port, energy, convergence, cancellation, checkpoint/restart, and process-recovery suites pass against approved tolerances;
4. cross-backend results and restart artifacts satisfy the published compatibility policy;
5. predicted and measured memory/runtime/storage are calibrated on named hardware;
6. CI covers the supported compiler, OS, driver, and hardware combination;
7. install, update, diagnostics, and third-party notices are verified from the packaged artifact.
