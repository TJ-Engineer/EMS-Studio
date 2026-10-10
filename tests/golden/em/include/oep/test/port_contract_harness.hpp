// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oep::test::port_contract {

inline constexpr std::string_view contract_version = "oep-rms-power-wave-1.0";

using Phasor = std::complex<double>;
using PhasorVector = std::vector<Phasor>;

enum class ErrorCode {
    non_finite_input,
    empty_port_id,
    non_positive_reference_impedance,
    invalid_orientation,
    non_propagating_mode,
    invalid_phase_anchor,
    ambiguous_mode_basis,
    incompatible_dimensions,
    non_square_scattering_matrix,
    invalid_basis_run,
    duplicate_basis_run,
    invalid_tolerance,
    zero_power_denominator,
};

struct Diagnostic {
    ErrorCode code{ErrorCode::non_finite_input};
    std::string message;
};

template <typename Value>
struct Result {
    std::optional<Value> value;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept { return value.has_value(); }
};

struct ComplexMatrix {
    std::size_t rows{};
    std::size_t columns{};
    PhasorVector values;

    [[nodiscard]] const Phasor& at(std::size_t row, std::size_t column) const;
    [[nodiscard]] Phasor& at(std::size_t row, std::size_t column);
};

struct Direction3d {
    double x{};
    double y{};
    double z{};
};

struct PhaseAnchorRequest {
    std::string overlap_functional;
    std::string fallback_functional;
    Phasor measured_overlap;
    double modal_field_norm{1.0};
    double minimum_relative_overlap{1.0e-8};
    bool near_degenerate{};
    std::string orthogonal_basis_id;
    std::string tracking_reference_id;
    double tracking_overlap{};
    double minimum_tracking_overlap{0.9};
};

struct PhaseAnchor {
    Phasor multiplier;
    Phasor anchored_overlap;
};

struct PortMode {
    std::string port_id;
    Direction3d inward_orientation;
    std::uint32_t mode_index{};
    double reference_impedance_ohm{};
    double propagation_constant_rad_per_m{};
    PhaseAnchorRequest phase_anchor;
};

struct PowerWaves {
    Phasor incident_sqrt_w;
    Phasor outgoing_sqrt_w;
};

struct VoltageCurrent {
    Phasor voltage_v;
    Phasor current_into_domain_a;
};

struct PowerBalance {
    double incident_w{};
    double outgoing_w{};
    double accepted_w{};
};

enum class MetricDenominator {
    radiated_power,
    accepted_power,
    total_incident_power,
};

struct BasisRun {
    std::size_t driven_port{};
    PhasorVector incident_waves_sqrt_w;
    PhasorVector outgoing_waves_sqrt_w;
    PhasorVector normalized_far_field_v_per_m_per_sqrt_w;
};

struct ResponseBasis {
    ComplexMatrix scattering;
    ComplexMatrix normalized_far_fields;
};

struct ReconstructedResponse {
    PhasorVector outgoing_waves_sqrt_w;
    PhasorVector far_field_v_per_m;
};

struct DirectExcitationCase {
    std::string id;
    PhasorVector incident_waves_sqrt_w;
    PhasorVector expected_outgoing_waves_sqrt_w;
    PhasorVector expected_far_field_v_per_m;
};

struct FourPortFixture {
    std::string contract;
    std::vector<PortMode> ports;
    std::vector<BasisRun> basis_runs;
    std::vector<double> inward_reference_plane_shifts_m;
    std::vector<DirectExcitationCase> direct_cases;
};

using PhaseAnchorResult = Result<PhaseAnchor>;
using PowerWaveResult = Result<PowerWaves>;
using VoltageCurrentResult = Result<VoltageCurrent>;
using PowerBalanceResult = Result<PowerBalance>;
using ScalarResult = Result<double>;
using MatrixResult = Result<ComplexMatrix>;
using BasisResult = Result<ResponseBasis>;
using ReconstructionResult = Result<ReconstructedResponse>;

[[nodiscard]] std::vector<Diagnostic> validate_port_mode(const PortMode& mode);
[[nodiscard]] PhaseAnchorResult anchor_modal_phase(const PhaseAnchorRequest& request);
[[nodiscard]] PowerWaveResult rms_power_waves(
    Phasor voltage_v,
    Phasor current_into_domain_a,
    double reference_impedance_ohm);
[[nodiscard]] VoltageCurrentResult rms_voltage_current(
    Phasor incident_sqrt_w,
    Phasor outgoing_sqrt_w,
    double reference_impedance_ohm);
[[nodiscard]] PowerBalanceResult power_balance(
    std::span<const Phasor> incident_sqrt_w,
    std::span<const Phasor> outgoing_sqrt_w);
[[nodiscard]] ScalarResult metric_denominator_w(
    MetricDenominator denominator,
    const PowerBalance& balance,
    double radiated_power_w);

[[nodiscard]] MatrixResult shift_reference_planes(
    const ComplexMatrix& scattering,
    std::span<const PortMode> ports,
    std::span<const double> inward_shifts_m);
[[nodiscard]] ScalarResult maximum_power_gain(const ComplexMatrix& scattering);
[[nodiscard]] ScalarResult reciprocity_error(const ComplexMatrix& scattering);

[[nodiscard]] BasisResult assemble_response_basis(
    std::span<const BasisRun> runs,
    std::size_t port_count,
    double tolerance = 1.0e-12);
[[nodiscard]] ReconstructionResult reconstruct(
    const ResponseBasis& basis,
    std::span<const Phasor> incident_sqrt_w);

[[nodiscard]] FourPortFixture canonical_four_port_fixture();

} // namespace oep::test::port_contract
