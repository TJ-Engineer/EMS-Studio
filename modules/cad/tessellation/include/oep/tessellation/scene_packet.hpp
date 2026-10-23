// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

// Immutable-by-convention display packet built from exact evaluated geometry.
// Meshes are disposable; persistent IDs are the only selection authority.

#pragma once

#include <oep/modeler/kernel_adapter.hpp>
#include <oep/topology_id/topology_identity.hpp>

#include <array>
#include <span>
#include <string>
#include <vector>

namespace oep::tessellation {

struct SceneVertex {
    modeler::Point3d position;
    modeler::Vector3d normal;
};

struct SceneTriangle {
    std::array<std::uint32_t, 3> vertex_indices{};
    std::string persistent_id;
};

struct ScenePolyline {
    std::vector<modeler::Point3d> points;
    std::string persistent_id;
};

struct ScenePacket {
    std::string geometry_hash;
    modeler::BoundingBox bounds;
    std::vector<SceneVertex> vertices;
    std::vector<SceneTriangle> triangles;
    std::vector<ScenePolyline> edge_polylines;
};

struct ScenePacketResult {
    ScenePacket packet;
    std::vector<modeler::GeometryDiagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept { return diagnostics.empty(); }
};

[[nodiscard]] ScenePacketResult build_scene_packet(
    modeler::GeometryKernel& kernel,
    modeler::ExactBodyHandle body,
    std::string geometry_hash,
    std::span<const topology_id::TopologyBinding> bindings,
    double linear_deflection = 0.2,
    double angular_deflection = 0.35);

} // namespace oep::tessellation
