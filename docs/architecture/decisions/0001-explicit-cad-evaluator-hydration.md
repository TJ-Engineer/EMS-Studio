# ADR 0001: Explicit CAD evaluator hydration before document restore

**Status:** Accepted  
**Date:** October 9, 2026

## Context

Document checkpoints persist semantic revisions, evaluation records, diagnostics, output hashes, and the revision-tagged last-good geometry hash. Exact-kernel body handles are process-local and cannot be serialized or reused after the model engine terminates. A restored document may also have a failed semantic head whose clean upstream feature outputs are needed for a later repair.

`Document::restore` already has a validation-only contract. Performing hidden regeneration there would make restore depend on kernel availability, change persisted evaluation state, and violate tests that require no evaluator calls during restore.

## Decision

The CAD layer explicitly hydrates a fresh `CadFeatureEvaluator` before calling `Document::restore`.

Hydration deterministically evaluates each persisted semantic revision in topological order to rebuild transient exact-body handles and feature artifacts. It verifies the evaluator identity, per-feature success/failed/blocked state, output hashes, typed diagnostics, and final geometry hash against the persisted evaluation record. A mismatch rejects hydration. The validated `DocumentState` is then passed unchanged to `Document::restore`, which performs no feature evaluation.

Topology-reconciliation decisions are emitted as typed feature-evaluation diagnostics. They therefore survive the existing checkpoint path and are included in hydration verification.

## Alternatives

- Persist raw C++ or kernel handles. These values are process-local, ABI-sensitive, and invalid after restart.
- Regenerate implicitly inside `Document::restore`. This hides expensive work and changes the platform restore contract.
- Restore only the final exact body. This does not rebuild clean upstream feature artifacts needed to repair a failed downstream feature.
- Trust persisted output hashes without rebuilding artifacts. The next incremental edit would fail because its upstream kernel objects would be absent.

## Consequences

- Restart has an explicit, testable regeneration phase before document activation.
- Failed semantic heads can reopen honestly and later repair only their dirty dependent subgraph.
- Checkpoints remain kernel-neutral and do not serialize process-local state.
- Hydration cost grows with stored revision history. A future native package may cache compatible exact-body snapshots, but those snapshots must still be checked against the same evaluator and content hashes.
- Changes to evaluator or kernel identity require migration or deliberate regeneration rather than silently accepting incompatible cached state.
