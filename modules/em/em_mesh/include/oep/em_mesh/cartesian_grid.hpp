// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <oep/em_geometry/computational_geometry.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace oep::em_mesh {

struct CellIndex {
    std::size_t x{};
    std::size_t y{};
    std::size_t z{};

    bool operator==(const CellIndex&) const = default;
};

struct CartesianGridSpec {
    em_geometry::Point3d origin_m;
    std::array<std::size_t, 3> cell_counts{};
    std::array<double, 3> cell_size_m{};
    double material_assignment_threshold{0.5};
    std::uint64_t maximum_cells{100'000'000};
};

struct CellOccupancy {
    CellIndex cell;
    double fraction{};
};

struct MaterialCell {
    CellIndex index;
    std::string material_id;
    std::string role_assignment_id;
    simulation_ir::EntityReference source_entity;
    double dominant_occupancy_fraction{1.0};
};

struct RegionCellMapping {
    std::string role_assignment_id;
    simulation_ir::ElectromagneticRole role{simulation_ir::ElectromagneticRole::dielectric};
    std::string material_id;
    simulation_ir::EntityReference source_entity;
    std::vector<CellOccupancy> cells;
};

struct SurfaceCellMapping {
    std::string role_assignment_id;
    simulation_ir::ElectromagneticRole role{simulation_ir::ElectromagneticRole::conductor};
    std::optional<std::string> material_id;
    simulation_ir::EntityReference source_entity;
    std::vector<CellOccupancy> cells;
};

struct PortCellMapping {
    std::string port_id;
    simulation_ir::PortKind kind{simulation_ir::PortKind::discrete};
    simulation_ir::EntityReference face_source;
    simulation_ir::EntityReference reference_plane_source;
    simulation_ir::Direction3d inward_orientation;
    std::uint32_t mode_index{};
    double reference_impedance_ohm{};
    double deembedding_length_m{};
    simulation_ir::PortTermination termination{simulation_ir::PortTermination::matched};
    std::vector<CellOccupancy> face_cells;
    std::vector<CellOccupancy> reference_plane_cells;
};

enum class StaircasingRisk {
    low,
    moderate,
    high,
};

enum class MappedFeatureKind {
    material_volume,
    role_surface,
    port_face,
    port_reference_plane,
};

struct StaircasingReport {
    std::string mapping_id;
    MappedFeatureKind kind{MappedFeatureKind::material_volume};
    double exact_measure{};
    double subcell_measure{};
    double binary_cell_measure{};
    double relative_binary_measure_error{};
    double minimum_feature_cells{};
    std::size_t cut_cell_count{};
    StaircasingRisk risk{StaircasingRisk::low};
};

enum class DiagnosticSeverity {
    error,
    warning,
};

enum class DiagnosticCode {
    invalid_grid,
    cell_limit_exceeded,
    geometry_outside_grid,
    overlapping_material_regions,
    mixed_material_cell,
    unmapped_feature,
    staircasing_risk,
};

struct Diagnostic {
    DiagnosticSeverity severity{DiagnosticSeverity::error};
    DiagnosticCode code{DiagnosticCode::invalid_grid};
    std::string path;
    std::string message;
};

struct CartesianGrid {
    CartesianGridSpec spec;
    std::string study_fingerprint;
    std::string geometry_hash;
    std::vector<simulation_ir::MaterialDefinition> materials;
    std::vector<MaterialCell> material_cells;
    std::vector<RegionCellMapping> region_mappings;
    std::vector<SurfaceCellMapping> surface_mappings;
    std::vector<PortCellMapping> port_mappings;
    std::vector<StaircasingReport> staircasing;

    [[nodiscard]] std::size_t linear_index(CellIndex index) const;
    [[nodiscard]] const MaterialCell& cell(CellIndex index) const;
};

struct GridResult {
    std::optional<CartesianGrid> grid;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept { return grid.has_value(); }
    [[nodiscard]] bool contains(DiagnosticCode code) const noexcept;
};

class CartesianGridBuilder {
public:
    [[nodiscard]] GridResult build(
        const em_geometry::ComputationalGeometry& geometry,
        const CartesianGridSpec& spec) const;
};

} // namespace oep::em_mesh
