// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

// Solver-neutral worker messages carry immutable study identity, resource
// estimates, progress, and artifacts. Keep numerical field ownership in workers;
// UI and transport consumers should depend on these value types instead.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace oep::em_solver_api {

inline constexpr std::string_view worker_contract_version =
    "oep-simulation-worker-1.0";

struct StructuredGrid3dConfig {
    std::uint64_t cells_x{};
    std::uint64_t cells_y{};
    std::uint64_t cells_z{};
    double length_x_m{};
    double length_y_m{};
    double length_z_m{};
    double relative_permittivity{1.0};
    double relative_permeability{1.0};
    double courant_factor{0.99};

    bool operator==(const StructuredGrid3dConfig&) const = default;
};

struct ResourceEstimateSnapshot {
    std::uint64_t grid_cells{};
    std::uint64_t time_steps{};
    std::uint64_t estimated_peak_memory_bytes{};
    std::uint64_t estimated_storage_bytes{};
    double runtime_upper_bound_s{};
    std::string calibration_hardware_id;
    std::string calibration_solver_build_id;
    bool within_budget{};
    bool requires_escalation{};

    bool operator==(const ResourceEstimateSnapshot&) const = default;
};

struct PreparedSimulationJob {
    std::string job_id;
    std::string study_fingerprint;
    StructuredGrid3dConfig grid;
    std::uint64_t total_time_steps{};
    ResourceEstimateSnapshot resource_estimate;
    std::optional<std::string> resource_approval_id;

    bool operator==(const PreparedSimulationJob&) const = default;
};

enum class JobPhase {
    queued,
    running,
    cancelling,
    cancelled,
    succeeded,
    failed,
};

[[nodiscard]] constexpr bool terminal(const JobPhase phase) noexcept
{
    return phase == JobPhase::cancelled || phase == JobPhase::succeeded
        || phase == JobPhase::failed;
}

struct JobState {
    std::string job_id;
    std::string study_fingerprint;
    JobPhase phase{JobPhase::queued};
    std::uint64_t completed_time_steps{};
    std::uint64_t total_time_steps{};
    double progress{};
    std::string worker_id;
    std::string solver_build_id;

    bool operator==(const JobState&) const = default;
};

enum class WorkerDiagnosticCode {
    invalid_request,
    duplicate_job,
    unknown_job,
    resource_approval_required,
    solver_build_mismatch,
    invalid_transition,
    solver_failure,
    checkpoint_mismatch,
    corrupt_checkpoint,
    protocol_error,
};

struct WorkerDiagnostic {
    WorkerDiagnosticCode code{WorkerDiagnosticCode::invalid_request};
    std::string path;
    std::string message;

    bool operator==(const WorkerDiagnostic&) const = default;
};

struct ValidationReport {
    std::vector<WorkerDiagnostic> diagnostics;

    [[nodiscard]] bool valid() const noexcept { return diagnostics.empty(); }
};

struct StateResult {
    std::optional<JobState> state;
    std::vector<WorkerDiagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept { return state.has_value(); }
};

struct CheckpointArtifact {
    std::string checkpoint_id;
    std::string job_id;
    std::string study_fingerprint;
    std::string solver_build_id;
    std::uint64_t completed_time_steps{};
    std::vector<std::byte> payload;

    bool operator==(const CheckpointArtifact&) const = default;
};

struct CheckpointResult {
    std::optional<CheckpointArtifact> checkpoint;
    std::vector<WorkerDiagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept
    {
        return checkpoint.has_value();
    }
};

struct RawArtifacts {
    std::string artifact_id;
    std::string job_id;
    std::string study_fingerprint;
    std::string solver_build_id;
    std::uint64_t completed_time_steps{};
    std::vector<std::byte> final_solver_checkpoint;

    bool operator==(const RawArtifacts&) const = default;
};

struct ArtifactResult {
    std::optional<RawArtifacts> artifacts;
    std::vector<WorkerDiagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept
    {
        return artifacts.has_value();
    }
};

} // namespace oep::em_solver_api
