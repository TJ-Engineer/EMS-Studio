// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#include <oep/fdtd/reference_fdtd.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <string>

namespace {

using namespace oep::fdtd;

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

void initialize_periodic_mode(YeeGrid1d& grid, const std::size_t mode)
{
    const auto dispersion = periodic_mode_dispersion(grid.config(), mode);
    check(dispersion.has_value(), "periodic mode must have a numerical dispersion value");
    for (std::size_t node = 0; node < grid.electric_field().size(); ++node) {
        const auto phase = dispersion->wavenumber_rad_per_m
            * grid.electric_position_m(node);
        grid.mutable_electric_field()[node] = std::cos(phase);
    }
    for (std::size_t cell = 0; cell < grid.magnetic_field().size(); ++cell) {
        const auto phase = dispersion->wavenumber_rad_per_m
                * grid.magnetic_position_m(cell)
            + 0.5 * dispersion->angular_frequency_rad_per_s * grid.time_step_s();
        grid.mutable_magnetic_field()[cell] = std::cos(phase)
            / grid.medium_impedance_ohm();
    }
}

void verify_validation_and_dispersion()
{
    const auto invalid = validate(Grid1dConfig{
        .cells = 2,
        .length_m = 0.0,
        .courant_factor = 1.1,
        .left_boundary = BoundaryCondition::periodic,
        .right_boundary = BoundaryCondition::first_order_absorbing,
    });
    check(invalid.size() == 4, "invalid 1-D configuration must retain every issue");

    const Grid1dConfig periodic{
        .cells = 32,
        .length_m = 1.0,
        .courant_factor = 1.0,
        .left_boundary = BoundaryCondition::periodic,
        .right_boundary = BoundaryCondition::periodic,
    };
    const auto dispersion = periodic_mode_dispersion(periodic, 1);
    check(dispersion.has_value(), "stable periodic mode must be accepted");
    check(
        approximately_equal(dispersion->phase_velocity_m_per_s, vacuum_light_speed),
        "one-dimensional Yee dispersion must be exact at unit Courant factor");

    const Grid3dConfig cavity{
        .cells_x = 20,
        .cells_y = 16,
        .cells_z = 12,
        .length_x_m = 0.10,
        .length_y_m = 0.08,
        .length_z_m = 0.06,
    };
    check(validate(cavity).empty(), "valid 3-D grid must pass validation");
    check(cavity_mode_frequency_hz(cavity, CavityMode{1, 1, 1}).has_value(),
        "supported cavity mode must have a numerical eigenfrequency");

    const Grid3dConfig waveguide{
        .cells_x = 24,
        .cells_y = 12,
        .cells_z = 32,
        .length_x_m = 0.02286,
        .length_y_m = 0.01016,
        .length_z_m = 0.04,
    };
    check(
        waveguide_mode_dispersion(waveguide, WaveguideMode{1, 0}, 10.0e9).has_value(),
        "WR90 TE10 mode must propagate at 10 GHz");
    check(
        !waveguide_mode_dispersion(waveguide, WaveguideMode{1, 0}, 6.0e9).has_value(),
        "WR90 TE10 mode must reject an operating point below cutoff");
}

void verify_one_dimensional_sources_boundaries_and_restart()
{
    YeeGrid1d driven(Grid1dConfig{
        .cells = 40,
        .length_m = 1.0,
        .left_boundary = BoundaryCondition::perfect_electric_conductor,
        .right_boundary = BoundaryCondition::first_order_absorbing,
    });
    const ElectricSource1d source{
        .node = 10,
        .value_v_per_m = 1.0,
        .mode = SourceMode::soft,
    };
    check(driven.step(std::span{&source, 1}).succeeded(),
        "valid soft source must advance the grid");
    check(driven.sample(ElectricProbe1d{10}).value_or(0.0) == 1.0,
        "probe must return the sourced electric sample");
    check(driven.electric_field().front() == 0.0,
        "PEC boundary must keep its tangential electric field zero");
    const ElectricSource1d invalid_source{
        .node = 0,
        .value_v_per_m = 1.0,
        .mode = SourceMode::hard,
    };
    const auto rejected = driven.step(std::span{&invalid_source, 1});
    check(!rejected.succeeded(), "source on a PEC node must be rejected");
    check(driven.step_index() == 1, "rejected source must not advance the grid");

    YeeGrid1d uninterrupted(Grid1dConfig{
        .cells = 48,
        .length_m = 1.0,
        .courant_factor = 0.9,
        .left_boundary = BoundaryCondition::periodic,
        .right_boundary = BoundaryCondition::periodic,
    });
    initialize_periodic_mode(uninterrupted, 2);
    for (int step = 0; step < 11; ++step) {
        check(uninterrupted.step().succeeded(), "periodic grid step must succeed");
    }
    const auto encoded = uninterrupted.checkpoint();
    auto restored = restore_yee_grid_1d(encoded);
    check(restored.succeeded(), "valid 1-D checkpoint must restore");
    for (int step = 0; step < 17; ++step) {
        check(uninterrupted.step().succeeded(), "uninterrupted grid step must succeed");
        check(restored.grid->step().succeeded(), "restored grid step must succeed");
    }
    check(
        std::ranges::equal(
            uninterrupted.electric_field(), restored.grid->electric_field()),
        "1-D restart must reproduce electric fields exactly");
    check(
        std::ranges::equal(
            uninterrupted.magnetic_field(), restored.grid->magnetic_field()),
        "1-D restart must reproduce magnetic fields exactly");

    auto corrupted = encoded;
    corrupted[20] ^= std::byte{0x01};
    const auto corrupt_result = restore_yee_grid_1d(corrupted);
    check(!corrupt_result.succeeded(), "corrupted checkpoint must fail");
    check(
        corrupt_result.diagnostics.front().code
            == DiagnosticCode::checkpoint_checksum_mismatch,
        "corrupted checkpoint must report its checksum failure");
}

void verify_three_dimensional_update_and_restart()
{
    YeeGrid3d uninterrupted(Grid3dConfig{
        .cells_x = 5,
        .cells_y = 4,
        .cells_z = 3,
        .length_x_m = 0.05,
        .length_y_m = 0.04,
        .length_z_m = 0.03,
        .courant_factor = 0.8,
    });
    check(uninterrupted.set(FieldComponent::electric_z, {2, 2, 1}, 1.0),
        "interior electric field must be settable");
    uninterrupted.center_magnetic_half_step_from_electric();
    for (int step = 0; step < 4; ++step) {
        check(uninterrupted.step().succeeded(), "3-D PEC grid step must succeed");
    }
    check(uninterrupted.last_discrete_energy_j().has_value(),
        "3-D update must expose discrete energy");
    const auto encoded = uninterrupted.checkpoint();
    auto restored = restore_yee_grid_3d(encoded);
    check(restored.succeeded(), "valid 3-D checkpoint must restore");
    for (int step = 0; step < 8; ++step) {
        check(uninterrupted.step().succeeded(), "uninterrupted 3-D step must succeed");
        check(restored.grid->step().succeeded(), "restored 3-D step must succeed");
    }
    for (const auto component : {
             FieldComponent::electric_x,
             FieldComponent::electric_y,
             FieldComponent::electric_z,
             FieldComponent::magnetic_x,
             FieldComponent::magnetic_y,
             FieldComponent::magnetic_z,
         }) {
        check(
            std::ranges::equal(
                uninterrupted.field(component), restored.grid->field(component)),
            "3-D restart must reproduce every staggered field exactly");
    }
    check(uninterrupted.step_index() == restored.grid->step_index(),
        "3-D restart must preserve the time-step index");

    const ElectricSource3d boundary_source{
        .component = FieldComponent::electric_z,
        .index = {0, 2, 1},
        .value_v_per_m = 1.0,
        .mode = SourceMode::hard,
    };
    check(!uninterrupted.step(std::span{&boundary_source, 1}).succeeded(),
        "source on a 3-D PEC tangential field must fail");

    const auto wrong_kind = restore_yee_grid_3d(
        YeeGrid1d(Grid1dConfig{
            .cells = 8,
            .length_m = 1.0,
            .left_boundary = BoundaryCondition::periodic,
            .right_boundary = BoundaryCondition::periodic,
        }).checkpoint());
    check(!wrong_kind.succeeded(), "wrong-dimensional checkpoint must fail");
    check(
        wrong_kind.diagnostics.front().code == DiagnosticCode::checkpoint_kind_mismatch,
        "wrong-dimensional checkpoint must retain a typed issue");
}

} // namespace

int main()
{
    verify_validation_and_dispersion();
    verify_one_dimensional_sources_boundaries_and_restart();
    verify_three_dimensional_update_and_restart();
    std::cout << "Reference FDTD validation, update, boundary, source, probe, and restart checks passed\n";
    return EXIT_SUCCESS;
}
