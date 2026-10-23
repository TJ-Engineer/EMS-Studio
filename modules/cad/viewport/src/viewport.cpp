// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

// Orthographic CPU projection supplies a deterministic fallback renderer and
// picking oracle. The scene-packet boundary permits a later GPU backend.

#include <oep/viewport/viewport.hpp>

#include <algorithm>
#include <cmath>

namespace oep::viewport {
namespace {

using modeler::Point3d;
using modeler::Vector3d;

[[nodiscard]] Vector3d subtract(const Point3d& a, const Point3d& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] double dot(const Vector3d& a, const Vector3d& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] Point3d transformed(
    const SceneTransform& transform,
    const Point3d& point)
{
    const auto& m = transform.values;
    return {
        m[0] * point.x + m[4] * point.y + m[8] * point.z + m[12],
        m[1] * point.x + m[5] * point.y + m[9] * point.z + m[13],
        m[2] * point.x + m[6] * point.y + m[10] * point.z + m[14],
    };
}

[[nodiscard]] Vector3d transformed(
    const SceneTransform& transform,
    const Vector3d& vector)
{
    const auto& m = transform.values;
    const Vector3d result{
        m[0] * vector.x + m[4] * vector.y + m[8] * vector.z,
        m[1] * vector.x + m[5] * vector.y + m[9] * vector.z,
        m[2] * vector.x + m[6] * vector.y + m[10] * vector.z,
    };
    const auto length = std::sqrt(dot(result, result));
    return length <= 1.0e-12
        ? Vector3d{}
        : Vector3d{result.x / length, result.y / length, result.z / length};
}

[[nodiscard]] std::string qualified_id(
    const std::string& instance_id,
    const std::string& topology_id)
{
    if (topology_id.empty() || instance_id.empty()) return topology_id;
    return instance_id + '/' + topology_id;
}

[[nodiscard]] Vector3d basis_right(const CameraState& camera)
{
    return {std::cos(camera.yaw), std::sin(camera.yaw), 0.0};
}

[[nodiscard]] Vector3d basis_view(const CameraState& camera)
{
    return {
        -std::sin(camera.yaw) * std::cos(camera.pitch),
        std::cos(camera.yaw) * std::cos(camera.pitch),
        std::sin(camera.pitch),
    };
}

[[nodiscard]] Vector3d basis_up(const CameraState& camera)
{
    const auto right = basis_right(camera);
    const auto view = basis_view(camera);
    return {
        view.y * right.z - view.z * right.y,
        view.z * right.x - view.x * right.z,
        view.x * right.y - view.y * right.x,
    };
}

[[nodiscard]] bool contains(const ProjectedTriangle& triangle, const ScreenPoint point)
{
    const auto cross = [](const ScreenPoint& a, const ScreenPoint& b, const ScreenPoint& p) {
        return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
    };
    const auto first = cross(triangle.points[0], triangle.points[1], point);
    const auto second = cross(triangle.points[1], triangle.points[2], point);
    const auto third = cross(triangle.points[2], triangle.points[0], point);
    return (first >= 0.0 && second >= 0.0 && third >= 0.0)
        || (first <= 0.0 && second <= 0.0 && third <= 0.0);
}

} // namespace

void orbit(CameraState& camera, const double delta_x, const double delta_y) noexcept
{
    camera.yaw += delta_x * 0.008;
    camera.pitch = std::clamp(camera.pitch + delta_y * 0.008, -1.45, 1.45);
}

void pan(CameraState& camera, const double delta_x, const double delta_y) noexcept
{
    camera.pan_x += delta_x;
    camera.pan_y += delta_y;
}

void zoom(CameraState& camera, const double wheel_delta) noexcept
{
    camera.zoom = std::clamp(camera.zoom * std::exp(wheel_delta * 0.001), 0.1, 20.0);
}

void set_clip_fraction(CameraState& camera, const double fraction) noexcept
{
    camera.clip_fraction = std::clamp(fraction, 0.0, 1.0);
}

void fit(CameraState& camera) noexcept
{
    camera.zoom = 1.0;
    camera.pan_x = 0.0;
    camera.pan_y = 0.0;
}

ProjectedFrame project(
    const tessellation::ScenePacket& packet,
    const CameraState& camera,
    const double width,
    const double height)
{
    ProjectedFrame frame{
        .triangles = {},
        .edge_polylines = {},
        .source_triangle_count = packet.triangles.size(),
        .rendered_triangle_count = 0,
        .visible_instance_count = 1,
        .used_lower_detail = false,
    };
    if (width <= 0.0 || height <= 0.0) return frame;
    const Point3d center{
        (packet.bounds.minimum.x + packet.bounds.maximum.x) * 0.5,
        (packet.bounds.minimum.y + packet.bounds.maximum.y) * 0.5,
        (packet.bounds.minimum.z + packet.bounds.maximum.z) * 0.5,
    };
    const auto diagonal = subtract(packet.bounds.maximum, packet.bounds.minimum);
    const auto extent = std::max(std::sqrt(dot(diagonal, diagonal)), 1.0e-9);
    const auto scale = std::min(width, height) * 0.72 * camera.zoom / extent;
    const auto right = basis_right(camera);
    const auto up = basis_up(camera);
    const auto view = basis_view(camera);
    const auto clip_z = packet.bounds.minimum.z
        + (packet.bounds.maximum.z - packet.bounds.minimum.z) * camera.clip_fraction;

    const auto screen = [&](const Point3d& point) {
        const auto relative = subtract(point, center);
        return ScreenPoint{
            width * 0.5 + camera.pan_x + dot(relative, right) * scale,
            height * 0.5 + camera.pan_y - dot(relative, up) * scale,
        };
    };
    for (const auto& triangle : packet.triangles) {
        const auto& a = packet.vertices[triangle.vertex_indices[0]];
        const auto& b = packet.vertices[triangle.vertex_indices[1]];
        const auto& c = packet.vertices[triangle.vertex_indices[2]];
        if ((a.position.z + b.position.z + c.position.z) / 3.0 > clip_z) continue;
        const auto centroid = Vector3d{
            (a.position.x + b.position.x + c.position.x) / 3.0 - center.x,
            (a.position.y + b.position.y + c.position.y) / 3.0 - center.y,
            (a.position.z + b.position.z + c.position.z) / 3.0 - center.z,
        };
        frame.triangles.push_back({
            .points = {screen(a.position), screen(b.position), screen(c.position)},
            .depth = dot(centroid, view),
            .shade = std::clamp(0.35 + 0.65 * std::abs(dot(a.normal, view)), 0.0, 1.0),
            .material = {},
            .persistent_id = triangle.persistent_id,
        });
    }
    std::ranges::sort(frame.triangles, {}, &ProjectedTriangle::depth);
    for (const auto& polyline : packet.edge_polylines) {
        ProjectedPolyline projected{.points = {}, .depth = 0.0, .persistent_id = polyline.persistent_id};
        std::size_t visible_count{};
        for (const auto& point : polyline.points) {
            if (point.z > clip_z) continue;
            projected.points.push_back(screen(point));
            projected.depth += dot(subtract(point, center), view);
            ++visible_count;
        }
        if (visible_count >= 2) {
            projected.depth /= static_cast<double>(visible_count);
            frame.edge_polylines.push_back(std::move(projected));
        }
    }
    std::ranges::sort(frame.edge_polylines, {}, &ProjectedPolyline::depth);
    frame.rendered_triangle_count = frame.triangles.size();
    return frame;
}

SceneAssembly make_assembly(std::vector<SceneInstance> instances)
{
    SceneAssembly assembly{
        .bounds = {{}, {}},
        .instances = std::move(instances),
        .source_triangle_count = 0,
    };
    bool has_bounds = false;
    for (const auto& instance : assembly.instances) {
        if (!instance.visible || instance.lods.empty() || instance.lods.front() == nullptr) continue;
        assembly.source_triangle_count += instance.lods.front()->triangles.size();
        const auto& bounds = instance.lods.front()->bounds;
        for (const auto x : {bounds.minimum.x, bounds.maximum.x}) {
            for (const auto y : {bounds.minimum.y, bounds.maximum.y}) {
                for (const auto z : {bounds.minimum.z, bounds.maximum.z}) {
                    const auto point = transformed(
                        instance.transform, Point3d{x, y, z});
                    if (!has_bounds) {
                        assembly.bounds = modeler::BoundingBox{point, point};
                        has_bounds = true;
                    } else {
                        assembly.bounds.minimum.x = std::min(assembly.bounds.minimum.x, point.x);
                        assembly.bounds.minimum.y = std::min(assembly.bounds.minimum.y, point.y);
                        assembly.bounds.minimum.z = std::min(assembly.bounds.minimum.z, point.z);
                        assembly.bounds.maximum.x = std::max(assembly.bounds.maximum.x, point.x);
                        assembly.bounds.maximum.y = std::max(assembly.bounds.maximum.y, point.y);
                        assembly.bounds.maximum.z = std::max(assembly.bounds.maximum.z, point.z);
                    }
                }
            }
        }
    }
    return assembly;
}

ProjectedFrame project(
    const SceneAssembly& assembly,
    const CameraState& camera,
    const double width,
    const double height,
    const ProjectionPolicy& policy)
{
    ProjectedFrame frame{
        .triangles = {},
        .edge_polylines = {},
        .source_triangle_count = assembly.source_triangle_count,
        .rendered_triangle_count = 0,
        .visible_instance_count = 0,
        .used_lower_detail = false,
    };
    if (width <= 0.0 || height <= 0.0 || assembly.instances.empty()) return frame;
    const Point3d center{
        (assembly.bounds.minimum.x + assembly.bounds.maximum.x) * 0.5,
        (assembly.bounds.minimum.y + assembly.bounds.maximum.y) * 0.5,
        (assembly.bounds.minimum.z + assembly.bounds.maximum.z) * 0.5,
    };
    const auto diagonal = subtract(assembly.bounds.maximum, assembly.bounds.minimum);
    const auto extent = std::max(std::sqrt(dot(diagonal, diagonal)), 1.0e-9);
    const auto scale = std::min(width, height) * 0.72 * camera.zoom / extent;
    const auto right = basis_right(camera);
    const auto up = basis_up(camera);
    const auto view = basis_view(camera);
    const auto clip_z = assembly.bounds.minimum.z
        + (assembly.bounds.maximum.z - assembly.bounds.minimum.z) * camera.clip_fraction;
    const auto screen = [&](const Point3d& point) {
        const auto relative = subtract(point, center);
        return ScreenPoint{
            width * 0.5 + camera.pan_x + dot(relative, right) * scale,
            height * 0.5 + camera.pan_y - dot(relative, up) * scale,
        };
    };

    const bool requires_lod = assembly.source_triangle_count > policy.triangle_budget;
    for (const auto& instance : assembly.instances) {
        if (!instance.visible || instance.lods.empty()) continue;
        const auto lod_index = requires_lod ? instance.lods.size() - 1 : std::size_t{};
        const auto& mesh = instance.lods[lod_index];
        if (mesh == nullptr) continue;
        frame.used_lower_detail = frame.used_lower_detail || lod_index != 0;
        ++frame.visible_instance_count;
        for (const auto& triangle : mesh->triangles) {
            const auto& local_a = mesh->vertices[triangle.vertex_indices[0]];
            const auto& local_b = mesh->vertices[triangle.vertex_indices[1]];
            const auto& local_c = mesh->vertices[triangle.vertex_indices[2]];
            const auto a = transformed(instance.transform, local_a.position);
            const auto b = transformed(instance.transform, local_b.position);
            const auto c = transformed(instance.transform, local_c.position);
            if ((a.z + b.z + c.z) / 3.0 > clip_z) continue;
            const Vector3d centroid{
                (a.x + b.x + c.x) / 3.0 - center.x,
                (a.y + b.y + c.y) / 3.0 - center.y,
                (a.z + b.z + c.z) / 3.0 - center.z,
            };
            const auto normal = transformed(instance.transform, local_a.normal);
            frame.triangles.push_back({
                .points = {screen(a), screen(b), screen(c)},
                .depth = dot(centroid, view),
                .shade = std::clamp(0.35 + 0.65 * std::abs(dot(normal, view)), 0.0, 1.0),
                .material = instance.material,
                .persistent_id = qualified_id(
                    instance.persistent_instance_id, triangle.persistent_id),
            });
        }
        for (const auto& polyline : mesh->edge_polylines) {
            ProjectedPolyline projected{
                .points = {}, .depth = 0.0,
                .persistent_id = qualified_id(
                    instance.persistent_instance_id, polyline.persistent_id),
            };
            for (const auto& local_point : polyline.points) {
                const auto point = transformed(instance.transform, local_point);
                if (point.z > clip_z) continue;
                projected.points.push_back(screen(point));
                projected.depth += dot(subtract(point, center), view);
            }
            if (projected.points.size() >= 2) {
                projected.depth /= static_cast<double>(projected.points.size());
                frame.edge_polylines.push_back(std::move(projected));
            }
        }
    }
    std::ranges::sort(frame.triangles, {}, &ProjectedTriangle::depth);
    std::ranges::sort(frame.edge_polylines, {}, &ProjectedPolyline::depth);
    frame.rendered_triangle_count = frame.triangles.size();
    return frame;
}

std::optional<std::string> hit_test(
    const ProjectedFrame& frame,
    const ScreenPoint point)
{
    for (auto triangle = frame.triangles.rbegin(); triangle != frame.triangles.rend(); ++triangle) {
        if (!triangle->persistent_id.empty() && contains(*triangle, point)) {
            return triangle->persistent_id;
        }
    }
    return std::nullopt;
}

} // namespace oep::viewport
