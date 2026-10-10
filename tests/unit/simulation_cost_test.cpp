// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#include <oep/simulation_cost/simulation_cost.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {

namespace cost = oep::simulation_cost;
namespace ir = oep::simulation_ir;

void check(const bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

[[nodiscard]] bool approximately_equal(
    const double left,
    const double right,
    const double tolerance = 1.0e-12)
{
    const auto scale = std::max({1.0, std::abs(left), std::abs(right)});
    return std::abs(left - right) <= tolerance * scale;
}

[[nodiscard]] ir::ResourceBudget generous_budget()
{
    return ir::ResourceBudget{
        .maximum_cells = 1'000'000'000,
        .maximum_memory_bytes = 64ULL * 1024ULL * 1024ULL * 1024ULL,
        .maximum_runtime = ir::SiValue{
            .value = 86'400.0,
            .dimension = ir::SiDimension::time,
        },
        .maximum_storage_bytes = 64ULL * 1024ULL * 1024ULL * 1024ULL,
        .require_escalation_on_exceedance = true,
    };
}

[[nodiscard]] cost::ReferenceHardwareCalibration deterministic_calibration()
{
    const cost::RuntimeObservation observations[]{
        {.grid_cells = 1'000, .time_steps = 100, .wall_time_s = 1.0},
        {.grid_cells = 2'000, .time_steps = 100, .wall_time_s = 2.0},
    };
    const auto result = cost::calibrate_reference_runtime(
        "unit-test-cpu/single-thread",
        "reference-fdtd-test-build",
        1,
        observations,
        0.10,
        4'096,
        4);
    check(result.succeeded(), "consistent measurements must produce a calibration");
    check(result.diagnostics.empty(), "valid calibration must be diagnostic-free");
    return *result.calibration;
}

[[nodiscard]] cost::ReferenceFdtdWorkload representative_workload()
{
    const auto time_step = 0.99
        / (cost::vacuum_light_speed_m_per_s * std::sqrt(3.0e6));
    return cost::ReferenceFdtdWorkload{
        .grid = oep::em_mesh::CartesianGridSpec{
            .origin_m = {0.0, 0.0, 0.0},
            .cell_counts = {10, 8, 6},
            .cell_size_m = {0.001, 0.001, 0.001},
            .material_assignment_threshold = 0.5,
            .maximum_cells = 1'000'000,
        },
        .courant_factor = 0.99,
        .maximum_wave_speed_m_per_s = cost::vacuum_light_speed_m_per_s,
        .simulated_duration_s = 9.5 * time_step,
        .retained_checkpoint_count = 2,
        .result_storage_bytes = 1'234,
    };
}

void exact_field_time_memory_and_storage_counts()
{
    const auto calibration = deterministic_calibration();
    const auto result = cost::ReferenceFdtdCostEstimator{}.estimate(
        representative_workload(), calibration, generous_budget());
    check(result.succeeded(), "valid reference workload must estimate");
    check(result.diagnostics.empty(), "valid estimate must be diagnostic-free");
    const auto& estimate = *result.estimate;

    check(estimate.grid_cells == 480, "logical grid cell product must be exact");
    check(estimate.electric_field_values == 1'840,
        "electric Yee extents must be counted exactly");
    check(estimate.magnetic_field_values == 1'628,
        "magnetic Yee extents must be counted exactly");
    check(estimate.field_values == 3'468,
        "all staggered field values must be retained");
    check(estimate.time_steps == 10, "duration must round up to a complete time step");
    check(estimate.cell_updates == 4'800, "cell updates must include every time step");
    check(estimate.field_storage_bytes == 27'744,
        "FP64 field bytes must match exact component extents");
    check(estimate.transient_scratch_bytes == 14'720,
        "reference electric-field copy must be included as transient scratch");
    check(estimate.checkpoint_bytes == 27'905,
        "checkpoint bytes must include exact version-1 payload overhead");
    check(estimate.estimated_peak_memory_bytes == 61'665,
        "peak payload must include live fields, checkpoint, and calibration overhead");
    check(estimate.estimated_storage_bytes == 57'044,
        "storage must include retained checkpoints and requested result artifacts");
    check(approximately_equal(estimate.estimated_runtime_s, 0.048),
        "runtime must use measured cell updates per second");
    check(approximately_equal(estimate.runtime_lower_bound_s, 0.0432),
        "runtime lower bound must retain calibration uncertainty");
    check(approximately_equal(estimate.runtime_upper_bound_s, 0.0528),
        "runtime upper bound must retain calibration uncertainty");
    check(estimate.within_budget, "representative workload must fit generous limits");
    check(!estimate.requires_escalation,
        "within-budget workload must not require escalation");
    check(estimate.calibration_hardware_id == "unit-test-cpu/single-thread",
        "estimate must retain its hardware calibration identity");
}

void every_hard_budget_is_visible()
{
    const auto calibration = deterministic_calibration();
    const auto baseline = cost::ReferenceFdtdCostEstimator{}.estimate(
        representative_workload(), calibration, generous_budget());
    check(baseline.succeeded(), "baseline budget fixture must estimate");
    const auto& expected = *baseline.estimate;
    auto restrictive = ir::ResourceBudget{
        .maximum_cells = expected.grid_cells - 1U,
        .maximum_memory_bytes = expected.estimated_peak_memory_bytes - 1U,
        .maximum_runtime = ir::SiValue{
            .value = expected.runtime_upper_bound_s * 0.5,
            .dimension = ir::SiDimension::time,
        },
        .maximum_storage_bytes = expected.estimated_storage_bytes - 1U,
        .require_escalation_on_exceedance = true,
    };
    const auto exceeded = cost::ReferenceFdtdCostEstimator{}.estimate(
        representative_workload(), calibration, restrictive);
    check(exceeded.succeeded(), "budget exceedance must retain a reviewable estimate");
    check(!exceeded.estimate->within_budget, "exceeded limits must fail budget assessment");
    check(exceeded.estimate->requires_escalation,
        "hard over-budget workload must require escalation");
    check(exceeded.estimate->budget_assessments.size() == 4,
        "all four resource dimensions must be assessed");
    check(std::ranges::all_of(
        exceeded.estimate->budget_assessments,
        [](const auto& assessment) {
            return assessment.exceeded && assessment.utilization > 1.0;
        }), "cell, memory, runtime, and storage limits must each report exceedance");

    restrictive.require_escalation_on_exceedance = false;
    const auto advisory = cost::ReferenceFdtdCostEstimator{}.estimate(
        representative_workload(), calibration, restrictive);
    check(advisory.succeeded() && !advisory.estimate->within_budget,
        "advisory policy must not erase budget exceedance");
    check(!advisory.estimate->requires_escalation,
        "ASIR escalation policy must control the escalation decision");
}

void tiny_features_expose_cubic_grid_and_time_pressure()
{
    const auto calibration = deterministic_calibration();
    auto coarse = cost::ReferenceFdtdWorkload{
        .grid = oep::em_mesh::CartesianGridSpec{
            .origin_m = {0.0, 0.0, 0.0},
            .cell_counts = {40, 30, 20},
            .cell_size_m = {0.001, 0.001, 0.001},
            .maximum_cells = 10'000'000,
        },
        .courant_factor = 0.9,
        .maximum_wave_speed_m_per_s = cost::vacuum_light_speed_m_per_s,
        .simulated_duration_s = 1.0e-9,
        .retained_checkpoint_count = 1,
    };
    const auto probe = cost::ReferenceFdtdCostEstimator{}.estimate(
        coarse, calibration, generous_budget());
    check(probe.succeeded(), "coarse-grid probe must estimate");
    coarse.simulated_duration_s = 99.25 * probe.estimate->time_step_s;
    auto refined = coarse;
    refined.grid.cell_counts = {80, 60, 40};
    refined.grid.cell_size_m = {0.0005, 0.0005, 0.0005};

    const auto coarse_result = cost::ReferenceFdtdCostEstimator{}.estimate(
        coarse, calibration, generous_budget());
    const auto refined_result = cost::ReferenceFdtdCostEstimator{}.estimate(
        refined, calibration, generous_budget());
    check(coarse_result.succeeded() && refined_result.succeeded(),
        "both tiny-feature comparison grids must estimate");
    const auto& first = *coarse_result.estimate;
    const auto& second = *refined_result.estimate;
    check(second.grid_cells == 8U * first.grid_cells,
        "halving uniform cell size in 3-D must create eight times the cells");
    check(second.time_steps == 199 && first.time_steps == 100,
        "halving cell size must nearly double steps for fixed physical duration");
    check(second.field_storage_bytes > 7U * first.field_storage_bytes,
        "staggered field memory must expose near-cubic refinement pressure");
    check(second.cell_updates > 15U * first.cell_updates,
        "tiny-feature refinement must expose roughly sixteen-fold runtime work");
}

void malformed_inputs_and_overflow_are_rejected()
{
    const cost::RuntimeObservation invalid_observation{
        .grid_cells = 1,
        .time_steps = 0,
        .wall_time_s = 0.0,
    };
    const auto invalid_calibration = cost::calibrate_reference_runtime(
        "", "", 0, std::span{&invalid_observation, 1}, 1.0);
    check(!invalid_calibration.succeeded(),
        "invalid calibration metadata and measurement must fail");

    auto invalid_workload = representative_workload();
    invalid_workload.grid.cell_counts[0] = 0;
    auto invalid_budget = generous_budget();
    invalid_budget.maximum_runtime.dimension = ir::SiDimension::frequency;
    const auto invalid = cost::ReferenceFdtdCostEstimator{}.estimate(
        invalid_workload, deterministic_calibration(), invalid_budget);
    check(!invalid.succeeded(), "invalid grid and budget must not produce an estimate");
    check(invalid.diagnostics.size() == 2,
        "independent invalid grid and budget errors must both remain visible");

    auto overflow = representative_workload();
    overflow.grid.cell_counts = {
        std::numeric_limits<std::size_t>::max(), 2, 2};
    const auto overflow_result = cost::ReferenceFdtdCostEstimator{}.estimate(
        overflow, deterministic_calibration(), generous_budget());
    check(!overflow_result.succeeded(), "overflowing field extents must be rejected");
    check(overflow_result.diagnostics.front().code
            == cost::DiagnosticCode::arithmetic_overflow,
        "overflow must retain a typed diagnostic");
}

} // namespace

int main()
{
    exact_field_time_memory_and_storage_counts();
    every_hard_budget_is_visible();
    tiny_features_expose_cubic_grid_and_time_pressure();
    malformed_inputs_and_overflow_are_rejected();
    std::cout << "Reference FDTD cost, budget, calibration, and tiny-feature checks passed\n";
    return EXIT_SUCCESS;
}
