// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

// Versioned binary IPC framing with correlation IDs, integrity checks, and
// bounded decoding. Checksums detect corruption; they do not authenticate a peer.
// Message-specific payload validation belongs to the consuming protocol.

#include <oep/ipc/frame.hpp>

#include <array>
#include <limits>
#include <string_view>
#include <utility>

namespace oep::ipc {
namespace {

constexpr std::string_view frame_magic = "OEPIPC01";
constexpr std::string_view stream_magic = "OEPSTR01";

[[nodiscard]] FrameDiagnostic issue(
    const FrameDiagnosticCode code,
    std::string path,
    std::string message)
{
    return FrameDiagnostic{
        .code = code,
        .path = std::move(path),
        .message = std::move(message),
    };
}

[[nodiscard]] std::uint64_t fnv1a(const std::span<const std::byte> bytes)
{
    auto hash = std::uint64_t{14'695'981'039'346'656'037ULL};
    for (const auto value : bytes) {
        hash ^= static_cast<std::uint64_t>(
            std::to_integer<unsigned int>(value));
        hash *= 1'099'511'628'211ULL;
    }
    return hash;
}

class Writer {
public:
    void u8(const std::uint8_t value)
    {
        bytes_.push_back(static_cast<std::byte>(value));
    }

    void u32(const std::uint32_t value)
    {
        for (unsigned int shift = 0; shift < 32; shift += 8) {
            u8(static_cast<std::uint8_t>((value >> shift) & 0xffU));
        }
    }

    void u64(const std::uint64_t value)
    {
        for (unsigned int shift = 0; shift < 64; shift += 8) {
            u8(static_cast<std::uint8_t>((value >> shift) & 0xffU));
        }
    }

    void text(const std::string_view value)
    {
        for (const auto character : value) {
            u8(static_cast<std::uint8_t>(character));
        }
    }

    void bytes(const std::span<const std::byte> value)
    {
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    [[nodiscard]] std::span<const std::byte> view() const noexcept
    {
        return bytes_;
    }

    [[nodiscard]] std::vector<std::byte> finish() &&
    {
        return std::move(bytes_);
    }

private:
    std::vector<std::byte> bytes_;
};

// Treat every encoded length as untrusted. Bounds checks belong before
// reading or allocating so malformed input cannot become unchecked indexing.
class Reader {
public:
    explicit Reader(const std::span<const std::byte> bytes)
        : bytes_(bytes)
    {
    }

    [[nodiscard]] bool require(const std::size_t count) const noexcept
    {
        return count <= bytes_.size() - position_;
    }

    [[nodiscard]] std::optional<std::uint8_t> u8()
    {
        if (!require(1)) {
            return std::nullopt;
        }
        return static_cast<std::uint8_t>(
            std::to_integer<unsigned int>(bytes_[position_++]));
    }

    [[nodiscard]] std::optional<std::uint32_t> u32()
    {
        auto value = std::uint32_t{};
        for (unsigned int shift = 0; shift < 32; shift += 8) {
            const auto byte = u8();
            if (!byte.has_value()) {
                return std::nullopt;
            }
            value |= static_cast<std::uint32_t>(*byte) << shift;
        }
        return value;
    }

    [[nodiscard]] std::optional<std::uint64_t> u64()
    {
        auto value = std::uint64_t{};
        for (unsigned int shift = 0; shift < 64; shift += 8) {
            const auto byte = u8();
            if (!byte.has_value()) {
                return std::nullopt;
            }
            value |= static_cast<std::uint64_t>(*byte) << shift;
        }
        return value;
    }

    [[nodiscard]] bool expect(const std::string_view expected)
    {
        for (const auto character : expected) {
            const auto byte = u8();
            if (!byte.has_value()
                || *byte != static_cast<std::uint8_t>(character)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::optional<std::vector<std::byte>> bytes(
        const std::uint64_t count)
    {
        if (count > static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max())) {
            return std::nullopt;
        }
        const auto size = static_cast<std::size_t>(count);
        if (!require(size)) {
            return std::nullopt;
        }
        std::vector<std::byte> result(
            bytes_.begin() + static_cast<std::ptrdiff_t>(position_),
            bytes_.begin() + static_cast<std::ptrdiff_t>(position_ + size));
        position_ += size;
        return result;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return position_ == bytes_.size();
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t position_{};
};

[[nodiscard]] bool valid_checksum(const std::span<const std::byte> encoded)
{
    if (encoded.size() < sizeof(std::uint64_t)) {
        return false;
    }
    auto reader = Reader(encoded.last(sizeof(std::uint64_t)));
    const auto expected = reader.u64();
    return expected.has_value()
        && fnv1a(encoded.first(encoded.size() - sizeof(std::uint64_t)))
            == *expected;
}

void finish(Writer& writer)
{
    writer.u64(fnv1a(writer.view()));
}

} // namespace

std::vector<std::byte> encode_frame(const Frame& frame)
{
    Writer writer;
    writer.text(frame_magic);
    writer.u32(frame_protocol_version);
    writer.u32(frame.message_type);
    writer.u64(frame.correlation_id);
    writer.u64(static_cast<std::uint64_t>(frame.payload.size()));
    writer.bytes(frame.payload);
    finish(writer);
    return std::move(writer).finish();
}

FrameDecodeResult decode_frame(
    const std::span<const std::byte> encoded,
    const FrameLimits& limits)
{
    FrameDecodeResult result;
    if (!valid_checksum(encoded)) {
        result.diagnostics.push_back(issue(
            FrameDiagnosticCode::checksum_mismatch,
            "frame",
            "frame is too short or its checksum does not match"));
        return result;
    }
    auto reader = Reader(encoded.first(encoded.size() - sizeof(std::uint64_t)));
    if (!reader.expect(frame_magic)) {
        result.diagnostics.push_back(issue(
            FrameDiagnosticCode::malformed,
            "frame.magic",
            "frame magic does not match"));
        return result;
    }
    const auto version = reader.u32();
    const auto message_type = reader.u32();
    const auto correlation_id = reader.u64();
    const auto payload_size = reader.u64();
    if (!version.has_value() || !message_type.has_value()
        || !correlation_id.has_value() || !payload_size.has_value()) {
        result.diagnostics.push_back(issue(
            FrameDiagnosticCode::malformed,
            "frame.header",
            "frame header is truncated"));
        return result;
    }
    if (*version != frame_protocol_version) {
        result.diagnostics.push_back(issue(
            FrameDiagnosticCode::unsupported_version,
            "frame.version",
            "frame protocol version is not supported"));
        return result;
    }
    if (*payload_size > limits.maximum_payload_bytes) {
        result.diagnostics.push_back(issue(
            FrameDiagnosticCode::payload_limit_exceeded,
            "frame.payload",
            "frame payload exceeds the configured decoding limit"));
        return result;
    }
    auto payload = reader.bytes(*payload_size);
    if (!payload.has_value()) {
        result.diagnostics.push_back(issue(
            FrameDiagnosticCode::malformed,
            "frame.payload",
            "frame payload is truncated"));
        return result;
    }
    if (!reader.empty()) {
        result.diagnostics.push_back(issue(
            FrameDiagnosticCode::trailing_data,
            "frame",
            "frame contains trailing payload data"));
        return result;
    }
    result.frame = Frame{
        .message_type = *message_type,
        .correlation_id = *correlation_id,
        .payload = std::move(*payload),
    };
    return result;
}

std::vector<std::byte> encode_frame_stream(const std::span<const Frame> frames)
{
    Writer writer;
    writer.text(stream_magic);
    writer.u32(frame_protocol_version);
    writer.u32(static_cast<std::uint32_t>(frames.size()));
    for (const auto& frame : frames) {
        const auto encoded = encode_frame(frame);
        writer.u64(static_cast<std::uint64_t>(encoded.size()));
        writer.bytes(encoded);
    }
    finish(writer);
    return std::move(writer).finish();
}

FrameStreamDecodeResult decode_frame_stream(
    const std::span<const std::byte> encoded,
    const FrameLimits& limits)
{
    FrameStreamDecodeResult result;
    if (!valid_checksum(encoded)) {
        result.diagnostics.push_back(issue(
            FrameDiagnosticCode::checksum_mismatch,
            "stream",
            "frame stream is too short or its checksum does not match"));
        return result;
    }
    auto reader = Reader(encoded.first(encoded.size() - sizeof(std::uint64_t)));
    if (!reader.expect(stream_magic)) {
        result.diagnostics.push_back(issue(
            FrameDiagnosticCode::malformed,
            "stream.magic",
            "frame-stream magic does not match"));
        return result;
    }
    const auto version = reader.u32();
    const auto count = reader.u32();
    if (!version.has_value() || !count.has_value()) {
        result.diagnostics.push_back(issue(
            FrameDiagnosticCode::malformed,
            "stream.header",
            "frame-stream header is truncated"));
        return result;
    }
    if (*version != frame_protocol_version) {
        result.diagnostics.push_back(issue(
            FrameDiagnosticCode::unsupported_version,
            "stream.version",
            "frame-stream protocol version is not supported"));
        return result;
    }
    if (*count > limits.maximum_frames) {
        result.diagnostics.push_back(issue(
            FrameDiagnosticCode::frame_count_limit_exceeded,
            "stream.frames",
            "frame count exceeds the configured decoding limit"));
        return result;
    }
    std::vector<Frame> frames;
    frames.reserve(*count);
    for (auto index = std::uint32_t{}; index < *count; ++index) {
        const auto frame_size = reader.u64();
        if (!frame_size.has_value()) {
            result.diagnostics.push_back(issue(
                FrameDiagnosticCode::malformed,
                "stream.frames[" + std::to_string(index) + "]",
                "frame size is missing"));
            return result;
        }
        auto frame_bytes = reader.bytes(*frame_size);
        if (!frame_bytes.has_value()) {
            result.diagnostics.push_back(issue(
                FrameDiagnosticCode::malformed,
                "stream.frames[" + std::to_string(index) + "]",
                "framed message is truncated"));
            return result;
        }
        auto decoded = decode_frame(*frame_bytes, limits);
        if (!decoded.succeeded()) {
            result.diagnostics = std::move(decoded.diagnostics);
            for (auto& diagnostic : result.diagnostics) {
                diagnostic.path = "stream.frames[" + std::to_string(index)
                    + "]." + diagnostic.path;
            }
            return result;
        }
        frames.push_back(std::move(*decoded.frame));
    }
    if (!reader.empty()) {
        result.diagnostics.push_back(issue(
            FrameDiagnosticCode::trailing_data,
            "stream",
            "frame stream contains trailing data"));
        return result;
    }
    result.frames = std::move(frames);
    return result;
}

} // namespace oep::ipc
