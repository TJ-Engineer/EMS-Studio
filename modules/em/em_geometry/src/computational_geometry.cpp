// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#include <oep/em_geometry/computational_geometry.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string_view>
#include <utility>

namespace oep::em_geometry {
namespace {

using simulation_ir::ElectromagneticRole;
using simulation_ir::EntityKind;
using simulation_ir::EntityReference;
using simulation_ir::PersistentEntityId;

[[nodiscard]] Diagnostic issue(
    const DiagnosticCode code,
    std::string path,
    std::string message)
{
    return Diagnostic{
        .severity = DiagnosticSeverity::error,
        .code = code,
        .path = std::move(path),
        .message = std::move(message),
    };
}

[[nodiscard]] bool finite(const double value)
{
    return std::isfinite(value);
}

[[nodiscard]] std::string entity_key(const PersistentEntityId& id)
{
    return id.feature_id + ':' + std::to_string(id.local_id);
}

[[nodiscard]] bool material_exists(
    const simulation_ir::AntennaStudy& study,
    const std::string_view material_id)
{
    return std::ranges::any_of(study.materials, [&](const auto& material) {
        return material.id == material_id;
    });
}

[[nodiscard]] const ResolvedCadEntity* resolve(
    const std::map<std::string, const ResolvedCadEntity*, std::less<>>& entities,
    const EntityReference& reference)
{
    const auto found = entities.find(entity_key(reference.persistent_id));
    return found == entities.end() ? nullptr : found->second;
}

[[nodiscard]] bool volume_kind(const EntityKind kind)
{
    return kind == EntityKind::body || kind == EntityKind::solid;
}

[[nodiscard]] bool material_volume_role(const ElectromagneticRole role)
{
    return role == ElectromagneticRole::conductor
        || role == ElectromagneticRole::dielectric
        || role == ElectromagneticRole::ground
        || role == ElectromagneticRole::radome
        || role == ElectromagneticRole::feed_structure;
}

[[nodiscard]] bool role_requires_material(const ElectromagneticRole role)
{
    return role == ElectromagneticRole::conductor
        || role == ElectromagneticRole::dielectric
        || role == ElectromagneticRole::ground
        || role == ElectromagneticRole::radome
        || role == ElectromagneticRole::air_or_vacuum;
}

[[nodiscard]] simulation_ir::Direction3d patch_direction(const PlanarPatch& patch)
{
    const auto sign = static_cast<double>(patch.inward_normal_sign);
    switch (patch.normal_axis) {
    case Axis::x:
        return {sign, 0.0, 0.0};
    case Axis::y:
        return {0.0, sign, 0.0};
    case Axis::z:
        return {0.0, 0.0, sign};
    }
    return {};
}

[[nodiscard]] bool same_direction(
    const simulation_ir::Direction3d& left,
    const simulation_ir::Direction3d& right)
{
    constexpr auto tolerance = 1.0e-12;
    return std::abs(left.x - right.x) <= tolerance
        && std::abs(left.y - right.y) <= tolerance
        && std::abs(left.z - right.z) <= tolerance;
}

[[nodiscard]] bool parallel_patch(const PlanarPatch& left, const PlanarPatch& right)
{
    return left.normal_axis == right.normal_axis
        && left.inward_normal_sign == right.inward_normal_sign;
}

} // namespace

bool PreparationResult::contains(const DiagnosticCode code) const noexcept
{
    return std::ranges::any_of(diagnostics, [code](const Diagnostic& item) {
        return item.code == code;
    });
}

bool valid(const AxisAlignedBox& bounds) noexcept
{
    return finite(bounds.minimum.x) && finite(bounds.minimum.y)
        && finite(bounds.minimum.z) && finite(bounds.maximum.x)
        && finite(bounds.maximum.y) && finite(bounds.maximum.z)
        && bounds.minimum.x < bounds.maximum.x
        && bounds.minimum.y < bounds.maximum.y
        && bounds.minimum.z < bounds.maximum.z;
}

bool valid(const PlanarPatch& patch) noexcept
{
    return (patch.inward_normal_sign == -1 || patch.inward_normal_sign == 1)
        && finite(patch.plane_coordinate_m) && finite(patch.first_minimum_m)
        && finite(patch.first_maximum_m) && finite(patch.second_minimum_m)
        && finite(patch.second_maximum_m)
        && patch.first_minimum_m < patch.first_maximum_m
        && patch.second_minimum_m < patch.second_maximum_m;
}

double volume_m3(const AxisAlignedBox& bounds) noexcept
{
    if (!valid(bounds)) {
        return 0.0;
    }
    return (bounds.maximum.x - bounds.minimum.x)
        * (bounds.maximum.y - bounds.minimum.y)
        * (bounds.maximum.z - bounds.minimum.z);
}

PreparationResult GeometryPreparer::prepare(
    const simulation_ir::AntennaStudy& study,
    const CadGeometrySnapshot& snapshot) const
{
    PreparationResult result;
    if (snapshot.geometry_hash.empty()
        || snapshot.geometry_hash != study.identity.geometry_hash) {
        result.diagnostics.push_back(issue(
            DiagnosticCode::geometry_hash_mismatch,
            "geometry_hash",
            "the resolved CAD snapshot must match the validated ASIR geometry hash"));
    }

    std::map<std::string, const ResolvedCadEntity*, std::less<>> entities;
    for (std::size_t index = 0; index < snapshot.entities.size(); ++index) {
        const auto& entity = snapshot.entities[index];
        const auto key = entity_key(entity.source.persistent_id);
        if (entity.source.persistent_id.feature_id.empty()
            || !entities.emplace(key, &entity).second) {
            result.diagnostics.push_back(issue(
                DiagnosticCode::duplicate_entity,
                "entities[" + std::to_string(index) + "]",
                "resolved CAD entities require unique nonempty persistent IDs"));
        }
        const auto geometry_valid = std::visit(
            [](const auto& geometry) { return valid(geometry); }, entity.geometry);
        if (!geometry_valid) {
            result.diagnostics.push_back(issue(
                DiagnosticCode::invalid_geometry,
                "entities[" + std::to_string(index) + "]",
                "resolved CAD geometry must have finite nondegenerate bounds"));
        }
        if ((std::holds_alternative<AxisAlignedBox>(entity.geometry)
                && !volume_kind(entity.source.expected_kind))
            || (std::holds_alternative<PlanarPatch>(entity.geometry)
                && entity.source.expected_kind != EntityKind::face)) {
            result.diagnostics.push_back(issue(
                DiagnosticCode::entity_kind_mismatch,
                "entities[" + std::to_string(index) + "]",
                "volume primitives require body/solid IDs and planar patches require face IDs"));
        }
    }

    std::optional<BackgroundRegion> background;
    std::vector<MaterialRegion> material_regions;
    std::vector<SurfaceRegion> surface_regions;
    for (const auto& role : study.roles) {
        const auto path = "roles[" + role.id + ']';
        const auto* entity = resolve(entities, role.target);
        if (entity == nullptr) {
            result.diagnostics.push_back(issue(
                DiagnosticCode::unresolved_entity,
                path + ".target",
                "the persistent role target is absent from the CAD geometry snapshot"));
            continue;
        }
        if (entity->source.expected_kind != role.target.expected_kind) {
            result.diagnostics.push_back(issue(
                DiagnosticCode::entity_kind_mismatch,
                path + ".target",
                "the resolved role target has a different topology kind"));
            continue;
        }
        if ((role_requires_material(role.role) && !role.material_id.has_value())
            || (role.material_id.has_value()
                && !material_exists(study, *role.material_id))) {
            result.diagnostics.push_back(issue(
                DiagnosticCode::missing_material,
                path + ".material_id",
                "the prepared role requires a material present in the ASIR study"));
            continue;
        }

        if (role.role == ElectromagneticRole::air_or_vacuum) {
            const auto* bounds = std::get_if<AxisAlignedBox>(&entity->geometry);
            if (bounds == nullptr) {
                result.diagnostics.push_back(issue(
                    DiagnosticCode::unsupported_role_geometry,
                    path,
                    "the background role requires a volume primitive"));
            } else if (background.has_value()) {
                result.diagnostics.push_back(issue(
                    DiagnosticCode::multiple_backgrounds,
                    path,
                    "the Cartesian spike accepts one authoritative background volume"));
            } else {
                background = BackgroundRegion{
                    .role_assignment_id = role.id,
                    .material_id = role.material_id.value(),
                    .source = role.target,
                    .bounds = *bounds,
                };
            }
            continue;
        }

        if (const auto* bounds = std::get_if<AxisAlignedBox>(&entity->geometry);
            bounds != nullptr && material_volume_role(role.role)
            && role.material_id.has_value()) {
            material_regions.push_back(MaterialRegion{
                .role_assignment_id = role.id,
                .role = role.role,
                .material_id = role.material_id.value(),
                .source = role.target,
                .bounds = *bounds,
            });
        } else if (const auto* patch = std::get_if<PlanarPatch>(&entity->geometry)) {
            surface_regions.push_back(SurfaceRegion{
                .role_assignment_id = role.id,
                .role = role.role,
                .material_id = role.material_id,
                .source = role.target,
                .patch = *patch,
            });
        } else {
            result.diagnostics.push_back(issue(
                DiagnosticCode::unsupported_role_geometry,
                path,
                "the role cannot be represented by the spike's volume or planar primitives"));
        }
    }

    if (!background.has_value()) {
        result.diagnostics.push_back(issue(
            DiagnosticCode::missing_background,
            "roles",
            "the Cartesian grid requires one resolved air-or-vacuum background volume"));
    }

    std::vector<PortRegion> ports;
    for (const auto& port : study.ports) {
        const auto path = "ports[" + port.id + ']';
        const auto* face_entity = resolve(entities, port.face);
        const auto* plane_entity = resolve(entities, port.reference_plane);
        if (face_entity == nullptr || plane_entity == nullptr) {
            result.diagnostics.push_back(issue(
                DiagnosticCode::unresolved_entity,
                path,
                "the port face and reference plane must both resolve to CAD geometry"));
            continue;
        }
        if (face_entity->source.expected_kind != port.face.expected_kind
            || plane_entity->source.expected_kind != port.reference_plane.expected_kind
            || port.face.expected_kind != EntityKind::face
            || port.reference_plane.expected_kind != EntityKind::face) {
            result.diagnostics.push_back(issue(
                DiagnosticCode::entity_kind_mismatch,
                path,
                "port and reference-plane identities must resolve as faces"));
            continue;
        }
        const auto* face = std::get_if<PlanarPatch>(&face_entity->geometry);
        const auto* reference_plane = std::get_if<PlanarPatch>(&plane_entity->geometry);
        if (face == nullptr || reference_plane == nullptr) {
            result.diagnostics.push_back(issue(
                DiagnosticCode::unsupported_role_geometry,
                path,
                "the Cartesian spike currently accepts axis-aligned planar ports"));
            continue;
        }
        if (!same_direction(port.orientation, patch_direction(*face))) {
            result.diagnostics.push_back(issue(
                DiagnosticCode::invalid_port_orientation,
                path + ".orientation",
                "the port orientation must equal the resolved face's inward normal"));
            continue;
        }
        if (!parallel_patch(*face, *reference_plane)) {
            result.diagnostics.push_back(issue(
                DiagnosticCode::incompatible_reference_plane,
                path + ".reference_plane",
                "the port reference plane must be parallel and consistently oriented"));
            continue;
        }
        ports.push_back(PortRegion{
            .port_id = port.id,
            .kind = port.kind,
            .face_source = port.face,
            .face = *face,
            .reference_plane_source = port.reference_plane,
            .reference_plane = *reference_plane,
            .inward_orientation = port.orientation,
            .mode_index = port.mode_index,
            .reference_impedance_ohm = port.reference_impedance.value,
            .deembedding_length_m = port.deembedding_length.value,
            .termination = port.termination,
        });
    }

    const auto has_error = std::ranges::any_of(result.diagnostics, [](const Diagnostic& item) {
        return item.severity == DiagnosticSeverity::error;
    });
    if (!has_error && background.has_value()) {
        auto materials = study.materials;
        std::ranges::sort(materials, {}, &simulation_ir::MaterialDefinition::id);
        std::ranges::sort(material_regions, {}, &MaterialRegion::role_assignment_id);
        std::ranges::sort(surface_regions, {}, &SurfaceRegion::role_assignment_id);
        std::ranges::sort(ports, {}, &PortRegion::port_id);
        result.geometry = ComputationalGeometry{
            .study_fingerprint = simulation_ir::canonical_fingerprint(study),
            .geometry_hash = snapshot.geometry_hash,
            .background = *background,
            .materials = std::move(materials),
            .material_regions = std::move(material_regions),
            .surface_regions = std::move(surface_regions),
            .ports = std::move(ports),
        };
    }
    return result;
}

} // namespace oep::em_geometry
