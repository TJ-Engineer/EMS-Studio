// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#include <oep/ipc/frame.hpp>
#include <oep/simulation_orchestrator/reference_worker.hpp>
#include <oep/simulation_orchestrator/worker_protocol.hpp>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace api = oep::em_solver_api;
namespace ipc = oep::ipc;
namespace orchestrator = oep::simulation_orchestrator;

void check(const bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        const auto stamp = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        path_ = std::filesystem::temp_directory_path()
            / ("oep-simulation-worker-process-" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

[[nodiscard]] api::PreparedSimulationJob job_fixture(std::string job_id)
{
    return api::PreparedSimulationJob{
        .job_id = std::move(job_id),
        .study_fingerprint = "asir-0.1-sha256:process-worker-fixture",
        .grid = api::StructuredGrid3dConfig{
            .cells_x = 6,
            .cells_y = 5,
            .cells_z = 4,
            .length_x_m = 0.006,
            .length_y_m = 0.005,
            .length_z_m = 0.004,
            .relative_permittivity = 1.0,
            .relative_permeability = 1.0,
            .courant_factor = 0.8,
        },
        .total_time_steps = 8,
        .resource_estimate = api::ResourceEstimateSnapshot{
            .grid_cells = 120,
            .time_steps = 8,
            .estimated_peak_memory_bytes = 64'000,
            .estimated_storage_bytes = 32'000,
            .runtime_upper_bound_s = 0.5,
            .calibration_hardware_id = "integration-test-host/single-thread",
            .calibration_solver_build_id = std::string(
                orchestrator::reference_worker_build_id),
            .within_budget = true,
            .requires_escalation = false,
        },
        .resource_approval_id = std::nullopt,
    };
}

void write_bytes(
    const std::filesystem::path& path,
    const std::span<const std::byte> bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    check(static_cast<bool>(output), "integration test request file must open");
    output.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    check(static_cast<bool>(output), "integration test request file must write");
}

[[nodiscard]] std::vector<std::byte> read_bytes(
    const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    check(static_cast<bool>(input), "integration test reply file must open");
    const auto size = input.tellg();
    check(size >= 0, "integration test reply size must be available");
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    check(static_cast<bool>(input) || bytes.empty(),
        "integration test reply file must read completely");
    return bytes;
}

[[nodiscard]] std::string shell_quote(const std::filesystem::path& path)
{
    const auto text = path.string();
    check(text.find('\'') == std::string::npos,
        "integration test paths must not contain a single quote");
    return "'" + text + "'";
}

[[nodiscard]] std::vector<orchestrator::WorkerReply> run_worker(
    const std::filesystem::path& executable,
    const std::filesystem::path& directory,
    const std::string& name,
    const std::span<const orchestrator::WorkerRequest> requests)
{
    std::vector<ipc::Frame> frames;
    frames.reserve(requests.size());
    for (const auto& request : requests) {
        frames.push_back(orchestrator::encode_worker_request(request));
    }
    const auto request_path = directory / (name + "-requests.bin");
    const auto reply_path = directory / (name + "-replies.bin");
    write_bytes(request_path, ipc::encode_frame_stream(frames));

    const auto command = shell_quote(executable) + " --batch "
        + shell_quote(request_path) + " " + shell_quote(reply_path);
    check(std::system(command.c_str()) == 0,
        "simulation worker subprocess must complete its framed batch");

    const auto stream = ipc::decode_frame_stream(read_bytes(reply_path));
    check(stream.succeeded(), "worker reply stream must pass framing checks");
    check(stream.frames->size() == requests.size(),
        "worker must return exactly one correlated reply per request");
    std::vector<orchestrator::WorkerReply> replies;
    replies.reserve(stream.frames->size());
    for (const auto& frame : *stream.frames) {
        auto reply = orchestrator::decode_worker_reply(frame);
        check(reply.succeeded(), "typed worker reply must decode");
        replies.push_back(std::move(*reply.reply));
    }
    return replies;
}

} // namespace

int main(const int argc, char** argv)
{
    check(argc == 2, "integration test requires the simulation-worker path");
    const std::filesystem::path worker_executable(argv[1]);
    TemporaryDirectory directory;

    const auto restart_job = job_fixture("process-restart-job");
    auto cancel_job = job_fixture("process-cancel-job");
    cancel_job.resource_estimate.within_budget = false;
    cancel_job.resource_estimate.requires_escalation = true;
    cancel_job.resource_approval_id = "integration-test-approval";
    const orchestrator::WorkerRequest first_requests[]{
        {1, orchestrator::SubmitCommand{restart_job}},
        {2, orchestrator::StatusCommand{restart_job.job_id}},
        {3, orchestrator::CheckpointCommand{restart_job.job_id}},
        {4, orchestrator::SubmitCommand{cancel_job}},
        {5, orchestrator::CancelCommand{cancel_job.job_id}},
        {6, orchestrator::CollectCommand{cancel_job.job_id}},
    };
    const auto first = run_worker(
        worker_executable, directory.path(), "before-restart", first_requests);
    check(first[0].succeeded() && first[0].state->phase == api::JobPhase::queued,
        "cross-process submit must return queued state");
    check(first[1].succeeded()
            && first[1].state->phase == api::JobPhase::running
            && first[1].state->completed_time_steps == 2,
        "cross-process status must report worker progress");
    check(first[2].succeeded() && first[2].checkpoint.has_value()
            && first[2].checkpoint->completed_time_steps == 4,
        "cross-process checkpoint must contain exact partial progress");
    check(first[4].succeeded()
            && first[4].state->phase == api::JobPhase::cancelled,
        "cross-process cancellation must reach terminal cancelled state");
    check(!first[5].succeeded()
            && first[5].diagnostics.front().code
                == api::WorkerDiagnosticCode::invalid_transition,
        "cancelled worker job must not leak successful artifacts");

    const auto partial_checkpoint = *first[2].checkpoint;
    const orchestrator::WorkerRequest second_requests[]{
        {7, orchestrator::ResumeCommand{restart_job, partial_checkpoint}},
        {8, orchestrator::StatusCommand{restart_job.job_id}},
        {9, orchestrator::StatusCommand{restart_job.job_id}},
        {10, orchestrator::CollectCommand{restart_job.job_id}},
    };
    const auto second = run_worker(
        worker_executable, directory.path(), "after-restart", second_requests);
    check(second[0].succeeded()
            && second[0].state->phase == api::JobPhase::running
            && second[0].state->completed_time_steps == 4,
        "new worker process must resume exact checkpoint progress");
    check(second[1].succeeded()
            && second[1].state->completed_time_steps == 6,
        "resumed worker process must continue from checkpoint");
    check(second[2].succeeded()
            && second[2].state->phase == api::JobPhase::succeeded
            && second[2].state->completed_time_steps == 8,
        "resumed job must reach successful terminal state");
    check(second[3].succeeded() && second[3].artifacts.has_value(),
        "successful resumed job must expose raw artifacts across IPC");
    check(second[3].artifacts->study_fingerprint
            == restart_job.study_fingerprint
            && second[3].artifacts->completed_time_steps == 8
            && !second[3].artifacts->final_solver_checkpoint.empty(),
        "cross-process artifacts must retain study, progress, and field checkpoint provenance");

    std::cout << "Simulation worker process IPC, cancellation, checkpoint restart, and artifact checks passed\n";
    return EXIT_SUCCESS;
}
