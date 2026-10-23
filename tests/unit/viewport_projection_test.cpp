// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#include <oep/viewport/viewport.hpp>

#include <cmath>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {

void require(const bool condition, const char* const message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main()
{
    oep::tessellation::ScenePacket packet{
        .geometry_hash = "geometry-7",
        .bounds = {{0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}},
        .vertices = {
            {{0.0, 0.0, 0.5}, {0.0, 0.0, 1.0}},
            {{1.0, 0.0, 0.5}, {0.0, 0.0, 1.0}},
            {{0.0, 1.0, 0.5}, {0.0, 0.0, 1.0}},
        },
        .triangles = {{{0, 1, 2}, "hole-cut:42"}},
        .edge_polylines = {{{{0.0, 0.0, 0.5}, {1.0, 0.0, 0.5}}, "hole-cut:9"}},
    };
    oep::viewport::CameraState camera;
    const auto initial = oep::viewport::project(packet, camera, 800.0, 600.0);
    require(initial.triangles.size() == 1, "a visible mesh triangle projects");
    require(initial.edge_polylines.size() == 1, "a visible edge projects");
    const auto& triangle = initial.triangles.front();
    const oep::viewport::ScreenPoint centroid{
        (triangle.points[0].x + triangle.points[1].x + triangle.points[2].x) / 3.0,
        (triangle.points[0].y + triangle.points[1].y + triangle.points[2].y) / 3.0,
    };
    require(oep::viewport::hit_test(initial, centroid) == "hole-cut:42",
        "picking returns the persistent document identity");

    oep::viewport::pan(camera, 20.0, -10.0);
    const auto panned = oep::viewport::project(packet, camera, 800.0, 600.0);
    require(std::abs(panned.triangles.front().points[0].x
        - initial.triangles.front().points[0].x - 20.0) < 1.0e-9,
        "pan translates projected geometry");
    require(std::abs(panned.triangles.front().points[0].y
        - initial.triangles.front().points[0].y + 10.0) < 1.0e-9,
        "vertical pan translates projected geometry");

    oep::viewport::orbit(camera, 25.0, 15.0);
    const auto orbited = oep::viewport::project(packet, camera, 800.0, 600.0);
    require(orbited.triangles.front().points != panned.triangles.front().points,
        "orbit changes the view basis");
    const auto zoom_before = camera.zoom;
    oep::viewport::zoom(camera, 120.0);
    require(camera.zoom > zoom_before, "wheel input changes bounded zoom");

    oep::viewport::set_clip_fraction(camera, 0.4);
    require(oep::viewport::project(packet, camera, 800.0, 600.0).triangles.empty(),
        "section clipping removes geometry beyond the plane");
    oep::viewport::fit(camera);
    require(camera.zoom == 1.0 && camera.pan_x == 0.0 && camera.pan_y == 0.0,
        "fit resets screen framing without changing orientation");

    // A shared fine mesh represents a one-million-triangle source assembly,
    // while every instance also supplies a deliberate low-detail display mesh.
    // This measures navigation projection, including transforms and sorting,
    // without allocating a duplicate million-triangle buffer per component.
    auto fine = packet;
    fine.triangles.clear();
    fine.triangles.reserve(4096);
    for (std::size_t triangle_index = 0; triangle_index < 4096; ++triangle_index) {
        fine.triangles.push_back({{0, 1, 2}, "component-face:7"});
    }
    const auto fine_mesh = std::make_shared<const oep::tessellation::ScenePacket>(std::move(fine));
    const auto coarse_mesh = std::make_shared<const oep::tessellation::ScenePacket>(packet);
    std::vector<oep::viewport::SceneInstance> instances;
    instances.reserve(257);
    for (std::size_t instance_index = 0; instance_index < 256; ++instance_index) {
        oep::viewport::SceneTransform transform;
        transform.values[12] = static_cast<double>(instance_index % 16) * 1.5;
        transform.values[13] = static_cast<double>(instance_index / 16) * 1.5;
        instances.push_back({
            .persistent_instance_id = "assembly-instance-" + std::to_string(instance_index),
            .transform = transform,
            .material = {{0.25, 0.6, 0.8, 1.0}},
            .visible = true,
            .lods = {fine_mesh, coarse_mesh},
        });
    }
    instances.push_back({
        .persistent_instance_id = "hidden-instance",
        .transform = {},
        .material = {},
        .visible = false,
        .lods = {fine_mesh, coarse_mesh},
    });
    const auto assembly = oep::viewport::make_assembly(std::move(instances));
    require(assembly.source_triangle_count == 1'048'576,
        "representative assembly retains its full source triangle count");
    oep::viewport::CameraState assembly_camera;
    const auto assembly_frame = oep::viewport::project(
        assembly, assembly_camera, 1280.0, 720.0, {.triangle_budget = 10'000});
    require(assembly_frame.used_lower_detail, "large assembly selects its display LOD");
    require(assembly_frame.visible_instance_count == 256,
        "visibility excludes hidden assembly occurrences");
    require(assembly_frame.rendered_triangle_count == 256,
        "shared coarse LOD bounds navigation work");
    require(assembly_frame.triangles.front().material.base_color[1] == 0.6,
        "instance material reaches projected primitives");
    const auto& assembly_triangle = assembly_frame.triangles.front();
    const oep::viewport::ScreenPoint assembly_centroid{
        (assembly_triangle.points[0].x + assembly_triangle.points[1].x
            + assembly_triangle.points[2].x) / 3.0,
        (assembly_triangle.points[0].y + assembly_triangle.points[1].y
            + assembly_triangle.points[2].y) / 3.0,
    };
    const auto assembly_hit = oep::viewport::hit_test(assembly_frame, assembly_centroid);
    require(assembly_hit.has_value()
            && assembly_hit->starts_with("assembly-instance-")
            && assembly_hit->ends_with("/hole-cut:42"),
        "assembly picking returns instance-qualified persistent identity");

    constexpr std::size_t measured_frames = 120;
    const auto started = std::chrono::steady_clock::now();
    std::size_t projected_triangles{};
    for (std::size_t frame_index = 0; frame_index < measured_frames; ++frame_index) {
        oep::viewport::orbit(assembly_camera, 0.2, 0.1);
        projected_triangles += oep::viewport::project(
            assembly, assembly_camera, 1280.0, 720.0,
            {.triangle_budget = 10'000}).rendered_triangle_count;
    }
    const auto average_milliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count()
        / static_cast<double>(measured_frames);
    require(projected_triangles > 0, "measured navigation produced display geometry");
    require(average_milliseconds < 16.667,
        "one-million-source-triangle assembly projects within a 60 Hz CPU budget");
    std::cout << "viewport assembly projection average: "
              << average_milliseconds << " ms\n";
    return 0;
}
