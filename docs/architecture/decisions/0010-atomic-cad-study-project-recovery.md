# ADR 0010: Atomic CAD and ASIR project recovery

**Status:** Accepted  
**Date:** October 12, 2026

## Context

CAD-only checkpoints preserve revision history but discard authored EM setup. Separately saving a study risks combining it with the wrong document or rewriting stale authority on reopening. Drafts are often incomplete and may legitimately reference an earlier evaluated revision after a later CAD failure.

## Decision

Add an internal compound project checkpoint containing the CAD document recovery bytes, a persistent document-session ID, and an optional complete ASIR 0.1 value. Keep the existing CAD-only APIs and File actions. New **Save/Open CAD + EM project** actions explicitly opt into compound recovery.

The original format uses `oep-project-checkpoint-1` inside a versioned, checksummed generic IPC frame with a dedicated project message type and zero correlation ID. Explicit field tuples encode every ASIR member independently of C++ layout. Integer/double bit patterns use little-endian 64-bit storage; strings, vectors, arrays, optionals, enums, and booleans have bounded/validated decoding. A stored canonical study fingerprint verifies content identity. Project-checkpoint v2 appends the optional opaque worker record defined by [ADR 0013](0013-atomic-worker-recovery-in-project-checkpoint.md); the reader remains compatible with v1.

The reader limits payloads to 64 MiB, individual text to 1 MiB, and aggregate non-blob collection entries to 100,000. CAD data retains its own checkpoint bounds and checksum. Nonfinite numeric data, invalid enum/boolean values, unsupported versions, trailing bytes, and nested allocation bounds fail before activation. The study document ID must match the compound ID, and its semantic/evaluated snapshot must be represented in the bundled CAD history with a matching evaluated geometry hash.

Recovery is not full ASIR preflight. Missing setup, unsupported execution policy, and stale/failed current CAD remain reviewable rather than being discarded. Loading preserves the snapshot and canonical fingerprint, validates document history, hydrates a separate exact-kernel session, and activates only after all gates pass. It restores the persisted document identity after attachment, which otherwise establishes a new session identity.

Reuse the platform's durable sibling-file replacement through `save_checkpoint_bytes_atomic`. No Qt serialization types enter the recovery format. The recovery helper in `simulation_ir` depends on platform persistence and IPC; the platform document/persistence modules remain independent of EM and Qt.

## Alternatives

- Save only the study: cannot atomically preserve the matching CAD history and failed-state recovery path.
- Require globally valid ASIR at save time: would lose incremental work and make failed CAD impossible to recover with its draft.
- Rebuild the study automatically on load: would change authority and reset authored definitions.
- Serialize only visible editor fields: would drop optional fields, goals, provenance, and policies supported by the ASIR contract.
- Introduce the public native package now: requires a broader schema/migration contract beyond internal recovery.

## Consequences and evidence

Compound recovery explicitly persists authored setup; CAD-only saves still omit it. Version 2 also permits an opaque, authority-bound worker restart record under [ADR 0013](0013-atomic-worker-recovery-in-project-checkpoint.md). Unsaved editor text, Qt layout/section expansion, live kernel handles, collected result databases, and execution permissions are not stored. There is no autosave or multi-document transaction.

[ASIR tests](../../../tests/unit/simulation_ir_test.cpp) verify complete content round trip, optional fields, absent study, failed CAD history/Undo, integrity/version/trailing-data rejection, nested bounds with a valid checksum, and identity mismatch rejection. [Controller tests](../../../tests/unit/app_shell_state_test.cpp) reopen a failed CAD/study in a fresh session, restore edit authority through Undo, reject corrupt replacement, and report save I/O failure. [Offscreen UI acceptance](../../../modules/platform/app_shell/src/application_qt.cpp) exercises the actual save/open actions and popup.

Any field-order change requires a recovery version and migration or deliberate rejection. This remains an internal recovery format, not a release-stable public native document package.
