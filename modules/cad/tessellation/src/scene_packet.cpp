// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

// Converts adapter tessellation into a renderer-neutral packet while replacing
// transient kernel IDs with persistent document topology IDs.

#include <oep/tessellation/scene_packet.hpp>

#include <algorithm>
#include <map>
#include <utility>

namespace oep::tessellation {
namespace {

using TopologyLookup = std::map<
    std::pair<modeler::TopologyKind, std::string>,
    std::string>;

[[nodiscard]] TopologyLookup make_topology_lookup(
    const std::span<const topology_id::TopologyBinding> bindings)
{
    TopologyLookup lookup;
    for (const auto& binding : bindings) {
        lookup.emplace(
            std::pair{binding.entity.kind, binding.entity.id.value},
            topology_id::to_string(binding.persistent_id));
    }
    return lookup;
}

[[nodiscard]] std::string persistent_id_for(
    const TopologyLookup& lookup,
    const modeler::KernelTopologyId& kernel_id,
    const modeler::TopologyKind kind)
{
    const auto found = lookup.find({kind, kernel_id.value});
    return found == lookup.end() ? std::string{} : found->second;
}

} // namespace

ScenePacketResult build_scene_packet(
    modeler::GeometryKernel& kernel,
    const modeler::ExactBodyHandle body,
    std::string geometry_hash,
    const std::span<const topology_id::TopologyBinding> bindings,
    const double linear_deflection,
    const double angular_deflection)
{
    auto display = kernel.tessellate(
        {.body = body,
         .linear_deflection = linear_deflection,
         .angular_deflection = angular_deflection},
        modeler::CancellationToken{});
    if (!display.succeeded()) {
        return {.packet = {}, .diagnostics = std::move(display.diagnostics)};
    }
    const auto topology_lookup = make_topology_lookup(bindings);

    ScenePacket packet{
        .geometry_hash = std::move(geometry_hash),
        .bounds = display.bounds,
        .vertices = {},
        .triangles = {},
        .edge_polylines = {},
    };
    packet.vertices.reserve(display.vertices.size());
    for (const auto& vertex : display.vertices) {
        packet.vertices.push_back({vertex.position, vertex.normal});
    }
    packet.triangles.reserve(display.triangles.size());
    for (const auto& triangle : display.triangles) {
        packet.triangles.push_back({
            .vertex_indices = triangle.vertex_indices,
            .persistent_id = persistent_id_for(
                topology_lookup, triangle.face_id, modeler::TopologyKind::face),
        });
    }
    packet.edge_polylines.reserve(display.edge_polylines.size());
    for (auto& polyline : display.edge_polylines) {
        packet.edge_polylines.push_back({
            .points = std::move(polyline.points),
            .persistent_id = persistent_id_for(
                topology_lookup, polyline.edge_id, modeler::TopologyKind::edge),
        });
    }
    return {.packet = std::move(packet), .diagnostics = {}};
}

} // namespace oep::tessellation
