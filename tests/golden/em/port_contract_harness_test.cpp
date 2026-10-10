// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#include <oep/test/port_contract_harness.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

namespace {

using namespace oep::test::port_contract;

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

[[nodiscard]] bool approximately_equal(
    const Phasor left,
    const Phasor right,
    const double tolerance = 1.0e-12)
{
    const auto scale = std::max({1.0, std::abs(left), std::abs(right)});
    return std::abs(left - right) <= tolerance * scale;
}

void check_diagnostic(
    const std::vector<Diagnostic>& diagnostics,
    const ErrorCode expected,
    const std::string& context)
{
    check(diagnostics.size() == 1, context + " must report exactly one diagnostic");
    check(diagnostics.front().code == expected, context + " reported the wrong diagnostic");
}

void verify_contract_and_modal_anchors(const FourPortFixture& fixture)
{
    check(fixture.contract == contract_version, "fixture must declare the V1 contract version");
    check(fixture.ports.size() == 4, "canonical fixture must contain four port modes");
    for (const auto& port : fixture.ports) {
        check(validate_port_mode(port).empty(), port.port_id + " mode contract must validate");
        const auto anchored = anchor_modal_phase(port.phase_anchor);
        check(anchored.succeeded(), port.port_id + " phase anchor must resolve");
        check(
            anchored.value->anchored_overlap.real() > 0.0,
            port.port_id + " anchored overlap must be positive");
        check(
            std::abs(anchored.value->anchored_overlap.imag()) <= 1.0e-15,
            port.port_id + " anchored overlap must be real");
        check(
            approximately_equal(std::abs(anchored.value->multiplier), 1.0),
            port.port_id + " phase correction must have unit magnitude");
    }

    auto weak_anchor = fixture.ports.front().phase_anchor;
    weak_anchor.measured_overlap = {1.0e-12, 0.0};
    const auto weak = anchor_modal_phase(weak_anchor);
    check(!weak.succeeded(), "near-zero modal overlap must be rejected");
    check_diagnostic(weak.diagnostics, ErrorCode::invalid_phase_anchor, "weak phase anchor");

    auto degenerate = fixture.ports.front().phase_anchor;
    degenerate.near_degenerate = true;
    const auto ambiguous = anchor_modal_phase(degenerate);
    check(!ambiguous.succeeded(), "untracked degenerate mode must be rejected");
    check_diagnostic(
        ambiguous.diagnostics, ErrorCode::ambiguous_mode_basis, "degenerate mode");
    degenerate.orthogonal_basis_id = "P1-degenerate-pair";
    degenerate.tracking_reference_id = "P1-previous-frequency";
    degenerate.tracking_overlap = 0.97;
    check(
        anchor_modal_phase(degenerate).succeeded(),
        "tracked explicit basis must resolve a degenerate mode");

    auto invalid_mode = fixture.ports.front();
    invalid_mode.reference_impedance_ohm = -50.0;
    const auto invalid_impedance = validate_port_mode(invalid_mode);
    check(
        std::ranges::any_of(invalid_impedance, [](const Diagnostic& item) {
            return item.code == ErrorCode::non_positive_reference_impedance;
        }),
        "non-positive-real mode impedance must be rejected");
    invalid_mode = fixture.ports.front();
    invalid_mode.propagation_constant_rad_per_m = 0.0;
    const auto cutoff = validate_port_mode(invalid_mode);
    check(
        std::ranges::any_of(cutoff, [](const Diagnostic& item) {
            return item.code == ErrorCode::non_propagating_mode;
        }),
        "cutoff or evanescent mode must be rejected");
}

void verify_rms_power_waves()
{
    const Phasor voltage{4.0, 2.0};
    const Phasor current{0.03, -0.01};
    constexpr auto impedance = 50.0;
    const auto waves = rms_power_waves(voltage, current, impedance);
    check(waves.succeeded(), "finite RMS voltage/current pair must normalize");
    const auto restored = rms_voltage_current(
        waves.value->incident_sqrt_w, waves.value->outgoing_sqrt_w, impedance);
    check(restored.succeeded(), "power waves must reconstruct voltage and current");
    check(
        approximately_equal(restored.value->voltage_v, voltage),
        "RMS voltage must survive the wave round trip");
    check(
        approximately_equal(restored.value->current_into_domain_a, current),
        "inward RMS current must survive the wave round trip");

    const auto terminal_power = std::real(voltage * std::conj(current));
    const auto wave_power = std::norm(waves.value->incident_sqrt_w)
        - std::norm(waves.value->outgoing_sqrt_w);
    check(
        approximately_equal(terminal_power, wave_power),
        "RMS terminal power must equal incident minus outgoing modal power");

    const auto reversed = rms_power_waves(voltage, -current, impedance);
    check(reversed.succeeded(), "reversed current orientation must remain representable");
    check(
        approximately_equal(
            reversed.value->incident_sqrt_w, waves.value->outgoing_sqrt_w),
        "reversing the current orientation must swap incident and outgoing waves");
    check(
        approximately_equal(
            reversed.value->outgoing_sqrt_w, waves.value->incident_sqrt_w),
        "reversing the current orientation must swap outgoing and incident waves");

    const auto matched = rms_power_waves({1.0, 0.0}, {1.0, 0.0}, 1.0);
    check(matched.succeeded(), "unit matched RMS state must normalize");
    check(
        approximately_equal(matched.value->incident_sqrt_w, {1.0, 0.0})
            && approximately_equal(matched.value->outgoing_sqrt_w, {0.0, 0.0}),
        "a 1 V RMS matched state at 1 ohm must carry 1 W incident power");

    const auto invalid = rms_power_waves(voltage, current, 0.0);
    check(!invalid.succeeded(), "zero reference impedance must fail");
    check_diagnostic(
        invalid.diagnostics,
        ErrorCode::non_positive_reference_impedance,
        "zero reference impedance");
}

void verify_four_port_basis(const FourPortFixture& fixture)
{
    const auto assembled = assemble_response_basis(fixture.basis_runs, fixture.ports.size());
    check(assembled.succeeded(), "four matched one-port runs must assemble a response basis");
    const auto& basis = *assembled.value;

    const auto reciprocity = reciprocity_error(basis.scattering);
    check(reciprocity.succeeded(), "square scattering matrix must support reciprocity check");
    check(
        approximately_equal(*reciprocity.value, 0.0),
        "canonical reciprocal four-port must have a symmetric scattering matrix");

    const auto maximum_gain = maximum_power_gain(basis.scattering);
    check(maximum_gain.succeeded(), "square scattering matrix must support passivity check");
    check(
        approximately_equal(*maximum_gain.value, 0.17268748261258818, 1.0e-10),
        "canonical four-port maximum power gain must match the analytic fixture");
    check(*maximum_gain.value < 1.0, "canonical four-port must be passive");

    auto active = basis.scattering;
    active.at(0, 0) = {1.1, 0.0};
    const auto active_gain = maximum_power_gain(active);
    check(
        active_gain.succeeded() && *active_gain.value > 1.0,
        "an active scattering fixture must fail the passive power-gain bound");

    for (const auto& test_case : fixture.direct_cases) {
        const auto response = reconstruct(basis, test_case.incident_waves_sqrt_w);
        check(response.succeeded(), test_case.id + " must reconstruct from the stored basis");
        check(
            response.value->outgoing_waves_sqrt_w.size()
                == test_case.expected_outgoing_waves_sqrt_w.size(),
            test_case.id + " must preserve outgoing port count");
        check(
            response.value->far_field_v_per_m.size()
                == test_case.expected_far_field_v_per_m.size(),
            test_case.id + " must preserve field sample count");
        for (std::size_t port = 0;
             port < test_case.expected_outgoing_waves_sqrt_w.size(); ++port) {
            check(
                approximately_equal(
                    response.value->outgoing_waves_sqrt_w[port],
                    test_case.expected_outgoing_waves_sqrt_w[port]),
                test_case.id + " outgoing wave differs from direct excitation");
        }
        for (std::size_t sample = 0;
             sample < test_case.expected_far_field_v_per_m.size(); ++sample) {
            check(
                approximately_equal(
                    response.value->far_field_v_per_m[sample],
                    test_case.expected_far_field_v_per_m[sample]),
                test_case.id + " far field differs from direct excitation");
        }

        const auto balance = power_balance(
            test_case.incident_waves_sqrt_w, response.value->outgoing_waves_sqrt_w);
        check(balance.succeeded(), test_case.id + " must produce a power balance");
        check(balance.value->accepted_w > 0.0, test_case.id + " must accept positive power");
        const auto directivity = metric_denominator_w(
            MetricDenominator::radiated_power, *balance.value, 0.8 * balance.value->accepted_w);
        const auto gain = metric_denominator_w(
            MetricDenominator::accepted_power, *balance.value, 0.8 * balance.value->accepted_w);
        const auto realized_gain = metric_denominator_w(
            MetricDenominator::total_incident_power,
            *balance.value,
            0.8 * balance.value->accepted_w);
        check(
            directivity.succeeded()
                && approximately_equal(*directivity.value, 0.8 * balance.value->accepted_w),
            "directivity denominator must be radiated power");
        check(
            gain.succeeded() && approximately_equal(*gain.value, balance.value->accepted_w),
            "gain denominator must be accepted power");
        check(
            realized_gain.succeeded()
                && approximately_equal(*realized_gain.value, balance.value->incident_w),
            "realized-gain denominator must be total incident power");
    }

    auto invalid_runs = fixture.basis_runs;
    invalid_runs.front().incident_waves_sqrt_w[1] = {0.01, 0.0};
    const auto unmatched = assemble_response_basis(invalid_runs, fixture.ports.size());
    check(!unmatched.succeeded(), "an incident wave on an inactive port must fail");
    check_diagnostic(
        unmatched.diagnostics, ErrorCode::invalid_basis_run, "unmatched inactive port");
}

void verify_reference_plane_shift(const FourPortFixture& fixture)
{
    const auto assembled = assemble_response_basis(fixture.basis_runs, fixture.ports.size());
    check(assembled.succeeded(), "reference-plane test requires the canonical basis");
    const auto shifted = shift_reference_planes(
        assembled.value->scattering,
        fixture.ports,
        fixture.inward_reference_plane_shifts_m);
    check(shifted.succeeded(), "finite inward reference-plane shifts must succeed");

    const auto row = std::size_t{2};
    const auto column = std::size_t{0};
    const auto expected_phase = std::polar(
        1.0,
        fixture.ports[row].propagation_constant_rad_per_m
                * fixture.inward_reference_plane_shifts_m[row]
            + fixture.ports[column].propagation_constant_rad_per_m
                * fixture.inward_reference_plane_shifts_m[column]);
    check(
        approximately_equal(
            shifted.value->at(row, column),
            expected_phase * assembled.value->scattering.at(row, column)),
        "positive-jwt inward plane shift must apply exp(+j beta_i d_i) on both ports");

    std::vector<double> inverse_shifts;
    inverse_shifts.reserve(fixture.inward_reference_plane_shifts_m.size());
    for (const auto shift : fixture.inward_reference_plane_shifts_m) {
        inverse_shifts.push_back(-shift);
    }
    const auto restored = shift_reference_planes(*shifted.value, fixture.ports, inverse_shifts);
    check(restored.succeeded(), "signed inverse reference-plane shift must succeed");
    for (std::size_t index = 0; index < restored.value->values.size(); ++index) {
        check(
            approximately_equal(
                restored.value->values[index], assembled.value->scattering.values[index]),
            "inverse plane shift must restore every scattering parameter");
    }

    const auto original_gain = maximum_power_gain(assembled.value->scattering);
    const auto shifted_gain = maximum_power_gain(*shifted.value);
    const auto shifted_reciprocity = reciprocity_error(*shifted.value);
    check(original_gain.succeeded() && shifted_gain.succeeded(), "shifted passivity must assess");
    check(
        approximately_equal(*original_gain.value, *shifted_gain.value, 1.0e-10),
        "lossless plane shifts must preserve maximum power gain");
    check(
        shifted_reciprocity.succeeded()
            && approximately_equal(*shifted_reciprocity.value, 0.0),
        "equal port-side phase factors must preserve reciprocity");
}

} // namespace

int main()
{
    const auto fixture = canonical_four_port_fixture();
    verify_contract_and_modal_anchors(fixture);
    verify_rms_power_waves();
    verify_four_port_basis(fixture);
    verify_reference_plane_shift(fixture);
    std::cout << "RMS four-port normalization, phase, reconstruction, passivity, reciprocity, and reference-plane contract passed\n";
}
