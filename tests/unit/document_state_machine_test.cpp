#include <oep/document/document.hpp>

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

using oep::document::CommandStatus;
using oep::document::CommandTransaction;
using oep::document::Diagnostic;
using oep::document::Document;
using oep::document::Evaluator;
using oep::document::FeatureDefinition;
using oep::document::FeatureDependency;
using oep::document::FeatureEvaluation;
using oep::document::FeatureEvaluationRequest;
using oep::document::FeatureEvaluationResult;
using oep::document::FeatureGraph;
using oep::document::FeatureId;
using oep::document::ParameterChange;
using oep::document::RevisionId;
using oep::document::SemanticState;
using oep::document::SetParameterCommand;

class TestEvaluator final : public Evaluator {
public:
    [[nodiscard]] std::string_view version() const noexcept override
    {
        return "test-evaluator-v1";
    }

    [[nodiscard]] FeatureEvaluationResult evaluate_feature(
        const FeatureEvaluationRequest& request) override
    {
        ++evaluation_count;
        const auto width = request.semantic_state.at("width");
        if (request.feature.id == FeatureId{"base-extrude"} && width <= 0.0) {
            return FeatureEvaluationResult{
                .output_hash = std::nullopt,
                .diagnostics = {Diagnostic{
                    .code = "InvalidExtrudeWidth",
                    .message = "width must be positive",
                    .feature_id = request.feature.id,
                }},
            };
        }

        return FeatureEvaluationResult{
            .output_hash = request.feature.id.value + ':' + std::to_string(width),
            .diagnostics = {},
        };
    }

    [[nodiscard]] std::optional<std::string> compose_geometry(
        const RevisionId revision_id,
        const SemanticState& semantic_state,
        std::span<const FeatureEvaluation>) override
    {
        return "body:" + std::to_string(revision_id.value) + ':'
            + std::to_string(semantic_state.at("width"));
    }

    int evaluation_count{};
};

[[nodiscard]] FeatureGraph feature_graph(const bool include_height = false)
{
    auto parameters = std::vector<std::string>{"width"};
    if (include_height) {
        parameters.push_back("height");
    }
    return {
        FeatureDefinition{
            .id = FeatureId{"base-extrude"},
            .parameter_ids = std::move(parameters),
            .dependencies = {},
        },
        FeatureDefinition{
            .id = FeatureId{"dependent-fillet"},
            .parameter_ids = {},
            .dependencies = {FeatureDependency{
                .upstream_feature_id = FeatureId{"base-extrude"},
                .input_name = "body",
            }},
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

void rejected_commands_do_not_create_revisions()
{
    TestEvaluator evaluator;
    Document document({{"width", 10.0}}, feature_graph(), evaluator);

    const auto result = document.apply(SetParameterCommand{
        .expected_revision = RevisionId{99},
        .parameter_id = "width",
        .value = 20.0,
    });

    check(result.status == CommandStatus::rejected, "stale command is rejected");
    check(document.semantic_head_revision_id() == RevisionId{1}, "rejection keeps semantic head");
    check(evaluator.evaluation_count == 2, "rejection does not invoke evaluator");
    check(document.command_journal().empty(), "rejection does not append to journal");
    check(document.current_geometry().available(), "initial geometry remains current");
}

void transactions_are_atomic_and_journaled()
{
    TestEvaluator evaluator;
    Document document(
        {{"height", 2.0}, {"width", 10.0}}, feature_graph(true), evaluator);

    const auto committed = document.apply(CommandTransaction{
        .expected_revision = RevisionId{1},
        .parameter_changes = {
            ParameterChange{.parameter_id = "width", .value = 20.0},
            ParameterChange{.parameter_id = "height", .value = 4.0},
        },
        .dependency_additions = {},
    });

    check(committed.status == CommandStatus::committed, "valid transaction commits");
    check(committed.semantic_revision == RevisionId{2}, "transaction creates one revision");
    check(document.semantic_state().at("width") == 20.0, "transaction updates width");
    check(document.semantic_state().at("height") == 4.0, "transaction updates height");
    check(document.command_journal().size() == 1, "transaction creates one journal entry");
    check(
        document.command_journal().front().base_revision_id == RevisionId{1}
            && document.command_journal().front().result_revision_id == RevisionId{2},
        "journal connects base and result revisions");
    check(
        document.command_journal().front().parameter_changes.size() == 2,
        "journal preserves the complete transaction intent");

    const auto evaluations_before_rejection = evaluator.evaluation_count;
    const auto rejected = document.apply(CommandTransaction{
        .expected_revision = RevisionId{2},
        .parameter_changes = {
            ParameterChange{.parameter_id = "width", .value = 30.0},
            ParameterChange{.parameter_id = "missing", .value = 8.0},
        },
        .dependency_additions = {},
    });

    check(rejected.status == CommandStatus::rejected, "partly invalid transaction is rejected");
    check(document.semantic_head_revision_id() == RevisionId{2}, "rejection creates no revision");
    check(document.semantic_state().at("width") == 20.0, "rejection applies no partial edit");
    check(document.command_journal().size() == 1, "rejection creates no journal entry");
    check(
        evaluator.evaluation_count == evaluations_before_rejection,
        "rejected transaction does not invoke evaluator");

    const auto no_op = document.apply(CommandTransaction{
        .expected_revision = RevisionId{2},
        .parameter_changes = {
            ParameterChange{.parameter_id = "width", .value = 20.0},
        },
        .dependency_additions = {},
    });
    check(no_op.status == CommandStatus::rejected, "no-op transaction is rejected");

    const auto reopened = Document::restore(document.snapshot(), evaluator);
    check(reopened.command_journal().size() == 1, "journal survives reopen");
    check(reopened.semantic_state().at("height") == 4.0, "replayed result survives reopen");
}

void failed_evaluations_are_saveable_and_repairable()
{
    TestEvaluator evaluator;
    Document document({{"width", 10.0}}, feature_graph(), evaluator);

    const auto failed = document.apply(SetParameterCommand{
        .expected_revision = RevisionId{1},
        .parameter_id = "width",
        .value = -2.0,
    });

    check(failed.status == CommandStatus::committed, "valid semantic edit is committed");
    check(!failed.evaluation_succeeded, "invalid geometry records failed evaluation");
    check(document.semantic_head_revision_id() == RevisionId{2}, "semantic head advances");
    check(document.evaluated_revision_id() == RevisionId{1}, "evaluated revision stays last good");

    const auto geometry = document.current_geometry();
    check(!geometry.available(), "failed semantic head has no current geometry");
    check(
        geometry.error.has_value() && geometry.error->code == "CurrentGeometryUnavailable",
        "geometry denial is typed");

    auto reopened = Document::restore(document.snapshot(), evaluator);
    check(reopened.semantic_head_revision_id() == RevisionId{2}, "reopen preserves semantic head");
    check(!reopened.current_evaluation().succeeded(), "reopen preserves failed evaluation");
    check(!reopened.current_geometry().available(), "reopen does not promote stale geometry");
    check(reopened.semantic_state().at("width") == -2.0, "reopen preserves failed edit intent");

    check(reopened.undo(), "failed edit can be undone");
    check(reopened.semantic_head_revision_id() == RevisionId{1}, "undo restores parent revision");
    check(reopened.current_geometry().available(), "undo restores matching geometry");
    check(reopened.redo(), "failed edit can be redone");
    check(!reopened.current_geometry().available(), "redo restores failed state honestly");

    const auto repaired = reopened.apply(SetParameterCommand{
        .expected_revision = RevisionId{2},
        .parameter_id = "width",
        .value = 5.0,
    });
    check(repaired.evaluation_succeeded, "repair evaluates successfully");
    check(reopened.semantic_head_revision_id() == RevisionId{3}, "repair creates a revision");
    check(reopened.evaluated_revision_id() == RevisionId{3}, "repair advances evaluated revision");
    check(reopened.current_geometry().available(), "repair exposes current geometry");

    check(reopened.undo(), "repair can be undone to failed revision");
    const auto undone_state = reopened.snapshot();
    check(
        undone_state.last_good_geometry.has_value()
            && undone_state.last_good_geometry->revision_id == RevisionId{1},
        "failed revision selects its nearest successful ancestor, not future geometry");
    check(!reopened.current_geometry().available(), "failed revision still denies geometry after undo");
}

void a_new_edit_after_undo_invalidates_redo()
{
    TestEvaluator evaluator;
    Document document({{"width", 10.0}}, feature_graph(), evaluator);

    const auto first = document.apply(SetParameterCommand{
        .expected_revision = RevisionId{1},
        .parameter_id = "width",
        .value = 20.0,
    });
    check(first.evaluation_succeeded, "first edit succeeds");
    check(document.undo(), "successful edit can be undone");

    const auto branch = document.apply(SetParameterCommand{
        .expected_revision = RevisionId{1},
        .parameter_id = "width",
        .value = 30.0,
    });
    check(branch.semantic_revision == RevisionId{3}, "branch receives a monotonic revision ID");
    check(!document.can_redo(), "new branch clears active redo history");
    check(document.semantic_state().at("width") == 30.0, "branch state becomes active");

    const auto reopened = Document::restore(document.snapshot(), evaluator);
    check(reopened.semantic_head_revision_id() == RevisionId{3}, "branched history reopens");
    check(reopened.current_geometry().available(), "branched history preserves current geometry");
}

void malformed_restored_state_is_rejected()
{
    TestEvaluator evaluator;
    Document document({{"width", 10.0}}, feature_graph(), evaluator);
    auto state = document.snapshot();
    state.last_good_geometry->content_hash = "tampered";

    auto rejected = false;
    try {
        static_cast<void>(Document::restore(std::move(state), evaluator));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected, "restore rejects mismatched evaluation and geometry state");

    Document journaled_document({{"width", 10.0}}, feature_graph(), evaluator);
    static_cast<void>(journaled_document.apply(SetParameterCommand{
        .expected_revision = RevisionId{1},
        .parameter_id = "width",
        .value = 20.0,
    }));
    auto corrupted_journal = journaled_document.snapshot();
    corrupted_journal.command_journal.front().parameter_changes.front().value = 30.0;

    rejected = false;
    try {
        static_cast<void>(Document::restore(std::move(corrupted_journal), evaluator));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected, "restore rejects a journal that does not reproduce its revision");
}

} // namespace

int main()
{
    rejected_commands_do_not_create_revisions();
    transactions_are_atomic_and_journaled();
    failed_evaluations_are_saveable_and_repairable();
    a_new_edit_after_undo_invalidates_redo();
    malformed_restored_state_is_rejected();

    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed\n";
        return 1;
    }

    std::cout << "document state-machine tests passed\n";
    return 0;
}
