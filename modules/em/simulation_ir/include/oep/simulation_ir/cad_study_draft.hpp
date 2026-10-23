// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

// Incremental ASIR authoring for the supported CAD example. Commands require
// both an expected study fingerprint and live CAD authority; successful edits
// may still leave an incomplete draft that full preflight reports separately.

#pragma once

#include "oep/simulation_ir/simulation_ir.hpp"

namespace oep::simulation_ir {

// Prepare a setup draft for the five-feature Reel 2 model, whose dimensions
// use millimetres. Roles, ports, monitors and execution provenance are left
// unset for explicit authoring. This draft is not an executable study.
[[nodiscard]] std::optional<AntennaStudy> make_reel2_study_draft(
    const document::Document& document, const std::string& cad_document_id);

// Obtain live geometry authority from the engine. Topology is supplied by the
// caller's identity service; no unresolved references are invented here.
[[nodiscard]] ValidationContext document_validation_context(
    const document::Document& document, const std::string& cad_document_id);

enum class StudyEditStatus { applied, stale_snapshot, stale_cad, invalid_band, invalid_definition };

[[nodiscard]] StudyEditStatus study_edit_authority(
    const AntennaStudy& study, const ValidationContext& context, std::string_view expected_fingerprint);

struct StudyDefinitionResult {
    StudyEditStatus status{StudyEditStatus::invalid_definition};
    std::vector<ValidationIssue> issues;
};

// Validate only the authored collection plus CAD authority, so an incomplete
// draft can be built incrementally. An invalid edit never mutates the study.
// Material band repairs can commit individually while other materials remain
// out of band; full preflight continues to report those outstanding issues.
[[nodiscard]] StudyDefinitionResult upsert_study_material(
    AntennaStudy& study, const MaterialDefinition& material, const ValidationContext& context,
    std::string_view expected_fingerprint);
[[nodiscard]] StudyDefinitionResult remove_study_material(
    AntennaStudy& study, std::string_view id, const ValidationContext& context,
    std::string_view expected_fingerprint);
[[nodiscard]] StudyDefinitionResult upsert_study_role(
    AntennaStudy& study, const RoleAssignment& role, const ValidationContext& context,
    std::string_view expected_fingerprint);
[[nodiscard]] StudyDefinitionResult upsert_study_port(
    AntennaStudy& study, const PortDefinition& port, const ValidationContext& context,
    std::string_view expected_fingerprint);

[[nodiscard]] StudyDefinitionResult upsert_study_boundary(
    AntennaStudy& study, const BoundaryDefinition& boundary, const ValidationContext& context,
    std::string_view expected_fingerprint);
[[nodiscard]] StudyDefinitionResult upsert_study_monitor(
    AntennaStudy& study, const MonitorDefinition& monitor, const ValidationContext& context,
    std::string_view expected_fingerprint);
[[nodiscard]] StudyDefinitionResult remove_study_boundary(
    AntennaStudy& study, std::string_view id, const ValidationContext& context, std::string_view expected_fingerprint);
[[nodiscard]] StudyDefinitionResult remove_study_monitor(
    AntennaStudy& study, std::string_view id, const ValidationContext& context, std::string_view expected_fingerprint);
[[nodiscard]] StudyDefinitionResult remove_study_port(
    AntennaStudy& study, std::string_view id, const ValidationContext& context, std::string_view expected_fingerprint);

// Explicit removal may leave an incomplete draft, which preflight reports.
[[nodiscard]] StudyEditStatus remove_study_definition(
    AntennaStudy& study, bool port, std::string_view id, const ValidationContext& context,
    std::string_view expected_fingerprint);

// Study-local command: compare the immutable content identity before mutation.
[[nodiscard]] StudyEditStatus set_study_frequency(
    AntennaStudy& study, const FrequencyBand& band, std::string_view expected_fingerprint);

} // namespace oep::simulation_ir
