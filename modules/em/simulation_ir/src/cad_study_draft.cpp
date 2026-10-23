// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

// Incremental ASIR authoring for the supported CAD example. Commands require
// both an expected study fingerprint and live CAD authority; successful edits
// may still leave an incomplete draft that full preflight reports separately.

#include "oep/simulation_ir/cad_study_draft.hpp"

#include <cmath>
#include <array>
#include <algorithm>
#include <utility>

namespace oep::simulation_ir {

ValidationContext document_validation_context(
    const document::Document& document, const std::string& cad_document_id)
{
    ValidationContext context;
    context.cad_document_id = cad_document_id;
    context.semantic_revision_id = document.semantic_head_revision_id();
    context.evaluated_revision_id = document.evaluated_revision_id();
    const auto geometry = document.current_geometry();
    if (geometry.available()) context.current_geometry_hash = geometry.geometry->content_hash;
    for (const auto& [id, value] : document.semantic_state()) {
        static_cast<void>(value);
        context.cad_parameter_ids.push_back(id);
    }
    return context;
}

std::optional<AntennaStudy> make_reel2_study_draft(
    const document::Document& document, const std::string& cad_document_id)
{
    const auto geometry = document.current_geometry();
    if (!geometry.available() || cad_document_id.empty()) return std::nullopt;
    constexpr std::array<std::string_view, 5> parameters{
        "width", "height", "depth", "fillet-radius", "hole-size"};
    constexpr std::array<std::string_view, 5> features{
        "base-sketch", "base-extrude", "edge-fillet", "hole-sketch", "hole-cut"};
    if (document.semantic_state().size() != parameters.size() ||
        document.feature_graph().size() != features.size()) return std::nullopt;
    for (const auto id : parameters) {
        if (!document.semantic_state().contains(id)) return std::nullopt;
    }
    for (const auto id : features) {
        if (std::ranges::none_of(document.feature_graph(), [id](const auto& feature) {
            return feature.id.value == id;
        })) return std::nullopt;
    }
    AntennaStudy study;
    study.identity.study_id = "draft/" + cad_document_id;
    study.identity.cad_document_id = cad_document_id;
    study.identity.semantic_revision_id = document.semantic_head_revision_id();
    study.identity.evaluated_revision_id = geometry.geometry->revision_id;
    study.identity.geometry_hash = geometry.geometry->content_hash;
    // Solver build and approval provenance are deliberately unset until a
    // real prepared job names the worker that will execute it.
    study.identity.mesh_policy_hash = "reel2-preview-mesh-v1";
    CoordinateFrame world;
    world.id = "world";
    world.origin = {SiValue{0.0, SiDimension::length}, SiValue{0.0, SiDimension::length},
                    SiValue{0.0, SiDimension::length}};
    study.coordinate_frames.push_back(world);
    ExactBodyReference body;
    body.id = "cad-body";
    body.component_id = "cad-component";
    body.exact_body_hash = geometry.geometry->content_hash;
    body.frame_id = "world";
    study.geometry.push_back(body);
    for (const auto& [id, value] : document.semantic_state()) {
        ParameterDefinition parameter;
        parameter.id = id;
        parameter.display_name = id;
        parameter.nominal = {value * 0.001, SiDimension::length};
        parameter.authority = ParameterAuthority::cad;
        study.parameters.push_back(parameter);
    }
    MaterialDefinition air;
    air.id = "air";
    air.display_name = "Ideal vacuum background";
    air.relative_permittivity = {1.0, SiDimension::relative_permittivity};
    air.relative_permeability = {1.0, SiDimension::relative_permeability};
    air.conductivity = {0.0, SiDimension::conductivity};
    air.loss_tangent = {0.0, SiDimension::loss_tangent};
    air.provenance = "Ideal vacuum default; CAD materials are unassigned";
    study.materials.push_back(air);
    study.solver_policy.allowed_families = {SolverFamily::fdtd};
    study.solver_policy.frequency_range = {{2.3e9, SiDimension::frequency}, {2.6e9, SiDimension::frequency}};
    study.solver_policy.accuracy_target = {0.02, SiDimension::dimensionless};
    study.solver_policy.mesh.minimum_cell_size = {0.0005, SiDimension::length};
    study.solver_policy.mesh.maximum_cell_size = {0.005, SiDimension::length};
    study.solver_policy.mesh.cells_per_wavelength = {12.0, SiDimension::dimensionless};
    study.fidelity_and_cost.budget.maximum_cells = 1'000'000;
    study.fidelity_and_cost.budget.maximum_memory_bytes = 1ULL << 30U;
    study.fidelity_and_cost.budget.maximum_runtime = {60.0, SiDimension::time};
    study.fidelity_and_cost.budget.maximum_storage_bytes = 1ULL << 30U;
    return study;
}

StudyEditStatus set_study_frequency(
    AntennaStudy& study, const FrequencyBand& band, const std::string_view expected_fingerprint)
{
    if (canonical_fingerprint(study) != expected_fingerprint) return StudyEditStatus::stale_snapshot;
    if (band.minimum.dimension != SiDimension::frequency || band.maximum.dimension != SiDimension::frequency ||
        !std::isfinite(band.minimum.value) || !std::isfinite(band.maximum.value) ||
        band.minimum.value <= 0.0 || band.maximum.value < band.minimum.value) return StudyEditStatus::invalid_band;
    study.solver_policy.frequency_range = band;
    return StudyEditStatus::applied;
}

StudyEditStatus study_edit_authority(
    const AntennaStudy& study, const ValidationContext& context, const std::string_view expected_fingerprint)
{
    if (canonical_fingerprint(study) != expected_fingerprint) return StudyEditStatus::stale_snapshot;
    if (study.identity.cad_document_id != context.cad_document_id ||
        study.identity.semantic_revision_id != context.semantic_revision_id ||
        !context.evaluated_revision_id.has_value() ||
        study.identity.evaluated_revision_id != *context.evaluated_revision_id ||
        context.semantic_revision_id != *context.evaluated_revision_id ||
        !context.current_geometry_hash.has_value() ||
        study.identity.geometry_hash != *context.current_geometry_hash) return StudyEditStatus::stale_cad;
    return StudyEditStatus::applied;
}

namespace {

template <typename Definition>
StudyDefinitionResult upsert_definition(
    AntennaStudy& study, const Definition& definition, const ValidationContext& context,
    const std::string_view expected_fingerprint,
    std::vector<Definition> AntennaStudy::* collection, const std::string_view path_prefix,
    const bool isolate_definition = false)
{
    const auto authority = study_edit_authority(study, context, expected_fingerprint);
    if (authority != StudyEditStatus::applied) return {authority, {}};
    // Edit a copy so rejection is atomic. Filter diagnostics to the authored
    // collection (or definition for incremental repairs); full preflight still
    // reports missing or invalid setup elsewhere in the draft.
    auto candidate = study;
    auto& definitions = candidate.*collection;
    const auto found = std::ranges::find(definitions, definition.id, &Definition::id);
    if (found == definitions.end()) definitions.push_back(definition);
    else *found = definition;
    StudyDefinitionResult result;
    const std::string authored_path = std::string(path_prefix) + '[' + definition.id + ']';
    for (const auto& issue : Validator{}.validate(candidate, context).issues) {
        const bool belongs_to_definition = issue.path.starts_with(authored_path) ||
            issue.path == std::string(path_prefix) + ".id" || issue.code == ValidationCode::duplicate_identifier;
        if (issue.severity == ValidationSeverity::error && issue.path.starts_with(path_prefix) &&
            (!isolate_definition || belongs_to_definition)) result.issues.push_back(issue);
    }
    if (!result.issues.empty()) return result;
    study = std::move(candidate);
    result.status = StudyEditStatus::applied;
    return result;
}

} // namespace

StudyDefinitionResult upsert_study_material(
    AntennaStudy& study, const MaterialDefinition& material, const ValidationContext& context,
    const std::string_view expected_fingerprint)
{
    return upsert_definition(study, material, context, expected_fingerprint, &AntennaStudy::materials, "materials", true);
}

StudyDefinitionResult remove_study_material(
    AntennaStudy& study, const std::string_view id, const ValidationContext& context,
    const std::string_view expected_fingerprint)
{
    const auto authority = study_edit_authority(study, context, expected_fingerprint);
    if (authority != StudyEditStatus::applied) return {authority, {}};
    const auto referenced_role = std::ranges::any_of(study.roles, [id](const auto& role) {
        return role.material_id.has_value() && *role.material_id == id;
    });
    const auto referenced_boundary = std::ranges::any_of(study.boundaries, [id](const auto& boundary) {
        return boundary.background_material_id == id;
    });
    if (referenced_role || referenced_boundary) {
        return {StudyEditStatus::invalid_definition, {{ValidationSeverity::error,
            ValidationCode::unknown_reference, "materials[" + std::string(id) + "]",
            "material is referenced by a role or boundary; reassign or remove those references first"}}};
    }
    if (std::erase_if(study.materials, [id](const auto& material) { return material.id == id; }) == 0)
        return {StudyEditStatus::invalid_definition, {}};
    return {StudyEditStatus::applied, {}};
}

StudyDefinitionResult upsert_study_role(
    AntennaStudy& study, const RoleAssignment& role, const ValidationContext& context,
    const std::string_view expected_fingerprint)
{
    return upsert_definition(study, role, context, expected_fingerprint, &AntennaStudy::roles, "roles");
}

StudyDefinitionResult upsert_study_port(
    AntennaStudy& study, const PortDefinition& port, const ValidationContext& context,
    const std::string_view expected_fingerprint)
{
    return upsert_definition(study, port, context, expected_fingerprint, &AntennaStudy::ports, "ports");
}

StudyDefinitionResult upsert_study_boundary(
    AntennaStudy& study, const BoundaryDefinition& boundary, const ValidationContext& context,
    const std::string_view expected_fingerprint)
{
    return upsert_definition(study, boundary, context, expected_fingerprint, &AntennaStudy::boundaries, "boundaries", true);
}

StudyDefinitionResult upsert_study_monitor(
    AntennaStudy& study, const MonitorDefinition& monitor, const ValidationContext& context,
    const std::string_view expected_fingerprint)
{
    return upsert_definition(study, monitor, context, expected_fingerprint, &AntennaStudy::monitors, "monitors", true);
}

StudyDefinitionResult remove_study_boundary(
    AntennaStudy& study, const std::string_view id, const ValidationContext& context,
    const std::string_view expected_fingerprint)
{
    const auto authority = study_edit_authority(study, context, expected_fingerprint);
    if (authority != StudyEditStatus::applied) return {authority, {}};
    if (std::ranges::any_of(study.boundaries, [id](const auto& boundary) {
        return boundary.paired_boundary_id.has_value() && *boundary.paired_boundary_id == id;
    })) return {StudyEditStatus::invalid_definition, {{ValidationSeverity::error,
        ValidationCode::invalid_boundary, "boundaries[" + std::string(id) + "]", "boundary is referenced by a paired boundary"}}};
    if (std::erase_if(study.boundaries, [id](const auto& boundary) { return boundary.id == id; }) == 0)
        return {StudyEditStatus::invalid_definition, {}};
    return {StudyEditStatus::applied, {}};
}

StudyDefinitionResult remove_study_monitor(
    AntennaStudy& study, const std::string_view id, const ValidationContext& context,
    const std::string_view expected_fingerprint)
{
    const auto authority = study_edit_authority(study, context, expected_fingerprint);
    if (authority != StudyEditStatus::applied) return {authority, {}};
    if (std::erase_if(study.monitors, [id](const auto& monitor) { return monitor.id == id; }) == 0)
        return {StudyEditStatus::invalid_definition, {}};
    return {StudyEditStatus::applied, {}};
}

StudyDefinitionResult remove_study_port(
    AntennaStudy& study, const std::string_view id, const ValidationContext& context,
    const std::string_view expected_fingerprint)
{
    const auto authority = study_edit_authority(study, context, expected_fingerprint);
    if (authority != StudyEditStatus::applied) return {authority, {}};
    if (std::ranges::any_of(study.monitors, [id](const auto& monitor) {
        return std::ranges::find(monitor.port_ids, id) != monitor.port_ids.end();
    })) return {StudyEditStatus::invalid_definition, {{ValidationSeverity::error,
        ValidationCode::invalid_monitor, "ports[" + std::string(id) + "]", "port is referenced by a monitor; reassign or remove that monitor first"}}};
    if (std::erase_if(study.ports, [id](const auto& port) { return port.id == id; }) == 0)
        return {StudyEditStatus::invalid_definition, {}};
    return {StudyEditStatus::applied, {}};
}

StudyEditStatus remove_study_definition(
    AntennaStudy& study, const bool port, const std::string_view id, const ValidationContext& context,
    const std::string_view expected_fingerprint)
{
    const auto authority = study_edit_authority(study, context, expected_fingerprint);
    if (authority != StudyEditStatus::applied) return authority;
    if (port) {
        return remove_study_port(study, id, context, expected_fingerprint).status;
    } else {
        if (std::erase_if(study.roles, [id](const auto& item) { return item.id == id; }) == 0)
            return StudyEditStatus::invalid_definition;
    }
    return StudyEditStatus::applied;
}

} // namespace oep::simulation_ir
