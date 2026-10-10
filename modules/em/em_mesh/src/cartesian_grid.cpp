// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#include <oep/em_mesh/cartesian_grid.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace oep::em_mesh {
namespace {

using em_geometry::Axis;
using em_geometry::AxisAlignedBox;
using em_geometry::PlanarPatch;
using em_geometry::Point3d;

constexpr auto fraction_tolerance = 1.0e-12;

[[nodiscard]] bool finite(const double value)
{
    return std::isfinite(value);
}

[[nodiscard]] Diagnostic issue(
    const DiagnosticSeverity severity,
    const DiagnosticCode code,
    std::string path,
    std::string message)
{
    return Diagnostic{
        .severity = severity,
        .code = code,
        .path = std::move(path),
        .message = std::move(message),
    };
}

[[nodiscard]] bool safe_cell_count(
    const CartesianGridSpec& spec,
    std::size_t& count)
{
    count = 1;
    for (const auto axis_count : spec.cell_counts) {
        if (axis_count == 0
            || count > std::numeric_limits<std::size_t>::max() / axis_count) {
            return false;
        }
        count *= axis_count;
    }
    return spec.maximum_cells > 0
        && count <= static_cast<std::uint64_t>(spec.maximum_cells);
}

[[nodiscard]] AxisAlignedBox grid_bounds(const CartesianGridSpec& spec)
{
    return AxisAlignedBox{
        .minimum = spec.origin_m,
        .maximum = Point3d{
            spec.origin_m.x
                + static_cast<double>(spec.cell_counts[0]) * spec.cell_size_m[0],
            spec.origin_m.y
                + static_cast<double>(spec.cell_counts[1]) * spec.cell_size_m[1],
            spec.origin_m.z
                + static_cast<double>(spec.cell_counts[2]) * spec.cell_size_m[2],
        },
    };
}

[[nodiscard]] bool contains(
    const AxisAlignedBox& outer,
    const AxisAlignedBox& inner)
{
    const auto scale = std::max({
        1.0,
        std::abs(outer.minimum.x),
        std::abs(outer.minimum.y),
        std::abs(outer.minimum.z),
        std::abs(outer.maximum.x),
        std::abs(outer.maximum.y),
        std::abs(outer.maximum.z),
    });
    const auto tolerance = 64.0 * std::numeric_limits<double>::epsilon() * scale;
    return inner.minimum.x >= outer.minimum.x - tolerance
        && inner.minimum.y >= outer.minimum.y - tolerance
        && inner.minimum.z >= outer.minimum.z - tolerance
        && inner.maximum.x <= outer.maximum.x + tolerance
        && inner.maximum.y <= outer.maximum.y + tolerance
        && inner.maximum.z <= outer.maximum.z + tolerance;
}

[[nodiscard]] double overlap_length(
    const double first_minimum,
    const double first_maximum,
    const double second_minimum,
    const double second_maximum)
{
    return std::max(
        0.0,
        std::min(first_maximum, second_maximum)
            - std::max(first_minimum, second_minimum));
}

[[nodiscard]] double overlap_volume(
    const AxisAlignedBox& first,
    const AxisAlignedBox& second)
{
    return overlap_length(
               first.minimum.x, first.maximum.x, second.minimum.x, second.maximum.x)
        * overlap_length(
            first.minimum.y, first.maximum.y, second.minimum.y, second.maximum.y)
        * overlap_length(
            first.minimum.z, first.maximum.z, second.minimum.z, second.maximum.z);
}

[[nodiscard]] AxisAlignedBox cell_bounds(
    const CartesianGridSpec& spec,
    const CellIndex index)
{
    const Point3d minimum{
        spec.origin_m.x + static_cast<double>(index.x) * spec.cell_size_m[0],
        spec.origin_m.y + static_cast<double>(index.y) * spec.cell_size_m[1],
        spec.origin_m.z + static_cast<double>(index.z) * spec.cell_size_m[2],
    };
    return AxisAlignedBox{
        .minimum = minimum,
        .maximum = Point3d{
            minimum.x + spec.cell_size_m[0],
            minimum.y + spec.cell_size_m[1],
            minimum.z + spec.cell_size_m[2],
        },
    };
}

[[nodiscard]] bool point_in_box(const Point3d point, const AxisAlignedBox& bounds)
{
    return point.x >= bounds.minimum.x && point.x <= bounds.maximum.x
        && point.y >= bounds.minimum.y && point.y <= bounds.maximum.y
        && point.z >= bounds.minimum.z && point.z <= bounds.maximum.z;
}

[[nodiscard]] Point3d center(const AxisAlignedBox& bounds)
{
    return Point3d{
        0.5 * (bounds.minimum.x + bounds.maximum.x),
        0.5 * (bounds.minimum.y + bounds.maximum.y),
        0.5 * (bounds.minimum.z + bounds.maximum.z),
    };
}

[[nodiscard]] double axis_origin(const CartesianGridSpec& spec, const Axis axis)
{
    switch (axis) {
    case Axis::x:
        return spec.origin_m.x;
    case Axis::y:
        return spec.origin_m.y;
    case Axis::z:
        return spec.origin_m.z;
    }
    return 0.0;
}

[[nodiscard]] std::size_t axis_position(const Axis axis)
{
    switch (axis) {
    case Axis::x:
        return 0;
    case Axis::y:
        return 1;
    case Axis::z:
        return 2;
    }
    return 0;
}

[[nodiscard]] std::array<Axis, 2> tangential_axes(const Axis normal)
{
    switch (normal) {
    case Axis::x:
        return {Axis::y, Axis::z};
    case Axis::y:
        return {Axis::x, Axis::z};
    case Axis::z:
        return {Axis::x, Axis::y};
    }
    return {Axis::x, Axis::y};
}

[[nodiscard]] CellIndex with_component(
    CellIndex index,
    const Axis axis,
    const std::size_t value)
{
    switch (axis) {
    case Axis::x:
        index.x = value;
        break;
    case Axis::y:
        index.y = value;
        break;
    case Axis::z:
        index.z = value;
        break;
    }
    return index;
}

[[nodiscard]] double coordinate_minimum(
    const CartesianGridSpec& spec,
    const Axis axis,
    const std::size_t index)
{
    const auto position = axis_position(axis);
    return axis_origin(spec, axis)
        + static_cast<double>(index) * spec.cell_size_m[position];
}

[[nodiscard]] std::optional<std::size_t> normal_layer(
    const CartesianGridSpec& spec,
    const PlanarPatch& patch)
{
    const auto position = axis_position(patch.normal_axis);
    const auto relative = (patch.plane_coordinate_m - axis_origin(spec, patch.normal_axis))
        / spec.cell_size_m[position];
    const auto count = spec.cell_counts[position];
    constexpr auto integer_tolerance = 1.0e-11;
    if (patch.inward_normal_sign > 0) {
        if (relative < -integer_tolerance
            || relative >= static_cast<double>(count) - integer_tolerance) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(std::floor(std::max(0.0, relative)));
    }
    if (relative <= integer_tolerance
        || relative > static_cast<double>(count) + integer_tolerance) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::ceil(std::min(
               static_cast<double>(count), relative)))
        - 1U;
}

struct PatchMapping {
    std::vector<CellOccupancy> cells;
    double exact_area{};
    double subcell_area{};
    double binary_area{};
    double minimum_feature_cells{};
    std::size_t cut_cells{};
};

[[nodiscard]] std::optional<PatchMapping> map_patch(
    const CartesianGridSpec& spec,
    const PlanarPatch& patch)
{
    const auto normal = normal_layer(spec, patch);
    if (!normal.has_value()) {
        return std::nullopt;
    }
    const auto tangent = tangential_axes(patch.normal_axis);
    const auto first_position = axis_position(tangent[0]);
    const auto second_position = axis_position(tangent[1]);
    const auto cell_area = spec.cell_size_m[first_position]
        * spec.cell_size_m[second_position];
    PatchMapping mapping;
    mapping.exact_area = (patch.first_maximum_m - patch.first_minimum_m)
        * (patch.second_maximum_m - patch.second_minimum_m);
    mapping.minimum_feature_cells = std::min(
        (patch.first_maximum_m - patch.first_minimum_m)
            / spec.cell_size_m[first_position],
        (patch.second_maximum_m - patch.second_minimum_m)
            / spec.cell_size_m[second_position]);

    for (std::size_t second = 0; second < spec.cell_counts[second_position]; ++second) {
        const auto second_minimum = coordinate_minimum(spec, tangent[1], second);
        const auto second_overlap = overlap_length(
            second_minimum,
            second_minimum + spec.cell_size_m[second_position],
            patch.second_minimum_m,
            patch.second_maximum_m);
        if (second_overlap <= 0.0) {
            continue;
        }
        for (std::size_t first = 0; first < spec.cell_counts[first_position]; ++first) {
            const auto first_minimum = coordinate_minimum(spec, tangent[0], first);
            const auto first_overlap = overlap_length(
                first_minimum,
                first_minimum + spec.cell_size_m[first_position],
                patch.first_minimum_m,
                patch.first_maximum_m);
            if (first_overlap <= 0.0) {
                continue;
            }
            const auto overlap_area = first_overlap * second_overlap;
            auto index = with_component({}, patch.normal_axis, *normal);
            index = with_component(index, tangent[0], first);
            index = with_component(index, tangent[1], second);
            const auto fraction = overlap_area / cell_area;
            mapping.cells.push_back(CellOccupancy{.cell = index, .fraction = fraction});
            mapping.subcell_area += overlap_area;
            if (fraction < 1.0 - fraction_tolerance) {
                ++mapping.cut_cells;
            }
            const auto first_center = first_minimum
                + 0.5 * spec.cell_size_m[first_position];
            const auto second_center = second_minimum
                + 0.5 * spec.cell_size_m[second_position];
            if (first_center >= patch.first_minimum_m
                && first_center <= patch.first_maximum_m
                && second_center >= patch.second_minimum_m
                && second_center <= patch.second_maximum_m) {
                mapping.binary_area += cell_area;
            }
        }
    }
    const auto relative_area_error = std::abs(mapping.subcell_area - mapping.exact_area)
        / mapping.exact_area;
    if (mapping.cells.empty() || relative_area_error > 1.0e-10) {
        return std::nullopt;
    }
    return mapping;
}

[[nodiscard]] StaircasingRisk classify_risk(
    const double minimum_feature_cells,
    const double relative_binary_error)
{
    if (minimum_feature_cells < 2.0 || relative_binary_error > 0.10) {
        return StaircasingRisk::high;
    }
    if (minimum_feature_cells < 4.0 || relative_binary_error > 0.03) {
        return StaircasingRisk::moderate;
    }
    return StaircasingRisk::low;
}

[[nodiscard]] StaircasingReport patch_report(
    std::string id,
    const MappedFeatureKind kind,
    const PatchMapping& mapping)
{
    const auto relative_error = std::abs(mapping.binary_area - mapping.exact_area)
        / mapping.exact_area;
    return StaircasingReport{
        .mapping_id = std::move(id),
        .kind = kind,
        .exact_measure = mapping.exact_area,
        .subcell_measure = mapping.subcell_area,
        .binary_cell_measure = mapping.binary_area,
        .relative_binary_measure_error = relative_error,
        .minimum_feature_cells = mapping.minimum_feature_cells,
        .cut_cell_count = mapping.cut_cells,
        .risk = classify_risk(mapping.minimum_feature_cells, relative_error),
    };
}

void report_risk(
    const StaircasingReport& report,
    std::vector<Diagnostic>& diagnostics)
{
    if (report.risk == StaircasingRisk::low) {
        return;
    }
    diagnostics.push_back(issue(
        DiagnosticSeverity::warning,
        DiagnosticCode::staircasing_risk,
        "staircasing[" + report.mapping_id + ']',
        report.risk == StaircasingRisk::high
            ? "feature resolution is below two cells or binary measure error exceeds ten percent"
            : "feature resolution is below four cells or binary measure error exceeds three percent"));
}

} // namespace

std::size_t CartesianGrid::linear_index(const CellIndex index) const
{
    if (index.x >= spec.cell_counts[0] || index.y >= spec.cell_counts[1]
        || index.z >= spec.cell_counts[2]) {
        throw std::out_of_range("Cartesian cell index is outside the grid");
    }
    return (index.z * spec.cell_counts[1] + index.y) * spec.cell_counts[0] + index.x;
}

const MaterialCell& CartesianGrid::cell(const CellIndex index) const
{
    return material_cells.at(linear_index(index));
}

bool GridResult::contains(const DiagnosticCode code) const noexcept
{
    return std::ranges::any_of(diagnostics, [code](const Diagnostic& item) {
        return item.code == code;
    });
}

GridResult CartesianGridBuilder::build(
    const em_geometry::ComputationalGeometry& geometry,
    const CartesianGridSpec& spec) const
{
    GridResult result;
    std::size_t cell_count{};
    const auto valid_spacing = std::ranges::all_of(spec.cell_size_m, [](const double value) {
        return finite(value) && value > 0.0;
    });
    if (!valid_spacing || !finite(spec.origin_m.x) || !finite(spec.origin_m.y)
        || !finite(spec.origin_m.z) || !finite(spec.material_assignment_threshold)
        || spec.material_assignment_threshold <= 0.0
        || spec.material_assignment_threshold > 1.0) {
        result.diagnostics.push_back(issue(
            DiagnosticSeverity::error,
            DiagnosticCode::invalid_grid,
            "grid",
            "Cartesian grid origin and cell sizes must be finite, spacing positive, and material threshold in (0, 1]"));
        return result;
    }
    if (!safe_cell_count(spec, cell_count)) {
        result.diagnostics.push_back(issue(
            DiagnosticSeverity::error,
            DiagnosticCode::cell_limit_exceeded,
            "grid.cell_counts",
            "Cartesian cell count is invalid, overflows, or exceeds the declared limit"));
        return result;
    }

    const auto domain = grid_bounds(spec);
    if (!contains(geometry.background.bounds, domain)) {
        result.diagnostics.push_back(issue(
            DiagnosticSeverity::error,
            DiagnosticCode::geometry_outside_grid,
            "background",
            "the Cartesian domain must remain inside the resolved background volume"));
    }
    for (const auto& region : geometry.material_regions) {
        if (!contains(domain, region.bounds)) {
            result.diagnostics.push_back(issue(
                DiagnosticSeverity::error,
                DiagnosticCode::geometry_outside_grid,
                "material_regions[" + region.role_assignment_id + ']',
                "material volume lies outside the Cartesian domain"));
        }
    }
    for (std::size_t first = 0; first < geometry.material_regions.size(); ++first) {
        for (std::size_t second = first + 1;
             second < geometry.material_regions.size(); ++second) {
            const auto overlap = overlap_volume(
                geometry.material_regions[first].bounds,
                geometry.material_regions[second].bounds);
            if (overlap > 0.0) {
                result.diagnostics.push_back(issue(
                    DiagnosticSeverity::error,
                    DiagnosticCode::overlapping_material_regions,
                    "material_regions",
                    "resolved non-background material volumes overlap and need an explicit precedence policy"));
            }
        }
    }
    if (std::ranges::any_of(result.diagnostics, [](const Diagnostic& item) {
            return item.severity == DiagnosticSeverity::error;
        })) {
        return result;
    }

    CartesianGrid grid{
        .spec = spec,
        .study_fingerprint = geometry.study_fingerprint,
        .geometry_hash = geometry.geometry_hash,
        .materials = geometry.materials,
        .material_cells = {},
        .region_mappings = {},
        .surface_mappings = {},
        .port_mappings = {},
        .staircasing = {},
    };
    grid.material_cells.reserve(cell_count);
    for (std::size_t z = 0; z < spec.cell_counts[2]; ++z) {
        for (std::size_t y = 0; y < spec.cell_counts[1]; ++y) {
            for (std::size_t x = 0; x < spec.cell_counts[0]; ++x) {
                grid.material_cells.push_back(MaterialCell{
                    .index = CellIndex{x, y, z},
                    .material_id = geometry.background.material_id,
                    .role_assignment_id = geometry.background.role_assignment_id,
                    .source_entity = geometry.background.source,
                    .dominant_occupancy_fraction = 1.0,
                });
            }
        }
    }

    std::vector<double> selected_fraction(cell_count);
    std::vector<double> occupied_fraction(cell_count);
    std::vector<std::size_t> contribution_count(cell_count);
    const auto cell_volume = spec.cell_size_m[0] * spec.cell_size_m[1]
        * spec.cell_size_m[2];
    for (const auto& region : geometry.material_regions) {
        RegionCellMapping mapping{
            .role_assignment_id = region.role_assignment_id,
            .role = region.role,
            .material_id = region.material_id,
            .source_entity = region.source,
            .cells = {},
        };
        auto subcell_volume = 0.0;
        auto binary_volume = 0.0;
        std::size_t cut_cells{};
        for (std::size_t z = 0; z < spec.cell_counts[2]; ++z) {
            for (std::size_t y = 0; y < spec.cell_counts[1]; ++y) {
                for (std::size_t x = 0; x < spec.cell_counts[0]; ++x) {
                    const CellIndex index{x, y, z};
                    const auto bounds = cell_bounds(spec, index);
                    const auto overlap = overlap_volume(bounds, region.bounds);
                    if (overlap <= 0.0) {
                        continue;
                    }
                    const auto fraction = overlap / cell_volume;
                    mapping.cells.push_back(CellOccupancy{.cell = index, .fraction = fraction});
                    subcell_volume += overlap;
                    if (fraction < 1.0 - fraction_tolerance) {
                        ++cut_cells;
                    }
                    if (point_in_box(center(bounds), region.bounds)) {
                        binary_volume += cell_volume;
                    }
                    const auto linear = grid.linear_index(index);
                    occupied_fraction[linear] += fraction;
                    ++contribution_count[linear];
                    if (fraction >= spec.material_assignment_threshold
                        && fraction > selected_fraction[linear]) {
                        selected_fraction[linear] = fraction;
                        auto& cell = grid.material_cells[linear];
                        cell.material_id = region.material_id;
                        cell.role_assignment_id = region.role_assignment_id;
                        cell.source_entity = region.source;
                        cell.dominant_occupancy_fraction = fraction;
                    }
                }
            }
        }
        const auto exact_volume = em_geometry::volume_m3(region.bounds);
        const auto relative_error = std::abs(binary_volume - exact_volume) / exact_volume;
        const auto minimum_cells = std::min({
            (region.bounds.maximum.x - region.bounds.minimum.x) / spec.cell_size_m[0],
            (region.bounds.maximum.y - region.bounds.minimum.y) / spec.cell_size_m[1],
            (region.bounds.maximum.z - region.bounds.minimum.z) / spec.cell_size_m[2],
        });
        auto report = StaircasingReport{
            .mapping_id = region.role_assignment_id,
            .kind = MappedFeatureKind::material_volume,
            .exact_measure = exact_volume,
            .subcell_measure = subcell_volume,
            .binary_cell_measure = binary_volume,
            .relative_binary_measure_error = relative_error,
            .minimum_feature_cells = minimum_cells,
            .cut_cell_count = cut_cells,
            .risk = classify_risk(minimum_cells, relative_error),
        };
        grid.staircasing.push_back(report);
        report_risk(report, result.diagnostics);
        grid.region_mappings.push_back(std::move(mapping));
    }
    for (std::size_t index = 0; index < cell_count; ++index) {
        if (selected_fraction[index] == 0.0) {
            grid.material_cells[index].dominant_occupancy_fraction
                = std::max(0.0, 1.0 - occupied_fraction[index]);
        }
    }
    const auto mixed_cells = static_cast<std::size_t>(std::ranges::count_if(
        contribution_count, [](const std::size_t count) { return count > 1; }));
    if (mixed_cells > 0) {
        result.diagnostics.push_back(issue(
            DiagnosticSeverity::warning,
            DiagnosticCode::mixed_material_cell,
            "material_cells",
            std::to_string(mixed_cells)
                + " cells contain distinct non-overlapping material contributions; subcell mappings preserve both"));
    }

    for (const auto& surface : geometry.surface_regions) {
        const auto mapped = map_patch(spec, surface.patch);
        if (!mapped.has_value()) {
            result.diagnostics.push_back(issue(
                DiagnosticSeverity::error,
                DiagnosticCode::unmapped_feature,
                "surface_regions[" + surface.role_assignment_id + ']',
                "surface patch lies outside the grid or cannot be represented on its inward cell layer"));
            continue;
        }
        grid.surface_mappings.push_back(SurfaceCellMapping{
            .role_assignment_id = surface.role_assignment_id,
            .role = surface.role,
            .material_id = surface.material_id,
            .source_entity = surface.source,
            .cells = mapped->cells,
        });
        auto report = patch_report(
            surface.role_assignment_id, MappedFeatureKind::role_surface, *mapped);
        grid.staircasing.push_back(report);
        report_risk(report, result.diagnostics);
    }

    for (const auto& port : geometry.ports) {
        const auto face = map_patch(spec, port.face);
        const auto reference = map_patch(spec, port.reference_plane);
        if (!face.has_value() || !reference.has_value()) {
            result.diagnostics.push_back(issue(
                DiagnosticSeverity::error,
                DiagnosticCode::unmapped_feature,
                "ports[" + port.port_id + ']',
                "port face or reference plane lies outside the Cartesian grid"));
            continue;
        }
        grid.port_mappings.push_back(PortCellMapping{
            .port_id = port.port_id,
            .kind = port.kind,
            .face_source = port.face_source,
            .reference_plane_source = port.reference_plane_source,
            .inward_orientation = port.inward_orientation,
            .mode_index = port.mode_index,
            .reference_impedance_ohm = port.reference_impedance_ohm,
            .deembedding_length_m = port.deembedding_length_m,
            .termination = port.termination,
            .face_cells = face->cells,
            .reference_plane_cells = reference->cells,
        });
        auto face_report = patch_report(
            port.port_id + ".face", MappedFeatureKind::port_face, *face);
        auto reference_report = patch_report(
            port.port_id + ".reference_plane",
            MappedFeatureKind::port_reference_plane,
            *reference);
        grid.staircasing.push_back(face_report);
        grid.staircasing.push_back(reference_report);
        report_risk(face_report, result.diagnostics);
        report_risk(reference_report, result.diagnostics);
    }

    if (std::ranges::any_of(result.diagnostics, [](const Diagnostic& item) {
            return item.severity == DiagnosticSeverity::error;
        })) {
        return result;
    }
    result.grid = std::move(grid);
    return result;
}

} // namespace oep::em_mesh
