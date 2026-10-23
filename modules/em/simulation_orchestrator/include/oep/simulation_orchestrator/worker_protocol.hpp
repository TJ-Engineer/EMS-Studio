// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

// Encodes solver-neutral commands and replies inside correlated IPC frames.
// Payload decoding has its own text, collection, and blob bounds in addition
// to outer frame limits; neither layer should trust lengths from the other.

#pragma once

#include <oep/em_solver_api/solver_api.hpp>
#include <oep/ipc/frame.hpp>
#include <oep/simulation_orchestrator/reference_worker.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace oep::simulation_orchestrator {

enum class WorkerOperation : std::uint32_t {
    submit = 1,
    resume = 2,
    status = 3,
    cancel = 4,
    checkpoint = 5,
    collect = 6,
};

struct SubmitCommand {
    em_solver_api::PreparedSimulationJob job;
};

struct ResumeCommand {
    em_solver_api::PreparedSimulationJob job;
    em_solver_api::CheckpointArtifact checkpoint;
};

struct StatusCommand {
    std::string job_id;
};

struct CancelCommand {
    std::string job_id;
};

struct CheckpointCommand {
    std::string job_id;
};

struct CollectCommand {
    std::string job_id;
};

using WorkerCommand = std::variant<
    SubmitCommand,
    ResumeCommand,
    StatusCommand,
    CancelCommand,
    CheckpointCommand,
    CollectCommand>;

struct WorkerRequest {
    std::uint64_t correlation_id{};
    WorkerCommand command;
};

struct WorkerReply {
    std::uint64_t correlation_id{};
    WorkerOperation operation{WorkerOperation::status};
    std::optional<em_solver_api::JobState> state;
    std::optional<em_solver_api::CheckpointArtifact> checkpoint;
    std::optional<em_solver_api::RawArtifacts> artifacts;
    std::vector<em_solver_api::WorkerDiagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept
    {
        return diagnostics.empty();
    }
};

struct WorkerRequestDecodeResult {
    std::optional<WorkerRequest> request;
    std::vector<em_solver_api::WorkerDiagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept { return request.has_value(); }
};

struct WorkerReplyDecodeResult {
    std::optional<WorkerReply> reply;
    std::vector<em_solver_api::WorkerDiagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept { return reply.has_value(); }
};

// Durable desktop recovery stores the same job/checkpoint pair used by the
// versioned resume command. The outer project file remains responsible for CAD
// and ASIR identity; this nested record protects worker payload integrity and
// preserves the solver-neutral process contract.
struct WorkerRecoveryRecord {
    em_solver_api::PreparedSimulationJob job;
    em_solver_api::CheckpointArtifact checkpoint;

    bool operator==(const WorkerRecoveryRecord&) const = default;
};

struct WorkerRecoveryDecodeResult {
    std::optional<WorkerRecoveryRecord> recovery;
    std::vector<em_solver_api::WorkerDiagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept
    {
        return recovery.has_value();
    }
};

[[nodiscard]] ipc::Frame encode_worker_request(const WorkerRequest& request);
[[nodiscard]] WorkerRequestDecodeResult decode_worker_request(
    const ipc::Frame& frame);
[[nodiscard]] ipc::Frame encode_worker_reply(const WorkerReply& reply);
[[nodiscard]] WorkerReplyDecodeResult decode_worker_reply(const ipc::Frame& frame);
[[nodiscard]] std::vector<std::byte> encode_worker_recovery(
    const WorkerRecoveryRecord& recovery);
[[nodiscard]] WorkerRecoveryDecodeResult decode_worker_recovery(
    std::span<const std::byte> encoded);

class WorkerProtocolSession {
public:
    WorkerProtocolSession(std::string worker_id, std::string solver_build_id);

    [[nodiscard]] WorkerReply handle(const WorkerRequest& request);
    void advance_all(std::uint64_t maximum_time_steps_per_job);

private:
    ReferenceSimulationWorker worker_;
};

} // namespace oep::simulation_orchestrator
