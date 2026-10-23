// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

// Persistent topology identity is separate from transient kernel entity IDs.
// Reconciliation combines operation provenance with geometric signatures;
// ambiguous or deleted references must be diagnosed rather than silently rebound.

#include <oep/topology_id/topology_identity.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <tuple>
#include <utility>

namespace oep::topology_id {
namespace {

using modeler::GeometryClass;
using modeler::TopologyDescriptor;
using modeler::TopologyEntity;
using modeler::TopologyKind;

[[nodiscard]] bool entity_less(const TopologyEntity& left, const TopologyEntity& right)
{
    return std::tie(left.body.value, left.kind, left.id.value)
        < std::tie(right.body.value, right.kind, right.id.value);
}

[[nodiscard]] const TopologyDescriptor* descriptor_for(
    const std::span<const TopologyDescriptor> descriptors,
    const TopologyEntity& entity)
{
    const auto found = std::ranges::find(descriptors, entity, &TopologyDescriptor::entity);
    return found == descriptors.end() ? nullptr : &*found;
}

[[nodiscard]] bool compatible_geometry(
    const GeometricSignature& previous,
    const TopologyDescriptor& current)
{
    if (previous.kind != current.entity.kind) {
        return false;
    }
    return previous.geometry_class == GeometryClass::unknown
        || current.geometry_class == GeometryClass::unknown
        || previous.geometry_class == current.geometry_class;
}

[[nodiscard]] double finite_similarity(const double left, const double right)
{
    if (!std::isfinite(left) || !std::isfinite(right)) {
        return 0.5;
    }
    const auto scale = std::max({std::abs(left), std::abs(right), 1.0e-12});
    return 1.0 - std::min(1.0, std::abs(left - right) / scale);
}

[[nodiscard]] double count_similarity(const std::uint32_t left, const std::uint32_t right)
{
    if (left == 0 && right == 0) {
        return 1.0;
    }
    const auto maximum = static_cast<double>(std::max(left, right));
    const auto difference = static_cast<double>(
        left > right ? left - right : right - left);
    return 1.0 - difference / maximum;
}

[[nodiscard]] double characteristic_scale(const GeometricSignature& signature)
{
    const auto measure = std::max(std::abs(signature.measure), 1.0e-18);
    switch (signature.kind) {
    case TopologyKind::vertex:
        return 1.0;
    case TopologyKind::edge:
        return measure;
    case TopologyKind::face:
    case TopologyKind::shell:
        return std::sqrt(measure);
    case TopologyKind::solid:
        return std::cbrt(measure);
    }
    return 1.0;
}

[[nodiscard]] double orientation_similarity(
    const GeometricSignature& previous,
    const TopologyDescriptor& current)
{
    if (!previous.orientation || !current.orientation) {
        return 0.5;
    }
    const auto magnitude = [](const modeler::Vector3d& vector) {
        return std::sqrt(
            vector.x * vector.x + vector.y * vector.y + vector.z * vector.z);
    };
    const auto previous_magnitude = magnitude(*previous.orientation);
    const auto current_magnitude = magnitude(*current.orientation);
    if (previous_magnitude <= 1.0e-15 || current_magnitude <= 1.0e-15) {
        return 0.5;
    }
    auto dot = (previous.orientation->x * current.orientation->x
                   + previous.orientation->y * current.orientation->y
                   + previous.orientation->z * current.orientation->z)
        / (previous_magnitude * current_magnitude);
    dot = std::clamp(dot, -1.0, 1.0);
    if (previous.geometry_class == GeometryClass::line
        || previous.geometry_class == GeometryClass::circle
        || previous.geometry_class == GeometryClass::ellipse) {
        dot = std::abs(dot);
        return dot;
    }
    return (dot + 1.0) * 0.5;
}

// Geometric similarity supports reference reconciliation; a score alone
// is not proof of identity. Ambiguous candidates must remain explicit.
[[nodiscard]] double signature_similarity(
    const GeometricSignature& previous,
    const TopologyDescriptor& current)
{
    if (previous.kind != current.entity.kind) {
        return 0.0;
    }

    double geometry = 0.0;
    if (previous.geometry_class == current.geometry_class) {
        geometry = 1.0;
    } else if (previous.geometry_class == GeometryClass::unknown
        || current.geometry_class == GeometryClass::unknown) {
        geometry = 0.5;
    }

    const auto measure = finite_similarity(previous.measure, current.measure);
    const auto delta_x = previous.centroid.x - current.centroid.x;
    const auto delta_y = previous.centroid.y - current.centroid.y;
    const auto delta_z = previous.centroid.z - current.centroid.z;
    const auto distance = std::sqrt(
        delta_x * delta_x + delta_y * delta_y + delta_z * delta_z);
    const auto centroid = 1.0
        / (1.0 + distance / std::max(characteristic_scale(previous), 1.0e-12));
    const auto orientation = orientation_similarity(previous, current);
    const auto adjacency = (
        count_similarity(
            previous.adjacent_vertex_count, current.adjacent_vertex_count)
        + count_similarity(previous.adjacent_edge_count, current.adjacent_edge_count)
        + count_similarity(previous.adjacent_face_count, current.adjacent_face_count))
        / 3.0;
    auto parameters = 0.0;
    for (std::size_t index = 0; index < previous.parameter_bounds.size(); ++index) {
        parameters += finite_similarity(
            previous.parameter_bounds[index], current.parameter_bounds[index]);
    }
    parameters /= static_cast<double>(previous.parameter_bounds.size());

    return geometry * 0.20 + measure * 0.20 + centroid * 0.20
        + orientation * 0.15 + adjacency * 0.15 + parameters * 0.10;
}

[[nodiscard]] ResolutionConfidence confidence_for_score(const double score)
{
    if (score >= 0.92) {
        return ResolutionConfidence::high;
    }
    if (score >= 0.84) {
        return ResolutionConfidence::medium;
    }
    return ResolutionConfidence::low;
}

[[nodiscard]] ReconciliationIssue issue(
    const ReconciliationIssueCode code,
    std::string message,
    std::vector<TopologyEntity> alternatives = {})
{
    std::ranges::sort(alternatives, entity_less);
    return ReconciliationIssue{
        .code = code,
        .message = std::move(message),
        .alternatives = std::move(alternatives),
    };
}

[[nodiscard]] std::vector<TopologyEntity> modified_results(
    const modeler::TopologyProvenance& provenance,
    const TopologyEntity& source)
{
    std::vector<TopologyEntity> results;
    for (const auto& modified : provenance.modified) {
        if (modified.source == source
            && std::ranges::find(results, modified.result) == results.end()) {
            results.push_back(modified.result);
        }
    }
    std::ranges::sort(results, entity_less);
    return results;
}

[[nodiscard]] bool was_deleted(
    const modeler::TopologyProvenance& provenance,
    const TopologyEntity& source)
{
    return std::ranges::find(provenance.deleted, source) != provenance.deleted.end();
}

[[nodiscard]] std::vector<TopologyEntity> entities_for_role(
    const modeler::TopologyProvenance& provenance,
    const std::string_view role)
{
    std::vector<TopologyEntity> entities;
    for (const auto& generated : provenance.generated) {
        if (generated.semantic_role == role
            && std::ranges::find(entities, generated.result) == entities.end()) {
            entities.push_back(generated.result);
        }
    }
    std::ranges::sort(entities, entity_less);
    return entities;
}

void resolve(
    ReconciliationDecision& decision,
    const TopologyEntity& entity,
    const ResolutionMethod method,
    const ResolutionConfidence confidence,
    const double score)
{
    decision.status = ResolutionStatus::resolved;
    decision.method = method;
    decision.confidence = confidence;
    decision.score = score;
    decision.resolved_entity = entity;
    decision.issue.reset();
}

void fail(
    ReconciliationDecision& decision,
    const ResolutionStatus status,
    ReconciliationIssue value)
{
    decision.status = status;
    decision.method = ResolutionMethod::none;
    decision.confidence = ResolutionConfidence::none;
    decision.score = 0.0;
    decision.resolved_entity.reset();
    decision.issue = std::move(value);
}

} // namespace

std::string to_string(const PersistentTopologyId& id)
{
    return id.feature_id + ":topology:" + std::to_string(id.local_id);
}

GeometricSignature signature_from(const TopologyDescriptor& descriptor)
{
    return GeometricSignature{
        .kind = descriptor.entity.kind,
        .geometry_class = descriptor.geometry_class,
        .measure = descriptor.measure,
        .centroid = descriptor.centroid,
        .orientation = descriptor.orientation,
        .parameter_bounds = descriptor.parameter_bounds,
        .adjacent_vertex_count = descriptor.adjacent_vertex_count,
        .adjacent_edge_count = descriptor.adjacent_edge_count,
        .adjacent_face_count = descriptor.adjacent_face_count,
    };
}

bool ReconciliationReport::fully_resolved() const noexcept
{
    return issues.empty()
        && std::ranges::all_of(decisions, [](const ReconciliationDecision& decision) {
               return decision.status == ResolutionStatus::resolved;
           });
}

const TopologyBinding* ReconciliationReport::find_binding(
    const PersistentTopologyId& id) const noexcept
{
    const auto found = std::ranges::find(bindings, id, &TopologyBinding::persistent_id);
    return found == bindings.end() ? nullptr : &*found;
}

const ReconciliationDecision* ReconciliationReport::find_decision(
    const PersistentTopologyId& id) const noexcept
{
    const auto found = std::ranges::find(decisions, id, &ReconciliationDecision::persistent_id);
    return found == decisions.end() ? nullptr : &*found;
}

ReconciliationReport TopologyReconciler::reconcile(
    const std::string_view evaluating_feature_id,
    const std::span<const TopologyBinding> previous_bindings,
    const std::span<const TopologyDescriptor> current_topology,
    const modeler::TopologyProvenance& provenance,
    const ReconciliationPolicy& policy) const
{
    ReconciliationReport report;
    report.decisions.reserve(previous_bindings.size());

    if (evaluating_feature_id.empty() || policy.minimum_signature_score < 0.0
        || policy.minimum_signature_score > 1.0 || policy.ambiguity_margin < 0.0
        || policy.ambiguity_margin > 1.0) {
        const auto invalid = issue(
            ReconciliationIssueCode::invalid_policy,
            "topology reconciliation requires a feature ID and scores in [0, 1]");
        report.issues.push_back(invalid);
        for (const auto& previous : previous_bindings) {
            ReconciliationDecision decision;
            decision.persistent_id = previous.persistent_id;
            fail(decision, ResolutionStatus::missing, invalid);
            report.decisions.push_back(std::move(decision));
        }
        return report;
    }

    for (const auto& previous : previous_bindings) {
        ReconciliationDecision decision;
        decision.persistent_id = previous.persistent_id;
        auto provenance_results = modified_results(provenance, previous.entity);
        const auto deleted = was_deleted(provenance, previous.entity);

        if (deleted && !provenance_results.empty()) {
            fail(
                decision,
                ResolutionStatus::ambiguous,
                issue(
                    ReconciliationIssueCode::provenance_conflict,
                    "kernel provenance reports the same topology as deleted and modified",
                    std::move(provenance_results)));
            report.decisions.push_back(std::move(decision));
            continue;
        }
        if (deleted) {
            fail(
                decision,
                ResolutionStatus::deleted,
                issue(
                    ReconciliationIssueCode::deleted_reference,
                    "referenced topology was explicitly deleted by the kernel"));
            report.decisions.push_back(std::move(decision));
            continue;
        }
        if (provenance_results.size() > 1) {
            fail(
                decision,
                ResolutionStatus::ambiguous,
                issue(
                    ReconciliationIssueCode::split_reference,
                    "referenced topology split into multiple results",
                    std::move(provenance_results)));
            report.decisions.push_back(std::move(decision));
            continue;
        }
        if (provenance_results.size() == 1) {
            const auto* descriptor = descriptor_for(current_topology, provenance_results.front());
            if (descriptor == nullptr) {
                fail(
                    decision,
                    ResolutionStatus::missing,
                    issue(
                        ReconciliationIssueCode::missing_descriptor,
                        "kernel provenance result is absent from topology inspection",
                        provenance_results));
            } else if (!compatible_geometry(previous.signature, *descriptor)) {
                fail(
                    decision,
                    ResolutionStatus::ambiguous,
                    issue(
                        ReconciliationIssueCode::provenance_conflict,
                        "kernel provenance changed the topology kind or geometry class",
                        provenance_results));
            } else {
                resolve(
                    decision,
                    provenance_results.front(),
                    ResolutionMethod::kernel_provenance,
                    ResolutionConfidence::exact,
                    1.0);
            }
            report.decisions.push_back(std::move(decision));
            continue;
        }

        if (descriptor_for(current_topology, previous.entity) != nullptr) {
            resolve(
                decision,
                previous.entity,
                ResolutionMethod::unchanged_entity,
                ResolutionConfidence::exact,
                1.0);
            report.decisions.push_back(std::move(decision));
            continue;
        }

        if (previous.ancestry.origin_feature_id == evaluating_feature_id) {
            auto role_entities = entities_for_role(
                provenance, previous.ancestry.semantic_role);
            role_entities.erase(
                std::remove_if(
                    role_entities.begin(),
                    role_entities.end(),
                    [&](const TopologyEntity& entity) {
                        const auto* descriptor = descriptor_for(current_topology, entity);
                        return descriptor == nullptr
                            || !compatible_geometry(previous.signature, *descriptor);
                    }),
                role_entities.end());
            if (role_entities.size() == 1) {
                resolve(
                    decision,
                    role_entities.front(),
                    ResolutionMethod::semantic_role,
                    ResolutionConfidence::high,
                    0.98);
            } else if (role_entities.size() > 1) {
                fail(
                    decision,
                    ResolutionStatus::ambiguous,
                    issue(
                        ReconciliationIssueCode::duplicate_semantic_role,
                        "feature emitted the same semantic role for multiple topology entities",
                        std::move(role_entities)));
            } else {
                fail(
                    decision,
                    ResolutionStatus::missing,
                    issue(
                        ReconciliationIssueCode::missing_match,
                        "the feature no longer emits the referenced semantic role"));
            }
            report.decisions.push_back(std::move(decision));
            continue;
        }

        struct ScoredCandidate {
            const TopologyDescriptor* descriptor{};
            double score{};
        };
        std::vector<ScoredCandidate> candidates;
        for (const auto& descriptor : current_topology) {
            const auto orientation_is_compatible =
                previous.signature.geometry_class != GeometryClass::plane
                || orientation_similarity(previous.signature, descriptor) >= 0.75;
            if (compatible_geometry(previous.signature, descriptor)
                && orientation_is_compatible) {
                candidates.push_back(ScoredCandidate{
                    .descriptor = &descriptor,
                    .score = signature_similarity(previous.signature, descriptor),
                });
            }
        }
        std::ranges::sort(candidates, [](const auto& left, const auto& right) {
            if (left.score != right.score) {
                return left.score > right.score;
            }
            return entity_less(left.descriptor->entity, right.descriptor->entity);
        });

        if (candidates.empty() || candidates.front().score < policy.minimum_signature_score) {
            fail(
                decision,
                ResolutionStatus::missing,
                issue(
                    ReconciliationIssueCode::missing_match,
                    "no topology candidate satisfies the geometric signature policy"));
        } else if (candidates.size() > 1
            && candidates.front().score - candidates[1].score < policy.ambiguity_margin) {
            std::vector<TopologyEntity> alternatives;
            for (const auto& candidate : candidates) {
                if (candidates.front().score - candidate.score >= policy.ambiguity_margin) {
                    break;
                }
                alternatives.push_back(candidate.descriptor->entity);
            }
            fail(
                decision,
                ResolutionStatus::ambiguous,
                issue(
                    ReconciliationIssueCode::ambiguous_match,
                    "multiple topology candidates have indistinguishable signatures",
                    std::move(alternatives)));
        } else {
            resolve(
                decision,
                candidates.front().descriptor->entity,
                ResolutionMethod::geometric_signature,
                confidence_for_score(candidates.front().score),
                candidates.front().score);
        }
        report.decisions.push_back(std::move(decision));
    }

    for (std::size_t first = 0; first < report.decisions.size(); ++first) {
        auto& decision = report.decisions[first];
        if (decision.status != ResolutionStatus::resolved) {
            continue;
        }
        std::vector<std::size_t> collisions{first};
        for (std::size_t second = first + 1; second < report.decisions.size(); ++second) {
            if (report.decisions[second].status == ResolutionStatus::resolved
                && report.decisions[second].resolved_entity == decision.resolved_entity) {
                collisions.push_back(second);
            }
        }
        if (collisions.size() > 1) {
            const auto target = *decision.resolved_entity;
            for (const auto index : collisions) {
                fail(
                    report.decisions[index],
                    ResolutionStatus::ambiguous,
                    issue(
                        ReconciliationIssueCode::duplicate_target,
                        "multiple persistent references resolved to the same topology entity",
                        {target}));
            }
        }
    }

    for (std::size_t index = 0; index < report.decisions.size(); ++index) {
        const auto& decision = report.decisions[index];
        if (decision.status != ResolutionStatus::resolved) {
            continue;
        }
        const auto* descriptor = descriptor_for(current_topology, *decision.resolved_entity);
        if (descriptor == nullptr) {
            continue;
        }
        auto ancestry = previous_bindings[index].ancestry;
        if (decision.method != ResolutionMethod::semantic_role
            && decision.method != ResolutionMethod::unchanged_entity
            && evaluating_feature_id != ancestry.origin_feature_id
            && (ancestry.modifier_feature_ids.empty()
                || ancestry.modifier_feature_ids.back() != evaluating_feature_id)) {
            ancestry.modifier_feature_ids.emplace_back(evaluating_feature_id);
        }
        report.bindings.push_back(TopologyBinding{
            .persistent_id = decision.persistent_id,
            .entity = *decision.resolved_entity,
            .ancestry = std::move(ancestry),
            .signature = signature_from(*descriptor),
        });
    }

    auto next_local_id = std::uint64_t{};
    for (const auto& previous : previous_bindings) {
        if (previous.persistent_id.feature_id == evaluating_feature_id) {
            next_local_id = std::max(next_local_id, previous.persistent_id.local_id);
        }
    }

    auto generated = provenance.generated;
    std::ranges::sort(generated, [](const auto& left, const auto& right) {
        if (left.semantic_role != right.semantic_role) {
            return left.semantic_role < right.semantic_role;
        }
        return entity_less(left.result, right.result);
    });
    for (std::size_t index = 0; index < generated.size();) {
        auto end = index + 1;
        while (end < generated.size()
            && generated[end].semantic_role == generated[index].semantic_role) {
            ++end;
        }
        if (end - index > 1) {
            std::vector<TopologyEntity> alternatives;
            for (auto duplicate = index; duplicate < end; ++duplicate) {
                alternatives.push_back(generated[duplicate].result);
            }
            report.issues.push_back(issue(
                ReconciliationIssueCode::duplicate_semantic_role,
                "generated topology semantic role is not unique within the feature",
                std::move(alternatives)));
            index = end;
            continue;
        }

        const auto& item = generated[index];
        const auto already_bound = std::ranges::find(
            report.bindings, item.result, &TopologyBinding::entity);
        if (already_bound == report.bindings.end()) {
            const auto* descriptor = descriptor_for(current_topology, item.result);
            if (descriptor == nullptr) {
                report.issues.push_back(issue(
                    ReconciliationIssueCode::missing_descriptor,
                    "generated topology is absent from topology inspection",
                    {item.result}));
            } else {
                report.bindings.push_back(TopologyBinding{
                    .persistent_id = PersistentTopologyId{
                        .feature_id = std::string(evaluating_feature_id),
                        .local_id = ++next_local_id,
                    },
                    .entity = item.result,
                    .ancestry = FeatureAncestry{
                        .origin_feature_id = std::string(evaluating_feature_id),
                        .semantic_role = item.semantic_role,
                        .modifier_feature_ids = {},
                    },
                    .signature = signature_from(*descriptor),
                });
            }
        }
        index = end;
    }

    std::ranges::sort(report.bindings, [](const auto& left, const auto& right) {
        return left.persistent_id < right.persistent_id;
    });
    return report;
}

} // namespace oep::topology_id
