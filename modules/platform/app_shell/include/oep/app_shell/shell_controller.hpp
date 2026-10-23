// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

// Native UI adapter projects engine state and routes commands into compiled
// contracts. Attached documents are borrowed; owned CAD sessions keep the kernel,
// evaluator, and document alive together. Compound project recovery explicitly persists authored EM drafts.

#pragma once

#include <memory>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include "oep/document/document.hpp"
#include "oep/simulation_ir/simulation_ir.hpp"
#include "oep/simulation_ir/cad_study_draft.hpp"

class QSettings;

namespace oep::app_shell {

// Pure presentation adapter for all scheduler states, including transient states.
// The synchronous CAD host normally publishes final states after regeneration.
[[nodiscard]] QStringList project_feature_rows(std::span<const document::FeatureEvaluation> features);

class ShellController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList studyPortIds READ study_port_ids NOTIFY document_changed)
    Q_PROPERTY(QString studySetup READ study_setup NOTIFY document_changed)
    Q_PROPERTY(QStringList studyMaterialIds READ study_material_ids NOTIFY document_changed)
    Q_PROPERTY(QString studyMaterials READ study_materials NOTIFY document_changed)
    Q_PROPERTY(QStringList studyFaces READ study_faces NOTIFY document_changed)
    Q_PROPERTY(QString studyDefinitions READ study_definitions NOTIFY document_changed)
    Q_PROPERTY(bool canEditStudy READ can_edit_study NOTIFY document_changed)
    Q_PROPERTY(QString studyStatus READ study_status NOTIFY document_changed)
    Q_PROPERTY(QString studyFingerprint READ study_fingerprint NOTIFY document_changed)
    Q_PROPERTY(QString studyDiagnostics READ study_diagnostics NOTIFY document_changed)
    Q_PROPERTY(QString studySummary READ study_summary NOTIFY document_changed)
    Q_PROPERTY(QStringList studyRows READ study_rows NOTIFY document_changed)
    Q_PROPERTY(bool canPrepareStudy READ can_prepare_study NOTIFY document_changed)
    Q_PROPERTY(bool hasStudy READ has_study NOTIFY document_changed)
    Q_PROPERTY(double studyMinimumFrequency READ study_minimum_frequency NOTIFY document_changed)
    Q_PROPERTY(double studyMaximumFrequency READ study_maximum_frequency NOTIFY document_changed)
    Q_PROPERTY(QString selectedFeature READ selected_feature NOTIFY document_changed)
    Q_PROPERTY(QVariantList parameterRows READ parameter_rows NOTIFY document_changed)
    Q_PROPERTY(QString documentRevision READ document_revision NOTIFY document_changed)
    Q_PROPERTY(QString fileMessage READ file_message NOTIFY document_changed)
    Q_PROPERTY(bool cadAvailable READ cad_available CONSTANT)
    Q_PROPERTY(QString documentStatus READ document_status NOTIFY document_changed)
    Q_PROPERTY(QStringList featureRows READ feature_rows NOTIFY document_changed)
    Q_PROPERTY(QStringList diagnosticRows READ diagnostic_rows NOTIFY document_changed)
    Q_PROPERTY(QString diagnosticText READ diagnostic_text NOTIFY document_changed)
    Q_PROPERTY(bool canUndo READ can_undo NOTIFY document_changed)
    Q_PROPERTY(bool canRedo READ can_redo NOTIFY document_changed)
    Q_PROPERTY(QString currentWorkspace READ current_workspace NOTIFY workspace_changed)
    Q_PROPERTY(QString productName READ product_name CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(double leftPanelWidth READ left_panel_width NOTIFY layout_changed)
    Q_PROPERTY(double rightPanelWidth READ right_panel_width NOTIFY layout_changed)
    Q_PROPERTY(double bottomPanelHeight READ bottom_panel_height NOTIFY layout_changed)
    Q_PROPERTY(bool leftPanelVisible READ left_panel_visible NOTIFY layout_changed)
    Q_PROPERTY(bool rightPanelVisible READ right_panel_visible NOTIFY layout_changed)
    Q_PROPERTY(bool bottomPanelVisible READ bottom_panel_visible NOTIFY layout_changed)
    Q_PROPERTY(bool hasViewportScene READ has_viewport_scene NOTIFY viewport_changed)
    Q_PROPERTY(QString viewportStatus READ viewport_status NOTIFY viewport_changed)
    Q_PROPERTY(QString viewportSelection READ viewport_selection NOTIFY viewport_changed)
    Q_PROPERTY(QString viewportPreselection READ viewport_preselection NOTIFY viewport_changed)
    Q_PROPERTY(double viewportClipFraction READ viewport_clip_fraction NOTIFY viewport_changed)
    Q_PROPERTY(bool canPrepareWorker READ can_prepare_worker NOTIFY worker_changed)
    Q_PROPERTY(bool canLaunchWorker READ can_launch_worker NOTIFY worker_changed)
    Q_PROPERTY(bool workerRunning READ worker_running NOTIFY worker_changed)
    Q_PROPERTY(bool canCheckpointWorker READ can_checkpoint_worker NOTIFY worker_changed)
    Q_PROPERTY(bool canCollectWorker READ can_collect_worker NOTIFY worker_changed)
    Q_PROPERTY(bool hasWorkerCheckpoint READ has_worker_checkpoint NOTIFY worker_changed)
    Q_PROPERTY(bool hasWorkerArtifacts READ has_worker_artifacts NOTIFY worker_changed)
    Q_PROPERTY(double workerProgress READ worker_progress NOTIFY worker_changed)
    Q_PROPERTY(QString workerStatus READ worker_status NOTIFY worker_changed)
    Q_PROPERTY(QString workerEstimate READ worker_estimate NOTIFY worker_changed)

public:
    explicit ShellController(
        QString initial_workspace,
        QString product_name,
        QString version,
        QString settings_file = {},
        QObject* parent = nullptr);
    ~ShellController() override;

    [[nodiscard]] QString current_workspace() const;
    [[nodiscard]] QString product_name() const;
    [[nodiscard]] QString version() const;
    [[nodiscard]] double left_panel_width() const;
    [[nodiscard]] double right_panel_width() const;
    [[nodiscard]] double bottom_panel_height() const;
    [[nodiscard]] bool left_panel_visible() const;
    [[nodiscard]] bool right_panel_visible() const;
    [[nodiscard]] bool bottom_panel_visible() const;
    [[nodiscard]] bool has_viewport_scene() const;
    [[nodiscard]] QString viewport_status() const;
    [[nodiscard]] QString viewport_selection() const;
    [[nodiscard]] QString viewport_preselection() const;
    [[nodiscard]] double viewport_clip_fraction() const;
    Q_INVOKABLE QVariant viewportFrame(double width, double height) const;
    Q_INVOKABLE void orbitViewport(double delta_x, double delta_y);
    Q_INVOKABLE void panViewport(double delta_x, double delta_y);
    Q_INVOKABLE void zoomViewport(double wheel_delta);
    Q_INVOKABLE void fitViewport();
    Q_INVOKABLE void setViewportClip(double fraction);
    Q_INVOKABLE void preselectViewport(double x, double y, double width, double height);
    Q_INVOKABLE void selectViewport(double x, double y, double width, double height);
    [[nodiscard]] bool can_prepare_worker() const;
    [[nodiscard]] bool can_launch_worker() const;
    [[nodiscard]] bool worker_running() const;
    [[nodiscard]] bool can_checkpoint_worker() const;
    [[nodiscard]] bool can_collect_worker() const;
    [[nodiscard]] bool has_worker_checkpoint() const;
    [[nodiscard]] bool has_worker_artifacts() const;
    [[nodiscard]] double worker_progress() const;
    [[nodiscard]] QString worker_status() const;
    [[nodiscard]] QString worker_estimate() const;
    Q_INVOKABLE bool prepareWorkerJob();
    Q_INVOKABLE bool launchWorker();
    Q_INVOKABLE bool advanceWorker();
    Q_INVOKABLE bool cancelWorker();
    Q_INVOKABLE bool checkpointWorker();
    Q_INVOKABLE bool restartWorkerFromCheckpoint();
    Q_INVOKABLE bool collectWorkerArtifacts();

    // The host owns the document and must detach it before destruction.
    void attach_document(document::Document* document);
    Q_INVOKABLE void refreshDocument();
    Q_INVOKABLE bool prepareStudy();
    Q_INVOKABLE QVariantMap materialValues(const QString& id) const;
    Q_INVOKABLE bool setStudyBoundary(const QString& id, bool open_pml, const QString& material_id,
        double padding, const QString& expected_fingerprint);
    Q_INVOKABLE bool setStudyMonitor(const QString& id, int kind, const QString& port_id,
        double minimum_frequency, double maximum_frequency, const QString& expected_fingerprint);
    Q_INVOKABLE bool removeStudySetup(bool boundary, const QString& id, const QString& expected_fingerprint);
    [[nodiscard]] QStringList study_port_ids() const;
    [[nodiscard]] QString study_setup() const;
    Q_INVOKABLE bool setStudyMaterial(const QString& id, double permittivity, double permeability,
        double conductivity, double loss_tangent, double minimum_frequency, double maximum_frequency,
        const QString& provenance, const QString& expected_fingerprint);
    Q_INVOKABLE bool removeStudyMaterial(const QString& id, const QString& expected_fingerprint);
    Q_INVOKABLE bool assignStudyRole(const QString& id, int face_index, int kind,
        const QString& material_id, const QString& expected_fingerprint);
    [[nodiscard]] QStringList study_material_ids() const;
    [[nodiscard]] QString study_materials() const;
    Q_INVOKABLE bool setStudyRole(const QString& id, int face_index, bool keep_out, const QString& expected_fingerprint);
    Q_INVOKABLE bool setStudyPort(const QString& id, int face_index, int plane_index, double impedance,
                                int orientation, const QString& expected_fingerprint);
    Q_INVOKABLE bool removeStudyDefinition(bool port, const QString& id, const QString& expected_fingerprint);
    [[nodiscard]] QStringList study_faces() const;
    [[nodiscard]] QString study_definitions() const;
    [[nodiscard]] bool can_edit_study() const;
    Q_INVOKABLE bool setStudyFrequency(double minimum, double maximum, const QString& expected_fingerprint);
    [[nodiscard]] bool can_prepare_study() const;
    [[nodiscard]] bool has_study() const { return study_.has_value(); }
    [[nodiscard]] QString study_status() const;
    [[nodiscard]] QString study_fingerprint() const;
    [[nodiscard]] QString study_diagnostics() const;
    [[nodiscard]] QString study_summary() const;
    [[nodiscard]] QStringList study_rows() const;
    [[nodiscard]] double study_minimum_frequency() const;
    [[nodiscard]] double study_maximum_frequency() const;
    Q_INVOKABLE bool newCadDocument();
    Q_INVOKABLE bool saveProject(const QString& path);
    Q_INVOKABLE bool loadProject(const QString& path);
    Q_INVOKABLE bool loadDocument(const QString& path);
    Q_INVOKABLE bool saveDocument(const QString& path);
    Q_INVOKABLE void selectFeature(int index);
    [[nodiscard]] QString selected_feature() const { return selected_feature_; }
    [[nodiscard]] QString file_message() const { return file_message_; }
    [[nodiscard]] QString document_revision() const;
    [[nodiscard]] QVariantList parameter_rows() const;
    [[nodiscard]] bool cad_available() const;

    [[nodiscard]] QString document_status() const;
    [[nodiscard]] QStringList feature_rows() const;
    [[nodiscard]] QStringList diagnostic_rows() const;
    [[nodiscard]] QString diagnostic_text() const { return diagnostic_rows().join(QStringLiteral("\n")); }
    [[nodiscard]] bool can_undo() const;
    [[nodiscard]] bool can_redo() const;
    Q_INVOKABLE bool setParameter(const QString& id, double value, const QString& expected_revision);
    Q_INVOKABLE bool undoDocument();
    Q_INVOKABLE bool redoDocument();

    Q_INVOKABLE void selectWorkspace(const QString& workspace);
    Q_INVOKABLE void setPanelVisible(const QString& panel, bool visible);
    Q_INVOKABLE void saveLayout(
        double left_width,
        double right_width,
        double bottom_height,
        bool left_visible,
        bool right_visible,
        bool bottom_visible);
    Q_INVOKABLE void resetLayout();

signals:
    void document_changed();
    void workspace_changed();
    void layout_changed();
    void viewport_changed();
    void worker_changed();

private:
    void load_layout();
    void persist_layout();
    void rebuild_viewport();
    void reset_worker();

    [[nodiscard]] simulation_ir::ValidationReport study_report() const;
    [[nodiscard]] simulation_ir::ValidationContext study_context() const;
    [[nodiscard]] std::vector<simulation_ir::EntityReference> study_face_references() const;
    bool present_study_result(const simulation_ir::StudyDefinitionResult& result);
    std::optional<simulation_ir::AntennaStudy> study_;
    std::string cad_document_id_;
    QString study_command_message_;
    struct CadSession;
    std::unique_ptr<CadSession> cad_session_;
    struct ViewportState;
    std::unique_ptr<ViewportState> viewport_;
    struct WorkerState;
    std::unique_ptr<WorkerState> worker_;
    document::Document* document_{};
    QString selected_feature_;
    QString file_message_;
    QStringList command_diagnostics_;
    QString current_workspace_;
    QString product_name_;
    QString version_;
    double left_panel_width_{252.0};
    double right_panel_width_{304.0};
    double bottom_panel_height_{174.0};
    bool left_panel_visible_{true};
    bool right_panel_visible_{true};
    bool bottom_panel_visible_{true};
    std::unique_ptr<QSettings> settings_;
};

} // namespace oep::app_shell
