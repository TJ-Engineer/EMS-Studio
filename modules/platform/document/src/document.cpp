#include <oep/document/document.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace oep::document {
namespace {

constexpr std::string_view current_geometry_unavailable = "CurrentGeometryUnavailable";

[[nodiscard]] Diagnostic make_diagnostic(
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

void validate_semantic_state(const SemanticState& semantic_state)
{
    if (semantic_state.empty()) {
        throw std::invalid_argument("a document revision requires semantic parameters");
    }

    for (const auto& [parameter_id, value] : semantic_state) {
        if (parameter_id.empty()) {
            throw std::invalid_argument("semantic parameter IDs must not be empty");
        }
        if (!std::isfinite(value)) {
            throw std::invalid_argument("semantic parameter values must be finite");
        }
    }
}

[[nodiscard]] const FeatureDefinition* find_feature(
    const FeatureGraph& graph,
    const FeatureId& feature_id)
{
    const auto found = std::ranges::find_if(
        graph,
        [&feature_id](const FeatureDefinition& feature) { return feature.id == feature_id; });
    return found == graph.end() ? nullptr : &*found;
}

[[nodiscard]] FeatureDefinition* find_feature(
    FeatureGraph& graph,
    const FeatureId& feature_id)
{
    const auto found = std::ranges::find_if(
        graph,
        [&feature_id](const FeatureDefinition& feature) { return feature.id == feature_id; });
    return found == graph.end() ? nullptr : &*found;
}

[[nodiscard]] std::vector<const FeatureDefinition*> topological_order(
    const FeatureGraph& graph)
{
    std::map<std::string, const FeatureDefinition*, std::less<>> features;
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
    for (const auto& [feature_id, indegree] : indegrees) {
        if (indegree == 0) {
            ready.insert(feature_id);
        }
    }

    std::vector<const FeatureDefinition*> result;
    result.reserve(graph.size());
    while (!ready.empty()) {
        auto ready_node = ready.extract(ready.begin());
        auto feature_id = std::move(ready_node.value());
        result.push_back(features.at(feature_id));
        for (const auto& dependent_id : dependents[feature_id]) {
            auto& indegree = indegrees.at(dependent_id);
            --indegree;
            if (indegree == 0) {
                ready.insert(dependent_id);
            }
        }
    }
    return result;
}

[[nodiscard]] std::vector<Diagnostic> feature_graph_diagnostics(
    const FeatureGraph& graph,
    const SemanticState& semantic_state)
{
    std::vector<Diagnostic> diagnostics;
    if (graph.empty()) {
        diagnostics.push_back(make_diagnostic(
            "EmptyFeatureGraph", "a document requires at least one feature"));
        return diagnostics;
    }

    std::unordered_set<std::string> feature_ids;
    std::unordered_set<std::string> owned_parameters;
    auto identifiers_are_valid = true;
    for (const auto& feature : graph) {
        if (feature.id.value.empty()) {
            diagnostics.push_back(make_diagnostic(
                "EmptyFeatureId", "a feature requires a stable identifier"));
            identifiers_are_valid = false;
        } else if (!feature_ids.insert(feature.id.value).second) {
            diagnostics.push_back(make_diagnostic(
                "DuplicateFeatureId",
                "feature identifiers must be unique",
                feature.id));
            identifiers_are_valid = false;
        }

        std::unordered_set<std::string> local_parameters;
        for (const auto& parameter_id : feature.parameter_ids) {
            if (parameter_id.empty() || !semantic_state.contains(parameter_id)) {
                diagnostics.push_back(make_diagnostic(
                    "UnknownFeatureParameter",
                    "a feature parameter must identify a document parameter",
                    feature.id));
            } else if (!local_parameters.insert(parameter_id).second
                || !owned_parameters.insert(parameter_id).second) {
                diagnostics.push_back(make_diagnostic(
                    "DuplicateFeatureParameter",
                    "each parameter must be owned by exactly one feature",
                    feature.id));
            }
        }
    }

    for (const auto& [parameter_id, unused_value] : semantic_state) {
        static_cast<void>(unused_value);
        if (!owned_parameters.contains(parameter_id)) {
            diagnostics.push_back(make_diagnostic(
                "UnownedParameter",
                "each document parameter must belong to a feature"));
        }
    }

    if (!identifiers_are_valid) {
        return diagnostics;
    }

    for (const auto& feature : graph) {
        std::unordered_set<std::string> input_names;
        for (const auto& dependency : feature.dependencies) {
            if (dependency.input_name.empty()) {
                diagnostics.push_back(make_diagnostic(
                    "EmptyFeatureInput",
                    "a feature dependency requires a typed input name",
                    feature.id));
            } else if (!input_names.insert(dependency.input_name).second) {
                diagnostics.push_back(make_diagnostic(
                    "DuplicateFeatureInput",
                    "feature input names must be unique within a feature",
                    feature.id));
            }
            if (!feature_ids.contains(dependency.upstream_feature_id.value)) {
                diagnostics.push_back(make_diagnostic(
                    "UnknownDependencyFeature",
                    "a dependency must identify an existing upstream feature",
                    feature.id));
            }
        }
    }

    if (diagnostics.empty() && topological_order(graph).size() != graph.size()) {
        diagnostics.push_back(make_diagnostic(
            "FeatureCycle", "feature dependencies must form a directed acyclic graph"));
    }
    return diagnostics;
}

void validate_document_model(
    const SemanticState& semantic_state,
    const FeatureGraph& feature_graph)
{
    validate_semantic_state(semantic_state);
    const auto diagnostics = feature_graph_diagnostics(feature_graph, semantic_state);
    if (!diagnostics.empty()) {
        throw std::invalid_argument(diagnostics.front().message);
    }
}

[[nodiscard]] const FeatureEvaluation* find_evaluation(
    const std::vector<FeatureEvaluation>& evaluations,
    const FeatureId& feature_id)
{
    const auto found = std::ranges::find_if(
        evaluations,
        [&feature_id](const FeatureEvaluation& evaluation) {
            return evaluation.feature_id == feature_id;
        });
    return found == evaluations.end() ? nullptr : &*found;
}

[[nodiscard]] std::set<std::string, std::less<>> dirty_features(
    const DocumentRevision& target,
    const DocumentRevision* baseline)
{
    std::set<std::string, std::less<>> dirty;
    if (baseline == nullptr) {
        for (const auto& feature : target.feature_graph) {
            dirty.insert(feature.id.value);
        }
    } else {
        for (const auto& [parameter_id, value] : target.semantic_state) {
            const auto baseline_value = baseline->semantic_state.find(parameter_id);
            if (baseline_value == baseline->semantic_state.end()
                || baseline_value->second != value) {
                for (const auto& feature : target.feature_graph) {
                    if (std::ranges::find(feature.parameter_ids, parameter_id)
                        != feature.parameter_ids.end()) {
                        dirty.insert(feature.id.value);
                    }
                }
            }
        }

        for (const auto& feature : target.feature_graph) {
            const auto* baseline_feature = find_feature(baseline->feature_graph, feature.id);
            if (baseline_feature == nullptr || *baseline_feature != feature) {
                dirty.insert(feature.id.value);
            }
        }
    }

    std::map<std::string, std::vector<std::string>, std::less<>> dependents;
    for (const auto& feature : target.feature_graph) {
        for (const auto& dependency : feature.dependencies) {
            dependents[dependency.upstream_feature_id.value].push_back(feature.id.value);
        }
    }

    std::vector<std::string> pending(dirty.begin(), dirty.end());
    for (std::size_t index = 0; index < pending.size(); ++index) {
        for (const auto& dependent_id : dependents[pending[index]]) {
            if (dirty.insert(dependent_id).second) {
                pending.push_back(dependent_id);
            }
        }
    }
    return dirty;
}

[[nodiscard]] bool has_diagnostic_code(
    const std::vector<Diagnostic>& diagnostics,
    const std::string_view code)
{
    return std::ranges::any_of(
        diagnostics,
        [code](const Diagnostic& diagnostic) { return diagnostic.code == code; });
}

} // namespace

Document::Document(
    SemanticState initial_state,
    FeatureGraph feature_graph,
    Evaluator& evaluator)
    : evaluator_(&evaluator)
{
    validate_document_model(initial_state, feature_graph);

    DocumentRevision initial_revision{
        .id = RevisionId{1},
        .parent_id = std::nullopt,
        .semantic_state = std::move(initial_state),
        .feature_graph = std::move(feature_graph),
    };

    state_.revisions.push_back(std::move(initial_revision));
    state_.timeline.push_back(RevisionId{1});
    state_.timeline_cursor = 0;
    state_.next_revision_value = 2;

    auto record = run_evaluation(state_.revisions.front(), std::nullopt);
    if (!record.succeeded()) {
        throw std::invalid_argument("the initial document state must evaluate successfully");
    }

    state_.evaluated_revision_id = RevisionId{1};
    state_.last_good_geometry = GeometrySnapshot{
        .revision_id = RevisionId{1},
        .content_hash = *record.geometry_hash,
    };
    state_.evaluations.push_back(std::move(record));
}

Document::Document(DocumentState state, Evaluator& evaluator, RestoreTag)
    : evaluator_(&evaluator)
    , state_(std::move(state))
{
    validate_restored_state();
}

Document Document::restore(DocumentState state, Evaluator& evaluator)
{
    return Document(std::move(state), evaluator, RestoreTag{});
}

RevisionId Document::semantic_head_revision_id() const noexcept
{
    return state_.timeline[state_.timeline_cursor];
}

std::optional<RevisionId> Document::evaluated_revision_id() const noexcept
{
    return state_.evaluated_revision_id;
}

const SemanticState& Document::semantic_state() const
{
    return current_revision().semantic_state;
}

const FeatureGraph& Document::feature_graph() const
{
    return current_revision().feature_graph;
}

const EvaluationRecord& Document::current_evaluation() const
{
    return evaluation(semantic_head_revision_id());
}

GeometryAccess Document::current_geometry() const
{
    const auto head = semantic_head_revision_id();
    if (state_.evaluated_revision_id == head && state_.last_good_geometry.has_value()
        && state_.last_good_geometry->revision_id == head) {
        return GeometryAccess{
            .geometry = state_.last_good_geometry,
            .error = std::nullopt,
        };
    }

    return GeometryAccess{
        .geometry = std::nullopt,
        .error = make_diagnostic(
            std::string(current_geometry_unavailable),
            "the semantic head does not have matching successfully evaluated geometry"),
    };
}

const std::vector<CommittedTransaction>& Document::command_journal() const noexcept
{
    return state_.command_journal;
}

bool Document::can_undo() const noexcept
{
    return state_.timeline_cursor > 0;
}

bool Document::can_redo() const noexcept
{
    return state_.timeline_cursor + 1 < state_.timeline.size();
}

bool Document::undo()
{
    if (!can_undo()) {
        return false;
    }

    const auto baseline_revision_id = semantic_head_revision_id();
    --state_.timeline_cursor;
    activate_revision(semantic_head_revision_id(), baseline_revision_id);
    return true;
}

bool Document::redo()
{
    if (!can_redo()) {
        return false;
    }

    const auto baseline_revision_id = semantic_head_revision_id();
    ++state_.timeline_cursor;
    activate_revision(semantic_head_revision_id(), baseline_revision_id);
    return true;
}

CommandResult Document::apply(const SetParameterCommand& command)
{
    return apply(CommandTransaction{
        .expected_revision = command.expected_revision,
        .parameter_changes = {ParameterChange{
            .parameter_id = command.parameter_id,
            .value = command.value,
        }},
        .dependency_additions = {},
    });
}

CommandResult Document::apply(const CommandTransaction& transaction)
{
    std::vector<Diagnostic> validation_diagnostics;

    if (transaction.expected_revision != semantic_head_revision_id()) {
        validation_diagnostics.push_back(make_diagnostic(
            "StaleCommand",
            "the command target revision is not the current semantic head"));
    }
    if (transaction.parameter_changes.empty() && transaction.dependency_additions.empty()) {
        validation_diagnostics.push_back(make_diagnostic(
            "EmptyTransaction",
            "a command transaction requires at least one semantic change"));
    }

    auto next_state = semantic_state();
    auto next_graph = feature_graph();
    auto has_effective_change = false;
    std::unordered_set<std::string> changed_parameters;
    for (const auto& change : transaction.parameter_changes) {
        if (change.parameter_id.empty()) {
            validation_diagnostics.push_back(make_diagnostic(
                "EmptyParameterId",
                "a parameter change requires a stable parameter identifier"));
            continue;
        }
        if (!changed_parameters.insert(change.parameter_id).second) {
            validation_diagnostics.push_back(make_diagnostic(
                "DuplicateParameterChange",
                "a transaction may change a parameter only once"));
        }
        const auto current_value = semantic_state().find(change.parameter_id);
        if (current_value == semantic_state().end()) {
            validation_diagnostics.push_back(make_diagnostic(
                "UnknownParameter",
                "the target parameter does not exist in the current semantic revision"));
        } else if (current_value->second != change.value) {
            has_effective_change = true;
            next_state[change.parameter_id] = change.value;
        }
        if (!std::isfinite(change.value)) {
            validation_diagnostics.push_back(make_diagnostic(
                "NonFiniteParameterValue", "a parameter value must be finite"));
        }
    }

    std::set<std::pair<std::string, std::string>> added_inputs;
    for (const auto& addition : transaction.dependency_additions) {
        const auto input_key = std::pair{
            addition.downstream_feature_id.value, addition.input_name};
        if (!added_inputs.insert(input_key).second) {
            validation_diagnostics.push_back(make_diagnostic(
                "DuplicateDependencyAddition",
                "a transaction may add a feature input only once",
                addition.downstream_feature_id));
            continue;
        }
        if (addition.input_name.empty()) {
            validation_diagnostics.push_back(make_diagnostic(
                "EmptyFeatureInput",
                "a feature dependency requires a typed input name",
                addition.downstream_feature_id));
            continue;
        }
        if (find_feature(next_graph, addition.upstream_feature_id) == nullptr) {
            validation_diagnostics.push_back(make_diagnostic(
                "UnknownDependencyFeature",
                "the upstream feature does not exist",
                addition.upstream_feature_id));
            continue;
        }
        auto* downstream = find_feature(next_graph, addition.downstream_feature_id);
        if (downstream == nullptr) {
            validation_diagnostics.push_back(make_diagnostic(
                "UnknownDependencyFeature",
                "the downstream feature does not exist",
                addition.downstream_feature_id));
            continue;
        }

        const auto existing = std::ranges::find_if(
            downstream->dependencies,
            [&addition](const FeatureDependency& dependency) {
                return dependency.input_name == addition.input_name;
            });
        if (existing != downstream->dependencies.end()) {
            if (existing->upstream_feature_id == addition.upstream_feature_id) {
                validation_diagnostics.push_back(make_diagnostic(
                    "NoEffectiveChange",
                    "the feature dependency already exists",
                    addition.downstream_feature_id));
            } else {
                validation_diagnostics.push_back(make_diagnostic(
                    "DuplicateFeatureInput",
                    "the feature input is already connected",
                    addition.downstream_feature_id));
            }
            continue;
        }

        downstream->dependencies.push_back(FeatureDependency{
            .upstream_feature_id = addition.upstream_feature_id,
            .input_name = addition.input_name,
        });
        has_effective_change = true;
    }

    if (validation_diagnostics.empty()) {
        auto graph_diagnostics = feature_graph_diagnostics(next_graph, next_state);
        validation_diagnostics.insert(
            validation_diagnostics.end(),
            std::make_move_iterator(graph_diagnostics.begin()),
            std::make_move_iterator(graph_diagnostics.end()));
    }
    if (!has_effective_change && !has_diagnostic_code(validation_diagnostics, "NoEffectiveChange")) {
        validation_diagnostics.push_back(make_diagnostic(
            "NoEffectiveChange",
            "the transaction does not change the current semantic state"));
    }

    if (!validation_diagnostics.empty()) {
        return CommandResult{
            .status = CommandStatus::rejected,
            .semantic_revision = semantic_head_revision_id(),
            .evaluation_succeeded = current_evaluation().succeeded(),
            .diagnostics = std::move(validation_diagnostics),
        };
    }

    const auto base_revision_id = semantic_head_revision_id();
    const auto next_revision_id = RevisionId{state_.next_revision_value};
    state_.revisions.push_back(DocumentRevision{
        .id = next_revision_id,
        .parent_id = base_revision_id,
        .semantic_state = std::move(next_state),
        .feature_graph = std::move(next_graph),
    });
    state_.command_journal.push_back(CommittedTransaction{
        .base_revision_id = base_revision_id,
        .result_revision_id = next_revision_id,
        .parameter_changes = transaction.parameter_changes,
        .dependency_additions = transaction.dependency_additions,
    });
    ++state_.next_revision_value;

    state_.timeline.resize(state_.timeline_cursor + 1);
    state_.timeline.push_back(next_revision_id);
    state_.timeline_cursor = state_.timeline.size() - 1;

    auto record = run_evaluation(state_.revisions.back(), base_revision_id);
    const auto succeeded = record.succeeded();
    auto diagnostics = record.diagnostics;
    for (const auto& feature : record.features) {
        diagnostics.insert(
            diagnostics.end(), feature.diagnostics.begin(), feature.diagnostics.end());
    }

    state_.evaluations.push_back(std::move(record));
    select_last_good_ancestor(next_revision_id);

    return CommandResult{
        .status = CommandStatus::committed,
        .semantic_revision = next_revision_id,
        .evaluation_succeeded = succeeded,
        .diagnostics = std::move(diagnostics),
    };
}

DocumentState Document::snapshot() const
{
    return state_;
}

const DocumentRevision& Document::current_revision() const
{
    return revision(semantic_head_revision_id());
}

const DocumentRevision& Document::revision(const RevisionId revision_id) const
{
    const auto found = std::ranges::find(
        state_.revisions, revision_id, &DocumentRevision::id);
    if (found == state_.revisions.end()) {
        throw std::logic_error("document timeline references a missing revision");
    }
    return *found;
}

const EvaluationRecord& Document::evaluation(const RevisionId revision_id) const
{
    const auto found = std::ranges::find(
        state_.evaluations, revision_id, &EvaluationRecord::revision_id);
    if (found == state_.evaluations.end()) {
        throw std::logic_error("document revision is missing its evaluation record");
    }
    return *found;
}

EvaluationRecord Document::run_evaluation(
    const DocumentRevision& revision_to_evaluate,
    const std::optional<RevisionId> baseline_revision_id)
{
    const auto* baseline_revision = baseline_revision_id.has_value()
        ? &revision(*baseline_revision_id)
        : nullptr;
    const auto* baseline_evaluation = baseline_revision_id.has_value()
        ? &evaluation(*baseline_revision_id)
        : nullptr;
    const auto dirty = dirty_features(revision_to_evaluate, baseline_revision);
    const auto schedule = topological_order(revision_to_evaluate.feature_graph);

    EvaluationRecord record{
        .revision_id = revision_to_evaluate.id,
        .evaluator_version = std::string(evaluator_->version()),
        .features = {},
        .diagnostics = {},
        .geometry_hash = std::nullopt,
    };
    record.features.reserve(schedule.size());

    for (const auto* feature : schedule) {
        if (!dirty.contains(feature->id.value)) {
            const auto* previous = find_evaluation(baseline_evaluation->features, feature->id);
            if (previous == nullptr) {
                throw std::logic_error("baseline evaluation is missing a feature result");
            }
            auto preserved = *previous;
            if (is_successful_feature_state(preserved.state)) {
                preserved.state = FeatureState::clean;
            }
            record.features.push_back(std::move(preserved));
            continue;
        }

        std::vector<FeatureInput> inputs;
        auto upstream_failed = false;
        for (const auto& dependency : feature->dependencies) {
            const auto* upstream = find_evaluation(
                record.features, dependency.upstream_feature_id);
            if (upstream == nullptr) {
                throw std::logic_error("feature schedule did not evaluate an upstream input");
            }
            if (!is_successful_feature_state(upstream->state)
                || !upstream->output_hash.has_value()) {
                upstream_failed = true;
                continue;
            }
            inputs.push_back(FeatureInput{
                .upstream_feature_id = dependency.upstream_feature_id,
                .input_name = dependency.input_name,
                .output_hash = *upstream->output_hash,
            });
        }
        std::ranges::sort(
            inputs,
            {},
            [](const FeatureInput& input) {
                return std::pair{input.input_name, input.upstream_feature_id.value};
            });

        if (upstream_failed) {
            record.features.push_back(FeatureEvaluation{
                .feature_id = feature->id,
                .state = FeatureState::blocked,
                .output_hash = std::nullopt,
                .diagnostics = {make_diagnostic(
                    "UpstreamFeatureFailed",
                    "one or more required upstream features did not produce output",
                    feature->id)},
            });
            continue;
        }

        FeatureEvaluationResult result;
        try {
            result = evaluator_->evaluate_feature(FeatureEvaluationRequest{
                .revision_id = revision_to_evaluate.id,
                .semantic_state = revision_to_evaluate.semantic_state,
                .feature = *feature,
                .inputs = inputs,
            });
        } catch (const std::exception& error) {
            result.diagnostics.push_back(make_diagnostic(
                "FeatureEvaluationException", error.what(), feature->id));
        } catch (...) {
            result.diagnostics.push_back(make_diagnostic(
                "FeatureEvaluationException",
                "feature evaluation raised an unknown exception",
                feature->id));
        }

        if (result.output_hash.has_value() && result.output_hash->empty()) {
            result.output_hash.reset();
            result.diagnostics.push_back(make_diagnostic(
                "EvaluatorContractViolation",
                "a successful feature evaluation requires a nonempty output hash",
                feature->id));
        }
        if (!result.output_hash.has_value() && result.diagnostics.empty()) {
            result.diagnostics.push_back(make_diagnostic(
                "FeatureEvaluationFailed",
                "the feature did not produce output",
                feature->id));
        }
        for (auto& diagnostic : result.diagnostics) {
            if (!diagnostic.feature_id.has_value()) {
                diagnostic.feature_id = feature->id;
            }
        }

        record.features.push_back(FeatureEvaluation{
            .feature_id = feature->id,
            .state = result.output_hash.has_value() ? FeatureState::succeeded
                                                    : FeatureState::failed,
            .output_hash = std::move(result.output_hash),
            .diagnostics = std::move(result.diagnostics),
        });
    }

    const auto features_succeeded = std::ranges::all_of(
        record.features,
        [](const FeatureEvaluation& feature) {
            return is_successful_feature_state(feature.state);
        });
    if (features_succeeded) {
        try {
            record.geometry_hash = evaluator_->compose_geometry(
                revision_to_evaluate.id,
                revision_to_evaluate.semantic_state,
                record.features);
        } catch (const std::exception& error) {
            record.diagnostics.push_back(make_diagnostic(
                "GeometryCompositionException", error.what()));
        } catch (...) {
            record.diagnostics.push_back(make_diagnostic(
                "GeometryCompositionException",
                "geometry composition raised an unknown exception"));
        }
        if (!record.geometry_hash.has_value() || record.geometry_hash->empty()) {
            record.geometry_hash.reset();
            record.diagnostics.push_back(make_diagnostic(
                "EvaluatorContractViolation",
                "a successful graph evaluation must provide a geometry hash"));
        }
    }
    return record;
}

void Document::activate_revision(
    const RevisionId revision_id,
    const RevisionId baseline_revision_id)
{
    auto record = run_evaluation(revision(revision_id), baseline_revision_id);

    const auto existing = std::ranges::find(
        state_.evaluations, revision_id, &EvaluationRecord::revision_id);
    if (existing == state_.evaluations.end()) {
        state_.evaluations.push_back(std::move(record));
    } else {
        *existing = std::move(record);
    }

    select_last_good_ancestor(revision_id);
}

void Document::select_last_good_ancestor(RevisionId revision_id)
{
    auto candidate = std::optional<RevisionId>{revision_id};
    while (candidate.has_value()) {
        const auto& record = evaluation(*candidate);
        if (record.succeeded()) {
            state_.evaluated_revision_id = *candidate;
            state_.last_good_geometry = GeometrySnapshot{
                .revision_id = *candidate,
                .content_hash = *record.geometry_hash,
            };
            return;
        }
        candidate = revision(*candidate).parent_id;
    }

    state_.evaluated_revision_id.reset();
    state_.last_good_geometry.reset();
}

void Document::validate_restored_state() const
{
    if (state_.revisions.empty() || state_.timeline.empty()) {
        throw std::invalid_argument("a restored document requires revisions and a timeline");
    }
    if (state_.timeline_cursor >= state_.timeline.size()) {
        throw std::invalid_argument("the restored timeline cursor is out of range");
    }

    std::unordered_set<std::uint64_t> revision_ids;
    std::uint64_t maximum_revision_id = 0;
    for (const auto& item : state_.revisions) {
        if (item.id.value == 0 || !revision_ids.insert(item.id.value).second) {
            throw std::invalid_argument("restored revision IDs must be unique and nonzero");
        }
        validate_document_model(item.semantic_state, item.feature_graph);
        maximum_revision_id = std::max(maximum_revision_id, item.id.value);
    }
    if (state_.next_revision_value <= maximum_revision_id) {
        throw std::invalid_argument("the restored next revision ID is not monotonic");
    }

    std::unordered_set<std::uint64_t> evaluation_ids;
    for (const auto& record : state_.evaluations) {
        if (!revision_ids.contains(record.revision_id.value)
            || !evaluation_ids.insert(record.revision_id.value).second) {
            throw std::invalid_argument(
                "evaluation records must refer uniquely to existing revisions");
        }
        const auto& graph = revision(record.revision_id).feature_graph;
        if (record.evaluator_version.empty() || record.features.size() != graph.size()) {
            throw std::invalid_argument(
                "evaluation records must contain one result for every feature");
        }

        std::unordered_set<std::string> evaluated_features;
        auto features_succeeded = true;
        for (const auto& feature : record.features) {
            if (find_feature(graph, feature.feature_id) == nullptr
                || !evaluated_features.insert(feature.feature_id.value).second
                || feature.state == FeatureState::dirty
                || feature.state == FeatureState::evaluating) {
                throw std::invalid_argument(
                    "evaluation records contain an invalid feature result");
            }
            const auto successful = is_successful_feature_state(feature.state);
            if (successful != feature.output_hash.has_value()
                || (feature.output_hash.has_value() && feature.output_hash->empty())) {
                throw std::invalid_argument(
                    "feature state and output hash do not agree");
            }
            features_succeeded = features_succeeded && successful;
        }
        if (record.geometry_hash.has_value() != features_succeeded
            || (record.geometry_hash.has_value() && record.geometry_hash->empty())) {
            throw std::invalid_argument(
                "evaluation geometry does not agree with feature states");
        }
    }
    if (evaluation_ids.size() != revision_ids.size()) {
        throw std::invalid_argument("every restored revision requires one evaluation record");
    }

    if (state_.command_journal.size() + 1 != state_.revisions.size()) {
        throw std::invalid_argument(
            "every non-root revision requires one committed transaction");
    }

    std::unordered_set<std::uint64_t> journal_result_ids;
    for (const auto& transaction : state_.command_journal) {
        if (!revision_ids.contains(transaction.base_revision_id.value)
            || !revision_ids.contains(transaction.result_revision_id.value)
            || !journal_result_ids.insert(transaction.result_revision_id.value).second) {
            throw std::invalid_argument(
                "journal transactions must refer uniquely to existing revisions");
        }

        const auto& base = revision(transaction.base_revision_id);
        const auto& result = revision(transaction.result_revision_id);
        if (result.parent_id != transaction.base_revision_id
            || (transaction.parameter_changes.empty()
                && transaction.dependency_additions.empty())) {
            throw std::invalid_argument(
                "journal transactions must connect a parent to its child revision");
        }

        auto replayed_state = base.semantic_state;
        auto replayed_graph = base.feature_graph;
        std::unordered_set<std::string> changed_parameters;
        for (const auto& change : transaction.parameter_changes) {
            if (change.parameter_id.empty() || !std::isfinite(change.value)
                || !replayed_state.contains(change.parameter_id)
                || !changed_parameters.insert(change.parameter_id).second) {
                throw std::invalid_argument(
                    "journal transaction contains an invalid parameter change");
            }
            replayed_state[change.parameter_id] = change.value;
        }
        std::set<std::pair<std::string, std::string>> added_inputs;
        for (const auto& addition : transaction.dependency_additions) {
            auto* downstream = find_feature(replayed_graph, addition.downstream_feature_id);
            if (downstream == nullptr
                || find_feature(replayed_graph, addition.upstream_feature_id) == nullptr
                || addition.input_name.empty()
                || !added_inputs.emplace(
                       addition.downstream_feature_id.value, addition.input_name)
                        .second
                || std::ranges::any_of(
                    downstream->dependencies,
                    [&addition](const FeatureDependency& dependency) {
                        return dependency.input_name == addition.input_name;
                    })) {
                throw std::invalid_argument(
                    "journal transaction contains an invalid dependency addition");
            }
            downstream->dependencies.push_back(FeatureDependency{
                .upstream_feature_id = addition.upstream_feature_id,
                .input_name = addition.input_name,
            });
        }
        if ((replayed_state == base.semantic_state && replayed_graph == base.feature_graph)
            || replayed_state != result.semantic_state
            || replayed_graph != result.feature_graph) {
            throw std::invalid_argument(
                "journal transaction does not reproduce its result revision");
        }
    }

    std::optional<RevisionId> root_revision_id;
    for (const auto& item : state_.revisions) {
        if (item.parent_id.has_value()) {
            if (!revision_ids.contains(item.parent_id->value)
                || item.parent_id->value >= item.id.value) {
                throw std::invalid_argument(
                    "revision parents must exist and precede their children");
            }
        } else if (root_revision_id.has_value()) {
            throw std::invalid_argument("a restored document must have exactly one root revision");
        } else {
            root_revision_id = item.id;
        }
    }
    if (!root_revision_id.has_value() || *root_revision_id != state_.timeline.front()) {
        throw std::invalid_argument("the active timeline must start at the document root");
    }

    std::unordered_set<std::uint64_t> timeline_ids;
    for (std::size_t index = 0; index < state_.timeline.size(); ++index) {
        const auto revision_id = state_.timeline[index];
        const auto& item = revision(revision_id);
        if (!timeline_ids.insert(revision_id.value).second) {
            throw std::invalid_argument("the active timeline cannot repeat a revision");
        }
        if (index == 0 && item.parent_id.has_value()) {
            throw std::invalid_argument("the active timeline must begin at a root revision");
        }
        if (index > 0 && item.parent_id != state_.timeline[index - 1]) {
            throw std::invalid_argument("the active timeline must follow revision parent links");
        }
        static_cast<void>(evaluation(revision_id));
    }

    if (state_.evaluated_revision_id.has_value() != state_.last_good_geometry.has_value()) {
        throw std::invalid_argument(
            "evaluated revision and last-good geometry must either both exist or both be absent");
    }

    if (state_.last_good_geometry.has_value()) {
        const auto& record = evaluation(state_.last_good_geometry->revision_id);
        if (!record.succeeded()
            || *record.geometry_hash != state_.last_good_geometry->content_hash) {
            throw std::invalid_argument(
                "last-good geometry does not match its evaluation record");
        }
    }

    if (state_.evaluated_revision_id.has_value()) {
        const auto& record = evaluation(*state_.evaluated_revision_id);
        if (!record.succeeded()) {
            throw std::invalid_argument(
                "evaluated revision ID refers to a failed evaluation");
        }
        if (state_.last_good_geometry->revision_id != *state_.evaluated_revision_id) {
            throw std::invalid_argument(
                "evaluated revision and last-good geometry revision do not match");
        }
    }

    auto nearest_success = std::optional<RevisionId>{semantic_head_revision_id()};
    while (nearest_success.has_value() && !evaluation(*nearest_success).succeeded()) {
        nearest_success = revision(*nearest_success).parent_id;
    }
    if (nearest_success != state_.evaluated_revision_id) {
        throw std::invalid_argument(
            "evaluated revision is not the nearest successful ancestor of the semantic head");
    }
}

} // namespace oep::document
