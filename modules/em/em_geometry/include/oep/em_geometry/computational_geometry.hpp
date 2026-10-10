// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <oep/simulation_ir/simulation_ir.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace oep::em_geometry {

struct Point3d {
    double x{};
    double y{};
    double z{};

    bool operator==(const Point3d&) const = default;
};

struct AxisAlignedBox {
    Point3d minimum;
    Point3d maximum;

    bool operator==(const AxisAlignedBox&) const = default;
};

enum class Axis {
    x,
    y,
    z,
};

struct PlanarPatch {
    Axis normal_axis{Axis::z};
    int inward_normal_sign{1};
    double plane_coordinate_m{};
    double first_minimum_m{};
    double first_maximum_m{};
    double second_minimum_m{};
    double second_maximum_m{};

    bool operator==(const PlanarPatch&) const = default;
};

using GeometryPrimitive = std::variant<AxisAlignedBox, PlanarPatch>;

struct ResolvedCadEntity {
    simulation_ir::EntityReference source;
    GeometryPrimitive geometry;
};

struct CadGeometrySnapshot {
    std::string geometry_hash;
    std::vector<ResolvedCadEntity> entities;
};

enum class DiagnosticSeverity {
    error,
    warning,
};

enum class DiagnosticCode {
    geometry_hash_mismatch,
    duplicate_entity,
    unresolved_entity,
    entity_kind_mismatch,
    invalid_geometry,
    missing_material,
    missing_background,
    multiple_backgrounds,
    unsupported_role_geometry,
    invalid_port_orientation,
    incompatible_reference_plane,
};

struct Diagnostic {
    DiagnosticSeverity severity{DiagnosticSeverity::error};
    DiagnosticCode code{DiagnosticCode::unresolved_entity};
    std::string path;
    std::string message;
};

struct BackgroundRegion {
    std::string role_assignment_id;
    std::string material_id;
    simulation_ir::EntityReference source;
    AxisAlignedBox bounds;
};

struct MaterialRegion {
    std::string role_assignment_id;
    simulation_ir::ElectromagneticRole role{simulation_ir::ElectromagneticRole::dielectric};
    std::string material_id;
    simulation_ir::EntityReference source;
    AxisAlignedBox bounds;
};

struct SurfaceRegion {
    std::string role_assignment_id;
    simulation_ir::ElectromagneticRole role{simulation_ir::ElectromagneticRole::conductor};
    std::optional<std::string> material_id;
    simulation_ir::EntityReference source;
    PlanarPatch patch;
};

struct PortRegion {
    std::string port_id;
    simulation_ir::PortKind kind{simulation_ir::PortKind::discrete};
    simulation_ir::EntityReference face_source;
    PlanarPatch face;
    simulation_ir::EntityReference reference_plane_source;
    PlanarPatch reference_plane;
    simulation_ir::Direction3d inward_orientation;
    std::uint32_t mode_index{};
    double reference_impedance_ohm{};
    double deembedding_length_m{};
    simulation_ir::PortTermination termination{simulation_ir::PortTermination::matched};
};

struct ComputationalGeometry {
    std::string study_fingerprint;
    std::string geometry_hash;
    BackgroundRegion background;
    std::vector<simulation_ir::MaterialDefinition> materials;
    std::vector<MaterialRegion> material_regions;
    std::vector<SurfaceRegion> surface_regions;
    std::vector<PortRegion> ports;
};

struct PreparationResult {
    std::optional<ComputationalGeometry> geometry;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept { return geometry.has_value(); }
    [[nodiscard]] bool contains(DiagnosticCode code) const noexcept;
};

class GeometryPreparer {
public:
    // The study must already have passed the ASIR validator. This boundary
    // resolves its persistent references against the exact-geometry snapshot.
    [[nodiscard]] PreparationResult prepare(
        const simulation_ir::AntennaStudy& study,
        const CadGeometrySnapshot& snapshot) const;
};

[[nodiscard]] bool valid(const AxisAlignedBox& bounds) noexcept;
[[nodiscard]] bool valid(const PlanarPatch& patch) noexcept;
[[nodiscard]] double volume_m3(const AxisAlignedBox& bounds) noexcept;

} // namespace oep::em_geometry
