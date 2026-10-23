// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

// Native UI adapter projects engine state and routes commands into compiled
// contracts. Attached documents are borrowed; owned CAD sessions keep the kernel,
// evaluator, and document alive together. Compound project recovery explicitly persists authored EM drafts.

#include "oep/app_shell/shell_controller.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <utility>

#include <QSettings>
#include <QDateTime>
#include <QUuid>
#include "oep/simulation_ir/cad_study_draft.hpp"
#include <QVariantMap>
#include <filesystem>
#include "oep/persistence/checkpoint.hpp"
#include "oep/simulation_ir/project_checkpoint.hpp"
#include "oep/simulation_cost/simulation_cost.hpp"
#include "oep/simulation_orchestrator/reference_worker.hpp"
#include "oep/simulation_orchestrator/worker_protocol.hpp"
#include "oep/viewport/viewport.hpp"
#ifdef OEP_SHELL_OCCT
#include "oep/modeler/cad_feature_evaluator.hpp"
#include "oep/modeler/occt_geometry_kernel.hpp"
#include "oep/tessellation/scene_packet.hpp"
#endif

namespace oep::app_shell {
namespace {

constexpr double default_left_width = 252.0;
constexpr double default_right_width = 304.0;
constexpr double default_bottom_height = 174.0;

[[nodiscard]] double bounded(
    const double value,
    const double minimum,
    const double maximum)
{
    return std::clamp(value, minimum, maximum);
}

[[nodiscard]] QString format_worker_estimate(
    const em_solver_api::ResourceEstimateSnapshot& estimate)
{
    return QStringLiteral(
        "%1 cells · %2 steps · %3 KiB peak · %4 KiB checkpoint/storage · ≤ %5 s")
        .arg(estimate.grid_cells)
        .arg(estimate.time_steps)
        .arg(estimate.estimated_peak_memory_bytes / 1024U)
        .arg(estimate.estimated_storage_bytes / 1024U)
        .arg(estimate.runtime_upper_bound_s, 0, 'f', 3);
}

} // namespace

struct ShellController::ViewportState {
    viewport::CameraState camera;
    std::shared_ptr<const viewport::SceneAssembly> scene;
    QString selection;
    QString preselection;
    QString diagnostic;
    double build_milliseconds{};
};

// Worker values are session execution state, not document state. Any CAD or
// ASIR authority change replaces this aggregate so an old job, checkpoint, or
// artifact cannot be presented against new inputs.
struct ShellController::WorkerState {
    std::unique_ptr<simulation_orchestrator::ReferenceSimulationWorker> worker;
    std::optional<em_solver_api::PreparedSimulationJob> job;
    std::optional<em_solver_api::JobState> state;
    std::optional<em_solver_api::CheckpointArtifact> checkpoint;
    std::optional<em_solver_api::RawArtifacts> artifacts;
    QString estimate;
    QString message{QStringLiteral("Prepare a valid study to estimate a reference job.")};
};

ShellController::ShellController(
    QString initial_workspace,
    QString product_name,
    QString version,
    QString settings_file,
    QObject* const parent)
    : QObject(parent),
      viewport_(std::make_unique<ViewportState>()),
      worker_(std::make_unique<WorkerState>()),
      current_workspace_(initial_workspace == QStringLiteral("em")
                             ? QStringLiteral("em")
                             : QStringLiteral("cad")),
      product_name_(std::move(product_name)),
      version_(std::move(version)),
      settings_(settings_file.isEmpty()
                    ? std::make_unique<QSettings>()
                    : std::make_unique<QSettings>(
                          std::move(settings_file), QSettings::IniFormat))
{
    load_layout();
}

struct ShellController::CadSession {
#ifdef OEP_SHELL_OCCT
    modeler::OcctGeometryKernel kernel;
    modeler::CadFeatureEvaluator evaluator{kernel};
    std::unique_ptr<document::Document> document;
#endif
};

ShellController::~ShellController() = default;

bool ShellController::cad_available() const
{
#ifdef OEP_SHELL_OCCT
    return true;
#else
    return false;
#endif
}

bool ShellController::has_viewport_scene() const
{
    return viewport_->scene != nullptr;
}

QString ShellController::viewport_selection() const { return viewport_->selection; }
QString ShellController::viewport_preselection() const { return viewport_->preselection; }
double ShellController::viewport_clip_fraction() const { return viewport_->camera.clip_fraction; }

bool ShellController::can_prepare_worker() const
{
    if (!study_.has_value()) return false;
    for (const auto& issue : study_report().issues) {
        if (issue.severity != simulation_ir::ValidationSeverity::error) continue;
        if (issue.path == "identity.solver_build" || issue.path == "provenance") continue;
        return false;
    }
    return true;
}

bool ShellController::can_launch_worker() const
{
    return worker_->job.has_value() && !worker_->state.has_value();
}

bool ShellController::worker_running() const
{
    return worker_->state.has_value()
        && (worker_->state->phase == em_solver_api::JobPhase::queued
            || worker_->state->phase == em_solver_api::JobPhase::running
            || worker_->state->phase == em_solver_api::JobPhase::cancelling);
}

bool ShellController::can_checkpoint_worker() const
{
    return worker_->state.has_value()
        && worker_->state->phase == em_solver_api::JobPhase::running
        && worker_->state->completed_time_steps > 0;
}

bool ShellController::can_collect_worker() const
{
    return worker_->state.has_value()
        && worker_->state->phase == em_solver_api::JobPhase::succeeded
        && !worker_->artifacts.has_value();
}

bool ShellController::has_worker_checkpoint() const { return worker_->checkpoint.has_value(); }
bool ShellController::has_worker_artifacts() const { return worker_->artifacts.has_value(); }
double ShellController::worker_progress() const
{
    return worker_->state.has_value() ? worker_->state->progress : 0.0;
}

QString ShellController::worker_status() const
{
    if (!worker_->state.has_value()) return worker_->message;
    QString phase;
    switch (worker_->state->phase) {
    case em_solver_api::JobPhase::queued: phase = QStringLiteral("Queued"); break;
    case em_solver_api::JobPhase::running: phase = QStringLiteral("Running"); break;
    case em_solver_api::JobPhase::cancelling: phase = QStringLiteral("Cancelling"); break;
    case em_solver_api::JobPhase::cancelled: phase = QStringLiteral("Cancelled"); break;
    case em_solver_api::JobPhase::succeeded: phase = QStringLiteral("Succeeded"); break;
    case em_solver_api::JobPhase::failed: phase = QStringLiteral("Failed"); break;
    }
    return QStringLiteral("%1 · %2/%3 steps · worker %4\n%5")
        .arg(phase)
        .arg(worker_->state->completed_time_steps)
        .arg(worker_->state->total_time_steps)
        .arg(QString::fromStdString(worker_->state->worker_id))
        .arg(worker_->message);
}

QString ShellController::worker_estimate() const { return worker_->estimate; }

QString ShellController::viewport_status() const
{
    if (viewport_->scene == nullptr) {
        return viewport_->diagnostic.isEmpty()
            ? QStringLiteral("No current evaluated geometry")
            : viewport_->diagnostic;
    }
    std::size_t edge_count{};
    for (const auto& instance : viewport_->scene->instances) {
        if (!instance.lods.empty() && instance.lods.front() != nullptr) {
            edge_count += instance.lods.front()->edge_polylines.size();
        }
    }
    return QStringLiteral("%1 source triangles · %2 edges · %3 instances · scene %4 ms")
        .arg(viewport_->scene->source_triangle_count)
        .arg(edge_count)
        .arg(viewport_->scene->instances.size())
        .arg(viewport_->build_milliseconds, 0, 'f', 1);
}

QVariant ShellController::viewportFrame(const double width, const double height) const
{
    QVariantList triangles;
    QVariantList edges;
    if (viewport_->scene == nullptr) {
        return QVariantMap{{QStringLiteral("triangles"), triangles}, {QStringLiteral("edges"), edges}};
    }
    const auto frame = viewport::project(*viewport_->scene, viewport_->camera, width, height);
    triangles.reserve(static_cast<qsizetype>(frame.triangles.size()));
    for (const auto& triangle : frame.triangles) {
        QVariantList points;
        for (const auto& point : triangle.points) {
            points.append(QVariantMap{{QStringLiteral("x"), point.x}, {QStringLiteral("y"), point.y}});
        }
        triangles.append(QVariantMap{
            {QStringLiteral("points"), points},
            {QStringLiteral("shade"), triangle.shade},
            {QStringLiteral("material"), QVariantList{
                triangle.material.base_color[0], triangle.material.base_color[1],
                triangle.material.base_color[2], triangle.material.base_color[3]}},
            {QStringLiteral("id"), QString::fromStdString(triangle.persistent_id)},
        });
    }
    edges.reserve(static_cast<qsizetype>(frame.edge_polylines.size()));
    for (const auto& edge : frame.edge_polylines) {
        QVariantList points;
        for (const auto& point : edge.points) {
            points.append(QVariantMap{{QStringLiteral("x"), point.x}, {QStringLiteral("y"), point.y}});
        }
        edges.append(QVariantMap{
            {QStringLiteral("points"), points},
            {QStringLiteral("id"), QString::fromStdString(edge.persistent_id)},
        });
    }
    return QVariantMap{{QStringLiteral("triangles"), triangles}, {QStringLiteral("edges"), edges}};
}

void ShellController::orbitViewport(const double delta_x, const double delta_y)
{
    viewport::orbit(viewport_->camera, delta_x, delta_y);
    emit viewport_changed();
}

void ShellController::panViewport(const double delta_x, const double delta_y)
{
    viewport::pan(viewport_->camera, delta_x, delta_y);
    emit viewport_changed();
}

void ShellController::zoomViewport(const double wheel_delta)
{
    viewport::zoom(viewport_->camera, wheel_delta);
    emit viewport_changed();
}

void ShellController::fitViewport()
{
    viewport::fit(viewport_->camera);
    emit viewport_changed();
}

void ShellController::setViewportClip(const double fraction)
{
    viewport::set_clip_fraction(viewport_->camera, fraction);
    emit viewport_changed();
}

void ShellController::preselectViewport(
    const double x, const double y, const double width, const double height)
{
    QString next;
    if (viewport_->scene != nullptr) {
        const auto frame = viewport::project(*viewport_->scene, viewport_->camera, width, height);
        if (const auto hit = viewport::hit_test(frame, {x, y}); hit.has_value()) {
            next = QString::fromStdString(*hit);
        }
    }
    if (next == viewport_->preselection) return;
    viewport_->preselection = std::move(next);
    emit viewport_changed();
}

void ShellController::selectViewport(
    const double x, const double y, const double width, const double height)
{
    QString next;
    if (viewport_->scene != nullptr) {
        const auto frame = viewport::project(*viewport_->scene, viewport_->camera, width, height);
        if (const auto hit = viewport::hit_test(frame, {x, y}); hit.has_value()) {
            next = QString::fromStdString(*hit);
        }
    }
    if (next == viewport_->selection) return;
    viewport_->selection = std::move(next);
    emit viewport_changed();
}

bool ShellController::prepareWorkerJob()
{
    reset_worker();
    if (!can_prepare_worker()) {
        worker_->message = QStringLiteral(
            "Worker preparation requires a current preflight-valid ASIR study.");
        emit worker_changed();
        return false;
    }

    // Preparing execution binds the previously authored draft to this solver
    // build and records the explicit local preparation action. This changes the
    // canonical ASIR fingerprint before the immutable worker job is created.
    auto execution_study = *study_;
    const auto timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString();
    execution_study.identity.solver_build =
        std::string(simulation_orchestrator::reference_worker_build_id);
    execution_study.provenance.solver_build_checksum =
        "builtin:" + std::string(simulation_orchestrator::reference_worker_build_id);
    execution_study.provenance.host_fingerprint = "desktop-local-reference";
    execution_study.provenance.created_at_utc = timestamp;
    execution_study.provenance.approvals.push_back({
        .id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(),
        .actor = "local-desktop-user",
        .action = "prepare reference preview execution",
        .timestamp_utc = timestamp,
    });
    execution_study.provenance.generated_operations.push_back(
        "prepared bounded homogeneous reference-worker job");
    const auto execution_report = simulation_ir::Validator{}.validate(
        execution_study, study_context());
    if (!execution_report.valid()) {
        worker_->message = QString::fromStdString(
            execution_report.issues.front().path + ": "
            + execution_report.issues.front().message);
        emit worker_changed();
        return false;
    }
    study_ = std::move(execution_study);
    emit document_changed();

    const auto parameter_length = [this](const std::string_view id, const double fallback) {
        const auto found = std::ranges::find(study_->parameters, id,
            &simulation_ir::ParameterDefinition::id);
        return found == study_->parameters.end() || found->nominal.value <= 0.0
            ? fallback : found->nominal.value;
    };
    const double length_x = parameter_length("width", 0.012);
    const double length_y = parameter_length("height", 0.010);
    const double length_z = parameter_length("depth", 0.008);
    // This deliberately bounded homogeneous grid proves the UI lifecycle. It
    // must not be confused with the heterogeneous CAD-to-grid preparation path.
    constexpr std::array<std::size_t, 3> counts{12, 10, 8};
    const em_mesh::CartesianGridSpec grid{
        .origin_m = {},
        .cell_counts = counts,
        .cell_size_m = {
            length_x / static_cast<double>(counts[0]),
            length_y / static_cast<double>(counts[1]),
            length_z / static_cast<double>(counts[2])},
        .material_assignment_threshold = 0.5,
        .maximum_cells = study_->fidelity_and_cost.budget.maximum_cells,
    };
    const simulation_cost::ReferenceHardwareCalibration calibration{
        .hardware_id = "interactive-reference/single-thread",
        .solver_build_id = std::string(simulation_orchestrator::reference_worker_build_id),
        .thread_count = 1,
        .cell_updates_per_second = 10'000'000.0,
        .relative_prediction_uncertainty = 0.25,
        .fixed_memory_overhead_bytes = 64'000,
        .bytes_per_cell_overhead = 16,
        .observation_count = 1,
    };
    const auto estimate = simulation_cost::ReferenceFdtdCostEstimator{}.estimate(
        {.grid = grid,
         .courant_factor = 0.8,
         .maximum_wave_speed_m_per_s = simulation_cost::vacuum_light_speed_m_per_s,
         .simulated_duration_s = 1.0e-10,
         .retained_checkpoint_count = 1,
         .result_storage_bytes = 0},
        calibration,
        study_->fidelity_and_cost.budget);
    if (!estimate.succeeded()) {
        worker_->message = estimate.diagnostics.empty()
            ? QStringLiteral("Reference cost estimation failed.")
            : QString::fromStdString(estimate.diagnostics.front().message);
        emit worker_changed();
        return false;
    }

    const auto& value = *estimate.estimate;
    worker_->job = em_solver_api::PreparedSimulationJob{
        .job_id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(),
        .study_fingerprint = study_fingerprint().toStdString(),
        .grid = {
            .cells_x = counts[0], .cells_y = counts[1], .cells_z = counts[2],
            .length_x_m = length_x, .length_y_m = length_y, .length_z_m = length_z,
            .relative_permittivity = 1.0, .relative_permeability = 1.0,
            .courant_factor = 0.8,
        },
        .total_time_steps = value.time_steps,
        .resource_estimate = {
            .grid_cells = value.grid_cells,
            .time_steps = value.time_steps,
            .estimated_peak_memory_bytes = value.estimated_peak_memory_bytes,
            .estimated_storage_bytes = value.estimated_storage_bytes,
            .runtime_upper_bound_s = value.runtime_upper_bound_s,
            .calibration_hardware_id = value.calibration_hardware_id,
            .calibration_solver_build_id = value.calibration_solver_build_id,
            .within_budget = value.within_budget,
            .requires_escalation = value.requires_escalation,
        },
        .resource_approval_id = std::nullopt,
    };
    worker_->worker = std::make_unique<simulation_orchestrator::ReferenceSimulationWorker>(
        "desktop-reference-worker",
        std::string(simulation_orchestrator::reference_worker_build_id));
    const auto validation = worker_->worker->validate(*worker_->job);
    if (!validation.valid()) {
        worker_->message = QString::fromStdString(validation.diagnostics.front().message);
        worker_->job.reset();
        emit worker_changed();
        return false;
    }
    worker_->estimate = format_worker_estimate(worker_->job->resource_estimate);
    worker_->message = QStringLiteral(
        "Prepared against ASIR %1 with calibrated reference-worker identity.")
        .arg(study_fingerprint());
    emit worker_changed();
    return true;
}

bool ShellController::launchWorker()
{
    if (!can_launch_worker() || worker_->worker == nullptr) return false;
    const auto result = worker_->worker->submit(*worker_->job);
    if (!result.succeeded()) {
        worker_->message = QString::fromStdString(result.diagnostics.front().message);
        emit worker_changed();
        return false;
    }
    worker_->state = *result.state;
    worker_->message = QStringLiteral("Reference job accepted; progress is worker-authoritative.");
    emit worker_changed();
    return true;
}

bool ShellController::advanceWorker()
{
    if (!worker_running() || worker_->worker == nullptr || !worker_->job.has_value()) return false;
    // Short slices let the QML timer repaint between calls while the worker
    // remains authoritative for progress and terminal phase transitions.
    const auto quantum = std::max<std::uint64_t>(
        1, worker_->job->total_time_steps / 12U);
    const auto result = worker_->worker->run_slice(worker_->job->job_id, quantum);
    if (!result.succeeded()) {
        worker_->message = QString::fromStdString(result.diagnostics.front().message);
        emit worker_changed();
        return false;
    }
    worker_->state = *result.state;
    if (worker_->state->phase == em_solver_api::JobPhase::succeeded) {
        worker_->message = QStringLiteral("Reference execution completed; collect raw artifacts.");
    }
    emit worker_changed();
    return true;
}

bool ShellController::cancelWorker()
{
    if (!worker_->state.has_value() || worker_->worker == nullptr) return false;
    const auto result = worker_->worker->cancel(worker_->state->job_id);
    if (!result.succeeded()) return false;
    worker_->state = *result.state;
    if (worker_->state->phase == em_solver_api::JobPhase::cancelling) {
        const auto terminal = worker_->worker->run_slice(worker_->state->job_id, 1);
        if (terminal.succeeded()) worker_->state = *terminal.state;
    }
    worker_->message = QStringLiteral("Cancellation acknowledged by the reference worker.");
    emit worker_changed();
    return true;
}

bool ShellController::checkpointWorker()
{
    if (!can_checkpoint_worker() || worker_->worker == nullptr) return false;
    const auto result = worker_->worker->checkpoint(worker_->state->job_id);
    if (!result.succeeded()) return false;
    worker_->checkpoint = *result.checkpoint;
    worker_->message = QStringLiteral(
        "Checkpoint captured at step %1. Save CAD + EM project to retain it across restart.")
        .arg(worker_->checkpoint->completed_time_steps);
    emit worker_changed();
    return true;
}

bool ShellController::restartWorkerFromCheckpoint()
{
    if (!worker_->checkpoint.has_value() || !worker_->job.has_value()) return false;
    auto restarted = std::make_unique<simulation_orchestrator::ReferenceSimulationWorker>(
        "desktop-reference-worker-restarted",
        std::string(simulation_orchestrator::reference_worker_build_id));
    const auto result = restarted->resume(*worker_->job, *worker_->checkpoint);
    if (!result.succeeded()) {
        worker_->message = QString::fromStdString(result.diagnostics.front().message);
        emit worker_changed();
        return false;
    }
    worker_->worker = std::move(restarted);
    worker_->state = *result.state;
    worker_->artifacts.reset();
    worker_->message = QStringLiteral("Fresh worker resumed the verified checkpoint.");
    emit worker_changed();
    return true;
}

bool ShellController::collectWorkerArtifacts()
{
    if (!can_collect_worker() || worker_->worker == nullptr) return false;
    const auto result = worker_->worker->collect(worker_->state->job_id);
    if (!result.succeeded()) return false;
    worker_->artifacts = *result.artifacts;
    worker_->message = QStringLiteral(
        "Collected %1-byte raw checkpoint artifact · study %2 · solver %3")
        .arg(worker_->artifacts->final_solver_checkpoint.size())
        .arg(QString::fromStdString(worker_->artifacts->study_fingerprint))
        .arg(QString::fromStdString(worker_->artifacts->solver_build_id));
    emit worker_changed();
    return true;
}

void ShellController::reset_worker()
{
    worker_ = std::make_unique<WorkerState>();
    emit worker_changed();
}

bool ShellController::newCadDocument()
{
#ifdef OEP_SHELL_OCCT
    try {
        auto candidate = std::make_unique<CadSession>();
        auto seed = modeler::make_reel2_cad_model();
        candidate->document = std::make_unique<document::Document>(
            std::move(seed.semantic_state), std::move(seed.feature_graph), candidate->evaluator);
        cad_session_ = std::move(candidate);
        attach_document(cad_session_->document.get());
        selectWorkspace(QStringLiteral("cad"));
        file_message_ = QStringLiteral("New CAD example. Dimensions use model units (mm).");
        emit document_changed();
        return true;
    } catch (const std::exception& error) {
        file_message_ = QString::fromUtf8(error.what());
    }
#else
    file_message_ = QStringLiteral("CAD editing requires the Qt + OCCT build.");
#endif
    emit document_changed();
    return false;
}

// Build and hydrate a separate candidate session first. Any decode, restore,
// or kernel failure preserves the active document and its editable state.
bool ShellController::loadDocument(const QString& path)
{
#ifdef OEP_SHELL_OCCT
    try {
        auto state = persistence::load_document_checkpoint(std::filesystem::path(path.toStdString()));
        auto candidate = std::make_unique<CadSession>();
        auto restored = document::Document::restore(state, candidate->evaluator);
        const auto hydration = candidate->evaluator.hydrate(state);
        if (!hydration.succeeded()) {
            file_message_ = QStringLiteral("Cannot reopen: ") +
                QString::fromStdString(hydration.diagnostics.front().message);
            emit document_changed();
            return false;
        }
        candidate->document = std::make_unique<document::Document>(std::move(restored));
        cad_session_ = std::move(candidate);
        attach_document(cad_session_->document.get());
        selectWorkspace(QStringLiteral("cad"));
        file_message_ = QStringLiteral("Opened ") + path;
        emit document_changed();
        return true;
    } catch (const std::exception& error) {
        file_message_ = QStringLiteral("Open failed: ") + QString::fromUtf8(error.what());
    }
#else
    Q_UNUSED(path)
    file_message_ = QStringLiteral("CAD loading requires the Qt + OCCT build.");
#endif
    emit document_changed();
    return false;
}

// Compound recovery preserves the original document identity and study snapshot.
// A stale/incomplete study stays stale/incomplete; loading must never rewrite it.
bool ShellController::saveProject(const QString& path)
{
    if (document_ == nullptr) return false;
    try {
        // Capture the latest running state at the same user-visible durability
        // boundary as CAD and ASIR. Prepared/queued jobs have no solver state
        // yet; completed work retains the last explicit restart point.
        if (can_checkpoint_worker() && !checkpointWorker()) {
            throw std::runtime_error("running worker could not produce a durable checkpoint");
        }
        std::optional<std::vector<std::byte>> worker_recovery;
        if (worker_->job.has_value() && worker_->checkpoint.has_value()) {
            worker_recovery = simulation_orchestrator::encode_worker_recovery({
                .job = *worker_->job,
                .checkpoint = *worker_->checkpoint,
            });
        }
        simulation_ir::save_project_checkpoint_atomic(std::filesystem::path(path.toStdString()),
            {document_->snapshot(), cad_document_id_, study_, std::move(worker_recovery)});
        file_message_ = QStringLiteral("Saved CAD, EM, and available worker recovery: ") + path;
        emit document_changed();
        return true;
    } catch (const std::exception& error) {
        file_message_ = QStringLiteral("Project save failed: ") + QString::fromUtf8(error.what());
        emit document_changed();
        return false;
    }
}

bool ShellController::loadProject(const QString& path)
{
#ifdef OEP_SHELL_OCCT
    try {
        auto project = simulation_ir::load_project_checkpoint(std::filesystem::path(path.toStdString()));
        auto candidate = std::make_unique<CadSession>();
        auto restored = document::Document::restore(project.document, candidate->evaluator);
        const auto hydration = candidate->evaluator.hydrate(project.document);
        if (!hydration.succeeded()) throw std::runtime_error(hydration.diagnostics.front().message);
        candidate->document = std::make_unique<document::Document>(std::move(restored));
        auto recovered_worker = std::make_unique<WorkerState>();
        if (project.worker_recovery.has_value()) {
            if (!project.study.has_value()) {
                throw std::runtime_error("worker recovery has no ASIR authority");
            }
            const auto current_geometry = candidate->document->current_geometry();
            if (!current_geometry.available()
                || project.study->identity.semantic_revision_id
                    != candidate->document->semantic_head_revision_id()
                || project.study->identity.evaluated_revision_id
                    != current_geometry.geometry->revision_id
                || project.study->identity.geometry_hash
                    != current_geometry.geometry->content_hash) {
                throw std::runtime_error(
                    "worker recovery requires the project's current evaluated CAD authority");
            }
            const auto decoded = simulation_orchestrator::decode_worker_recovery(
                *project.worker_recovery);
            if (!decoded.succeeded()) {
                throw std::runtime_error(decoded.diagnostics.front().message);
            }
            const auto expected_fingerprint = simulation_ir::canonical_fingerprint(*project.study);
            if (decoded.recovery->job.study_fingerprint != expected_fingerprint
                || project.study->identity.solver_build
                    != simulation_orchestrator::reference_worker_build_id) {
                throw std::runtime_error(
                    "worker recovery ASIR fingerprint or solver build does not match the project");
            }
            recovered_worker->worker =
                std::make_unique<simulation_orchestrator::ReferenceSimulationWorker>(
                    "desktop-reference-worker-restored",
                    std::string(simulation_orchestrator::reference_worker_build_id));
            const auto resumed = recovered_worker->worker->resume(
                decoded.recovery->job, decoded.recovery->checkpoint);
            if (!resumed.succeeded()) {
                throw std::runtime_error(resumed.diagnostics.front().message);
            }
            recovered_worker->job = std::move(decoded.recovery->job);
            recovered_worker->checkpoint = std::move(decoded.recovery->checkpoint);
            recovered_worker->state = *resumed.state;
            recovered_worker->estimate = format_worker_estimate(
                recovered_worker->job->resource_estimate);
            recovered_worker->message = QStringLiteral(
                "Recovered the verified desktop checkpoint at step %1.")
                .arg(recovered_worker->state->completed_time_steps);
        }
        // Activate only after every file, history, and exact-kernel gate passes.
        cad_session_ = std::move(candidate);
        attach_document(cad_session_->document.get());
        cad_document_id_ = std::move(project.cad_document_id);
        study_ = std::move(project.study);
        worker_ = std::move(recovered_worker);
        emit worker_changed();
        selectWorkspace(study_ ? QStringLiteral("em") : QStringLiteral("cad"));
        file_message_ = QStringLiteral("Opened CAD, EM, and available worker recovery: ") + path;
        emit document_changed();
        return true;
    } catch (const std::exception& error) {
        file_message_ = QStringLiteral("Project open failed: ") + QString::fromUtf8(error.what());
    }
#else
    Q_UNUSED(path)
    file_message_ = QStringLiteral("Project loading requires the Qt + OCCT build.");
#endif
    emit document_changed();
    return false;
}

bool ShellController::saveDocument(const QString& path)
{
    if (document_ == nullptr) return false;
    try {
        persistence::save_document_checkpoint_atomic(
            std::filesystem::path(path.toStdString()), document_->snapshot());
        file_message_ = QStringLiteral("Saved ") + path;
        emit document_changed();
        return true;
    } catch (const std::exception& error) {
        file_message_ = QStringLiteral("Save failed: ") + QString::fromUtf8(error.what());
        emit document_changed();
        return false;
    }
}

bool ShellController::can_prepare_study() const
{
#ifdef OEP_SHELL_OCCT
    return document_ != nullptr && cad_session_ != nullptr &&
           document_ == cad_session_->document.get() && document_->current_geometry().available();
#else
    return false;
#endif
}

bool ShellController::prepareStudy()
{
    if (!can_prepare_study()) return false;
    auto draft = simulation_ir::make_reel2_study_draft(*document_, cad_document_id_);
    if (!draft.has_value()) return false;
    study_ = std::move(draft);
    study_command_message_.clear();
    reset_worker();
    selectWorkspace(QStringLiteral("em"));
    emit document_changed();
    return true;
}

simulation_ir::ValidationContext ShellController::study_context() const
{
    if (document_ == nullptr) return {};
    auto context = simulation_ir::document_validation_context(*document_, cad_document_id_);
#ifdef OEP_SHELL_OCCT
    if (cad_session_ != nullptr && document_ == cad_session_->document.get() &&
        document_->current_geometry().available()) {
        for (const auto& feature : document_->current_evaluation().features) {
            if (feature.feature_id.value != modeler::hole_cut_feature_id || !feature.output_hash.has_value()) continue;
            const auto* artifact = cad_session_->evaluator.artifact(*feature.output_hash);
            if (artifact == nullptr) continue;
            for (const auto& binding : artifact->topology_bindings) {
                simulation_ir::EntityKind kind;
                switch (binding.entity.kind) {
                case modeler::TopologyKind::face: kind = simulation_ir::EntityKind::face; break;
                case modeler::TopologyKind::edge: kind = simulation_ir::EntityKind::edge; break;
                case modeler::TopologyKind::vertex: kind = simulation_ir::EntityKind::vertex; break;
                case modeler::TopologyKind::shell: kind = simulation_ir::EntityKind::shell; break;
                case modeler::TopologyKind::solid: kind = simulation_ir::EntityKind::solid; break;
                }
                context.available_topology.push_back({
                    {binding.persistent_id.feature_id, binding.persistent_id.local_id}, kind});
            }
        }
        std::ranges::sort(context.available_topology, {}, &simulation_ir::EntityReference::persistent_id);
    }
#endif
    return context;
}

std::vector<simulation_ir::EntityReference> ShellController::study_face_references() const
{
    std::vector<simulation_ir::EntityReference> faces;
    for (const auto& reference : study_context().available_topology) {
        if (reference.expected_kind == simulation_ir::EntityKind::face) faces.push_back(reference);
    }
    return faces;
}

QStringList ShellController::study_faces() const
{
    QStringList rows;
    for (const auto& reference : study_face_references()) {
        QString label = QString::fromStdString(reference.persistent_id.feature_id) + QStringLiteral(" / face ") +
                        QString::number(reference.persistent_id.local_id);
#ifdef OEP_SHELL_OCCT
        for (const auto& feature : document_->current_evaluation().features) {
            if (feature.feature_id.value != modeler::hole_cut_feature_id || !feature.output_hash.has_value()) continue;
            const auto* artifact = cad_session_->evaluator.artifact(*feature.output_hash);
            if (artifact == nullptr) continue;
            for (const auto& binding : artifact->topology_bindings) {
                if (binding.persistent_id.feature_id == reference.persistent_id.feature_id &&
                    binding.persistent_id.local_id == reference.persistent_id.local_id &&
                    !binding.ancestry.semantic_role.empty())
                    label += QStringLiteral(" · ") + QString::fromStdString(binding.ancestry.semantic_role);
            }
        }
#endif
        rows.append(label);
    }
    return rows;
}

bool ShellController::can_edit_study() const
{
    return study_.has_value() && simulation_ir::study_edit_authority(
        *study_, study_context(), study_fingerprint().toStdString()) == simulation_ir::StudyEditStatus::applied;
}

bool ShellController::present_study_result(const simulation_ir::StudyDefinitionResult& result)
{
    study_command_message_.clear();
    if (result.status != simulation_ir::StudyEditStatus::applied) {
        study_command_message_ = QStringLiteral("Definition edit rejected: check the current CAD/study snapshot and definition values.");
        for (const auto& issue : result.issues)
            study_command_message_ += QStringLiteral("\n") + QString::fromStdString(issue.path + ": " + issue.message);
    } else {
        reset_worker();
    }
    emit document_changed();
    return result.status == simulation_ir::StudyEditStatus::applied;
}

QStringList ShellController::study_material_ids() const
{
    QStringList ids;
    if (study_.has_value()) {
        for (const auto& material : study_->materials) ids.append(QString::fromStdString(material.id));
    }
    ids.sort();
    return ids;
}

QString ShellController::study_materials() const
{
    QStringList rows;
    if (study_.has_value()) {
        for (const auto& material : study_->materials) {
            QString row = QString::fromStdString(material.id) +
                QStringLiteral(" · εr %1 · μr %2 · σ %3 S/m · tanδ %4 · nondispersive")
                    .arg(material.relative_permittivity.value).arg(material.relative_permeability.value)
                    .arg(material.conductivity.value).arg(material.loss_tangent.value);
            if (material.validity_band.has_value())
                row += QStringLiteral(" · %1–%2 Hz").arg(material.validity_band->minimum.value).arg(material.validity_band->maximum.value);
            row += QStringLiteral("\n") + QString::fromStdString(material.provenance);
            rows.append(row);
        }
    }
    return rows.join(QStringLiteral("\n\n"));
}

QVariantMap ShellController::materialValues(const QString& id) const
{
    if (!study_.has_value()) return {};
    for (const auto& material : study_->materials) {
        if (material.id != id.toStdString()) continue;
        return {{QStringLiteral("id"), id},
            {QStringLiteral("permittivity"), material.relative_permittivity.value},
            {QStringLiteral("permeability"), material.relative_permeability.value},
            {QStringLiteral("conductivity"), material.conductivity.value},
            {QStringLiteral("lossTangent"), material.loss_tangent.value},
            {QStringLiteral("minimumFrequency"), material.validity_band.has_value()
                ? material.validity_band->minimum.value : study_minimum_frequency()},
            {QStringLiteral("maximumFrequency"), material.validity_band.has_value()
                ? material.validity_band->maximum.value : study_maximum_frequency()},
            {QStringLiteral("provenance"), QString::fromStdString(material.provenance)}};
    }
    return {};
}

QStringList ShellController::study_port_ids() const
{
    QStringList ids;
    if (study_.has_value()) for (const auto& port : study_->ports) ids.append(QString::fromStdString(port.id));
    ids.sort();
    return ids;
}

QString ShellController::study_setup() const
{
    QStringList rows;
    if (!study_.has_value()) return {};
    for (const auto& boundary : study_->boundaries)
        rows.append(QString::fromStdString(boundary.id) +
            (boundary.kind == simulation_ir::BoundaryKind::open_pml ? QStringLiteral(" · open PML") : QStringLiteral(" · PEC")) +
            QStringLiteral(" · background ") + QString::fromStdString(boundary.background_material_id) +
            QStringLiteral(" · padding %1 m").arg(boundary.padding.value));
    for (const auto& monitor : study_->monitors) {
        QStringList ports;
        for (const auto& port : monitor.port_ids) ports.append(QString::fromStdString(port));
        rows.append(QString::fromStdString(monitor.id) +
            (monitor.kind == simulation_ir::MonitorKind::s_parameters ? QStringLiteral(" · S parameters") : QStringLiteral(" · impedance")) +
            QStringLiteral(" · ports ") + ports.join(QStringLiteral(", ")) +
            QStringLiteral(" · %1–%2 Hz").arg(monitor.frequency_range.minimum.value).arg(monitor.frequency_range.maximum.value));
    }
    return rows.join(QStringLiteral("\n"));
}

bool ShellController::setStudyBoundary(
    const QString& id, const bool open_pml, const QString& material_id, const double padding,
    const QString& expected_fingerprint)
{
    if (!study_.has_value()) return false;
    simulation_ir::BoundaryDefinition boundary;
    boundary.id = id.toStdString();
    boundary.kind = open_pml ? simulation_ir::BoundaryKind::open_pml : simulation_ir::BoundaryKind::perfect_electric;
    boundary.background_material_id = material_id.toStdString();
    boundary.padding = {padding, simulation_ir::SiDimension::length};
    return present_study_result(simulation_ir::upsert_study_boundary(
        *study_, boundary, study_context(), expected_fingerprint.toStdString()));
}

bool ShellController::setStudyMonitor(
    const QString& id, const int kind, const QString& port_id, const double minimum_frequency,
    const double maximum_frequency, const QString& expected_fingerprint)
{
    if (!study_.has_value() || kind < 0 || kind > 1)
        return present_study_result({simulation_ir::StudyEditStatus::invalid_definition, {}});
    simulation_ir::MonitorDefinition monitor;
    monitor.id = id.toStdString();
    monitor.kind = kind == 0 ? simulation_ir::MonitorKind::s_parameters : simulation_ir::MonitorKind::impedance;
    monitor.port_ids = {port_id.toStdString()};
    monitor.frequency_range = {{minimum_frequency, simulation_ir::SiDimension::frequency},
                              {maximum_frequency, simulation_ir::SiDimension::frequency}};
    return present_study_result(simulation_ir::upsert_study_monitor(
        *study_, monitor, study_context(), expected_fingerprint.toStdString()));
}

bool ShellController::removeStudySetup(
    const bool boundary, const QString& id, const QString& expected_fingerprint)
{
    if (!study_.has_value()) return false;
    return present_study_result(boundary ? simulation_ir::remove_study_boundary(
        *study_, id.toStdString(), study_context(), expected_fingerprint.toStdString()) :
        simulation_ir::remove_study_monitor(*study_, id.toStdString(), study_context(), expected_fingerprint.toStdString()));
}

bool ShellController::setStudyMaterial(
    const QString& id, const double permittivity, const double permeability, const double conductivity,
    const double loss_tangent, const double minimum_frequency, const double maximum_frequency,
    const QString& provenance, const QString& expected_fingerprint)
{
    if (!study_.has_value()) return false;
    simulation_ir::MaterialDefinition material;
    material.id = id.toStdString();
    material.display_name = material.id;
    material.relative_permittivity = {permittivity, simulation_ir::SiDimension::relative_permittivity};
    material.relative_permeability = {permeability, simulation_ir::SiDimension::relative_permeability};
    material.conductivity = {conductivity, simulation_ir::SiDimension::conductivity};
    material.loss_tangent = {loss_tangent, simulation_ir::SiDimension::loss_tangent};
    material.validity_band = simulation_ir::FrequencyBand{
        {minimum_frequency, simulation_ir::SiDimension::frequency}, {maximum_frequency, simulation_ir::SiDimension::frequency}};
    material.provenance = provenance.toStdString();
    return present_study_result(simulation_ir::upsert_study_material(
        *study_, material, study_context(), expected_fingerprint.toStdString()));
}

bool ShellController::removeStudyMaterial(const QString& id, const QString& expected_fingerprint)
{
    if (!study_.has_value()) return false;
    return present_study_result(simulation_ir::remove_study_material(
        *study_, id.toStdString(), study_context(), expected_fingerprint.toStdString()));
}

bool ShellController::setStudyRole(
    const QString& id, const int face_index, const bool keep_out, const QString& expected_fingerprint)
{
    return assignStudyRole(id, face_index, keep_out ? 1 : 0, QStringLiteral("air"), expected_fingerprint);
}

bool ShellController::assignStudyRole(
    const QString& id, const int face_index, const int kind, const QString& material_id,
    const QString& expected_fingerprint)
{
    const auto faces = study_face_references();
    const std::array<simulation_ir::ElectromagneticRole, 5> roles{
        simulation_ir::ElectromagneticRole::air_or_vacuum, simulation_ir::ElectromagneticRole::keep_out,
        simulation_ir::ElectromagneticRole::conductor, simulation_ir::ElectromagneticRole::dielectric,
        simulation_ir::ElectromagneticRole::ground};
    if (!study_.has_value() || face_index < 0 || static_cast<std::size_t>(face_index) >= faces.size() ||
        kind < 0 || static_cast<std::size_t>(kind) >= roles.size())
        return present_study_result({simulation_ir::StudyEditStatus::invalid_definition, {}});
    simulation_ir::RoleAssignment role;
    role.id = id.toStdString();
    role.target = faces[static_cast<std::size_t>(face_index)];
    role.role = roles[static_cast<std::size_t>(kind)];
    if (role.role != simulation_ir::ElectromagneticRole::keep_out) role.material_id = material_id.toStdString();
    return present_study_result(simulation_ir::upsert_study_role(
        *study_, role, study_context(), expected_fingerprint.toStdString()));
}

bool ShellController::setStudyPort(
    const QString& id, const int face_index, const int plane_index, const double impedance,
    const int orientation, const QString& expected_fingerprint)
{
    const auto faces = study_face_references();
    if (!study_.has_value() || face_index < 0 || plane_index < 0 || orientation < 0 || orientation >= 6 ||
        static_cast<std::size_t>(face_index) >= faces.size() || static_cast<std::size_t>(plane_index) >= faces.size())
        return present_study_result({simulation_ir::StudyEditStatus::invalid_definition, {}});
    const std::array<simulation_ir::Direction3d, 6> directions{
        simulation_ir::Direction3d{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    simulation_ir::PortDefinition port;
    port.id = id.toStdString();
    port.face = faces[static_cast<std::size_t>(face_index)];
    port.reference_plane = faces[static_cast<std::size_t>(plane_index)];
    port.orientation = directions[static_cast<std::size_t>(orientation)];
    port.reference_impedance = {impedance, simulation_ir::SiDimension::impedance};
    port.deembedding_length = {0.0, simulation_ir::SiDimension::length};
    port.amplitude = {1.0, simulation_ir::SiDimension::electric_potential};
    port.phase = {0.0, simulation_ir::SiDimension::angle};
    return present_study_result(simulation_ir::upsert_study_port(
        *study_, port, study_context(), expected_fingerprint.toStdString()));
}

bool ShellController::removeStudyDefinition(
    const bool port, const QString& id, const QString& expected_fingerprint)
{
    if (!study_.has_value()) return false;
    if (port) return present_study_result(simulation_ir::remove_study_port(
        *study_, id.toStdString(), study_context(), expected_fingerprint.toStdString()));
    return present_study_result({simulation_ir::remove_study_definition(
        *study_, port, id.toStdString(), study_context(), expected_fingerprint.toStdString()), {}});
}

QString ShellController::study_definitions() const
{
    if (!study_.has_value()) return {};
    QStringList rows;
    const auto label = [](const simulation_ir::EntityReference& reference) {
        return QString::fromStdString(reference.persistent_id.feature_id) + QStringLiteral(" / face ") +
               QString::number(reference.persistent_id.local_id);
    };
    for (const auto& role : study_->roles) {
        QString kind;
        switch (role.role) {
        case simulation_ir::ElectromagneticRole::air_or_vacuum: kind = QStringLiteral("air/vacuum"); break;
        case simulation_ir::ElectromagneticRole::keep_out: kind = QStringLiteral("keep out"); break;
        case simulation_ir::ElectromagneticRole::conductor: kind = QStringLiteral("conductor"); break;
        case simulation_ir::ElectromagneticRole::dielectric: kind = QStringLiteral("dielectric"); break;
        case simulation_ir::ElectromagneticRole::ground: kind = QStringLiteral("ground"); break;
        default: kind = QStringLiteral("other EM role"); break;
        }
        rows.append(QString::fromStdString(role.id) + QStringLiteral(" · ") + kind + QStringLiteral(" · ") + label(role.target) +
            QStringLiteral(" · material ") + (role.material_id.has_value() ? QString::fromStdString(*role.material_id) : QStringLiteral("unassigned")));
    }
    for (const auto& port : study_->ports)
        rows.append(QString::fromStdString(port.id) + QStringLiteral(" · discrete · ") + label(port.face) +
            QStringLiteral(" → reference ") + label(port.reference_plane) +
            QStringLiteral(" · %1 Ω · orientation (%2,%3,%4) · matched, mode 0, 1 V, 0 rad, 0 m de-embedding")
                .arg(port.reference_impedance.value).arg(port.orientation.x).arg(port.orientation.y).arg(port.orientation.z));
    return rows.join(QStringLiteral("\n"));
}

simulation_ir::ValidationReport ShellController::study_report() const
{
    if (!study_.has_value() || document_ == nullptr) return {};
    return simulation_ir::Validator{}.validate(*study_,
        study_context());
}

QString ShellController::study_status() const
{
    if (!study_.has_value()) return QStringLiteral("No EM study prepared");
    if (document_ == nullptr || !document_->current_geometry().available())
        return QStringLiteral("CAD geometry unavailable · draft blocked");
    const auto report = study_report();
    if (report.contains(simulation_ir::ValidationCode::revision_mismatch) ||
        report.contains(simulation_ir::ValidationCode::geometry_hash_mismatch))
        return QStringLiteral("Stale CAD snapshot · rebuild draft");
    if (report.valid()) return QStringLiteral("ASIR valid · reference execution ready");
    if (can_prepare_worker()) return QStringLiteral("ASIR setup ready · prepare execution");
    return QStringLiteral("ASIR draft · setup incomplete");
}

QString ShellController::study_fingerprint() const
{
    return study_.has_value() ? QString::fromStdString(simulation_ir::canonical_fingerprint(*study_)) : QString{};
}

QString ShellController::study_diagnostics() const
{
    QStringList rows;
    if (!study_command_message_.isEmpty()) rows.append(study_command_message_);
    if (!study_.has_value()) rows.append(QStringLiteral("Create or open CAD, then prepare an EM draft."));
    for (const auto& issue : study_report().issues) {
        rows.append((issue.severity == simulation_ir::ValidationSeverity::error
                         ? QStringLiteral("Error · ") : QStringLiteral("Warning · ")) +
                    QString::fromStdString(issue.path + ": " + issue.message));
    }
    return rows.join(QStringLiteral("\n"));
}

QStringList ShellController::study_rows() const
{
    if (!study_.has_value()) return {};
    return {QStringLiteral("ASIR %1 · CAD revision %2")
                .arg(QString::fromStdString(study_->identity.version))
                .arg(study_->identity.semantic_revision_id.value),
            QStringLiteral("Exact geometry · %1 body").arg(study_->geometry.size()),
            QStringLiteral("Materials · %1").arg(study_->materials.size()),
            QStringLiteral("EM roles · %1").arg(study_->roles.size()),
            QStringLiteral("Ports · %1").arg(study_->ports.size()),
            QStringLiteral("Boundaries · %1").arg(study_->boundaries.size()),
            QStringLiteral("Monitors · %1").arg(study_->monitors.size())};
}

QString ShellController::study_summary() const
{
    if (!study_.has_value()) return QStringLiteral("No study loaded.");
    return QStringLiteral("CAD length parameters converted from mm to SI metres.\n"
        "FP64 FDTD draft policy · RMS exp(+jωt).\n"
        "Mesh defaults: 0.5–5 mm, 12 cells/wavelength.\n"
        "Budget defaults: 1,000,000 cells, 1 GiB RAM, 60 s, 1 GiB storage.\n"
        "Nondispersive material properties and role bindings are user-authored.\n"
        "Estimate binds execution provenance and prepares a bounded homogeneous reference job. Open PML remains setup only; 3-D CPML is not implemented.\n"
        "Worker progress, cancellation, session checkpoint restart, and raw artifact collection use the reference lifecycle contract.\n"
        "Save CAD + EM project retains authored setup; CAD-only checkpoints omit it.\n"
        "Snapshot fingerprint: ") + study_fingerprint();
}

double ShellController::study_minimum_frequency() const
{
    return study_.has_value() ? study_->solver_policy.frequency_range.minimum.value : 0.0;
}

double ShellController::study_maximum_frequency() const
{
    return study_.has_value() ? study_->solver_policy.frequency_range.maximum.value : 0.0;
}

bool ShellController::setStudyFrequency(
    const double minimum, const double maximum, const QString& expected_fingerprint)
{
    if (!study_.has_value()) return false;
    const auto status = simulation_ir::set_study_frequency(*study_,
        {{minimum, simulation_ir::SiDimension::frequency}, {maximum, simulation_ir::SiDimension::frequency}},
        expected_fingerprint.toStdString());
    if (status != simulation_ir::StudyEditStatus::applied) {
        study_command_message_ = QStringLiteral("Frequency edit rejected: use a current snapshot and a positive ordered band.");
        emit document_changed();
        return false;
    }
    study_command_message_.clear();
    reset_worker();
    emit document_changed();
    return true;
}

QString ShellController::document_revision() const
{
    return document_ == nullptr ? QString{} : QString::number(document_->semantic_head_revision_id().value);
}

void ShellController::selectFeature(const int index)
{
    if (document_ == nullptr || index < 0 ||
        static_cast<std::size_t>(index) >= document_->current_evaluation().features.size()) return;
    selected_feature_ = QString::fromStdString(
        document_->current_evaluation().features[static_cast<std::size_t>(index)].feature_id.value);
    emit document_changed();
}

QVariantList ShellController::parameter_rows() const
{
    QVariantList rows;
    if (document_ == nullptr) return rows;
    for (const auto& feature : document_->feature_graph()) {
        if (QString::fromStdString(feature.id.value) != selected_feature_) continue;
        for (const auto& id : feature.parameter_ids) {
            const auto found = document_->semantic_state().find(id);
            if (found == document_->semantic_state().end()) continue;
            rows.append(QVariantMap{{QStringLiteral("id"), QString::fromStdString(id)},
                {QStringLiteral("value"), QString::number(found->second, 'g', 17)},
                {QStringLiteral("revision"), document_revision()}});
        }
    }
    return rows;
}

QString ShellController::current_workspace() const { return current_workspace_; }
QString ShellController::product_name() const { return product_name_; }
QString ShellController::version() const { return version_; }
double ShellController::left_panel_width() const { return left_panel_width_; }
double ShellController::right_panel_width() const { return right_panel_width_; }
double ShellController::bottom_panel_height() const { return bottom_panel_height_; }
bool ShellController::left_panel_visible() const { return left_panel_visible_; }
bool ShellController::right_panel_visible() const { return right_panel_visible_; }
bool ShellController::bottom_panel_visible() const { return bottom_panel_visible_; }

// Changing document authority invalidates session study state and feature
// selection. A borrowed document must outlive its attachment to the controller.
void ShellController::attach_document(document::Document* const document)
{
    document_ = document;
    study_.reset();
    study_command_message_.clear();
    cad_document_id_ = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    selected_feature_.clear();
    reset_worker();
    refreshDocument();
}

void ShellController::refreshDocument()
{
    command_diagnostics_.clear();
    reset_worker();
    rebuild_viewport();
    emit document_changed();
}

void ShellController::rebuild_viewport()
{
    viewport_->scene.reset();
    viewport_->selection.clear();
    viewport_->preselection.clear();
    viewport_->diagnostic.clear();
#ifdef OEP_SHELL_OCCT
    if (document_ != nullptr && cad_session_ != nullptr
        && document_ == cad_session_->document.get()
        && document_->current_geometry().available()) {
        for (const auto& feature : document_->current_evaluation().features) {
            if (feature.feature_id.value != modeler::hole_cut_feature_id
                || !feature.output_hash.has_value()) continue;
            const auto* artifact = cad_session_->evaluator.artifact(*feature.output_hash);
            if (artifact == nullptr || !artifact->body.has_value()) continue;
            const auto started = std::chrono::steady_clock::now();
            auto result = tessellation::build_scene_packet(
                cad_session_->kernel,
                *artifact->body,
                document_->current_geometry().geometry->content_hash,
                artifact->topology_bindings);
            viewport_->build_milliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
            if (result.succeeded()) {
                auto mesh = std::make_shared<const tessellation::ScenePacket>(
                    std::move(result.packet));
                auto assembly = viewport::make_assembly({viewport::SceneInstance{
                    .persistent_instance_id = {},
                    .transform = {},
                    .material = {},
                    .visible = true,
                    .lods = {std::move(mesh)},
                }});
                viewport_->scene = std::make_shared<const viewport::SceneAssembly>(
                    std::move(assembly));
            } else if (!result.diagnostics.empty()) {
                viewport_->diagnostic = QString::fromStdString(result.diagnostics.front().message);
            }
            break;
        }
    }
#endif
    emit viewport_changed();
}

QString ShellController::document_status() const
{
    if (document_ == nullptr) return QStringLiteral("No document loaded");
    return QStringLiteral("%1 · revision %2")
        .arg(document_->current_geometry().available() ? QStringLiteral("Evaluated")
                                                     : QStringLiteral("Current geometry unavailable"))
        .arg(document_->semantic_head_revision_id().value);
}

QStringList project_feature_rows(std::span<const document::FeatureEvaluation> features)
{
    QStringList rows;
    for (const auto& feature : features) {
        QString state;
        switch (feature.state) {
        case document::FeatureState::clean: state = QStringLiteral("clean"); break;
        case document::FeatureState::dirty: state = QStringLiteral("dirty"); break;
        case document::FeatureState::evaluating: state = QStringLiteral("evaluating"); break;
        case document::FeatureState::succeeded: state = QStringLiteral("succeeded"); break;
        case document::FeatureState::failed: state = QStringLiteral("failed"); break;
        case document::FeatureState::blocked: state = QStringLiteral("blocked"); break;
        }
        rows.append(QString::fromStdString(feature.feature_id.value) + QStringLiteral(" · ") + state);
    }
    return rows;
}

QStringList ShellController::feature_rows() const
{
    return document_ == nullptr ? QStringList{} : project_feature_rows(document_->current_evaluation().features);
}

QStringList ShellController::diagnostic_rows() const
{
    QStringList rows = command_diagnostics_;
    if (document_ == nullptr) return rows;
    for (const auto& diagnostic : document_->current_evaluation().diagnostics) {
        rows.append(QString::fromStdString(diagnostic.code + ": " + diagnostic.message));
    }
    for (const auto& feature : document_->current_evaluation().features) {
        for (const auto& diagnostic : feature.diagnostics) {
            rows.append(QString::fromStdString(feature.feature_id.value + ": " +
                                               diagnostic.code + ": " + diagnostic.message));
        }
    }
    return rows;
}

bool ShellController::can_undo() const { return document_ != nullptr && document_->can_undo(); }
bool ShellController::can_redo() const { return document_ != nullptr && document_->can_redo(); }

bool ShellController::setParameter(
    const QString& id, const double value, const QString& expected_revision)
{
    if (document_ == nullptr) return false;
    bool valid = false;
    const auto revision = expected_revision.toULongLong(&valid);
    if (!valid) return false;
    const auto result = document_->apply(document::SetParameterCommand{
        {revision}, id.toStdString(), value});
    command_diagnostics_.clear();
    // Evaluation diagnostics are already projected from the document.
    if (result.status == document::CommandStatus::rejected) {
        for (const auto& diagnostic : result.diagnostics) {
            command_diagnostics_.append(QString::fromStdString(diagnostic.code + ": " + diagnostic.message));
        }
    }
    rebuild_viewport();
    if (result.status == document::CommandStatus::committed) reset_worker();
    emit document_changed();
    return result.status == document::CommandStatus::committed;
}

bool ShellController::undoDocument()
{
    if (document_ == nullptr || !document_->undo()) return false;
    refreshDocument();
    return true;
}

bool ShellController::redoDocument()
{
    if (document_ == nullptr || !document_->redo()) return false;
    refreshDocument();
    return true;
}

void ShellController::selectWorkspace(const QString& workspace)
{
    const QString normalized = workspace.trimmed().toLower();
    if ((normalized != QStringLiteral("cad") && normalized != QStringLiteral("em")) ||
        normalized == current_workspace_) {
        return;
    }
    current_workspace_ = normalized;
    emit workspace_changed();
}

void ShellController::setPanelVisible(const QString& panel, const bool visible)
{
    bool changed = false;
    if (panel == QStringLiteral("left") && left_panel_visible_ != visible) {
        left_panel_visible_ = visible;
        changed = true;
    } else if (panel == QStringLiteral("right") && right_panel_visible_ != visible) {
        right_panel_visible_ = visible;
        changed = true;
    } else if (panel == QStringLiteral("bottom") && bottom_panel_visible_ != visible) {
        bottom_panel_visible_ = visible;
        changed = true;
    }

    if (changed) {
        persist_layout();
        emit layout_changed();
    }
}

void ShellController::saveLayout(
    const double left_width,
    const double right_width,
    const double bottom_height,
    const bool left_visible,
    const bool right_visible,
    const bool bottom_visible)
{
    left_panel_width_ = bounded(left_width, 180.0, 520.0);
    right_panel_width_ = bounded(right_width, 220.0, 560.0);
    bottom_panel_height_ = bounded(bottom_height, 110.0, 420.0);
    left_panel_visible_ = left_visible;
    right_panel_visible_ = right_visible;
    bottom_panel_visible_ = bottom_visible;
    persist_layout();
    emit layout_changed();
}

void ShellController::resetLayout()
{
    left_panel_width_ = default_left_width;
    right_panel_width_ = default_right_width;
    bottom_panel_height_ = default_bottom_height;
    left_panel_visible_ = true;
    right_panel_visible_ = true;
    bottom_panel_visible_ = true;
    persist_layout();
    emit layout_changed();
}

void ShellController::load_layout()
{
    settings_->beginGroup(QStringLiteral("shell/layout-v1"));
    left_panel_width_ = bounded(
        settings_->value(QStringLiteral("left-width"), default_left_width).toDouble(),
        180.0,
        520.0);
    right_panel_width_ = bounded(
        settings_->value(QStringLiteral("right-width"), default_right_width).toDouble(),
        220.0,
        560.0);
    bottom_panel_height_ = bounded(
        settings_->value(QStringLiteral("bottom-height"), default_bottom_height).toDouble(),
        110.0,
        420.0);
    left_panel_visible_ = settings_->value(QStringLiteral("left-visible"), true).toBool();
    right_panel_visible_ = settings_->value(QStringLiteral("right-visible"), true).toBool();
    bottom_panel_visible_ = settings_->value(QStringLiteral("bottom-visible"), true).toBool();
    settings_->endGroup();
}

void ShellController::persist_layout()
{
    settings_->beginGroup(QStringLiteral("shell/layout-v1"));
    settings_->setValue(QStringLiteral("left-width"), left_panel_width_);
    settings_->setValue(QStringLiteral("right-width"), right_panel_width_);
    settings_->setValue(QStringLiteral("bottom-height"), bottom_panel_height_);
    settings_->setValue(QStringLiteral("left-visible"), left_panel_visible_);
    settings_->setValue(QStringLiteral("right-visible"), right_panel_visible_);
    settings_->setValue(QStringLiteral("bottom-visible"), bottom_panel_visible_);
    settings_->endGroup();
    settings_->sync();
}

} // namespace oep::app_shell
