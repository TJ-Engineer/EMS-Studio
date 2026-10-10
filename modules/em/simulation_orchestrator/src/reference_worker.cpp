// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#include <oep/simulation_orchestrator/reference_worker.hpp>

#include <oep/fdtd/reference_fdtd.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <utility>

namespace oep::simulation_orchestrator {
namespace {

namespace api = em_solver_api;

[[nodiscard]] api::WorkerDiagnostic issue(
    const api::WorkerDiagnosticCode code,
    std::string path,
    std::string message)
{
    return api::WorkerDiagnostic{
        .code = code,
        .path = std::move(path),
        .message = std::move(message),
    };
}

[[nodiscard]] bool finite_positive(const double value)
{
    return std::isfinite(value) && value > 0.0;
}

[[nodiscard]] bool checked_product(
    const std::uint64_t first,
    const std::uint64_t second,
    const std::uint64_t third,
    std::uint64_t& result)
{
    if (first == 0 || second == 0 || third == 0
        || first > std::numeric_limits<std::uint64_t>::max() / second) {
        return false;
    }
    const auto first_two = first * second;
    if (first_two > std::numeric_limits<std::uint64_t>::max() / third) {
        return false;
    }
    result = first_two * third;
    return true;
}

[[nodiscard]] std::optional<fdtd::Grid3dConfig> fdtd_config(
    const api::StructuredGrid3dConfig& grid)
{
    const auto size_limit = static_cast<std::uint64_t>(
        std::numeric_limits<std::size_t>::max());
    if (grid.cells_x > size_limit || grid.cells_y > size_limit
        || grid.cells_z > size_limit) {
        return std::nullopt;
    }
    return fdtd::Grid3dConfig{
        .cells_x = static_cast<std::size_t>(grid.cells_x),
        .cells_y = static_cast<std::size_t>(grid.cells_y),
        .cells_z = static_cast<std::size_t>(grid.cells_z),
        .length_x_m = grid.length_x_m,
        .length_y_m = grid.length_y_m,
        .length_z_m = grid.length_z_m,
        .relative_permittivity = grid.relative_permittivity,
        .relative_permeability = grid.relative_permeability,
        .courant_factor = grid.courant_factor,
    };
}

[[nodiscard]] bool same_config(
    const fdtd::Grid3dConfig& left,
    const api::StructuredGrid3dConfig& right)
{
    return left.cells_x == right.cells_x && left.cells_y == right.cells_y
        && left.cells_z == right.cells_z
        && left.length_x_m == right.length_x_m
        && left.length_y_m == right.length_y_m
        && left.length_z_m == right.length_z_m
        && left.relative_permittivity == right.relative_permittivity
        && left.relative_permeability == right.relative_permeability
        && left.courant_factor == right.courant_factor;
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

[[nodiscard]] std::string content_id(
    const std::string_view prefix,
    const std::span<const std::byte> payload)
{
    std::ostringstream stream;
    stream << prefix << std::hex << std::setfill('0') << std::setw(16)
           << fnv1a(payload);
    return stream.str();
}

[[nodiscard]] api::JobState state_for(
    const std::string& worker_id,
    const std::string& solver_build_id,
    const api::PreparedSimulationJob& request,
    const api::JobPhase phase,
    const std::uint64_t completed_steps)
{
    return api::JobState{
        .job_id = request.job_id,
        .study_fingerprint = request.study_fingerprint,
        .phase = phase,
        .completed_time_steps = completed_steps,
        .total_time_steps = request.total_time_steps,
        .progress = static_cast<double>(completed_steps)
            / static_cast<double>(request.total_time_steps),
        .worker_id = worker_id,
        .solver_build_id = solver_build_id,
    };
}

} // namespace

class ReferenceSimulationWorker::Impl {
public:
    struct Job {
        api::PreparedSimulationJob request;
        api::JobPhase phase{api::JobPhase::queued};
        fdtd::YeeGrid3d grid;
        std::vector<api::WorkerDiagnostic> diagnostics;

        Job(api::PreparedSimulationJob submitted, fdtd::YeeGrid3d solver_grid)
            : request(std::move(submitted))
            , grid(std::move(solver_grid))
        {
        }
    };

    Impl(std::string worker, std::string build)
        : worker_id(std::move(worker))
        , solver_build_id(std::move(build))
    {
    }

    [[nodiscard]] api::JobState state(const Job& job) const
    {
        return state_for(
            worker_id,
            solver_build_id,
            job.request,
            job.phase,
            job.grid.step_index());
    }

    std::string worker_id;
    std::string solver_build_id;
    std::map<std::string, std::unique_ptr<Job>> jobs;
};

ReferenceSimulationWorker::ReferenceSimulationWorker(
    std::string worker_id,
    std::string solver_build_id)
    : impl_(std::make_unique<Impl>(
        std::move(worker_id), std::move(solver_build_id)))
{
}

ReferenceSimulationWorker::~ReferenceSimulationWorker() = default;
ReferenceSimulationWorker::ReferenceSimulationWorker(
    ReferenceSimulationWorker&&) noexcept = default;
ReferenceSimulationWorker& ReferenceSimulationWorker::operator=(
    ReferenceSimulationWorker&&) noexcept = default;

api::ValidationReport ReferenceSimulationWorker::validate(
    const api::PreparedSimulationJob& request) const
{
    api::ValidationReport report;
    if (request.job_id.empty()) {
        report.diagnostics.push_back(issue(
            api::WorkerDiagnosticCode::invalid_request,
            "job_id",
            "job ID must not be empty"));
    }
    if (request.study_fingerprint.empty()) {
        report.diagnostics.push_back(issue(
            api::WorkerDiagnosticCode::invalid_request,
            "study_fingerprint",
            "study fingerprint must not be empty"));
    }
    if (request.total_time_steps == 0) {
        report.diagnostics.push_back(issue(
            api::WorkerDiagnosticCode::invalid_request,
            "total_time_steps",
            "job must request at least one time step"));
    }

    auto cell_count = std::uint64_t{};
    if (!checked_product(
            request.grid.cells_x,
            request.grid.cells_y,
            request.grid.cells_z,
            cell_count)) {
        report.diagnostics.push_back(issue(
            api::WorkerDiagnosticCode::invalid_request,
            "grid",
            "grid cell count is zero or overflows"));
    }
    const auto config = fdtd_config(request.grid);
    if (!config.has_value() || !fdtd::validate(*config).empty()) {
        report.diagnostics.push_back(issue(
            api::WorkerDiagnosticCode::invalid_request,
            "grid",
            "grid is not valid for the reference FDTD engine"));
    }

    const auto& estimate = request.resource_estimate;
    if (cell_count != 0 && estimate.grid_cells != cell_count) {
        report.diagnostics.push_back(issue(
            api::WorkerDiagnosticCode::invalid_request,
            "resource_estimate.grid_cells",
            "resource estimate does not match the prepared grid"));
    }
    if (estimate.time_steps != request.total_time_steps
        || estimate.estimated_peak_memory_bytes == 0
        || estimate.estimated_storage_bytes == 0
        || !finite_positive(estimate.runtime_upper_bound_s)
        || estimate.calibration_hardware_id.empty()
        || estimate.calibration_solver_build_id.empty()
        || (estimate.within_budget && estimate.requires_escalation)) {
        report.diagnostics.push_back(issue(
            api::WorkerDiagnosticCode::invalid_request,
            "resource_estimate",
            "resource estimate must be complete and match the prepared workload"));
    }
    if (estimate.calibration_solver_build_id != impl_->solver_build_id) {
        report.diagnostics.push_back(issue(
            api::WorkerDiagnosticCode::solver_build_mismatch,
            "resource_estimate.calibration_solver_build_id",
            "runtime calibration belongs to a different solver build"));
    }
    if (estimate.requires_escalation
        && (!request.resource_approval_id.has_value()
            || request.resource_approval_id->empty())) {
        report.diagnostics.push_back(issue(
            api::WorkerDiagnosticCode::resource_approval_required,
            "resource_approval_id",
            "over-budget execution requires an explicit approval ID"));
    }
    return report;
}

api::StateResult ReferenceSimulationWorker::submit(
    const api::PreparedSimulationJob& request)
{
    auto report = validate(request);
    if (!report.valid()) {
        return api::StateResult{
            .state = std::nullopt,
            .diagnostics = std::move(report.diagnostics),
        };
    }
    if (impl_->jobs.contains(request.job_id)) {
        return api::StateResult{
            .state = std::nullopt,
            .diagnostics = {issue(
                api::WorkerDiagnosticCode::duplicate_job,
                "job_id",
                "job ID already exists in this worker")},
        };
    }
    auto job = std::make_unique<Impl::Job>(
        request, fdtd::YeeGrid3d(*fdtd_config(request.grid)));
    const auto state = impl_->state(*job);
    impl_->jobs.emplace(request.job_id, std::move(job));
    return api::StateResult{.state = state, .diagnostics = {}};
}

api::StateResult ReferenceSimulationWorker::resume(
    const api::PreparedSimulationJob& request,
    const api::CheckpointArtifact& checkpoint)
{
    auto report = validate(request);
    if (!report.valid()) {
        return api::StateResult{
            .state = std::nullopt,
            .diagnostics = std::move(report.diagnostics),
        };
    }
    if (impl_->jobs.contains(request.job_id)) {
        return api::StateResult{
            .state = std::nullopt,
            .diagnostics = {issue(
                api::WorkerDiagnosticCode::duplicate_job,
                "job_id",
                "job ID already exists in this worker")},
        };
    }
    if (checkpoint.checkpoint_id
            != content_id("checkpoint-fnv1a64:", checkpoint.payload)
        || checkpoint.job_id != request.job_id
        || checkpoint.study_fingerprint != request.study_fingerprint
        || checkpoint.solver_build_id != impl_->solver_build_id) {
        return api::StateResult{
            .state = std::nullopt,
            .diagnostics = {issue(
                api::WorkerDiagnosticCode::checkpoint_mismatch,
                "checkpoint",
                "checkpoint identity, job, study, or solver build does not match the resume request")},
        };
    }
    auto restored = fdtd::restore_yee_grid_3d(checkpoint.payload);
    if (!restored.succeeded()) {
        return api::StateResult{
            .state = std::nullopt,
            .diagnostics = {issue(
                api::WorkerDiagnosticCode::corrupt_checkpoint,
                "checkpoint.payload",
                "reference solver rejected the checkpoint payload")},
        };
    }
    if (!same_config(restored.grid->config(), request.grid)
        || restored.grid->step_index() != checkpoint.completed_time_steps
        || restored.grid->step_index() > request.total_time_steps) {
        return api::StateResult{
            .state = std::nullopt,
            .diagnostics = {issue(
                api::WorkerDiagnosticCode::checkpoint_mismatch,
                "checkpoint",
                "checkpoint configuration or progress does not match the prepared job")},
        };
    }

    auto job = std::make_unique<Impl::Job>(request, std::move(*restored.grid));
    job->phase = job->grid.step_index() == request.total_time_steps
        ? api::JobPhase::succeeded
        : api::JobPhase::running;
    const auto state = impl_->state(*job);
    impl_->jobs.emplace(request.job_id, std::move(job));
    return api::StateResult{.state = state, .diagnostics = {}};
}

api::StateResult ReferenceSimulationWorker::status(
    const std::string& job_id) const
{
    const auto found = impl_->jobs.find(job_id);
    if (found == impl_->jobs.end()) {
        return api::StateResult{
            .state = std::nullopt,
            .diagnostics = {issue(
                api::WorkerDiagnosticCode::unknown_job,
                "job_id",
                "job ID is not known to this worker")},
        };
    }
    return api::StateResult{
        .state = impl_->state(*found->second),
        .diagnostics = found->second->diagnostics,
    };
}

api::StateResult ReferenceSimulationWorker::run_slice(
    const std::string& job_id,
    const std::uint64_t maximum_time_steps)
{
    const auto found = impl_->jobs.find(job_id);
    if (found == impl_->jobs.end()) {
        return status(job_id);
    }
    auto& job = *found->second;
    if (maximum_time_steps == 0) {
        return api::StateResult{
            .state = std::nullopt,
            .diagnostics = {issue(
                api::WorkerDiagnosticCode::invalid_request,
                "maximum_time_steps",
                "execution slice must allow at least one time step")},
        };
    }
    if (api::terminal(job.phase)) {
        return api::StateResult{
            .state = impl_->state(job),
            .diagnostics = job.diagnostics,
        };
    }
    if (job.phase == api::JobPhase::cancelling) {
        job.phase = api::JobPhase::cancelled;
        return api::StateResult{.state = impl_->state(job), .diagnostics = {}};
    }
    job.phase = api::JobPhase::running;
    const auto remaining = job.request.total_time_steps - job.grid.step_index();
    const auto steps = std::min(maximum_time_steps, remaining);
    for (auto index = std::uint64_t{}; index < steps; ++index) {
        const auto step = job.grid.step();
        if (!step.succeeded()) {
            job.phase = api::JobPhase::failed;
            job.diagnostics.push_back(issue(
                api::WorkerDiagnosticCode::solver_failure,
                "solver",
                "reference solver failed while advancing the job"));
            break;
        }
    }
    if (job.grid.step_index() == job.request.total_time_steps) {
        job.phase = api::JobPhase::succeeded;
    }
    return api::StateResult{
        .state = impl_->state(job),
        .diagnostics = job.diagnostics,
    };
}

void ReferenceSimulationWorker::run_ready_jobs(
    const std::uint64_t maximum_time_steps_per_job)
{
    if (maximum_time_steps_per_job == 0) {
        return;
    }
    for (const auto& [job_id, job] : impl_->jobs) {
        if (!api::terminal(job->phase)) {
            static_cast<void>(run_slice(job_id, maximum_time_steps_per_job));
        }
    }
}

api::StateResult ReferenceSimulationWorker::cancel(const std::string& job_id)
{
    const auto found = impl_->jobs.find(job_id);
    if (found == impl_->jobs.end()) {
        return status(job_id);
    }
    auto& job = *found->second;
    if (job.phase == api::JobPhase::cancelled) {
        return api::StateResult{.state = impl_->state(job), .diagnostics = {}};
    }
    if (job.phase == api::JobPhase::succeeded
        || job.phase == api::JobPhase::failed) {
        return api::StateResult{
            .state = std::nullopt,
            .diagnostics = {issue(
                api::WorkerDiagnosticCode::invalid_transition,
                "job.phase",
                "completed or failed job cannot be cancelled")},
        };
    }
    job.phase = api::JobPhase::cancelled;
    return api::StateResult{.state = impl_->state(job), .diagnostics = {}};
}

api::CheckpointResult ReferenceSimulationWorker::checkpoint(
    const std::string& job_id) const
{
    const auto found = impl_->jobs.find(job_id);
    if (found == impl_->jobs.end()) {
        return api::CheckpointResult{
            .checkpoint = std::nullopt,
            .diagnostics = {issue(
                api::WorkerDiagnosticCode::unknown_job,
                "job_id",
                "job ID is not known to this worker")},
        };
    }
    const auto& job = *found->second;
    if (job.phase == api::JobPhase::cancelled
        || job.phase == api::JobPhase::failed) {
        return api::CheckpointResult{
            .checkpoint = std::nullopt,
            .diagnostics = {issue(
                api::WorkerDiagnosticCode::invalid_transition,
                "job.phase",
                "cancelled or failed job cannot produce a current checkpoint")},
        };
    }
    auto payload = job.grid.checkpoint();
    auto checkpoint = api::CheckpointArtifact{
        .checkpoint_id = content_id("checkpoint-fnv1a64:", payload),
        .job_id = job.request.job_id,
        .study_fingerprint = job.request.study_fingerprint,
        .solver_build_id = impl_->solver_build_id,
        .completed_time_steps = job.grid.step_index(),
        .payload = std::move(payload),
    };
    return api::CheckpointResult{
        .checkpoint = std::move(checkpoint),
        .diagnostics = {},
    };
}

api::ArtifactResult ReferenceSimulationWorker::collect(
    const std::string& job_id) const
{
    const auto found = impl_->jobs.find(job_id);
    if (found == impl_->jobs.end()) {
        return api::ArtifactResult{
            .artifacts = std::nullopt,
            .diagnostics = {issue(
                api::WorkerDiagnosticCode::unknown_job,
                "job_id",
                "job ID is not known to this worker")},
        };
    }
    const auto& job = *found->second;
    if (job.phase != api::JobPhase::succeeded) {
        return api::ArtifactResult{
            .artifacts = std::nullopt,
            .diagnostics = {issue(
                api::WorkerDiagnosticCode::invalid_transition,
                "job.phase",
                "raw artifacts are available only after successful completion")},
        };
    }
    auto payload = job.grid.checkpoint();
    auto artifacts = api::RawArtifacts{
        .artifact_id = content_id("raw-fdtd-fnv1a64:", payload),
        .job_id = job.request.job_id,
        .study_fingerprint = job.request.study_fingerprint,
        .solver_build_id = impl_->solver_build_id,
        .completed_time_steps = job.grid.step_index(),
        .final_solver_checkpoint = std::move(payload),
    };
    return api::ArtifactResult{
        .artifacts = std::move(artifacts),
        .diagnostics = {},
    };
}

} // namespace oep::simulation_orchestrator
