// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <oep/document/document.hpp>
#include <oep/modeler/kernel_adapter.hpp>
#include <oep/sketch/sketch.hpp>
#include <oep/topology_id/topology_identity.hpp>

#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oep::modeler {

inline constexpr std::string_view base_sketch_feature_id = "base-sketch";
inline constexpr std::string_view base_extrude_feature_id = "base-extrude";
inline constexpr std::string_view edge_fillet_feature_id = "edge-fillet";
inline constexpr std::string_view hole_sketch_feature_id = "hole-sketch";
inline constexpr std::string_view hole_cut_feature_id = "hole-cut";

inline constexpr std::string_view width_parameter_id = "width";
inline constexpr std::string_view height_parameter_id = "height";
inline constexpr std::string_view depth_parameter_id = "depth";
inline constexpr std::string_view fillet_radius_parameter_id = "fillet-radius";
inline constexpr std::string_view hole_size_parameter_id = "hole-size";

struct CadModelSeed {
    document::SemanticState semantic_state;
    document::FeatureGraph feature_graph;
};

[[nodiscard]] CadModelSeed make_reel2_cad_model();

enum class CadArtifactKind {
    sketch_profile,
    exact_body,
};

struct CadFeatureArtifact {
    CadArtifactKind kind{CadArtifactKind::sketch_profile};
    document::FeatureId feature_id;
    std::string output_hash;
    std::optional<PlanarProfile> profile;
    std::optional<sketch::SolveResult> sketch_result;
    std::optional<ExactBodyHandle> body;
    std::optional<BodySummary> body_summary;
    std::vector<topology_id::TopologyBinding> topology_bindings;
    std::vector<topology_id::ReconciliationDecision> topology_decisions;
    std::vector<topology_id::ReconciliationIssue> topology_issues;
    std::optional<topology_id::PersistentTopologyId> support_reference;
};

struct CadHydrationResult {
    std::vector<document::Diagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept { return diagnostics.empty(); }
};

class CadFeatureEvaluator final : public document::Evaluator {
public:
    explicit CadFeatureEvaluator(GeometryKernel& kernel);

    [[nodiscard]] std::string_view version() const noexcept override;
    [[nodiscard]] document::FeatureEvaluationResult evaluate_feature(
        const document::FeatureEvaluationRequest& request) override;
    [[nodiscard]] std::optional<std::string> compose_geometry(
        document::RevisionId revision_id,
        const document::SemanticState& semantic_state,
        std::span<const document::FeatureEvaluation> features) override;

    // Rebuilds the evaluator's transient kernel handles from persisted semantic
    // revisions and verifies them against the recorded evaluation hashes. This
    // is explicit so Document::restore remains a validation-only operation.
    [[nodiscard]] CadHydrationResult hydrate(const document::DocumentState& state);

    [[nodiscard]] const CadFeatureArtifact* artifact(std::string_view output_hash) const noexcept;
    [[nodiscard]] std::size_t evaluation_count(std::string_view feature_id) const noexcept;

private:
    [[nodiscard]] document::FeatureEvaluationResult evaluate_base_sketch(
        const document::FeatureEvaluationRequest& request);
    [[nodiscard]] document::FeatureEvaluationResult evaluate_base_extrude(
        const document::FeatureEvaluationRequest& request);
    [[nodiscard]] document::FeatureEvaluationResult evaluate_fillet(
        const document::FeatureEvaluationRequest& request);
    [[nodiscard]] document::FeatureEvaluationResult evaluate_hole_sketch(
        const document::FeatureEvaluationRequest& request);
    [[nodiscard]] document::FeatureEvaluationResult evaluate_cut(
        const document::FeatureEvaluationRequest& request);

    GeometryKernel* kernel_;
    std::string version_;
    std::map<std::string, CadFeatureArtifact, std::less<>> artifacts_;
    std::map<std::string, std::size_t, std::less<>> evaluation_counts_;
};

[[nodiscard]] const topology_id::TopologyBinding* binding_for_role(
    const CadFeatureArtifact& artifact,
    std::string_view semantic_role) noexcept;

} // namespace oep::modeler
