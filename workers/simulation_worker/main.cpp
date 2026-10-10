// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#include <oep/ipc/frame.hpp>
#include <oep/simulation_orchestrator/reference_worker.hpp>
#include <oep/simulation_orchestrator/worker_protocol.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint64_t maximum_batch_bytes = 256ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t execution_quantum_steps = 2;

[[nodiscard]] std::vector<std::byte> read_file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("cannot open worker request batch");
    }
    const auto end = input.tellg();
    if (end < 0 || static_cast<std::uint64_t>(end) > maximum_batch_bytes
        || static_cast<std::uint64_t>(end)
            > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("worker request batch exceeds its size limit");
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!input && !bytes.empty()) {
        throw std::runtime_error("cannot read complete worker request batch");
    }
    return bytes;
}

void write_file(
    const std::filesystem::path& path,
    const std::span<const std::byte> bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot open worker reply batch");
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    output.flush();
    if (!output) {
        throw std::runtime_error("cannot write complete worker reply batch");
    }
}

int run_batch(
    const std::filesystem::path& request_path,
    const std::filesystem::path& reply_path)
{
    namespace ipc = oep::ipc;
    namespace orchestrator = oep::simulation_orchestrator;

    const auto request_bytes = read_file(request_path);
    const auto decoded = ipc::decode_frame_stream(request_bytes);
    if (!decoded.succeeded()) {
        std::cerr << "simulation-worker: invalid framed request batch\n";
        return 2;
    }

    orchestrator::WorkerProtocolSession session(
        "local-simulation-worker",
        std::string(orchestrator::reference_worker_build_id));
    std::vector<ipc::Frame> replies;
    replies.reserve(decoded.frames->size());
    for (const auto& frame : *decoded.frames) {
        auto request = orchestrator::decode_worker_request(frame);
        if (!request.succeeded()) {
            std::cerr << "simulation-worker: malformed typed worker request\n";
            return 3;
        }
        replies.push_back(orchestrator::encode_worker_reply(
            session.handle(*request.request)));
        session.advance_all(execution_quantum_steps);
    }
    write_file(reply_path, ipc::encode_frame_stream(replies));
    return 0;
}

} // namespace

int main(const int argc, char** argv)
{
    if (argc == 4 && std::string(argv[1]) == "--batch") {
        try {
            return run_batch(argv[2], argv[3]);
        } catch (const std::exception& exception) {
            std::cerr << "simulation-worker: " << exception.what() << '\n';
            return 4;
        }
    }
    if (argc != 1) {
        std::cerr << "usage: simulation-worker [--batch request-file reply-file]\n";
        return 1;
    }
    std::cout << "Open Engineering Platform simulation worker " << OEP_VERSION << '\n';
    return 0;
}
