#include <oep/test/fake_geometry_kernel.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace oep::test {
namespace {

using namespace modeler;

constexpr double tolerance = 1.0e-9;

[[nodiscard]] GeometryDiagnostic diagnostic(
    const GeometryErrorCode code,
    std::string message,
    std::optional<TopologyEntity> entity = std::nullopt,
    std::optional<std::string> repair_hint = std::nullopt)
{
    return GeometryDiagnostic{
        .code = code,
        .message = std::move(message),
        .entity = std::move(entity),
        .repair_hint = std::move(repair_hint),
    };
}

[[nodiscard]] OperationResult operation_failure(GeometryDiagnostic error)
{
    return OperationResult{
        .body = std::nullopt,
        .provenance = {},
        .diagnostics = {std::move(error)},
    };
}

[[nodiscard]] OperationResult cancelled_operation()
{
    return operation_failure(diagnostic(
        GeometryErrorCode::cancelled,
        "operation cancelled before execution",
        std::nullopt,
        "retry the operation when cancellation is no longer requested"));
}

[[nodiscard]] BodySummaryResult summary_failure(GeometryDiagnostic error)
{
    return BodySummaryResult{
        .summary = std::nullopt,
        .diagnostics = {std::move(error)},
    };
}

[[nodiscard]] BodySnapshotResult snapshot_failure(GeometryDiagnostic error)
{
    return BodySnapshotResult{
        .snapshot = std::nullopt,
        .diagnostics = {std::move(error)},
    };
}

[[nodiscard]] PlanarFaceResult face_failure(GeometryDiagnostic error)
{
    return PlanarFaceResult{
        .frame = std::nullopt,
        .diagnostics = {std::move(error)},
    };
}

[[nodiscard]] bool finite(const double value)
{
    return std::isfinite(value);
}

[[nodiscard]] bool finite(const Point3d& point)
{
    return finite(point.x) && finite(point.y) && finite(point.z);
}

[[nodiscard]] bool finite(const Vector3d& vector)
{
    return finite(vector.x) && finite(vector.y) && finite(vector.z);
}

[[nodiscard]] double magnitude(const Vector3d& vector)
{
    return std::sqrt(vector.x * vector.x + vector.y * vector.y + vector.z * vector.z);
}

[[nodiscard]] bool approximately_equal(const double left, const double right)
{
    return std::abs(left - right) <= tolerance;
}

[[nodiscard]] std::string stable_hash(
    const std::string_view operation,
    const BodySummary& summary)
{
    std::ostringstream stream;
    stream << std::setprecision(17) << operation << ':' << summary.solid_count << ':'
           << summary.face_count << ':' << summary.edge_count << ':'
           << summary.vertex_count << ':' << summary.volume << ':'
           << summary.bounds.minimum.x << ':' << summary.bounds.minimum.y << ':'
           << summary.bounds.minimum.z << ':' << summary.bounds.maximum.x << ':'
           << summary.bounds.maximum.y << ':' << summary.bounds.maximum.z << ':'
           << summary.topology_signature;
    return stream.str();
}

[[nodiscard]] double rectangle_area(const ProfileLoop& loop)
{
    if (loop.vertices.size() < 3) {
        return 0.0;
    }
    auto twice_area = 0.0;
    for (std::size_t index = 0; index < loop.vertices.size(); ++index) {
        const auto& current = loop.vertices[index];
        const auto& next = loop.vertices[(index + 1) % loop.vertices.size()];
        if (!finite(current.x) || !finite(current.y)) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        twice_area += current.x * next.y - next.x * current.y;
    }
    return std::abs(twice_area) * 0.5;
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

[[nodiscard]] double minimum_extent(const BoundingBox& bounds)
{
    return std::min({
        bounds.maximum.x - bounds.minimum.x,
        bounds.maximum.y - bounds.minimum.y,
        bounds.maximum.z - bounds.minimum.z,
    });
}

[[nodiscard]] TopologyEntity add_entity(
    FakeGeometryKernel::BodyData& body,
    const ExactBodyHandle handle,
    std::string role,
    const TopologyKind kind)
{
    auto entity = TopologyEntity{
        .body = handle,
        .id = KernelTopologyId{std::to_string(handle.value) + ':' + role},
        .kind = kind,
    };
    body.entities.emplace(std::move(role), entity);
    return entity;
}

} // namespace

KernelIdentity FakeGeometryKernel::identity() const
{
    return KernelIdentity{
        .name = "deterministic-fake",
        .version = "1",
        .provides_exact_brep = false,
    };
}

OperationResult FakeGeometryKernel::extrude(
    const ExtrudeRequest& request,
    const CancellationToken& cancellation)
{
    if (cancellation.is_cancellation_requested()) {
        return cancelled_operation();
    }
    if (!finite(request.distance) || request.distance <= tolerance
        || request.profile.loops.size() != 1
        || request.profile.loops.front().is_hole) {
        return operation_failure(diagnostic(
            GeometryErrorCode::degenerate_geometry,
            "fake extrusion requires one non-hole loop and positive distance",
            std::nullopt,
            "supply a closed outer loop and a distance greater than zero"));
    }
    const auto& frame = request.profile.frame;
    if (!finite(frame.origin) || !finite(frame.normal) || !finite(frame.x_direction)
        || !approximately_equal(frame.normal.x, 0.0)
        || !approximately_equal(frame.normal.y, 0.0)
        || !approximately_equal(frame.normal.z, 1.0)
        || !approximately_equal(frame.x_direction.x, 1.0)
        || !approximately_equal(frame.x_direction.y, 0.0)
        || !approximately_equal(frame.x_direction.z, 0.0)) {
        return operation_failure(diagnostic(
            GeometryErrorCode::unsupported_operation,
            "fake adapter supports profiles in an XY frame only",
            std::nullopt,
            "run this case against an exact kernel adapter"));
    }

    const auto& loop = request.profile.loops.front();
    const auto area = rectangle_area(loop);
    if (!finite(area) || area <= tolerance || loop.vertices.size() != 4) {
        return operation_failure(diagnostic(
            GeometryErrorCode::degenerate_geometry,
            "profile loop must be a finite nondegenerate quadrilateral",
            std::nullopt,
            "remove coincident points or increase the profile area"));
    }

    auto minimum_x = loop.vertices.front().x;
    auto maximum_x = minimum_x;
    auto minimum_y = loop.vertices.front().y;
    auto maximum_y = minimum_y;
    for (const auto& point : loop.vertices) {
        minimum_x = std::min(minimum_x, point.x);
        maximum_x = std::max(maximum_x, point.x);
        minimum_y = std::min(minimum_y, point.y);
        maximum_y = std::max(maximum_y, point.y);
    }
    if (!approximately_equal(area, (maximum_x - minimum_x) * (maximum_y - minimum_y))) {
        return operation_failure(diagnostic(
            GeometryErrorCode::unsupported_operation,
            "fake adapter supports axis-aligned rectangular profiles only"));
    }

    BodyData body_data;
    body_data.summary = BodySummary{
        .solid_count = 1,
        .face_count = 6,
        .edge_count = 12,
        .vertex_count = 8,
        .volume = area * request.distance,
        .bounds = BoundingBox{
            .minimum = Point3d{
                frame.origin.x + minimum_x,
                frame.origin.y + minimum_y,
                frame.origin.z,
            },
            .maximum = Point3d{
                frame.origin.x + maximum_x,
                frame.origin.y + maximum_y,
                frame.origin.z + request.distance,
            },
        },
        .topology_signature = "extrude:solid1:face6:edge12:vertex8",
        .content_hash = {},
    };
    body_data.summary.content_hash = stable_hash("extrude", body_data.summary);

    const auto handle = ExactBodyHandle{next_handle_++};
    auto provenance = TopologyProvenance{};
    const auto start_face = add_entity(body_data, handle, "extrude.cap.start", TopologyKind::face);
    const auto end_face = add_entity(body_data, handle, "extrude.cap.end", TopologyKind::face);
    provenance.generated.push_back(GeneratedTopology{
        .semantic_role = "extrude.cap.start",
        .result = start_face,
    });
    provenance.generated.push_back(GeneratedTopology{
        .semantic_role = "extrude.cap.end",
        .result = end_face,
    });
    body_data.face_frames.emplace(
        start_face.id.value,
        PlaneFrame{
            .origin = Point3d{frame.origin.x, frame.origin.y, frame.origin.z},
            .normal = Vector3d{0.0, 0.0, -1.0},
            .x_direction = Vector3d{1.0, 0.0, 0.0},
        });
    body_data.face_frames.emplace(
        end_face.id.value,
        PlaneFrame{
            .origin = Point3d{
                frame.origin.x, frame.origin.y, frame.origin.z + request.distance},
            .normal = Vector3d{0.0, 0.0, 1.0},
            .x_direction = Vector3d{1.0, 0.0, 0.0},
        });
    for (std::size_t index = 0; index < 4; ++index) {
        const auto role = "extrude.side." + std::to_string(index);
        provenance.generated.push_back(GeneratedTopology{
            .semantic_role = role,
            .result = add_entity(body_data, handle, role, TopologyKind::face),
        });
    }
    for (std::size_t index = 0; index < 12; ++index) {
        const auto role = "extrude.edge." + std::to_string(index);
        provenance.generated.push_back(GeneratedTopology{
            .semantic_role = role,
            .result = add_entity(body_data, handle, role, TopologyKind::edge),
        });
    }
    bodies_.emplace(handle.value, std::move(body_data));
    return OperationResult{
        .body = handle,
        .provenance = std::move(provenance),
        .diagnostics = {},
    };
}

OperationResult FakeGeometryKernel::cut(
    const BooleanCutRequest& request,
    const CancellationToken& cancellation)
{
    if (cancellation.is_cancellation_requested()) {
        return cancelled_operation();
    }
    const auto* target = body(request.target);
    const auto* tool = body(request.tool);
    if (target == nullptr || tool == nullptr) {
        return operation_failure(diagnostic(
            GeometryErrorCode::invalid_handle,
            "boolean cut requires live target and tool handles"));
    }
    const auto overlap_x = overlap_length(
        target->summary.bounds.minimum.x,
        target->summary.bounds.maximum.x,
        tool->summary.bounds.minimum.x,
        tool->summary.bounds.maximum.x);
    const auto overlap_y = overlap_length(
        target->summary.bounds.minimum.y,
        target->summary.bounds.maximum.y,
        tool->summary.bounds.minimum.y,
        tool->summary.bounds.maximum.y);
    const auto overlap_z = overlap_length(
        target->summary.bounds.minimum.z,
        target->summary.bounds.maximum.z,
        tool->summary.bounds.minimum.z,
        tool->summary.bounds.maximum.z);
    const auto removed_volume = overlap_x * overlap_y * overlap_z;
    if (removed_volume <= tolerance || removed_volume >= target->summary.volume - tolerance) {
        return operation_failure(diagnostic(
            GeometryErrorCode::degenerate_geometry,
            "boolean tool must remove a proper nonempty portion of the target",
            std::nullopt,
            "move or resize the tool so it intersects without consuming the target"));
    }

    BodyData result_data;
    result_data.summary = target->summary;
    result_data.summary.face_count += 4;
    result_data.summary.edge_count += 8;
    result_data.summary.volume -= removed_volume;
    result_data.summary.topology_signature = "cut:" + target->summary.topology_signature;
    result_data.summary.content_hash = stable_hash("cut", result_data.summary);
    const auto handle = ExactBodyHandle{next_handle_++};
    auto provenance = TopologyProvenance{};
    const auto wall = add_entity(result_data, handle, "cut.wall.0", TopologyKind::face);
    provenance.generated.push_back(GeneratedTopology{
        .semantic_role = "cut.wall.0",
        .result = wall,
    });
    const auto source = target->entities.find("extrude.cap.end");
    if (source != target->entities.end()) {
        const auto modified = add_entity(
            result_data, handle, "cut.modified.cap.end", TopologyKind::face);
        provenance.modified.push_back(ModifiedTopology{
            .source = source->second,
            .result = modified,
        });
        const auto source_frame = target->face_frames.find(source->second.id.value);
        if (source_frame != target->face_frames.end()) {
            result_data.face_frames.emplace(modified.id.value, source_frame->second);
        }
    }
    bodies_.emplace(handle.value, std::move(result_data));
    return OperationResult{
        .body = handle,
        .provenance = std::move(provenance),
        .diagnostics = {},
    };
}

OperationResult FakeGeometryKernel::fillet(
    const FilletRequest& request,
    const CancellationToken& cancellation)
{
    if (cancellation.is_cancellation_requested()) {
        return cancelled_operation();
    }
    const auto* source = body(request.body);
    if (source == nullptr) {
        return operation_failure(diagnostic(
            GeometryErrorCode::invalid_handle, "fillet body handle is not live"));
    }
    if (!finite(request.radius) || request.radius <= tolerance
        || request.radius >= minimum_extent(source->summary.bounds) * 0.5) {
        return operation_failure(diagnostic(
            GeometryErrorCode::degenerate_geometry,
            "fillet radius is outside the supported geometric range",
            std::nullopt,
            "use a radius below half the smallest body extent"));
    }
    if (request.edges.empty()) {
        return operation_failure(diagnostic(
            GeometryErrorCode::invalid_input,
            "fillet requires at least one edge",
            std::nullopt,
            "select one or more edges from the input body"));
    }
    for (const auto& edge : request.edges) {
        const auto known = std::ranges::any_of(
            source->entities,
            [&edge](const auto& entry) { return entry.second == edge; });
        if (edge.body != request.body || edge.kind != TopologyKind::edge || !known) {
            return operation_failure(diagnostic(
                GeometryErrorCode::topology_reference_missing,
                "fillet edge does not belong to the input body",
                edge,
                "refresh the edge reference from current topology provenance"));
        }
    }

    BodyData result_data;
    result_data.summary = source->summary;
    result_data.summary.face_count += request.edges.size();
    result_data.summary.edge_count += request.edges.size() * 2;
    result_data.summary.volume -= request.radius * request.radius
        * static_cast<double>(request.edges.size());
    result_data.summary.topology_signature = "fillet:" + source->summary.topology_signature;
    result_data.summary.content_hash = stable_hash("fillet", result_data.summary);
    const auto handle = ExactBodyHandle{next_handle_++};
    auto provenance = TopologyProvenance{};
    for (std::size_t index = 0; index < request.edges.size(); ++index) {
        const auto role = "fillet.face." + std::to_string(index);
        const auto generated = add_entity(result_data, handle, role, TopologyKind::face);
        provenance.generated.push_back(GeneratedTopology{
            .semantic_role = role,
            .result = generated,
        });
        const auto modified = add_entity(
            result_data,
            handle,
            "fillet.modified.edge." + std::to_string(index),
            TopologyKind::edge);
        provenance.modified.push_back(ModifiedTopology{
            .source = request.edges[index],
            .result = modified,
        });
    }
    const auto source_cap = source->entities.find("extrude.cap.end");
    if (source_cap != source->entities.end()) {
        const auto modified_cap = add_entity(
            result_data, handle, "fillet.modified.cap.end", TopologyKind::face);
        provenance.modified.push_back(ModifiedTopology{
            .source = source_cap->second,
            .result = modified_cap,
        });
        const auto source_frame = source->face_frames.find(source_cap->second.id.value);
        if (source_frame != source->face_frames.end()) {
            result_data.face_frames.emplace(modified_cap.id.value, source_frame->second);
        }
    }
    bodies_.emplace(handle.value, std::move(result_data));
    return OperationResult{
        .body = handle,
        .provenance = std::move(provenance),
        .diagnostics = {},
    };
}

OperationResult FakeGeometryKernel::linear_pattern(
    const LinearPatternRequest& request,
    const CancellationToken& cancellation)
{
    if (cancellation.is_cancellation_requested()) {
        return cancelled_operation();
    }
    const auto* source = body(request.body);
    if (source == nullptr) {
        return operation_failure(diagnostic(
            GeometryErrorCode::invalid_handle, "pattern body handle is not live"));
    }
    const auto direction_magnitude = magnitude(request.direction);
    if (!finite(request.spacing) || request.spacing <= tolerance
        || !finite(request.direction) || direction_magnitude <= tolerance
        || request.instance_count < 2) {
        return operation_failure(diagnostic(
            GeometryErrorCode::invalid_input,
            "linear pattern requires a direction, positive spacing, and at least two instances"));
    }

    const auto scale = request.spacing
        * static_cast<double>(request.instance_count - 1) / direction_magnitude;
    const auto offset = Vector3d{
        request.direction.x * scale,
        request.direction.y * scale,
        request.direction.z * scale,
    };
    BodyData result_data;
    result_data.summary = source->summary;
    result_data.summary.solid_count = request.fuse ? 1 : request.instance_count;
    result_data.summary.face_count *= request.instance_count;
    result_data.summary.edge_count *= request.instance_count;
    result_data.summary.vertex_count *= request.instance_count;
    result_data.summary.volume *= static_cast<double>(request.instance_count);
    result_data.summary.bounds.minimum.x += std::min(0.0, offset.x);
    result_data.summary.bounds.minimum.y += std::min(0.0, offset.y);
    result_data.summary.bounds.minimum.z += std::min(0.0, offset.z);
    result_data.summary.bounds.maximum.x += std::max(0.0, offset.x);
    result_data.summary.bounds.maximum.y += std::max(0.0, offset.y);
    result_data.summary.bounds.maximum.z += std::max(0.0, offset.z);
    result_data.summary.topology_signature = "pattern:"
        + std::to_string(request.instance_count) + ':' + source->summary.topology_signature;
    result_data.summary.content_hash = stable_hash("pattern", result_data.summary);
    const auto handle = ExactBodyHandle{next_handle_++};
    auto provenance = TopologyProvenance{};
    for (std::uint32_t index = 0; index < request.instance_count; ++index) {
        const auto role = "pattern.instance." + std::to_string(index);
        provenance.generated.push_back(GeneratedTopology{
            .semantic_role = role,
            .result = add_entity(result_data, handle, role, TopologyKind::solid),
        });
    }
    bodies_.emplace(handle.value, std::move(result_data));
    return OperationResult{
        .body = handle,
        .provenance = std::move(provenance),
        .diagnostics = {},
    };
}

OperationResult FakeGeometryKernel::heal(
    const HealRequest& request,
    const CancellationToken& cancellation)
{
    if (cancellation.is_cancellation_requested()) {
        return cancelled_operation();
    }
    const auto* source = body(request.body);
    if (source == nullptr) {
        return operation_failure(diagnostic(
            GeometryErrorCode::invalid_handle, "healing body handle is not live"));
    }
    if (!finite(request.tolerance) || !finite(request.maximum_tolerance)
        || request.tolerance <= 0.0
        || request.maximum_tolerance < request.tolerance) {
        return operation_failure(diagnostic(
            GeometryErrorCode::invalid_input,
            "healing tolerances must be positive and ordered"));
    }

    const auto handle = ExactBodyHandle{next_handle_++};
    BodyData healed{
        .summary = source->summary,
        .entities = {},
        .face_frames = {},
    };
    bodies_.emplace(handle.value, std::move(healed));
    return OperationResult{
        .body = handle,
        .provenance = {},
        .diagnostics = {},
    };
}

PlanarFaceResult FakeGeometryKernel::planar_face(
    const PlanarFaceRequest& request,
    const CancellationToken& cancellation) const
{
    if (cancellation.is_cancellation_requested()) {
        return face_failure(diagnostic(
            GeometryErrorCode::cancelled, "planar-face query was cancelled"));
    }
    const auto* source = body(request.face.body);
    if (source == nullptr) {
        return face_failure(diagnostic(
            GeometryErrorCode::invalid_handle, "face body handle is not live"));
    }
    const auto found = source->face_frames.find(request.face.id.value);
    if (request.face.kind != TopologyKind::face || found == source->face_frames.end()) {
        return face_failure(diagnostic(
            GeometryErrorCode::topology_reference_missing,
            "topology reference is not a known planar face",
            request.face));
    }
    return PlanarFaceResult{
        .frame = found->second,
        .diagnostics = {},
    };
}

BodySummaryResult FakeGeometryKernel::summarize(
    const ExactBodyHandle handle,
    const CancellationToken& cancellation) const
{
    if (cancellation.is_cancellation_requested()) {
        return summary_failure(diagnostic(
            GeometryErrorCode::cancelled, "body summary was cancelled"));
    }
    const auto* stored = body(handle);
    if (stored == nullptr) {
        return summary_failure(diagnostic(
            GeometryErrorCode::invalid_handle, "body handle is not live"));
    }
    return BodySummaryResult{
        .summary = stored->summary,
        .diagnostics = {},
    };
}

TopologyInspectionResult FakeGeometryKernel::inspect_topology(
    const ExactBodyHandle handle,
    const CancellationToken& cancellation) const
{
    if (cancellation.is_cancellation_requested()) {
        return TopologyInspectionResult{
            .descriptors = {},
            .diagnostics = {diagnostic(
                GeometryErrorCode::cancelled, "topology inspection was cancelled")},
        };
    }
    const auto* stored = body(handle);
    if (stored == nullptr) {
        return TopologyInspectionResult{
            .descriptors = {},
            .diagnostics = {diagnostic(
                GeometryErrorCode::invalid_handle, "body handle is not live")},
        };
    }

    const auto& bounds = stored->summary.bounds;
    const auto extent_x = bounds.maximum.x - bounds.minimum.x;
    const auto extent_y = bounds.maximum.y - bounds.minimum.y;
    const auto extent_z = bounds.maximum.z - bounds.minimum.z;
    const auto center = Point3d{
        (bounds.minimum.x + bounds.maximum.x) * 0.5,
        (bounds.minimum.y + bounds.maximum.y) * 0.5,
        (bounds.minimum.z + bounds.maximum.z) * 0.5,
    };

    TopologyInspectionResult result;
    result.descriptors.reserve(stored->entities.size());
    for (const auto& [role, entity] : stored->entities) {
        TopologyDescriptor descriptor{
            .entity = entity,
            .geometry_class = GeometryClass::other,
            .measure = 0.0,
            .centroid = center,
            .orientation = std::nullopt,
            .parameter_bounds = {},
            .adjacent_vertex_count = 0,
            .adjacent_edge_count = 0,
            .adjacent_face_count = 0,
        };
        switch (entity.kind) {
        case TopologyKind::vertex:
            descriptor.geometry_class = GeometryClass::point;
            descriptor.adjacent_edge_count = 3;
            descriptor.adjacent_face_count = 3;
            break;
        case TopologyKind::edge:
            descriptor.geometry_class = GeometryClass::line;
            descriptor.measure = std::max({extent_x, extent_y, extent_z});
            descriptor.orientation = Vector3d{1.0, 0.0, 0.0};
            descriptor.parameter_bounds[1] = descriptor.measure;
            descriptor.adjacent_vertex_count = 2;
            descriptor.adjacent_edge_count = 1;
            descriptor.adjacent_face_count = 2;
            break;
        case TopologyKind::face:
            descriptor.geometry_class = GeometryClass::plane;
            descriptor.measure = std::max(
                {extent_x * extent_y, extent_x * extent_z, extent_y * extent_z});
            descriptor.orientation = Vector3d{0.0, 0.0, 1.0};
            descriptor.adjacent_vertex_count = 4;
            descriptor.adjacent_edge_count = 4;
            descriptor.adjacent_face_count = 4;
            if (role.find("cap.start") != std::string::npos) {
                descriptor.centroid.z = bounds.minimum.z;
                descriptor.orientation = Vector3d{0.0, 0.0, -1.0};
                descriptor.measure = extent_x * extent_y;
            } else if (role.find("cap.end") != std::string::npos) {
                descriptor.centroid.z = bounds.maximum.z;
                descriptor.measure = extent_x * extent_y;
            }
            break;
        case TopologyKind::shell:
            descriptor.measure = 2.0
                * (extent_x * extent_y + extent_x * extent_z + extent_y * extent_z);
            descriptor.adjacent_face_count = static_cast<std::uint32_t>(
                stored->summary.face_count);
            break;
        case TopologyKind::solid:
            descriptor.measure = stored->summary.volume;
            descriptor.adjacent_face_count = static_cast<std::uint32_t>(
                stored->summary.face_count);
            break;
        }
        result.descriptors.push_back(std::move(descriptor));
    }
    return result;
}

BodySnapshotResult FakeGeometryKernel::snapshot(
    const ExactBodyHandle handle,
    const CancellationToken& cancellation) const
{
    if (cancellation.is_cancellation_requested()) {
        return snapshot_failure(diagnostic(
            GeometryErrorCode::cancelled, "body snapshot was cancelled"));
    }
    const auto* stored = body(handle);
    if (stored == nullptr) {
        return snapshot_failure(diagnostic(
            GeometryErrorCode::invalid_handle, "body handle is not live"));
    }

    const auto& summary = stored->summary;
    std::ostringstream encoded;
    encoded << std::setprecision(17) << summary.solid_count << ' ' << summary.face_count
            << ' ' << summary.edge_count << ' ' << summary.vertex_count << ' '
            << summary.volume << ' ' << summary.bounds.minimum.x << ' '
            << summary.bounds.minimum.y << ' ' << summary.bounds.minimum.z << ' '
            << summary.bounds.maximum.x << ' ' << summary.bounds.maximum.y << ' '
            << summary.bounds.maximum.z << ' ' << std::quoted(summary.topology_signature)
            << ' ' << std::quoted(summary.content_hash);
    const auto text = encoded.str();
    std::vector<std::byte> payload;
    payload.reserve(text.size());
    for (const auto character : text) {
        payload.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return BodySnapshotResult{
        .snapshot = ExactBodySnapshot{
            .format = "oep-fake-summary-v1",
            .kernel_version = "1",
            .content_hash = summary.content_hash,
            .payload = std::move(payload),
        },
        .diagnostics = {},
    };
}

OperationResult FakeGeometryKernel::restore_snapshot(
    const ExactBodySnapshot& snapshot,
    const CancellationToken& cancellation)
{
    if (cancellation.is_cancellation_requested()) {
        return cancelled_operation();
    }
    if (snapshot.format != "oep-fake-summary-v1" || snapshot.kernel_version != "1") {
        return operation_failure(diagnostic(
            GeometryErrorCode::unsupported_operation,
            "snapshot format does not belong to the fake adapter"));
    }
    std::string text;
    text.reserve(snapshot.payload.size());
    for (const auto byte : snapshot.payload) {
        text.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
    }
    BodySummary summary;
    std::istringstream encoded(text);
    encoded >> summary.solid_count >> summary.face_count >> summary.edge_count
        >> summary.vertex_count >> summary.volume >> summary.bounds.minimum.x
        >> summary.bounds.minimum.y >> summary.bounds.minimum.z
        >> summary.bounds.maximum.x >> summary.bounds.maximum.y
        >> summary.bounds.maximum.z >> std::quoted(summary.topology_signature)
        >> std::quoted(summary.content_hash);
    if (!encoded || summary.content_hash != snapshot.content_hash) {
        return operation_failure(diagnostic(
            GeometryErrorCode::invalid_input,
            "snapshot payload is malformed or does not match its content hash"));
    }

    const auto handle = ExactBodyHandle{next_handle_++};
    bodies_.emplace(
        handle.value,
        BodyData{.summary = std::move(summary), .entities = {}, .face_frames = {}});
    return OperationResult{
        .body = handle,
        .provenance = {},
        .diagnostics = {},
    };
}

const FakeGeometryKernel::BodyData* FakeGeometryKernel::body(
    const ExactBodyHandle handle) const
{
    const auto found = bodies_.find(handle.value);
    return found == bodies_.end() ? nullptr : &found->second;
}

} // namespace oep::test
