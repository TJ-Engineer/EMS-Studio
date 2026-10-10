# ADR 0002: Validated ASIR boundary for electromagnetic execution

**Status:** Accepted  
**Date:** October 9, 2026

## Context

The electromagnetic path must preserve engineering intent from CAD without allowing stale geometry, transient kernel IDs, unqualified physical values, or AI-generated free-form instructions to reach a solver. The simulation representation must remain independent of kernel handles and solver discretization details while retaining enough identity and provenance to reproduce a study.

## Decision

ASIR 0.1 is a compiled solver-neutral value model. Every physical scalar is normalized to SI and carries an explicit dimension. CAD entities use wire-safe persistent IDs consisting of the originating feature ID and local topology ID; process-local modeler and kernel types do not enter the schema.

Validation receives an authoritative context from the model engine. A study is valid only when its document ID, equal semantic/evaluated revision IDs, exact-geometry hash, persistent topology references, entity kinds, and CAD-authoritative parameter IDs match that context. Failed semantic heads and stale geometry hashes are rejected.

Materials, semantic roles, ports, reference planes, boundaries, solver/mesh policy, resource budgets, monitors, goals, approvals, and build provenance are explicit study data. ASIR 0.1 supports FDTD execution only. No orchestration layer may silently substitute another solver family or physics policy.

Every study has a deterministic SHA-256 fingerprint over all fields in canonical stable-ID order.

## Alternatives

- Pass live CAD or kernel objects to the solver. This couples processes and makes restart, audit, and distributed execution unsafe.
- Use transient kernel face numbers. Parameter changes can silently move an EM role or port to the wrong topology.
- Store untyped numeric values with display-unit metadata elsewhere. Validation cannot reliably distinguish length, frequency, angle, and impedance mistakes.
- Allow solver-specific configuration objects to be the study format. That prevents review across engines and makes orchestration choose physics implicitly.
- Accept the last-good body for a failed semantic head. Results would claim to represent parameter values that were never successfully regenerated.

## Consequences

- Study generation must resolve CAD authority before solver preparation.
- Schema DTOs intentionally duplicate the small persistent-ID wire shape rather than importing CAD modeler types.
- Validation errors are typed and path-addressed for UI, automation, and audit use.
- Fingerprints change with physics, geometry, policy, budgets, and provenance, providing a stable cache and result linkage key.
- Concrete JSON or Protocol Buffers encoding remains a separate choice; it must preserve the ASIR 0.1 semantics and canonical fingerprint inputs.
