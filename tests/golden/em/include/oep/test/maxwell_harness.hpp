#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace oep::test::maxwell {

inline constexpr double vacuum_light_speed = 299'792'458.0;
inline constexpr double vacuum_permeability = 1.256'637'062'12e-6;
inline constexpr double vacuum_permittivity =
    1.0 / (vacuum_permeability * vacuum_light_speed * vacuum_light_speed);

enum class ReferenceErrorCode {
    non_finite_input,
    non_positive_input,
    invalid_material,
    invalid_mode,
    below_cutoff,
    incompatible_samples,
    zero_reference_norm,
    invalid_refinement_ratio,
    invalid_energy_balance,
};

struct ReferenceDiagnostic {
    ReferenceErrorCode code{ReferenceErrorCode::non_finite_input};
    std::string message;
};

struct PlaneWaveRequest {
    double frequency_hz{};
    double relative_permittivity{1.0};
    double relative_permeability{1.0};
};

struct PlaneWaveReference {
    double phase_velocity_m_per_s{};
    double wavelength_m{};
    double wave_impedance_ohm{};
    double wavenumber_rad_per_m{};
};

struct PlaneWaveResult {
    std::optional<PlaneWaveReference> reference;
    std::vector<ReferenceDiagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept { return reference.has_value(); }
};

enum class ModeFamily {
    transverse_electric,
    transverse_magnetic,
};

struct ModeIndices {
    unsigned int m{};
    unsigned int n{};
    unsigned int p{};
};

struct CavityRequest {
    double length_x_m{};
    double length_y_m{};
    double length_z_m{};
    ModeFamily family{ModeFamily::transverse_magnetic};
    ModeIndices mode;
    double relative_permittivity{1.0};
    double relative_permeability{1.0};
};

struct CavityReference {
    double resonant_frequency_hz{};
    double angular_frequency_rad_per_s{};
    double vacuum_wavelength_m{};
};

struct CavityResult {
    std::optional<CavityReference> reference;
    std::vector<ReferenceDiagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept { return reference.has_value(); }
};

struct WaveguideRequest {
    double width_m{};
    double height_m{};
    ModeFamily family{ModeFamily::transverse_electric};
    ModeIndices mode;
    double operating_frequency_hz{};
    double relative_permittivity{1.0};
    double relative_permeability{1.0};
};

struct WaveguideReference {
    double cutoff_frequency_hz{};
    double propagation_constant_rad_per_m{};
    double guide_wavelength_m{};
    double phase_velocity_m_per_s{};
    double group_velocity_m_per_s{};
    double mode_impedance_ohm{};
};

struct WaveguideResult {
    std::optional<WaveguideReference> reference;
    std::vector<ReferenceDiagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept { return reference.has_value(); }
};

[[nodiscard]] PlaneWaveResult plane_wave_reference(const PlaneWaveRequest& request);
[[nodiscard]] CavityResult rectangular_cavity_reference(const CavityRequest& request);
[[nodiscard]] WaveguideResult rectangular_waveguide_reference(
    const WaveguideRequest& request);

struct ScalarMetricResult {
    std::optional<double> value;
    std::vector<ReferenceDiagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept { return value.has_value(); }
};

[[nodiscard]] ScalarMetricResult relative_error(double observed, double expected);
[[nodiscard]] ScalarMetricResult relative_l2_error(
    std::span<const double> observed,
    std::span<const double> expected);
[[nodiscard]] ScalarMetricResult linf_absolute_error(
    std::span<const double> observed,
    std::span<const double> expected);
[[nodiscard]] ScalarMetricResult observed_convergence_order(
    double coarse_error,
    double fine_error,
    double refinement_ratio);
[[nodiscard]] ScalarMetricResult normalized_energy_residual(
    double initial_energy,
    double injected_energy,
    double stored_energy,
    double escaped_energy,
    double dissipated_energy);

enum class VerificationCaseKind {
    plane_wave_1d,
    pec_rectangular_cavity,
    rectangular_waveguide,
};

enum class MetricKind {
    phase_velocity_relative_error,
    resonant_frequency_relative_error,
    cutoff_frequency_relative_error,
    propagation_constant_relative_error,
    field_l2_relative_error,
    field_linf_relative_error,
    open_boundary_reflection_absolute,
    energy_conservation_relative_residual,
    observed_convergence_order,
    restart_linf_absolute_error,
};

enum class RequirementComparison {
    maximum,
    minimum,
};

struct MetricRequirement {
    MetricKind metric{MetricKind::field_l2_relative_error};
    RequirementComparison comparison{RequirementComparison::maximum};
    double limit{};
    std::string rationale;
};

using AnalyticReference =
    std::variant<PlaneWaveReference, CavityReference, WaveguideReference>;
using AnalyticProblem =
    std::variant<PlaneWaveRequest, CavityRequest, WaveguideRequest>;

struct VerificationCase {
    std::string id;
    VerificationCaseKind kind{VerificationCaseKind::plane_wave_1d};
    AnalyticProblem problem;
    AnalyticReference analytic_reference;
    std::vector<MetricRequirement> requirements;
};

[[nodiscard]] std::vector<VerificationCase> canonical_verification_cases();

struct MetricObservation {
    MetricKind metric{MetricKind::field_l2_relative_error};
    double value{};
};

enum class AssessmentIssueCode {
    missing_metric,
    duplicate_metric,
    non_finite_metric,
    invalid_metric,
    tolerance_exceeded,
};

struct AssessmentIssue {
    AssessmentIssueCode code{AssessmentIssueCode::missing_metric};
    MetricKind metric{MetricKind::field_l2_relative_error};
    std::string message;
};

struct CaseAssessment {
    std::string case_id;
    std::vector<AssessmentIssue> issues;

    [[nodiscard]] bool passed() const noexcept { return issues.empty(); }
};

[[nodiscard]] CaseAssessment assess(
    const VerificationCase& test_case,
    std::span<const MetricObservation> observations);

} // namespace oep::test::maxwell
