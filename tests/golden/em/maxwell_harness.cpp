#include <oep/test/maxwell_harness.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <type_traits>
#include <utility>

namespace oep::test::maxwell {
namespace {

[[nodiscard]] ReferenceDiagnostic diagnostic(
    const ReferenceErrorCode code,
    std::string message)
{
    return ReferenceDiagnostic{.code = code, .message = std::move(message)};
}

template <typename Reference>
[[nodiscard]] auto failure(const ReferenceErrorCode code, std::string message)
{
    if constexpr (std::is_same_v<Reference, PlaneWaveReference>) {
        return PlaneWaveResult{
            .reference = std::nullopt,
            .diagnostics = {diagnostic(code, std::move(message))},
        };
    } else if constexpr (std::is_same_v<Reference, CavityReference>) {
        return CavityResult{
            .reference = std::nullopt,
            .diagnostics = {diagnostic(code, std::move(message))},
        };
    } else {
        return WaveguideResult{
            .reference = std::nullopt,
            .diagnostics = {diagnostic(code, std::move(message))},
        };
    }
}

[[nodiscard]] ScalarMetricResult metric_failure(
    const ReferenceErrorCode code,
    std::string message)
{
    return ScalarMetricResult{
        .value = std::nullopt,
        .diagnostics = {diagnostic(code, std::move(message))},
    };
}

[[nodiscard]] bool finite_positive(const double value)
{
    return std::isfinite(value) && value > 0.0;
}

[[nodiscard]] bool valid_material(
    const double relative_permittivity,
    const double relative_permeability)
{
    return finite_positive(relative_permittivity)
        && finite_positive(relative_permeability);
}

[[nodiscard]] double medium_light_speed(
    const double relative_permittivity,
    const double relative_permeability)
{
    return vacuum_light_speed
        / std::sqrt(relative_permittivity * relative_permeability);
}

[[nodiscard]] double medium_impedance(
    const double relative_permittivity,
    const double relative_permeability)
{
    return std::sqrt(
        vacuum_permeability * relative_permeability
        / (vacuum_permittivity * relative_permittivity));
}

[[nodiscard]] bool valid_cavity_mode(
    const ModeFamily family,
    const ModeIndices mode)
{
    if (family == ModeFamily::transverse_magnetic) {
        return mode.m > 0 && mode.n > 0;
    }
    return mode.p > 0 && (mode.m > 0 || mode.n > 0);
}

[[nodiscard]] bool valid_waveguide_mode(
    const ModeFamily family,
    const ModeIndices mode)
{
    if (mode.p != 0) {
        return false;
    }
    if (family == ModeFamily::transverse_magnetic) {
        return mode.m > 0 && mode.n > 0;
    }
    return mode.m > 0 || mode.n > 0;
}

[[nodiscard]] MetricRequirement maximum_requirement(
    const MetricKind metric,
    const double limit,
    std::string rationale)
{
    return MetricRequirement{
        .metric = metric,
        .comparison = RequirementComparison::maximum,
        .limit = limit,
        .rationale = std::move(rationale),
    };
}

[[nodiscard]] MetricRequirement minimum_requirement(
    const MetricKind metric,
    const double limit,
    std::string rationale)
{
    return MetricRequirement{
        .metric = metric,
        .comparison = RequirementComparison::minimum,
        .limit = limit,
        .rationale = std::move(rationale),
    };
}

} // namespace

PlaneWaveResult plane_wave_reference(const PlaneWaveRequest& request)
{
    if (!std::isfinite(request.frequency_hz)
        || !std::isfinite(request.relative_permittivity)
        || !std::isfinite(request.relative_permeability)) {
        return failure<PlaneWaveReference>(
            ReferenceErrorCode::non_finite_input,
            "plane-wave inputs must be finite");
    }
    if (request.frequency_hz <= 0.0) {
        return failure<PlaneWaveReference>(
            ReferenceErrorCode::non_positive_input,
            "plane-wave frequency must be positive");
    }
    if (!valid_material(
            request.relative_permittivity, request.relative_permeability)) {
        return failure<PlaneWaveReference>(
            ReferenceErrorCode::invalid_material,
            "lossless reference medium requires positive relative permittivity and permeability");
    }

    const auto velocity = medium_light_speed(
        request.relative_permittivity, request.relative_permeability);
    const auto wavelength = velocity / request.frequency_hz;
    return PlaneWaveResult{
        .reference = PlaneWaveReference{
            .phase_velocity_m_per_s = velocity,
            .wavelength_m = wavelength,
            .wave_impedance_ohm = medium_impedance(
                request.relative_permittivity, request.relative_permeability),
            .wavenumber_rad_per_m = 2.0 * std::numbers::pi / wavelength,
        },
        .diagnostics = {},
    };
}

CavityResult rectangular_cavity_reference(const CavityRequest& request)
{
    if (!std::isfinite(request.length_x_m) || !std::isfinite(request.length_y_m)
        || !std::isfinite(request.length_z_m)
        || !std::isfinite(request.relative_permittivity)
        || !std::isfinite(request.relative_permeability)) {
        return failure<CavityReference>(
            ReferenceErrorCode::non_finite_input,
            "cavity inputs must be finite");
    }
    if (request.length_x_m <= 0.0 || request.length_y_m <= 0.0
        || request.length_z_m <= 0.0) {
        return failure<CavityReference>(
            ReferenceErrorCode::non_positive_input,
            "cavity dimensions must be positive");
    }
    if (!valid_material(
            request.relative_permittivity, request.relative_permeability)) {
        return failure<CavityReference>(
            ReferenceErrorCode::invalid_material,
            "lossless cavity reference requires positive relative material constants");
    }
    if (!valid_cavity_mode(request.family, request.mode)) {
        return failure<CavityReference>(
            ReferenceErrorCode::invalid_mode,
            "cavity mode indices do not describe a supported nonzero TE or TM mode");
    }

    const auto m_term = static_cast<double>(request.mode.m) / request.length_x_m;
    const auto n_term = static_cast<double>(request.mode.n) / request.length_y_m;
    const auto p_term = static_cast<double>(request.mode.p) / request.length_z_m;
    const auto frequency = medium_light_speed(
        request.relative_permittivity, request.relative_permeability)
        * 0.5 * std::sqrt(m_term * m_term + n_term * n_term + p_term * p_term);
    return CavityResult{
        .reference = CavityReference{
            .resonant_frequency_hz = frequency,
            .angular_frequency_rad_per_s = 2.0 * std::numbers::pi * frequency,
            .vacuum_wavelength_m = vacuum_light_speed / frequency,
        },
        .diagnostics = {},
    };
}

WaveguideResult rectangular_waveguide_reference(
    const WaveguideRequest& request)
{
    if (!std::isfinite(request.width_m) || !std::isfinite(request.height_m)
        || !std::isfinite(request.operating_frequency_hz)
        || !std::isfinite(request.relative_permittivity)
        || !std::isfinite(request.relative_permeability)) {
        return failure<WaveguideReference>(
            ReferenceErrorCode::non_finite_input,
            "waveguide inputs must be finite");
    }
    if (request.width_m <= 0.0 || request.height_m <= 0.0
        || request.operating_frequency_hz <= 0.0) {
        return failure<WaveguideReference>(
            ReferenceErrorCode::non_positive_input,
            "waveguide dimensions and operating frequency must be positive");
    }
    if (!valid_material(
            request.relative_permittivity, request.relative_permeability)) {
        return failure<WaveguideReference>(
            ReferenceErrorCode::invalid_material,
            "lossless waveguide reference requires positive relative material constants");
    }
    if (!valid_waveguide_mode(request.family, request.mode)) {
        return failure<WaveguideReference>(
            ReferenceErrorCode::invalid_mode,
            "waveguide mode requires p=0 and valid transverse TE or TM indices");
    }

    const auto m_term = static_cast<double>(request.mode.m) / request.width_m;
    const auto n_term = static_cast<double>(request.mode.n) / request.height_m;
    const auto velocity = medium_light_speed(
        request.relative_permittivity, request.relative_permeability);
    const auto cutoff = velocity * 0.5
        * std::sqrt(m_term * m_term + n_term * n_term);
    if (request.operating_frequency_hz <= cutoff) {
        return failure<WaveguideReference>(
            ReferenceErrorCode::below_cutoff,
            "operating frequency must exceed cutoff for a propagating reference mode");
    }

    const auto cutoff_ratio = cutoff / request.operating_frequency_hz;
    const auto propagation_factor = std::sqrt(1.0 - cutoff_ratio * cutoff_ratio);
    const auto free_wavenumber = 2.0 * std::numbers::pi
        * request.operating_frequency_hz / velocity;
    const auto propagation_constant = free_wavenumber * propagation_factor;
    const auto impedance = medium_impedance(
        request.relative_permittivity, request.relative_permeability);
    return WaveguideResult{
        .reference = WaveguideReference{
            .cutoff_frequency_hz = cutoff,
            .propagation_constant_rad_per_m = propagation_constant,
            .guide_wavelength_m = 2.0 * std::numbers::pi / propagation_constant,
            .phase_velocity_m_per_s = velocity / propagation_factor,
            .group_velocity_m_per_s = velocity * propagation_factor,
            .mode_impedance_ohm = request.family == ModeFamily::transverse_electric
                ? impedance / propagation_factor
                : impedance * propagation_factor,
        },
        .diagnostics = {},
    };
}

ScalarMetricResult relative_error(const double observed, const double expected)
{
    if (!std::isfinite(observed) || !std::isfinite(expected)) {
        return metric_failure(
            ReferenceErrorCode::non_finite_input,
            "relative-error inputs must be finite");
    }
    if (expected == 0.0) {
        return metric_failure(
            ReferenceErrorCode::zero_reference_norm,
            "relative error requires a nonzero expected value");
    }
    return ScalarMetricResult{
        .value = std::abs(observed - expected) / std::abs(expected),
        .diagnostics = {},
    };
}

ScalarMetricResult relative_l2_error(
    const std::span<const double> observed,
    const std::span<const double> expected)
{
    if (observed.empty() || observed.size() != expected.size()) {
        return metric_failure(
            ReferenceErrorCode::incompatible_samples,
            "relative L2 error requires equally sized nonempty sample sets");
    }
    auto squared_error = 0.0;
    auto squared_reference = 0.0;
    for (std::size_t index = 0; index < observed.size(); ++index) {
        if (!std::isfinite(observed[index]) || !std::isfinite(expected[index])) {
            return metric_failure(
                ReferenceErrorCode::non_finite_input,
                "relative L2 samples must be finite");
        }
        const auto error = observed[index] - expected[index];
        squared_error += error * error;
        squared_reference += expected[index] * expected[index];
    }
    if (squared_reference <= std::numeric_limits<double>::min()) {
        return metric_failure(
            ReferenceErrorCode::zero_reference_norm,
            "relative L2 error requires nonzero reference energy");
    }
    return ScalarMetricResult{
        .value = std::sqrt(squared_error / squared_reference),
        .diagnostics = {},
    };
}

ScalarMetricResult linf_absolute_error(
    const std::span<const double> observed,
    const std::span<const double> expected)
{
    if (observed.empty() || observed.size() != expected.size()) {
        return metric_failure(
            ReferenceErrorCode::incompatible_samples,
            "L-infinity error requires equally sized nonempty sample sets");
    }
    auto maximum = 0.0;
    for (std::size_t index = 0; index < observed.size(); ++index) {
        if (!std::isfinite(observed[index]) || !std::isfinite(expected[index])) {
            return metric_failure(
                ReferenceErrorCode::non_finite_input,
                "L-infinity samples must be finite");
        }
        maximum = std::max(maximum, std::abs(observed[index] - expected[index]));
    }
    return ScalarMetricResult{.value = maximum, .diagnostics = {}};
}

ScalarMetricResult observed_convergence_order(
    const double coarse_error,
    const double fine_error,
    const double refinement_ratio)
{
    if (!finite_positive(coarse_error) || !finite_positive(fine_error)) {
        return metric_failure(
            ReferenceErrorCode::non_finite_input,
            "convergence errors must be finite and positive");
    }
    if (!std::isfinite(refinement_ratio) || refinement_ratio <= 1.0) {
        return metric_failure(
            ReferenceErrorCode::invalid_refinement_ratio,
            "refinement ratio must be finite and greater than one");
    }
    return ScalarMetricResult{
        .value = std::log(coarse_error / fine_error) / std::log(refinement_ratio),
        .diagnostics = {},
    };
}

ScalarMetricResult normalized_energy_residual(
    const double initial_energy,
    const double injected_energy,
    const double stored_energy,
    const double escaped_energy,
    const double dissipated_energy)
{
    const std::array values{
        initial_energy,
        injected_energy,
        stored_energy,
        escaped_energy,
        dissipated_energy,
    };
    if (!std::ranges::all_of(values, [](const double value) {
            return std::isfinite(value) && value >= 0.0;
        })) {
        return metric_failure(
            ReferenceErrorCode::invalid_energy_balance,
            "energy terms must be finite and nonnegative");
    }
    const auto supplied = initial_energy + injected_energy;
    const auto accounted = stored_energy + escaped_energy + dissipated_energy;
    const auto scale = std::max({supplied, accounted, std::numeric_limits<double>::min()});
    return ScalarMetricResult{
        .value = std::abs(supplied - accounted) / scale,
        .diagnostics = {},
    };
}

std::vector<VerificationCase> canonical_verification_cases()
{
    const PlaneWaveRequest plane_wave_problem{
        .frequency_hz = 1.0e9,
        .relative_permittivity = 1.0,
        .relative_permeability = 1.0,
    };
    const CavityRequest cavity_problem{
        .length_x_m = 0.10,
        .length_y_m = 0.08,
        .length_z_m = 0.06,
        .family = ModeFamily::transverse_magnetic,
        .mode = ModeIndices{1, 1, 1},
        .relative_permittivity = 1.0,
        .relative_permeability = 1.0,
    };
    const WaveguideRequest waveguide_problem{
        .width_m = 0.02286,
        .height_m = 0.01016,
        .family = ModeFamily::transverse_electric,
        .mode = ModeIndices{1, 0, 0},
        .operating_frequency_hz = 10.0e9,
        .relative_permittivity = 1.0,
        .relative_permeability = 1.0,
    };
    const auto plane_wave = plane_wave_reference(plane_wave_problem);
    const auto cavity = rectangular_cavity_reference(cavity_problem);
    const auto waveguide = rectangular_waveguide_reference(waveguide_problem);

    return {
        VerificationCase{
            .id = "plane-wave-1d-vacuum",
            .kind = VerificationCaseKind::plane_wave_1d,
            .problem = plane_wave_problem,
            .analytic_reference = *plane_wave.reference,
            .requirements = {
                maximum_requirement(
                    MetricKind::phase_velocity_relative_error,
                    0.01,
                    "phase velocity must approach the analytic lossless-medium value"),
                maximum_requirement(
                    MetricKind::field_l2_relative_error,
                    0.02,
                    "sampled fields must match the analytic traveling wave"),
                maximum_requirement(
                    MetricKind::open_boundary_reflection_absolute,
                    1.0e-3,
                    "the open-boundary fixture must not dominate propagation error"),
                maximum_requirement(
                    MetricKind::energy_conservation_relative_residual,
                    0.01,
                    "injected energy must be stored, escaped, or dissipated explicitly"),
                minimum_requirement(
                    MetricKind::observed_convergence_order,
                    1.8,
                    "the reference Yee path should demonstrate near-second-order convergence"),
                maximum_requirement(
                    MetricKind::restart_linf_absolute_error,
                    1.0e-12,
                    "FP64 restart must reproduce the uninterrupted field state"),
            },
        },
        VerificationCase{
            .id = "pec-cavity-tm111",
            .kind = VerificationCaseKind::pec_rectangular_cavity,
            .problem = cavity_problem,
            .analytic_reference = *cavity.reference,
            .requirements = {
                maximum_requirement(
                    MetricKind::resonant_frequency_relative_error,
                    0.01,
                    "PEC cavity resonance must approach the analytic eigenfrequency"),
                maximum_requirement(
                    MetricKind::field_l2_relative_error,
                    0.03,
                    "cavity mode shape must match its separable analytic field"),
                maximum_requirement(
                    MetricKind::energy_conservation_relative_residual,
                    0.005,
                    "lossless PEC cavity energy must remain accounted"),
                minimum_requirement(
                    MetricKind::observed_convergence_order,
                    1.8,
                    "cavity eigenfrequency should demonstrate near-second-order convergence"),
                maximum_requirement(
                    MetricKind::restart_linf_absolute_error,
                    1.0e-12,
                    "FP64 restart must preserve cavity fields and phase"),
            },
        },
        VerificationCase{
            .id = "wr90-waveguide-te10",
            .kind = VerificationCaseKind::rectangular_waveguide,
            .problem = waveguide_problem,
            .analytic_reference = *waveguide.reference,
            .requirements = {
                maximum_requirement(
                    MetricKind::cutoff_frequency_relative_error,
                    0.005,
                    "TE10 cutoff must match the rectangular-waveguide reference"),
                maximum_requirement(
                    MetricKind::propagation_constant_relative_error,
                    0.01,
                    "guided phase progression must match the analytic propagation constant"),
                maximum_requirement(
                    MetricKind::field_l2_relative_error,
                    0.03,
                    "transverse TE10 field shape must match the analytic mode"),
                maximum_requirement(
                    MetricKind::energy_conservation_relative_residual,
                    0.01,
                    "guided power must remain explicitly accounted"),
                minimum_requirement(
                    MetricKind::observed_convergence_order,
                    1.8,
                    "waveguide phase error should demonstrate near-second-order convergence"),
                maximum_requirement(
                    MetricKind::restart_linf_absolute_error,
                    1.0e-12,
                    "FP64 restart must preserve guided fields and phase"),
            },
        },
    };
}

CaseAssessment assess(
    const VerificationCase& test_case,
    const std::span<const MetricObservation> observations)
{
    CaseAssessment result{.case_id = test_case.id, .issues = {}};
    std::map<MetricKind, std::vector<double>> observed;
    for (const auto& observation : observations) {
        observed[observation.metric].push_back(observation.value);
    }

    for (const auto& requirement : test_case.requirements) {
        const auto found = observed.find(requirement.metric);
        if (found == observed.end()) {
            result.issues.push_back(AssessmentIssue{
                .code = AssessmentIssueCode::missing_metric,
                .metric = requirement.metric,
                .message = "required verification metric was not reported",
            });
            continue;
        }
        if (found->second.size() != 1) {
            result.issues.push_back(AssessmentIssue{
                .code = AssessmentIssueCode::duplicate_metric,
                .metric = requirement.metric,
                .message = "verification metric must be reported exactly once",
            });
            continue;
        }
        const auto value = found->second.front();
        if (!std::isfinite(value)) {
            result.issues.push_back(AssessmentIssue{
                .code = AssessmentIssueCode::non_finite_metric,
                .metric = requirement.metric,
                .message = "verification metrics must be finite",
            });
            continue;
        }
        if (value < 0.0
            && requirement.metric != MetricKind::observed_convergence_order) {
            result.issues.push_back(AssessmentIssue{
                .code = AssessmentIssueCode::invalid_metric,
                .metric = requirement.metric,
                .message = "error, reflection, and residual metrics must be nonnegative",
            });
            continue;
        }
        const auto satisfies = requirement.comparison == RequirementComparison::maximum
            ? value <= requirement.limit
            : value >= requirement.limit;
        if (!satisfies) {
            result.issues.push_back(AssessmentIssue{
                .code = AssessmentIssueCode::tolerance_exceeded,
                .metric = requirement.metric,
                .message = requirement.rationale,
            });
        }
    }
    return result;
}

} // namespace oep::test::maxwell
