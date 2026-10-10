// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#include <oep/test/maxwell_harness.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numbers>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace {

using namespace oep::test::maxwell;

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
    const double relative_tolerance = 1.0e-12)
{
    const auto scale = std::max({1.0, std::abs(left), std::abs(right)});
    return std::abs(left - right) <= relative_tolerance * scale;
}

void check_diagnostic(
    const std::vector<ReferenceDiagnostic>& diagnostics,
    const ReferenceErrorCode expected,
    const std::string& context)
{
    check(diagnostics.size() == 1, context + " must report one diagnostic");
    check(diagnostics.front().code == expected, context + " reported the wrong diagnostic");
}

void verify_plane_wave_reference()
{
    const auto vacuum = plane_wave_reference(PlaneWaveRequest{.frequency_hz = 1.0e9});
    check(vacuum.succeeded(), "vacuum plane-wave reference must succeed");
    const auto& reference = *vacuum.reference;
    check(
        approximately_equal(reference.phase_velocity_m_per_s, vacuum_light_speed),
        "vacuum phase velocity must equal c0");
    check(
        approximately_equal(reference.wavelength_m, vacuum_light_speed / 1.0e9),
        "vacuum wavelength must equal c0/f");
    check(
        approximately_equal(
            reference.wave_impedance_ohm,
            std::sqrt(vacuum_permeability / vacuum_permittivity)),
        "vacuum impedance must equal sqrt(mu0/epsilon0)");
    check(
        approximately_equal(
            reference.wavenumber_rad_per_m * reference.wavelength_m,
            2.0 * std::numbers::pi),
        "wavenumber and wavelength must be reciprocal");

    const auto dielectric = plane_wave_reference(PlaneWaveRequest{
        .frequency_hz = 1.0e9,
        .relative_permittivity = 4.0,
        .relative_permeability = 1.0,
    });
    check(dielectric.succeeded(), "lossless dielectric plane-wave reference must succeed");
    check(
        approximately_equal(
            dielectric.reference->phase_velocity_m_per_s,
            reference.phase_velocity_m_per_s / 2.0),
        "relative permittivity four must halve phase velocity");
    check(
        approximately_equal(
            dielectric.reference->wave_impedance_ohm,
            reference.wave_impedance_ohm / 2.0),
        "relative permittivity four must halve wave impedance");

    const auto invalid = plane_wave_reference(PlaneWaveRequest{.frequency_hz = 0.0});
    check(!invalid.succeeded(), "zero-frequency plane wave must fail");
    check_diagnostic(
        invalid.diagnostics,
        ReferenceErrorCode::non_positive_input,
        "zero-frequency plane wave");
}

void verify_cavity_reference()
{
    const CavityRequest base{
        .length_x_m = 0.10,
        .length_y_m = 0.08,
        .length_z_m = 0.06,
        .family = ModeFamily::transverse_magnetic,
        .mode = ModeIndices{1, 1, 0},
    };
    const auto tm110 = rectangular_cavity_reference(base);
    check(tm110.succeeded(), "TM110 rectangular-cavity reference must succeed");
    check(
        approximately_equal(
            tm110.reference->angular_frequency_rad_per_s,
            2.0 * std::numbers::pi * tm110.reference->resonant_frequency_hz),
        "cavity angular frequency must equal 2 pi f");

    auto doubled = base;
    doubled.length_x_m *= 2.0;
    doubled.length_y_m *= 2.0;
    doubled.length_z_m *= 2.0;
    const auto scaled = rectangular_cavity_reference(doubled);
    check(scaled.succeeded(), "scaled cavity reference must succeed");
    check(
        approximately_equal(
            scaled.reference->resonant_frequency_hz,
            tm110.reference->resonant_frequency_hz / 2.0),
        "doubling all cavity dimensions must halve resonance frequency");

    auto invalid_tm = base;
    invalid_tm.mode = ModeIndices{1, 0, 1};
    const auto rejected_tm = rectangular_cavity_reference(invalid_tm);
    check(!rejected_tm.succeeded(), "TM mode with a zero transverse index must fail");
    check_diagnostic(
        rejected_tm.diagnostics,
        ReferenceErrorCode::invalid_mode,
        "invalid TM cavity mode");

    auto invalid_te = base;
    invalid_te.family = ModeFamily::transverse_electric;
    invalid_te.mode = ModeIndices{1, 1, 0};
    const auto rejected_te = rectangular_cavity_reference(invalid_te);
    check(!rejected_te.succeeded(), "TE mode with a zero longitudinal index must fail");
    check_diagnostic(
        rejected_te.diagnostics,
        ReferenceErrorCode::invalid_mode,
        "invalid TE cavity mode");
}

void verify_waveguide_reference()
{
    const WaveguideRequest wr90{
        .width_m = 0.02286,
        .height_m = 0.01016,
        .family = ModeFamily::transverse_electric,
        .mode = ModeIndices{1, 0, 0},
        .operating_frequency_hz = 10.0e9,
    };
    const auto te10 = rectangular_waveguide_reference(wr90);
    check(te10.succeeded(), "WR90 TE10 reference at 10 GHz must propagate");
    const auto& reference = *te10.reference;
    check(
        reference.cutoff_frequency_hz > 6.55e9
            && reference.cutoff_frequency_hz < 6.57e9,
        "WR90 TE10 cutoff must be approximately 6.56 GHz");
    check(
        reference.phase_velocity_m_per_s > vacuum_light_speed,
        "waveguide phase velocity must exceed medium light speed");
    check(
        reference.group_velocity_m_per_s < vacuum_light_speed,
        "waveguide group velocity must remain below medium light speed");
    check(
        approximately_equal(
            reference.phase_velocity_m_per_s * reference.group_velocity_m_per_s,
            vacuum_light_speed * vacuum_light_speed),
        "lossless waveguide phase and group velocities must multiply to c squared");
    check(
        reference.mode_impedance_ohm
            > std::sqrt(vacuum_permeability / vacuum_permittivity),
        "TE impedance above cutoff must exceed the medium impedance");

    auto below_cutoff = wr90;
    below_cutoff.operating_frequency_hz = 6.0e9;
    const auto evanescent = rectangular_waveguide_reference(below_cutoff);
    check(!evanescent.succeeded(), "below-cutoff waveguide request must fail");
    check_diagnostic(
        evanescent.diagnostics,
        ReferenceErrorCode::below_cutoff,
        "below-cutoff waveguide request");

    auto invalid_mode = wr90;
    invalid_mode.family = ModeFamily::transverse_magnetic;
    const auto invalid = rectangular_waveguide_reference(invalid_mode);
    check(!invalid.succeeded(), "TM10 waveguide mode must fail");
    check_diagnostic(
        invalid.diagnostics,
        ReferenceErrorCode::invalid_mode,
        "invalid waveguide mode");
}

void verify_metric_helpers()
{
    const auto scalar = relative_error(99.0, 100.0);
    check(scalar.succeeded() && approximately_equal(*scalar.value, 0.01),
        "scalar relative error must be normalized by the reference");

    const std::array observed{1.1, 1.8};
    const std::array expected{1.0, 2.0};
    const auto l2 = relative_l2_error(observed, expected);
    check(l2.succeeded() && approximately_equal(*l2.value, 0.1),
        "relative L2 field error must use the reference L2 norm");
    const auto linf = linf_absolute_error(observed, expected);
    check(linf.succeeded() && approximately_equal(*linf.value, 0.2),
        "L-infinity error must report the largest pointwise difference");

    const auto order = observed_convergence_order(0.04, 0.01, 2.0);
    check(order.succeeded() && approximately_equal(*order.value, 2.0),
        "fourfold error reduction at twofold refinement must be second order");
    const auto energy = normalized_energy_residual(1.0, 1.0, 1.2, 0.5, 0.3);
    check(energy.succeeded() && approximately_equal(*energy.value, 0.0),
        "balanced energy terms must have zero normalized residual");

    const std::array short_samples{1.0};
    const auto incompatible = relative_l2_error(short_samples, expected);
    check(!incompatible.succeeded(), "mismatched field samples must fail");
    check_diagnostic(
        incompatible.diagnostics,
        ReferenceErrorCode::incompatible_samples,
        "mismatched field samples");
    const auto invalid_order = observed_convergence_order(0.04, 0.01, 1.0);
    check(!invalid_order.succeeded(), "unit refinement ratio must fail");
    check_diagnostic(
        invalid_order.diagnostics,
        ReferenceErrorCode::invalid_refinement_ratio,
        "unit refinement ratio");
    const auto invalid_energy = normalized_energy_residual(1.0, 0.0, -1.0, 0.0, 0.0);
    check(!invalid_energy.succeeded(), "negative energy term must fail");
    check_diagnostic(
        invalid_energy.diagnostics,
        ReferenceErrorCode::invalid_energy_balance,
        "negative energy term");
}

[[nodiscard]] std::vector<MetricObservation> passing_observations(
    const VerificationCase& test_case)
{
    std::vector<MetricObservation> observations;
    observations.reserve(test_case.requirements.size());
    for (const auto& requirement : test_case.requirements) {
        observations.push_back(MetricObservation{
            .metric = requirement.metric,
            .value = requirement.comparison == RequirementComparison::maximum
                ? requirement.limit * 0.5
                : requirement.limit + 0.1,
        });
    }
    return observations;
}

void verify_case_contracts()
{
    const auto cases = canonical_verification_cases();
    check(cases.size() == 3, "Maxwell harness must expose three canonical cases");
    std::set<std::string> ids;
    for (const auto& test_case : cases) {
        check(ids.insert(test_case.id).second, "canonical case IDs must be unique");
        check(!test_case.requirements.empty(), "each canonical case must declare requirements");
        check(
            std::ranges::any_of(test_case.requirements, [](const MetricRequirement& requirement) {
                return requirement.metric == MetricKind::energy_conservation_relative_residual;
            }),
            "each canonical case must require an energy balance");
        check(
            std::ranges::any_of(test_case.requirements, [](const MetricRequirement& requirement) {
                return requirement.metric == MetricKind::observed_convergence_order;
            }),
            "each canonical case must require convergence evidence");
        check(
            std::ranges::any_of(test_case.requirements, [](const MetricRequirement& requirement) {
                return requirement.metric == MetricKind::restart_linf_absolute_error;
            }),
            "each canonical case must require restart equivalence");

        auto observations = passing_observations(test_case);
        check(assess(test_case, observations).passed(),
            test_case.id + " must accept metrics within tolerance");

        observations.front().value = test_case.requirements.front().comparison
                == RequirementComparison::maximum
            ? test_case.requirements.front().limit * 2.0
            : test_case.requirements.front().limit - 0.1;
        const auto failed = assess(test_case, observations);
        check(!failed.passed(), test_case.id + " must reject a tolerance violation");
        check(
            failed.issues.front().code == AssessmentIssueCode::tolerance_exceeded,
            "out-of-tolerance observation must retain a typed issue");
    }

    const auto& plane_wave = cases.front();
    check(std::holds_alternative<PlaneWaveRequest>(plane_wave.problem),
        "plane-wave case must retain its physical problem definition");
    check(std::holds_alternative<PlaneWaveReference>(plane_wave.analytic_reference),
        "plane-wave case must retain its analytic reference type");
    auto incomplete = passing_observations(plane_wave);
    incomplete.pop_back();
    const auto missing = assess(plane_wave, incomplete);
    check(!missing.passed(), "missing metric must fail case assessment");
    check(
        missing.issues.front().code == AssessmentIssueCode::missing_metric,
        "missing metric must retain a typed issue");

    auto duplicate = passing_observations(plane_wave);
    duplicate.push_back(duplicate.front());
    const auto duplicated = assess(plane_wave, duplicate);
    check(!duplicated.passed(), "duplicate metric must fail case assessment");
    check(
        duplicated.issues.front().code == AssessmentIssueCode::duplicate_metric,
        "duplicate metric must retain a typed issue");

    auto non_finite = passing_observations(plane_wave);
    non_finite.front().value = std::numeric_limits<double>::quiet_NaN();
    const auto rejected = assess(plane_wave, non_finite);
    check(!rejected.passed(), "non-finite metric must fail case assessment");
    check(
        rejected.issues.front().code == AssessmentIssueCode::non_finite_metric,
        "non-finite metric must retain a typed issue");

    auto negative = passing_observations(plane_wave);
    negative.front().value = -1.0;
    const auto invalid = assess(plane_wave, negative);
    check(!invalid.passed(), "negative error metric must fail case assessment");
    check(
        invalid.issues.front().code == AssessmentIssueCode::invalid_metric,
        "negative error metric must retain a typed issue");
}

} // namespace

int main()
{
    verify_plane_wave_reference();
    verify_cavity_reference();
    verify_waveguide_reference();
    verify_metric_helpers();
    verify_case_contracts();
    std::cout << "Maxwell verification harness passed analytic, metric, and acceptance checks\n";
    return EXIT_SUCCESS;
}
