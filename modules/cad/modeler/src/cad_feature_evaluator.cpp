#include <oep/modeler/cad_feature_evaluator.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace oep::modeler {
namespace {

using document::Diagnostic;
using document::FeatureEvaluation;
using document::FeatureEvaluationRequest;
using document::FeatureEvaluationResult;
using document::FeatureId;
using document::FeatureInput;
using document::FeatureState;
using topology_id::PersistentTopologyId;
using topology_id::ReconciliationDecision;
using topology_id::ReconciliationReport;
using topology_id::TopologyBinding;

[[nodiscard]] Diagnostic diagnostic(
    std::string code,
    std::string message,
    std::optional<FeatureId> feature_id = std::nullopt)
{
    return Diagnostic{
        .code = std::move(code),
        .message = std::move(message),
        .feature_id = std::move(feature_id),
    };
}

[[nodiscard]] std::string_view geometry_code(const GeometryErrorCode code)
{
    switch (code) {
    case GeometryErrorCode::invalid_input:
        return "InvalidInput";
    case GeometryErrorCode::invalid_handle:
        return "InvalidHandle";
    case GeometryErrorCode::degenerate_geometry:
        return "DegenerateGeometry";
    case GeometryErrorCode::topology_reference_missing:
        return "TopologyReferenceMissing";
    case GeometryErrorCode::unsupported_operation:
        return "UnsupportedOperation";
    case GeometryErrorCode::cancelled:
        return "Cancelled";
    case GeometryErrorCode::kernel_failure:
        return "KernelFailure";
    }
    return "KernelFailure";
}

[[nodiscard]] std::vector<Diagnostic> geometry_diagnostics(
    const std::span<const GeometryDiagnostic> diagnostics,
    const FeatureId& feature_id)
{
    std::vector<Diagnostic> result;
    result.reserve(diagnostics.size());
    for (const auto& item : diagnostics) {
        result.push_back(diagnostic(
            "Geometry" + std::string(geometry_code(item.code)), item.message, feature_id));
    }
    return result;
}

[[nodiscard]] std::string_view resolution_status_name(
    const topology_id::ResolutionStatus status)
{
    switch (status) {
    case topology_id::ResolutionStatus::resolved:
        return "resolved";
    case topology_id::ResolutionStatus::deleted:
        return "deleted";
    case topology_id::ResolutionStatus::ambiguous:
        return "ambiguous";
    case topology_id::ResolutionStatus::missing:
        return "missing";
    }
    return "missing";
}

[[nodiscard]] std::string_view resolution_method_name(
    const topology_id::ResolutionMethod method)
{
    switch (method) {
    case topology_id::ResolutionMethod::none:
        return "none";
    case topology_id::ResolutionMethod::unchanged_entity:
        return "unchanged-entity";
    case topology_id::ResolutionMethod::kernel_provenance:
        return "kernel-provenance";
    case topology_id::ResolutionMethod::semantic_role:
        return "semantic-role";
    case topology_id::ResolutionMethod::geometric_signature:
        return "geometric-signature";
    }
    return "none";
}

[[nodiscard]] std::string_view resolution_confidence_name(
    const topology_id::ResolutionConfidence confidence)
{
    switch (confidence) {
    case topology_id::ResolutionConfidence::none:
        return "none";
    case topology_id::ResolutionConfidence::low:
        return "low";
    case topology_id::ResolutionConfidence::medium:
        return "medium";
    case topology_id::ResolutionConfidence::high:
        return "high";
    case topology_id::ResolutionConfidence::exact:
        return "exact";
    }
    return "none";
}

[[nodiscard]] std::string_view topology_diagnostic_code(
    const topology_id::ResolutionStatus status)
{
    switch (status) {
    case topology_id::ResolutionStatus::resolved:
        return "TopologyReferenceResolved";
    case topology_id::ResolutionStatus::deleted:
        return "TopologyReferenceDeleted";
    case topology_id::ResolutionStatus::ambiguous:
        return "TopologyReferenceAmbiguous";
    case topology_id::ResolutionStatus::missing:
        return "TopologyReferenceMissing";
    }
    return "TopologyReferenceMissing";
}

[[nodiscard]] std::vector<Diagnostic> topology_diagnostics(
    const ReconciliationReport& report,
    const FeatureId& feature_id)
{
    std::vector<Diagnostic> result;
    result.reserve(report.decisions.size());
    for (const ReconciliationDecision& decision : report.decisions) {
        const auto status = resolution_status_name(decision.status);
        std::ostringstream message;
        message << std::setprecision(17)
                << topology_id::to_string(decision.persistent_id) << " was " << status
                << " by " << resolution_method_name(decision.method) << " with "
                << resolution_confidence_name(decision.confidence)
                << " confidence (score " << decision.score << ')';
        if (decision.issue.has_value()) {
            message << ": " << decision.issue->message;
        }
        result.push_back(diagnostic(
            std::string(topology_diagnostic_code(decision.status)),
            std::move(message).str(),
            feature_id));
    }
    return result;
}

[[nodiscard]] const FeatureInput* input(
    const FeatureEvaluationRequest& request,
    const std::string_view name)
{
    const auto found = std::ranges::find(
        request.inputs, name, &FeatureInput::input_name);
    return found == request.inputs.end() ? nullptr : &*found;
}

[[nodiscard]] double parameter(
    const FeatureEvaluationRequest& request,
    const std::string_view parameter_id)
{
    const auto found = request.semantic_state.find(parameter_id);
    if (found == request.semantic_state.end()) {
        throw std::logic_error("CAD feature parameter is missing from semantic state");
    }
    return found->second;
}

[[nodiscard]] sketch::Definition rectangle_definition(
    const double minimum_x,
    const double minimum_y,
    const double width,
    const double height,
    const std::string_view prefix)
{
    const auto entity_id = [prefix](const std::string_view suffix) {
        return sketch::EntityId{std::string(prefix) + std::string(suffix)};
    };
    const auto constraint_id = [prefix](const std::string_view suffix) {
        return sketch::ConstraintId{std::string(prefix) + std::string(suffix)};
    };

    const auto p0 = entity_id("point.0");
    const auto p1 = entity_id("point.1");
    const auto p2 = entity_id("point.2");
    const auto p3 = entity_id("point.3");
    const auto l0 = entity_id("line.0");
    const auto l1 = entity_id("line.1");
    const auto l2 = entity_id("line.2");
    const auto l3 = entity_id("line.3");

    return sketch::Definition{
        .entities = {
            sketch::PointEntity{p0, sketch::Point2d{minimum_x + 0.1, minimum_y - 0.1}},
            sketch::PointEntity{p1, sketch::Point2d{minimum_x + width + 0.1, minimum_y + 0.1}},
            sketch::PointEntity{p2, sketch::Point2d{minimum_x + width - 0.1, minimum_y + height + 0.1}},
            sketch::PointEntity{p3, sketch::Point2d{minimum_x - 0.1, minimum_y + height - 0.1}},
            sketch::LineEntity{l0, p0, p1},
            sketch::LineEntity{l1, p1, p2},
            sketch::LineEntity{l2, p2, p3},
            sketch::LineEntity{l3, p3, p0},
        },
        .constraints = {
            sketch::FixedPointConstraint{
                constraint_id("fixed"), p0, sketch::Point2d{minimum_x, minimum_y}},
            sketch::HorizontalConstraint{constraint_id("horizontal.0"), l0},
            sketch::VerticalConstraint{constraint_id("vertical.0"), l1},
            sketch::HorizontalConstraint{constraint_id("horizontal.1"), l2},
            sketch::VerticalConstraint{constraint_id("vertical.1"), l3},
            sketch::DistanceConstraint{constraint_id("width"), p0, p1, width},
            sketch::DistanceConstraint{constraint_id("height"), p1, p2, height},
        },
    };
}

[[nodiscard]] std::optional<PlanarProfile> profile_from_rectangle(
    const sketch::SolveResult& result,
    const std::string_view prefix,
    PlaneFrame frame)
{
    ProfileLoop loop;
    for (const auto index : {0, 1, 2, 3}) {
        const auto* point = result.find_point(
            sketch::EntityId{std::string(prefix) + "point." + std::to_string(index)});
        if (point == nullptr) {
            return std::nullopt;
        }
        loop.vertices.push_back(Point2d{point->position.x, point->position.y});
    }
    return PlanarProfile{
        .frame = frame,
        .loops = {std::move(loop)},
    };
}

[[nodiscard]] std::string sketch_hash(
    const std::string_view feature_id,
    const sketch::SolveResult& result,
    const std::optional<PersistentTopologyId>& support = std::nullopt)
{
    std::ostringstream stream;
    stream << feature_id << "|sketch|" << result.deterministic_signature;
    if (support.has_value()) {
        stream << "|support|" << topology_id::to_string(*support);
    }
    return stream.str();
}

[[nodiscard]] std::string body_hash(
    const std::string_view feature_id,
    const BodySummary& summary)
{
    return std::string(feature_id) + "|body|" + summary.content_hash;
}

[[nodiscard]] FeatureEvaluationResult missing_artifact(
    const FeatureId& feature_id,
    const std::string_view input_name)
{
    return FeatureEvaluationResult{
        .output_hash = std::nullopt,
        .diagnostics = {diagnostic(
            "MissingFeatureArtifact",
            "the " + std::string(input_name)
                + " input has no hydrated CAD artifact",
            feature_id)},
    };
}

[[nodiscard]] std::vector<const document::FeatureDefinition*> topological_order(
    const document::FeatureGraph& graph)
{
    std::map<std::string, const document::FeatureDefinition*, std::less<>> features;
    std::map<std::string, std::size_t, std::less<>> indegrees;
    std::map<std::string, std::vector<std::string>, std::less<>> dependents;
    for (const auto& feature : graph) {
        features.emplace(feature.id.value, &feature);
        indegrees.emplace(feature.id.value, feature.dependencies.size());
        for (const auto& dependency : feature.dependencies) {
            dependents[dependency.upstream_feature_id.value].push_back(feature.id.value);
        }
    }
    std::set<std::string, std::less<>> ready;
    for (const auto& [id, indegree] : indegrees) {
        if (indegree == 0) {
            ready.insert(id);
        }
    }
    std::vector<const document::FeatureDefinition*> result;
    while (!ready.empty()) {
        const auto id = *ready.begin();
        ready.erase(ready.begin());
        result.push_back(features.at(id));
        for (const auto& dependent : dependents[id]) {
            auto& indegree = indegrees.at(dependent);
            --indegree;
            if (indegree == 0) {
                ready.insert(dependent);
            }
        }
    }
    return result;
}

[[nodiscard]] const document::EvaluationRecord* evaluation_for(
    const document::DocumentState& state,
    const document::RevisionId revision_id)
{
    const auto found = std::ranges::find(
        state.evaluations, revision_id, &document::EvaluationRecord::revision_id);
    return found == state.evaluations.end() ? nullptr : &*found;
}

[[nodiscard]] const FeatureEvaluation* feature_evaluation_for(
    const document::EvaluationRecord& record,
    const FeatureId& feature_id)
{
    const auto found = std::ranges::find(
        record.features, feature_id, &FeatureEvaluation::feature_id);
    return found == record.features.end() ? nullptr : &*found;
}

[[nodiscard]] bool equivalent_state(
    const FeatureState expected,
    const FeatureState actual)
{
    if (document::is_successful_feature_state(expected)) {
        return document::is_successful_feature_state(actual);
    }
    return expected == actual;
}

[[nodiscard]] bool equivalent_diagnostics(
    const std::span<const Diagnostic> expected,
    const std::span<const Diagnostic> actual)
{
    return expected.size() == actual.size()
        && std::ranges::equal(
            expected,
            actual,
            [](const Diagnostic& left, const Diagnostic& right) {
                return left.code == right.code && left.message == right.message
                    && left.feature_id == right.feature_id;
            });
}

} // namespace

CadModelSeed make_reel2_cad_model()
{
    return CadModelSeed{
        .semantic_state = {
            {std::string(width_parameter_id), 20.0},
            {std::string(height_parameter_id), 14.0},
            {std::string(depth_parameter_id), 6.0},
            {std::string(fillet_radius_parameter_id), 1.0},
            {std::string(hole_size_parameter_id), 4.0},
        },
        .feature_graph = {
            document::FeatureDefinition{
                .id = FeatureId{std::string(base_sketch_feature_id)},
                .parameter_ids = {
                    std::string(width_parameter_id), std::string(height_parameter_id)},
                .dependencies = {},
            },
            document::FeatureDefinition{
                .id = FeatureId{std::string(base_extrude_feature_id)},
                .parameter_ids = {std::string(depth_parameter_id)},
                .dependencies = {{
                    .upstream_feature_id = FeatureId{std::string(base_sketch_feature_id)},
                    .input_name = "profile",
                }},
            },
            document::FeatureDefinition{
                .id = FeatureId{std::string(edge_fillet_feature_id)},
                .parameter_ids = {std::string(fillet_radius_parameter_id)},
                .dependencies = {{
                    .upstream_feature_id = FeatureId{std::string(base_extrude_feature_id)},
                    .input_name = "body",
                }},
            },
            document::FeatureDefinition{
                .id = FeatureId{std::string(hole_sketch_feature_id)},
                .parameter_ids = {std::string(hole_size_parameter_id)},
                .dependencies = {{
                    .upstream_feature_id = FeatureId{std::string(edge_fillet_feature_id)},
                    .input_name = "support",
                }},
            },
            document::FeatureDefinition{
                .id = FeatureId{std::string(hole_cut_feature_id)},
                .parameter_ids = {},
                .dependencies = {
                    {
                        .upstream_feature_id = FeatureId{std::string(edge_fillet_feature_id)},
                        .input_name = "target",
                    },
                    {
                        .upstream_feature_id = FeatureId{std::string(hole_sketch_feature_id)},
                        .input_name = "profile",
                    },
                },
            },
        },
    };
}

CadFeatureEvaluator::CadFeatureEvaluator(GeometryKernel& kernel)
    : kernel_(&kernel)
{
    const auto identity = kernel.identity();
    version_ = "reel2-cad-v1/" + identity.name + '/' + identity.version;
}

std::string_view CadFeatureEvaluator::version() const noexcept
{
    return version_;
}

FeatureEvaluationResult CadFeatureEvaluator::evaluate_feature(
    const FeatureEvaluationRequest& request)
{
    ++evaluation_counts_[request.feature.id.value];
    if (request.feature.id.value == base_sketch_feature_id) {
        return evaluate_base_sketch(request);
    }
    if (request.feature.id.value == base_extrude_feature_id) {
        return evaluate_base_extrude(request);
    }
    if (request.feature.id.value == edge_fillet_feature_id) {
        return evaluate_fillet(request);
    }
    if (request.feature.id.value == hole_sketch_feature_id) {
        return evaluate_hole_sketch(request);
    }
    if (request.feature.id.value == hole_cut_feature_id) {
        return evaluate_cut(request);
    }
    return FeatureEvaluationResult{
        .output_hash = std::nullopt,
        .diagnostics = {diagnostic(
            "UnknownCadFeature", "the CAD evaluator does not recognize this feature", request.feature.id)},
    };
}

FeatureEvaluationResult CadFeatureEvaluator::evaluate_base_sketch(
    const FeatureEvaluationRequest& request)
{
    const auto width = parameter(request, width_parameter_id);
    const auto height = parameter(request, height_parameter_id);
    const auto solved = sketch::Solver{}.solve(
        rectangle_definition(0.0, 0.0, width, height, "base."));
    if (solved.status != sketch::SolveStatus::fully_constrained) {
        return FeatureEvaluationResult{
            .output_hash = std::nullopt,
            .diagnostics = {diagnostic(
                "SketchNotFullyConstrained",
                "the base profile must be a valid fully constrained sketch",
                request.feature.id)},
        };
    }
    auto profile = profile_from_rectangle(
        solved,
        "base.",
        PlaneFrame{
            .origin = Point3d{0.0, 0.0, 0.0},
            .normal = Vector3d{0.0, 0.0, 1.0},
            .x_direction = Vector3d{1.0, 0.0, 0.0},
        });
    if (!profile.has_value()) {
        return FeatureEvaluationResult{
            .output_hash = std::nullopt,
            .diagnostics = {diagnostic(
                "SketchProfileIncomplete",
                "the solved sketch is missing a required profile point",
                request.feature.id)},
        };
    }
    const auto hash = sketch_hash(base_sketch_feature_id, solved);
    CadFeatureArtifact artifact_value;
    artifact_value.kind = CadArtifactKind::sketch_profile;
    artifact_value.feature_id = request.feature.id;
    artifact_value.output_hash = hash;
    artifact_value.profile = std::move(profile);
    artifact_value.sketch_result = solved;
    artifacts_[hash] = std::move(artifact_value);
    return FeatureEvaluationResult{.output_hash = hash, .diagnostics = {}};
}

FeatureEvaluationResult CadFeatureEvaluator::evaluate_base_extrude(
    const FeatureEvaluationRequest& request)
{
    const auto* profile_input = input(request, "profile");
    const auto* profile_artifact = profile_input == nullptr
        ? nullptr
        : artifact(profile_input->output_hash);
    if (profile_artifact == nullptr || !profile_artifact->profile.has_value()) {
        return missing_artifact(request.feature.id, "profile");
    }

    const auto operation = kernel_->extrude(
        ExtrudeRequest{
            .profile = *profile_artifact->profile,
            .distance = parameter(request, depth_parameter_id),
        },
        CancellationToken{});
    if (!operation.succeeded()) {
        return FeatureEvaluationResult{
            .output_hash = std::nullopt,
            .diagnostics = geometry_diagnostics(operation.diagnostics, request.feature.id),
        };
    }
    const auto summary = kernel_->summarize(*operation.body, CancellationToken{});
    const auto inspection = kernel_->inspect_topology(*operation.body, CancellationToken{});
    if (!summary.succeeded()) {
        return FeatureEvaluationResult{
            .output_hash = std::nullopt,
            .diagnostics = geometry_diagnostics(summary.diagnostics, request.feature.id),
        };
    }
    if (!inspection.succeeded()) {
        return FeatureEvaluationResult{
            .output_hash = std::nullopt,
            .diagnostics = geometry_diagnostics(inspection.diagnostics, request.feature.id),
        };
    }
    auto topology = topology_id::TopologyReconciler{}.reconcile(
        base_extrude_feature_id, {}, inspection.descriptors, operation.provenance);
    const auto hash = body_hash(base_extrude_feature_id, *summary.summary);
    CadFeatureArtifact artifact_value;
    artifact_value.kind = CadArtifactKind::exact_body;
    artifact_value.feature_id = request.feature.id;
    artifact_value.output_hash = hash;
    artifact_value.body = operation.body;
    artifact_value.body_summary = summary.summary;
    artifact_value.topology_bindings = std::move(topology.bindings);
    artifact_value.topology_decisions = std::move(topology.decisions);
    artifact_value.topology_issues = std::move(topology.issues);
    artifacts_[hash] = std::move(artifact_value);
    return FeatureEvaluationResult{.output_hash = hash, .diagnostics = {}};
}

FeatureEvaluationResult CadFeatureEvaluator::evaluate_fillet(
    const FeatureEvaluationRequest& request)
{
    const auto* body_input = input(request, "body");
    const auto* source = body_input == nullptr ? nullptr : artifact(body_input->output_hash);
    if (source == nullptr || !source->body.has_value()) {
        return missing_artifact(request.feature.id, "body");
    }
    const auto* edge = binding_for_role(*source, "extrude.edge.0");
    const auto* cap = binding_for_role(*source, "extrude.cap.end");
    if (edge == nullptr || cap == nullptr) {
        return FeatureEvaluationResult{
            .output_hash = std::nullopt,
            .diagnostics = {diagnostic(
                "RequiredTopologyReferenceMissing",
                "fillet requires the authored edge and attached-sketch cap references",
                request.feature.id)},
        };
    }

    const auto operation = kernel_->fillet(
        FilletRequest{
            .body = *source->body,
            .edges = {edge->entity},
            .radius = parameter(request, fillet_radius_parameter_id),
        },
        CancellationToken{});
    if (!operation.succeeded()) {
        return FeatureEvaluationResult{
            .output_hash = std::nullopt,
            .diagnostics = geometry_diagnostics(operation.diagnostics, request.feature.id),
        };
    }
    const auto summary = kernel_->summarize(*operation.body, CancellationToken{});
    const auto inspection = kernel_->inspect_topology(*operation.body, CancellationToken{});
    if (!summary.succeeded()) {
        return FeatureEvaluationResult{
            .output_hash = std::nullopt,
            .diagnostics = geometry_diagnostics(summary.diagnostics, request.feature.id),
        };
    }
    if (!inspection.succeeded()) {
        return FeatureEvaluationResult{
            .output_hash = std::nullopt,
            .diagnostics = geometry_diagnostics(inspection.diagnostics, request.feature.id),
        };
    }
    auto topology = topology_id::TopologyReconciler{}.reconcile(
        edge_fillet_feature_id,
        source->topology_bindings,
        inspection.descriptors,
        operation.provenance);
    const auto cap_still_resolved = topology.find_binding(cap->persistent_id) != nullptr;
    if (!cap_still_resolved) {
        return FeatureEvaluationResult{
            .output_hash = std::nullopt,
            .diagnostics = {diagnostic(
                "AttachedSketchSupportUnresolved",
                "the fillet could not preserve or uniquely resolve its planar support face",
                request.feature.id)},
        };
    }
    auto decisions = topology_diagnostics(topology, request.feature.id);
    const auto hash = body_hash(edge_fillet_feature_id, *summary.summary);
    CadFeatureArtifact artifact_value;
    artifact_value.kind = CadArtifactKind::exact_body;
    artifact_value.feature_id = request.feature.id;
    artifact_value.output_hash = hash;
    artifact_value.body = operation.body;
    artifact_value.body_summary = summary.summary;
    artifact_value.topology_bindings = std::move(topology.bindings);
    artifact_value.topology_decisions = std::move(topology.decisions);
    artifact_value.topology_issues = std::move(topology.issues);
    artifacts_[hash] = std::move(artifact_value);
    return FeatureEvaluationResult{
        .output_hash = hash,
        .diagnostics = std::move(decisions),
    };
}

FeatureEvaluationResult CadFeatureEvaluator::evaluate_hole_sketch(
    const FeatureEvaluationRequest& request)
{
    const auto* support_input = input(request, "support");
    const auto* support = support_input == nullptr ? nullptr : artifact(support_input->output_hash);
    if (support == nullptr || !support->body.has_value()) {
        return missing_artifact(request.feature.id, "support");
    }
    const auto* cap = binding_for_role(*support, "extrude.cap.end");
    if (cap == nullptr) {
        return FeatureEvaluationResult{
            .output_hash = std::nullopt,
            .diagnostics = {diagnostic(
                "AttachedSketchSupportUnresolved",
                "the persistent planar support face is unavailable",
                request.feature.id)},
        };
    }
    const auto face = kernel_->planar_face(
        PlanarFaceRequest{.face = cap->entity}, CancellationToken{});
    if (!face.succeeded()) {
        return FeatureEvaluationResult{
            .output_hash = std::nullopt,
            .diagnostics = geometry_diagnostics(face.diagnostics, request.feature.id),
        };
    }

    const auto hole_size = parameter(request, hole_size_parameter_id);
    const auto minimum_x = (parameter(request, width_parameter_id) - hole_size) * 0.5;
    const auto minimum_y = (parameter(request, height_parameter_id) - hole_size) * 0.5;
    const auto solved = sketch::Solver{}.solve(
        rectangle_definition(minimum_x, minimum_y, hole_size, hole_size, "hole."));
    if (solved.status != sketch::SolveStatus::fully_constrained) {
        return FeatureEvaluationResult{
            .output_hash = std::nullopt,
            .diagnostics = {diagnostic(
                "SketchNotFullyConstrained",
                "the attached cut profile must be a valid fully constrained sketch",
                request.feature.id)},
        };
    }
    auto profile = profile_from_rectangle(solved, "hole.", *face.frame);
    if (!profile.has_value()) {
        return FeatureEvaluationResult{
            .output_hash = std::nullopt,
            .diagnostics = {diagnostic(
                "SketchProfileIncomplete",
                "the attached sketch is missing a required profile point",
                request.feature.id)},
        };
    }
    const auto hash = sketch_hash(hole_sketch_feature_id, solved, cap->persistent_id);
    CadFeatureArtifact artifact_value;
    artifact_value.kind = CadArtifactKind::sketch_profile;
    artifact_value.feature_id = request.feature.id;
    artifact_value.output_hash = hash;
    artifact_value.profile = std::move(profile);
    artifact_value.sketch_result = solved;
    artifact_value.support_reference = cap->persistent_id;
    artifacts_[hash] = std::move(artifact_value);
    return FeatureEvaluationResult{.output_hash = hash, .diagnostics = {}};
}

FeatureEvaluationResult CadFeatureEvaluator::evaluate_cut(
    const FeatureEvaluationRequest& request)
{
    const auto* target_input = input(request, "target");
    const auto* profile_input = input(request, "profile");
    const auto* target = target_input == nullptr ? nullptr : artifact(target_input->output_hash);
    const auto* profile = profile_input == nullptr ? nullptr : artifact(profile_input->output_hash);
    if (target == nullptr || !target->body.has_value()) {
        return missing_artifact(request.feature.id, "target");
    }
    if (profile == nullptr || !profile->profile.has_value()) {
        return missing_artifact(request.feature.id, "profile");
    }

    auto tool_profile = *profile->profile;
    const auto depth = parameter(request, depth_parameter_id);
    tool_profile.frame.origin.x -= tool_profile.frame.normal.x * depth;
    tool_profile.frame.origin.y -= tool_profile.frame.normal.y * depth;
    tool_profile.frame.origin.z -= tool_profile.frame.normal.z * depth;
    const auto tool = kernel_->extrude(
        ExtrudeRequest{.profile = std::move(tool_profile), .distance = depth},
        CancellationToken{});
    if (!tool.succeeded()) {
        return FeatureEvaluationResult{
            .output_hash = std::nullopt,
            .diagnostics = geometry_diagnostics(tool.diagnostics, request.feature.id),
        };
    }
    const auto operation = kernel_->cut(
        BooleanCutRequest{.target = *target->body, .tool = *tool.body},
        CancellationToken{});
    if (!operation.succeeded()) {
        return FeatureEvaluationResult{
            .output_hash = std::nullopt,
            .diagnostics = geometry_diagnostics(operation.diagnostics, request.feature.id),
        };
    }
    const auto summary = kernel_->summarize(*operation.body, CancellationToken{});
    const auto inspection = kernel_->inspect_topology(*operation.body, CancellationToken{});
    if (!summary.succeeded()) {
        return FeatureEvaluationResult{
            .output_hash = std::nullopt,
            .diagnostics = geometry_diagnostics(summary.diagnostics, request.feature.id),
        };
    }
    if (!inspection.succeeded()) {
        return FeatureEvaluationResult{
            .output_hash = std::nullopt,
            .diagnostics = geometry_diagnostics(inspection.diagnostics, request.feature.id),
        };
    }
    auto topology = topology_id::TopologyReconciler{}.reconcile(
        hole_cut_feature_id,
        target->topology_bindings,
        inspection.descriptors,
        operation.provenance);
    auto decisions = topology_diagnostics(topology, request.feature.id);
    const auto hash = body_hash(hole_cut_feature_id, *summary.summary);
    CadFeatureArtifact artifact_value;
    artifact_value.kind = CadArtifactKind::exact_body;
    artifact_value.feature_id = request.feature.id;
    artifact_value.output_hash = hash;
    artifact_value.body = operation.body;
    artifact_value.body_summary = summary.summary;
    artifact_value.topology_bindings = std::move(topology.bindings);
    artifact_value.topology_decisions = std::move(topology.decisions);
    artifact_value.topology_issues = std::move(topology.issues);
    artifacts_[hash] = std::move(artifact_value);
    return FeatureEvaluationResult{
        .output_hash = hash,
        .diagnostics = std::move(decisions),
    };
}

std::optional<std::string> CadFeatureEvaluator::compose_geometry(
    const document::RevisionId,
    const document::SemanticState&,
    const std::span<const FeatureEvaluation> features)
{
    const auto final = std::ranges::find(
        features,
        FeatureId{std::string(hole_cut_feature_id)},
        &FeatureEvaluation::feature_id);
    if (final == features.end() || !final->output_hash.has_value()) {
        return std::nullopt;
    }
    const auto* final_artifact = artifact(*final->output_hash);
    if (final_artifact == nullptr || !final_artifact->body_summary.has_value()) {
        return std::nullopt;
    }
    return final_artifact->body_summary->content_hash;
}

CadHydrationResult CadFeatureEvaluator::hydrate(const document::DocumentState& state)
{
    artifacts_.clear();
    evaluation_counts_.clear();
    CadHydrationResult hydration;
    auto revisions = state.revisions;
    std::ranges::sort(revisions, {}, &document::DocumentRevision::id);
    for (const auto& revision : revisions) {
        const auto* expected = evaluation_for(state, revision.id);
        if (expected == nullptr) {
            hydration.diagnostics.push_back(diagnostic(
                "CadHydrationMissingEvaluation",
                "a persisted revision has no evaluation record"));
            continue;
        }
        if (expected->evaluator_version != version_) {
            hydration.diagnostics.push_back(diagnostic(
                "CadHydrationEvaluatorMismatch",
                "the persisted evaluation was produced by a different CAD evaluator"));
            continue;
        }

        std::vector<FeatureEvaluation> produced;
        for (const auto* feature : topological_order(revision.feature_graph)) {
            std::vector<FeatureInput> inputs;
            auto blocked = false;
            for (const auto& dependency : feature->dependencies) {
                const auto upstream = std::ranges::find(
                    produced, dependency.upstream_feature_id, &FeatureEvaluation::feature_id);
                if (upstream == produced.end() || !upstream->output_hash.has_value()) {
                    blocked = true;
                    continue;
                }
                inputs.push_back(FeatureInput{
                    .upstream_feature_id = dependency.upstream_feature_id,
                    .input_name = dependency.input_name,
                    .output_hash = *upstream->output_hash,
                });
            }
            FeatureEvaluationResult result;
            if (!blocked) {
                result = evaluate_feature(FeatureEvaluationRequest{
                    .revision_id = revision.id,
                    .semantic_state = revision.semantic_state,
                    .feature = *feature,
                    .inputs = inputs,
                });
            } else {
                result.diagnostics.push_back(diagnostic(
                    "UpstreamFeatureFailed",
                    "one or more required upstream features did not produce output",
                    feature->id));
            }
            produced.push_back(FeatureEvaluation{
                .feature_id = feature->id,
                .state = blocked ? FeatureState::blocked
                                 : (result.output_hash.has_value() ? FeatureState::succeeded
                                                                   : FeatureState::failed),
                .output_hash = std::move(result.output_hash),
                .diagnostics = std::move(result.diagnostics),
            });

            const auto* expected_feature = feature_evaluation_for(*expected, feature->id);
            const auto& actual_feature = produced.back();
            if (expected_feature == nullptr
                || !equivalent_state(expected_feature->state, actual_feature.state)
                || expected_feature->output_hash != actual_feature.output_hash
                || !equivalent_diagnostics(
                    expected_feature->diagnostics, actual_feature.diagnostics)) {
                hydration.diagnostics.push_back(diagnostic(
                    "CadHydrationMismatch",
                    "recomputed feature output does not match the persisted evaluation",
                    feature->id));
            }
        }

        const auto all_succeeded = std::ranges::all_of(
            produced,
            [](const FeatureEvaluation& item) {
                return document::is_successful_feature_state(item.state);
            });
        if (all_succeeded) {
            const auto geometry = compose_geometry(
                revision.id, revision.semantic_state, produced);
            if (geometry != expected->geometry_hash) {
                hydration.diagnostics.push_back(diagnostic(
                    "CadHydrationGeometryMismatch",
                    "recomputed exact body does not match the persisted geometry hash"));
            }
        } else if (expected->geometry_hash.has_value()) {
            hydration.diagnostics.push_back(diagnostic(
                "CadHydrationGeometryMismatch",
                "a failed recomputation unexpectedly has persisted current geometry"));
        }
    }
    return hydration;
}

const CadFeatureArtifact* CadFeatureEvaluator::artifact(
    const std::string_view output_hash) const noexcept
{
    const auto found = artifacts_.find(output_hash);
    return found == artifacts_.end() ? nullptr : &found->second;
}

std::size_t CadFeatureEvaluator::evaluation_count(
    const std::string_view feature_id) const noexcept
{
    const auto found = evaluation_counts_.find(feature_id);
    return found == evaluation_counts_.end() ? 0 : found->second;
}

const TopologyBinding* binding_for_role(
    const CadFeatureArtifact& artifact,
    const std::string_view semantic_role) noexcept
{
    const auto found = std::ranges::find(
        artifact.topology_bindings,
        semantic_role,
        [](const TopologyBinding& binding) -> std::string_view {
            return binding.ancestry.semantic_role;
        });
    return found == artifact.topology_bindings.end() ? nullptr : &*found;
}

} // namespace oep::modeler
