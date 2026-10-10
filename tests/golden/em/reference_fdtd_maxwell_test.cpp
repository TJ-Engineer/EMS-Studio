// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#include <oep/fdtd/reference_fdtd.hpp>
#include <oep/test/maxwell_harness.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <span>
#include <string>
#include <vector>

namespace {

namespace fdtd = oep::fdtd;
namespace maxwell = oep::test::maxwell;

void check(const bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

[[nodiscard]] double metric_value(
    const maxwell::ScalarMetricResult& metric,
    const std::string& context)
{
    check(metric.succeeded(), context + " metric calculation must succeed");
    return *metric.value;
}

[[nodiscard]] std::vector<double> concatenate_electric(const fdtd::YeeGrid3d& grid)
{
    std::vector<double> values;
    for (const auto component : {
             fdtd::FieldComponent::electric_x,
             fdtd::FieldComponent::electric_y,
             fdtd::FieldComponent::electric_z,
         }) {
        const auto field = grid.field(component);
        values.insert(values.end(), field.begin(), field.end());
    }
    return values;
}

[[nodiscard]] std::vector<double> concatenate_all_fields(const fdtd::YeeGrid3d& grid)
{
    std::vector<double> values;
    for (const auto component : {
             fdtd::FieldComponent::electric_x,
             fdtd::FieldComponent::electric_y,
             fdtd::FieldComponent::electric_z,
             fdtd::FieldComponent::magnetic_x,
             fdtd::FieldComponent::magnetic_y,
             fdtd::FieldComponent::magnetic_z,
         }) {
        const auto field = grid.field(component);
        values.insert(values.end(), field.begin(), field.end());
    }
    return values;
}

[[nodiscard]] double scaled_shape_error(
    const std::span<const double> observed,
    const std::span<const double> reference)
{
    check(observed.size() == reference.size(), "field-shape vectors must have equal size");
    auto cross = 0.0;
    auto reference_norm = 0.0;
    for (std::size_t index = 0; index < observed.size(); ++index) {
        cross += observed[index] * reference[index];
        reference_norm += reference[index] * reference[index];
    }
    check(reference_norm > 0.0, "field-shape reference must be nonzero");
    const auto scale = cross / reference_norm;
    std::vector<double> scaled(reference.begin(), reference.end());
    for (auto& value : scaled) {
        value *= scale;
    }
    return metric_value(
        maxwell::relative_l2_error(observed, scaled), "field L2");
}

[[nodiscard]] double restart_error(
    const fdtd::YeeGrid3d& uninterrupted,
    const fdtd::YeeGrid3d& restored)
{
    const auto first = concatenate_all_fields(uninterrupted);
    const auto second = concatenate_all_fields(restored);
    return metric_value(maxwell::linf_absolute_error(first, second), "3-D restart");
}

void initialize_periodic_plane_wave(
    fdtd::YeeGrid1d& grid,
    const fdtd::Dispersion1d& dispersion)
{
    for (std::size_t node = 0; node < grid.electric_field().size(); ++node) {
        const auto phase = dispersion.wavenumber_rad_per_m
            * grid.electric_position_m(node);
        grid.mutable_electric_field()[node] = std::cos(phase);
    }
    for (std::size_t cell = 0; cell < grid.magnetic_field().size(); ++cell) {
        const auto phase = dispersion.wavenumber_rad_per_m
                * grid.magnetic_position_m(cell)
            + 0.5 * dispersion.angular_frequency_rad_per_s * grid.time_step_s();
        grid.mutable_magnetic_field()[cell] = std::cos(phase)
            / grid.medium_impedance_ohm();
    }
}

[[nodiscard]] double open_boundary_reflection(
    const maxwell::PlaneWaveReference& reference)
{
    const auto wavelength = reference.wavelength_m;
    fdtd::YeeGrid1d grid(fdtd::Grid1dConfig{
        .cells = 400,
        .length_m = 10.0 * wavelength,
        .courant_factor = 0.99,
        .left_boundary = fdtd::BoundaryCondition::first_order_absorbing,
        .right_boundary = fdtd::BoundaryCondition::first_order_absorbing,
    });
    const auto center = 2.0 * wavelength;
    const auto width = 0.35 * wavelength;
    const auto pulse = [center, width](const double position) {
        const auto normalized = (position - center) / width;
        return std::exp(-(normalized * normalized));
    };
    for (std::size_t node = 0; node < grid.electric_field().size(); ++node) {
        grid.mutable_electric_field()[node] = pulse(grid.electric_position_m(node));
    }
    for (std::size_t cell = 0; cell < grid.magnetic_field().size(); ++cell) {
        const auto centered_position = grid.magnetic_position_m(cell)
            + 0.5 * grid.medium_velocity_m_per_s() * grid.time_step_s();
        grid.mutable_magnetic_field()[cell] = pulse(centered_position)
            / grid.medium_impedance_ohm();
    }

    const auto probe_node = static_cast<std::size_t>(
        std::llround(5.0 * wavelength / grid.cell_size_m()));
    auto incident_peak = 0.0;
    auto reflected_peak = 0.0;
    const auto period = wavelength / grid.medium_velocity_m_per_s();
    const auto final_step = static_cast<std::size_t>(
        std::ceil(17.0 * period / grid.time_step_s()));
    for (std::size_t step = 0; step < final_step; ++step) {
        check(grid.step().succeeded(), "open-boundary pulse step must succeed");
        const auto sample = std::abs(*grid.sample(fdtd::ElectricProbe1d{probe_node}));
        if (grid.time_s() < 7.0 * period) {
            incident_peak = std::max(incident_peak, sample);
        } else if (grid.time_s() > 10.0 * period) {
            reflected_peak = std::max(reflected_peak, sample);
        }
    }
    check(incident_peak > 0.5, "incident pulse must cross the open-boundary probe");
    return reflected_peak / incident_peak;
}

[[nodiscard]] std::vector<maxwell::MetricObservation> verify_plane_wave(
    const maxwell::VerificationCase& test_case)
{
    const auto& problem = std::get<maxwell::PlaneWaveRequest>(test_case.problem);
    const auto& reference = std::get<maxwell::PlaneWaveReference>(
        test_case.analytic_reference);
    const auto make_config = [&problem, &reference](const std::size_t cells) {
        return fdtd::Grid1dConfig{
            .cells = cells,
            .length_m = reference.wavelength_m,
            .relative_permittivity = problem.relative_permittivity,
            .relative_permeability = problem.relative_permeability,
            .courant_factor = 0.9,
            .left_boundary = fdtd::BoundaryCondition::periodic,
            .right_boundary = fdtd::BoundaryCondition::periodic,
        };
    };
    const auto coarse = fdtd::periodic_mode_dispersion(make_config(12), 1);
    const auto fine = fdtd::periodic_mode_dispersion(make_config(24), 1);
    check(coarse.has_value() && fine.has_value(), "plane-wave dispersion must be available");
    const auto coarse_error = metric_value(
        maxwell::relative_error(
            coarse->phase_velocity_m_per_s, reference.phase_velocity_m_per_s),
        "coarse plane-wave phase velocity");
    const auto phase_error = metric_value(
        maxwell::relative_error(
            fine->phase_velocity_m_per_s, reference.phase_velocity_m_per_s),
        "fine plane-wave phase velocity");
    const auto order = metric_value(
        maxwell::observed_convergence_order(coarse_error, phase_error, 2.0),
        "plane-wave convergence");

    fdtd::YeeGrid1d uninterrupted(make_config(24));
    initialize_periodic_plane_wave(uninterrupted, *fine);
    const auto total_steps = static_cast<std::size_t>(std::llround(
        (1.0 / problem.frequency_hz) / uninterrupted.time_step_s()));
    const auto checkpoint_step = total_steps / 2U;
    std::optional<double> initial_energy;
    std::optional<fdtd::YeeGrid1d> restored;
    for (std::size_t step = 0; step < total_steps; ++step) {
        check(uninterrupted.step().succeeded(), "periodic plane-wave step must succeed");
        if (!initial_energy.has_value()) {
            initial_energy = uninterrupted.last_discrete_energy_j();
        }
        if (step + 1U == checkpoint_step) {
            auto result = fdtd::restore_yee_grid_1d(uninterrupted.checkpoint());
            check(result.succeeded(), "plane-wave checkpoint must restore");
            restored = std::move(*result.grid);
        } else if (restored.has_value() && step + 1U > checkpoint_step) {
            check(restored->step().succeeded(), "restored plane-wave step must succeed");
        }
    }
    check(restored.has_value(), "plane-wave restart branch must be created");

    std::vector<double> expected(uninterrupted.electric_field().size());
    for (std::size_t node = 0; node < expected.size(); ++node) {
        expected[node] = std::cos(
            fine->wavenumber_rad_per_m * uninterrupted.electric_position_m(node)
            - 2.0 * std::numbers::pi * problem.frequency_hz * uninterrupted.time_s());
    }
    const auto field_error = metric_value(
        maxwell::relative_l2_error(uninterrupted.electric_field(), expected),
        "plane-wave field");
    const auto energy_error = metric_value(
        maxwell::normalized_energy_residual(
            *initial_energy,
            0.0,
            *uninterrupted.last_discrete_energy_j(),
            0.0,
            0.0),
        "plane-wave energy");
    const auto restart = metric_value(
        maxwell::linf_absolute_error(
            uninterrupted.electric_field(), restored->electric_field()),
        "plane-wave restart");
    const auto magnetic_restart = metric_value(
        maxwell::linf_absolute_error(
            uninterrupted.magnetic_field(), restored->magnetic_field()),
        "plane-wave magnetic restart");

    return {
        {maxwell::MetricKind::phase_velocity_relative_error, phase_error},
        {maxwell::MetricKind::field_l2_relative_error, field_error},
        {maxwell::MetricKind::open_boundary_reflection_absolute,
            open_boundary_reflection(reference)},
        {maxwell::MetricKind::energy_conservation_relative_residual, energy_error},
        {maxwell::MetricKind::observed_convergence_order, order},
        {maxwell::MetricKind::restart_linf_absolute_error,
            std::max(restart, magnetic_restart)},
    };
}

[[nodiscard]] std::vector<double> initialize_tm111_cavity(fdtd::YeeGrid3d& grid)
{
    const auto kx = std::numbers::pi / grid.config().length_x_m;
    const auto ky = std::numbers::pi / grid.config().length_y_m;
    const auto kz = std::numbers::pi / grid.config().length_z_m;
    const auto transverse_squared = kx * kx + ky * ky;
    std::vector<double> reference;
    for (const auto component : {
             fdtd::FieldComponent::electric_x,
             fdtd::FieldComponent::electric_y,
             fdtd::FieldComponent::electric_z,
         }) {
        const auto extent = grid.extent(component);
        for (std::size_t i = 0; i < extent.i; ++i) {
            for (std::size_t j = 0; j < extent.j; ++j) {
                for (std::size_t k = 0; k < extent.k; ++k) {
                    const auto index = fdtd::Index3d{i, j, k};
                    const auto point = grid.position(component, index);
                    auto value = 0.0;
                    if (component == fdtd::FieldComponent::electric_x) {
                        value = -(kx * kz / transverse_squared)
                            * std::cos(kx * point.x_m)
                            * std::sin(ky * point.y_m)
                            * std::sin(kz * point.z_m);
                    } else if (component == fdtd::FieldComponent::electric_y) {
                        value = -(ky * kz / transverse_squared)
                            * std::sin(kx * point.x_m)
                            * std::cos(ky * point.y_m)
                            * std::sin(kz * point.z_m);
                    } else {
                        value = std::sin(kx * point.x_m)
                            * std::sin(ky * point.y_m)
                            * std::cos(kz * point.z_m);
                    }
                    check(grid.set(component, index, value), "TM111 sample must initialize");
                    reference.push_back(value);
                }
            }
        }
    }
    grid.center_magnetic_half_step_from_electric();
    return reference;
}

[[nodiscard]] double frequency_from_zero_crossings(
    const std::span<const double> crossing_times)
{
    check(crossing_times.size() >= 6, "mode trace must contain at least six zero crossings");
    return static_cast<double>(crossing_times.size() - 1U)
        / (2.0 * (crossing_times.back() - crossing_times.front()));
}

[[nodiscard]] std::vector<maxwell::MetricObservation> verify_cavity(
    const maxwell::VerificationCase& test_case)
{
    const auto& problem = std::get<maxwell::CavityRequest>(test_case.problem);
    const auto& reference = std::get<maxwell::CavityReference>(
        test_case.analytic_reference);
    const auto make_config = [&problem](const std::size_t scale) {
        return fdtd::Grid3dConfig{
            .cells_x = 5U * scale,
            .cells_y = 4U * scale,
            .cells_z = 3U * scale,
            .length_x_m = problem.length_x_m,
            .length_y_m = problem.length_y_m,
            .length_z_m = problem.length_z_m,
            .relative_permittivity = problem.relative_permittivity,
            .relative_permeability = problem.relative_permeability,
            .courant_factor = 0.9,
        };
    };
    const auto coarse_frequency = fdtd::cavity_mode_frequency_hz(
        make_config(1), {problem.mode.m, problem.mode.n, problem.mode.p});
    const auto fine_frequency = fdtd::cavity_mode_frequency_hz(
        make_config(2), {problem.mode.m, problem.mode.n, problem.mode.p});
    check(coarse_frequency.has_value() && fine_frequency.has_value(),
        "TM111 numerical eigenfrequencies must be available");
    const auto coarse_error = metric_value(
        maxwell::relative_error(*coarse_frequency, reference.resonant_frequency_hz),
        "coarse cavity frequency");
    const auto frequency_error = metric_value(
        maxwell::relative_error(*fine_frequency, reference.resonant_frequency_hz),
        "fine cavity frequency");
    const auto order = metric_value(
        maxwell::observed_convergence_order(coarse_error, frequency_error, 2.0),
        "cavity convergence");

    fdtd::YeeGrid3d uninterrupted(make_config(2));
    const auto expected_shape = initialize_tm111_cavity(uninterrupted);
    const auto total_steps = static_cast<std::size_t>(std::llround(
        8.0 / (*fine_frequency * uninterrupted.time_step_s())));
    const auto checkpoint_step = total_steps / 2U;
    const fdtd::FieldProbe3d probe{
        .component = fdtd::FieldComponent::electric_z,
        .index = {uninterrupted.config().cells_x / 2U,
            uninterrupted.config().cells_y / 2U,
            0},
    };
    auto previous_value = *uninterrupted.sample(probe);
    auto previous_time = uninterrupted.time_s();
    std::vector<double> crossings;
    std::optional<double> initial_energy;
    std::optional<fdtd::YeeGrid3d> restored;
    for (std::size_t step = 0; step < total_steps; ++step) {
        check(uninterrupted.step().succeeded(), "TM111 cavity step must succeed");
        if (!initial_energy.has_value()) {
            initial_energy = uninterrupted.last_discrete_energy_j();
        }
        const auto value = *uninterrupted.sample(probe);
        if ((previous_value < 0.0 && value >= 0.0)
            || (previous_value > 0.0 && value <= 0.0)) {
            const auto fraction = std::abs(previous_value)
                / (std::abs(previous_value) + std::abs(value));
            crossings.push_back(previous_time + fraction * uninterrupted.time_step_s());
        }
        previous_value = value;
        previous_time = uninterrupted.time_s();
        if (step + 1U == checkpoint_step) {
            auto result = fdtd::restore_yee_grid_3d(uninterrupted.checkpoint());
            check(result.succeeded(), "cavity checkpoint must restore");
            restored = std::move(*result.grid);
        } else if (restored.has_value() && step + 1U > checkpoint_step) {
            check(restored->step().succeeded(), "restored cavity step must succeed");
        }
    }
    check(restored.has_value(), "cavity restart branch must be created");
    const auto measured_frequency = frequency_from_zero_crossings(crossings);
    const auto measured_prediction_error = metric_value(
        maxwell::relative_error(measured_frequency, *fine_frequency),
        "measured cavity stencil frequency");
    check(measured_prediction_error < 0.005,
        "time-stepped cavity frequency must match the Yee dispersion prediction");

    const auto field_error = scaled_shape_error(
        concatenate_electric(uninterrupted), expected_shape);
    const auto energy_error = metric_value(
        maxwell::normalized_energy_residual(
            *initial_energy,
            0.0,
            *uninterrupted.last_discrete_energy_j(),
            0.0,
            0.0),
        "cavity energy");
    return {
        {maxwell::MetricKind::resonant_frequency_relative_error, frequency_error},
        {maxwell::MetricKind::field_l2_relative_error, field_error},
        {maxwell::MetricKind::energy_conservation_relative_residual, energy_error},
        {maxwell::MetricKind::observed_convergence_order, order},
        {maxwell::MetricKind::restart_linf_absolute_error,
            restart_error(uninterrupted, *restored)},
    };
}

[[nodiscard]] std::vector<double> initialize_te101_waveguide(fdtd::YeeGrid3d& grid)
{
    const auto kx = std::numbers::pi / grid.config().length_x_m;
    const auto kz = std::numbers::pi / grid.config().length_z_m;
    std::vector<double> reference;
    for (const auto component : {
             fdtd::FieldComponent::electric_x,
             fdtd::FieldComponent::electric_y,
             fdtd::FieldComponent::electric_z,
         }) {
        const auto extent = grid.extent(component);
        for (std::size_t i = 0; i < extent.i; ++i) {
            for (std::size_t j = 0; j < extent.j; ++j) {
                for (std::size_t k = 0; k < extent.k; ++k) {
                    const auto index = fdtd::Index3d{i, j, k};
                    const auto point = grid.position(component, index);
                    const auto value = component == fdtd::FieldComponent::electric_y
                        ? std::sin(kx * point.x_m) * std::sin(kz * point.z_m)
                        : 0.0;
                    check(grid.set(component, index, value), "TE101 sample must initialize");
                    reference.push_back(value);
                }
            }
        }
    }
    grid.center_magnetic_half_step_from_electric();
    return reference;
}

[[nodiscard]] std::vector<maxwell::MetricObservation> verify_waveguide(
    const maxwell::VerificationCase& test_case)
{
    const auto& problem = std::get<maxwell::WaveguideRequest>(test_case.problem);
    const auto& reference = std::get<maxwell::WaveguideReference>(
        test_case.analytic_reference);
    const auto guide_length = std::numbers::pi / reference.propagation_constant_rad_per_m;
    const auto make_config = [&problem, guide_length](const std::size_t scale) {
        return fdtd::Grid3dConfig{
            .cells_x = 6U * scale,
            .cells_y = 3U * scale,
            .cells_z = 10U * scale,
            .length_x_m = problem.width_m,
            .length_y_m = problem.height_m,
            .length_z_m = guide_length,
            .relative_permittivity = problem.relative_permittivity,
            .relative_permeability = problem.relative_permeability,
            .courant_factor = 0.9,
        };
    };
    const auto coarse = fdtd::waveguide_mode_dispersion(
        make_config(1), {problem.mode.m, problem.mode.n}, problem.operating_frequency_hz);
    const auto fine = fdtd::waveguide_mode_dispersion(
        make_config(2), {problem.mode.m, problem.mode.n}, problem.operating_frequency_hz);
    check(coarse.has_value() && fine.has_value(),
        "WR90 numerical dispersion values must be available");
    const auto coarse_propagation_error = metric_value(
        maxwell::relative_error(
            coarse->propagation_constant_rad_per_m,
            reference.propagation_constant_rad_per_m),
        "coarse waveguide propagation");
    const auto propagation_error = metric_value(
        maxwell::relative_error(
            fine->propagation_constant_rad_per_m,
            reference.propagation_constant_rad_per_m),
        "fine waveguide propagation");
    const auto cutoff_error = metric_value(
        maxwell::relative_error(
            fine->cutoff_frequency_hz, reference.cutoff_frequency_hz),
        "waveguide cutoff");
    const auto order = metric_value(
        maxwell::observed_convergence_order(
            coarse_propagation_error, propagation_error, 2.0),
        "waveguide convergence");

    fdtd::YeeGrid3d uninterrupted(make_config(2));
    const auto expected_shape = initialize_te101_waveguide(uninterrupted);
    const auto mode_frequency = fdtd::cavity_mode_frequency_hz(
        uninterrupted.config(), {problem.mode.m, problem.mode.n, 1});
    check(mode_frequency.has_value(), "TE101 standing mode frequency must be available");
    const auto total_steps = static_cast<std::size_t>(std::llround(
        8.0 / (*mode_frequency * uninterrupted.time_step_s())));
    const auto checkpoint_step = total_steps / 2U;
    std::optional<double> initial_energy;
    std::optional<fdtd::YeeGrid3d> restored;
    for (std::size_t step = 0; step < total_steps; ++step) {
        check(uninterrupted.step().succeeded(), "TE101 waveguide step must succeed");
        if (!initial_energy.has_value()) {
            initial_energy = uninterrupted.last_discrete_energy_j();
        }
        if (step + 1U == checkpoint_step) {
            auto result = fdtd::restore_yee_grid_3d(uninterrupted.checkpoint());
            check(result.succeeded(), "waveguide checkpoint must restore");
            restored = std::move(*result.grid);
        } else if (restored.has_value() && step + 1U > checkpoint_step) {
            check(restored->step().succeeded(), "restored waveguide step must succeed");
        }
    }
    check(restored.has_value(), "waveguide restart branch must be created");
    const auto field_error = scaled_shape_error(
        concatenate_electric(uninterrupted), expected_shape);
    const auto energy_error = metric_value(
        maxwell::normalized_energy_residual(
            *initial_energy,
            0.0,
            *uninterrupted.last_discrete_energy_j(),
            0.0,
            0.0),
        "waveguide energy");
    return {
        {maxwell::MetricKind::cutoff_frequency_relative_error, cutoff_error},
        {maxwell::MetricKind::propagation_constant_relative_error, propagation_error},
        {maxwell::MetricKind::field_l2_relative_error, field_error},
        {maxwell::MetricKind::energy_conservation_relative_residual, energy_error},
        {maxwell::MetricKind::observed_convergence_order, order},
        {maxwell::MetricKind::restart_linf_absolute_error,
            restart_error(uninterrupted, *restored)},
    };
}

void print_observations(
    const maxwell::VerificationCase& test_case,
    const std::span<const maxwell::MetricObservation> observations)
{
    std::cout << test_case.id;
    for (const auto& observation : observations) {
        std::cout << ' ' << static_cast<int>(observation.metric) << '='
                  << std::scientific << std::setprecision(6) << observation.value;
    }
    std::cout << '\n';
}

} // namespace

int main()
{
    const auto cases = maxwell::canonical_verification_cases();
    for (const auto& test_case : cases) {
        std::vector<maxwell::MetricObservation> observations;
        switch (test_case.kind) {
        case maxwell::VerificationCaseKind::plane_wave_1d:
            observations = verify_plane_wave(test_case);
            break;
        case maxwell::VerificationCaseKind::pec_rectangular_cavity:
            observations = verify_cavity(test_case);
            break;
        case maxwell::VerificationCaseKind::rectangular_waveguide:
            observations = verify_waveguide(test_case);
            break;
        }
        print_observations(test_case, observations);
        const auto assessment = maxwell::assess(test_case, observations);
        for (const auto& issue : assessment.issues) {
            std::cerr << "FAIL: " << assessment.case_id << " metric "
                      << static_cast<int>(issue.metric) << ": " << issue.message << '\n';
        }
        check(assessment.passed(), test_case.id + " must satisfy its frozen Maxwell contract");
    }
    std::cout << "Reference FP64 Yee solver passed all canonical Maxwell cases\n";
    return EXIT_SUCCESS;
}
