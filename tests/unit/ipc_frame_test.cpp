// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#include <oep/ipc/frame.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

namespace ipc = oep::ipc;

void check(const bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

[[nodiscard]] std::vector<std::byte> payload(const std::string& text)
{
    std::vector<std::byte> result;
    result.reserve(text.size());
    for (const auto character : text) {
        result.push_back(static_cast<std::byte>(character));
    }
    return result;
}

void frame_and_stream_round_trip()
{
    const ipc::Frame first{
        .message_type = 101,
        .correlation_id = 41,
        .payload = payload("submit-job"),
    };
    const ipc::Frame second{
        .message_type = 104,
        .correlation_id = 42,
        .payload = payload("checkpoint-job"),
    };
    const auto decoded = ipc::decode_frame(ipc::encode_frame(first));
    check(decoded.succeeded() && *decoded.frame == first,
        "versioned frame must round-trip exactly");

    const ipc::Frame frames[]{first, second};
    const auto stream = ipc::decode_frame_stream(ipc::encode_frame_stream(frames));
    check(stream.succeeded(), "bounded frame stream must decode");
    check(*stream.frames == std::vector<ipc::Frame>({first, second}),
        "frame stream must preserve order, type, correlation, and payload");
}

void corruption_bounds_and_trailing_data_are_rejected()
{
    const ipc::Frame frame{
        .message_type = 7,
        .correlation_id = 99,
        .payload = payload("bounded-payload"),
    };
    auto corrupt = ipc::encode_frame(frame);
    corrupt[12] ^= std::byte{0x01};
    const auto bad_checksum = ipc::decode_frame(corrupt);
    check(!bad_checksum.succeeded(), "corrupted frame must fail");
    check(bad_checksum.diagnostics.front().code
            == ipc::FrameDiagnosticCode::checksum_mismatch,
        "corrupted frame must retain checksum diagnostic");

    const auto limited = ipc::decode_frame(
        ipc::encode_frame(frame),
        ipc::FrameLimits{.maximum_payload_bytes = 2, .maximum_frames = 1});
    check(!limited.succeeded(), "oversized payload must fail before allocation");
    check(limited.diagnostics.front().code
            == ipc::FrameDiagnosticCode::payload_limit_exceeded,
        "payload bound failure must remain typed");

    const ipc::Frame frames[]{frame, frame};
    const auto too_many = ipc::decode_frame_stream(
        ipc::encode_frame_stream(frames),
        ipc::FrameLimits{.maximum_payload_bytes = 1'024, .maximum_frames = 1});
    check(!too_many.succeeded(), "frame-count limit must be enforced");
    check(too_many.diagnostics.front().code
            == ipc::FrameDiagnosticCode::frame_count_limit_exceeded,
        "frame-count limit failure must remain typed");

    auto truncated = ipc::encode_frame(frame);
    truncated.pop_back();
    check(!ipc::decode_frame(truncated).succeeded(),
        "truncated checksum must fail closed");
}

} // namespace

int main()
{
    frame_and_stream_round_trip();
    corruption_bounds_and_trailing_data_are_rejected();
    std::cout << "Versioned bounded IPC frame and stream checks passed\n";
    return EXIT_SUCCESS;
}
