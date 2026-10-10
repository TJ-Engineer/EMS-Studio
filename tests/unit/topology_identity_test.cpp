#include <oep/topology_id/topology_identity.hpp>

#include <algorithm>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace oep::modeler;
using namespace oep::topology_id;

int failures{};

void check(const bool condition, const std::string_view description)
{
    if (!condition) {
        std::cerr << "FAILED: " << description << '\n';
        ++failures;
    }
}

[[nodiscard]] TopologyEntity entity(
    const std::uint64_t body,
    std::string id,
    const TopologyKind kind = TopologyKind::face)
{
    return TopologyEntity{
        .body = ExactBodyHandle{body},
        .id = KernelTopologyId{std::move(id)},
        .kind = kind,
    };
}

[[nodiscard]] TopologyDescriptor descriptor(
    TopologyEntity value,
    const double measure,
    const Point3d centroid,
    const GeometryClass geometry = GeometryClass::plane)
{
    return TopologyDescriptor{
        .entity = std::move(value),
        .geometry_class = geometry,
        .measure = measure,
        .centroid = centroid,
        .orientation = Vector3d{0.0, 0.0, 1.0},
        .parameter_bounds = {0.0, 10.0, 0.0, 10.0},
        .adjacent_vertex_count = 4,
        .adjacent_edge_count = 4,
        .adjacent_face_count = 1,
    };
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

[[nodiscard]] ReconciliationReport seed_extrude(TopologyReconciler& reconciler)
{
    const auto start = entity(1, "start");
    const auto end = entity(1, "end");
    const std::vector descriptors{
        descriptor(end, 50.0, Point3d{5.0, 2.5, 2.0}),
        descriptor(start, 50.0, Point3d{5.0, 2.5, 0.0}),
    };
    const TopologyProvenance provenance{
        .generated = {
            GeneratedTopology{.semantic_role = "extrude.cap.start", .result = start},
            GeneratedTopology{.semantic_role = "extrude.cap.end", .result = end},
        },
        .modified = {},
        .deleted = {},
    };
    return reconciler.reconcile("base-extrude", {}, descriptors, provenance);
}

void generated_roles_receive_deterministic_ids()
{
    TopologyReconciler reconciler;
    const auto report = seed_extrude(reconciler);
    const auto* end = binding_for_role(report, "extrude.cap.end");
    const auto* start = binding_for_role(report, "extrude.cap.start");

    check(report.fully_resolved(), "initial generated topology is accepted");
    check(report.bindings.size() == 2, "generated topology receives bindings");
    check(end != nullptr && start != nullptr, "semantic roles are retained in ancestry");
    if (end != nullptr && start != nullptr) {
        check(
            end->persistent_id == PersistentTopologyId{"base-extrude", 1},
            "generated IDs use semantic-role order");
        check(
            start->persistent_id == PersistentTopologyId{"base-extrude", 2},
            "generated IDs are deterministic across input order");
    }
}

void parameter_sweep_preserves_semantic_identity()
{
    TopologyReconciler reconciler;
    const auto initial = seed_extrude(reconciler);
    const auto end = *binding_for_role(initial, "extrude.cap.end");
    const auto new_end = entity(2, "new-end");
    const std::vector descriptors{
        descriptor(new_end, 72.0, Point3d{6.0, 3.0, 7.0}),
    };
    const TopologyProvenance provenance{
        .generated = {GeneratedTopology{
            .semantic_role = "extrude.cap.end",
            .result = new_end,
        }},
        .modified = {},
        .deleted = {},
    };
    const std::vector previous{end};
    const auto report = reconciler.reconcile(
        "base-extrude", previous, descriptors, provenance);
    const auto* decision = report.find_decision(end.persistent_id);
    const auto* binding = report.find_binding(end.persistent_id);

    check(report.fully_resolved(), "parameter sweep resolves its prior reference");
    check(
        decision != nullptr && decision->method == ResolutionMethod::semantic_role,
        "parameter sweep uses feature ancestry and semantic role");
    check(
        decision != nullptr && decision->confidence == ResolutionConfidence::high,
        "semantic-role match reports high confidence");
    check(
        binding != nullptr && binding->entity == new_end,
        "parameter sweep keeps the persistent ID on the new cap");
}

void modified_provenance_preserves_identity()
{
    TopologyReconciler reconciler;
    const auto initial = seed_extrude(reconciler);
    const auto end = *binding_for_role(initial, "extrude.cap.end");
    const auto modified = entity(3, "cut-cap");
    const auto wall = entity(3, "cut-wall");
    const std::vector descriptors{
        descriptor(wall, 12.0, Point3d{5.0, 3.0, 2.0}, GeometryClass::cylinder),
        descriptor(modified, 46.0, Point3d{5.0, 2.5, 2.0}),
    };
    const TopologyProvenance provenance{
        .generated = {GeneratedTopology{
            .semantic_role = "cut.wall.0",
            .result = wall,
        }},
        .modified = {ModifiedTopology{.source = end.entity, .result = modified}},
        .deleted = {},
    };
    const std::vector previous{end};
    const auto report = reconciler.reconcile("hole-cut", previous, descriptors, provenance);
    const auto* decision = report.find_decision(end.persistent_id);
    const auto* binding = report.find_binding(end.persistent_id);
    const auto* wall_binding = binding_for_role(report, "cut.wall.0");

    check(report.fully_resolved(), "modified provenance resolves exactly");
    check(
        decision != nullptr && decision->method == ResolutionMethod::kernel_provenance,
        "modified topology prefers kernel provenance");
    check(
        decision != nullptr && decision->confidence == ResolutionConfidence::exact,
        "kernel provenance reports exact confidence");
    check(
        binding != nullptr && binding->persistent_id == end.persistent_id,
        "modified topology retains its persistent ID");
    check(
        binding != nullptr && binding->ancestry.modifier_feature_ids == std::vector<std::string>{"hole-cut"},
        "modified topology records feature ancestry");
    check(
        wall_binding != nullptr && wall_binding->persistent_id.feature_id == "hole-cut",
        "newly generated cut topology receives a new feature-owned ID");
}

void deletion_never_silently_reattaches()
{
    TopologyReconciler reconciler;
    const auto initial = seed_extrude(reconciler);
    const auto end = *binding_for_role(initial, "extrude.cap.end");
    const auto replacement = entity(4, "replacement");
    const std::vector descriptors{
        descriptor(replacement, end.signature.measure, end.signature.centroid),
    };
    const TopologyProvenance provenance{
        .generated = {GeneratedTopology{
            .semantic_role = "extrude.cap.end",
            .result = replacement,
        }},
        .modified = {},
        .deleted = {end.entity},
    };
    const std::vector previous{end};
    const auto report = reconciler.reconcile(
        "base-extrude", previous, descriptors, provenance);
    const auto* decision = report.find_decision(end.persistent_id);

    check(!report.fully_resolved(), "deleted reference blocks publication");
    check(
        decision != nullptr && decision->status == ResolutionStatus::deleted,
        "kernel deletion is authoritative over identical geometry");
    check(
        decision != nullptr && decision->issue
            && decision->issue->code == ReconciliationIssueCode::deleted_reference,
        "deleted reference returns a typed diagnostic");
    check(
        report.find_binding(end.persistent_id) == nullptr,
        "deleted persistent ID is not assigned to replacement geometry");
    const auto* new_binding = binding_for_role(report, "extrude.cap.end");
    check(
        new_binding != nullptr && new_binding->persistent_id != end.persistent_id,
        "replacement geometry receives a distinct persistent ID");
}

void split_and_ambiguous_matches_remain_unresolved()
{
    TopologyReconciler reconciler;
    const auto initial = seed_extrude(reconciler);
    const auto end = *binding_for_role(initial, "extrude.cap.end");
    const auto split_a = entity(5, "split-a");
    const auto split_b = entity(5, "split-b");
    const std::vector split_descriptors{
        descriptor(split_a, 25.0, Point3d{2.5, 2.5, 2.0}),
        descriptor(split_b, 25.0, Point3d{7.5, 2.5, 2.0}),
    };
    const TopologyProvenance split_provenance{
        .generated = {},
        .modified = {
            ModifiedTopology{.source = end.entity, .result = split_b},
            ModifiedTopology{.source = end.entity, .result = split_a},
        },
        .deleted = {},
    };
    const std::vector previous{end};
    const auto split_report = reconciler.reconcile(
        "face-split", previous, split_descriptors, split_provenance);
    const auto* split_decision = split_report.find_decision(end.persistent_id);
    check(
        split_decision != nullptr
            && split_decision->status == ResolutionStatus::ambiguous,
        "one-to-many provenance is explicitly ambiguous");
    check(
        split_decision != nullptr && split_decision->issue
            && split_decision->issue->code == ReconciliationIssueCode::split_reference
            && split_decision->issue->alternatives == std::vector<TopologyEntity>{split_a, split_b},
        "split alternatives are deterministically ordered");

    auto signature_a = descriptor(entity(6, "candidate-a"), 50.0, end.signature.centroid);
    auto signature_b = descriptor(entity(6, "candidate-b"), 50.0, end.signature.centroid);
    const std::vector tied_descriptors{signature_b, signature_a};
    const auto ambiguous_report = reconciler.reconcile(
        "downstream-operation", previous, tied_descriptors, {});
    const auto* ambiguous = ambiguous_report.find_decision(end.persistent_id);
    check(
        ambiguous != nullptr && ambiguous->status == ResolutionStatus::ambiguous,
        "equal geometric signatures are not broken by transient entity order");
    check(
        ambiguous != nullptr && !ambiguous->resolved_entity.has_value(),
        "ambiguous signature does not publish a selected entity");
}

void unique_signature_fallback_is_scored()
{
    TopologyReconciler reconciler;
    const auto initial = seed_extrude(reconciler);
    const auto end = *binding_for_role(initial, "extrude.cap.end");
    const auto close = entity(7, "close");
    const auto distant = entity(7, "distant");
    const std::vector descriptors{
        descriptor(distant, 5.0, Point3d{100.0, 100.0, 100.0}),
        descriptor(close, 50.5, Point3d{5.05, 2.5, 2.0}),
    };
    const std::vector previous{end};
    const auto report = reconciler.reconcile(
        "downstream-operation", previous, descriptors, {});
    const auto* decision = report.find_decision(end.persistent_id);

    check(report.fully_resolved(), "unique geometric signature resolves");
    check(
        decision != nullptr
            && decision->method == ResolutionMethod::geometric_signature,
        "fallback records geometric-signature method");
    check(
        decision != nullptr && decision->resolved_entity == close,
        "highest distinct signature candidate is selected");
    check(
        decision != nullptr && decision->score >= 0.78,
        "signature decision records its policy score");
}

void incompatible_geometry_is_never_a_fallback_match()
{
    TopologyReconciler reconciler;
    const auto initial = seed_extrude(reconciler);
    const auto end = *binding_for_role(initial, "extrude.cap.end");
    const auto cylinder = descriptor(
        entity(10, "same-metrics-cylinder"),
        end.signature.measure,
        end.signature.centroid,
        GeometryClass::cylinder);
    const auto opposite_plane = TopologyDescriptor{
        .entity = entity(10, "opposite-plane"),
        .geometry_class = GeometryClass::plane,
        .measure = end.signature.measure,
        .centroid = end.signature.centroid,
        .orientation = Vector3d{0.0, 0.0, -1.0},
        .parameter_bounds = end.signature.parameter_bounds,
        .adjacent_vertex_count = end.signature.adjacent_vertex_count,
        .adjacent_edge_count = end.signature.adjacent_edge_count,
        .adjacent_face_count = end.signature.adjacent_face_count,
    };
    const std::vector descriptors{cylinder, opposite_plane};
    const std::vector previous{end};
    const auto report = reconciler.reconcile(
        "downstream-operation", previous, descriptors, {});
    const auto* decision = report.find_decision(end.persistent_id);

    check(
        decision != nullptr && decision->status == ResolutionStatus::missing,
        "geometry-class and opposite-normal candidates are rejected");
    check(
        decision != nullptr && !decision->resolved_entity,
        "incompatible geometry never receives a persistent reference");
}

void many_to_one_provenance_is_ambiguous()
{
    TopologyReconciler reconciler;
    const auto first_entity = entity(8, "old-a");
    const auto second_entity = entity(8, "old-b");
    const auto target = entity(9, "merged");
    const auto target_descriptor = descriptor(target, 100.0, Point3d{});
    const auto signature = signature_from(target_descriptor);
    const std::vector previous{
        TopologyBinding{
            .persistent_id = {"feature-a", 1},
            .entity = first_entity,
            .ancestry = {.origin_feature_id = "feature-a", .semantic_role = "face.a", .modifier_feature_ids = {}},
            .signature = signature,
        },
        TopologyBinding{
            .persistent_id = {"feature-a", 2},
            .entity = second_entity,
            .ancestry = {.origin_feature_id = "feature-a", .semantic_role = "face.b", .modifier_feature_ids = {}},
            .signature = signature,
        },
    };
    const std::vector descriptors{target_descriptor};
    const TopologyProvenance provenance{
        .generated = {},
        .modified = {
            ModifiedTopology{.source = first_entity, .result = target},
            ModifiedTopology{.source = second_entity, .result = target},
        },
        .deleted = {},
    };
    const auto report = reconciler.reconcile("merge", previous, descriptors, provenance);

    check(!report.fully_resolved(), "many-to-one mapping blocks publication");
    check(
        report.decisions.size() == 2
            && std::ranges::all_of(report.decisions, [](const auto& decision) {
                   return decision.status == ResolutionStatus::ambiguous
                       && decision.issue
                       && decision.issue->code == ReconciliationIssueCode::duplicate_target;
               }),
        "every reference in a many-to-one mapping is diagnosed");
    check(report.bindings.empty(), "many-to-one mapping publishes no binding");
}

} // namespace

int main()
{
    generated_roles_receive_deterministic_ids();
    parameter_sweep_preserves_semantic_identity();
    modified_provenance_preserves_identity();
    deletion_never_silently_reattaches();
    split_and_ambiguous_matches_remain_unresolved();
    unique_signature_fallback_is_scored();
    incompatible_geometry_is_never_a_fallback_match();
    many_to_one_provenance_is_ambiguous();

    if (failures != 0) {
        std::cerr << failures << " topology identity checks failed\n";
        return 1;
    }
    std::cout << "topology identity checks passed\n";
    return 0;
}
