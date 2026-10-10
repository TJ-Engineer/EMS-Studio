# Versioned schemas

Schema source files live by contract family:

- `document/`: semantic document, command, revision, and evaluation records.
- `ipc/`: UI/worker request, response, event, cancellation, and shared-buffer descriptors.
- `simulation_ir/`: Antenna Study Intermediate Representation and candidate records.
- `results/`: normalized scalar, network, field, convergence, and provenance records.

Generated C++ and Python bindings belong in the CMake build tree. Schema compatibility tests belong under `tests/conformance/`.

Current contracts:

- [ASIR 0.1](simulation_ir/asir-0.1.md)
