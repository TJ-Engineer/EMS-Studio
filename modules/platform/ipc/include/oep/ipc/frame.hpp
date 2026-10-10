// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace oep::ipc {

inline constexpr std::uint32_t frame_protocol_version = 1;

struct Frame {
    std::uint32_t message_type{};
    std::uint64_t correlation_id{};
    std::vector<std::byte> payload;

    bool operator==(const Frame&) const = default;
};

struct FrameLimits {
    std::uint64_t maximum_payload_bytes{64ULL * 1024ULL * 1024ULL};
    std::uint32_t maximum_frames{4'096};
};

enum class FrameDiagnosticCode {
    malformed,
    unsupported_version,
    payload_limit_exceeded,
    frame_count_limit_exceeded,
    checksum_mismatch,
    trailing_data,
};

struct FrameDiagnostic {
    FrameDiagnosticCode code{FrameDiagnosticCode::malformed};
    std::string path;
    std::string message;
};

struct FrameDecodeResult {
    std::optional<Frame> frame;
    std::vector<FrameDiagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept { return frame.has_value(); }
};

struct FrameStreamDecodeResult {
    std::optional<std::vector<Frame>> frames;
    std::vector<FrameDiagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept { return frames.has_value(); }
};

[[nodiscard]] std::vector<std::byte> encode_frame(const Frame& frame);
[[nodiscard]] FrameDecodeResult decode_frame(
    std::span<const std::byte> encoded,
    const FrameLimits& limits = {});
[[nodiscard]] std::vector<std::byte> encode_frame_stream(
    std::span<const Frame> frames);
[[nodiscard]] FrameStreamDecodeResult decode_frame_stream(
    std::span<const std::byte> encoded,
    const FrameLimits& limits = {});

} // namespace oep::ipc
