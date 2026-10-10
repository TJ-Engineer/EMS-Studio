// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <oep/em_mesh/cartesian_grid.hpp>
#include <oep/simulation_ir/simulation_ir.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace oep::simulation_cost {

inline constexpr double vacuum_light_speed_m_per_s = 299'792'458.0;

enum class DiagnosticCode {
    invalid_grid,
    invalid_workload,
    invalid_calibration,
    invalid_budget,
    arithmetic_overflow,
};

struct Diagnostic {
    DiagnosticCode code{DiagnosticCode::invalid_grid};
    std::string path;
    std::string message;
};

struct RuntimeObservation {
    std::uint64_t grid_cells{};
    std::uint64_t time_steps{};
    double wall_time_s{};
};

struct ReferenceHardwareCalibration {
    std::string hardware_id;
    std::string solver_build_id;
    std::uint32_t thread_count{1};
    double cell_updates_per_second{};
    double relative_prediction_uncertainty{};
    std::uint64_t fixed_memory_overhead_bytes{};
    std::uint64_t bytes_per_cell_overhead{};
    std::uint64_t observation_count{};
};

struct CalibrationResult {
    std::optional<ReferenceHardwareCalibration> calibration;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept
    {
        return calibration.has_value();
    }
};

[[nodiscard]] CalibrationResult calibrate_reference_runtime(
    std::string hardware_id,
    std::string solver_build_id,
    std::uint32_t thread_count,
    std::span<const RuntimeObservation> observations,
    double minimum_relative_uncertainty = 0.10,
    std::uint64_t fixed_memory_overhead_bytes = 0,
    std::uint64_t bytes_per_cell_overhead = 0);

struct ReferenceFdtdWorkload {
    em_mesh::CartesianGridSpec grid;
    double courant_factor{0.99};
    double maximum_wave_speed_m_per_s{vacuum_light_speed_m_per_s};
    double simulated_duration_s{};
    std::uint64_t retained_checkpoint_count{1};
    std::uint64_t result_storage_bytes{};
};

enum class BudgetResource {
    cells,
    memory,
    runtime,
    storage,
};

struct BudgetAssessment {
    BudgetResource resource{BudgetResource::cells};
    bool exceeded{};
    double utilization{};
};

struct ReferenceFdtdEstimate {
    std::uint64_t grid_cells{};
    std::uint64_t electric_field_values{};
    std::uint64_t magnetic_field_values{};
    std::uint64_t field_values{};
    double time_step_s{};
    std::uint64_t time_steps{};
    std::uint64_t cell_updates{};
    std::uint64_t field_storage_bytes{};
    std::uint64_t transient_scratch_bytes{};
    std::uint64_t checkpoint_bytes{};
    std::uint64_t estimated_peak_memory_bytes{};
    std::uint64_t estimated_storage_bytes{};
    double estimated_runtime_s{};
    double runtime_lower_bound_s{};
    double runtime_upper_bound_s{};
    std::string calibration_hardware_id;
    std::string calibration_solver_build_id;
    std::vector<BudgetAssessment> budget_assessments;
    bool within_budget{};
    bool requires_escalation{};
};

struct EstimateResult {
    std::optional<ReferenceFdtdEstimate> estimate;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept { return estimate.has_value(); }
};

class ReferenceFdtdCostEstimator {
public:
    [[nodiscard]] EstimateResult estimate(
        const ReferenceFdtdWorkload& workload,
        const ReferenceHardwareCalibration& calibration,
        const simulation_ir::ResourceBudget& budget) const;
};

} // namespace oep::simulation_cost
