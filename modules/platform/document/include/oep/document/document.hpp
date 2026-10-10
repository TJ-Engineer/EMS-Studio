// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oep::document {

struct RevisionId {
    std::uint64_t value{};

    auto operator<=>(const RevisionId&) const = default;
};

struct FeatureId {
    std::string value;

    auto operator<=>(const FeatureId&) const = default;
};

using SemanticState = std::map<std::string, double, std::less<>>;

struct FeatureDependency {
    FeatureId upstream_feature_id;
    std::string input_name;

    bool operator==(const FeatureDependency&) const = default;
};

struct FeatureDefinition {
    FeatureId id;
    std::vector<std::string> parameter_ids;
    std::vector<FeatureDependency> dependencies;

    bool operator==(const FeatureDefinition&) const = default;
};

using FeatureGraph = std::vector<FeatureDefinition>;

enum class FeatureState {
    clean,
    dirty,
    evaluating,
    succeeded,
    failed,
    blocked,
};

[[nodiscard]] constexpr bool is_successful_feature_state(const FeatureState state) noexcept
{
    return state == FeatureState::clean || state == FeatureState::succeeded;
}

struct Diagnostic {
    std::string code;
    std::string message;
    std::optional<FeatureId> feature_id;
};

struct FeatureEvaluation {
    FeatureId feature_id;
    FeatureState state{FeatureState::dirty};
    std::optional<std::string> output_hash;
    std::vector<Diagnostic> diagnostics;
};

struct FeatureInput {
    FeatureId upstream_feature_id;
    std::string input_name;
    std::string output_hash;
};

struct FeatureEvaluationRequest {
    RevisionId revision_id;
    const SemanticState& semantic_state;
    const FeatureDefinition& feature;
    std::span<const FeatureInput> inputs;
};

struct FeatureEvaluationResult {
    std::optional<std::string> output_hash;
    std::vector<Diagnostic> diagnostics;
};

class Evaluator {
public:
    virtual ~Evaluator() = default;

    [[nodiscard]] virtual std::string_view version() const noexcept = 0;
    [[nodiscard]] virtual FeatureEvaluationResult evaluate_feature(
        const FeatureEvaluationRequest& request) = 0;
    [[nodiscard]] virtual std::optional<std::string> compose_geometry(
        RevisionId revision_id,
        const SemanticState& semantic_state,
        std::span<const FeatureEvaluation> features) = 0;
};

struct SetParameterCommand {
    RevisionId expected_revision;
    std::string parameter_id;
    double value{};
};

struct ParameterChange {
    std::string parameter_id;
    double value{};

    bool operator==(const ParameterChange&) const = default;
};

struct FeatureDependencyAddition {
    FeatureId upstream_feature_id;
    FeatureId downstream_feature_id;
    std::string input_name;

    bool operator==(const FeatureDependencyAddition&) const = default;
};

struct CommandTransaction {
    RevisionId expected_revision;
    std::vector<ParameterChange> parameter_changes;
    std::vector<FeatureDependencyAddition> dependency_additions;
};

struct CommittedTransaction {
    RevisionId base_revision_id;
    RevisionId result_revision_id;
    std::vector<ParameterChange> parameter_changes;
    std::vector<FeatureDependencyAddition> dependency_additions;
};

enum class CommandStatus {
    rejected,
    committed,
};

struct CommandResult {
    CommandStatus status{CommandStatus::rejected};
    RevisionId semantic_revision;
    bool evaluation_succeeded{};
    std::vector<Diagnostic> diagnostics;
};

struct GeometrySnapshot {
    RevisionId revision_id;
    std::string content_hash;
};

struct GeometryAccess {
    std::optional<GeometrySnapshot> geometry;
    std::optional<Diagnostic> error;

    [[nodiscard]] bool available() const noexcept { return geometry.has_value(); }
};

struct DocumentRevision {
    RevisionId id;
    std::optional<RevisionId> parent_id;
    SemanticState semantic_state;
    FeatureGraph feature_graph;
};

struct EvaluationRecord {
    RevisionId revision_id;
    std::string evaluator_version;
    std::vector<FeatureEvaluation> features;
    std::vector<Diagnostic> diagnostics;
    std::optional<std::string> geometry_hash;

    [[nodiscard]] bool succeeded() const noexcept
    {
        if (!geometry_hash.has_value()) {
            return false;
        }
        for (const auto& feature : features) {
            if (!is_successful_feature_state(feature.state)) {
                return false;
            }
        }
        return true;
    }
};

struct DocumentState {
    std::vector<DocumentRevision> revisions;
    std::vector<RevisionId> timeline;
    std::size_t timeline_cursor{};
    std::vector<CommittedTransaction> command_journal;
    std::vector<EvaluationRecord> evaluations;
    std::optional<RevisionId> evaluated_revision_id;
    std::optional<GeometrySnapshot> last_good_geometry;
    std::uint64_t next_revision_value{1};
};

class Document {
public:
    Document(SemanticState initial_state, FeatureGraph feature_graph, Evaluator& evaluator);

    [[nodiscard]] static Document restore(DocumentState state, Evaluator& evaluator);

    [[nodiscard]] RevisionId semantic_head_revision_id() const noexcept;
    [[nodiscard]] std::optional<RevisionId> evaluated_revision_id() const noexcept;
    [[nodiscard]] const SemanticState& semantic_state() const;
    [[nodiscard]] const FeatureGraph& feature_graph() const;
    [[nodiscard]] const EvaluationRecord& current_evaluation() const;
    [[nodiscard]] GeometryAccess current_geometry() const;
    [[nodiscard]] const std::vector<CommittedTransaction>& command_journal() const noexcept;

    [[nodiscard]] bool can_undo() const noexcept;
    [[nodiscard]] bool can_redo() const noexcept;
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();

    [[nodiscard]] CommandResult apply(const SetParameterCommand& command);
    [[nodiscard]] CommandResult apply(const CommandTransaction& transaction);
    [[nodiscard]] DocumentState snapshot() const;

private:
    struct RestoreTag {};

    Document(DocumentState state, Evaluator& evaluator, RestoreTag);

    [[nodiscard]] const DocumentRevision& current_revision() const;
    [[nodiscard]] const DocumentRevision& revision(RevisionId revision_id) const;
    [[nodiscard]] const EvaluationRecord& evaluation(RevisionId revision_id) const;
    [[nodiscard]] EvaluationRecord run_evaluation(
        const DocumentRevision& revision,
        std::optional<RevisionId> baseline_revision_id);
    void activate_revision(RevisionId revision_id, RevisionId baseline_revision_id);
    void select_last_good_ancestor(RevisionId revision_id);
    void validate_restored_state() const;

    Evaluator* evaluator_;
    DocumentState state_;
};

} // namespace oep::document
