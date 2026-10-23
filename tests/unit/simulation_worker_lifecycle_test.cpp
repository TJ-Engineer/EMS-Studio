// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#include <oep/simulation_orchestrator/reference_worker.hpp>
#include <oep/simulation_orchestrator/worker_protocol.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

namespace api = oep::em_solver_api;
namespace orchestrator = oep::simulation_orchestrator;

constexpr auto solver_build = "reference-fdtd-worker-test-build";

void check(const bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

[[nodiscard]] api::PreparedSimulationJob job_fixture(std::string job_id)
{
    return api::PreparedSimulationJob{
        .job_id = std::move(job_id),
        .study_fingerprint = "asir-0.1-sha256:worker-lifecycle-fixture",
        .grid = api::StructuredGrid3dConfig{
            .cells_x = 6,
            .cells_y = 5,
            .cells_z = 4,
            .length_x_m = 0.006,
            .length_y_m = 0.005,
            .length_z_m = 0.004,
            .relative_permittivity = 1.0,
            .relative_permeability = 1.0,
            .courant_factor = 0.8,
        },
        .total_time_steps = 12,
        .resource_estimate = api::ResourceEstimateSnapshot{
            .grid_cells = 120,
            .time_steps = 12,
            .estimated_peak_memory_bytes = 64'000,
            .estimated_storage_bytes = 32'000,
            .runtime_upper_bound_s = 0.5,
            .calibration_hardware_id = "unit-test-host/single-thread",
            .calibration_solver_build_id = solver_build,
            .within_budget = true,
            .requires_escalation = false,
        },
        .resource_approval_id = std::nullopt,
    };
}

void validation_and_resource_gate_are_typed()
{
    orchestrator::ReferenceSimulationWorker worker("worker-validation", solver_build);
    auto invalid = job_fixture("");
    invalid.grid.cells_x = 0;
    invalid.total_time_steps = 0;
    invalid.resource_estimate.grid_cells = 1;
    invalid.resource_estimate.time_steps = 1;
    invalid.resource_estimate.requires_escalation = true;
    invalid.resource_estimate.within_budget = false;
    const auto report = worker.validate(invalid);
    check(!report.valid(), "invalid prepared job must fail validation");
    check(report.diagnostics.size() >= 4,
        "validation must retain independent identity, grid, estimate, and approval issues");

    auto over_budget = job_fixture("approved-job");
    over_budget.resource_estimate.within_budget = false;
    over_budget.resource_estimate.requires_escalation = true;
    const auto denied = worker.submit(over_budget);
    check(!denied.succeeded(), "unapproved over-budget job must not submit");
    check(denied.diagnostics.back().code
            == api::WorkerDiagnosticCode::resource_approval_required,
        "resource denial must use its typed diagnostic");
    over_budget.resource_approval_id = "approval-2026-10-10-001";
    check(worker.submit(over_budget).succeeded(),
        "explicitly approved over-budget job must submit");
}

void execution_checkpoint_restart_and_collection_are_consistent()
{
    orchestrator::ReferenceSimulationWorker first("worker-before-restart", solver_build);
    const auto request = job_fixture("restart-job");
    const auto submitted = first.submit(request);
    check(submitted.succeeded(), "valid prepared job must submit");
    check(submitted.state->phase == api::JobPhase::queued,
        "new job must begin queued");
    check(submitted.state->completed_time_steps == 0,
        "queued job must not imply solver progress");

    const auto partial = first.run_slice(request.job_id, 5);
    check(partial.succeeded() && partial.state->phase == api::JobPhase::running,
        "bounded slice must move queued job to running");
    check(partial.state->completed_time_steps == 5,
        "bounded slice must report exact completed steps");
    check(partial.state->progress > 0.4 && partial.state->progress < 0.5,
        "running progress must reflect completed and total steps");

    const auto saved = first.checkpoint(request.job_id);
    check(saved.succeeded(), "running job must produce a checkpoint");
    check(saved.checkpoint->completed_time_steps == 5,
        "checkpoint metadata must match solver progress");
    check(!saved.checkpoint->payload.empty(),
        "checkpoint must retain solver field state");

    orchestrator::ReferenceSimulationWorker restarted(
        "worker-after-restart", solver_build);
    const auto resumed = restarted.resume(request, *saved.checkpoint);
    check(resumed.succeeded(), "fresh worker must resume a valid checkpoint");
    check(resumed.state->phase == api::JobPhase::running
            && resumed.state->completed_time_steps == 5,
        "resumed job must preserve phase and exact progress");
    check(resumed.state->worker_id == "worker-after-restart",
        "resumed status must identify the current worker");

    const auto completed = restarted.run_slice(request.job_id, 100);
    check(completed.succeeded()
            && completed.state->phase == api::JobPhase::succeeded,
        "slice larger than remaining work must stop at successful completion");
    check(completed.state->completed_time_steps == request.total_time_steps
            && completed.state->progress == 1.0,
        "successful state must report exact final progress");
    const auto artifacts = restarted.collect(request.job_id);
    check(artifacts.succeeded(), "successful job must expose raw artifacts");
    check(artifacts.artifacts->study_fingerprint == request.study_fingerprint,
        "raw artifacts must retain ASIR identity");
    check(artifacts.artifacts->completed_time_steps == request.total_time_steps,
        "raw artifacts must retain final solver progress");
    check(!artifacts.artifacts->final_solver_checkpoint.empty(),
        "raw artifact must contain restartable final field state");
    check(!restarted.cancel(request.job_id).succeeded(),
        "successful terminal job must reject cancellation");
}

void cancellation_and_checkpoint_mismatch_do_not_leak_results()
{
    orchestrator::ReferenceSimulationWorker worker("worker-cancel", solver_build);
    const auto request = job_fixture("cancel-job");
    check(worker.submit(request).succeeded(), "cancellation fixture must submit");
    check(worker.run_slice(request.job_id, 3).succeeded(),
        "cancellation fixture must begin execution");
    const auto checkpoint = worker.checkpoint(request.job_id);
    check(checkpoint.succeeded(), "running cancellation fixture must checkpoint");
    const auto cancelled = worker.cancel(request.job_id);
    check(cancelled.succeeded() && cancelled.state->phase == api::JobPhase::cancelled,
        "cancellation must produce an explicit terminal state");
    check(!worker.collect(request.job_id).succeeded(),
        "cancelled job must not expose successful artifacts");
    check(!worker.checkpoint(request.job_id).succeeded(),
        "cancelled state must not be presented as a current checkpoint");
    check(worker.cancel(request.job_id).succeeded(),
        "cancellation must be idempotent");

    orchestrator::ReferenceSimulationWorker restarted("worker-other", solver_build);
    auto mismatched = *checkpoint.checkpoint;
    mismatched.study_fingerprint = "asir-0.1-sha256:different-study";
    const auto refused = restarted.resume(request, mismatched);
    check(!refused.succeeded(), "mismatched checkpoint must not resume");
    check(refused.diagnostics.front().code
            == api::WorkerDiagnosticCode::checkpoint_mismatch,
        "checkpoint mismatch must remain typed");

    auto mislabeled = *checkpoint.checkpoint;
    mislabeled.checkpoint_id = "checkpoint-fnv1a64:0000000000000000";
    const auto identity_refused = restarted.resume(request, mislabeled);
    check(!identity_refused.succeeded(),
        "checkpoint with a false content identity must not resume");
    check(identity_refused.diagnostics.front().code
            == api::WorkerDiagnosticCode::checkpoint_mismatch,
        "checkpoint identity mismatch must remain typed");

    check(!worker.status("missing-job").succeeded(),
        "unknown status request must fail without creating state");
}

void protocol_version_mismatch_is_typed()
{
    auto frame = orchestrator::encode_worker_request(
        orchestrator::WorkerRequest{
            .correlation_id = 81,
            .command = orchestrator::StatusCommand{.job_id = "version-job"},
        });
    check(frame.payload.size() > 4,
        "worker request must encode its contract version");
    frame.payload[4] ^= std::byte{0x01};
    const auto rejected = orchestrator::decode_worker_request(frame);
    check(!rejected.succeeded(),
        "unsupported worker contract version must fail before dispatch");
    check(rejected.diagnostics.front().code
            == api::WorkerDiagnosticCode::protocol_error,
        "worker contract mismatch must remain a typed protocol error");
}

void durable_recovery_record_is_bounded_and_versioned()
{
    const auto request = job_fixture("durable-job");
    orchestrator::ReferenceSimulationWorker worker("durable-source", solver_build);
    check(worker.submit(request).succeeded(), "durable fixture submits");
    check(worker.run_slice(request.job_id, 4).succeeded(), "durable fixture advances");
    const auto checkpoint = worker.checkpoint(request.job_id);
    check(checkpoint.succeeded(), "durable fixture checkpoints");

    const orchestrator::WorkerRecoveryRecord record{
        .job = request,
        .checkpoint = *checkpoint.checkpoint,
    };
    const auto encoded = orchestrator::encode_worker_recovery(record);
    const auto decoded = orchestrator::decode_worker_recovery(encoded);
    check(decoded.succeeded() && *decoded.recovery == record,
        "durable worker record round-trips exact job and solver state");

    auto corrupt = encoded;
    corrupt.back() ^= std::byte{1};
    check(!orchestrator::decode_worker_recovery(corrupt).succeeded(),
        "durable worker record rejects checksum corruption");

    const auto submit_only = oep::ipc::encode_frame(
        orchestrator::encode_worker_request({
            .correlation_id = 0,
            .command = orchestrator::SubmitCommand{.job = request},
        }));
    check(!orchestrator::decode_worker_recovery(submit_only).succeeded(),
        "durable worker record rejects a non-resume operation");
}

} // namespace

int main()
{
    validation_and_resource_gate_are_typed();
    execution_checkpoint_restart_and_collection_are_consistent();
    cancellation_and_checkpoint_mismatch_do_not_leak_results();
    protocol_version_mismatch_is_typed();
    durable_recovery_record_is_bounded_and_versioned();
    std::cout << "Simulation worker validation, execution, cancellation, restart, and collection checks passed\n";
    return EXIT_SUCCESS;
}
