// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#include <oep/fdtd/reference_fdtd.hpp>
#include <oep/simulation_cost/simulation_cost.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

namespace cost = oep::simulation_cost;
namespace fdtd = oep::fdtd;
namespace ir = oep::simulation_ir;

using Clock = std::chrono::steady_clock;

void check(const bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

struct MeasuredRun {
    fdtd::Grid3dConfig config;
    cost::RuntimeObservation observation;
    std::uint64_t field_bytes{};
    std::uint64_t checkpoint_bytes{};
};

[[nodiscard]] MeasuredRun measure_reference_run(
    const std::array<std::size_t, 3> counts)
{
    constexpr auto cell_size_m = 0.001;
    const auto config = fdtd::Grid3dConfig{
        .cells_x = counts[0],
        .cells_y = counts[1],
        .cells_z = counts[2],
        .length_x_m = static_cast<double>(counts[0]) * cell_size_m,
        .length_y_m = static_cast<double>(counts[1]) * cell_size_m,
        .length_z_m = static_cast<double>(counts[2]) * cell_size_m,
        .courant_factor = 0.8,
    };
    fdtd::YeeGrid3d grid(config);
    auto steps = std::uint64_t{};
    const auto started = Clock::now();
    auto elapsed_s = 0.0;
    do {
        check(grid.step().succeeded(), "measured reference step must succeed");
        ++steps;
        elapsed_s = std::chrono::duration<double>(Clock::now() - started).count();
    } while (elapsed_s < 0.025 && steps < 2'000);
    check(elapsed_s > 0.0 && steps > 0,
        "steady clock must measure positive reference-solver work");

    auto field_values = std::uint64_t{};
    for (const auto component : {
             fdtd::FieldComponent::electric_x,
             fdtd::FieldComponent::electric_y,
             fdtd::FieldComponent::electric_z,
             fdtd::FieldComponent::magnetic_x,
             fdtd::FieldComponent::magnetic_y,
             fdtd::FieldComponent::magnetic_z,
         }) {
        field_values += static_cast<std::uint64_t>(grid.field(component).size());
    }
    const auto cells = static_cast<std::uint64_t>(counts[0])
        * static_cast<std::uint64_t>(counts[1])
        * static_cast<std::uint64_t>(counts[2]);
    return MeasuredRun{
        .config = config,
        .observation = cost::RuntimeObservation{
            .grid_cells = cells,
            .time_steps = steps,
            .wall_time_s = elapsed_s,
        },
        .field_bytes = field_values * sizeof(double),
        .checkpoint_bytes = static_cast<std::uint64_t>(grid.checkpoint().size()),
    };
}

[[nodiscard]] cost::ReferenceFdtdWorkload workload_for(
    const MeasuredRun& run)
{
    const auto steps = run.observation.time_steps;
    return cost::ReferenceFdtdWorkload{
        .grid = oep::em_mesh::CartesianGridSpec{
            .origin_m = {0.0, 0.0, 0.0},
            .cell_counts = {
                run.config.cells_x, run.config.cells_y, run.config.cells_z},
            .cell_size_m = {
                run.config.length_x_m / static_cast<double>(run.config.cells_x),
                run.config.length_y_m / static_cast<double>(run.config.cells_y),
                run.config.length_z_m / static_cast<double>(run.config.cells_z),
            },
            .maximum_cells = 100'000'000,
        },
        .courant_factor = run.config.courant_factor,
        .maximum_wave_speed_m_per_s = fdtd::vacuum_light_speed,
        .simulated_duration_s = (static_cast<double>(steps) - 0.25)
            * (run.config.courant_factor
                / (fdtd::vacuum_light_speed * std::sqrt(3.0e6))),
        .retained_checkpoint_count = 1,
        .result_storage_bytes = 0,
    };
}

[[nodiscard]] ir::ResourceBudget calibration_budget()
{
    return ir::ResourceBudget{
        .maximum_cells = 100'000'000,
        .maximum_memory_bytes = 8ULL * 1024ULL * 1024ULL * 1024ULL,
        .maximum_runtime = ir::SiValue{
            .value = 3'600.0,
            .dimension = ir::SiDimension::time,
        },
        .maximum_storage_bytes = 8ULL * 1024ULL * 1024ULL * 1024ULL,
        .require_escalation_on_exceedance = true,
    };
}

} // namespace

int main()
{
    const auto first = measure_reference_run({20, 16, 12});
    const auto second = measure_reference_run({24, 18, 14});
    const cost::RuntimeObservation observations[]{
        first.observation,
        second.observation,
    };
    const auto calibrated = cost::calibrate_reference_runtime(
        "current-test-host/reference-cpu/single-thread",
        "oep-reference-fdtd-0.1-debug",
        1,
        observations,
        0.15);
    check(calibrated.succeeded(),
        "measured reference runs must produce a named hardware calibration");

    const auto holdout = measure_reference_run({22, 17, 13});
    const auto estimated = cost::ReferenceFdtdCostEstimator{}.estimate(
        workload_for(holdout), *calibrated.calibration, calibration_budget());
    check(estimated.succeeded(), "measured holdout workload must estimate");
    const auto& value = *estimated.estimate;
    check(value.time_steps == holdout.observation.time_steps,
        "estimated and measured holdout step counts must match");
    check(value.field_storage_bytes == holdout.field_bytes,
        "estimated FP64 staggered fields must match allocated solver payload");
    check(value.checkpoint_bytes == holdout.checkpoint_bytes,
        "estimated checkpoint bytes must match the serialized solver checkpoint");

    const auto runtime_ratio = value.estimated_runtime_s
        / holdout.observation.wall_time_s;
    check(runtime_ratio >= 0.35 && runtime_ratio <= 2.85,
        "holdout wall time must remain within the explicit measured regression factor");
    check(value.within_budget && !value.requires_escalation,
        "measured holdout must fit the calibration acceptance budget");

    std::cout << std::fixed << std::setprecision(3)
              << "Reference cost calibration passed: "
              << calibrated.calibration->cell_updates_per_second / 1.0e6
              << " Mcell-updates/s, uncertainty ±"
              << 100.0 * calibrated.calibration->relative_prediction_uncertainty
              << "%, holdout predicted/actual " << runtime_ratio << '\n';
    return EXIT_SUCCESS;
}
