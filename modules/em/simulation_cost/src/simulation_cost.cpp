// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

// Estimates resource use before solver allocation and assesses each hard budget.
// Runtime depends on explicitly supplied calibration observations; an estimate
// is not a measurement or permission to silently reduce simulation fidelity.

#include <oep/simulation_cost/simulation_cost.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <utility>

namespace oep::simulation_cost {
namespace {

constexpr std::uint64_t fp64_bytes = sizeof(double);
constexpr std::uint64_t checkpoint_payload_overhead_bytes = 161;

[[nodiscard]] Diagnostic diagnostic(
    const DiagnosticCode code,
    std::string path,
    std::string message)
{
    return Diagnostic{
        .code = code,
        .path = std::move(path),
        .message = std::move(message),
    };
}

[[nodiscard]] bool finite_positive(const double value)
{
    return std::isfinite(value) && value > 0.0;
}

[[nodiscard]] bool checked_add(
    const std::uint64_t left,
    const std::uint64_t right,
    std::uint64_t& result)
{
    if (left > std::numeric_limits<std::uint64_t>::max() - right) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool checked_multiply(
    const std::uint64_t left,
    const std::uint64_t right,
    std::uint64_t& result)
{
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

[[nodiscard]] bool checked_product(
    const std::uint64_t first,
    const std::uint64_t second,
    const std::uint64_t third,
    std::uint64_t& result)
{
    auto first_two = std::uint64_t{};
    return checked_multiply(first, second, first_two)
        && checked_multiply(first_two, third, result);
}

[[nodiscard]] bool checked_sum(
    const std::span<const std::uint64_t> values,
    std::uint64_t& result)
{
    result = 0;
    for (const auto value : values) {
        if (!checked_add(result, value, result)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool field_counts(
    const std::array<std::size_t, 3>& counts,
    std::uint64_t& cells,
    std::uint64_t& electric,
    std::uint64_t& magnetic)
{
    const auto nx = static_cast<std::uint64_t>(counts[0]);
    const auto ny = static_cast<std::uint64_t>(counts[1]);
    const auto nz = static_cast<std::uint64_t>(counts[2]);
    if (nx == std::numeric_limits<std::uint64_t>::max()
        || ny == std::numeric_limits<std::uint64_t>::max()
        || nz == std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }
    const auto nx1 = nx + 1U;
    const auto ny1 = ny + 1U;
    const auto nz1 = nz + 1U;
    std::array<std::uint64_t, 3> electric_components{};
    std::array<std::uint64_t, 3> magnetic_components{};
    if (!checked_product(nx, ny, nz, cells)
        || !checked_product(nx, ny1, nz1, electric_components[0])
        || !checked_product(nx1, ny, nz1, electric_components[1])
        || !checked_product(nx1, ny1, nz, electric_components[2])
        || !checked_product(nx1, ny, nz, magnetic_components[0])
        || !checked_product(nx, ny1, nz, magnetic_components[1])
        || !checked_product(nx, ny, nz1, magnetic_components[2])) {
        return false;
    }
    return checked_sum(electric_components, electric)
        && checked_sum(magnetic_components, magnetic);
}

[[nodiscard]] std::vector<Diagnostic> validate_budget(
    const simulation_ir::ResourceBudget& budget)
{
    std::vector<Diagnostic> diagnostics;
    if (budget.maximum_cells == 0
        || budget.maximum_memory_bytes == 0
        || budget.maximum_storage_bytes == 0
        || budget.maximum_runtime.dimension != simulation_ir::SiDimension::time
        || !finite_positive(budget.maximum_runtime.value)) {
        diagnostics.push_back(diagnostic(
            DiagnosticCode::invalid_budget,
            "budget",
            "cell, memory, runtime, and storage limits must be positive and runtime must use the time dimension"));
    }
    return diagnostics;
}

[[nodiscard]] std::vector<Diagnostic> validate_calibration(
    const ReferenceHardwareCalibration& calibration)
{
    std::vector<Diagnostic> diagnostics;
    if (calibration.hardware_id.empty() || calibration.solver_build_id.empty()
        || calibration.thread_count == 0
        || calibration.observation_count == 0
        || !finite_positive(calibration.cell_updates_per_second)
        || !std::isfinite(calibration.relative_prediction_uncertainty)
        || calibration.relative_prediction_uncertainty < 0.0
        || calibration.relative_prediction_uncertainty >= 1.0) {
        diagnostics.push_back(diagnostic(
            DiagnosticCode::invalid_calibration,
            "calibration",
            "calibration requires named hardware and solver build, positive observations and throughput, and uncertainty in [0, 1)"));
    }
    return diagnostics;
}

[[nodiscard]] BudgetAssessment assess_integer(
    const BudgetResource resource,
    const std::uint64_t estimated,
    const std::uint64_t limit)
{
    return BudgetAssessment{
        .resource = resource,
        .exceeded = estimated > limit,
        .utilization = static_cast<double>(estimated)
            / static_cast<double>(limit),
    };
}

[[nodiscard]] BudgetAssessment assess_runtime(
    const double estimated,
    const double limit)
{
    return BudgetAssessment{
        .resource = BudgetResource::runtime,
        .exceeded = estimated > limit,
        .utilization = estimated / limit,
    };
}

} // namespace

CalibrationResult calibrate_reference_runtime(
    std::string hardware_id,
    std::string solver_build_id,
    const std::uint32_t thread_count,
    const std::span<const RuntimeObservation> observations,
    const double minimum_relative_uncertainty,
    const std::uint64_t fixed_memory_overhead_bytes,
    const std::uint64_t bytes_per_cell_overhead)
{
    CalibrationResult result;
    if (hardware_id.empty() || solver_build_id.empty() || thread_count == 0
        || observations.empty() || !std::isfinite(minimum_relative_uncertainty)
        || minimum_relative_uncertainty < 0.0
        || minimum_relative_uncertainty >= 1.0) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticCode::invalid_calibration,
            "calibration",
            "calibration metadata, observations, and uncertainty floor must be valid"));
        return result;
    }

    auto total_cell_updates = std::uint64_t{};
    auto total_wall_time_s = 0.0;
    std::vector<std::uint64_t> observation_work;
    observation_work.reserve(observations.size());
    for (std::size_t index = 0; index < observations.size(); ++index) {
        const auto& observation = observations[index];
        auto cell_updates = std::uint64_t{};
        if (observation.grid_cells == 0 || observation.time_steps == 0
            || !finite_positive(observation.wall_time_s)) {
            result.diagnostics.push_back(diagnostic(
                DiagnosticCode::invalid_calibration,
                "observations[" + std::to_string(index) + "]",
                "calibration observations require positive cells, steps, and wall time"));
            continue;
        }
        if (!checked_multiply(
                observation.grid_cells, observation.time_steps, cell_updates)
            || !checked_add(total_cell_updates, cell_updates, total_cell_updates)) {
            result.diagnostics.push_back(diagnostic(
                DiagnosticCode::arithmetic_overflow,
                "observations[" + std::to_string(index) + "]",
                "calibration cell updates exceed the supported count range"));
            continue;
        }
        observation_work.push_back(cell_updates);
        total_wall_time_s += observation.wall_time_s;
    }
    if (!result.diagnostics.empty() || !finite_positive(total_wall_time_s)) {
        return result;
    }

    const auto rate = static_cast<double>(total_cell_updates) / total_wall_time_s;
    if (!finite_positive(rate)) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticCode::invalid_calibration,
            "calibration.cell_updates_per_second",
            "measured throughput must be finite and positive"));
        return result;
    }

    auto relative_uncertainty = minimum_relative_uncertainty;
    for (std::size_t index = 0; index < observations.size(); ++index) {
        const auto predicted = static_cast<double>(observation_work[index]) / rate;
        const auto residual = std::abs(predicted - observations[index].wall_time_s)
            / observations[index].wall_time_s;
        relative_uncertainty = std::max(relative_uncertainty, residual);
    }
    if (relative_uncertainty >= 1.0) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticCode::invalid_calibration,
            "calibration.relative_prediction_uncertainty",
            "observations disagree too strongly for a bounded proportional runtime model"));
        return result;
    }

    result.calibration = ReferenceHardwareCalibration{
        .hardware_id = std::move(hardware_id),
        .solver_build_id = std::move(solver_build_id),
        .thread_count = thread_count,
        .cell_updates_per_second = rate,
        .relative_prediction_uncertainty = relative_uncertainty,
        .fixed_memory_overhead_bytes = fixed_memory_overhead_bytes,
        .bytes_per_cell_overhead = bytes_per_cell_overhead,
        .observation_count = static_cast<std::uint64_t>(observations.size()),
    };
    return result;
}

EstimateResult ReferenceFdtdCostEstimator::estimate(
    const ReferenceFdtdWorkload& workload,
    const ReferenceHardwareCalibration& calibration,
    const simulation_ir::ResourceBudget& budget) const
{
    EstimateResult result;
    result.diagnostics = validate_calibration(calibration);
    auto budget_diagnostics = validate_budget(budget);
    result.diagnostics.insert(
        result.diagnostics.end(),
        std::make_move_iterator(budget_diagnostics.begin()),
        std::make_move_iterator(budget_diagnostics.end()));

    const auto& counts = workload.grid.cell_counts;
    const auto& sizes = workload.grid.cell_size_m;
    if (counts[0] == 0 || counts[1] == 0 || counts[2] == 0
        || !finite_positive(sizes[0]) || !finite_positive(sizes[1])
        || !finite_positive(sizes[2])
        || !std::isfinite(workload.grid.origin_m.x)
        || !std::isfinite(workload.grid.origin_m.y)
        || !std::isfinite(workload.grid.origin_m.z)
        || !finite_positive(workload.grid.material_assignment_threshold)
        || workload.grid.material_assignment_threshold > 1.0
        || workload.grid.maximum_cells == 0) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticCode::invalid_grid,
            "workload.grid",
            "grid origin, counts, cell sizes, material threshold, and allocation limit must be valid"));
    }
    if (!finite_positive(workload.courant_factor)
        || workload.courant_factor > 1.0
        || !finite_positive(workload.maximum_wave_speed_m_per_s)
        || !finite_positive(workload.simulated_duration_s)) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticCode::invalid_workload,
            "workload",
            "Courant factor, maximum wave speed, and simulated duration must define a stable positive workload"));
    }
    if (!result.diagnostics.empty()) {
        return result;
    }

    auto grid_cells = std::uint64_t{};
    auto electric_values = std::uint64_t{};
    auto magnetic_values = std::uint64_t{};
    if (!field_counts(counts, grid_cells, electric_values, magnetic_values)) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticCode::arithmetic_overflow,
            "workload.grid.cell_counts",
            "grid or staggered field extents exceed the supported count range"));
        return result;
    }
    if (grid_cells > workload.grid.maximum_cells) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticCode::invalid_grid,
            "workload.grid.maximum_cells",
            "logical cell count exceeds the Cartesian grid allocation limit"));
        return result;
    }

    auto field_values = std::uint64_t{};
    auto field_storage_bytes = std::uint64_t{};
    auto transient_scratch_bytes = std::uint64_t{};
    auto checkpoint_bytes = std::uint64_t{};
    if (!checked_add(electric_values, magnetic_values, field_values)
        || !checked_multiply(field_values, fp64_bytes, field_storage_bytes)
        || !checked_multiply(electric_values, fp64_bytes, transient_scratch_bytes)
        || !checked_add(
            field_storage_bytes, checkpoint_payload_overhead_bytes, checkpoint_bytes)) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticCode::arithmetic_overflow,
            "estimate.memory",
            "field or checkpoint storage exceeds the supported byte range"));
        return result;
    }

    const auto inverse_spacing_norm = std::sqrt(
        1.0 / (sizes[0] * sizes[0])
        + 1.0 / (sizes[1] * sizes[1])
        + 1.0 / (sizes[2] * sizes[2]));
    const auto time_step_s = workload.courant_factor
        / (workload.maximum_wave_speed_m_per_s * inverse_spacing_norm);
    const auto time_step_count = std::ceil(workload.simulated_duration_s / time_step_s);
    if (!finite_positive(time_step_s) || !std::isfinite(time_step_count)
        || time_step_count > static_cast<double>(
            std::numeric_limits<std::uint64_t>::max())) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticCode::arithmetic_overflow,
            "estimate.time_steps",
            "time-step count exceeds the supported range"));
        return result;
    }
    const auto time_steps = static_cast<std::uint64_t>(time_step_count);

    auto cell_updates = std::uint64_t{};
    auto auxiliary_per_cell_bytes = std::uint64_t{};
    auto runtime_payload_bytes = std::uint64_t{};
    auto checkpoint_payload_bytes = field_storage_bytes;
    auto peak_payload_bytes = std::uint64_t{};
    auto estimated_peak_memory_bytes = std::uint64_t{};
    auto retained_checkpoint_bytes = std::uint64_t{};
    auto estimated_storage_bytes = std::uint64_t{};
    if (!checked_multiply(grid_cells, time_steps, cell_updates)
        || !checked_multiply(
            grid_cells, calibration.bytes_per_cell_overhead, auxiliary_per_cell_bytes)
        || !checked_add(field_storage_bytes, transient_scratch_bytes, runtime_payload_bytes)
        || (workload.retained_checkpoint_count > 0
            && !checked_add(field_storage_bytes, checkpoint_bytes, checkpoint_payload_bytes))
        || !checked_add(
            std::max(runtime_payload_bytes, checkpoint_payload_bytes),
            auxiliary_per_cell_bytes,
            peak_payload_bytes)
        || !checked_add(
            peak_payload_bytes,
            calibration.fixed_memory_overhead_bytes,
            estimated_peak_memory_bytes)
        || !checked_multiply(
            checkpoint_bytes,
            workload.retained_checkpoint_count,
            retained_checkpoint_bytes)
        || !checked_add(
            retained_checkpoint_bytes,
            workload.result_storage_bytes,
            estimated_storage_bytes)) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticCode::arithmetic_overflow,
            "estimate",
            "work, memory, or storage estimate exceeds the supported range"));
        return result;
    }

    const auto estimated_runtime_s = static_cast<double>(cell_updates)
        / calibration.cell_updates_per_second;
    if (!finite_positive(estimated_runtime_s)) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticCode::arithmetic_overflow,
            "estimate.runtime",
            "runtime estimate is not finite and positive"));
        return result;
    }
    const auto uncertainty = calibration.relative_prediction_uncertainty;
    const auto runtime_lower_bound_s = estimated_runtime_s * (1.0 - uncertainty);
    const auto runtime_upper_bound_s = estimated_runtime_s * (1.0 + uncertainty);
    if (!std::isfinite(runtime_lower_bound_s)
        || !std::isfinite(runtime_upper_bound_s)) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticCode::arithmetic_overflow,
            "estimate.runtime",
            "runtime prediction interval exceeds the supported range"));
        return result;
    }

    std::vector<BudgetAssessment> assessments;
    assessments.reserve(4);
    assessments.push_back(assess_integer(
        BudgetResource::cells,
        grid_cells,
        budget.maximum_cells));
    assessments.push_back(assess_integer(
        BudgetResource::memory,
        estimated_peak_memory_bytes,
        budget.maximum_memory_bytes));
    assessments.push_back(assess_runtime(
        runtime_upper_bound_s,
        budget.maximum_runtime.value));
    assessments.push_back(assess_integer(
        BudgetResource::storage,
        estimated_storage_bytes,
        budget.maximum_storage_bytes));
    const auto any_exceeded = std::ranges::any_of(
        assessments, [](const auto& assessment) { return assessment.exceeded; });

    result.estimate = ReferenceFdtdEstimate{
        .grid_cells = grid_cells,
        .electric_field_values = electric_values,
        .magnetic_field_values = magnetic_values,
        .field_values = field_values,
        .time_step_s = time_step_s,
        .time_steps = time_steps,
        .cell_updates = cell_updates,
        .field_storage_bytes = field_storage_bytes,
        .transient_scratch_bytes = transient_scratch_bytes,
        .checkpoint_bytes = checkpoint_bytes,
        .estimated_peak_memory_bytes = estimated_peak_memory_bytes,
        .estimated_storage_bytes = estimated_storage_bytes,
        .estimated_runtime_s = estimated_runtime_s,
        .runtime_lower_bound_s = runtime_lower_bound_s,
        .runtime_upper_bound_s = runtime_upper_bound_s,
        .calibration_hardware_id = calibration.hardware_id,
        .calibration_solver_build_id = calibration.solver_build_id,
        .budget_assessments = std::move(assessments),
        .within_budget = !any_exceeded,
        .requires_escalation = any_exceeded
            && budget.require_escalation_on_exceedance,
    };
    return result;
}

} // namespace oep::simulation_cost
