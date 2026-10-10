// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#include <oep/test/topology_mutation_corpus.hpp>

#include <oep/topology_id/topology_identity.hpp>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>
#include <vector>

namespace oep::test {
namespace {

using namespace modeler;
using namespace topology_id;

constexpr double tolerance = 1.0e-8;

class CaseCheck {
public:
    void require(const bool condition, std::string message)
    {
        if (!condition && passed_) {
            passed_ = false;
            detail_ = std::move(message);
        }
    }

    [[nodiscard]] bool passed() const noexcept { return passed_; }
    [[nodiscard]] std::string detail() const
    {
        return passed_ ? "passed" : detail_;
    }

private:
    bool passed_{true};
    std::string detail_;
};

[[nodiscard]] PlanarProfile rectangle_profile(
    const double width,
    const double height,
    const Point3d origin = {})
{
    return PlanarProfile{
        .frame = PlaneFrame{
            .origin = origin,
            .normal = Vector3d{0.0, 0.0, 1.0},
            .x_direction = Vector3d{1.0, 0.0, 0.0},
        },
        .loops = {ProfileLoop{
            .vertices = {
                Point2d{0.0, 0.0},
                Point2d{width, 0.0},
                Point2d{width, height},
                Point2d{0.0, height},
            },
            .is_hole = false,
        }},
    };
}

[[nodiscard]] ExtrudeRequest box_request(
    const double width,
    const double height,
    const double depth,
    const Point3d origin = {})
{
    return ExtrudeRequest{
        .profile = rectangle_profile(width, height, origin),
        .distance = depth,
    };
}

[[nodiscard]] bool nearly_equal(const double left, const double right)
{
    return std::abs(left - right)
        <= tolerance * std::max({1.0, std::abs(left), std::abs(right)});
}

void append_case(
    TopologyMutationReport& report,
    std::string id,
    const CaseCheck& check)
{
    report.cases.push_back(TopologyMutationCaseResult{
        .case_id = std::move(id),
        .passed = check.passed(),
        .detail = check.detail(),
    });
}

[[nodiscard]] const TopologyBinding* binding_for_role(
    const ReconciliationReport& report,
    const std::string_view role)
{
    const auto found = std::ranges::find(
        report.bindings, role, [](const TopologyBinding& binding) {
            return std::string_view(binding.ancestry.semantic_role);
        });
    return found == report.bindings.end() ? nullptr : &*found;
}

[[nodiscard]] ReconciliationReport seed_operation(
    GeometryKernel& kernel,
    const std::string_view feature_id,
    const OperationResult& operation,
    CaseCheck& check)
{
    if (!operation.succeeded()) {
        check.require(false, "operation fixture failed before topology seeding");
        return {};
    }
    const CancellationToken cancellation;
    const auto inspection = kernel.inspect_topology(*operation.body, cancellation);
    check.require(inspection.succeeded(), "topology inspection failed during seeding");
    if (!inspection.succeeded()) {
        return {};
    }
    auto report = TopologyReconciler{}.reconcile(
        feature_id, {}, inspection.descriptors, operation.provenance);
    check.require(report.fully_resolved(), "generated topology seeding was ambiguous");
    return report;
}

void topology_inspection_case(GeometryKernel& kernel, TopologyMutationReport& report)
{
    CaseCheck check;
    const CancellationToken cancellation;
    const auto extrusion = kernel.extrude(box_request(10.0, 5.0, 2.0), cancellation);
    check.require(extrusion.succeeded(), "inspection extrusion failed");
    if (extrusion.succeeded()) {
        const auto inspection = kernel.inspect_topology(*extrusion.body, cancellation);
        check.require(inspection.succeeded(), "body topology inspection failed");
        const auto* end = generated_topology(extrusion.provenance, "extrude.cap.end");
        check.require(end != nullptr, "end-cap provenance is missing");
        if (inspection.succeeded() && end != nullptr) {
            const auto found = std::ranges::find(
                inspection.descriptors, end->result, &TopologyDescriptor::entity);
            check.require(found != inspection.descriptors.end(), "end cap was not inspected");
            if (found != inspection.descriptors.end()) {
                check.require(
                    found->geometry_class == GeometryClass::plane,
                    "end cap is not classified as a plane");
                check.require(nearly_equal(found->measure, 50.0), "end-cap area differs");
                check.require(nearly_equal(found->centroid.z, 2.0), "end-cap centroid differs");
                check.require(found->adjacent_edge_count >= 4, "end-cap adjacency is incomplete");
            }
        }
        CancellationSource source;
        const auto cancelled = source.token();
        source.request_cancellation();
        const auto cancelled_inspection = kernel.inspect_topology(
            *extrusion.body, cancelled);
        check.require(
            !cancelled_inspection.succeeded()
                && first_diagnostic(
                    cancelled_inspection.diagnostics,
                    GeometryErrorCode::cancelled)
                    != nullptr,
            "cancelled topology inspection did not return a typed diagnostic");
    }
    append_case(report, "inspect-topology-signatures", check);
}

void extrusion_sweep_case(GeometryKernel& kernel, TopologyMutationReport& report)
{
    CaseCheck check;
    const CancellationToken cancellation;
    const auto initial = kernel.extrude(box_request(10.0, 5.0, 2.0), cancellation);
    const auto seeded = seed_operation(kernel, "base-extrude", initial, check);
    const auto* initial_end = binding_for_role(seeded, "extrude.cap.end");
    check.require(initial_end != nullptr, "initial end cap has no persistent binding");

    const auto changed = kernel.extrude(box_request(12.0, 6.0, 7.0), cancellation);
    check.require(changed.succeeded(), "parameter-sweep extrusion failed");
    if (initial_end != nullptr && changed.succeeded()) {
        const auto inspection = kernel.inspect_topology(*changed.body, cancellation);
        check.require(inspection.succeeded(), "changed extrusion inspection failed");
        if (inspection.succeeded()) {
            const std::vector previous{*initial_end};
            const auto reconciled = TopologyReconciler{}.reconcile(
                "base-extrude", previous, inspection.descriptors, changed.provenance);
            const auto* decision = reconciled.find_decision(initial_end->persistent_id);
            const auto* binding = reconciled.find_binding(initial_end->persistent_id);
            check.require(
                reconciled.fully_resolved(),
                "parameter sweep did not preserve the end-cap reference");
            check.require(
                decision != nullptr && decision->method == ResolutionMethod::semantic_role,
                "parameter sweep did not use semantic ancestry");
            check.require(binding != nullptr, "parameter sweep lost the persistent ID");
            if (binding != nullptr) {
                const auto plane = kernel.planar_face(
                    PlanarFaceRequest{.face = binding->entity}, cancellation);
                check.require(plane.succeeded(), "preserved end cap cannot host a sketch");
                if (plane.succeeded()) {
                    check.require(
                        nearly_equal(plane.frame->origin.z, 7.0),
                        "attached-sketch frame did not follow the parameter sweep");
                }
            }
        }
    }
    append_case(report, "extrude-sweep-preserves-attached-face", check);
}

void cut_provenance_case(GeometryKernel& kernel, TopologyMutationReport& report)
{
    CaseCheck check;
    const CancellationToken cancellation;
    const auto target = kernel.extrude(box_request(10.0, 8.0, 4.0), cancellation);
    const auto seeded = seed_operation(kernel, "base-extrude", target, check);
    const auto* initial_end = binding_for_role(seeded, "extrude.cap.end");
    check.require(initial_end != nullptr, "cut target end cap has no persistent binding");
    const auto tool = kernel.extrude(
        box_request(2.0, 2.0, 6.0, Point3d{4.0, 3.0, -1.0}), cancellation);
    check.require(tool.succeeded(), "cut tool construction failed");
    if (initial_end != nullptr && target.succeeded() && tool.succeeded()) {
        const auto cut = kernel.cut(
            BooleanCutRequest{.target = *target.body, .tool = *tool.body}, cancellation);
        check.require(cut.succeeded(), "topology-changing cut failed");
        if (cut.succeeded()) {
            const auto inspection = kernel.inspect_topology(*cut.body, cancellation);
            check.require(inspection.succeeded(), "cut topology inspection failed");
            if (inspection.succeeded()) {
                const std::vector previous{*initial_end};
                const auto reconciled = TopologyReconciler{}.reconcile(
                    "through-cut", previous, inspection.descriptors, cut.provenance);
                const auto* decision = reconciled.find_decision(initial_end->persistent_id);
                const auto* binding = reconciled.find_binding(initial_end->persistent_id);
                check.require(
                    decision != nullptr
                        && decision->method == ResolutionMethod::kernel_provenance
                        && decision->confidence == ResolutionConfidence::exact,
                    "cut did not preserve its modified cap through exact provenance");
                check.require(binding != nullptr, "cut lost its modified cap binding");
                if (binding != nullptr) {
                    const auto plane = kernel.planar_face(
                        PlanarFaceRequest{.face = binding->entity}, cancellation);
                    check.require(plane.succeeded(), "modified cut cap is not sketchable");
                }
                const auto generated = std::ranges::find_if(
                    reconciled.bindings,
                    [](const TopologyBinding& candidate) {
                        return candidate.persistent_id.feature_id == "through-cut";
                    });
                check.require(
                    generated != reconciled.bindings.end(),
                    "cut-generated topology did not receive a persistent ID");
            }
        }
    }
    append_case(report, "cut-preserves-modified-cap", check);
}

void fillet_reference_case(GeometryKernel& kernel, TopologyMutationReport& report)
{
    CaseCheck check;
    const CancellationToken cancellation;
    const auto base = kernel.extrude(box_request(10.0, 8.0, 4.0), cancellation);
    const auto seeded = seed_operation(kernel, "base-extrude", base, check);
    const auto* edge = binding_for_role(seeded, "extrude.edge.0");
    const auto* cap = binding_for_role(seeded, "extrude.cap.end");
    check.require(edge != nullptr, "fillet source edge has no persistent binding");
    check.require(cap != nullptr, "fillet result cap has no persistent binding");
    if (edge != nullptr && cap != nullptr && base.succeeded()) {
        const auto fillet = kernel.fillet(
            FilletRequest{
                .body = *base.body,
                .edges = {edge->entity},
                .radius = 1.0,
            },
            cancellation);
        check.require(fillet.succeeded(), "fillet operation failed");
        if (fillet.succeeded()) {
            const auto inspection = kernel.inspect_topology(*fillet.body, cancellation);
            check.require(inspection.succeeded(), "fillet topology inspection failed");
            if (inspection.succeeded()) {
                const std::vector previous{*edge, *cap};
                const auto reconciled = TopologyReconciler{}.reconcile(
                    "edge-fillet", previous, inspection.descriptors, fillet.provenance);
                const auto* decision = reconciled.find_decision(edge->persistent_id);
                check.require(decision != nullptr, "fillet omitted a reference decision");
                if (decision != nullptr) {
                    check.require(
                        decision->method != ResolutionMethod::geometric_signature,
                        "fillet silently reassigned its source edge by geometry");
                    check.require(
                        decision->status == ResolutionStatus::resolved
                            || decision->status == ResolutionStatus::deleted
                            || decision->status == ResolutionStatus::ambiguous,
                        "fillet returned an unclassified source-edge result");
                }
                const auto* cap_decision = reconciled.find_decision(cap->persistent_id);
                const auto* cap_binding = reconciled.find_binding(cap->persistent_id);
                check.require(
                    cap_decision != nullptr
                        && cap_decision->status == ResolutionStatus::resolved,
                    "fillet did not preserve a planar face for an attached sketch");
                check.require(
                    cap_binding != nullptr,
                    "fillet result lost the persistent planar-face ID");
                if (cap_binding != nullptr) {
                    const auto plane = kernel.planar_face(
                        PlanarFaceRequest{.face = cap_binding->entity}, cancellation);
                    check.require(
                        plane.succeeded(),
                        "preserved fillet-result face cannot host an attached sketch");
                }
                const auto generated = std::ranges::find_if(
                    reconciled.bindings,
                    [](const TopologyBinding& candidate) {
                        return candidate.persistent_id.feature_id == "edge-fillet"
                            && candidate.entity.kind == TopologyKind::face;
                    });
                check.require(
                    generated != reconciled.bindings.end(),
                    "fillet-generated face did not receive a persistent ID");
            }
        }
    }
    append_case(report, "fillet-classifies-source-and-generated-topology", check);
}

void pattern_parameter_case(GeometryKernel& kernel, TopologyMutationReport& report)
{
    CaseCheck check;
    const CancellationToken cancellation;
    const auto base = kernel.extrude(box_request(1.0, 1.0, 1.0), cancellation);
    check.require(base.succeeded(), "pattern base construction failed");
    if (base.succeeded()) {
        const auto first = kernel.linear_pattern(
            LinearPatternRequest{
                .body = *base.body,
                .direction = Vector3d{1.0, 0.0, 0.0},
                .instance_count = 3,
                .spacing = 2.0,
                .fuse = false,
            },
            cancellation);
        const auto seeded = seed_operation(kernel, "linear-pattern", first, check);
        check.require(seeded.bindings.size() == 3, "pattern instances were not seeded");

        const auto changed = kernel.linear_pattern(
            LinearPatternRequest{
                .body = *base.body,
                .direction = Vector3d{1.0, 0.0, 0.0},
                .instance_count = 3,
                .spacing = 3.0,
                .fuse = false,
            },
            cancellation);
        check.require(changed.succeeded(), "changed pattern failed");
        if (changed.succeeded() && seeded.bindings.size() == 3) {
            const auto inspection = kernel.inspect_topology(*changed.body, cancellation);
            check.require(inspection.succeeded(), "changed pattern inspection failed");
            if (inspection.succeeded()) {
                const auto reconciled = TopologyReconciler{}.reconcile(
                    "linear-pattern",
                    seeded.bindings,
                    inspection.descriptors,
                    changed.provenance);
                check.require(
                    reconciled.fully_resolved(),
                    "pattern spacing change did not preserve instance references");
                for (const auto& previous : seeded.bindings) {
                    const auto* binding = reconciled.find_binding(previous.persistent_id);
                    check.require(
                        binding != nullptr
                            && binding->ancestry.semantic_role
                                == previous.ancestry.semantic_role,
                        "pattern instance ID moved to a different semantic role");
                }
            }
        }
    }
    append_case(report, "pattern-spacing-preserves-instance-ids", check);
}

} // namespace

bool TopologyMutationReport::passed() const noexcept
{
    return std::ranges::all_of(
        cases, [](const TopologyMutationCaseResult& result) { return result.passed; });
}

TopologyMutationReport run_topology_mutation_corpus(GeometryKernel& kernel)
{
    TopologyMutationReport report{
        .kernel = kernel.identity(),
        .cases = {},
    };
    topology_inspection_case(kernel, report);
    extrusion_sweep_case(kernel, report);
    cut_provenance_case(kernel, report);
    fillet_reference_case(kernel, report);
    pattern_parameter_case(kernel, report);
    return report;
}

} // namespace oep::test
