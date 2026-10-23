// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

// OCCT implementation of the kernel-neutral contract. Keep OCCT types inside
// this adapter so document and EM code depend on stable project types. Exact-body
// handles are local to this kernel instance; snapshots provide the restart path.

#include <oep/modeler/occt_geometry_kernel.hpp>

#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <Standard_Failure.hxx>
#include <ShapeFix_Shape.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Vertex.hxx>
#include <Poly_Triangulation.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace oep::modeler {
namespace {

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
        "operation was cancelled",
        std::nullopt,
        "retry when cancellation is no longer requested"));
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

[[nodiscard]] std::string exception_message(const Standard_Failure& error)
{
    const auto* message = error.GetMessageString();
    return message == nullptr ? "Open CASCADE reported an unspecified failure" : message;
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

[[nodiscard]] double loop_area(const ProfileLoop& loop)
{
    if (loop.vertices.size() < 3) {
        return 0.0;
    }
    auto twice_area = 0.0;
    for (std::size_t index = 0; index < loop.vertices.size(); ++index) {
        const auto& current = loop.vertices[index];
        const auto& next = loop.vertices[(index + 1) % loop.vertices.size()];
        if (!finite(current.x) || !finite(current.y)) {
            return 0.0;
        }
        twice_area += current.x * next.y - next.x * current.y;
    }
    return std::abs(twice_area) * 0.5;
}

[[nodiscard]] TopologyKind topology_kind(const TopAbs_ShapeEnum shape_type)
{
    switch (shape_type) {
    case TopAbs_VERTEX:
        return TopologyKind::vertex;
    case TopAbs_EDGE:
        return TopologyKind::edge;
    case TopAbs_FACE:
        return TopologyKind::face;
    case TopAbs_SHELL:
        return TopologyKind::shell;
    case TopAbs_SOLID:
    default:
        return TopologyKind::solid;
    }
}

[[nodiscard]] std::string topology_kind_name(const TopologyKind kind)
{
    switch (kind) {
    case TopologyKind::vertex:
        return "vertex";
    case TopologyKind::edge:
        return "edge";
    case TopologyKind::face:
        return "face";
    case TopologyKind::shell:
        return "shell";
    case TopologyKind::solid:
        return "solid";
    }
    return "unknown";
}

[[nodiscard]] GeometryClass curve_geometry_class(const GeomAbs_CurveType type)
{
    switch (type) {
    case GeomAbs_Line:
        return GeometryClass::line;
    case GeomAbs_Circle:
        return GeometryClass::circle;
    case GeomAbs_Ellipse:
        return GeometryClass::ellipse;
    case GeomAbs_BezierCurve:
        return GeometryClass::bezier_curve;
    case GeomAbs_BSplineCurve:
        return GeometryClass::bspline_curve;
    default:
        return GeometryClass::other;
    }
}

[[nodiscard]] GeometryClass surface_geometry_class(const GeomAbs_SurfaceType type)
{
    switch (type) {
    case GeomAbs_Plane:
        return GeometryClass::plane;
    case GeomAbs_Cylinder:
        return GeometryClass::cylinder;
    case GeomAbs_Cone:
        return GeometryClass::cone;
    case GeomAbs_Sphere:
        return GeometryClass::sphere;
    case GeomAbs_Torus:
        return GeometryClass::torus;
    case GeomAbs_BezierSurface:
        return GeometryClass::bezier_surface;
    case GeomAbs_BSplineSurface:
        return GeometryClass::bspline_surface;
    default:
        return GeometryClass::other;
    }
}

[[nodiscard]] Vector3d vector3d(const gp_Dir& direction)
{
    return Vector3d{direction.X(), direction.Y(), direction.Z()};
}

[[nodiscard]] std::optional<Vector3d> topology_orientation(const TopoDS_Shape& shape)
{
    if (shape.ShapeType() == TopAbs_EDGE) {
        const BRepAdaptor_Curve curve(TopoDS::Edge(shape));
        switch (curve.GetType()) {
        case GeomAbs_Line:
            return vector3d(curve.Line().Direction());
        case GeomAbs_Circle:
            return vector3d(curve.Circle().Axis().Direction());
        case GeomAbs_Ellipse:
            return vector3d(curve.Ellipse().Axis().Direction());
        default:
            return std::nullopt;
        }
    }
    if (shape.ShapeType() == TopAbs_FACE) {
        const auto face = TopoDS::Face(shape);
        const BRepAdaptor_Surface surface(face, Standard_False);
        gp_Dir direction;
        switch (surface.GetType()) {
        case GeomAbs_Plane:
            direction = surface.Plane().Axis().Direction();
            break;
        case GeomAbs_Cylinder:
            direction = surface.Cylinder().Axis().Direction();
            break;
        case GeomAbs_Cone:
            direction = surface.Cone().Axis().Direction();
            break;
        case GeomAbs_Sphere:
            direction = surface.Sphere().Position().Direction();
            break;
        case GeomAbs_Torus:
            direction = surface.Torus().Position().Direction();
            break;
        default:
            return std::nullopt;
        }
        if (face.Orientation() == TopAbs_REVERSED) {
            direction.Reverse();
        }
        return vector3d(direction);
    }
    return std::nullopt;
}

[[nodiscard]] std::uint64_t topology_count(
    const TopoDS_Shape& shape,
    const TopAbs_ShapeEnum shape_type)
{
    TopTools_IndexedMapOfShape shapes;
    TopExp::MapShapes(shape, shape_type, shapes);
    return static_cast<std::uint64_t>(shapes.Extent());
}

[[nodiscard]] BodySummary summarize_shape(const TopoDS_Shape& shape)
{
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);

    Bnd_Box bounds;
    BRepBndLib::AddOptimal(shape, bounds, Standard_False, Standard_False);
    Standard_Real minimum_x = 0.0;
    Standard_Real minimum_y = 0.0;
    Standard_Real minimum_z = 0.0;
    Standard_Real maximum_x = 0.0;
    Standard_Real maximum_y = 0.0;
    Standard_Real maximum_z = 0.0;
    bounds.Get(
        minimum_x,
        minimum_y,
        minimum_z,
        maximum_x,
        maximum_y,
        maximum_z);

    BodySummary summary{
        .solid_count = topology_count(shape, TopAbs_SOLID),
        .face_count = topology_count(shape, TopAbs_FACE),
        .edge_count = topology_count(shape, TopAbs_EDGE),
        .vertex_count = topology_count(shape, TopAbs_VERTEX),
        .volume = std::abs(properties.Mass()),
        .bounds = BoundingBox{
            .minimum = Point3d{minimum_x, minimum_y, minimum_z},
            .maximum = Point3d{maximum_x, maximum_y, maximum_z},
        },
        .topology_signature = {},
        .content_hash = {},
    };
    summary.topology_signature = "solid" + std::to_string(summary.solid_count)
        + ":face" + std::to_string(summary.face_count)
        + ":edge" + std::to_string(summary.edge_count)
        + ":vertex" + std::to_string(summary.vertex_count);

    std::ostringstream hash;
    hash << std::setprecision(17) << summary.topology_signature << ':' << summary.volume
         << ':' << summary.bounds.minimum.x << ':' << summary.bounds.minimum.y << ':'
         << summary.bounds.minimum.z << ':' << summary.bounds.maximum.x << ':'
         << summary.bounds.maximum.y << ':' << summary.bounds.maximum.z;
    summary.content_hash = hash.str();
    return summary;
}

[[nodiscard]] bool same_shape(const TopoDS_Shape& left, const TopoDS_Shape& right)
{
    return !left.IsNull() && !right.IsNull() && left.IsSame(right);
}

[[nodiscard]] bool contains_subshape(
    const TopoDS_Shape& container,
    const TopoDS_Shape& candidate)
{
    TopTools_IndexedMapOfShape subshapes;
    TopExp::MapShapes(container, candidate.ShapeType(), subshapes);
    for (Standard_Integer index = 1; index <= subshapes.Extent(); ++index) {
        if (same_shape(subshapes(index), candidate)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool shares_subshape(
    const TopoDS_Shape& left,
    const TopoDS_Shape& right,
    const TopAbs_ShapeEnum shape_type)
{
    TopTools_IndexedMapOfShape left_subshapes;
    TopTools_IndexedMapOfShape right_subshapes;
    TopExp::MapShapes(left, shape_type, left_subshapes);
    TopExp::MapShapes(right, shape_type, right_subshapes);
    for (Standard_Integer left_index = 1;
         left_index <= left_subshapes.Extent();
         ++left_index) {
        for (Standard_Integer right_index = 1;
             right_index <= right_subshapes.Extent();
             ++right_index) {
            if (same_shape(
                    left_subshapes(left_index), right_subshapes(right_index))) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

struct OcctGeometryKernel::Impl {
    struct EntityShape {
        TopologyEntity entity;
        TopoDS_Shape shape;
    };

    struct StoredBody {
        TopoDS_Shape shape;
        std::vector<EntityShape> entities;
        std::map<std::string, PlaneFrame, std::less<>> semantic_planes;
    };

    [[nodiscard]] const StoredBody* body(const ExactBodyHandle handle) const
    {
        const auto found = bodies.find(handle.value);
        return found == bodies.end() ? nullptr : &found->second;
    }

    [[nodiscard]] StoredBody* mutable_body(const ExactBodyHandle handle)
    {
        const auto found = bodies.find(handle.value);
        return found == bodies.end() ? nullptr : &found->second;
    }

    [[nodiscard]] const EntityShape* entity(const TopologyEntity& reference) const
    {
        const auto* stored = body(reference.body);
        if (stored == nullptr) {
            return nullptr;
        }
        const auto found = std::ranges::find_if(
            stored->entities,
            [&reference](const EntityShape& item) { return item.entity == reference; });
        return found == stored->entities.end() ? nullptr : &*found;
    }

    [[nodiscard]] const TopologyEntity* entity_for_shape(
        const ExactBodyHandle handle,
        const TopoDS_Shape& shape) const
    {
        const auto* stored = body(handle);
        if (stored == nullptr) {
            return nullptr;
        }
        const auto found = std::ranges::find_if(
            stored->entities,
            [&shape](const EntityShape& item) { return same_shape(item.shape, shape); });
        return found == stored->entities.end() ? nullptr : &found->entity;
    }

    [[nodiscard]] ExactBodyHandle store(const TopoDS_Shape& shape)
    {
        const auto handle = ExactBodyHandle{next_handle++};
        StoredBody stored{.shape = shape, .entities = {}, .semantic_planes = {}};
        constexpr std::array shape_types{
            TopAbs_SOLID,
            TopAbs_SHELL,
            TopAbs_FACE,
            TopAbs_EDGE,
            TopAbs_VERTEX,
        };
        for (const auto shape_type : shape_types) {
            TopTools_IndexedMapOfShape subshapes;
            TopExp::MapShapes(shape, shape_type, subshapes);
            const auto kind = topology_kind(shape_type);
            for (Standard_Integer index = 1; index <= subshapes.Extent(); ++index) {
                stored.entities.push_back(EntityShape{
                    .entity = TopologyEntity{
                        .body = handle,
                        .id = KernelTopologyId{
                            std::to_string(handle.value) + ':' + topology_kind_name(kind)
                            + ':' + std::to_string(index - 1)},
                        .kind = kind,
                    },
                    .shape = subshapes(index),
                });
            }
        }
        bodies.emplace(handle.value, std::move(stored));
        return handle;
    }

    template <typename Builder>
    void append_history(
        Builder& builder,
        const StoredBody& source,
        const ExactBodyHandle result_handle,
        const std::string_view operation,
        TopologyProvenance& provenance) const
    {
        auto generated_index = provenance.generated.size();
        for (const auto& source_entity : source.entities) {
            if (builder.IsDeleted(source_entity.shape)) {
                provenance.deleted.push_back(source_entity.entity);
            }
            const auto& modified = builder.Modified(source_entity.shape);
            for (TopTools_ListIteratorOfListOfShape iterator(modified); iterator.More();
                 iterator.Next()) {
                if (const auto* result = entity_for_shape(result_handle, iterator.Value())) {
                    provenance.modified.push_back(ModifiedTopology{
                        .source = source_entity.entity,
                        .result = *result,
                    });
                }
            }
            const auto& generated = builder.Generated(source_entity.shape);
            for (TopTools_ListIteratorOfListOfShape iterator(generated); iterator.More();
                 iterator.Next()) {
                if (const auto* result = entity_for_shape(result_handle, iterator.Value())) {
                    provenance.generated.push_back(GeneratedTopology{
                        .semantic_role = std::string(operation) + ".generated."
                            + std::to_string(generated_index++),
                        .result = *result,
                    });
                }
            }
        }
    }

    std::map<std::uint64_t, StoredBody> bodies;
    std::uint64_t next_handle{1};
};

OcctGeometryKernel::OcctGeometryKernel()
    : impl_(std::make_unique<Impl>())
{
}

OcctGeometryKernel::~OcctGeometryKernel() = default;
OcctGeometryKernel::OcctGeometryKernel(OcctGeometryKernel&&) noexcept = default;
OcctGeometryKernel& OcctGeometryKernel::operator=(OcctGeometryKernel&&) noexcept = default;

KernelIdentity OcctGeometryKernel::identity() const
{
    return KernelIdentity{
        .name = "Open CASCADE Technology",
        .version = OEP_OCCT_VERSION,
        .provides_exact_brep = true,
    };
}

OperationResult OcctGeometryKernel::extrude(
    const ExtrudeRequest& request,
    const CancellationToken& cancellation)
{
    if (cancellation.is_cancellation_requested()) {
        return cancelled_operation();
    }
    if (!finite(request.distance) || request.distance <= tolerance
        || request.profile.loops.empty() || !finite(request.profile.frame.origin)
        || !finite(request.profile.frame.normal)
        || !finite(request.profile.frame.x_direction)
        || magnitude(request.profile.frame.normal) <= tolerance
        || magnitude(request.profile.frame.x_direction) <= tolerance) {
        return operation_failure(diagnostic(
            GeometryErrorCode::invalid_input,
            "extrusion requires a finite frame, closed profile, and positive distance"));
    }

    const auto outer_count = std::ranges::count_if(
        request.profile.loops,
        [](const ProfileLoop& loop) { return !loop.is_hole; });
    if (outer_count != 1
        || std::ranges::any_of(
            request.profile.loops,
            [](const ProfileLoop& loop) {
                return loop.vertices.size() < 3 || loop_area(loop) <= tolerance;
            })) {
        return operation_failure(diagnostic(
            GeometryErrorCode::degenerate_geometry,
            "profile requires one nondegenerate outer loop and optional nondegenerate holes",
            std::nullopt,
            "remove coincident points or increase the profile area"));
    }

    try {
        const auto& frame = request.profile.frame;
        const gp_Dir normal(frame.normal.x, frame.normal.y, frame.normal.z);
        const gp_Dir x_direction(
            frame.x_direction.x, frame.x_direction.y, frame.x_direction.z);
        if (std::abs(normal.Dot(x_direction)) > tolerance) {
            return operation_failure(diagnostic(
                GeometryErrorCode::invalid_input,
                "profile normal and X direction must be perpendicular"));
        }
        const gp_Vec x_vector(x_direction);
        const gp_Vec y_vector = gp_Vec(normal).Crossed(x_vector);
        const gp_Pnt origin(frame.origin.x, frame.origin.y, frame.origin.z);

        const auto make_wire = [&](const ProfileLoop& loop) {
            BRepBuilderAPI_MakePolygon polygon;
            for (const auto& point : loop.vertices) {
                polygon.Add(origin.Translated(x_vector * point.x + y_vector * point.y));
            }
            polygon.Close();
            if (!polygon.IsDone()) {
                throw std::runtime_error("profile wire construction failed");
            }
            return polygon.Wire();
        };

        const auto outer = std::ranges::find_if(
            request.profile.loops,
            [](const ProfileLoop& loop) { return !loop.is_hole; });
        const auto outer_wire = make_wire(*outer);
        BRepBuilderAPI_MakeFace face_builder(outer_wire, Standard_True);
        if (!face_builder.IsDone()) {
            return operation_failure(diagnostic(
                GeometryErrorCode::degenerate_geometry,
                "profile could not form a planar face"));
        }
        for (const auto& loop : request.profile.loops) {
            if (loop.is_hole) {
                face_builder.Add(make_wire(loop));
            }
        }

        const auto face = face_builder.Face();
        BRepPrimAPI_MakePrism prism(face, gp_Vec(normal) * request.distance, Standard_False);
        prism.Build();
        if (!prism.IsDone() || prism.Shape().IsNull()) {
            return operation_failure(diagnostic(
                GeometryErrorCode::kernel_failure,
                "Open CASCADE could not build the extrusion"));
        }
        if (cancellation.is_cancellation_requested()) {
            return cancelled_operation();
        }

        const auto handle = impl_->store(prism.Shape());
        TopologyProvenance provenance;
        const auto first_shape = prism.FirstShape();
        const auto last_shape = prism.LastShape();
        if (const auto* first = impl_->entity_for_shape(handle, first_shape)) {
            provenance.generated.push_back(GeneratedTopology{
                .semantic_role = "extrude.cap.start",
                .result = *first,
            });
            impl_->mutable_body(handle)->semantic_planes.emplace(
                first->id.value,
                PlaneFrame{
                    .origin = frame.origin,
                    .normal = Vector3d{
                        -normal.X(), -normal.Y(), -normal.Z()},
                    .x_direction = frame.x_direction,
                });
        }
        if (const auto* last = impl_->entity_for_shape(handle, last_shape)) {
            provenance.generated.push_back(GeneratedTopology{
                .semantic_role = "extrude.cap.end",
                .result = *last,
            });
            impl_->mutable_body(handle)->semantic_planes.emplace(
                last->id.value,
                PlaneFrame{
                    .origin = Point3d{
                        frame.origin.x + normal.X() * request.distance,
                        frame.origin.y + normal.Y() * request.distance,
                        frame.origin.z + normal.Z() * request.distance,
                    },
                    .normal = Vector3d{normal.X(), normal.Y(), normal.Z()},
                    .x_direction = frame.x_direction,
                });
        }
        const auto* stored = impl_->body(handle);
        auto side_index = std::size_t{0};
        auto edge_index = std::size_t{0};
        for (const auto& item : stored->entities) {
            if (item.entity.kind == TopologyKind::face
                && !same_shape(item.shape, first_shape)
                && !same_shape(item.shape, last_shape)) {
                provenance.generated.push_back(GeneratedTopology{
                    .semantic_role = "extrude.side." + std::to_string(side_index++),
                    .result = item.entity,
                });
            } else if (item.entity.kind == TopologyKind::edge) {
                provenance.generated.push_back(GeneratedTopology{
                    .semantic_role = "extrude.edge." + std::to_string(edge_index++),
                    .result = item.entity,
                });
            }
        }
        return OperationResult{
            .body = handle,
            .provenance = std::move(provenance),
            .diagnostics = {},
        };
    } catch (const Standard_Failure& error) {
        return operation_failure(diagnostic(
            GeometryErrorCode::kernel_failure, exception_message(error)));
    } catch (const std::exception& error) {
        return operation_failure(diagnostic(
            GeometryErrorCode::degenerate_geometry, error.what()));
    }
}

OperationResult OcctGeometryKernel::cut(
    const BooleanCutRequest& request,
    const CancellationToken& cancellation)
{
    if (cancellation.is_cancellation_requested()) {
        return cancelled_operation();
    }
    const auto* target = impl_->body(request.target);
    const auto* tool = impl_->body(request.tool);
    if (target == nullptr || tool == nullptr) {
        return operation_failure(diagnostic(
            GeometryErrorCode::invalid_handle,
            "boolean cut requires live target and tool handles"));
    }
    try {
        BRepAlgoAPI_Cut builder(target->shape, tool->shape);
        builder.Build();
        if (!builder.IsDone() || builder.Shape().IsNull()) {
            return operation_failure(diagnostic(
                GeometryErrorCode::kernel_failure,
                "Open CASCADE boolean cut failed",
                std::nullopt,
                "inspect body validity and avoid tangent or coincident cut boundaries"));
        }
        const auto summary = summarize_shape(builder.Shape());
        if (summary.solid_count == 0 || summary.volume <= tolerance
            || std::abs(summary.volume - summarize_shape(target->shape).volume) <= tolerance) {
            return operation_failure(diagnostic(
                GeometryErrorCode::degenerate_geometry,
                "boolean tool did not leave a proper changed solid",
                std::nullopt,
                "move or resize the tool so it removes a proper portion of the target"));
        }
        if (cancellation.is_cancellation_requested()) {
            return cancelled_operation();
        }
        const auto handle = impl_->store(builder.Shape());
        TopologyProvenance provenance;
        impl_->append_history(builder, *target, handle, "cut", provenance);
        impl_->append_history(builder, *tool, handle, "cut", provenance);
        if (provenance.generated.empty()) {
            const auto* result = impl_->body(handle);
            const auto generated = std::ranges::find_if(
                result->entities,
                [](const Impl::EntityShape& item) {
                    return item.entity.kind == TopologyKind::face;
                });
            if (generated != result->entities.end()) {
                provenance.generated.push_back(GeneratedTopology{
                    .semantic_role = "cut.generated.face.0",
                    .result = generated->entity,
                });
            }
        }
        return OperationResult{
            .body = handle,
            .provenance = std::move(provenance),
            .diagnostics = {},
        };
    } catch (const Standard_Failure& error) {
        return operation_failure(diagnostic(
            GeometryErrorCode::kernel_failure, exception_message(error)));
    }
}

OperationResult OcctGeometryKernel::fillet(
    const FilletRequest& request,
    const CancellationToken& cancellation)
{
    if (cancellation.is_cancellation_requested()) {
        return cancelled_operation();
    }
    const auto* source = impl_->body(request.body);
    if (source == nullptr) {
        return operation_failure(diagnostic(
            GeometryErrorCode::invalid_handle, "fillet body handle is not live"));
    }
    if (request.edges.empty() || !finite(request.radius) || request.radius <= tolerance) {
        return operation_failure(diagnostic(
            GeometryErrorCode::invalid_input,
            "fillet requires at least one edge and a positive finite radius"));
    }
    const auto source_summary = summarize_shape(source->shape);
    const auto minimum_extent = std::min({
        source_summary.bounds.maximum.x - source_summary.bounds.minimum.x,
        source_summary.bounds.maximum.y - source_summary.bounds.minimum.y,
        source_summary.bounds.maximum.z - source_summary.bounds.minimum.z,
    });
    if (request.radius >= minimum_extent * 0.5) {
        return operation_failure(diagnostic(
            GeometryErrorCode::degenerate_geometry,
            "fillet radius is too large for the body extent",
            std::nullopt,
            "use a radius below half the smallest body extent"));
    }
    try {
        BRepFilletAPI_MakeFillet builder(source->shape);
        for (const auto& edge : request.edges) {
            const auto* stored_edge = impl_->entity(edge);
            if (stored_edge == nullptr || edge.body != request.body
                || edge.kind != TopologyKind::edge) {
                return operation_failure(diagnostic(
                    GeometryErrorCode::topology_reference_missing,
                    "fillet edge does not belong to the input body",
                    edge,
                    "refresh the edge reference from current topology provenance"));
            }
            builder.Add(request.radius, TopoDS::Edge(stored_edge->shape));
        }
        builder.Build();
        if (!builder.IsDone() || builder.Shape().IsNull()) {
            return operation_failure(diagnostic(
                GeometryErrorCode::degenerate_geometry,
                "Open CASCADE could not construct the requested fillet",
                std::nullopt,
                "reduce the radius or select a different edge set"));
        }
        if (cancellation.is_cancellation_requested()) {
            return cancelled_operation();
        }
        const auto handle = impl_->store(builder.Shape());
        TopologyProvenance provenance;
        impl_->append_history(builder, *source, handle, "fillet", provenance);
        return OperationResult{
            .body = handle,
            .provenance = std::move(provenance),
            .diagnostics = {},
        };
    } catch (const Standard_Failure& error) {
        return operation_failure(diagnostic(
            GeometryErrorCode::kernel_failure, exception_message(error)));
    }
}

OperationResult OcctGeometryKernel::linear_pattern(
    const LinearPatternRequest& request,
    const CancellationToken& cancellation)
{
    if (cancellation.is_cancellation_requested()) {
        return cancelled_operation();
    }
    const auto* source = impl_->body(request.body);
    const auto direction_magnitude = magnitude(request.direction);
    if (source == nullptr) {
        return operation_failure(diagnostic(
            GeometryErrorCode::invalid_handle, "pattern body handle is not live"));
    }
    if (request.instance_count < 2 || !finite(request.spacing)
        || request.spacing <= tolerance || !finite(request.direction)
        || direction_magnitude <= tolerance) {
        return operation_failure(diagnostic(
            GeometryErrorCode::invalid_input,
            "linear pattern requires a direction, positive spacing, and at least two instances"));
    }

    try {
        std::vector<TopoDS_Shape> instances;
        instances.reserve(request.instance_count);
        instances.push_back(source->shape);
        const gp_Vec unit_direction(
            request.direction.x / direction_magnitude,
            request.direction.y / direction_magnitude,
            request.direction.z / direction_magnitude);
        for (std::uint32_t index = 1; index < request.instance_count; ++index) {
            gp_Trsf transform;
            transform.SetTranslation(
                unit_direction * (request.spacing * static_cast<double>(index)));
            BRepBuilderAPI_Transform transformer(source->shape, transform, Standard_True);
            transformer.Build();
            if (!transformer.IsDone()) {
                return operation_failure(diagnostic(
                    GeometryErrorCode::kernel_failure,
                    "Open CASCADE could not transform a pattern instance"));
            }
            instances.push_back(transformer.Shape());
        }

        TopoDS_Shape pattern_shape;
        if (request.fuse) {
            pattern_shape = instances.front();
            for (std::size_t index = 1; index < instances.size(); ++index) {
                BRepAlgoAPI_Fuse fuse(pattern_shape, instances[index]);
                fuse.Build();
                if (!fuse.IsDone()) {
                    return operation_failure(diagnostic(
                        GeometryErrorCode::kernel_failure,
                        "Open CASCADE could not fuse pattern instances"));
                }
                pattern_shape = fuse.Shape();
            }
        } else {
            TopoDS_Compound compound;
            BRep_Builder compound_builder;
            compound_builder.MakeCompound(compound);
            for (const auto& instance : instances) {
                compound_builder.Add(compound, instance);
            }
            pattern_shape = compound;
        }
        if (cancellation.is_cancellation_requested()) {
            return cancelled_operation();
        }
        const auto handle = impl_->store(pattern_shape);
        TopologyProvenance provenance;
        for (std::size_t index = 0; index < instances.size(); ++index) {
            if (const auto* entity = impl_->entity_for_shape(handle, instances[index])) {
                provenance.generated.push_back(GeneratedTopology{
                    .semantic_role = "pattern.instance." + std::to_string(index),
                    .result = *entity,
                });
            }
        }
        return OperationResult{
            .body = handle,
            .provenance = std::move(provenance),
            .diagnostics = {},
        };
    } catch (const Standard_Failure& error) {
        return operation_failure(diagnostic(
            GeometryErrorCode::kernel_failure, exception_message(error)));
    }
}

OperationResult OcctGeometryKernel::heal(
    const HealRequest& request,
    const CancellationToken& cancellation)
{
    if (cancellation.is_cancellation_requested()) {
        return cancelled_operation();
    }
    const auto* source = impl_->body(request.body);
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

    try {
        ShapeFix_Shape healer(source->shape);
        healer.SetPrecision(request.tolerance);
        healer.SetMaxTolerance(request.maximum_tolerance);
        static_cast<void>(healer.Perform());
        const auto healed_shape = healer.Shape();
        if (healed_shape.IsNull() || !BRepCheck_Analyzer(healed_shape).IsValid()) {
            return operation_failure(diagnostic(
                GeometryErrorCode::kernel_failure,
                "Open CASCADE healing did not produce a valid body",
                std::nullopt,
                "inspect invalid subshapes and adjust the healing tolerance"));
        }
        if (cancellation.is_cancellation_requested()) {
            return cancelled_operation();
        }

        const auto handle = impl_->store(healed_shape);
        TopologyProvenance provenance;
        for (const auto& source_entity : source->entities) {
            if (const auto* result = impl_->entity_for_shape(handle, source_entity.shape)) {
                provenance.modified.push_back(ModifiedTopology{
                    .source = source_entity.entity,
                    .result = *result,
                });
            }
        }
        return OperationResult{
            .body = handle,
            .provenance = std::move(provenance),
            .diagnostics = {},
        };
    } catch (const Standard_Failure& error) {
        return operation_failure(diagnostic(
            GeometryErrorCode::kernel_failure, exception_message(error)));
    }
}

PlanarFaceResult OcctGeometryKernel::planar_face(
    const PlanarFaceRequest& request,
    const CancellationToken& cancellation) const
{
    if (cancellation.is_cancellation_requested()) {
        return face_failure(diagnostic(
            GeometryErrorCode::cancelled, "planar-face query was cancelled"));
    }
    const auto* stored_face = impl_->entity(request.face);
    if (stored_face == nullptr || request.face.kind != TopologyKind::face) {
        return face_failure(diagnostic(
            GeometryErrorCode::topology_reference_missing,
            "topology reference is not a face in the current body",
            request.face));
    }
    try {
        const auto* stored_body = impl_->body(request.face.body);
        const auto semantic_frame = stored_body->semantic_planes.find(request.face.id.value);
        if (semantic_frame != stored_body->semantic_planes.end()) {
            return PlanarFaceResult{
                .frame = semantic_frame->second,
                .diagnostics = {},
            };
        }
        const auto face = TopoDS::Face(stored_face->shape);
        const BRepAdaptor_Surface surface(face, Standard_False);
        if (surface.GetType() != GeomAbs_Plane) {
            return face_failure(diagnostic(
                GeometryErrorCode::unsupported_operation,
                "selected face is not planar",
                request.face,
                "select a planar face for an attached sketch"));
        }
        const auto position = surface.Plane().Position();
        auto normal = position.Direction();
        if (face.Orientation() == TopAbs_REVERSED) {
            normal.Reverse();
        }
        const auto origin = position.Location();
        const auto x_direction = position.XDirection();
        return PlanarFaceResult{
            .frame = PlaneFrame{
                .origin = Point3d{origin.X(), origin.Y(), origin.Z()},
                .normal = Vector3d{normal.X(), normal.Y(), normal.Z()},
                .x_direction = Vector3d{
                    x_direction.X(), x_direction.Y(), x_direction.Z()},
            },
            .diagnostics = {},
        };
    } catch (const Standard_Failure& error) {
        return face_failure(diagnostic(
            GeometryErrorCode::kernel_failure,
            exception_message(error),
            request.face));
    }
}

BodySummaryResult OcctGeometryKernel::summarize(
    const ExactBodyHandle handle,
    const CancellationToken& cancellation) const
{
    if (cancellation.is_cancellation_requested()) {
        return summary_failure(diagnostic(
            GeometryErrorCode::cancelled, "body summary was cancelled"));
    }
    const auto* stored = impl_->body(handle);
    if (stored == nullptr) {
        return summary_failure(diagnostic(
            GeometryErrorCode::invalid_handle, "body handle is not live"));
    }
    try {
        return BodySummaryResult{
            .summary = summarize_shape(stored->shape),
            .diagnostics = {},
        };
    } catch (const Standard_Failure& error) {
        return summary_failure(diagnostic(
            GeometryErrorCode::kernel_failure, exception_message(error)));
    }
}

TopologyInspectionResult OcctGeometryKernel::inspect_topology(
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
    const auto* stored = impl_->body(handle);
    if (stored == nullptr) {
        return TopologyInspectionResult{
            .descriptors = {},
            .diagnostics = {diagnostic(
                GeometryErrorCode::invalid_handle, "body handle is not live")},
        };
    }

    try {
        TopologyInspectionResult result;
        result.descriptors.reserve(stored->entities.size());
        for (const auto& item : stored->entities) {
            TopologyDescriptor descriptor{
                .entity = item.entity,
                .geometry_class = GeometryClass::unknown,
                .measure = 0.0,
                .centroid = {},
                .orientation = topology_orientation(item.shape),
                .parameter_bounds = {},
                .adjacent_vertex_count = 0,
                .adjacent_edge_count = 0,
                .adjacent_face_count = 0,
            };

            descriptor.adjacent_vertex_count = static_cast<std::uint32_t>(
                topology_count(item.shape, TopAbs_VERTEX));
            descriptor.adjacent_edge_count = static_cast<std::uint32_t>(
                topology_count(item.shape, TopAbs_EDGE));
            if (item.entity.kind == TopologyKind::vertex) {
                descriptor.adjacent_edge_count = static_cast<std::uint32_t>(
                    std::ranges::count_if(
                        stored->entities,
                        [&item](const Impl::EntityShape& candidate) {
                            return candidate.entity.kind == TopologyKind::edge
                                && contains_subshape(candidate.shape, item.shape);
                        }));
                descriptor.adjacent_face_count = static_cast<std::uint32_t>(
                    std::ranges::count_if(
                        stored->entities,
                        [&item](const Impl::EntityShape& candidate) {
                            return candidate.entity.kind == TopologyKind::face
                                && contains_subshape(candidate.shape, item.shape);
                        }));
            } else if (item.entity.kind == TopologyKind::edge) {
                descriptor.adjacent_face_count = static_cast<std::uint32_t>(
                    std::ranges::count_if(
                        stored->entities,
                        [&item](const Impl::EntityShape& candidate) {
                            return candidate.entity.kind == TopologyKind::face
                                && contains_subshape(candidate.shape, item.shape);
                        }));
            } else if (item.entity.kind == TopologyKind::face) {
                descriptor.adjacent_face_count = static_cast<std::uint32_t>(
                    std::ranges::count_if(
                        stored->entities,
                        [&item](const Impl::EntityShape& candidate) {
                            return candidate.entity.kind == TopologyKind::face
                                && !same_shape(candidate.shape, item.shape)
                                && shares_subshape(
                                    candidate.shape, item.shape, TopAbs_EDGE);
                        }));
            } else {
                descriptor.adjacent_face_count = static_cast<std::uint32_t>(
                    topology_count(item.shape, TopAbs_FACE));
            }

            GProp_GProps properties;
            switch (item.entity.kind) {
            case TopologyKind::vertex: {
                descriptor.geometry_class = GeometryClass::point;
                const auto point = BRep_Tool::Pnt(TopoDS::Vertex(item.shape));
                descriptor.centroid = Point3d{point.X(), point.Y(), point.Z()};
                break;
            }
            case TopologyKind::edge: {
                const BRepAdaptor_Curve curve(TopoDS::Edge(item.shape));
                descriptor.geometry_class = curve_geometry_class(curve.GetType());
                descriptor.parameter_bounds = {
                    curve.FirstParameter(), curve.LastParameter(), 0.0, 0.0};
                BRepGProp::LinearProperties(item.shape, properties);
                descriptor.measure = std::abs(properties.Mass());
                const auto center = properties.CentreOfMass();
                descriptor.centroid = Point3d{center.X(), center.Y(), center.Z()};
                break;
            }
            case TopologyKind::face: {
                const auto face = TopoDS::Face(item.shape);
                const BRepAdaptor_Surface surface(face, Standard_False);
                descriptor.geometry_class = surface_geometry_class(surface.GetType());
                Standard_Real minimum_u = 0.0;
                Standard_Real maximum_u = 0.0;
                Standard_Real minimum_v = 0.0;
                Standard_Real maximum_v = 0.0;
                BRepTools::UVBounds(
                    face, minimum_u, maximum_u, minimum_v, maximum_v);
                descriptor.parameter_bounds = {
                    minimum_u, maximum_u, minimum_v, maximum_v};
                BRepGProp::SurfaceProperties(item.shape, properties);
                descriptor.measure = std::abs(properties.Mass());
                const auto center = properties.CentreOfMass();
                descriptor.centroid = Point3d{center.X(), center.Y(), center.Z()};
                break;
            }
            case TopologyKind::shell: {
                descriptor.geometry_class = GeometryClass::other;
                BRepGProp::SurfaceProperties(item.shape, properties);
                descriptor.measure = std::abs(properties.Mass());
                const auto center = properties.CentreOfMass();
                descriptor.centroid = Point3d{center.X(), center.Y(), center.Z()};
                break;
            }
            case TopologyKind::solid: {
                descriptor.geometry_class = GeometryClass::other;
                BRepGProp::VolumeProperties(item.shape, properties);
                descriptor.measure = std::abs(properties.Mass());
                const auto center = properties.CentreOfMass();
                descriptor.centroid = Point3d{center.X(), center.Y(), center.Z()};
                break;
            }
            }
            result.descriptors.push_back(std::move(descriptor));
        }
        if (cancellation.is_cancellation_requested()) {
            return TopologyInspectionResult{
                .descriptors = {},
                .diagnostics = {diagnostic(
                    GeometryErrorCode::cancelled, "topology inspection was cancelled")},
            };
        }
        return result;
    } catch (const Standard_Failure& error) {
        return TopologyInspectionResult{
            .descriptors = {},
            .diagnostics = {diagnostic(
                GeometryErrorCode::kernel_failure, exception_message(error))},
        };
    }
}

TessellationResult OcctGeometryKernel::tessellate(
    const TessellationRequest& request,
    const CancellationToken& cancellation) const
{
    auto failure = [](GeometryDiagnostic error) {
        TessellationResult result;
        result.diagnostics.push_back(std::move(error));
        return result;
    };
    if (cancellation.is_cancellation_requested()) {
        return failure(diagnostic(
            GeometryErrorCode::cancelled, "display tessellation was cancelled"));
    }
    const auto* stored = impl_->body(request.body);
    if (stored == nullptr) {
        return failure(diagnostic(
            GeometryErrorCode::invalid_handle, "body handle is not live"));
    }
    if (!finite(request.linear_deflection) || request.linear_deflection <= 0.0
        || !finite(request.angular_deflection) || request.angular_deflection <= 0.0) {
        return failure(diagnostic(
            GeometryErrorCode::invalid_input,
            "tessellation deflections must be finite and positive"));
    }

    try {
        BRepMesh_IncrementalMesh mesh(
            stored->shape,
            request.linear_deflection,
            Standard_False,
            request.angular_deflection,
            Standard_True);
        mesh.Perform();
        if (!mesh.IsDone()) {
            return failure(diagnostic(
                GeometryErrorCode::kernel_failure,
                "Open CASCADE could not generate display tessellation"));
        }

        TessellationResult result;
        result.bounds = summarize_shape(stored->shape).bounds;
        for (const auto& item : stored->entities) {
            if (cancellation.is_cancellation_requested()) {
                return failure(diagnostic(
                    GeometryErrorCode::cancelled, "display tessellation was cancelled"));
            }
            if (item.entity.kind == TopologyKind::face) {
                const auto face = TopoDS::Face(item.shape);
                TopLoc_Location location;
                const auto triangulation = BRep_Tool::Triangulation(face, location);
                if (triangulation.IsNull()) continue;
                const auto transform = location.Transformation();
                for (Standard_Integer index = 1;
                     index <= triangulation->NbTriangles(); ++index) {
                    Standard_Integer first{};
                    Standard_Integer second{};
                    Standard_Integer third{};
                    triangulation->Triangle(index).Get(first, second, third);
                    if (face.Orientation() == TopAbs_REVERSED) std::swap(second, third);
                    const auto a = triangulation->Node(first).Transformed(transform);
                    const auto b = triangulation->Node(second).Transformed(transform);
                    const auto c = triangulation->Node(third).Transformed(transform);
                    gp_Vec ab(a, b);
                    gp_Vec ac(a, c);
                    auto normal = ab.Crossed(ac);
                    if (normal.SquareMagnitude() <= tolerance * tolerance) continue;
                    normal.Normalize();
                    const auto base = static_cast<std::uint32_t>(result.vertices.size());
                    for (const auto& point : {a, b, c}) {
                        result.vertices.push_back({
                            .position = {point.X(), point.Y(), point.Z()},
                            .normal = {normal.X(), normal.Y(), normal.Z()},
                        });
                    }
                    result.triangles.push_back({
                        .vertex_indices = {base, base + 1, base + 2},
                        .face_id = item.entity.id,
                    });
                }
            } else if (item.entity.kind == TopologyKind::edge) {
                const BRepAdaptor_Curve curve(TopoDS::Edge(item.shape));
                const auto first = curve.FirstParameter();
                const auto last = curve.LastParameter();
                if (!std::isfinite(first) || !std::isfinite(last) || last < first) continue;
                const Standard_Integer segment_count =
                    curve.GetType() == GeomAbs_Line ? 1 : 16;
                DisplayPolyline polyline{.points = {}, .edge_id = item.entity.id};
                polyline.points.reserve(static_cast<std::size_t>(segment_count + 1));
                for (Standard_Integer sample = 0; sample <= segment_count; ++sample) {
                    const auto parameter = first + (last - first)
                        * static_cast<double>(sample) / static_cast<double>(segment_count);
                    const auto point = curve.Value(parameter);
                    polyline.points.push_back({point.X(), point.Y(), point.Z()});
                }
                if (polyline.points.size() >= 2) {
                    result.edge_polylines.push_back(std::move(polyline));
                }
            }
        }
        return result;
    } catch (const Standard_Failure& error) {
        return failure(diagnostic(
            GeometryErrorCode::kernel_failure, exception_message(error)));
    }
}

BodySnapshotResult OcctGeometryKernel::snapshot(
    const ExactBodyHandle handle,
    const CancellationToken& cancellation) const
{
    if (cancellation.is_cancellation_requested()) {
        return snapshot_failure(diagnostic(
            GeometryErrorCode::cancelled, "body snapshot was cancelled"));
    }
    const auto* stored = impl_->body(handle);
    if (stored == nullptr) {
        return snapshot_failure(diagnostic(
            GeometryErrorCode::invalid_handle, "body handle is not live"));
    }
    try {
        std::ostringstream encoded(std::ios::binary);
        BRepTools::Write(stored->shape, encoded);
        if (!encoded) {
            return snapshot_failure(diagnostic(
                GeometryErrorCode::kernel_failure,
                "Open CASCADE could not serialize the exact body"));
        }
        const auto text = encoded.str();
        std::vector<std::byte> payload;
        payload.reserve(text.size());
        for (const auto character : text) {
            payload.push_back(static_cast<std::byte>(
                static_cast<unsigned char>(character)));
        }
        return BodySnapshotResult{
            .snapshot = ExactBodySnapshot{
                .format = "occt-brep-ascii-v3",
                .kernel_version = OEP_OCCT_VERSION,
                .content_hash = summarize_shape(stored->shape).content_hash,
                .payload = std::move(payload),
            },
            .diagnostics = {},
        };
    } catch (const Standard_Failure& error) {
        return snapshot_failure(diagnostic(
            GeometryErrorCode::kernel_failure, exception_message(error)));
    }
}

OperationResult OcctGeometryKernel::restore_snapshot(
    const ExactBodySnapshot& snapshot,
    const CancellationToken& cancellation)
{
    if (cancellation.is_cancellation_requested()) {
        return cancelled_operation();
    }
    if (snapshot.format != "occt-brep-ascii-v3"
        || snapshot.kernel_version != OEP_OCCT_VERSION || snapshot.payload.empty()) {
        return operation_failure(diagnostic(
            GeometryErrorCode::unsupported_operation,
            "snapshot format or kernel version is incompatible with this adapter"));
    }
    try {
        std::string text;
        text.reserve(snapshot.payload.size());
        for (const auto byte : snapshot.payload) {
            text.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
        }
        std::istringstream encoded(text, std::ios::binary);
        TopoDS_Shape shape;
        BRep_Builder builder;
        BRepTools::Read(shape, encoded, builder);
        if (!encoded || shape.IsNull() || !BRepCheck_Analyzer(shape).IsValid()) {
            return operation_failure(diagnostic(
                GeometryErrorCode::invalid_input,
                "exact-body snapshot is malformed or contains invalid geometry"));
        }
        const auto restored_summary = summarize_shape(shape);
        if (restored_summary.content_hash != snapshot.content_hash) {
            return operation_failure(diagnostic(
                GeometryErrorCode::invalid_input,
                "restored exact body does not match its recorded content hash"));
        }
        if (cancellation.is_cancellation_requested()) {
            return cancelled_operation();
        }
        return OperationResult{
            .body = impl_->store(shape),
            .provenance = {},
            .diagnostics = {},
        };
    } catch (const Standard_Failure& error) {
        return operation_failure(diagnostic(
            GeometryErrorCode::invalid_input, exception_message(error)));
    }
}

} // namespace oep::modeler
