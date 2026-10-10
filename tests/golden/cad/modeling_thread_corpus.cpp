#include <oep/test/modeling_thread_corpus.hpp>

#include <oep/document/document.hpp>
#include <oep/modeler/cad_feature_evaluator.hpp>
#include <oep/persistence/checkpoint.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

namespace oep::test {
namespace {

using namespace document;
using namespace modeler;
using topology_id::PersistentTopologyId;

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

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        std::random_device random_source;
        path_ = std::filesystem::temp_directory_path()
            / ("oep-reel2-modeling-thread-" + std::to_string(random_source()));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

void append_case(
    ModelingThreadReport& report,
    std::string case_id,
    const CaseCheck& check)
{
    report.cases.push_back(ModelingThreadCaseResult{
        .case_id = std::move(case_id),
        .passed = check.passed(),
        .detail = check.detail(),
    });
}

[[nodiscard]] const FeatureEvaluation* feature_evaluation(
    const EvaluationRecord& record,
    const std::string_view feature_id)
{
    const auto found = std::ranges::find(
        record.features,
        FeatureId{std::string(feature_id)},
        &FeatureEvaluation::feature_id);
    return found == record.features.end() ? nullptr : &*found;
}

[[nodiscard]] const CadFeatureArtifact* artifact_for_feature(
    const EvaluationRecord& record,
    const std::string_view feature_id,
    const CadFeatureEvaluator& evaluator)
{
    const auto* feature = feature_evaluation(record, feature_id);
    return feature == nullptr || !feature->output_hash.has_value()
        ? nullptr
        : evaluator.artifact(*feature->output_hash);
}

[[nodiscard]] bool has_diagnostic(
    const std::span<const Diagnostic> diagnostics,
    const std::string_view code)
{
    return std::ranges::any_of(
        diagnostics,
        [code](const Diagnostic& item) { return item.code == code; });
}

struct ThreadEvidence {
    PersistentTopologyId support_reference;
    BodySummary final_summary;
    std::string final_geometry_hash;
};

[[nodiscard]] std::optional<ThreadEvidence> current_evidence(
    const Document& document,
    const CadFeatureEvaluator& evaluator,
    CaseCheck& check)
{
    const auto& record = document.current_evaluation();
    const auto* sketch = artifact_for_feature(record, hole_sketch_feature_id, evaluator);
    const auto* final_body = artifact_for_feature(record, hole_cut_feature_id, evaluator);
    check.require(sketch != nullptr, "attached sketch artifact is missing");
    check.require(final_body != nullptr, "cut body artifact is missing");
    if (sketch == nullptr || final_body == nullptr) {
        return std::nullopt;
    }
    check.require(sketch->support_reference.has_value(), "attached sketch has no persistent support ID");
    check.require(final_body->body_summary.has_value(), "cut has no exact body summary");
    const auto geometry = document.current_geometry();
    check.require(geometry.available(), "successful feature graph has no current geometry");
    if (!sketch->support_reference.has_value() || !final_body->body_summary.has_value()
        || !geometry.geometry.has_value()) {
        return std::nullopt;
    }
    return ThreadEvidence{
        .support_reference = *sketch->support_reference,
        .final_summary = *final_body->body_summary,
        .final_geometry_hash = geometry.geometry->content_hash,
    };
}

void require_failed_head(const Document& document, CaseCheck& check)
{
    const auto geometry = document.current_geometry();
    check.require(!geometry.available(), "failed semantic head exposed stale geometry as current");
    check.require(
        geometry.error.has_value() && geometry.error->code == "CurrentGeometryUnavailable",
        "failed semantic head did not return typed geometry denial");
    const auto& record = document.current_evaluation();
    const auto* base_sketch = feature_evaluation(record, base_sketch_feature_id);
    const auto* extrude = feature_evaluation(record, base_extrude_feature_id);
    const auto* fillet = feature_evaluation(record, edge_fillet_feature_id);
    const auto* attached = feature_evaluation(record, hole_sketch_feature_id);
    const auto* cut = feature_evaluation(record, hole_cut_feature_id);
    check.require(base_sketch != nullptr && base_sketch->state == FeatureState::clean,
                  "unaffected base sketch was not preserved as clean");
    check.require(extrude != nullptr && extrude->state == FeatureState::clean,
                  "unaffected extrusion was not preserved as clean");
    check.require(fillet != nullptr && fillet->state == FeatureState::failed,
                  "oversized fillet was not classified as failed");
    check.require(attached != nullptr && attached->state == FeatureState::blocked,
                  "attached sketch was not blocked by the failed fillet");
    check.require(cut != nullptr && cut->state == FeatureState::blocked,
                  "cut was not blocked by the failed fillet");
    if (fillet != nullptr) {
        check.require(
            has_diagnostic(fillet->diagnostics, "GeometryDegenerateGeometry"),
            "oversized fillet did not retain its typed geometry diagnostic");
    }
}

} // namespace

bool ModelingThreadReport::passed() const noexcept
{
    return std::ranges::all_of(cases, &ModelingThreadCaseResult::passed);
}

ModelingThreadReport run_modeling_thread_corpus(
    const GeometryKernelFactory& make_kernel)
{
    auto identity_kernel = make_kernel();
    if (identity_kernel == nullptr) {
        throw std::invalid_argument("modeling thread requires a geometry-kernel factory");
    }
    ModelingThreadReport report{.kernel = identity_kernel->identity(), .cases = {}};
    identity_kernel.reset();

    TemporaryDirectory directory;
    const auto failed_checkpoint = directory.path() / "failed.chk";
    const auto repaired_checkpoint = directory.path() / "repaired.chk";
    std::optional<ThreadEvidence> changed_evidence;
    std::optional<ThreadEvidence> repaired_evidence;

    {
        CaseCheck check;
        auto kernel = make_kernel();
        CadFeatureEvaluator evaluator(*kernel);
        auto seed = make_reel2_cad_model();
        Document document(
            std::move(seed.semantic_state), std::move(seed.feature_graph), evaluator);
        const auto initial_evidence = current_evidence(document, evaluator, check);
        check.require(initial_evidence.has_value(), "initial modeling thread did not complete");
        const auto* fillet_artifact = artifact_for_feature(
            document.current_evaluation(), edge_fillet_feature_id, evaluator);
        const auto* fillet_evaluation = feature_evaluation(
            document.current_evaluation(), edge_fillet_feature_id);
        check.require(
            fillet_artifact != nullptr && !fillet_artifact->topology_decisions.empty(),
            "fillet did not classify its upstream topology references");
        check.require(
            fillet_artifact != nullptr && fillet_evaluation != nullptr
                && fillet_artifact->topology_decisions.size()
                    == fillet_evaluation->diagnostics.size(),
            "topology reconciliation decisions were not recorded as typed evaluation diagnostics");

        const auto width_change = document.apply(SetParameterCommand{
            .expected_revision = document.semantic_head_revision_id(),
            .parameter_id = std::string(width_parameter_id),
            .value = 24.0,
        });
        check.require(width_change.status == CommandStatus::committed,
                      "base dimension change was rejected");
        check.require(width_change.evaluation_succeeded,
                      "base dimension change did not regenerate the model");
        changed_evidence = current_evidence(document, evaluator, check);
        check.require(changed_evidence.has_value(), "changed model has no evidence");
        if (initial_evidence.has_value() && changed_evidence.has_value()) {
            check.require(
                initial_evidence->support_reference == changed_evidence->support_reference,
                "dimension change moved the attached sketch to a different persistent face ID");
            check.require(
                initial_evidence->final_geometry_hash != changed_evidence->final_geometry_hash,
                "dimension change did not alter the exact body hash");
            check.require(
                changed_evidence->final_summary.volume > initial_evidence->final_summary.volume,
                "wider base did not increase final body volume");
        }
        append_case(report, "sketch-extrude-fillet-attached-sketch-cut", check);

        CaseCheck failure_check;
        const auto base_count = evaluator.evaluation_count(base_sketch_feature_id);
        const auto extrude_count = evaluator.evaluation_count(base_extrude_feature_id);
        const auto failed = document.apply(SetParameterCommand{
            .expected_revision = document.semantic_head_revision_id(),
            .parameter_id = std::string(fillet_radius_parameter_id),
            .value = 3.1,
        });
        failure_check.require(failed.status == CommandStatus::committed,
                              "valid semantic fillet edit was not committed");
        failure_check.require(!failed.evaluation_succeeded,
                              "oversized fillet unexpectedly regenerated");
        failure_check.require(
            evaluator.evaluation_count(base_sketch_feature_id) == base_count,
            "failed fillet reevaluated the independent base sketch");
        failure_check.require(
            evaluator.evaluation_count(base_extrude_feature_id) == extrude_count,
            "failed fillet reevaluated the clean extrusion");
        require_failed_head(document, failure_check);
        persistence::save_document_checkpoint_atomic(failed_checkpoint, document.snapshot());
        failure_check.require(std::filesystem::exists(failed_checkpoint),
                              "failed evaluation checkpoint was not written");
        append_case(report, "classified-failure-denies-stale-geometry", failure_check);
    }

    {
        CaseCheck check;
        auto kernel = make_kernel();
        CadFeatureEvaluator evaluator(*kernel);
        auto state = persistence::load_document_checkpoint(failed_checkpoint);
        const auto hydration = evaluator.hydrate(state);
        check.require(hydration.succeeded(), "failed checkpoint could not hydrate CAD artifacts");
        const auto calls_after_hydration = evaluator.evaluation_count(edge_fillet_feature_id);
        auto document = Document::restore(std::move(state), evaluator);
        check.require(
            evaluator.evaluation_count(edge_fillet_feature_id) == calls_after_hydration,
            "document restore performed hidden feature evaluation");
        check.require(document.semantic_state().at(std::string(fillet_radius_parameter_id)) == 3.1,
                      "failed semantic head did not survive process restart");
        require_failed_head(document, check);

        const auto repaired = document.apply(SetParameterCommand{
            .expected_revision = document.semantic_head_revision_id(),
            .parameter_id = std::string(fillet_radius_parameter_id),
            .value = 1.25,
        });
        check.require(repaired.status == CommandStatus::committed,
                      "fillet repair was rejected");
        check.require(repaired.evaluation_succeeded,
                      "fillet repair did not regenerate blocked descendants");
        repaired_evidence = current_evidence(document, evaluator, check);
        check.require(repaired_evidence.has_value(), "repaired model has no exact body evidence");
        if (changed_evidence.has_value() && repaired_evidence.has_value()) {
            check.require(
                changed_evidence->support_reference == repaired_evidence->support_reference,
                "repair did not recover the original attached-sketch support ID");
        }

        check.require(document.undo(), "repair revision could not be undone");
        require_failed_head(document, check);
        check.require(document.redo(), "repair revision could not be redone");
        const auto redone = current_evidence(document, evaluator, check);
        check.require(redone.has_value(), "redo did not reproduce the repaired body");
        if (redone.has_value() && repaired_evidence.has_value()) {
            check.require(redone->final_geometry_hash == repaired_evidence->final_geometry_hash,
                          "redo changed the repaired exact body hash");
        }
        persistence::save_document_checkpoint_atomic(repaired_checkpoint, document.snapshot());
        append_case(report, "reopen-failed-repair-undo-redo", check);
    }

    {
        CaseCheck check;
        auto kernel = make_kernel();
        CadFeatureEvaluator evaluator(*kernel);
        auto state = persistence::load_document_checkpoint(repaired_checkpoint);
        const auto hydration = evaluator.hydrate(state);
        check.require(hydration.succeeded(), "repaired checkpoint could not hydrate CAD artifacts");
        auto document = Document::restore(std::move(state), evaluator);
        const auto reopened = current_evidence(document, evaluator, check);
        check.require(document.semantic_state().at(std::string(width_parameter_id)) == 24.0,
                      "reopened repaired model lost the changed base dimension");
        check.require(document.semantic_state().at(std::string(fillet_radius_parameter_id)) == 1.25,
                      "reopened repaired model lost the repaired fillet radius");
        check.require(reopened.has_value(), "reopened repaired model has no exact body evidence");
        if (reopened.has_value() && repaired_evidence.has_value()) {
            check.require(
                reopened->support_reference == repaired_evidence->support_reference,
                "restart changed the persistent attached-sketch support ID");
            check.require(
                reopened->final_geometry_hash == repaired_evidence->final_geometry_hash,
                "restart changed the repaired exact body hash");
            check.require(
                reopened->final_summary.content_hash
                    == repaired_evidence->final_summary.content_hash,
                "restart changed the exact body summary");
        }
        append_case(report, "repaired-save-terminate-reopen", check);
    }

    return report;
}

} // namespace oep::test
