# Versioned local IPC framing

The compiled `oep::ipc` module provides the transport-neutral envelope used by local worker protocols. A frame carries a protocol version, message type, correlation ID, payload length, payload, and checksum. A frame stream packages an ordered request or reply batch with its own version, frame count, and checksum.

Decoding is fail-closed. Callers receive typed diagnostics for bad magic, unsupported versions, checksum failures, truncated or trailing data, payload limits, and frame-count limits. The default decoder accepts at most 64 MiB per payload and 4,096 frames; a caller can lower both limits. Correlation IDs and message types are preserved without interpretation so each domain protocol owns and independently versions its command semantics.

The current simulation worker uses file-backed frame streams as the first real process seam. The envelope does not require files: a future pipe, local socket, or platform IPC adapter can carry the same encoded frames.

## Deliberate limits

- FNV-1a detects accidental corruption; it is not an authentication mechanism. A remote or mutually untrusted transport requires authenticated framing.
- Stream encoding is an in-memory batch format. Incremental I/O and shared-memory payload references remain future transport work.
- Flow control, retries, authorization, worker discovery, and job persistence belong above this module.
