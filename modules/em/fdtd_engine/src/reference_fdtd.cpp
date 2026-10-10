// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#include <oep/fdtd/reference_fdtd.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace oep::fdtd {
namespace {

constexpr std::uint32_t checkpoint_version = 1;
constexpr std::uint32_t checkpoint_kind_1d = 1;
constexpr std::uint32_t checkpoint_kind_3d = 3;
constexpr std::size_t maximum_field_values = 100'000'000;
constexpr std::string_view checkpoint_magic = "OEPFDTD1";

[[nodiscard]] Diagnostic diagnostic(
    const DiagnosticCode code,
    std::string path,
    std::string message)
{
    return Diagnostic{
        .code = code,
        .path = std::move(path),
        .message = std::move(message),
    };
}

[[nodiscard]] bool finite_positive(const double value)
{
    return std::isfinite(value) && value > 0.0;
}

[[nodiscard]] double medium_velocity(
    const double relative_permittivity,
    const double relative_permeability)
{
    return vacuum_light_speed
        / std::sqrt(relative_permittivity)
        / std::sqrt(relative_permeability);
}

[[nodiscard]] bool checked_product(
    const std::size_t first,
    const std::size_t second,
    const std::size_t third,
    std::size_t& product)
{
    if (first == 0 || second == 0 || third == 0) {
        product = 0;
        return true;
    }
    if (first > std::numeric_limits<std::size_t>::max() / second) {
        return false;
    }
    const auto first_two = first * second;
    if (first_two > std::numeric_limits<std::size_t>::max() / third) {
        return false;
    }
    product = first_two * third;
    return product <= maximum_field_values;
}

[[nodiscard]] std::size_t flat_offset(
    const Extent3d extent,
    const Index3d index)
{
    return (index.i * extent.j + index.j) * extent.k + index.k;
}

[[nodiscard]] bool contains(const Extent3d extent, const Index3d index)
{
    return index.i < extent.i && index.j < extent.j && index.k < extent.k;
}

[[nodiscard]] bool is_electric(const FieldComponent component)
{
    return component == FieldComponent::electric_x
        || component == FieldComponent::electric_y
        || component == FieldComponent::electric_z;
}

[[nodiscard]] bool is_tangential_pec_boundary(
    const Grid3dConfig& config,
    const FieldComponent component,
    const Index3d index)
{
    switch (component) {
    case FieldComponent::electric_x:
        return index.j == 0 || index.j == config.cells_y
            || index.k == 0 || index.k == config.cells_z;
    case FieldComponent::electric_y:
        return index.i == 0 || index.i == config.cells_x
            || index.k == 0 || index.k == config.cells_z;
    case FieldComponent::electric_z:
        return index.i == 0 || index.i == config.cells_x
            || index.j == 0 || index.j == config.cells_y;
    default:
        return false;
    }
}

[[nodiscard]] std::uint64_t fnv1a(const std::span<const std::byte> bytes)
{
    auto hash = std::uint64_t{14'695'981'039'346'656'037ULL};
    for (const auto byte : bytes) {
        hash ^= static_cast<std::uint64_t>(std::to_integer<unsigned int>(byte));
        hash *= 1'099'511'628'211ULL;
    }
    return hash;
}

class Writer {
public:
    void u8(const std::uint8_t value)
    {
        bytes_.push_back(static_cast<std::byte>(value));
    }

    void u32(const std::uint32_t value)
    {
        for (unsigned int shift = 0; shift < 32; shift += 8) {
            u8(static_cast<std::uint8_t>((value >> shift) & 0xffU));
        }
    }

    void u64(const std::uint64_t value)
    {
        for (unsigned int shift = 0; shift < 64; shift += 8) {
            u8(static_cast<std::uint8_t>((value >> shift) & 0xffU));
        }
    }

    void f64(const double value)
    {
        u64(std::bit_cast<std::uint64_t>(value));
    }

    void text(const std::string_view value)
    {
        for (const auto character : value) {
            u8(static_cast<std::uint8_t>(character));
        }
    }

    void values(const std::span<const double> values)
    {
        u64(static_cast<std::uint64_t>(values.size()));
        for (const auto value : values) {
            f64(value);
        }
    }

    [[nodiscard]] std::span<const std::byte> view() const noexcept { return bytes_; }
    [[nodiscard]] std::vector<std::byte> finish() && { return std::move(bytes_); }

private:
    std::vector<std::byte> bytes_;
};

class DecodeFailure final : public std::runtime_error {
public:
    DecodeFailure(const DiagnosticCode code, std::string message)
        : std::runtime_error(std::move(message))
        , code(code)
    {
    }

    DiagnosticCode code;
};

class Reader {
public:
    explicit Reader(const std::span<const std::byte> bytes)
        : bytes_(bytes)
    {
    }

    [[nodiscard]] std::uint8_t u8()
    {
        require(1);
        return static_cast<std::uint8_t>(
            std::to_integer<unsigned int>(bytes_[position_++]));
    }

    [[nodiscard]] std::uint32_t u32()
    {
        auto value = std::uint32_t{};
        for (unsigned int shift = 0; shift < 32; shift += 8) {
            value |= static_cast<std::uint32_t>(u8()) << shift;
        }
        return value;
    }

    [[nodiscard]] std::uint64_t u64()
    {
        auto value = std::uint64_t{};
        for (unsigned int shift = 0; shift < 64; shift += 8) {
            value |= static_cast<std::uint64_t>(u8()) << shift;
        }
        return value;
    }

    [[nodiscard]] double f64()
    {
        return std::bit_cast<double>(u64());
    }

    void expect_text(const std::string_view expected)
    {
        for (const auto character : expected) {
            if (u8() != static_cast<std::uint8_t>(character)) {
                throw DecodeFailure(
                    DiagnosticCode::malformed_checkpoint,
                    "checkpoint magic does not match");
            }
        }
    }

    [[nodiscard]] std::vector<double> values(const std::size_t expected_size)
    {
        const auto encoded_size = u64();
        if (encoded_size != static_cast<std::uint64_t>(expected_size)) {
            throw DecodeFailure(
                DiagnosticCode::malformed_checkpoint,
                "checkpoint field extent does not match its grid configuration");
        }
        std::vector<double> result(expected_size);
        for (auto& value : result) {
            value = f64();
            if (!std::isfinite(value)) {
                throw DecodeFailure(
                    DiagnosticCode::malformed_checkpoint,
                    "checkpoint contains a nonfinite field value");
            }
        }
        return result;
    }

    [[nodiscard]] bool empty() const noexcept { return position_ == bytes_.size(); }

private:
    void require(const std::size_t count)
    {
        if (count > bytes_.size() - position_) {
            throw DecodeFailure(
                DiagnosticCode::malformed_checkpoint,
                "checkpoint ended before all fields were decoded");
        }
    }

    std::span<const std::byte> bytes_;
    std::size_t position_{};
};

[[nodiscard]] std::uint64_t trailing_u64(const std::span<const std::byte> bytes)
{
    if (bytes.size() < sizeof(std::uint64_t)) {
        throw DecodeFailure(
            DiagnosticCode::malformed_checkpoint,
            "checkpoint is shorter than its checksum");
    }
    auto value = std::uint64_t{};
    const auto offset = bytes.size() - sizeof(std::uint64_t);
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        const auto index = offset + static_cast<std::size_t>(shift / 8U);
        value |= static_cast<std::uint64_t>(
                     std::to_integer<unsigned int>(bytes[index]))
            << shift;
    }
    return value;
}

[[nodiscard]] Reader checkpoint_reader(
    const std::span<const std::byte> checkpoint,
    const std::uint32_t expected_kind)
{
    if (checkpoint.size() < checkpoint_magic.size() + 16U) {
        throw DecodeFailure(
            DiagnosticCode::malformed_checkpoint,
            "checkpoint is too short");
    }
    const auto content = checkpoint.first(checkpoint.size() - sizeof(std::uint64_t));
    if (fnv1a(content) != trailing_u64(checkpoint)) {
        throw DecodeFailure(
            DiagnosticCode::checkpoint_checksum_mismatch,
            "checkpoint checksum does not match its payload");
    }
    Reader reader(content);
    reader.expect_text(checkpoint_magic);
    if (reader.u32() != checkpoint_version) {
        throw DecodeFailure(
            DiagnosticCode::unsupported_checkpoint_version,
            "checkpoint version is not supported");
    }
    if (reader.u32() != expected_kind) {
        throw DecodeFailure(
            DiagnosticCode::checkpoint_kind_mismatch,
            "checkpoint contains a different Yee-grid dimensionality");
    }
    return reader;
}

void finish_checkpoint(Writer& writer)
{
    const auto checksum = fnv1a(writer.view());
    writer.u64(checksum);
}

[[nodiscard]] std::uint32_t encoded_boundary(const BoundaryCondition boundary)
{
    return static_cast<std::uint32_t>(boundary);
}

[[nodiscard]] BoundaryCondition decoded_boundary(const std::uint32_t encoded)
{
    if (encoded > static_cast<std::uint32_t>(BoundaryCondition::first_order_absorbing)) {
        throw DecodeFailure(
            DiagnosticCode::malformed_checkpoint,
            "checkpoint contains an unknown boundary condition");
    }
    return static_cast<BoundaryCondition>(encoded);
}

} // namespace

std::vector<Diagnostic> validate(const Grid1dConfig& config)
{
    std::vector<Diagnostic> diagnostics;
    if (config.cells < 4) {
        diagnostics.push_back(diagnostic(
            DiagnosticCode::insufficient_cells,
            "cells",
            "a one-dimensional Yee grid requires at least four cells"));
    }
    if (config.cells > (maximum_field_values - 1U) / 2U) {
        diagnostics.push_back(diagnostic(
            DiagnosticCode::checkpoint_size_limit,
            "cells",
            "grid cell count exceeds the reference solver field limit"));
    }
    if (!finite_positive(config.length_m)) {
        diagnostics.push_back(diagnostic(
            std::isfinite(config.length_m) ? DiagnosticCode::non_positive_value
                                           : DiagnosticCode::non_finite_value,
            "length_m",
            "grid length must be finite and positive"));
    }
    if (!finite_positive(config.relative_permittivity)) {
        diagnostics.push_back(diagnostic(
            std::isfinite(config.relative_permittivity) ? DiagnosticCode::non_positive_value
                                                        : DiagnosticCode::non_finite_value,
            "relative_permittivity",
            "relative permittivity must be finite and positive"));
    }
    if (!finite_positive(config.relative_permeability)) {
        diagnostics.push_back(diagnostic(
            std::isfinite(config.relative_permeability) ? DiagnosticCode::non_positive_value
                                                        : DiagnosticCode::non_finite_value,
            "relative_permeability",
            "relative permeability must be finite and positive"));
    }
    if (finite_positive(config.relative_permittivity)
        && finite_positive(config.relative_permeability)
        && !finite_positive(medium_velocity(
            config.relative_permittivity, config.relative_permeability))) {
        diagnostics.push_back(diagnostic(
            DiagnosticCode::non_finite_value,
            "material",
            "material constants do not produce a representable wave velocity"));
    }
    if (!std::isfinite(config.courant_factor)
        || config.courant_factor <= 0.0 || config.courant_factor > 1.0) {
        diagnostics.push_back(diagnostic(
            DiagnosticCode::unstable_courant_factor,
            "courant_factor",
            "one-dimensional Courant factor must be finite and in (0, 1]"));
    }
    const auto left_periodic = config.left_boundary == BoundaryCondition::periodic;
    const auto right_periodic = config.right_boundary == BoundaryCondition::periodic;
    if (left_periodic != right_periodic) {
        diagnostics.push_back(diagnostic(
            DiagnosticCode::unsupported_boundary,
            "boundaries",
            "periodic boundaries must be selected on both ends"));
    }
    return diagnostics;
}

YeeGrid1d::YeeGrid1d(Grid1dConfig config)
    : config_(std::move(config))
{
    const auto diagnostics = validate(config_);
    if (!diagnostics.empty()) {
        throw std::invalid_argument(diagnostics.front().message);
    }
    dx_ = config_.length_m / static_cast<double>(config_.cells);
    const auto velocity = medium_velocity(
        config_.relative_permittivity, config_.relative_permeability);
    dt_ = config_.courant_factor * dx_ / velocity;
    epsilon_ = vacuum_permittivity * config_.relative_permittivity;
    permeability_ = vacuum_permeability * config_.relative_permeability;
    const auto periodic = config_.left_boundary == BoundaryCondition::periodic;
    electric_.assign(config_.cells + (periodic ? 0U : 1U), 0.0);
    magnetic_.assign(config_.cells, 0.0);
}

const Grid1dConfig& YeeGrid1d::config() const noexcept { return config_; }
double YeeGrid1d::cell_size_m() const noexcept { return dx_; }
double YeeGrid1d::time_step_s() const noexcept { return dt_; }
double YeeGrid1d::time_s() const noexcept { return static_cast<double>(step_index_) * dt_; }
std::uint64_t YeeGrid1d::step_index() const noexcept { return step_index_; }

double YeeGrid1d::medium_velocity_m_per_s() const noexcept
{
    return medium_velocity(config_.relative_permittivity, config_.relative_permeability);
}

double YeeGrid1d::medium_impedance_ohm() const noexcept
{
    return std::sqrt(permeability_ / epsilon_);
}

std::span<const double> YeeGrid1d::electric_field() const noexcept { return electric_; }
std::span<const double> YeeGrid1d::magnetic_field() const noexcept { return magnetic_; }
std::span<double> YeeGrid1d::mutable_electric_field() noexcept { return electric_; }
std::span<double> YeeGrid1d::mutable_magnetic_field() noexcept { return magnetic_; }

double YeeGrid1d::electric_position_m(const std::size_t node) const
{
    if (node >= electric_.size()) {
        throw std::out_of_range("electric-field node is outside the one-dimensional grid");
    }
    return static_cast<double>(node) * dx_;
}

double YeeGrid1d::magnetic_position_m(const std::size_t cell) const
{
    if (cell >= magnetic_.size()) {
        throw std::out_of_range("magnetic-field cell is outside the one-dimensional grid");
    }
    return (static_cast<double>(cell) + 0.5) * dx_;
}

std::optional<double> YeeGrid1d::sample(const ElectricProbe1d& probe) const
{
    if (probe.node >= electric_.size()) {
        return std::nullopt;
    }
    return electric_[probe.node];
}

StepResult YeeGrid1d::step(const std::span<const ElectricSource1d> sources)
{
    StepResult result;
    const auto periodic = config_.left_boundary == BoundaryCondition::periodic;
    for (std::size_t source_index = 0; source_index < sources.size(); ++source_index) {
        const auto& source = sources[source_index];
        const auto path = "sources[" + std::to_string(source_index) + "]";
        if (source.node >= electric_.size() || !std::isfinite(source.value_v_per_m)) {
            result.diagnostics.push_back(diagnostic(
                DiagnosticCode::invalid_source,
                path,
                "source node and value must describe a finite in-grid electric sample"));
            continue;
        }
        const auto left_pec = source.node == 0
            && config_.left_boundary == BoundaryCondition::perfect_electric_conductor;
        const auto right_pec = !periodic && source.node + 1 == electric_.size()
            && config_.right_boundary == BoundaryCondition::perfect_electric_conductor;
        if (left_pec || right_pec) {
            result.diagnostics.push_back(diagnostic(
                DiagnosticCode::invalid_source,
                path,
                "an electric source cannot overwrite a PEC boundary node"));
        }
    }
    if (!result.succeeded()) {
        return result;
    }

    const auto previous_electric = electric_;
    const auto magnetic_coefficient = dt_ / (permeability_ * dx_);
    if (periodic) {
        for (std::size_t index = 0; index < magnetic_.size(); ++index) {
            const auto next = (index + 1U) % electric_.size();
            magnetic_[index] -= magnetic_coefficient
                * (electric_[next] - electric_[index]);
        }
    } else {
        for (std::size_t index = 0; index < magnetic_.size(); ++index) {
            magnetic_[index] -= magnetic_coefficient
                * (electric_[index + 1U] - electric_[index]);
        }
    }

    const auto electric_coefficient = dt_ / (epsilon_ * dx_);
    if (periodic) {
        for (std::size_t index = 0; index < electric_.size(); ++index) {
            const auto previous = index == 0 ? magnetic_.size() - 1U : index - 1U;
            electric_[index] -= electric_coefficient
                * (magnetic_[index] - magnetic_[previous]);
        }
    } else {
        for (std::size_t index = 1; index + 1U < electric_.size(); ++index) {
            electric_[index] -= electric_coefficient
                * (magnetic_[index] - magnetic_[index - 1U]);
        }

        const auto mur_coefficient = (config_.courant_factor - 1.0)
            / (config_.courant_factor + 1.0);
        if (config_.left_boundary == BoundaryCondition::first_order_absorbing) {
            electric_.front() = previous_electric[1]
                + mur_coefficient * (electric_[1] - previous_electric.front());
        } else {
            electric_.front() = 0.0;
        }
        if (config_.right_boundary == BoundaryCondition::first_order_absorbing) {
            electric_.back() = previous_electric[previous_electric.size() - 2U]
                + mur_coefficient
                    * (electric_[electric_.size() - 2U] - previous_electric.back());
        } else {
            electric_.back() = 0.0;
        }
    }

    for (const auto& source : sources) {
        if (source.mode == SourceMode::hard) {
            electric_[source.node] = source.value_v_per_m;
        } else {
            electric_[source.node] += source.value_v_per_m;
        }
    }

    auto electric_sum = 0.0;
    for (std::size_t index = 0; index < electric_.size(); ++index) {
        auto weight = 1.0;
        if (!periodic && (index == 0 || index + 1U == electric_.size())) {
            weight = 0.5;
        }
        electric_sum += weight * previous_electric[index] * electric_[index];
    }
    auto magnetic_sum = 0.0;
    for (const auto value : magnetic_) {
        magnetic_sum += value * value;
    }
    last_discrete_energy_j_ = 0.5 * dx_
        * (epsilon_ * electric_sum + permeability_ * magnetic_sum);
    ++step_index_;
    return result;
}

std::optional<double> YeeGrid1d::last_discrete_energy_j() const noexcept
{
    return last_discrete_energy_j_;
}

std::optional<Dispersion1d> periodic_mode_dispersion(
    const Grid1dConfig& config,
    const std::size_t mode_index)
{
    if (!validate(config).empty()
        || config.left_boundary != BoundaryCondition::periodic
        || mode_index == 0 || mode_index > config.cells / 2U) {
        return std::nullopt;
    }
    const auto dx = config.length_m / static_cast<double>(config.cells);
    const auto velocity = medium_velocity(
        config.relative_permittivity, config.relative_permeability);
    const auto dt = config.courant_factor * dx / velocity;
    const auto wavenumber = 2.0 * std::numbers::pi
        * static_cast<double>(mode_index) / config.length_m;
    const auto argument = config.courant_factor
        * std::sin(0.5 * wavenumber * dx);
    if (std::abs(argument) > 1.0) {
        return std::nullopt;
    }
    const auto angular_frequency = 2.0 * std::asin(argument) / dt;
    return Dispersion1d{
        .wavenumber_rad_per_m = wavenumber,
        .angular_frequency_rad_per_s = angular_frequency,
        .phase_velocity_m_per_s = angular_frequency / wavenumber,
    };
}

std::vector<Diagnostic> validate(const Grid3dConfig& config)
{
    std::vector<Diagnostic> diagnostics;
    const auto validate_cells = [&diagnostics](const std::size_t cells, const char* path) {
        if (cells < 2) {
            diagnostics.push_back(diagnostic(
                DiagnosticCode::insufficient_cells,
                path,
                "each three-dimensional Yee-grid axis requires at least two cells"));
        }
    };
    validate_cells(config.cells_x, "cells_x");
    validate_cells(config.cells_y, "cells_y");
    validate_cells(config.cells_z, "cells_z");

    const auto validate_length = [&diagnostics](const double length, const char* path) {
        if (!finite_positive(length)) {
            diagnostics.push_back(diagnostic(
                std::isfinite(length) ? DiagnosticCode::non_positive_value
                                      : DiagnosticCode::non_finite_value,
                path,
                "grid length must be finite and positive"));
        }
    };
    validate_length(config.length_x_m, "length_x_m");
    validate_length(config.length_y_m, "length_y_m");
    validate_length(config.length_z_m, "length_z_m");

    if (!finite_positive(config.relative_permittivity)) {
        diagnostics.push_back(diagnostic(
            std::isfinite(config.relative_permittivity) ? DiagnosticCode::non_positive_value
                                                        : DiagnosticCode::non_finite_value,
            "relative_permittivity",
            "relative permittivity must be finite and positive"));
    }
    if (!finite_positive(config.relative_permeability)) {
        diagnostics.push_back(diagnostic(
            std::isfinite(config.relative_permeability) ? DiagnosticCode::non_positive_value
                                                        : DiagnosticCode::non_finite_value,
            "relative_permeability",
            "relative permeability must be finite and positive"));
    }
    if (finite_positive(config.relative_permittivity)
        && finite_positive(config.relative_permeability)
        && !finite_positive(medium_velocity(
            config.relative_permittivity, config.relative_permeability))) {
        diagnostics.push_back(diagnostic(
            DiagnosticCode::non_finite_value,
            "material",
            "material constants do not produce a representable wave velocity"));
    }
    if (!std::isfinite(config.courant_factor)
        || config.courant_factor <= 0.0 || config.courant_factor > 1.0) {
        diagnostics.push_back(diagnostic(
            DiagnosticCode::unstable_courant_factor,
            "courant_factor",
            "three-dimensional Courant factor must be finite and in (0, 1]"));
    }

    const auto axes_fit = config.cells_x < maximum_field_values
        && config.cells_y < maximum_field_values
        && config.cells_z < maximum_field_values;
    auto dimensions_fit = axes_fit;
    auto total_values = std::size_t{};
    if (axes_fit) {
        const std::array extents{
            Extent3d{config.cells_x, config.cells_y + 1U, config.cells_z + 1U},
            Extent3d{config.cells_x + 1U, config.cells_y, config.cells_z + 1U},
            Extent3d{config.cells_x + 1U, config.cells_y + 1U, config.cells_z},
            Extent3d{config.cells_x + 1U, config.cells_y, config.cells_z},
            Extent3d{config.cells_x, config.cells_y + 1U, config.cells_z},
            Extent3d{config.cells_x, config.cells_y, config.cells_z + 1U},
        };
        for (const auto extent : extents) {
            std::size_t field_values{};
            if (!checked_product(extent.i, extent.j, extent.k, field_values)
                || field_values > maximum_field_values - total_values) {
                dimensions_fit = false;
                break;
            }
            total_values += field_values;
        }
    }
    if (!dimensions_fit) {
        diagnostics.push_back(diagnostic(
            DiagnosticCode::checkpoint_size_limit,
            "cells",
            "grid dimensions exceed the reference solver field limit"));
    }
    return diagnostics;
}

YeeGrid3d::YeeGrid3d(Grid3dConfig config)
    : config_(std::move(config))
{
    const auto diagnostics = validate(config_);
    if (!diagnostics.empty()) {
        throw std::invalid_argument(diagnostics.front().message);
    }
    dx_ = config_.length_x_m / static_cast<double>(config_.cells_x);
    dy_ = config_.length_y_m / static_cast<double>(config_.cells_y);
    dz_ = config_.length_z_m / static_cast<double>(config_.cells_z);
    const auto velocity = medium_velocity(
        config_.relative_permittivity, config_.relative_permeability);
    const auto inverse_spacing_norm = std::sqrt(
        1.0 / (dx_ * dx_) + 1.0 / (dy_ * dy_) + 1.0 / (dz_ * dz_));
    dt_ = config_.courant_factor / (velocity * inverse_spacing_norm);
    epsilon_ = vacuum_permittivity * config_.relative_permittivity;
    permeability_ = vacuum_permeability * config_.relative_permeability;

    const auto allocate = [](const Extent3d extent) {
        return std::vector<double>(extent.i * extent.j * extent.k, 0.0);
    };
    electric_x_ = allocate(extent(FieldComponent::electric_x));
    electric_y_ = allocate(extent(FieldComponent::electric_y));
    electric_z_ = allocate(extent(FieldComponent::electric_z));
    magnetic_x_ = allocate(extent(FieldComponent::magnetic_x));
    magnetic_y_ = allocate(extent(FieldComponent::magnetic_y));
    magnetic_z_ = allocate(extent(FieldComponent::magnetic_z));
}

const Grid3dConfig& YeeGrid3d::config() const noexcept { return config_; }
double YeeGrid3d::cell_size_x_m() const noexcept { return dx_; }
double YeeGrid3d::cell_size_y_m() const noexcept { return dy_; }
double YeeGrid3d::cell_size_z_m() const noexcept { return dz_; }
double YeeGrid3d::time_step_s() const noexcept { return dt_; }
double YeeGrid3d::time_s() const noexcept { return static_cast<double>(step_index_) * dt_; }
std::uint64_t YeeGrid3d::step_index() const noexcept { return step_index_; }

double YeeGrid3d::medium_velocity_m_per_s() const noexcept
{
    return medium_velocity(config_.relative_permittivity, config_.relative_permeability);
}

Extent3d YeeGrid3d::extent(const FieldComponent component) const noexcept
{
    switch (component) {
    case FieldComponent::electric_x:
        return {config_.cells_x, config_.cells_y + 1U, config_.cells_z + 1U};
    case FieldComponent::electric_y:
        return {config_.cells_x + 1U, config_.cells_y, config_.cells_z + 1U};
    case FieldComponent::electric_z:
        return {config_.cells_x + 1U, config_.cells_y + 1U, config_.cells_z};
    case FieldComponent::magnetic_x:
        return {config_.cells_x + 1U, config_.cells_y, config_.cells_z};
    case FieldComponent::magnetic_y:
        return {config_.cells_x, config_.cells_y + 1U, config_.cells_z};
    case FieldComponent::magnetic_z:
        return {config_.cells_x, config_.cells_y, config_.cells_z + 1U};
    }
    return {};
}

std::size_t YeeGrid3d::offset(
    const FieldComponent component,
    const Index3d index) const
{
    const auto field_extent = extent(component);
    if (!contains(field_extent, index)) {
        throw std::out_of_range("field index is outside the staggered Yee extent");
    }
    return flat_offset(field_extent, index);
}

Point3d YeeGrid3d::position(
    const FieldComponent component,
    const Index3d index) const
{
    static_cast<void>(offset(component, index));
    const auto i = static_cast<double>(index.i);
    const auto j = static_cast<double>(index.j);
    const auto k = static_cast<double>(index.k);
    switch (component) {
    case FieldComponent::electric_x:
        return {(i + 0.5) * dx_, j * dy_, k * dz_};
    case FieldComponent::electric_y:
        return {i * dx_, (j + 0.5) * dy_, k * dz_};
    case FieldComponent::electric_z:
        return {i * dx_, j * dy_, (k + 0.5) * dz_};
    case FieldComponent::magnetic_x:
        return {i * dx_, (j + 0.5) * dy_, (k + 0.5) * dz_};
    case FieldComponent::magnetic_y:
        return {(i + 0.5) * dx_, j * dy_, (k + 0.5) * dz_};
    case FieldComponent::magnetic_z:
        return {(i + 0.5) * dx_, (j + 0.5) * dy_, k * dz_};
    }
    return {};
}

std::span<const double> YeeGrid3d::field(const FieldComponent component) const noexcept
{
    switch (component) {
    case FieldComponent::electric_x:
        return electric_x_;
    case FieldComponent::electric_y:
        return electric_y_;
    case FieldComponent::electric_z:
        return electric_z_;
    case FieldComponent::magnetic_x:
        return magnetic_x_;
    case FieldComponent::magnetic_y:
        return magnetic_y_;
    case FieldComponent::magnetic_z:
        return magnetic_z_;
    }
    return {};
}

std::span<double> YeeGrid3d::mutable_field(const FieldComponent component) noexcept
{
    switch (component) {
    case FieldComponent::electric_x:
        return electric_x_;
    case FieldComponent::electric_y:
        return electric_y_;
    case FieldComponent::electric_z:
        return electric_z_;
    case FieldComponent::magnetic_x:
        return magnetic_x_;
    case FieldComponent::magnetic_y:
        return magnetic_y_;
    case FieldComponent::magnetic_z:
        return magnetic_z_;
    }
    return {};
}

std::optional<double> YeeGrid3d::sample(const FieldProbe3d& probe) const
{
    if (!contains(extent(probe.component), probe.index)) {
        return std::nullopt;
    }
    return field(probe.component)[flat_offset(extent(probe.component), probe.index)];
}

bool YeeGrid3d::set(
    const FieldComponent component,
    const Index3d index,
    const double value)
{
    if (!contains(extent(component), index) || !std::isfinite(value)) {
        return false;
    }
    mutable_field(component)[flat_offset(extent(component), index)] = value;
    return true;
}

void YeeGrid3d::center_magnetic_half_step_from_electric()
{
    const auto half_coefficient = 0.5 * dt_ / permeability_;
    const auto ex_extent = extent(FieldComponent::electric_x);
    const auto ey_extent = extent(FieldComponent::electric_y);
    const auto ez_extent = extent(FieldComponent::electric_z);
    const auto hx_extent = extent(FieldComponent::magnetic_x);
    const auto hy_extent = extent(FieldComponent::magnetic_y);
    const auto hz_extent = extent(FieldComponent::magnetic_z);

    for (std::size_t i = 0; i < hx_extent.i; ++i) {
        for (std::size_t j = 0; j < hx_extent.j; ++j) {
            for (std::size_t k = 0; k < hx_extent.k; ++k) {
                const auto curl =
                    (electric_z_[flat_offset(ez_extent, {i, j + 1U, k})]
                        - electric_z_[flat_offset(ez_extent, {i, j, k})])
                        / dy_
                    - (electric_y_[flat_offset(ey_extent, {i, j, k + 1U})]
                        - electric_y_[flat_offset(ey_extent, {i, j, k})])
                        / dz_;
                magnetic_x_[flat_offset(hx_extent, {i, j, k})] = half_coefficient * curl;
            }
        }
    }
    for (std::size_t i = 0; i < hy_extent.i; ++i) {
        for (std::size_t j = 0; j < hy_extent.j; ++j) {
            for (std::size_t k = 0; k < hy_extent.k; ++k) {
                const auto curl =
                    (electric_x_[flat_offset(ex_extent, {i, j, k + 1U})]
                        - electric_x_[flat_offset(ex_extent, {i, j, k})])
                        / dz_
                    - (electric_z_[flat_offset(ez_extent, {i + 1U, j, k})]
                        - electric_z_[flat_offset(ez_extent, {i, j, k})])
                        / dx_;
                magnetic_y_[flat_offset(hy_extent, {i, j, k})] = half_coefficient * curl;
            }
        }
    }
    for (std::size_t i = 0; i < hz_extent.i; ++i) {
        for (std::size_t j = 0; j < hz_extent.j; ++j) {
            for (std::size_t k = 0; k < hz_extent.k; ++k) {
                const auto curl =
                    (electric_y_[flat_offset(ey_extent, {i + 1U, j, k})]
                        - electric_y_[flat_offset(ey_extent, {i, j, k})])
                        / dx_
                    - (electric_x_[flat_offset(ex_extent, {i, j + 1U, k})]
                        - electric_x_[flat_offset(ex_extent, {i, j, k})])
                        / dy_;
                magnetic_z_[flat_offset(hz_extent, {i, j, k})] = half_coefficient * curl;
            }
        }
    }
}

StepResult YeeGrid3d::step(const std::span<const ElectricSource3d> sources)
{
    StepResult result;
    for (std::size_t source_index = 0; source_index < sources.size(); ++source_index) {
        const auto& source = sources[source_index];
        const auto path = "sources[" + std::to_string(source_index) + "]";
        if (!is_electric(source.component)
            || !contains(extent(source.component), source.index)
            || !std::isfinite(source.value_v_per_m)
            || is_tangential_pec_boundary(config_, source.component, source.index)) {
            result.diagnostics.push_back(diagnostic(
                DiagnosticCode::invalid_source,
                path,
                "source must target a finite interior electric-field sample"));
        }
    }
    if (!result.succeeded()) {
        return result;
    }

    const auto previous_ex = electric_x_;
    const auto previous_ey = electric_y_;
    const auto previous_ez = electric_z_;
    const auto ex_extent = extent(FieldComponent::electric_x);
    const auto ey_extent = extent(FieldComponent::electric_y);
    const auto ez_extent = extent(FieldComponent::electric_z);
    const auto hx_extent = extent(FieldComponent::magnetic_x);
    const auto hy_extent = extent(FieldComponent::magnetic_y);
    const auto hz_extent = extent(FieldComponent::magnetic_z);
    const auto magnetic_coefficient = dt_ / permeability_;

    for (std::size_t i = 0; i < hx_extent.i; ++i) {
        for (std::size_t j = 0; j < hx_extent.j; ++j) {
            for (std::size_t k = 0; k < hx_extent.k; ++k) {
                const auto curl =
                    (electric_z_[flat_offset(ez_extent, {i, j + 1U, k})]
                        - electric_z_[flat_offset(ez_extent, {i, j, k})])
                        / dy_
                    - (electric_y_[flat_offset(ey_extent, {i, j, k + 1U})]
                        - electric_y_[flat_offset(ey_extent, {i, j, k})])
                        / dz_;
                magnetic_x_[flat_offset(hx_extent, {i, j, k})]
                    -= magnetic_coefficient * curl;
            }
        }
    }
    for (std::size_t i = 0; i < hy_extent.i; ++i) {
        for (std::size_t j = 0; j < hy_extent.j; ++j) {
            for (std::size_t k = 0; k < hy_extent.k; ++k) {
                const auto curl =
                    (electric_x_[flat_offset(ex_extent, {i, j, k + 1U})]
                        - electric_x_[flat_offset(ex_extent, {i, j, k})])
                        / dz_
                    - (electric_z_[flat_offset(ez_extent, {i + 1U, j, k})]
                        - electric_z_[flat_offset(ez_extent, {i, j, k})])
                        / dx_;
                magnetic_y_[flat_offset(hy_extent, {i, j, k})]
                    -= magnetic_coefficient * curl;
            }
        }
    }
    for (std::size_t i = 0; i < hz_extent.i; ++i) {
        for (std::size_t j = 0; j < hz_extent.j; ++j) {
            for (std::size_t k = 0; k < hz_extent.k; ++k) {
                const auto curl =
                    (electric_y_[flat_offset(ey_extent, {i + 1U, j, k})]
                        - electric_y_[flat_offset(ey_extent, {i, j, k})])
                        / dx_
                    - (electric_x_[flat_offset(ex_extent, {i, j + 1U, k})]
                        - electric_x_[flat_offset(ex_extent, {i, j, k})])
                        / dy_;
                magnetic_z_[flat_offset(hz_extent, {i, j, k})]
                    -= magnetic_coefficient * curl;
            }
        }
    }

    const auto electric_coefficient = dt_ / epsilon_;
    for (std::size_t i = 0; i < ex_extent.i; ++i) {
        for (std::size_t j = 1; j + 1U < ex_extent.j; ++j) {
            for (std::size_t k = 1; k + 1U < ex_extent.k; ++k) {
                const auto curl =
                    (magnetic_z_[flat_offset(hz_extent, {i, j, k})]
                        - magnetic_z_[flat_offset(hz_extent, {i, j - 1U, k})])
                        / dy_
                    - (magnetic_y_[flat_offset(hy_extent, {i, j, k})]
                        - magnetic_y_[flat_offset(hy_extent, {i, j, k - 1U})])
                        / dz_;
                electric_x_[flat_offset(ex_extent, {i, j, k})]
                    += electric_coefficient * curl;
            }
        }
    }
    for (std::size_t i = 1; i + 1U < ey_extent.i; ++i) {
        for (std::size_t j = 0; j < ey_extent.j; ++j) {
            for (std::size_t k = 1; k + 1U < ey_extent.k; ++k) {
                const auto curl =
                    (magnetic_x_[flat_offset(hx_extent, {i, j, k})]
                        - magnetic_x_[flat_offset(hx_extent, {i, j, k - 1U})])
                        / dz_
                    - (magnetic_z_[flat_offset(hz_extent, {i, j, k})]
                        - magnetic_z_[flat_offset(hz_extent, {i - 1U, j, k})])
                        / dx_;
                electric_y_[flat_offset(ey_extent, {i, j, k})]
                    += electric_coefficient * curl;
            }
        }
    }
    for (std::size_t i = 1; i + 1U < ez_extent.i; ++i) {
        for (std::size_t j = 1; j + 1U < ez_extent.j; ++j) {
            for (std::size_t k = 0; k < ez_extent.k; ++k) {
                const auto curl =
                    (magnetic_y_[flat_offset(hy_extent, {i, j, k})]
                        - magnetic_y_[flat_offset(hy_extent, {i - 1U, j, k})])
                        / dx_
                    - (magnetic_x_[flat_offset(hx_extent, {i, j, k})]
                        - magnetic_x_[flat_offset(hx_extent, {i, j - 1U, k})])
                        / dy_;
                electric_z_[flat_offset(ez_extent, {i, j, k})]
                    += electric_coefficient * curl;
            }
        }
    }

    for (const auto& source : sources) {
        auto& value = mutable_field(source.component)[offset(source.component, source.index)];
        if (source.mode == SourceMode::hard) {
            value = source.value_v_per_m;
        } else {
            value += source.value_v_per_m;
        }
    }

    auto electric_sum = 0.0;
    for (std::size_t index = 0; index < electric_x_.size(); ++index) {
        electric_sum += previous_ex[index] * electric_x_[index];
    }
    for (std::size_t index = 0; index < electric_y_.size(); ++index) {
        electric_sum += previous_ey[index] * electric_y_[index];
    }
    for (std::size_t index = 0; index < electric_z_.size(); ++index) {
        electric_sum += previous_ez[index] * electric_z_[index];
    }
    auto magnetic_sum = 0.0;
    for (const auto value : magnetic_x_) {
        magnetic_sum += value * value;
    }
    for (const auto value : magnetic_y_) {
        magnetic_sum += value * value;
    }
    for (const auto value : magnetic_z_) {
        magnetic_sum += value * value;
    }
    last_discrete_energy_j_ = 0.5 * dx_ * dy_ * dz_
        * (epsilon_ * electric_sum + permeability_ * magnetic_sum);
    ++step_index_;
    return result;
}

std::optional<double> YeeGrid3d::last_discrete_energy_j() const noexcept
{
    return last_discrete_energy_j_;
}

std::optional<double> cavity_mode_frequency_hz(
    const Grid3dConfig& config,
    const CavityMode mode)
{
    if (!validate(config).empty()
        || (mode.m == 0 && mode.n == 0 && mode.p == 0)
        || mode.m > config.cells_x || mode.n > config.cells_y
        || mode.p > config.cells_z) {
        return std::nullopt;
    }
    const auto dx = config.length_x_m / static_cast<double>(config.cells_x);
    const auto dy = config.length_y_m / static_cast<double>(config.cells_y);
    const auto dz = config.length_z_m / static_cast<double>(config.cells_z);
    const auto velocity = medium_velocity(
        config.relative_permittivity, config.relative_permeability);
    const auto inverse_spacing_norm = std::sqrt(
        1.0 / (dx * dx) + 1.0 / (dy * dy) + 1.0 / (dz * dz));
    const auto dt = config.courant_factor / (velocity * inverse_spacing_norm);
    const auto x_term = std::sin(
        static_cast<double>(mode.m) * std::numbers::pi
        / (2.0 * static_cast<double>(config.cells_x)))
        / dx;
    const auto y_term = std::sin(
        static_cast<double>(mode.n) * std::numbers::pi
        / (2.0 * static_cast<double>(config.cells_y)))
        / dy;
    const auto z_term = std::sin(
        static_cast<double>(mode.p) * std::numbers::pi
        / (2.0 * static_cast<double>(config.cells_z)))
        / dz;
    const auto argument = velocity * dt
        * std::sqrt(x_term * x_term + y_term * y_term + z_term * z_term);
    if (argument > 1.0) {
        return std::nullopt;
    }
    return std::asin(argument) / (std::numbers::pi * dt);
}

std::optional<WaveguideDispersion> waveguide_mode_dispersion(
    const Grid3dConfig& config,
    const WaveguideMode mode,
    const double operating_frequency_hz)
{
    if (!validate(config).empty() || !finite_positive(operating_frequency_hz)
        || (mode.m == 0 && mode.n == 0)
        || mode.m > config.cells_x || mode.n > config.cells_y) {
        return std::nullopt;
    }
    const auto dx = config.length_x_m / static_cast<double>(config.cells_x);
    const auto dy = config.length_y_m / static_cast<double>(config.cells_y);
    const auto dz = config.length_z_m / static_cast<double>(config.cells_z);
    const auto velocity = medium_velocity(
        config.relative_permittivity, config.relative_permeability);
    const auto inverse_spacing_norm = std::sqrt(
        1.0 / (dx * dx) + 1.0 / (dy * dy) + 1.0 / (dz * dz));
    const auto dt = config.courant_factor / (velocity * inverse_spacing_norm);
    const auto x_term = std::sin(
        static_cast<double>(mode.m) * std::numbers::pi
        / (2.0 * static_cast<double>(config.cells_x)))
        / dx;
    const auto y_term = std::sin(
        static_cast<double>(mode.n) * std::numbers::pi
        / (2.0 * static_cast<double>(config.cells_y)))
        / dy;
    const auto transverse_squared = x_term * x_term + y_term * y_term;
    const auto cutoff_argument = velocity * dt * std::sqrt(transverse_squared);
    if (cutoff_argument > 1.0) {
        return std::nullopt;
    }
    const auto cutoff = std::asin(cutoff_argument) / (std::numbers::pi * dt);
    if (operating_frequency_hz <= cutoff) {
        return std::nullopt;
    }
    const auto temporal = std::sin(
        std::numbers::pi * operating_frequency_hz * dt)
        / (velocity * dt);
    const auto longitudinal_squared = temporal * temporal - transverse_squared;
    if (longitudinal_squared <= 0.0) {
        return std::nullopt;
    }
    const auto beta_argument = dz * std::sqrt(longitudinal_squared);
    if (beta_argument > 1.0) {
        return std::nullopt;
    }
    return WaveguideDispersion{
        .cutoff_frequency_hz = cutoff,
        .propagation_constant_rad_per_m = 2.0 * std::asin(beta_argument) / dz,
    };
}

struct YeeGrid1dRestoreAccess {
    static void restore(
        YeeGrid1d& grid,
        const std::uint64_t step_index,
        std::vector<double> electric,
        std::vector<double> magnetic,
        const std::optional<double> energy)
    {
        grid.step_index_ = step_index;
        grid.electric_ = std::move(electric);
        grid.magnetic_ = std::move(magnetic);
        grid.last_discrete_energy_j_ = energy;
    }
};

std::vector<std::byte> YeeGrid1d::checkpoint() const
{
    Writer writer;
    writer.text(checkpoint_magic);
    writer.u32(checkpoint_version);
    writer.u32(checkpoint_kind_1d);
    writer.u64(static_cast<std::uint64_t>(config_.cells));
    writer.f64(config_.length_m);
    writer.f64(config_.relative_permittivity);
    writer.f64(config_.relative_permeability);
    writer.f64(config_.courant_factor);
    writer.u32(encoded_boundary(config_.left_boundary));
    writer.u32(encoded_boundary(config_.right_boundary));
    writer.u64(step_index_);
    writer.u8(static_cast<std::uint8_t>(last_discrete_energy_j_.has_value()));
    if (last_discrete_energy_j_.has_value()) {
        writer.f64(*last_discrete_energy_j_);
    }
    writer.values(electric_);
    writer.values(magnetic_);
    finish_checkpoint(writer);
    return std::move(writer).finish();
}

Restore1dResult restore_yee_grid_1d(const std::span<const std::byte> checkpoint)
{
    try {
        auto reader = checkpoint_reader(checkpoint, checkpoint_kind_1d);
        const auto encoded_cells = reader.u64();
        if (encoded_cells > static_cast<std::uint64_t>(maximum_field_values)
            || encoded_cells > static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max())) {
            throw DecodeFailure(
                DiagnosticCode::checkpoint_size_limit,
                "checkpoint cell count exceeds the reference solver limit");
        }
        Grid1dConfig config{
            .cells = static_cast<std::size_t>(encoded_cells),
            .length_m = reader.f64(),
            .relative_permittivity = reader.f64(),
            .relative_permeability = reader.f64(),
            .courant_factor = reader.f64(),
            .left_boundary = decoded_boundary(reader.u32()),
            .right_boundary = decoded_boundary(reader.u32()),
        };
        const auto diagnostics = validate(config);
        if (!diagnostics.empty()) {
            return Restore1dResult{.grid = std::nullopt, .diagnostics = diagnostics};
        }
        const auto step_index = reader.u64();
        const auto has_energy = reader.u8();
        if (has_energy > 1U) {
            throw DecodeFailure(
                DiagnosticCode::malformed_checkpoint,
                "checkpoint energy marker is invalid");
        }
        std::optional<double> energy;
        if (has_energy == 1U) {
            energy = reader.f64();
            if (!std::isfinite(*energy)) {
                throw DecodeFailure(
                    DiagnosticCode::malformed_checkpoint,
                    "checkpoint energy must be finite");
            }
        }
        YeeGrid1d grid(config);
        auto electric = reader.values(grid.electric_field().size());
        auto magnetic = reader.values(grid.magnetic_field().size());
        if (!reader.empty()) {
            throw DecodeFailure(
                DiagnosticCode::malformed_checkpoint,
                "checkpoint has trailing payload bytes");
        }
        YeeGrid1dRestoreAccess::restore(
            grid, step_index, std::move(electric), std::move(magnetic), energy);
        return Restore1dResult{.grid = std::move(grid), .diagnostics = {}};
    } catch (const DecodeFailure& failure) {
        return Restore1dResult{
            .grid = std::nullopt,
            .diagnostics = {diagnostic(failure.code, "checkpoint", failure.what())},
        };
    }
}

struct YeeGrid3dRestoreAccess {
    static void restore(
        YeeGrid3d& grid,
        const std::uint64_t step_index,
        std::vector<double> electric_x,
        std::vector<double> electric_y,
        std::vector<double> electric_z,
        std::vector<double> magnetic_x,
        std::vector<double> magnetic_y,
        std::vector<double> magnetic_z,
        const std::optional<double> energy)
    {
        grid.step_index_ = step_index;
        grid.electric_x_ = std::move(electric_x);
        grid.electric_y_ = std::move(electric_y);
        grid.electric_z_ = std::move(electric_z);
        grid.magnetic_x_ = std::move(magnetic_x);
        grid.magnetic_y_ = std::move(magnetic_y);
        grid.magnetic_z_ = std::move(magnetic_z);
        grid.last_discrete_energy_j_ = energy;
    }
};

std::vector<std::byte> YeeGrid3d::checkpoint() const
{
    Writer writer;
    writer.text(checkpoint_magic);
    writer.u32(checkpoint_version);
    writer.u32(checkpoint_kind_3d);
    writer.u64(static_cast<std::uint64_t>(config_.cells_x));
    writer.u64(static_cast<std::uint64_t>(config_.cells_y));
    writer.u64(static_cast<std::uint64_t>(config_.cells_z));
    writer.f64(config_.length_x_m);
    writer.f64(config_.length_y_m);
    writer.f64(config_.length_z_m);
    writer.f64(config_.relative_permittivity);
    writer.f64(config_.relative_permeability);
    writer.f64(config_.courant_factor);
    writer.u64(step_index_);
    writer.u8(static_cast<std::uint8_t>(last_discrete_energy_j_.has_value()));
    if (last_discrete_energy_j_.has_value()) {
        writer.f64(*last_discrete_energy_j_);
    }
    writer.values(electric_x_);
    writer.values(electric_y_);
    writer.values(electric_z_);
    writer.values(magnetic_x_);
    writer.values(magnetic_y_);
    writer.values(magnetic_z_);
    finish_checkpoint(writer);
    return std::move(writer).finish();
}

Restore3dResult restore_yee_grid_3d(const std::span<const std::byte> checkpoint)
{
    try {
        auto reader = checkpoint_reader(checkpoint, checkpoint_kind_3d);
        const auto read_cells = [&reader]() {
            const auto encoded = reader.u64();
            if (encoded > static_cast<std::uint64_t>(maximum_field_values)
                || encoded > static_cast<std::uint64_t>(
                    std::numeric_limits<std::size_t>::max())) {
                throw DecodeFailure(
                    DiagnosticCode::checkpoint_size_limit,
                    "checkpoint cell count exceeds the reference solver limit");
            }
            return static_cast<std::size_t>(encoded);
        };
        Grid3dConfig config{
            .cells_x = read_cells(),
            .cells_y = read_cells(),
            .cells_z = read_cells(),
            .length_x_m = reader.f64(),
            .length_y_m = reader.f64(),
            .length_z_m = reader.f64(),
            .relative_permittivity = reader.f64(),
            .relative_permeability = reader.f64(),
            .courant_factor = reader.f64(),
        };
        const auto diagnostics = validate(config);
        if (!diagnostics.empty()) {
            return Restore3dResult{.grid = std::nullopt, .diagnostics = diagnostics};
        }
        const auto step_index = reader.u64();
        const auto has_energy = reader.u8();
        if (has_energy > 1U) {
            throw DecodeFailure(
                DiagnosticCode::malformed_checkpoint,
                "checkpoint energy marker is invalid");
        }
        std::optional<double> energy;
        if (has_energy == 1U) {
            energy = reader.f64();
            if (!std::isfinite(*energy)) {
                throw DecodeFailure(
                    DiagnosticCode::malformed_checkpoint,
                    "checkpoint energy must be finite");
            }
        }
        YeeGrid3d grid(config);
        auto electric_x = reader.values(grid.field(FieldComponent::electric_x).size());
        auto electric_y = reader.values(grid.field(FieldComponent::electric_y).size());
        auto electric_z = reader.values(grid.field(FieldComponent::electric_z).size());
        auto magnetic_x = reader.values(grid.field(FieldComponent::magnetic_x).size());
        auto magnetic_y = reader.values(grid.field(FieldComponent::magnetic_y).size());
        auto magnetic_z = reader.values(grid.field(FieldComponent::magnetic_z).size());
        if (!reader.empty()) {
            throw DecodeFailure(
                DiagnosticCode::malformed_checkpoint,
                "checkpoint has trailing payload bytes");
        }
        YeeGrid3dRestoreAccess::restore(
            grid,
            step_index,
            std::move(electric_x),
            std::move(electric_y),
            std::move(electric_z),
            std::move(magnetic_x),
            std::move(magnetic_y),
            std::move(magnetic_z),
            energy);
        return Restore3dResult{.grid = std::move(grid), .diagnostics = {}};
    } catch (const DecodeFailure& failure) {
        return Restore3dResult{
            .grid = std::nullopt,
            .diagnostics = {diagnostic(failure.code, "checkpoint", failure.what())},
        };
    }
}

} // namespace oep::fdtd
