// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace oep::fdtd {

inline constexpr double vacuum_light_speed = 299'792'458.0;
inline constexpr double vacuum_permeability = 1.256'637'062'12e-6;
inline constexpr double vacuum_permittivity =
    1.0 / (vacuum_permeability * vacuum_light_speed * vacuum_light_speed);

enum class DiagnosticCode {
    non_finite_value,
    non_positive_value,
    insufficient_cells,
    unstable_courant_factor,
    unsupported_boundary,
    invalid_source,
    invalid_probe,
    invalid_mode,
    below_cutoff,
    malformed_checkpoint,
    unsupported_checkpoint_version,
    checkpoint_kind_mismatch,
    checkpoint_checksum_mismatch,
    checkpoint_size_limit,
};

struct Diagnostic {
    DiagnosticCode code{DiagnosticCode::non_finite_value};
    std::string path;
    std::string message;
};

enum class BoundaryCondition {
    periodic,
    perfect_electric_conductor,
    first_order_absorbing,
};

enum class SourceMode {
    soft,
    hard,
};

struct Grid1dConfig {
    std::size_t cells{};
    double length_m{};
    double relative_permittivity{1.0};
    double relative_permeability{1.0};
    double courant_factor{0.99};
    BoundaryCondition left_boundary{BoundaryCondition::first_order_absorbing};
    BoundaryCondition right_boundary{BoundaryCondition::first_order_absorbing};
};

struct ElectricSource1d {
    std::size_t node{};
    double value_v_per_m{};
    SourceMode mode{SourceMode::soft};
};

struct ElectricProbe1d {
    std::size_t node{};
};

struct StepResult {
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept { return diagnostics.empty(); }
};

struct Dispersion1d {
    double wavenumber_rad_per_m{};
    double angular_frequency_rad_per_s{};
    double phase_velocity_m_per_s{};
};

[[nodiscard]] std::vector<Diagnostic> validate(const Grid1dConfig& config);

class YeeGrid1d {
public:
    explicit YeeGrid1d(Grid1dConfig config);

    [[nodiscard]] const Grid1dConfig& config() const noexcept;
    [[nodiscard]] double cell_size_m() const noexcept;
    [[nodiscard]] double time_step_s() const noexcept;
    [[nodiscard]] double time_s() const noexcept;
    [[nodiscard]] std::uint64_t step_index() const noexcept;
    [[nodiscard]] double medium_velocity_m_per_s() const noexcept;
    [[nodiscard]] double medium_impedance_ohm() const noexcept;

    [[nodiscard]] std::span<const double> electric_field() const noexcept;
    [[nodiscard]] std::span<const double> magnetic_field() const noexcept;
    [[nodiscard]] std::span<double> mutable_electric_field() noexcept;
    [[nodiscard]] std::span<double> mutable_magnetic_field() noexcept;
    [[nodiscard]] double electric_position_m(std::size_t node) const;
    [[nodiscard]] double magnetic_position_m(std::size_t cell) const;
    [[nodiscard]] std::optional<double> sample(const ElectricProbe1d& probe) const;

    [[nodiscard]] StepResult step(std::span<const ElectricSource1d> sources = {});
    [[nodiscard]] std::optional<double> last_discrete_energy_j() const noexcept;
    [[nodiscard]] std::vector<std::byte> checkpoint() const;

private:
    friend struct YeeGrid1dRestoreAccess;

    Grid1dConfig config_;
    double dx_{};
    double dt_{};
    double epsilon_{};
    double permeability_{};
    std::uint64_t step_index_{};
    std::vector<double> electric_;
    std::vector<double> magnetic_;
    std::optional<double> last_discrete_energy_j_;
};

struct Restore1dResult {
    std::optional<YeeGrid1d> grid;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept { return grid.has_value(); }
};

[[nodiscard]] Restore1dResult restore_yee_grid_1d(
    std::span<const std::byte> checkpoint);
[[nodiscard]] std::optional<Dispersion1d> periodic_mode_dispersion(
    const Grid1dConfig& config,
    std::size_t mode_index);

enum class FieldComponent {
    electric_x,
    electric_y,
    electric_z,
    magnetic_x,
    magnetic_y,
    magnetic_z,
};

struct Index3d {
    std::size_t i{};
    std::size_t j{};
    std::size_t k{};
};

struct Extent3d {
    std::size_t i{};
    std::size_t j{};
    std::size_t k{};
};

struct Point3d {
    double x_m{};
    double y_m{};
    double z_m{};
};

struct Grid3dConfig {
    std::size_t cells_x{};
    std::size_t cells_y{};
    std::size_t cells_z{};
    double length_x_m{};
    double length_y_m{};
    double length_z_m{};
    double relative_permittivity{1.0};
    double relative_permeability{1.0};
    double courant_factor{0.99};
};

struct ElectricSource3d {
    FieldComponent component{FieldComponent::electric_z};
    Index3d index;
    double value_v_per_m{};
    SourceMode mode{SourceMode::soft};
};

struct FieldProbe3d {
    FieldComponent component{FieldComponent::electric_z};
    Index3d index;
};

struct CavityMode {
    unsigned int m{};
    unsigned int n{};
    unsigned int p{};
};

struct WaveguideMode {
    unsigned int m{};
    unsigned int n{};
};

struct WaveguideDispersion {
    double cutoff_frequency_hz{};
    double propagation_constant_rad_per_m{};
};

[[nodiscard]] std::vector<Diagnostic> validate(const Grid3dConfig& config);

class YeeGrid3d {
public:
    explicit YeeGrid3d(Grid3dConfig config);

    [[nodiscard]] const Grid3dConfig& config() const noexcept;
    [[nodiscard]] double cell_size_x_m() const noexcept;
    [[nodiscard]] double cell_size_y_m() const noexcept;
    [[nodiscard]] double cell_size_z_m() const noexcept;
    [[nodiscard]] double time_step_s() const noexcept;
    [[nodiscard]] double time_s() const noexcept;
    [[nodiscard]] std::uint64_t step_index() const noexcept;
    [[nodiscard]] double medium_velocity_m_per_s() const noexcept;

    [[nodiscard]] Extent3d extent(FieldComponent component) const noexcept;
    [[nodiscard]] Point3d position(FieldComponent component, Index3d index) const;
    [[nodiscard]] std::span<const double> field(FieldComponent component) const noexcept;
    [[nodiscard]] std::span<double> mutable_field(FieldComponent component) noexcept;
    [[nodiscard]] std::optional<double> sample(const FieldProbe3d& probe) const;
    [[nodiscard]] bool set(FieldComponent component, Index3d index, double value);

    void center_magnetic_half_step_from_electric();
    [[nodiscard]] StepResult step(std::span<const ElectricSource3d> sources = {});
    [[nodiscard]] std::optional<double> last_discrete_energy_j() const noexcept;
    [[nodiscard]] std::vector<std::byte> checkpoint() const;

private:
    friend struct YeeGrid3dRestoreAccess;

    [[nodiscard]] std::size_t offset(FieldComponent component, Index3d index) const;

    Grid3dConfig config_;
    double dx_{};
    double dy_{};
    double dz_{};
    double dt_{};
    double epsilon_{};
    double permeability_{};
    std::uint64_t step_index_{};
    std::vector<double> electric_x_;
    std::vector<double> electric_y_;
    std::vector<double> electric_z_;
    std::vector<double> magnetic_x_;
    std::vector<double> magnetic_y_;
    std::vector<double> magnetic_z_;
    std::optional<double> last_discrete_energy_j_;
};

struct Restore3dResult {
    std::optional<YeeGrid3d> grid;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept { return grid.has_value(); }
};

[[nodiscard]] Restore3dResult restore_yee_grid_3d(
    std::span<const std::byte> checkpoint);
[[nodiscard]] std::optional<double> cavity_mode_frequency_hz(
    const Grid3dConfig& config,
    CavityMode mode);
[[nodiscard]] std::optional<WaveguideDispersion> waveguide_mode_dispersion(
    const Grid3dConfig& config,
    WaveguideMode mode,
    double operating_frequency_hz);

} // namespace oep::fdtd
