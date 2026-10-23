// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

// Internal document recovery format with bounded, checksummed decoding and
// atomic file replacement. Decoding reconstructs value state; Document::restore
// validates semantic history, and CAD evaluators hydrate exact artifacts separately.

#pragma once

#include <oep/document/document.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <vector>

namespace oep::persistence {

// Receiver policy for untrusted/corrupt files. Payload, collection, and
// string caps constrain different allocations and must all remain enforced.
struct CheckpointDecodeLimits {
    std::uint64_t max_payload_bytes{64U * 1024U * 1024U};
    std::uint64_t max_collection_entries{1'000'000U};
    std::uint64_t max_string_bytes{1U * 1024U * 1024U};
};

class CheckpointFormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// This is an internal recovery-checkpoint format. It is not the public native
// document package or a replacement for the planned versioned schema contract.
[[nodiscard]] std::vector<std::byte> encode_document_checkpoint(
    const document::DocumentState& state);

[[nodiscard]] document::DocumentState decode_document_checkpoint(
    std::span<const std::byte> encoded,
    const CheckpointDecodeLimits& limits = {});

// Reuse the durable sibling-file replacement for versioned compound checkpoints.
void save_checkpoint_bytes_atomic(const std::filesystem::path& path, std::span<const std::byte> bytes);

void save_document_checkpoint_atomic(
    const std::filesystem::path& path,
    const document::DocumentState& state);

[[nodiscard]] document::DocumentState load_document_checkpoint(
    const std::filesystem::path& path,
    const CheckpointDecodeLimits& limits = {});

} // namespace oep::persistence
