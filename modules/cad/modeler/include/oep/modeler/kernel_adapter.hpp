// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

// Kernel-neutral modeling boundary: handles identify adapter-owned exact bodies,
// not persistent document entities. Operation diagnostics and topology provenance
// allow callers to distinguish geometric failure from reference reconciliation.

#pragma once

#include <atomic>
#include <array>
#include <cstddef>
#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oep::modeler {

// Opaque instance-local body identity. Do not serialize this value as a
// persistent topology reference; use snapshots and reconciled IDs instead.
struct ExactBodyHandle {
    std::uint64_t value{};

    auto operator<=>(const ExactBodyHandle&) const = default;
};

struct KernelTopologyId {
    std::string value;

    auto operator<=>(const KernelTopologyId&) const = default;
};

enum class TopologyKind {
    vertex,
    edge,
    face,
    shell,
    solid,
};

enum class GeometryClass {
    unknown,
    point,
    line,
    circle,
    ellipse,
    bezier_curve,
    bspline_curve,
    plane,
    cylinder,
    cone,
    sphere,
    torus,
    bezier_surface,
    bspline_surface,
    other,
};

struct TopologyEntity {
    ExactBodyHandle body;
    KernelTopologyId id;
    TopologyKind kind{TopologyKind::face};

    bool operator==(const TopologyEntity&) const = default;
};

struct Point2d {
    double x{};
    double y{};

    bool operator==(const Point2d&) const = default;
};

struct Point3d {
    double x{};
    double y{};
    double z{};

    bool operator==(const Point3d&) const = default;
};

struct Vector3d {
    double x{};
    double y{};
    double z{};

    bool operator==(const Vector3d&) const = default;
};

struct PlaneFrame {
    Point3d origin;
    Vector3d normal;
    Vector3d x_direction;

    bool operator==(const PlaneFrame&) const = default;
};

struct ProfileLoop {
    std::vector<Point2d> vertices;
    bool is_hole{};
};

struct PlanarProfile {
    PlaneFrame frame;
    std::vector<ProfileLoop> loops;
};

struct ExtrudeRequest {
    PlanarProfile profile;
    double distance{};
};

struct BooleanCutRequest {
    ExactBodyHandle target;
    ExactBodyHandle tool;
};

struct FilletRequest {
    ExactBodyHandle body;
    std::vector<TopologyEntity> edges;
    double radius{};
};

struct LinearPatternRequest {
    ExactBodyHandle body;
    Vector3d direction;
    std::uint32_t instance_count{};
    double spacing{};
    bool fuse{};
};

struct PlanarFaceRequest {
    TopologyEntity face;
};

struct HealRequest {
    ExactBodyHandle body;
    double tolerance{1.0e-7};
    double maximum_tolerance{1.0e-3};
};

enum class GeometryErrorCode {
    invalid_input,
    invalid_handle,
    degenerate_geometry,
    topology_reference_missing,
    unsupported_operation,
    cancelled,
    kernel_failure,
};

struct GeometryDiagnostic {
    GeometryErrorCode code{GeometryErrorCode::kernel_failure};
    std::string message;
    std::optional<TopologyEntity> entity;
    std::optional<std::string> repair_hint;
};

struct GeneratedTopology {
    std::string semantic_role;
    TopologyEntity result;
};

struct ModifiedTopology {
    TopologyEntity source;
    TopologyEntity result;
};

struct TopologyProvenance {
    std::vector<GeneratedTopology> generated;
    std::vector<ModifiedTopology> modified;
    std::vector<TopologyEntity> deleted;
};

struct OperationResult {
    std::optional<ExactBodyHandle> body;
    TopologyProvenance provenance;
    std::vector<GeometryDiagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept { return body.has_value(); }
};

struct BoundingBox {
    Point3d minimum;
    Point3d maximum;
};

struct BodySummary {
    std::uint64_t solid_count{};
    std::uint64_t face_count{};
    std::uint64_t edge_count{};
    std::uint64_t vertex_count{};
    double volume{};
    BoundingBox bounds;
    std::string topology_signature;
    std::string content_hash;
};

struct BodySummaryResult {
    std::optional<BodySummary> summary;
    std::vector<GeometryDiagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept { return summary.has_value(); }
};

struct TopologyDescriptor {
    TopologyEntity entity;
    GeometryClass geometry_class{GeometryClass::unknown};
    double measure{};
    Point3d centroid;
    std::optional<Vector3d> orientation;
    std::array<double, 4> parameter_bounds{};
    std::uint32_t adjacent_vertex_count{};
    std::uint32_t adjacent_edge_count{};
    std::uint32_t adjacent_face_count{};
};

struct TopologyInspectionResult {
    std::vector<TopologyDescriptor> descriptors;
    std::vector<GeometryDiagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept { return diagnostics.empty(); }
};

// Disposable render data generated from an exact body. Kernel topology IDs
// remain attached to primitives so the tessellation layer can translate them
// to document-persistent identities without exposing kernel-native objects.
struct DisplayVertex {
    Point3d position;
    Vector3d normal;
};

struct DisplayTriangle {
    std::array<std::uint32_t, 3> vertex_indices{};
    KernelTopologyId face_id;
};

struct DisplayPolyline {
    std::vector<Point3d> points;
    KernelTopologyId edge_id;
};

struct TessellationRequest {
    ExactBodyHandle body;
    double linear_deflection{0.2};
    double angular_deflection{0.35};
};

struct TessellationResult {
    std::vector<DisplayVertex> vertices;
    std::vector<DisplayTriangle> triangles;
    std::vector<DisplayPolyline> edge_polylines;
    BoundingBox bounds;
    std::vector<GeometryDiagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept { return diagnostics.empty(); }
};

struct PlanarFaceResult {
    std::optional<PlaneFrame> frame;
    std::vector<GeometryDiagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept { return frame.has_value(); }
};

struct ExactBodySnapshot {
    std::string format;
    std::string kernel_version;
    std::string content_hash;
    std::vector<std::byte> payload;
};

struct BodySnapshotResult {
    std::optional<ExactBodySnapshot> snapshot;
    std::vector<GeometryDiagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept { return snapshot.has_value(); }
};

struct KernelIdentity {
    std::string name;
    std::string version;
    bool provides_exact_brep{};
};

class CancellationToken {
public:
    CancellationToken();

    [[nodiscard]] bool is_cancellation_requested() const noexcept;

private:
    explicit CancellationToken(std::shared_ptr<std::atomic_bool> state);

    std::shared_ptr<std::atomic_bool> state_;

    friend class CancellationSource;
};

class CancellationSource {
public:
    CancellationSource();

    [[nodiscard]] CancellationToken token() const;
    void request_cancellation() noexcept;

private:
    std::shared_ptr<std::atomic_bool> state_;
};

class GeometryKernel {
public:
    virtual ~GeometryKernel() = default;

    [[nodiscard]] virtual KernelIdentity identity() const = 0;
    [[nodiscard]] virtual OperationResult extrude(
        const ExtrudeRequest& request,
        const CancellationToken& cancellation) = 0;
    [[nodiscard]] virtual OperationResult cut(
        const BooleanCutRequest& request,
        const CancellationToken& cancellation) = 0;
    [[nodiscard]] virtual OperationResult fillet(
        const FilletRequest& request,
        const CancellationToken& cancellation) = 0;
    [[nodiscard]] virtual OperationResult linear_pattern(
        const LinearPatternRequest& request,
        const CancellationToken& cancellation) = 0;
    [[nodiscard]] virtual OperationResult heal(
        const HealRequest& request,
        const CancellationToken& cancellation) = 0;
    [[nodiscard]] virtual PlanarFaceResult planar_face(
        const PlanarFaceRequest& request,
        const CancellationToken& cancellation) const = 0;
    [[nodiscard]] virtual BodySummaryResult summarize(
        ExactBodyHandle body,
        const CancellationToken& cancellation) const = 0;
    [[nodiscard]] virtual TopologyInspectionResult inspect_topology(
        ExactBodyHandle body,
        const CancellationToken& cancellation) const = 0;
    // Adapters may decline display tessellation. A default implementation keeps
    // non-rendering test kernels source-compatible with the modeling contract.
    [[nodiscard]] virtual TessellationResult tessellate(
        const TessellationRequest& request,
        const CancellationToken& cancellation) const;
    [[nodiscard]] virtual BodySnapshotResult snapshot(
        ExactBodyHandle body,
        const CancellationToken& cancellation) const = 0;
    [[nodiscard]] virtual OperationResult restore_snapshot(
        const ExactBodySnapshot& snapshot,
        const CancellationToken& cancellation) = 0;
};

[[nodiscard]] const GeometryDiagnostic* first_diagnostic(
    std::span<const GeometryDiagnostic> diagnostics,
    GeometryErrorCode code) noexcept;

[[nodiscard]] const GeneratedTopology* generated_topology(
    const TopologyProvenance& provenance,
    std::string_view semantic_role) noexcept;

} // namespace oep::modeler
