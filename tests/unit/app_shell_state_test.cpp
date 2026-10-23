// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#include "oep/app_shell/shell_controller.hpp"
#include "oep/simulation_ir/project_checkpoint.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

namespace {

class Evaluator final : public oep::document::Evaluator {
public:
    std::string_view version() const noexcept override { return "panel-test-1"; }
    oep::document::FeatureEvaluationResult evaluate_feature(
        const oep::document::FeatureEvaluationRequest& request) override
    {
        if (request.semantic_state.at("width") <= 0.0)
            return {std::nullopt, {{"InvalidWidth", "Width must be positive", request.feature.id}}};
        return {"body", {}};
    }
    std::optional<std::string> compose_geometry(
        oep::document::RevisionId, const oep::document::SemanticState&,
        std::span<const oep::document::FeatureEvaluation>) override { return "geometry"; }
};

void require(const bool condition, const char* const message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

[[nodiscard]] bool near(const double lhs, const double rhs)
{
    return std::abs(lhs - rhs) < 1.0e-9;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    // The scheduler's transient states have a presentation contract even though
    // synchronous evaluation normally publishes only the final record.
    std::vector<oep::document::FeatureEvaluation> states;
    for (const auto state : {oep::document::FeatureState::clean, oep::document::FeatureState::dirty,
                            oep::document::FeatureState::evaluating, oep::document::FeatureState::succeeded,
                            oep::document::FeatureState::failed, oep::document::FeatureState::blocked})
        states.push_back({{"feature"}, state, std::nullopt, {}});
    require(oep::app_shell::project_feature_rows(states) == QStringList{
        "feature · clean", "feature · dirty", "feature · evaluating", "feature · succeeded",
        "feature · failed", "feature · blocked"}, "all typed scheduler states project truthfully");
    QTemporaryDir temporary_directory;
    require(temporary_directory.isValid(), "temporary settings directory is available");
    const QString settings_file = temporary_directory.filePath(QStringLiteral("shell.ini"));

    {
        oep::app_shell::ShellController controller(
            QStringLiteral("cad"),
            QStringLiteral("CAD Studio"),
            QStringLiteral("0.1.0"),
            settings_file);
        require(controller.current_workspace() == QStringLiteral("cad"), "CAD starts selected");

        require(controller.feature_rows().isEmpty(), "unattached document has no features");
        Evaluator evaluator;
        oep::document::Document document({{"width", 2.0}}, {{{"base"}, {"width"}, {}}}, evaluator);
        controller.attach_document(&document);
        require(controller.document_status().startsWith("Evaluated"), "current geometry is projected");
        const auto revision = QString::number(document.semantic_head_revision_id().value);
        require(controller.setParameter("width", -1.0, revision), "failed regeneration commits semantic edit");
        require(controller.feature_rows().first().contains("failed"), "failed feature is projected");
        require(!controller.diagnostic_rows().isEmpty(), "engine diagnostics are projected");
        require(controller.document_status().startsWith("Current geometry unavailable"), "last-good geometry is not presented as current");
        require(!controller.setParameter("width", 3.0, revision), "stale UI command is rejected");
        require(controller.undoDocument(), "UI undo uses document timeline");
        require(controller.document_status().startsWith("Evaluated"), "undo restores evaluated state");
        require(controller.redoDocument(), "UI redo restores failed state");
        require(controller.saveDocument(temporary_directory.filePath("foreign.checkpoint")), "external document checkpoint saves");
        controller.attach_document(nullptr);
        require(!controller.can_undo(), "detaching disables document actions");

        controller.selectWorkspace(QStringLiteral("em"));
        require(controller.current_workspace() == QStringLiteral("em"), "workspace switches");
        controller.selectWorkspace(QStringLiteral("invalid"));
        require(controller.current_workspace() == QStringLiteral("em"), "invalid workspace is ignored");

        controller.saveLayout(333.0, 377.0, 211.0, false, true, false);
    }

    {
        oep::app_shell::ShellController restored(
            QStringLiteral("cad"),
            QStringLiteral("CAD Studio"),
            QStringLiteral("0.1.0"),
            settings_file);
        require(restored.current_workspace() == QStringLiteral("cad"), "entry workspace remains authoritative");
        require(near(restored.left_panel_width(), 333.0), "left width is restored");
        require(near(restored.right_panel_width(), 377.0), "right width is restored");
        require(near(restored.bottom_panel_height(), 211.0), "bottom height is restored");
        require(!restored.left_panel_visible(), "left visibility is restored");
        require(restored.right_panel_visible(), "right visibility is restored");
        require(!restored.bottom_panel_visible(), "bottom visibility is restored");

        restored.saveLayout(1.0, 9'999.0, 0.0, true, true, true);
        require(near(restored.left_panel_width(), 180.0), "left width is bounded");
        require(near(restored.right_panel_width(), 560.0), "right width is bounded");
        require(near(restored.bottom_panel_height(), 110.0), "bottom height is bounded");

        restored.resetLayout();
        require(near(restored.left_panel_width(), 252.0), "reset restores left width");
        require(near(restored.right_panel_width(), 304.0), "reset restores right width");
        require(near(restored.bottom_panel_height(), 174.0), "reset restores bottom height");
        require(restored.left_panel_visible(), "reset shows left panel");
        require(restored.right_panel_visible(), "reset shows right panel");
        require(restored.bottom_panel_visible(), "reset shows bottom panel");
    }

    {
        oep::app_shell::ShellController controller("cad", "CAD", "0.1", settings_file);
        if (controller.cad_available()) {
            require(controller.newCadDocument(), "OCCT example creates a document");
            require(controller.has_viewport_scene(), "evaluated exact body creates a native scene packet");
            require(controller.viewport_status().contains("triangles"), "viewport reports measured scene size");
            const auto viewport_frame = controller.viewportFrame(800.0, 600.0).toMap();
            const auto viewport_triangles = viewport_frame.value("triangles").toList();
            require(!viewport_triangles.isEmpty(), "OCCT tessellation projects visible triangles");
            const auto points = viewport_triangles.first().toMap().value("points").toList();
            const double pick_x = (points[0].toMap().value("x").toDouble()
                + points[1].toMap().value("x").toDouble()
                + points[2].toMap().value("x").toDouble()) / 3.0;
            const double pick_y = (points[0].toMap().value("y").toDouble()
                + points[1].toMap().value("y").toDouble()
                + points[2].toMap().value("y").toDouble()) / 3.0;
            controller.preselectViewport(pick_x, pick_y, 800.0, 600.0);
            require(!controller.viewport_preselection().isEmpty(), "hover resolves a persistent face identity");
            controller.selectViewport(pick_x, pick_y, 800.0, 600.0);
            require(controller.viewport_selection() == controller.viewport_preselection(),
                "click selection round-trips the hovered document identity");
            controller.setViewportClip(0.0);
            require(controller.viewportFrame(800.0, 600.0).toMap().value("triangles").toList().size()
                    < viewport_triangles.size(),
                "native section plane clips most of the evaluated model");
            controller.setViewportClip(1.0);
            require(controller.feature_rows().size() == 5, "five modeling features load");
            require(controller.can_prepare_study(), "evaluated CAD permits draft preparation");
            require(controller.prepareStudy(), "ASIR draft snapshots current exact geometry");
            require(controller.current_workspace() == "em", "preparing draft opens EM workspace");
            require(controller.study_status().contains("setup incomplete"), "draft is not falsely execution ready");
            require(controller.study_diagnostics().contains("ports"), "missing ports have engine diagnostics");
            require(controller.study_diagnostics().contains("roles"), "missing roles have engine diagnostics");
            const auto before_material = controller.study_fingerprint();
            require(!controller.setStudyMaterial("user-material", -1, 1, 0, 0, 2e9, 3e9,
                "test assumption", before_material), "invalid material constants reject atomically");
            require(controller.study_fingerprint() == before_material, "material rejection preserves draft");
            require(controller.setStudyMaterial("user-material", 2.2, 1, 0, 0.01, 1e9, 10e9,
                "user supplied test properties", before_material), "nondispersive user material commits");
            require(controller.study_material_ids().contains("user-material"), "material catalog projects to selector");
            require(controller.materialValues("user-material").value("permittivity").toDouble() == 2.2,
                "existing material values load into editor");
            require(controller.assignStudyRole("test-dielectric", 0, 3, "user-material", controller.study_fingerprint()),
                "dielectric face uses authored material");
            require(controller.study_definitions().contains("dielectric") && controller.study_definitions().contains("user-material"),
                "role inspector displays role kind and material identity");
            require(controller.assignStudyRole("test-dielectric", 0, 2, "user-material", controller.study_fingerprint()),
                "same role ID changes to conductor without duplicating");
            require(controller.study_definitions().contains("conductor"), "conductor kind projects accurately");
            require(!controller.assignStudyRole("test-dielectric", 0, 4, "missing", controller.study_fingerprint()),
                "ground role cannot name an unknown material");
            require(!controller.removeStudyMaterial("user-material", controller.study_fingerprint()), "referenced material is protected");
            require(controller.removeStudyDefinition(false, "test-dielectric", controller.study_fingerprint()), "remove test role explicitly");
            require(controller.removeStudyMaterial("user-material", controller.study_fingerprint()), "unreferenced material removes");
            require(!controller.study_faces().isEmpty(), "final OCCT body exposes resolved persistent faces");
            require(controller.can_edit_study(), "current draft permits topology authoring");
            require(controller.setStudyRole("region-1", 0, false, controller.study_fingerprint()), "resolved vacuum role commits");
            require(!controller.study_diagnostics().contains("an antenna study requires at least one persistent"), "missing-role preflight clears after authoring");
            const auto before_bad_port = controller.study_fingerprint();
            require(!controller.setStudyPort("port-1", 0, 0, -50.0, 4, before_bad_port), "invalid port impedance rejected");
            require(controller.study_fingerprint() == before_bad_port, "invalid port preserves draft");
            require(controller.setStudyPort("port-1", 0, 0, 50.0, 4, controller.study_fingerprint()), "discrete port commits with final-body identities");
            require(!controller.study_diagnostics().contains("unknown persistent"), "authored references resolve in preflight");
            require(controller.study_definitions().contains("port-1"), "port properties projected to inspector");
            const auto before_setup = controller.study_fingerprint();
            require(!controller.setStudyBoundary("domain-1", true, "air", -1, before_setup), "negative padding rejects");
            require(!controller.setStudyMonitor("network-1", 0, "missing", 2.3e9, 2.6e9, before_setup), "unknown monitor port rejects");
            require(controller.study_fingerprint() == before_setup, "invalid setup commands preserve draft identity");
            require(controller.setStudyBoundary("domain-1", true, "air", 0.03, before_setup), "PML boundary policy commits");
            require(controller.setStudyMonitor("network-1", 1, "port-1", 2.3e9, 2.6e9, controller.study_fingerprint()), "impedance monitor commits");
            require(controller.study_setup().contains("open PML") && controller.study_setup().contains("impedance"), "setup properties project to inspector");
            require(controller.can_prepare_worker(), "preflight-valid study enables worker preparation");
            require(controller.prepareWorkerJob(), "reference job receives a calibrated resource estimate");
            require(controller.worker_estimate().contains("cells") && controller.can_launch_worker(),
                "prepared resource snapshot is visible before allocation");
            require(controller.launchWorker() && controller.worker_running(),
                "validated reference job enters worker lifecycle");
            require(controller.advanceWorker() && controller.worker_progress() > 0.0,
                "worker publishes authoritative bounded progress");
            require(controller.checkpointWorker() && controller.has_worker_checkpoint(),
                "running worker exposes a verified checkpoint");
            const auto checkpoint_progress = controller.worker_progress();
            const QString durable_worker_path = temporary_directory.filePath("durable-worker.project");
            require(controller.saveProject(durable_worker_path),
                "project save atomically includes the current worker checkpoint");
            {
                oep::app_shell::ShellController recovered(
                    "em", "EM Studio", "0.1.0",
                    temporary_directory.filePath("worker-recovered.ini"));
                require(recovered.loadProject(durable_worker_path),
                    "fresh desktop session restores CAD, ASIR, job, and checkpoint");
                require(recovered.worker_running()
                        && recovered.worker_progress() == checkpoint_progress
                        && recovered.worker_status().contains("restored")
                        && recovered.has_worker_checkpoint(),
                    "restored worker reattaches authoritative progress in a fresh session");
                const auto active_fingerprint = recovered.study_fingerprint();
                const auto active_progress = recovered.worker_progress();

                auto corrupt_project = oep::simulation_ir::load_project_checkpoint(
                    std::filesystem::path(durable_worker_path.toStdString()));
                corrupt_project.worker_recovery->back() ^= std::byte{1};
                const QString corrupt_worker_path = temporary_directory.filePath(
                    "corrupt-worker.project");
                oep::simulation_ir::save_project_checkpoint_atomic(
                    std::filesystem::path(corrupt_worker_path.toStdString()),
                    corrupt_project);
                require(!recovered.loadProject(corrupt_worker_path),
                    "nested worker corruption rejects the compound project before activation");
                require(recovered.study_fingerprint() == active_fingerprint
                        && recovered.worker_progress() == active_progress,
                    "corrupt worker recovery preserves the active desktop session");

                auto mismatched_project = oep::simulation_ir::load_project_checkpoint(
                    std::filesystem::path(durable_worker_path.toStdString()));
                mismatched_project.study->provenance.warnings.push_back(
                    "intentional fingerprint mismatch fixture");
                const QString mismatched_worker_path = temporary_directory.filePath(
                    "mismatched-worker.project");
                oep::simulation_ir::save_project_checkpoint_atomic(
                    std::filesystem::path(mismatched_worker_path.toStdString()),
                    mismatched_project);
                require(!recovered.loadProject(mismatched_worker_path),
                    "worker recovery rejects a different canonical ASIR fingerprint");
                require(recovered.study_fingerprint() == active_fingerprint
                        && recovered.worker_progress() == active_progress,
                    "authority mismatch preserves the active desktop session");

                while (recovered.worker_running()) {
                    require(recovered.advanceWorker(),
                        "restored desktop worker advances to completion");
                }
                require(recovered.can_collect_worker()
                        && recovered.collectWorkerArtifacts()
                        && recovered.has_worker_artifacts(),
                    "restored worker completes and collects provenance-bearing artifacts");
            }
            require(controller.restartWorkerFromCheckpoint(), "fresh worker resumes UI checkpoint");
            require(controller.worker_progress() == checkpoint_progress
                    && controller.worker_status().contains("restarted"),
                "restart preserves exact progress and reports new worker identity");
            while (controller.worker_running()) require(controller.advanceWorker(), "worker advances to completion");
            require(controller.worker_status().startsWith("Succeeded"), "worker reports terminal success");
            require(controller.can_collect_worker(), "only successful uncollected work enables artifact collection");
            require(controller.collectWorkerArtifacts() && controller.has_worker_artifacts(),
                "successful job exposes provenance-bearing raw artifacts");
            require(!controller.can_collect_worker(), "collected artifacts disable duplicate collection");
            require(controller.prepareWorkerJob() && controller.launchWorker(),
                "valid study can prepare another reference job");
            require(controller.cancelWorker() && controller.worker_status().startsWith("Cancelled"),
                "desktop cancellation reaches an explicit terminal worker state");
            require(!controller.removeStudyDefinition(true, "port-1", controller.study_fingerprint()), "referenced port cannot be removed");
            require(controller.study_diagnostics().contains("referenced by a monitor"), "port deletion reports dependency diagnostic");
            require(!controller.removeStudyMaterial("air", controller.study_fingerprint()), "boundary keeps background material referenced");
            require(controller.removeStudySetup(false, "network-1", controller.study_fingerprint()), "monitor removes explicitly");
            require(controller.study_diagnostics().contains("at least one requested observable"), "monitor removal restores missing setup");
            require(controller.removeStudySetup(true, "domain-1", controller.study_fingerprint()), "boundary removes explicitly");
            require(controller.study_diagnostics().contains("explicit PML"), "boundary removal restores missing setup");
            require(!controller.setStudyRole("bad", -1, true, controller.study_fingerprint()), "invalid face index rejected");
            const auto study_identity = controller.study_fingerprint();
            const auto cad_revision = controller.document_revision();
            require(controller.setStudyFrequency(2.0e9, 3.0e9, study_identity), "study-local frequency edit commits");
            require(controller.document_revision() == cad_revision, "EM edit does not mutate CAD authority");
            require(!controller.setStudyFrequency(2.1e9, 3.1e9, study_identity), "stale study command is rejected");
            const auto updated_identity = controller.study_fingerprint();
            require(!controller.setStudyFrequency(-1.0, 3.0e9, updated_identity), "invalid band is rejected");
            require(controller.study_fingerprint() == updated_identity, "rejected command preserves study");
            require(controller.setParameter("width", 44.0, cad_revision), "CAD changes after draft");
            require(controller.study_status().contains("Stale"), "CAD edit makes snapshot stale");
            require(!controller.can_edit_study(), "stale draft disables topology authoring");
            require(!controller.setStudyBoundary("stale", true, "air", 0.03, updated_identity), "stale CAD rejects boundary authoring");
            require(!controller.setStudyMonitor("stale", 0, "port-1", 2e9, 3e9, updated_identity), "stale CAD rejects monitor authoring");
            require(!controller.setStudyMaterial("stale", 2.2, 1, 0, 0, 1e9, 10e9,
                "assumption", updated_identity), "stale CAD blocks material edits");
            require(!controller.setStudyRole("region-1", 0, true, updated_identity), "stale CAD rejects role mutation");
            require(controller.study_fingerprint() == updated_identity, "CAD edit does not silently rewrite ASIR snapshot");
            require(controller.undoDocument(), "undo returns to draft authority");
            require(controller.study_status().contains("setup incomplete"), "undo recovers ASIR authority");
            require(controller.can_edit_study(), "undo restores topology authoring authority");
            require(controller.removeStudyDefinition(false, "region-1", controller.study_fingerprint()), "role removal commits explicitly");
            require(controller.study_diagnostics().contains("at least one persistent"), "role removal restores incomplete preflight");
            controller.selectFeature(0);
            require(!controller.parameter_rows().isEmpty(), "selected sketch exposes parameters");
            const auto checkpoint = temporary_directory.filePath("cad.oep-checkpoint");
            require(controller.setParameter("width", -1.0, controller.document_revision()), "invalid dimension commits failed regeneration");
            require(!controller.has_viewport_scene(), "failed current regeneration clears the native scene");
            require(!controller.can_prepare_study(), "failed CAD blocks new study preparation");
            require(controller.study_status().contains("geometry unavailable"), "existing draft is blocked on failed CAD");
            require(controller.study_diagnostics().contains("no matching current geometry"), "validator reports unavailable current geometry");
            const QString project_path = temporary_directory.filePath("cad-em.project");
            const auto saved_study = controller.study_fingerprint();
            require(controller.saveProject(project_path), "failed CAD and authored study save together");
            {
                oep::app_shell::ShellController reopened("em", "EM Studio", "0.1.0", temporary_directory.filePath("reopened.ini"));
                require(reopened.loadProject(project_path), "fresh session restores compound project");
                require(reopened.study_fingerprint() == saved_study, "restart preserves all authored ASIR content");
                require(reopened.study_status().contains("geometry unavailable"), "recovered failed CAD blocks study honestly");
                require(reopened.undoDocument(), "project recovery retains Undo history");
                require(reopened.has_viewport_scene(), "Undo rebuilds exact scene after project recovery");
                require(reopened.can_edit_study(), "Undo restores persisted study authority without rewriting it");
                require(reopened.setStudyFrequency(2.2e9, 2.6e9, saved_study), "reopened study remains editable");
                const auto before_bad_project = reopened.study_fingerprint();
                QFile bad_project(temporary_directory.filePath("bad.project"));
                require(bad_project.open(QIODevice::WriteOnly), "bad project fixture opens");
                bad_project.write("corrupt"); bad_project.close();
                require(!reopened.loadProject(bad_project.fileName()), "corrupt project rejected");
                require(reopened.study_fingerprint() == before_bad_project && reopened.can_edit_study(), "failed compound load preserves live CAD and study");
                require(!reopened.saveProject(temporary_directory.filePath("missing-dir/file")), "compound save I/O failure is reported");
            }
            require(controller.saveDocument(checkpoint), "failed document saves atomically");
            const auto failed_status = controller.document_status();
            require(controller.newCadDocument(), "new example replaces active document");
            require(!controller.has_study(), "replacement clears session-only draft");
            require(controller.loadDocument(checkpoint), "failed document reopens and hydrates");
            require(controller.document_status() == failed_status, "reopen preserves failed authority");
            require(controller.undoDocument(), "reopened timeline can undo");
            require(controller.document_status().startsWith("Evaluated"), "undo recovers geometry after restart");
            require(controller.has_viewport_scene(), "reopened Undo rebuilds the current exact scene");
            require(controller.setParameter("width", 45.0, controller.document_revision()), "edit after hydration uses OCCT");
            require(controller.document_status().startsWith("Evaluated"), "valid edit regenerates exact model");
            require(controller.saveDocument(checkpoint), "valid edit saves");
            require(controller.loadDocument(checkpoint), "valid edit reopens");
            const auto before_bad_load = controller.document_status();
            require(!controller.loadDocument(temporary_directory.filePath("missing")), "missing file rejected");
            require(controller.document_status() == before_bad_load, "failed load preserves active document");
            require(!controller.loadDocument(temporary_directory.filePath("foreign.checkpoint")), "incompatible evaluator is rejected");
            require(controller.document_status() == before_bad_load, "incompatible load preserves active document");
            QFile corrupt(temporary_directory.filePath("corrupt.checkpoint"));
            require(corrupt.open(QIODevice::WriteOnly), "corrupt fixture opens");
            require(corrupt.write("corrupt") == 7, "corrupt fixture writes");
            corrupt.close();
            require(!controller.loadDocument(corrupt.fileName()), "corrupt checkpoint is rejected");
            require(controller.document_status() == before_bad_load, "corrupt load preserves active document");
            require(!controller.saveDocument(temporary_directory.filePath("missing-dir/file")), "failed save is reported");
            controller.selectFeature(0);
            require(controller.parameter_rows().first().toMap().value("revision").toString() == controller.document_revision(), "editor revision matches document");
        }
    }

    return 0;
}
