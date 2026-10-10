# Antenna Study Intermediate Representation 0.1

ASIR 0.1 is the solver-neutral, reviewable contract between an evaluated CAD document and electromagnetic preparation or solver services. The compiled schema is [the `oep::simulation_ir` value model](../../modules/em/simulation_ir/include/oep/simulation_ir/simulation_ir.hpp); this document records its wire-level meaning and validation rules before a concrete JSON or Protocol Buffers encoding is selected.

## Authority and execution rule

An ASIR study is executable only when all of these values match the model engine's current validation context:

- CAD document ID;
- semantic revision ID;
- successfully evaluated revision ID;
- current exact-geometry hash;
- every referenced persistent topology ID and expected entity kind;
- every parameter declared as CAD-authoritative.

The semantic and evaluated revision IDs must be equal. A failed semantic head has no current geometry and cannot produce valid ASIR, even when a last-good body is retained for recovery.

## Physical values

All physical scalars are stored in SI through `SiValue { value, dimension }`. `unspecified` dimensions are invalid. ASIR 0.1 defines dimensions for length, time, frequency, angle, conductivity, relative permittivity, relative permeability, loss tangent, impedance, electric potential, power, and dimensionless values.

Revision numbers, persistent local IDs, enum values, counts, byte budgets, and unit-vector components are identifiers or dimensionless structural data rather than physical scalars.

## Required domains

| Domain | ASIR 0.1 representation |
|---|---|
| Identity | Study/document IDs, equal semantic and evaluated revision IDs, geometry hash, ASIR version, solver build, mesh-policy hash, optional candidate ID |
| Geometry | Right-handed orthonormal coordinate frames and exact-body/component references |
| Parameters | Stable ID, display name, dimensioned nominal value and optional bounds, discrete flag, and CAD/study-local/derived authority |
| Materials | Relative permittivity/permeability, conductivity, loss tangent, dispersion kind, optional thickness/roughness and validity band, source provenance; 0.1 execution accepts nondispersive models until coefficient records are versioned |
| EM roles | Persistent evaluated-CAD entity reference, semantic role, and required material binding |
| Excitation | Stable port ID, type, persistent face and reference plane, orientation, mode, impedance, de-embedding, amplitude, phase, and termination |
| Boundaries | PML/electric/magnetic/symmetry/periodic policy, background material, padding, periodic pair and scan angles |
| Solver policy | Allowed family, frequency band, accuracy, cell-size policy, wavelength resolution, symmetry/adaptation flags, precision and phasor convention |
| Fidelity/cost | Named fidelity profile and hard cell, memory, runtime, and storage budgets with escalation policy |
| Monitors and goals | Frequency-bounded requested observables, port/region references, expressions, weights, tolerance and pass thresholds |
| Provenance | Approval records, generated operations, solver checksum, host, creation time, warnings and raw-artifact references |

## Validation behavior

Validation produces stable typed issue codes and paths. Errors include:

- unsupported version, empty or duplicate IDs;
- document, revision, or geometry-hash mismatch;
- unavailable current geometry;
- non-finite, unqualified, or incorrectly dimensioned values;
- invalid coordinate frames, parameter bounds, material properties, mesh policy, resource budget, monitors, or goals;
- CAD-authoritative parameters absent from the evaluated document;
- derived parameters with writable bounds;
- missing or kind-mismatched persistent topology references;
- missing role or boundary materials;
- dispersive material selections whose coefficient records are not defined by ASIR 0.1;
- duplicate or malformed ports;
- solver frequencies outside a material model's validity band;
- incomplete approval/build/host/time provenance.

Warnings do not make a study invalid. ASIR 0.1 currently warns when wavelength resolution is below ten cells while rejecting fewer than four.

## Canonical fingerprint

`canonical_fingerprint` writes every ASIR 0.1 field in a length-delimited canonical form. Collections keyed by stable IDs and semantic sets are sorted before encoding. IEEE negative zero is normalized to zero. The resulting bytes are hashed with SHA-256 and identified as:

```text
asir-0.1-sha256:<64 lowercase hexadecimal digits>
```

Any change to CAD authority, geometry, materials, roles, ports, solver policy, budgets, monitors, goals, or provenance changes the fingerprint. Reordering ID-keyed collections does not.

## Compatibility

The version string is part of both validation and the fingerprint. A future schema version must define migration and unknown-field preservation before it is accepted. ASIR 0.1 accepts only the FDTD solver family; later solver families require an explicit schema/validator revision rather than silent selection.
