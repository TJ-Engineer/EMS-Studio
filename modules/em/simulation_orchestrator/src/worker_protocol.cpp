// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#include <oep/simulation_orchestrator/worker_protocol.hpp>

#include <bit>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace oep::simulation_orchestrator {
namespace {

namespace api = em_solver_api;

constexpr std::uint32_t request_type_base = 0x100U;
constexpr std::uint32_t reply_type_base = 0x200U;
constexpr std::uint32_t maximum_text_bytes = 1U * 1024U * 1024U;
constexpr std::uint64_t maximum_blob_bytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t maximum_diagnostics = 4'096;

[[nodiscard]] api::WorkerDiagnostic protocol_issue(
    std::string path,
    std::string message)
{
    return api::WorkerDiagnostic{
        .code = api::WorkerDiagnosticCode::protocol_error,
        .path = std::move(path),
        .message = std::move(message),
    };
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

    void f64(const double value)
    {
        u64(std::bit_cast<std::uint64_t>(value));
    }

    void boolean(const bool value)
    {
        u8(value ? 1U : 0U);
    }

    void text(const std::string_view value)
    {
        u32(static_cast<std::uint32_t>(value.size()));
        for (const auto character : value) {
            u8(static_cast<std::uint8_t>(character));
        }
    }

    void optional_text(const std::optional<std::string>& value)
    {
        boolean(value.has_value());
        if (value.has_value()) {
            text(*value);
        }
    }

    void blob(const std::span<const std::byte> value)
    {
        u64(static_cast<std::uint64_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    [[nodiscard]] std::vector<std::byte> finish() &&
    {
        return std::move(bytes_);
    }

private:
    std::vector<std::byte> bytes_;
};

class Reader {
public:
    explicit Reader(const std::span<const std::byte> bytes)
        : bytes_(bytes)
    {
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

    [[nodiscard]] std::optional<double> f64()
    {
        const auto encoded = u64();
        if (!encoded.has_value()) {
            return std::nullopt;
        }
        return std::bit_cast<double>(*encoded);
    }

    [[nodiscard]] std::optional<bool> boolean()
    {
        const auto encoded = u8();
        if (!encoded.has_value() || *encoded > 1U) {
            return std::nullopt;
        }
        return *encoded == 1U;
    }

    [[nodiscard]] std::optional<std::string> text()
    {
        const auto size = u32();
        if (!size.has_value() || *size > maximum_text_bytes
            || !require(static_cast<std::size_t>(*size))) {
            return std::nullopt;
        }
        std::string value;
        value.reserve(*size);
        for (auto index = std::uint32_t{}; index < *size; ++index) {
            value.push_back(static_cast<char>(
                std::to_integer<unsigned int>(bytes_[position_++])));
        }
        return value;
    }

    [[nodiscard]] std::optional<std::optional<std::string>> optional_text()
    {
        const auto present = boolean();
        if (!present.has_value()) {
            return std::nullopt;
        }
        if (!*present) {
            return std::optional<std::optional<std::string>>{
                std::in_place,
                std::nullopt,
            };
        }
        const auto value = text();
        if (!value.has_value()) {
            return std::nullopt;
        }
        return std::optional<std::string>{std::move(*value)};
    }

    [[nodiscard]] std::optional<std::vector<std::byte>> blob()
    {
        const auto encoded_size = u64();
        if (!encoded_size.has_value() || *encoded_size > maximum_blob_bytes
            || *encoded_size > static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max())) {
            return std::nullopt;
        }
        const auto size = static_cast<std::size_t>(*encoded_size);
        if (!require(size)) {
            return std::nullopt;
        }
        std::vector<std::byte> value(
            bytes_.begin() + static_cast<std::ptrdiff_t>(position_),
            bytes_.begin() + static_cast<std::ptrdiff_t>(position_ + size));
        position_ += size;
        return value;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return position_ == bytes_.size();
    }

private:
    [[nodiscard]] bool require(const std::size_t count) const noexcept
    {
        return count <= bytes_.size() - position_;
    }

    std::span<const std::byte> bytes_;
    std::size_t position_{};
};

void write_grid(Writer& writer, const api::StructuredGrid3dConfig& grid)
{
    writer.u64(grid.cells_x);
    writer.u64(grid.cells_y);
    writer.u64(grid.cells_z);
    writer.f64(grid.length_x_m);
    writer.f64(grid.length_y_m);
    writer.f64(grid.length_z_m);
    writer.f64(grid.relative_permittivity);
    writer.f64(grid.relative_permeability);
    writer.f64(grid.courant_factor);
}

[[nodiscard]] std::optional<api::StructuredGrid3dConfig> read_grid(Reader& reader)
{
    const auto cells_x = reader.u64();
    const auto cells_y = reader.u64();
    const auto cells_z = reader.u64();
    const auto length_x = reader.f64();
    const auto length_y = reader.f64();
    const auto length_z = reader.f64();
    const auto permittivity = reader.f64();
    const auto permeability = reader.f64();
    const auto courant = reader.f64();
    if (!cells_x.has_value() || !cells_y.has_value() || !cells_z.has_value()
        || !length_x.has_value() || !length_y.has_value() || !length_z.has_value()
        || !permittivity.has_value() || !permeability.has_value()
        || !courant.has_value()) {
        return std::nullopt;
    }
    return api::StructuredGrid3dConfig{
        .cells_x = *cells_x,
        .cells_y = *cells_y,
        .cells_z = *cells_z,
        .length_x_m = *length_x,
        .length_y_m = *length_y,
        .length_z_m = *length_z,
        .relative_permittivity = *permittivity,
        .relative_permeability = *permeability,
        .courant_factor = *courant,
    };
}

void write_estimate(Writer& writer, const api::ResourceEstimateSnapshot& estimate)
{
    writer.u64(estimate.grid_cells);
    writer.u64(estimate.time_steps);
    writer.u64(estimate.estimated_peak_memory_bytes);
    writer.u64(estimate.estimated_storage_bytes);
    writer.f64(estimate.runtime_upper_bound_s);
    writer.text(estimate.calibration_hardware_id);
    writer.text(estimate.calibration_solver_build_id);
    writer.boolean(estimate.within_budget);
    writer.boolean(estimate.requires_escalation);
}

[[nodiscard]] std::optional<api::ResourceEstimateSnapshot> read_estimate(
    Reader& reader)
{
    const auto cells = reader.u64();
    const auto steps = reader.u64();
    const auto memory = reader.u64();
    const auto storage = reader.u64();
    const auto runtime = reader.f64();
    auto hardware = reader.text();
    auto build = reader.text();
    const auto within = reader.boolean();
    const auto escalation = reader.boolean();
    if (!cells.has_value() || !steps.has_value() || !memory.has_value()
        || !storage.has_value() || !runtime.has_value() || !hardware.has_value()
        || !build.has_value() || !within.has_value() || !escalation.has_value()) {
        return std::nullopt;
    }
    return api::ResourceEstimateSnapshot{
        .grid_cells = *cells,
        .time_steps = *steps,
        .estimated_peak_memory_bytes = *memory,
        .estimated_storage_bytes = *storage,
        .runtime_upper_bound_s = *runtime,
        .calibration_hardware_id = std::move(*hardware),
        .calibration_solver_build_id = std::move(*build),
        .within_budget = *within,
        .requires_escalation = *escalation,
    };
}

void write_job(Writer& writer, const api::PreparedSimulationJob& job)
{
    writer.text(job.job_id);
    writer.text(job.study_fingerprint);
    write_grid(writer, job.grid);
    writer.u64(job.total_time_steps);
    write_estimate(writer, job.resource_estimate);
    writer.optional_text(job.resource_approval_id);
}

[[nodiscard]] std::optional<api::PreparedSimulationJob> read_job(Reader& reader)
{
    auto job_id = reader.text();
    auto fingerprint = reader.text();
    auto grid = read_grid(reader);
    const auto total_steps = reader.u64();
    auto estimate = read_estimate(reader);
    auto approval = reader.optional_text();
    if (!job_id.has_value() || !fingerprint.has_value() || !grid.has_value()
        || !total_steps.has_value() || !estimate.has_value()
        || !approval.has_value()) {
        return std::nullopt;
    }
    return api::PreparedSimulationJob{
        .job_id = std::move(*job_id),
        .study_fingerprint = std::move(*fingerprint),
        .grid = *grid,
        .total_time_steps = *total_steps,
        .resource_estimate = std::move(*estimate),
        .resource_approval_id = std::move(*approval),
    };
}

void write_state(Writer& writer, const api::JobState& state)
{
    writer.text(state.job_id);
    writer.text(state.study_fingerprint);
    writer.u32(static_cast<std::uint32_t>(state.phase));
    writer.u64(state.completed_time_steps);
    writer.u64(state.total_time_steps);
    writer.f64(state.progress);
    writer.text(state.worker_id);
    writer.text(state.solver_build_id);
}

[[nodiscard]] std::optional<api::JobState> read_state(Reader& reader)
{
    auto job_id = reader.text();
    auto fingerprint = reader.text();
    const auto phase = reader.u32();
    const auto completed = reader.u64();
    const auto total = reader.u64();
    const auto progress = reader.f64();
    auto worker = reader.text();
    auto build = reader.text();
    if (!job_id.has_value() || !fingerprint.has_value() || !phase.has_value()
        || *phase > static_cast<std::uint32_t>(api::JobPhase::failed)
        || !completed.has_value() || !total.has_value() || !progress.has_value()
        || !worker.has_value() || !build.has_value()) {
        return std::nullopt;
    }
    return api::JobState{
        .job_id = std::move(*job_id),
        .study_fingerprint = std::move(*fingerprint),
        .phase = static_cast<api::JobPhase>(*phase),
        .completed_time_steps = *completed,
        .total_time_steps = *total,
        .progress = *progress,
        .worker_id = std::move(*worker),
        .solver_build_id = std::move(*build),
    };
}

void write_checkpoint(Writer& writer, const api::CheckpointArtifact& checkpoint)
{
    writer.text(checkpoint.checkpoint_id);
    writer.text(checkpoint.job_id);
    writer.text(checkpoint.study_fingerprint);
    writer.text(checkpoint.solver_build_id);
    writer.u64(checkpoint.completed_time_steps);
    writer.blob(checkpoint.payload);
}

[[nodiscard]] std::optional<api::CheckpointArtifact> read_checkpoint(Reader& reader)
{
    auto checkpoint_id = reader.text();
    auto job_id = reader.text();
    auto fingerprint = reader.text();
    auto build = reader.text();
    const auto steps = reader.u64();
    auto payload = reader.blob();
    if (!checkpoint_id.has_value() || !job_id.has_value()
        || !fingerprint.has_value() || !build.has_value() || !steps.has_value()
        || !payload.has_value()) {
        return std::nullopt;
    }
    return api::CheckpointArtifact{
        .checkpoint_id = std::move(*checkpoint_id),
        .job_id = std::move(*job_id),
        .study_fingerprint = std::move(*fingerprint),
        .solver_build_id = std::move(*build),
        .completed_time_steps = *steps,
        .payload = std::move(*payload),
    };
}

void write_artifacts(Writer& writer, const api::RawArtifacts& artifacts)
{
    writer.text(artifacts.artifact_id);
    writer.text(artifacts.job_id);
    writer.text(artifacts.study_fingerprint);
    writer.text(artifacts.solver_build_id);
    writer.u64(artifacts.completed_time_steps);
    writer.blob(artifacts.final_solver_checkpoint);
}

[[nodiscard]] std::optional<api::RawArtifacts> read_artifacts(Reader& reader)
{
    auto artifact_id = reader.text();
    auto job_id = reader.text();
    auto fingerprint = reader.text();
    auto build = reader.text();
    const auto steps = reader.u64();
    auto payload = reader.blob();
    if (!artifact_id.has_value() || !job_id.has_value()
        || !fingerprint.has_value() || !build.has_value() || !steps.has_value()
        || !payload.has_value()) {
        return std::nullopt;
    }
    return api::RawArtifacts{
        .artifact_id = std::move(*artifact_id),
        .job_id = std::move(*job_id),
        .study_fingerprint = std::move(*fingerprint),
        .solver_build_id = std::move(*build),
        .completed_time_steps = *steps,
        .final_solver_checkpoint = std::move(*payload),
    };
}

void write_diagnostics(
    Writer& writer,
    const std::span<const api::WorkerDiagnostic> diagnostics)
{
    writer.u32(static_cast<std::uint32_t>(diagnostics.size()));
    for (const auto& diagnostic : diagnostics) {
        writer.u32(static_cast<std::uint32_t>(diagnostic.code));
        writer.text(diagnostic.path);
        writer.text(diagnostic.message);
    }
}

[[nodiscard]] std::optional<std::vector<api::WorkerDiagnostic>> read_diagnostics(
    Reader& reader)
{
    const auto count = reader.u32();
    if (!count.has_value() || *count > maximum_diagnostics) {
        return std::nullopt;
    }
    std::vector<api::WorkerDiagnostic> diagnostics;
    diagnostics.reserve(*count);
    for (auto index = std::uint32_t{}; index < *count; ++index) {
        const auto code = reader.u32();
        auto path = reader.text();
        auto message = reader.text();
        if (!code.has_value()
            || *code > static_cast<std::uint32_t>(
                api::WorkerDiagnosticCode::protocol_error)
            || !path.has_value() || !message.has_value()) {
            return std::nullopt;
        }
        diagnostics.push_back(api::WorkerDiagnostic{
            .code = static_cast<api::WorkerDiagnosticCode>(*code),
            .path = std::move(*path),
            .message = std::move(*message),
        });
    }
    return diagnostics;
}

[[nodiscard]] std::optional<WorkerOperation> operation_from_type(
    const std::uint32_t message_type,
    const std::uint32_t base)
{
    if (message_type <= base) {
        return std::nullopt;
    }
    const auto value = message_type - base;
    if (value < static_cast<std::uint32_t>(WorkerOperation::submit)
        || value > static_cast<std::uint32_t>(WorkerOperation::collect)) {
        return std::nullopt;
    }
    return static_cast<WorkerOperation>(value);
}

[[nodiscard]] WorkerOperation operation_for(const WorkerCommand& command)
{
    return std::visit([](const auto& value) {
        using Command = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Command, SubmitCommand>) {
            return WorkerOperation::submit;
        } else if constexpr (std::is_same_v<Command, ResumeCommand>) {
            return WorkerOperation::resume;
        } else if constexpr (std::is_same_v<Command, StatusCommand>) {
            return WorkerOperation::status;
        } else if constexpr (std::is_same_v<Command, CancelCommand>) {
            return WorkerOperation::cancel;
        } else if constexpr (std::is_same_v<Command, CheckpointCommand>) {
            return WorkerOperation::checkpoint;
        } else {
            return WorkerOperation::collect;
        }
    }, command);
}

[[nodiscard]] WorkerReply state_reply(
    const std::uint64_t correlation,
    const WorkerOperation operation,
    api::StateResult result)
{
    return WorkerReply{
        .correlation_id = correlation,
        .operation = operation,
        .state = std::move(result.state),
        .checkpoint = std::nullopt,
        .artifacts = std::nullopt,
        .diagnostics = std::move(result.diagnostics),
    };
}

[[nodiscard]] bool read_contract_version(Reader& reader)
{
    const auto version = reader.text();
    return version.has_value() && *version == api::worker_contract_version;
}

} // namespace

ipc::Frame encode_worker_request(const WorkerRequest& request)
{
    Writer writer;
    writer.text(api::worker_contract_version);
    std::visit([&writer](const auto& command) {
        using Command = std::decay_t<decltype(command)>;
        if constexpr (std::is_same_v<Command, SubmitCommand>) {
            write_job(writer, command.job);
        } else if constexpr (std::is_same_v<Command, ResumeCommand>) {
            write_job(writer, command.job);
            write_checkpoint(writer, command.checkpoint);
        } else {
            writer.text(command.job_id);
        }
    }, request.command);
    return ipc::Frame{
        .message_type = request_type_base
            + static_cast<std::uint32_t>(operation_for(request.command)),
        .correlation_id = request.correlation_id,
        .payload = std::move(writer).finish(),
    };
}

WorkerRequestDecodeResult decode_worker_request(const ipc::Frame& frame)
{
    const auto operation = operation_from_type(frame.message_type, request_type_base);
    if (!operation.has_value()) {
        return WorkerRequestDecodeResult{
            .request = std::nullopt,
            .diagnostics = {protocol_issue(
                "frame.message_type", "message is not a known worker request")},
        };
    }
    Reader reader(frame.payload);
    if (!read_contract_version(reader)) {
        return WorkerRequestDecodeResult{
            .request = std::nullopt,
            .diagnostics = {protocol_issue(
                "frame.payload.contract_version",
                "worker contract version is missing or unsupported")},
        };
    }
    std::optional<WorkerCommand> command;
    if (*operation == WorkerOperation::submit) {
        auto job = read_job(reader);
        if (job.has_value()) {
            command = SubmitCommand{.job = std::move(*job)};
        }
    } else if (*operation == WorkerOperation::resume) {
        auto job = read_job(reader);
        auto checkpoint = read_checkpoint(reader);
        if (job.has_value() && checkpoint.has_value()) {
            command = ResumeCommand{
                .job = std::move(*job),
                .checkpoint = std::move(*checkpoint),
            };
        }
    } else {
        auto job_id = reader.text();
        if (job_id.has_value()) {
            switch (*operation) {
            case WorkerOperation::status:
                command = StatusCommand{.job_id = std::move(*job_id)};
                break;
            case WorkerOperation::cancel:
                command = CancelCommand{.job_id = std::move(*job_id)};
                break;
            case WorkerOperation::checkpoint:
                command = CheckpointCommand{.job_id = std::move(*job_id)};
                break;
            case WorkerOperation::collect:
                command = CollectCommand{.job_id = std::move(*job_id)};
                break;
            default:
                break;
            }
        }
    }
    if (!command.has_value() || !reader.empty()) {
        return WorkerRequestDecodeResult{
            .request = std::nullopt,
            .diagnostics = {protocol_issue(
                "frame.payload", "worker request payload is malformed or has trailing data")},
        };
    }
    return WorkerRequestDecodeResult{
        .request = WorkerRequest{
            .correlation_id = frame.correlation_id,
            .command = std::move(*command),
        },
        .diagnostics = {},
    };
}

ipc::Frame encode_worker_reply(const WorkerReply& reply)
{
    Writer writer;
    writer.text(api::worker_contract_version);
    writer.boolean(reply.state.has_value());
    if (reply.state.has_value()) {
        write_state(writer, *reply.state);
    }
    writer.boolean(reply.checkpoint.has_value());
    if (reply.checkpoint.has_value()) {
        write_checkpoint(writer, *reply.checkpoint);
    }
    writer.boolean(reply.artifacts.has_value());
    if (reply.artifacts.has_value()) {
        write_artifacts(writer, *reply.artifacts);
    }
    write_diagnostics(writer, reply.diagnostics);
    return ipc::Frame{
        .message_type = reply_type_base
            + static_cast<std::uint32_t>(reply.operation),
        .correlation_id = reply.correlation_id,
        .payload = std::move(writer).finish(),
    };
}

WorkerReplyDecodeResult decode_worker_reply(const ipc::Frame& frame)
{
    const auto operation = operation_from_type(frame.message_type, reply_type_base);
    if (!operation.has_value()) {
        return WorkerReplyDecodeResult{
            .reply = std::nullopt,
            .diagnostics = {protocol_issue(
                "frame.message_type", "message is not a known worker reply")},
        };
    }
    Reader reader(frame.payload);
    if (!read_contract_version(reader)) {
        return WorkerReplyDecodeResult{
            .reply = std::nullopt,
            .diagnostics = {protocol_issue(
                "frame.payload.contract_version",
                "worker contract version is missing or unsupported")},
        };
    }
    const auto has_state = reader.boolean();
    std::optional<api::JobState> state;
    if (has_state.value_or(false)) {
        state = read_state(reader);
        if (!state.has_value()) {
            return WorkerReplyDecodeResult{
                .reply = std::nullopt,
                .diagnostics = {protocol_issue(
                    "frame.payload.state", "worker state payload is malformed")},
            };
        }
    }
    const auto has_checkpoint = reader.boolean();
    std::optional<api::CheckpointArtifact> checkpoint;
    if (has_checkpoint.value_or(false)) {
        checkpoint = read_checkpoint(reader);
        if (!checkpoint.has_value()) {
            return WorkerReplyDecodeResult{
                .reply = std::nullopt,
                .diagnostics = {protocol_issue(
                    "frame.payload.checkpoint", "worker checkpoint payload is malformed")},
            };
        }
    }
    const auto has_artifacts = reader.boolean();
    std::optional<api::RawArtifacts> artifacts;
    if (has_artifacts.value_or(false)) {
        artifacts = read_artifacts(reader);
        if (!artifacts.has_value()) {
            return WorkerReplyDecodeResult{
                .reply = std::nullopt,
                .diagnostics = {protocol_issue(
                    "frame.payload.artifacts", "worker artifact payload is malformed")},
            };
        }
    }
    auto diagnostics = read_diagnostics(reader);
    if (!has_state.has_value() || !has_checkpoint.has_value()
        || !has_artifacts.has_value() || !diagnostics.has_value()
        || !reader.empty()) {
        return WorkerReplyDecodeResult{
            .reply = std::nullopt,
            .diagnostics = {protocol_issue(
                "frame.payload", "worker reply payload is malformed or has trailing data")},
        };
    }
    return WorkerReplyDecodeResult{
        .reply = WorkerReply{
            .correlation_id = frame.correlation_id,
            .operation = *operation,
            .state = std::move(state),
            .checkpoint = std::move(checkpoint),
            .artifacts = std::move(artifacts),
            .diagnostics = std::move(*diagnostics),
        },
        .diagnostics = {},
    };
}

WorkerProtocolSession::WorkerProtocolSession(
    std::string worker_id,
    std::string solver_build_id)
    : worker_(std::move(worker_id), std::move(solver_build_id))
{
}

WorkerReply WorkerProtocolSession::handle(const WorkerRequest& request)
{
    const auto operation = operation_for(request.command);
    return std::visit([this, &request, operation](const auto& command) {
        using Command = std::decay_t<decltype(command)>;
        if constexpr (std::is_same_v<Command, SubmitCommand>) {
            return state_reply(
                request.correlation_id, operation, worker_.submit(command.job));
        } else if constexpr (std::is_same_v<Command, ResumeCommand>) {
            return state_reply(
                request.correlation_id,
                operation,
                worker_.resume(command.job, command.checkpoint));
        } else if constexpr (std::is_same_v<Command, StatusCommand>) {
            return state_reply(
                request.correlation_id, operation, worker_.status(command.job_id));
        } else if constexpr (std::is_same_v<Command, CancelCommand>) {
            return state_reply(
                request.correlation_id, operation, worker_.cancel(command.job_id));
        } else if constexpr (std::is_same_v<Command, CheckpointCommand>) {
            auto result = worker_.checkpoint(command.job_id);
            return WorkerReply{
                .correlation_id = request.correlation_id,
                .operation = operation,
                .state = std::nullopt,
                .checkpoint = std::move(result.checkpoint),
                .artifacts = std::nullopt,
                .diagnostics = std::move(result.diagnostics),
            };
        } else {
            auto result = worker_.collect(command.job_id);
            return WorkerReply{
                .correlation_id = request.correlation_id,
                .operation = operation,
                .state = std::nullopt,
                .checkpoint = std::nullopt,
                .artifacts = std::move(result.artifacts),
                .diagnostics = std::move(result.diagnostics),
            };
        }
    }, request.command);
}

void WorkerProtocolSession::advance_all(
    const std::uint64_t maximum_time_steps_per_job)
{
    worker_.run_ready_jobs(maximum_time_steps_per_job);
}

} // namespace oep::simulation_orchestrator
