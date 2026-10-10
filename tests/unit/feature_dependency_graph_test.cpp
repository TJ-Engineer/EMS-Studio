#include <oep/document/document.hpp>
#include <oep/persistence/checkpoint.hpp>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using oep::document::CommandStatus;
using oep::document::CommandTransaction;
using oep::document::Diagnostic;
using oep::document::Document;
using oep::document::Evaluator;
using oep::document::FeatureDefinition;
using oep::document::FeatureDependency;
using oep::document::FeatureDependencyAddition;
using oep::document::FeatureEvaluation;
using oep::document::FeatureEvaluationRequest;
using oep::document::FeatureEvaluationResult;
using oep::document::FeatureGraph;
using oep::document::FeatureId;
using oep::document::FeatureState;
using oep::document::ParameterChange;
using oep::document::RevisionId;
using oep::document::SemanticState;
using oep::document::SetParameterCommand;
using oep::persistence::load_document_checkpoint;
using oep::persistence::save_document_checkpoint_atomic;

class GraphEvaluator final : public Evaluator {
public:
    [[nodiscard]] std::string_view version() const noexcept override
    {
        return "feature-graph-test-v1";
    }

    [[nodiscard]] FeatureEvaluationResult evaluate_feature(
        const FeatureEvaluationRequest& request) override
    {
        calls.push_back(request.feature.id.value);
        if (request.feature.id == FeatureId{"B"}
            && request.semantic_state.at("b") < 0.0) {
            return FeatureEvaluationResult{
                .output_hash = std::nullopt,
                .diagnostics = {Diagnostic{
                    .code = "FeatureBFailed",
                    .message = "B requires a nonnegative parameter",
                    .feature_id = request.feature.id,
                }},
            };
        }

        auto hash = request.feature.id.value;
        for (const auto& parameter_id : request.feature.parameter_ids) {
            hash += ':' + std::to_string(request.semantic_state.at(parameter_id));
        }
        for (const auto& input : request.inputs) {
            hash += ':' + input.input_name + '=' + input.output_hash;
        }
        return FeatureEvaluationResult{
            .output_hash = std::move(hash),
            .diagnostics = {},
        };
    }

    [[nodiscard]] std::optional<std::string> compose_geometry(
        const RevisionId revision_id,
        const SemanticState&,
        const std::span<const FeatureEvaluation> features) override
    {
        auto hash = "geometry:" + std::to_string(revision_id.value);
        for (const auto& feature : features) {
            hash += ':' + feature.feature_id.value + '=' + *feature.output_hash;
        }
        return hash;
    }

    void clear() { calls.clear(); }

    std::vector<std::string> calls;
};

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        std::random_device random_source;
        for (int attempt = 0; attempt < 16; ++attempt) {
            path_ = std::filesystem::temp_directory_path()
                / ("oep-feature-graph-test-" + std::to_string(random_source()));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
            if (error && error != std::errc::file_exists) {
                throw std::system_error(error, "cannot create test directory");
            }
        }
        throw std::runtime_error("cannot allocate a unique test directory");
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

[[nodiscard]] FeatureGraph make_graph()
{
    return {
        FeatureDefinition{
            .id = FeatureId{"D"},
            .parameter_ids = {"d"},
            .dependencies = {},
        },
        FeatureDefinition{
            .id = FeatureId{"C"},
            .parameter_ids = {"c"},
            .dependencies = {FeatureDependency{
                .upstream_feature_id = FeatureId{"B"},
                .input_name = "b-result",
            }},
        },
        FeatureDefinition{
            .id = FeatureId{"B"},
            .parameter_ids = {"b"},
            .dependencies = {FeatureDependency{
                .upstream_feature_id = FeatureId{"A"},
                .input_name = "a-result",
            }},
        },
        FeatureDefinition{
            .id = FeatureId{"A"},
            .parameter_ids = {"a"},
            .dependencies = {},
        },
    };
}

int failures{};

void check(const bool condition, const std::string_view description)
{
    if (!condition) {
        std::cerr << "FAILED: " << description << '\n';
        ++failures;
    }
}

[[nodiscard]] const FeatureEvaluation& feature(
    const Document& document,
    const std::string_view feature_id)
{
    const auto& features = document.current_evaluation().features;
    const auto found = std::ranges::find_if(
        features,
        [feature_id](const FeatureEvaluation& evaluation) {
            return evaluation.feature_id.value == feature_id;
        });
    if (found == features.end()) {
        throw std::logic_error("test feature is missing");
    }
    return *found;
}

[[nodiscard]] bool contains_diagnostic(
    const std::vector<Diagnostic>& diagnostics,
    const std::string_view code)
{
    return std::ranges::any_of(
        diagnostics,
        [code](const Diagnostic& diagnostic) { return diagnostic.code == code; });
}

void dependency_graph_story_survives_recovery_and_history_navigation()
{
    GraphEvaluator evaluator;
    Document document(
        {{"a", 1.0}, {"b", 1.0}, {"c", 1.0}, {"d", 1.0}},
        make_graph(),
        evaluator);

    check(
        evaluator.calls == std::vector<std::string>({"A", "B", "C", "D"}),
        "initial graph evaluates in stable topological order");
    const auto independent_output = feature(document, "D").output_hash;

    evaluator.clear();
    const auto changed_a = document.apply(SetParameterCommand{
        .expected_revision = RevisionId{1},
        .parameter_id = "a",
        .value = 2.0,
    });
    check(changed_a.evaluation_succeeded, "A branch change evaluates successfully");
    check(
        evaluator.calls == std::vector<std::string>({"A", "B", "C"}),
        "changing A evaluates only A and its descendants in stable order");
    check(feature(document, "D").state == FeatureState::clean, "D remains clean");
    check(
        feature(document, "D").output_hash == independent_output,
        "D preserves its prior output");

    evaluator.clear();
    const auto failed_b = document.apply(SetParameterCommand{
        .expected_revision = RevisionId{2},
        .parameter_id = "b",
        .value = -1.0,
    });
    check(!failed_b.evaluation_succeeded, "B failure is committed as evaluation state");
    check(evaluator.calls == std::vector<std::string>({"B"}), "only B executes before failure");
    check(feature(document, "A").state == FeatureState::clean, "A remains clean");
    check(feature(document, "B").state == FeatureState::failed, "B is failed");
    check(feature(document, "C").state == FeatureState::blocked, "C is blocked");
    check(feature(document, "D").state == FeatureState::clean, "D remains independent");
    check(
        contains_diagnostic(feature(document, "C").diagnostics, "UpstreamFeatureFailed"),
        "blocked C records an upstream diagnostic");
    check(!document.current_geometry().available(), "failed graph denies current geometry");
    check(document.evaluated_revision_id() == RevisionId{2}, "last good revision remains active");

    TemporaryDirectory directory;
    const auto checkpoint = directory.path() / "failed-graph.chk";
    save_document_checkpoint_atomic(checkpoint, document.snapshot());
    evaluator.clear();
    auto reopened = Document::restore(load_document_checkpoint(checkpoint), evaluator);
    check(evaluator.calls.empty(), "reopen preserves recorded evaluation without hidden work");
    check(feature(reopened, "B").state == FeatureState::failed, "reopen preserves failed B");
    check(feature(reopened, "C").state == FeatureState::blocked, "reopen preserves blocked C");
    check(feature(reopened, "D").state == FeatureState::clean, "reopen preserves clean D");
    check(!reopened.current_geometry().available(), "reopen preserves current-geometry denial");

    evaluator.clear();
    const auto repaired_b = reopened.apply(SetParameterCommand{
        .expected_revision = RevisionId{3},
        .parameter_id = "b",
        .value = 3.0,
    });
    check(repaired_b.evaluation_succeeded, "repair succeeds");
    check(
        evaluator.calls == std::vector<std::string>({"B", "C"}),
        "repair evaluates only B and C");
    check(reopened.evaluated_revision_id() == RevisionId{4}, "repair advances evaluated revision");
    check(reopened.current_geometry().available(), "repair restores current geometry");
    check(feature(reopened, "A").state == FeatureState::clean, "repair preserves clean A");
    check(feature(reopened, "D").state == FeatureState::clean, "repair preserves clean D");

    evaluator.clear();
    check(reopened.undo(), "repair can be undone");
    check(evaluator.calls == std::vector<std::string>({"B"}), "undo reproduces failed branch");
    check(feature(reopened, "B").state == FeatureState::failed, "undo restores failed B");
    check(feature(reopened, "C").state == FeatureState::blocked, "undo restores blocked C");
    check(!reopened.current_geometry().available(), "undo restores geometry denial");

    evaluator.clear();
    check(reopened.redo(), "repair can be redone");
    check(
        evaluator.calls == std::vector<std::string>({"B", "C"}),
        "redo reproduces repaired branch");
    check(feature(reopened, "B").state == FeatureState::succeeded, "redo restores B success");
    check(feature(reopened, "C").state == FeatureState::succeeded, "redo restores C success");
    check(reopened.current_geometry().available(), "redo restores current geometry");

    const auto revision_before_cycle = reopened.semantic_head_revision_id();
    const auto journal_size_before_cycle = reopened.command_journal().size();
    evaluator.clear();
    const auto cycle = reopened.apply(CommandTransaction{
        .expected_revision = revision_before_cycle,
        .parameter_changes = {},
        .dependency_additions = {FeatureDependencyAddition{
            .upstream_feature_id = FeatureId{"C"},
            .downstream_feature_id = FeatureId{"A"},
            .input_name = "feedback",
        }},
    });
    check(cycle.status == CommandStatus::rejected, "C to A cycle is rejected");
    check(contains_diagnostic(cycle.diagnostics, "FeatureCycle"), "cycle rejection is typed");
    check(
        reopened.semantic_head_revision_id() == revision_before_cycle,
        "cycle rejection creates no revision");
    check(
        reopened.command_journal().size() == journal_size_before_cycle,
        "cycle rejection creates no journal entry");
    check(evaluator.calls.empty(), "cycle rejection does not invoke the evaluator");

    evaluator.clear();
    const auto connected_branch = reopened.apply(CommandTransaction{
        .expected_revision = revision_before_cycle,
        .parameter_changes = {},
        .dependency_additions = {FeatureDependencyAddition{
            .upstream_feature_id = FeatureId{"A"},
            .downstream_feature_id = FeatureId{"D"},
            .input_name = "a-context",
        }},
    });
    check(connected_branch.evaluation_succeeded, "valid dependency addition commits");
    check(
        evaluator.calls == std::vector<std::string>({"D"}),
        "adding A to D reevaluates only D");

    const auto connected_checkpoint = directory.path() / "connected-graph.chk";
    save_document_checkpoint_atomic(connected_checkpoint, reopened.snapshot());
    const auto connected_reopen = Document::restore(
        load_document_checkpoint(connected_checkpoint), evaluator);
    const auto d_definition = std::ranges::find_if(
        connected_reopen.feature_graph(),
        [](const FeatureDefinition& definition) {
            return definition.id == FeatureId{"D"};
        });
    check(
        d_definition != connected_reopen.feature_graph().end()
            && d_definition->dependencies.size() == 1
            && d_definition->dependencies.front().input_name == "a-context",
        "checkpoint and journal preserve the typed dependency addition");
}

} // namespace

int main()
{
    dependency_graph_story_survives_recovery_and_history_navigation();

    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed\n";
        return 1;
    }

    std::cout << "feature dependency graph tests passed\n";
    return 0;
}
