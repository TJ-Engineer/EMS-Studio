// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

// Renderer-neutral camera, clipping, projection, and hit-testing contract.
// Qt consumes projected frames, while selection remains a persistent model ID.

#pragma once

#include <oep/tessellation/scene_packet.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace oep::viewport {

struct ScreenPoint {
    double x{};
    double y{};

    bool operator==(const ScreenPoint&) const = default;
};

struct CameraState {
    double yaw{-0.65};
    double pitch{0.45};
    double zoom{1.0};
    double pan_x{};
    double pan_y{};
    double clip_fraction{1.0};
};

struct SceneMaterial {
    std::array<double, 4> base_color{0.34, 0.55, 0.65, 1.0};
};

// Column-major affine transform. Instances share immutable mesh LODs, so a
// large assembly does not duplicate display buffers or persistent entity IDs.
struct SceneTransform {
    std::array<double, 16> values{
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0,
    };
};

struct SceneInstance {
    std::string persistent_instance_id;
    SceneTransform transform;
    SceneMaterial material;
    bool visible{true};
    // Ordered from highest to lowest detail. Mesh values are immutable and can
    // be shared by every occurrence of the same component.
    std::vector<std::shared_ptr<const tessellation::ScenePacket>> lods;
};

struct SceneAssembly {
    modeler::BoundingBox bounds;
    std::vector<SceneInstance> instances;
    std::size_t source_triangle_count{};
};

struct ProjectionPolicy {
    std::size_t triangle_budget{100'000};
};

struct ProjectedTriangle {
    std::array<ScreenPoint, 3> points{};
    double depth{};
    double shade{};
    SceneMaterial material;
    std::string persistent_id;
};

struct ProjectedPolyline {
    std::vector<ScreenPoint> points;
    double depth{};
    std::string persistent_id;
};

struct ProjectedFrame {
    std::vector<ProjectedTriangle> triangles;
    std::vector<ProjectedPolyline> edge_polylines;
    std::size_t source_triangle_count{};
    std::size_t rendered_triangle_count{};
    std::size_t visible_instance_count{};
    bool used_lower_detail{};
};

void orbit(CameraState& camera, double delta_x, double delta_y) noexcept;
void pan(CameraState& camera, double delta_x, double delta_y) noexcept;
void zoom(CameraState& camera, double wheel_delta) noexcept;
void set_clip_fraction(CameraState& camera, double fraction) noexcept;
void fit(CameraState& camera) noexcept;

[[nodiscard]] ProjectedFrame project(
    const tessellation::ScenePacket& packet,
    const CameraState& camera,
    double width,
    double height);

[[nodiscard]] SceneAssembly make_assembly(std::vector<SceneInstance> instances);

[[nodiscard]] ProjectedFrame project(
    const SceneAssembly& assembly,
    const CameraState& camera,
    double width,
    double height,
    const ProjectionPolicy& policy = {});

[[nodiscard]] std::optional<std::string> hit_test(
    const ProjectedFrame& frame,
    ScreenPoint point);

} // namespace oep::viewport
