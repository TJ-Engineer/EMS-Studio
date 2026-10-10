// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#include <oep/test/port_contract_harness.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <set>
#include <utility>

namespace oep::test::port_contract {
namespace {

[[nodiscard]] bool finite(const double value)
{
    return std::isfinite(value);
}

[[nodiscard]] bool finite(const Phasor value)
{
    return finite(value.real()) && finite(value.imag());
}

[[nodiscard]] bool finite(const std::span<const Phasor> values)
{
    return std::ranges::all_of(values, [](const Phasor value) { return finite(value); });
}

[[nodiscard]] Diagnostic diagnostic(const ErrorCode code, std::string message)
{
    return Diagnostic{.code = code, .message = std::move(message)};
}

template <typename Value>
[[nodiscard]] Result<Value> failure(const ErrorCode code, std::string message)
{
    return Result<Value>{
        .value = std::nullopt,
        .diagnostics = {diagnostic(code, std::move(message))},
    };
}

[[nodiscard]] bool valid_matrix_shape(const ComplexMatrix& matrix)
{
    if (matrix.rows == 0 || matrix.columns == 0) {
        return false;
    }
    if (matrix.rows > std::numeric_limits<std::size_t>::max() / matrix.columns) {
        return false;
    }
    return matrix.values.size() == matrix.rows * matrix.columns;
}

[[nodiscard]] bool square_matrix(const ComplexMatrix& matrix)
{
    return valid_matrix_shape(matrix) && matrix.rows == matrix.columns;
}

[[nodiscard]] PhasorVector multiply(
    const ComplexMatrix& matrix,
    const std::span<const Phasor> vector)
{
    PhasorVector result(matrix.rows);
    for (std::size_t row = 0; row < matrix.rows; ++row) {
        for (std::size_t column = 0; column < matrix.columns; ++column) {
            result[row] += matrix.at(row, column) * vector[column];
        }
    }
    return result;
}

[[nodiscard]] ComplexMatrix matrix(
    const std::size_t rows,
    const std::size_t columns,
    PhasorVector values)
{
    return ComplexMatrix{
        .rows = rows,
        .columns = columns,
        .values = std::move(values),
    };
}

[[nodiscard]] std::vector<BasisRun> basis_runs(
    const ComplexMatrix& scattering,
    const ComplexMatrix& fields)
{
    std::vector<BasisRun> runs;
    runs.reserve(scattering.columns);
    for (std::size_t driven = 0; driven < scattering.columns; ++driven) {
        BasisRun run{
            .driven_port = driven,
            .incident_waves_sqrt_w = PhasorVector(scattering.columns),
            .outgoing_waves_sqrt_w = PhasorVector(scattering.rows),
            .normalized_far_field_v_per_m_per_sqrt_w = PhasorVector(fields.rows),
        };
        run.incident_waves_sqrt_w[driven] = Phasor{1.0, 0.0};
        for (std::size_t port = 0; port < scattering.rows; ++port) {
            run.outgoing_waves_sqrt_w[port] = scattering.at(port, driven);
        }
        for (std::size_t sample = 0; sample < fields.rows; ++sample) {
            run.normalized_far_field_v_per_m_per_sqrt_w[sample]
                = fields.at(sample, driven);
        }
        runs.push_back(std::move(run));
    }
    return runs;
}

[[nodiscard]] DirectExcitationCase direct_case(
    std::string id,
    PhasorVector incident,
    PhasorVector outgoing,
    PhasorVector fields)
{
    return DirectExcitationCase{
        .id = std::move(id),
        .incident_waves_sqrt_w = std::move(incident),
        .expected_outgoing_waves_sqrt_w = std::move(outgoing),
        .expected_far_field_v_per_m = std::move(fields),
    };
}

} // namespace

const Phasor& ComplexMatrix::at(const std::size_t row, const std::size_t column) const
{
    return values.at(row * columns + column);
}

Phasor& ComplexMatrix::at(const std::size_t row, const std::size_t column)
{
    return values.at(row * columns + column);
}

std::vector<Diagnostic> validate_port_mode(const PortMode& mode)
{
    std::vector<Diagnostic> diagnostics;
    const auto norm = std::sqrt(
        mode.inward_orientation.x * mode.inward_orientation.x
        + mode.inward_orientation.y * mode.inward_orientation.y
        + mode.inward_orientation.z * mode.inward_orientation.z);
    if (mode.port_id.empty()) {
        diagnostics.push_back(diagnostic(
            ErrorCode::empty_port_id, "a port mode requires a stable nonempty port ID"));
    }
    if (!finite(norm) || std::abs(norm - 1.0) > 1.0e-12) {
        diagnostics.push_back(diagnostic(
            ErrorCode::invalid_orientation,
            "the inward port orientation must be a finite unit vector"));
    }
    if (!finite(mode.reference_impedance_ohm)
        || mode.reference_impedance_ohm <= 0.0) {
        diagnostics.push_back(diagnostic(
            ErrorCode::non_positive_reference_impedance,
            "V1 power waves require a finite positive-real reference impedance"));
    }
    if (!finite(mode.propagation_constant_rad_per_m)
        || mode.propagation_constant_rad_per_m <= 0.0) {
        diagnostics.push_back(diagnostic(
            ErrorCode::non_propagating_mode,
            "V1 response bases accept propagating modes above cutoff only"));
    }
    const auto anchor = anchor_modal_phase(mode.phase_anchor);
    diagnostics.insert(
        diagnostics.end(), anchor.diagnostics.begin(), anchor.diagnostics.end());
    return diagnostics;
}

PhaseAnchorResult anchor_modal_phase(const PhaseAnchorRequest& request)
{
    if (request.overlap_functional.empty() || request.fallback_functional.empty()
        || !finite(request.measured_overlap) || !finite(request.modal_field_norm)
        || !finite(request.minimum_relative_overlap)
        || request.modal_field_norm <= 0.0 || request.minimum_relative_overlap <= 0.0
        || request.minimum_relative_overlap >= 1.0) {
        return failure<PhaseAnchor>(
            ErrorCode::invalid_phase_anchor,
            "a phase anchor requires named overlap and fallback functionals plus finite positive norms");
    }
    if (std::abs(request.measured_overlap)
        <= request.minimum_relative_overlap * request.modal_field_norm) {
        return failure<PhaseAnchor>(
            ErrorCode::invalid_phase_anchor,
            "the declared modal overlap is too small to establish a stable phase");
    }
    if (request.near_degenerate
        && (request.orthogonal_basis_id.empty() || request.tracking_reference_id.empty()
            || !finite(request.tracking_overlap)
            || !finite(request.minimum_tracking_overlap)
            || request.minimum_tracking_overlap <= 0.0
            || request.minimum_tracking_overlap > 1.0
            || request.tracking_overlap < request.minimum_tracking_overlap
            || request.tracking_overlap > 1.0)) {
        return failure<PhaseAnchor>(
            ErrorCode::ambiguous_mode_basis,
            "a degenerate mode requires an explicit orthogonal basis with unambiguous frequency tracking");
    }

    const auto multiplier = std::conj(request.measured_overlap)
        / std::abs(request.measured_overlap);
    return PhaseAnchorResult{
        .value = PhaseAnchor{
            .multiplier = multiplier,
            .anchored_overlap = request.measured_overlap * multiplier,
        },
        .diagnostics = {},
    };
}

PowerWaveResult rms_power_waves(
    const Phasor voltage_v,
    const Phasor current_into_domain_a,
    const double reference_impedance_ohm)
{
    if (!finite(voltage_v) || !finite(current_into_domain_a)) {
        return failure<PowerWaves>(
            ErrorCode::non_finite_input, "RMS voltage and current phasors must be finite");
    }
    if (!finite(reference_impedance_ohm) || reference_impedance_ohm <= 0.0) {
        return failure<PowerWaves>(
            ErrorCode::non_positive_reference_impedance,
            "V1 power waves require a finite positive-real reference impedance");
    }
    const auto root_impedance = std::sqrt(reference_impedance_ohm);
    return PowerWaveResult{
        .value = PowerWaves{
            .incident_sqrt_w = (voltage_v + reference_impedance_ohm
                    * current_into_domain_a)
                / (2.0 * root_impedance),
            .outgoing_sqrt_w = (voltage_v - reference_impedance_ohm
                    * current_into_domain_a)
                / (2.0 * root_impedance),
        },
        .diagnostics = {},
    };
}

VoltageCurrentResult rms_voltage_current(
    const Phasor incident_sqrt_w,
    const Phasor outgoing_sqrt_w,
    const double reference_impedance_ohm)
{
    if (!finite(incident_sqrt_w) || !finite(outgoing_sqrt_w)) {
        return failure<VoltageCurrent>(
            ErrorCode::non_finite_input, "incident and outgoing power waves must be finite");
    }
    if (!finite(reference_impedance_ohm) || reference_impedance_ohm <= 0.0) {
        return failure<VoltageCurrent>(
            ErrorCode::non_positive_reference_impedance,
            "V1 power waves require a finite positive-real reference impedance");
    }
    const auto root_impedance = std::sqrt(reference_impedance_ohm);
    return VoltageCurrentResult{
        .value = VoltageCurrent{
            .voltage_v = root_impedance * (incident_sqrt_w + outgoing_sqrt_w),
            .current_into_domain_a = (incident_sqrt_w - outgoing_sqrt_w)
                / root_impedance,
        },
        .diagnostics = {},
    };
}

PowerBalanceResult power_balance(
    const std::span<const Phasor> incident_sqrt_w,
    const std::span<const Phasor> outgoing_sqrt_w)
{
    if (incident_sqrt_w.empty() || incident_sqrt_w.size() != outgoing_sqrt_w.size()) {
        return failure<PowerBalance>(
            ErrorCode::incompatible_dimensions,
            "incident and outgoing vectors must have the same nonzero port count");
    }
    if (!finite(incident_sqrt_w) || !finite(outgoing_sqrt_w)) {
        return failure<PowerBalance>(
            ErrorCode::non_finite_input, "power-wave vectors must contain finite phasors");
    }
    auto incident = 0.0;
    auto outgoing = 0.0;
    for (std::size_t port = 0; port < incident_sqrt_w.size(); ++port) {
        incident += std::norm(incident_sqrt_w[port]);
        outgoing += std::norm(outgoing_sqrt_w[port]);
    }
    return PowerBalanceResult{
        .value = PowerBalance{
            .incident_w = incident,
            .outgoing_w = outgoing,
            .accepted_w = incident - outgoing,
        },
        .diagnostics = {},
    };
}

ScalarResult metric_denominator_w(
    const MetricDenominator denominator,
    const PowerBalance& balance,
    const double radiated_power_w)
{
    if (!finite(balance.incident_w) || !finite(balance.outgoing_w)
        || !finite(balance.accepted_w) || !finite(radiated_power_w)
        || balance.incident_w < 0.0 || balance.outgoing_w < 0.0
        || radiated_power_w < 0.0) {
        return failure<double>(
            ErrorCode::non_finite_input, "power metric inputs must be finite and nonnegative");
    }
    double value{};
    switch (denominator) {
    case MetricDenominator::radiated_power:
        value = radiated_power_w;
        break;
    case MetricDenominator::accepted_power:
        value = balance.accepted_w;
        break;
    case MetricDenominator::total_incident_power:
        value = balance.incident_w;
        break;
    }
    if (value <= 0.0) {
        return failure<double>(
            ErrorCode::zero_power_denominator,
            "the selected metric requires a strictly positive power denominator");
    }
    return ScalarResult{.value = value, .diagnostics = {}};
}

MatrixResult shift_reference_planes(
    const ComplexMatrix& scattering,
    const std::span<const PortMode> ports,
    const std::span<const double> inward_shifts_m)
{
    if (!square_matrix(scattering)) {
        return failure<ComplexMatrix>(
            ErrorCode::non_square_scattering_matrix,
            "reference-plane shifts require a nonempty square scattering matrix");
    }
    if (ports.size() != scattering.rows || inward_shifts_m.size() != scattering.rows) {
        return failure<ComplexMatrix>(
            ErrorCode::incompatible_dimensions,
            "the scattering matrix, port modes, and shift vector must have equal order");
    }
    if (!finite(scattering.values)) {
        return failure<ComplexMatrix>(
            ErrorCode::non_finite_input, "scattering parameters must be finite");
    }

    std::set<std::string, std::less<>> ids;
    for (std::size_t port = 0; port < ports.size(); ++port) {
        const auto diagnostics = validate_port_mode(ports[port]);
        if (!diagnostics.empty()) {
            return MatrixResult{.value = std::nullopt, .diagnostics = diagnostics};
        }
        if (!ids.insert(ports[port].port_id).second) {
            return failure<ComplexMatrix>(
                ErrorCode::incompatible_dimensions, "port-mode IDs must be unique");
        }
        if (!finite(inward_shifts_m[port])) {
            return failure<ComplexMatrix>(
                ErrorCode::non_finite_input, "reference-plane shifts must be finite");
        }
    }

    ComplexMatrix shifted{
        .rows = scattering.rows,
        .columns = scattering.columns,
        .values = PhasorVector(scattering.values.size()),
    };
    for (std::size_t row = 0; row < scattering.rows; ++row) {
        const auto row_phase = std::polar(
            1.0, ports[row].propagation_constant_rad_per_m * inward_shifts_m[row]);
        for (std::size_t column = 0; column < scattering.columns; ++column) {
            const auto column_phase = std::polar(
                1.0,
                ports[column].propagation_constant_rad_per_m
                    * inward_shifts_m[column]);
            shifted.at(row, column)
                = row_phase * scattering.at(row, column) * column_phase;
        }
    }
    return MatrixResult{.value = std::move(shifted), .diagnostics = {}};
}

ScalarResult maximum_power_gain(const ComplexMatrix& scattering)
{
    if (!square_matrix(scattering)) {
        return failure<double>(
            ErrorCode::non_square_scattering_matrix,
            "passivity assessment requires a nonempty square scattering matrix");
    }
    if (!finite(scattering.values)) {
        return failure<double>(
            ErrorCode::non_finite_input, "scattering parameters must be finite");
    }

    const auto order = scattering.rows;
    const auto embedded_order = 2 * order;
    std::vector<double> hermitian(embedded_order * embedded_order);
    for (std::size_t row = 0; row < order; ++row) {
        for (std::size_t column = 0; column < order; ++column) {
            Phasor value{};
            for (std::size_t port = 0; port < order; ++port) {
                value += std::conj(scattering.at(port, row))
                    * scattering.at(port, column);
            }
            hermitian[row * embedded_order + column] = value.real();
            hermitian[row * embedded_order + column + order] = -value.imag();
            hermitian[(row + order) * embedded_order + column] = value.imag();
            hermitian[(row + order) * embedded_order + column + order] = value.real();
        }
    }

    const auto convergence = 64.0 * std::numeric_limits<double>::epsilon();
    for (std::size_t sweep = 0; sweep < 64 * embedded_order; ++sweep) {
        auto largest = 0.0;
        std::size_t pivot_row{};
        std::size_t pivot_column{};
        for (std::size_t row = 0; row < embedded_order; ++row) {
            for (std::size_t column = row + 1; column < embedded_order; ++column) {
                const auto candidate = std::abs(
                    hermitian[row * embedded_order + column]);
                if (candidate > largest) {
                    largest = candidate;
                    pivot_row = row;
                    pivot_column = column;
                }
            }
        }
        if (largest <= convergence) {
            break;
        }

        const auto diagonal_row = hermitian[pivot_row * embedded_order + pivot_row];
        const auto diagonal_column
            = hermitian[pivot_column * embedded_order + pivot_column];
        const auto off_diagonal
            = hermitian[pivot_row * embedded_order + pivot_column];
        const auto angle = 0.5
            * std::atan2(2.0 * off_diagonal, diagonal_column - diagonal_row);
        const auto cosine = std::cos(angle);
        const auto sine = std::sin(angle);

        for (std::size_t index = 0; index < embedded_order; ++index) {
            if (index == pivot_row || index == pivot_column) {
                continue;
            }
            const auto row_value = hermitian[index * embedded_order + pivot_row];
            const auto column_value
                = hermitian[index * embedded_order + pivot_column];
            const auto rotated_row = cosine * row_value - sine * column_value;
            const auto rotated_column = sine * row_value + cosine * column_value;
            hermitian[index * embedded_order + pivot_row] = rotated_row;
            hermitian[pivot_row * embedded_order + index] = rotated_row;
            hermitian[index * embedded_order + pivot_column] = rotated_column;
            hermitian[pivot_column * embedded_order + index] = rotated_column;
        }
        hermitian[pivot_row * embedded_order + pivot_row]
            = cosine * cosine * diagonal_row - 2.0 * sine * cosine * off_diagonal
            + sine * sine * diagonal_column;
        hermitian[pivot_column * embedded_order + pivot_column]
            = sine * sine * diagonal_row + 2.0 * sine * cosine * off_diagonal
            + cosine * cosine * diagonal_column;
        hermitian[pivot_row * embedded_order + pivot_column] = 0.0;
        hermitian[pivot_column * embedded_order + pivot_row] = 0.0;
    }

    auto maximum = 0.0;
    for (std::size_t index = 0; index < embedded_order; ++index) {
        maximum = std::max(maximum, hermitian[index * embedded_order + index]);
    }
    return ScalarResult{.value = maximum, .diagnostics = {}};
}

ScalarResult reciprocity_error(const ComplexMatrix& scattering)
{
    if (!square_matrix(scattering)) {
        return failure<double>(
            ErrorCode::non_square_scattering_matrix,
            "reciprocity assessment requires a nonempty square scattering matrix");
    }
    if (!finite(scattering.values)) {
        return failure<double>(
            ErrorCode::non_finite_input, "scattering parameters must be finite");
    }
    auto error = 0.0;
    for (std::size_t row = 0; row < scattering.rows; ++row) {
        for (std::size_t column = row + 1; column < scattering.columns; ++column) {
            error = std::max(
                error, std::abs(scattering.at(row, column) - scattering.at(column, row)));
        }
    }
    return ScalarResult{.value = error, .diagnostics = {}};
}

BasisResult assemble_response_basis(
    const std::span<const BasisRun> runs,
    const std::size_t port_count,
    const double tolerance)
{
    if (!finite(tolerance) || tolerance < 0.0) {
        return failure<ResponseBasis>(
            ErrorCode::invalid_tolerance, "basis tolerance must be finite and nonnegative");
    }
    if (port_count == 0 || runs.size() != port_count) {
        return failure<ResponseBasis>(
            ErrorCode::incompatible_dimensions,
            "a response basis requires exactly one run per port");
    }
    const auto field_count = runs.front().normalized_far_field_v_per_m_per_sqrt_w.size();
    if (field_count == 0) {
        return failure<ResponseBasis>(
            ErrorCode::incompatible_dimensions,
            "a response basis requires at least one far-field sample");
    }

    ResponseBasis basis{
        .scattering = ComplexMatrix{
            .rows = port_count,
            .columns = port_count,
            .values = PhasorVector(port_count * port_count),
        },
        .normalized_far_fields = ComplexMatrix{
            .rows = field_count,
            .columns = port_count,
            .values = PhasorVector(field_count * port_count),
        },
    };
    std::vector<bool> observed(port_count);
    for (const auto& run : runs) {
        if (run.driven_port >= port_count || observed[run.driven_port]) {
            return failure<ResponseBasis>(
                ErrorCode::duplicate_basis_run,
                "basis runs must identify every driven port exactly once");
        }
        if (run.incident_waves_sqrt_w.size() != port_count
            || run.outgoing_waves_sqrt_w.size() != port_count
            || run.normalized_far_field_v_per_m_per_sqrt_w.size() != field_count) {
            return failure<ResponseBasis>(
                ErrorCode::incompatible_dimensions,
                "basis-run wave and field vectors must have consistent dimensions");
        }
        if (!finite(run.incident_waves_sqrt_w) || !finite(run.outgoing_waves_sqrt_w)
            || !finite(run.normalized_far_field_v_per_m_per_sqrt_w)) {
            return failure<ResponseBasis>(
                ErrorCode::non_finite_input, "basis runs must contain finite phasors");
        }
        for (std::size_t port = 0; port < port_count; ++port) {
            const auto expected = port == run.driven_port ? Phasor{1.0, 0.0} : Phasor{};
            if (std::abs(run.incident_waves_sqrt_w[port] - expected) > tolerance) {
                return failure<ResponseBasis>(
                    ErrorCode::invalid_basis_run,
                    "each basis run must drive one 1 sqrt(W) incident wave and match every inactive port");
            }
            basis.scattering.at(port, run.driven_port)
                = run.outgoing_waves_sqrt_w[port];
        }
        for (std::size_t sample = 0; sample < field_count; ++sample) {
            basis.normalized_far_fields.at(sample, run.driven_port)
                = run.normalized_far_field_v_per_m_per_sqrt_w[sample];
        }
        observed[run.driven_port] = true;
    }
    return BasisResult{.value = std::move(basis), .diagnostics = {}};
}

ReconstructionResult reconstruct(
    const ResponseBasis& basis,
    const std::span<const Phasor> incident_sqrt_w)
{
    if (!square_matrix(basis.scattering)
        || !valid_matrix_shape(basis.normalized_far_fields)
        || basis.normalized_far_fields.columns != basis.scattering.columns
        || incident_sqrt_w.size() != basis.scattering.columns) {
        return failure<ReconstructedResponse>(
            ErrorCode::incompatible_dimensions,
            "response basis and incident drive must share a nonzero port count");
    }
    if (!finite(basis.scattering.values) || !finite(basis.normalized_far_fields.values)
        || !finite(incident_sqrt_w)) {
        return failure<ReconstructedResponse>(
            ErrorCode::non_finite_input, "response basis and drive must contain finite phasors");
    }
    return ReconstructionResult{
        .value = ReconstructedResponse{
            .outgoing_waves_sqrt_w = multiply(basis.scattering, incident_sqrt_w),
            .far_field_v_per_m = multiply(basis.normalized_far_fields, incident_sqrt_w),
        },
        .diagnostics = {},
    };
}

FourPortFixture canonical_four_port_fixture()
{
    const auto scattering = matrix(4, 4, {
        {0.10, 0.02}, {0.18, -0.04}, {0.08, 0.03}, {0.12, -0.02},
        {0.18, -0.04}, {-0.09, 0.01}, {0.14, 0.05}, {0.07, -0.06},
        {0.08, 0.03}, {0.14, 0.05}, {0.11, -0.03}, {0.16, 0.02},
        {0.12, -0.02}, {0.07, -0.06}, {0.16, 0.02}, {-0.08, -0.04},
    });
    const auto fields = matrix(3, 4, {
        {1.0, 0.2}, {0.3, -0.4}, {-0.2, 0.7}, {0.5, 0.1},
        {-0.1, 0.6}, {0.8, 0.2}, {0.4, -0.3}, {-0.5, 0.9},
        {0.7, -0.2}, {-0.4, 0.5}, {0.6, 0.1}, {0.2, -0.8},
    });

    const auto anchor = [](const Phasor overlap) {
        return PhaseAnchorRequest{
            .overlap_functional = "transverse-electric-reference-direction",
            .fallback_functional = "stored-reference-field-overlap",
            .measured_overlap = overlap,
            .modal_field_norm = 1.0,
            .minimum_relative_overlap = 1.0e-8,
            .near_degenerate = false,
            .orthogonal_basis_id = {},
            .tracking_reference_id = {},
            .tracking_overlap = 0.0,
            .minimum_tracking_overlap = 0.9,
        };
    };

    return FourPortFixture{
        .contract = std::string(contract_version),
        .ports = {
            PortMode{"P1", {1.0, 0.0, 0.0}, 0, 50.0, 22.0, anchor({0.4, 0.3})},
            PortMode{"P2", {0.0, 1.0, 0.0}, 0, 55.0, 25.0, anchor({-0.2, 0.6})},
            PortMode{"P3", {-1.0, 0.0, 0.0}, 0, 60.0, 28.0, anchor({0.3, -0.5})},
            PortMode{"P4", {0.0, -1.0, 0.0}, 0, 65.0, 31.0, anchor({-0.7, -0.1})},
        },
        .basis_runs = basis_runs(scattering, fields),
        .inward_reference_plane_shifts_m = {0.002, 0.003, 0.004, 0.005},
        .direct_cases = {
            direct_case(
                "quadrature-drive",
                {{0.5, 0.0}, {0.0, 0.5}, {-0.5, 0.0}, {0.0, -0.5}},
                {{0.02, 0.025}, {-0.015, -0.125}, {-0.03, 0.02}, {-0.01, 0.055}},
                {{0.85, -0.35}, {0.10, 1.10}, {-0.60, -0.45}}),
            direct_case(
                "arbitrary-complex-drive",
                {{0.8, 0.1}, {-0.25, 0.4}, {0.15, -0.2}, {-0.1, -0.3}},
                {{0.049, 0.0625}, {0.1725, -0.088}, {0.0065, -0.001}, {0.1285, 0.038}},
                {{0.955, 0.465}, {-0.10, 0.675}, {0.33, -0.46}}),
        },
    };
}

} // namespace oep::test::port_contract
