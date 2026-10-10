// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <oep/em_solver_api/solver_api.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace oep::simulation_orchestrator {

inline constexpr std::string_view reference_worker_build_id =
    "reference-fdtd-worker-0.1";

class ReferenceSimulationWorker {
public:
    ReferenceSimulationWorker(std::string worker_id, std::string solver_build_id);
    ~ReferenceSimulationWorker();

    ReferenceSimulationWorker(ReferenceSimulationWorker&&) noexcept;
    ReferenceSimulationWorker& operator=(ReferenceSimulationWorker&&) noexcept;
    ReferenceSimulationWorker(const ReferenceSimulationWorker&) = delete;
    ReferenceSimulationWorker& operator=(const ReferenceSimulationWorker&) = delete;

    [[nodiscard]] em_solver_api::ValidationReport validate(
        const em_solver_api::PreparedSimulationJob& request) const;
    [[nodiscard]] em_solver_api::StateResult submit(
        const em_solver_api::PreparedSimulationJob& request);
    [[nodiscard]] em_solver_api::StateResult resume(
        const em_solver_api::PreparedSimulationJob& request,
        const em_solver_api::CheckpointArtifact& checkpoint);
    [[nodiscard]] em_solver_api::StateResult status(const std::string& job_id) const;
    [[nodiscard]] em_solver_api::StateResult run_slice(
        const std::string& job_id,
        std::uint64_t maximum_time_steps);
    void run_ready_jobs(std::uint64_t maximum_time_steps_per_job);
    [[nodiscard]] em_solver_api::StateResult cancel(const std::string& job_id);
    [[nodiscard]] em_solver_api::CheckpointResult checkpoint(
        const std::string& job_id) const;
    [[nodiscard]] em_solver_api::ArtifactResult collect(
        const std::string& job_id) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace oep::simulation_orchestrator
