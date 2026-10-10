#pragma once

#include <oep/modeler/kernel_adapter.hpp>

#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oep::topology_id {

struct PersistentTopologyId {
    std::string feature_id;
    std::uint64_t local_id{};

    auto operator<=>(const PersistentTopologyId&) const = default;
};

[[nodiscard]] std::string to_string(const PersistentTopologyId& id);

struct GeometricSignature {
    modeler::TopologyKind kind{modeler::TopologyKind::face};
    modeler::GeometryClass geometry_class{modeler::GeometryClass::unknown};
    double measure{};
    modeler::Point3d centroid;
    std::optional<modeler::Vector3d> orientation;
    std::array<double, 4> parameter_bounds{};
    std::uint32_t adjacent_vertex_count{};
    std::uint32_t adjacent_edge_count{};
    std::uint32_t adjacent_face_count{};
};

[[nodiscard]] GeometricSignature signature_from(
    const modeler::TopologyDescriptor& descriptor);

struct FeatureAncestry {
    std::string origin_feature_id;
    std::string semantic_role;
    std::vector<std::string> modifier_feature_ids;
};

struct TopologyBinding {
    PersistentTopologyId persistent_id;
    modeler::TopologyEntity entity;
    FeatureAncestry ancestry;
    GeometricSignature signature;
};

enum class ResolutionStatus {
    resolved,
    deleted,
    ambiguous,
    missing,
};

enum class ResolutionMethod {
    none,
    unchanged_entity,
    kernel_provenance,
    semantic_role,
    geometric_signature,
};

enum class ResolutionConfidence {
    none,
    low,
    medium,
    high,
    exact,
};

enum class ReconciliationIssueCode {
    deleted_reference,
    split_reference,
    ambiguous_match,
    missing_match,
    provenance_conflict,
    duplicate_target,
    missing_descriptor,
    duplicate_semantic_role,
    invalid_policy,
};

struct ReconciliationIssue {
    ReconciliationIssueCode code{ReconciliationIssueCode::missing_match};
    std::string message;
    std::vector<modeler::TopologyEntity> alternatives;
};

struct ReconciliationDecision {
    PersistentTopologyId persistent_id;
    ResolutionStatus status{ResolutionStatus::missing};
    ResolutionMethod method{ResolutionMethod::none};
    ResolutionConfidence confidence{ResolutionConfidence::none};
    double score{};
    std::optional<modeler::TopologyEntity> resolved_entity;
    std::optional<ReconciliationIssue> issue;
};

struct ReconciliationPolicy {
    double minimum_signature_score{0.78};
    double ambiguity_margin{0.04};
};

struct ReconciliationReport {
    std::vector<TopologyBinding> bindings;
    std::vector<ReconciliationDecision> decisions;
    std::vector<ReconciliationIssue> issues;

    [[nodiscard]] bool fully_resolved() const noexcept;
    [[nodiscard]] const TopologyBinding* find_binding(
        const PersistentTopologyId& id) const noexcept;
    [[nodiscard]] const ReconciliationDecision* find_decision(
        const PersistentTopologyId& id) const noexcept;
};

class TopologyReconciler {
public:
    [[nodiscard]] ReconciliationReport reconcile(
        std::string_view evaluating_feature_id,
        std::span<const TopologyBinding> previous_bindings,
        std::span<const modeler::TopologyDescriptor> current_topology,
        const modeler::TopologyProvenance& provenance,
        const ReconciliationPolicy& policy = {}) const;
};

} // namespace oep::topology_id
