// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#include <oep/simulation_ir/simulation_ir.hpp>
#include <oep/simulation_ir/project_checkpoint.hpp>
#include <oep/simulation_ir/cad_study_draft.hpp>
#include <limits>
#include <oep/ipc/frame.hpp>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

namespace {

using namespace oep::simulation_ir;

void check(const bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

[[nodiscard]] SiValue si(const double value, const SiDimension dimension)
{
    return SiValue{.value = value, .dimension = dimension};
}

[[nodiscard]] FrequencyBand band(const double minimum, const double maximum)
{
    return FrequencyBand{
        .minimum = si(minimum, SiDimension::frequency),
        .maximum = si(maximum, SiDimension::frequency),
    };
}

[[nodiscard]] EntityReference entity(
    std::string feature_id,
    const std::uint64_t local_id,
    const EntityKind kind)
{
    return EntityReference{
        .persistent_id = PersistentEntityId{
            .feature_id = std::move(feature_id),
            .local_id = local_id,
        },
        .expected_kind = kind,
    };
}

[[nodiscard]] ValidationContext valid_context()
{
    return ValidationContext{
        .cad_document_id = "cad-document-001",
        .semantic_revision_id = oep::document::RevisionId{42},
        .evaluated_revision_id = oep::document::RevisionId{42},
        .current_geometry_hash = "exact-body-sha256-001",
        .available_topology = {
            entity("base-extrude", 1, EntityKind::face),
            entity("hole-cut", 2, EntityKind::face),
            entity("edge-fillet", 3, EntityKind::face),
            entity("base-extrude", 4, EntityKind::solid),
        },
        .cad_parameter_ids = {"width"},
    };
}

[[nodiscard]] AntennaStudy valid_study()
{
    AntennaStudy study;
    study.identity = StudyIdentity{
        .study_id = "patch-study-001",
        .cad_document_id = "cad-document-001",
        .semantic_revision_id = oep::document::RevisionId{42},
        .evaluated_revision_id = oep::document::RevisionId{42},
        .candidate_id = std::nullopt,
        .geometry_hash = "exact-body-sha256-001",
        .version = "0.1",
        .solver_build = "reference-fdtd-0.1",
        .mesh_policy_hash = "mesh-policy-sha256-001",
    };
    study.coordinate_frames = {CoordinateFrame{
        .id = "world",
        .origin = {
            si(0.0, SiDimension::length),
            si(0.0, SiDimension::length),
            si(0.0, SiDimension::length),
        },
        .basis = {
            1.0, 0.0, 0.0,
            0.0, 1.0, 0.0,
            0.0, 0.0, 1.0,
        },
    }};
    study.geometry = {ExactBodyReference{
        .id = "radiator-body",
        .component_id = "radiator-component",
        .parent_component_id = std::nullopt,
        .exact_body_hash = "exact-body-sha256-001",
        .frame_id = "world",
    }};
    study.parameters = {
        ParameterDefinition{
            .id = "width",
            .display_name = "Patch width",
            .nominal = si(0.038, SiDimension::length),
            .bounds = ParameterBounds{
                .minimum = si(0.03, SiDimension::length),
                .maximum = si(0.05, SiDimension::length),
            },
            .discrete = false,
            .authority = ParameterAuthority::cad,
        },
        ParameterDefinition{
            .id = "grid-resolution",
            .display_name = "Grid resolution",
            .nominal = si(0.001, SiDimension::length),
            .bounds = ParameterBounds{
                .minimum = si(0.0005, SiDimension::length),
                .maximum = si(0.002, SiDimension::length),
            },
            .discrete = false,
            .authority = ParameterAuthority::study_local,
        },
        ParameterDefinition{
            .id = "predicted-resonance",
            .display_name = "Predicted resonance",
            .nominal = si(2.45e9, SiDimension::frequency),
            .bounds = std::nullopt,
            .discrete = false,
            .authority = ParameterAuthority::derived,
        },
    };
    study.materials = {
        MaterialDefinition{
            .id = "air",
            .display_name = "Air",
            .relative_permittivity = si(1.0, SiDimension::relative_permittivity),
            .relative_permeability = si(1.0, SiDimension::relative_permeability),
            .conductivity = si(0.0, SiDimension::conductivity),
            .loss_tangent = si(0.0, SiDimension::loss_tangent),
            .dispersion = DispersionModel::nondispersive,
            .thickness = std::nullopt,
            .roughness = std::nullopt,
            .validity_band = band(1.0e9, 10.0e9),
            .provenance = "CODATA dry-air approximation",
        },
        MaterialDefinition{
            .id = "copper",
            .display_name = "Copper",
            .relative_permittivity = si(1.0, SiDimension::relative_permittivity),
            .relative_permeability = si(1.0, SiDimension::relative_permeability),
            .conductivity = si(5.8e7, SiDimension::conductivity),
            .loss_tangent = si(0.0, SiDimension::loss_tangent),
            .dispersion = DispersionModel::nondispersive,
            .thickness = si(35.0e-6, SiDimension::length),
            .roughness = si(1.0e-6, SiDimension::length),
            .validity_band = band(1.0e9, 10.0e9),
            .provenance = "engineering-handbook conductivity at 20 C",
        },
    };
    study.roles = {
        RoleAssignment{
            .id = "ground-role",
            .role = ElectromagneticRole::ground,
            .target = entity("base-extrude", 1, EntityKind::face),
            .material_id = "copper",
        },
        RoleAssignment{
            .id = "background-role",
            .role = ElectromagneticRole::air_or_vacuum,
            .target = entity("base-extrude", 4, EntityKind::solid),
            .material_id = "air",
        },
    };
    study.ports = {PortDefinition{
        .id = "port-1",
        .kind = PortKind::discrete,
        .face = entity("hole-cut", 2, EntityKind::face),
        .reference_plane = entity("edge-fillet", 3, EntityKind::face),
        .orientation = Direction3d{0.0, 0.0, 1.0},
        .mode_index = 0,
        .reference_impedance = si(50.0, SiDimension::impedance),
        .deembedding_length = si(0.0, SiDimension::length),
        .amplitude = si(1.0, SiDimension::electric_potential),
        .phase = si(0.0, SiDimension::angle),
        .termination = PortTermination::matched,
    }};
    study.boundaries = {BoundaryDefinition{
        .id = "open-domain",
        .kind = BoundaryKind::open_pml,
        .background_material_id = "air",
        .padding = si(0.03, SiDimension::length),
        .paired_boundary_id = std::nullopt,
        .scan_theta = std::nullopt,
        .scan_phi = std::nullopt,
    }};
    study.solver_policy = SolverPolicy{
        .allowed_families = {SolverFamily::fdtd},
        .frequency_range = band(2.3e9, 2.6e9),
        .accuracy_target = si(0.02, SiDimension::dimensionless),
        .mesh = MeshPolicy{
            .minimum_cell_size = si(0.0005, SiDimension::length),
            .maximum_cell_size = si(0.005, SiDimension::length),
            .cells_per_wavelength = si(12.0, SiDimension::dimensionless),
            .adaptive_refinement = false,
            .allow_symmetry = false,
        },
        .precision = PrecisionPolicy::fp64_reference,
        .phasor_convention = PhasorConvention::exp_positive_jwt,
    };
    study.fidelity_and_cost = FidelityAndCostPolicy{
        .profile = FidelityProfile::verification,
        .budget = ResourceBudget{
            .maximum_cells = 50'000'000,
            .maximum_memory_bytes = 16ULL * 1024ULL * 1024ULL * 1024ULL,
            .maximum_runtime = si(7200.0, SiDimension::time),
            .maximum_storage_bytes = 50ULL * 1024ULL * 1024ULL * 1024ULL,
            .require_escalation_on_exceedance = true,
        },
    };
    study.monitors = {MonitorDefinition{
        .id = "s11",
        .kind = MonitorKind::s_parameters,
        .frequency_range = band(2.3e9, 2.6e9),
        .port_ids = {"port-1"},
        .region = std::nullopt,
    }};
    study.goals = {GoalDefinition{
        .id = "return-loss-goal",
        .expression = "max(abs(S(port-1,port-1)))",
        .frequency_range = band(2.4e9, 2.5e9),
        .weight = si(1.0, SiDimension::dimensionless),
        .tolerance = si(0.01, SiDimension::dimensionless),
        .pass_threshold = si(0.316227766, SiDimension::dimensionless),
    }};
    study.provenance = Provenance{
        .approvals = {ApprovalRecord{
            .id = "approval-1",
            .actor = "engineer@example.invalid",
            .action = "approved verification study",
            .timestamp_utc = "2026-10-09T18:00:00Z",
        }},
        .generated_operations = {"assign ground role", "create discrete port"},
        .solver_build_checksum = "sha256:solver-build-001",
        .host_fingerprint = "reference-workstation-linux-x86_64",
        .created_at_utc = "2026-10-09T18:00:00Z",
        .warnings = {},
        .raw_artifact_references = {},
    };
    return study;
}

void valid_study_is_accepted()
{
    const auto study = valid_study();
    const auto report = Validator{}.validate(study, valid_context());
    check(report.valid(), "representative ASIR 0.1 study validates");
    check(report.issues.empty(), "verification fixture has no validation warnings");
}

void fingerprint_is_deterministic_and_complete()
{
    const auto study = valid_study();
    const auto initial = canonical_fingerprint(study);
    check(initial == canonical_fingerprint(study), "ASIR fingerprint repeats deterministically");
    check(initial.starts_with("asir-0.1-sha256:"), "fingerprint declares its algorithm");
    check(initial.size() == std::string("asir-0.1-sha256:").size() + 64,
          "fingerprint contains a complete SHA-256 digest");

    auto reordered = study;
    std::ranges::reverse(reordered.materials);
    std::ranges::reverse(reordered.roles);
    check(initial == canonical_fingerprint(reordered), "collection order does not change fingerprint");

    auto changed = study;
    changed.materials[1].conductivity.value = 5.7e7;
    check(initial != canonical_fingerprint(changed), "material change invalidates fingerprint");
    changed = study;
    changed.identity.geometry_hash = "different-body";
    check(initial != canonical_fingerprint(changed), "CAD geometry change invalidates fingerprint");
}

void stale_or_failed_cad_state_is_rejected()
{
    auto study = valid_study();
    auto context = valid_context();
    context.evaluated_revision_id.reset();
    context.current_geometry_hash.reset();
    const auto failed = Validator{}.validate(study, context);
    check(!failed.valid(), "failed CAD semantic head cannot produce executable ASIR");
    check(failed.contains(ValidationCode::revision_mismatch), "failed head reports revision mismatch");
    check(failed.contains(ValidationCode::current_geometry_unavailable), "failed head reports unavailable geometry");

    context = valid_context();
    study.identity.geometry_hash = "stale-body";
    const auto stale = Validator{}.validate(study, context);
    check(stale.contains(ValidationCode::geometry_hash_mismatch), "stale geometry hash is rejected");
}

void invalid_units_references_and_ports_are_rejected()
{
    auto study = valid_study();
    study.ports.push_back(study.ports.front());
    study.ports.back().reference_impedance.dimension = SiDimension::unspecified;
    study.ports.back().face = entity("deleted-face", 99, EntityKind::face);
    const auto report = Validator{}.validate(study, valid_context());
    check(!report.valid(), "ambiguous port fixture is rejected");
    check(report.contains(ValidationCode::duplicate_identifier), "duplicate port ID is diagnosed");
    check(report.contains(ValidationCode::unspecified_dimension), "unqualified impedance is diagnosed");
    check(report.contains(ValidationCode::unknown_reference), "disappeared port face is diagnosed");
}

void invalid_material_frequency_and_authority_are_rejected()
{
    auto study = valid_study();
    study.materials[0].validity_band = band(1.0e9, 2.0e9);
    study.materials[1].dispersion = DispersionModel::debye;
    study.parameters[0].id = "unknown-cad-parameter";
    study.parameters[0].bounds->minimum.value = 0.06;
    study.parameters[2].bounds = ParameterBounds{
        .minimum = si(2.0e9, SiDimension::frequency),
        .maximum = si(3.0e9, SiDimension::frequency),
    };
    const auto report = Validator{}.validate(study, valid_context());
    check(!report.valid(), "invalid physics authority fixture is rejected");
    check(report.contains(ValidationCode::frequency_outside_material_validity),
          "material validity is checked against solver band");
    check(report.contains(ValidationCode::unsupported_material_model),
          "dispersive models without coefficients are rejected");
    check(report.contains(ValidationCode::parameter_authority_mismatch),
          "CAD and derived parameter authority errors are diagnosed");
    check(report.contains(ValidationCode::invalid_range),
          "bounds that exclude the nominal CAD value are diagnosed");
}

class DraftEvaluator final : public oep::document::Evaluator {
public:
    std::string_view version() const noexcept override { return "draft-authority-test"; }
    oep::document::FeatureEvaluationResult evaluate_feature(
        const oep::document::FeatureEvaluationRequest& request) override
    {
        if (request.semantic_state.at("width") <= 0.0) return {std::nullopt, {{"BadWidth", "invalid width", request.feature.id}}};
        return {request.feature.id.value + std::to_string(request.revision_id.value), {}};
    }
    std::optional<std::string> compose_geometry(
        const oep::document::RevisionId revision, const oep::document::SemanticState&,
        std::span<const oep::document::FeatureEvaluation>) override
    {
        return "evaluated-geometry-" + std::to_string(revision.value);
    }
};

void project_recovery_is_complete_bounded_and_honest()
{
    DraftEvaluator evaluator;
    oep::document::Document document({{"width", 20.0}}, {{{"base"}, {"width"}, {}}}, evaluator);
    auto study = valid_study();
    study.identity.semantic_revision_id = document.semantic_head_revision_id();
    study.identity.evaluated_revision_id = document.semantic_head_revision_id();
    study.identity.geometry_hash = document.current_geometry().geometry->content_hash;
    study.identity.candidate_id = "candidate-A";
    study.materials.front().thickness = si(0.001, SiDimension::length);
    study.materials.front().roughness = si(0.0001, SiDimension::length);
    study.boundaries.front().paired_boundary_id = "other";
    study.boundaries.front().scan_theta = si(0.2, SiDimension::angle);
    study.boundaries.front().scan_phi = si(0.3, SiDimension::angle);
    study.monitors.front().region = study.ports.front().face;
    const auto fingerprint = canonical_fingerprint(study);
    ProjectCheckpoint project{
        document.snapshot(), study.identity.cad_document_id, study, std::nullopt};
    const auto encoded = encode_project_checkpoint(project);
    auto reopened = decode_project_checkpoint(encoded);
    check(reopened.study && canonical_fingerprint(*reopened.study) == fingerprint, "every ASIR field survives project recovery");
    check(reopened.document.timeline == project.document.timeline, "CAD history travels with study");
    auto legacy_frame = *oep::ipc::decode_frame(encoded).frame;
    legacy_frame.payload.resize(legacy_frame.payload.size() - 8);
    legacy_frame.payload[8 + std::string("oep-project-checkpoint-2").size() - 1] = std::byte{'1'};
    const auto legacy = decode_project_checkpoint(oep::ipc::encode_frame(legacy_frame));
    check(!legacy.worker_recovery, "project-v1 recovery remains readable without worker state");
    project.worker_recovery = std::vector<std::byte>{std::byte{1}, std::byte{2}, std::byte{3}};
    reopened = decode_project_checkpoint(encode_project_checkpoint(project));
    check(reopened.worker_recovery == project.worker_recovery,
        "opaque bounded worker recovery travels atomically with CAD and ASIR");
    project.worker_recovery.reset();
    const auto rejects = [](const std::vector<std::byte>& bytes) {
        try { static_cast<void>(decode_project_checkpoint(bytes)); return false; }
        catch (const oep::persistence::CheckpointFormatError&) { return true; }
    };
    auto forged = *oep::ipc::decode_frame(encoded).frame;
    std::fill_n(forged.payload.begin(), 8, std::byte{0xff});
    check(rejects(oep::ipc::encode_frame(forged)), "valid checksum cannot bypass nested string allocation bounds");
    forged = *oep::ipc::decode_frame(encoded).frame;
    forged.payload[8 + std::string("oep-project-checkpoint-2").size() - 1] = std::byte{'9'};
    check(rejects(oep::ipc::encode_frame(forged)), "unsupported compound version is rejected");
    auto corrupt = encoded; corrupt.back() ^= std::byte{1};
    check(rejects(corrupt), "project integrity rejects bit corruption");
    corrupt = encoded; corrupt.pop_back();
    check(rejects(corrupt), "project rejects truncation");
    corrupt = encoded; corrupt.push_back(std::byte{0});
    check(rejects(corrupt), "project rejects trailing bytes");
    check(document.apply({document.semantic_head_revision_id(), "width", -1.0}).status == oep::document::CommandStatus::committed, "failed CAD edit commits for recovery test");
    project.document = document.snapshot();
    reopened = decode_project_checkpoint(encode_project_checkpoint(project));
    auto restored = oep::document::Document::restore(reopened.document, evaluator);
    check(!restored.current_geometry().available(), "project preserves failed CAD head");
    check(canonical_fingerprint(*reopened.study) == fingerprint, "failed CAD recovery does not rewrite study snapshot");
    check(restored.undo() && restored.current_geometry().available(), "project history supports Undo after recovery");
    project.study.reset();
    check(!decode_project_checkpoint(encode_project_checkpoint(project)).study, "CAD-only project is supported");
    project.study = study; project.study->identity.cad_document_id = "different";
    bool mismatch = false;
    try { static_cast<void>(encode_project_checkpoint(project)); }
    catch (const oep::persistence::CheckpointFormatError&) { mismatch = true; }
    check(mismatch, "compound identity mismatch is rejected");
}

void cad_draft_retains_explicit_engine_authority()
{
    DraftEvaluator evaluator;
    oep::document::Document document(
        {{"width", 40.0}, {"height", 30.0}, {"depth", 10.0}, {"fillet-radius", 2.0}, {"hole-size", 4.0}},
        {{{"base-sketch"}, {"width", "height"}, {}}, {{{"base-extrude"}}, {"depth"}, {}},
         {{{"edge-fillet"}}, {"fillet-radius"}, {}}, {{{"hole-sketch"}}, {"hole-size"}, {}},
         {{{"hole-cut"}}, {}, {}}}, evaluator);
    const auto draft = make_reel2_study_draft(document, "cad-test");
    check(draft.has_value(), "evaluated model prepares a draft");
    const auto width = std::ranges::find(draft->parameters, "width", &ParameterDefinition::id);
    check(width != draft->parameters.end() && width->nominal.value == 0.04 &&
          width->nominal.dimension == SiDimension::length && width->authority == ParameterAuthority::cad,
          "CAD millimetres become dimensioned SI metres with CAD authority");
    const auto context = document_validation_context(document, "cad-test");
    const auto report = Validator{}.validate(*draft, context);
    check(!report.valid() && report.contains(ValidationCode::invalid_port) &&
          report.contains(ValidationCode::missing_provenance), "draft leaves missing setup and provenance explicit");
    check(!report.contains(ValidationCode::revision_mismatch) &&
          !report.contains(ValidationCode::geometry_hash_mismatch), "initial draft matches actual CAD authority");
    const auto changed = document.apply(oep::document::SetParameterCommand{
        document.semantic_head_revision_id(), "width", 45.0});
    check(changed.evaluation_succeeded, "CAD mutation evaluates");
    const auto stale = Validator{}.validate(*draft, document_validation_context(document, "cad-test"));
    check(stale.contains(ValidationCode::revision_mismatch) && stale.contains(ValidationCode::geometry_hash_mismatch),
          "old draft rejects both changed revision and changed geometry");
    static_cast<void>(document.apply(oep::document::SetParameterCommand{
        document.semantic_head_revision_id(), "width", -1.0}));
    check(!make_reel2_study_draft(document, "cad-test").has_value(), "failed CAD cannot prepare a draft");
    const auto unavailable = Validator{}.validate(*draft, document_validation_context(document, "cad-test"));
    check(unavailable.contains(ValidationCode::current_geometry_unavailable), "last-good geometry is never accepted as current authority");
}

void definition_commands_validate_identity_and_preserve_failed_edits()
{
    auto study = valid_study();
    auto context = valid_context();
    auto role = study.roles.front();
    role.id = "new-role";
    const auto before = canonical_fingerprint(study);
    check(upsert_study_role(study, role, context, before).status == StudyEditStatus::applied,
          "resolved role commits in an otherwise valid study");
    const auto with_role = canonical_fingerprint(study);
    role.target.persistent_id.local_id = 9999;
    const auto invalid_role = upsert_study_role(study, role, context, with_role);
    check(invalid_role.status == StudyEditStatus::invalid_definition && !invalid_role.issues.empty(),
          "unresolved role is rejected with typed diagnostics");
    check(canonical_fingerprint(study) == with_role, "failed role replacement is atomic");
    role = study.roles.front();
    role.id.clear();
    check(upsert_study_role(study, role, context, with_role).status == StudyEditStatus::invalid_definition,
          "empty role ID is rejected");
    role.id = "bad-material";
    role.material_id = "unknown";
    check(upsert_study_role(study, role, context, with_role).status == StudyEditStatus::invalid_definition,
          "unknown role material is rejected");
    check(upsert_study_role(study, role, context, before).status == StudyEditStatus::stale_snapshot,
          "stale study content cannot author a definition");
    auto port = study.ports.front();
    port.reference_impedance.value = -50.0;
    check(upsert_study_port(study, port, context, with_role).status == StudyEditStatus::invalid_definition,
          "nonpositive port impedance is rejected");
    port.reference_impedance.value = 75.0;
    port.orientation = {0.0, 0.0, 0.0};
    check(upsert_study_port(study, port, context, with_role).status == StudyEditStatus::invalid_definition,
          "zero port orientation is rejected");
    port.orientation = {0.0, 0.0, 1.0};
    port.reference_plane.persistent_id.local_id = 9999;
    check(upsert_study_port(study, port, context, with_role).status == StudyEditStatus::invalid_definition,
          "unknown reference plane is rejected");
    check(canonical_fingerprint(study) == with_role, "all invalid port edits preserve study identity");
    port = study.ports.front();
    port.reference_impedance.value = 75.0;
    const auto count = study.ports.size();
    check(upsert_study_port(study, port, context, with_role).status == StudyEditStatus::applied && study.ports.size() == count,
          "same port ID replaces one definition without duplicates");
    const auto edited = canonical_fingerprint(study);
    context.semantic_revision_id.value += 1;
    check(upsert_study_port(study, port, context, edited).status == StudyEditStatus::stale_cad,
          "stale CAD prevents topology edits");
    check(remove_study_definition(study, true, port.id, context, edited) == StudyEditStatus::stale_cad,
          "stale CAD prevents removals");
    context = valid_context();
    context.current_geometry_hash.reset();
    check(upsert_study_port(study, port, context, edited).status == StudyEditStatus::stale_cad,
          "unavailable current geometry prevents definitions");
    context = valid_context();
    study.monitors.clear();
    check(remove_study_definition(study, true, port.id, context, canonical_fingerprint(study)) == StudyEditStatus::applied,
          "explicit port removal commits");
    check(Validator{}.validate(study, context).contains(ValidationCode::invalid_port),
          "removing last port exposes incomplete preflight");
    const auto removed = canonical_fingerprint(study);
    check(remove_study_definition(study, true, "missing", context, removed) == StudyEditStatus::invalid_definition &&
          canonical_fingerprint(study) == removed, "unknown removal leaves study intact");
}

void material_commands_validate_properties_bands_and_dependencies()
{
    auto study = valid_study();
    auto context = valid_context();
    auto material = study.materials.front();
    material.id = "user-material";
    material.display_name = "User material";
    material.relative_permittivity.value = 2.2;
    const auto initial = canonical_fingerprint(study);
    check(upsert_study_material(study, material, context, initial).status == StudyEditStatus::applied,
          "user material commits with dimensions and provenance");
    const auto added = canonical_fingerprint(study);
    material.relative_permittivity.value = -1.0;
    check(upsert_study_material(study, material, context, added).status == StudyEditStatus::invalid_definition,
          "negative relative permittivity is rejected");
    material.relative_permittivity.value = 2.2;
    material.provenance.clear();
    check(upsert_study_material(study, material, context, added).status == StudyEditStatus::invalid_definition,
          "missing provenance is rejected");
    material.provenance = "User assumption";
    material.conductivity = {1.0, SiDimension::length};
    check(upsert_study_material(study, material, context, added).status == StudyEditStatus::invalid_definition,
          "wrong conductivity units are rejected");
    material.conductivity = {std::numeric_limits<double>::quiet_NaN(), SiDimension::conductivity};
    check(upsert_study_material(study, material, context, added).status == StudyEditStatus::invalid_definition,
          "nonfinite material values are rejected");
    material.conductivity.value = 0.0;
    material.dispersion = DispersionModel::debye;
    check(upsert_study_material(study, material, context, added).status == StudyEditStatus::invalid_definition,
          "unsupported dispersive material is rejected");
    material.dispersion = DispersionModel::nondispersive;
    material.validity_band = band(1.0e9, 2.0e9);
    const auto bad_band = upsert_study_material(study, material, context, added);
    check(bad_band.status == StudyEditStatus::invalid_definition && !bad_band.issues.empty(),
          "material band excluding solver band is rejected with diagnostics");
    check(canonical_fingerprint(study) == added, "all invalid material edits leave content identity intact");
    material.validity_band = band(1.0e9, 10.0e9);
    material.relative_permittivity.value = 3.0;
    const auto count = study.materials.size();
    check(upsert_study_material(study, material, context, added).status == StudyEditStatus::applied &&
          study.materials.size() == count, "same material ID replaces values without duplicating");
    const auto replaced = canonical_fingerprint(study);
    check(upsert_study_material(study, material, context, initial).status == StudyEditStatus::stale_snapshot,
          "stale material command is rejected");
    context.semantic_revision_id.value += 1;
    check(upsert_study_material(study, material, context, replaced).status == StudyEditStatus::stale_cad,
          "stale CAD prevents material mutation");
    context = valid_context();
    check(remove_study_material(study, "air", context, replaced).status == StudyEditStatus::invalid_definition,
          "boundary and role references prevent material removal");
    auto boundary_only = study;
    boundary_only.roles.clear();
    check(remove_study_material(boundary_only, "air", context, canonical_fingerprint(boundary_only)).status == StudyEditStatus::invalid_definition,
          "boundary reference alone prevents material removal");
    auto role_only = study;
    role_only.boundaries.clear();
    check(remove_study_material(role_only, "copper", context, canonical_fingerprint(role_only)).status == StudyEditStatus::invalid_definition,
          "role reference alone prevents material removal");
    auto role = study.roles.front();
    role.role = ElectromagneticRole::dielectric;
    role.material_id = "user-material";
    check(upsert_study_role(study, role, context, replaced).status == StudyEditStatus::applied,
          "dielectric role selects the authored material");
    const auto assigned = canonical_fingerprint(study);
    check(remove_study_material(study, "user-material", context, assigned).status == StudyEditStatus::invalid_definition &&
          canonical_fingerprint(study) == assigned, "referenced user material cannot be deleted");
    role.material_id = "air";
    check(upsert_study_role(study, role, context, assigned).status == StudyEditStatus::applied, "role can reassign material");
    check(remove_study_material(study, "user-material", context, canonical_fingerprint(study)).status == StudyEditStatus::applied,
          "unreferenced material can be removed");
    const auto removed = canonical_fingerprint(study);
    check(remove_study_material(study, "missing", context, removed).status == StudyEditStatus::invalid_definition &&
          canonical_fingerprint(study) == removed, "unknown material deletion preserves snapshot");
}

void material_bands_can_be_repaired_one_at_a_time()
{
    auto study = valid_study();
    const auto context = valid_context();
    for (auto& material : study.materials) material.validity_band = band(2.3e9, 2.6e9);
    check(set_study_frequency(study, band(2.0e9, 3.0e9), canonical_fingerprint(study)) == StudyEditStatus::applied,
          "expanding a study band exposes material validity diagnostics");
    auto first = study.materials[0];
    first.validity_band = band(1.0e9, 4.0e9);
    check(upsert_study_material(study, first, context, canonical_fingerprint(study)).status == StudyEditStatus::applied,
          "one material can be repaired while another remains out of band");
    check(Validator{}.validate(study, context).contains(ValidationCode::frequency_outside_material_validity),
          "other material remains visibly out of band");
    auto second = study.materials[1];
    second.validity_band = band(1.0e9, 4.0e9);
    check(upsert_study_material(study, second, context, canonical_fingerprint(study)).status == StudyEditStatus::applied,
          "second material can be repaired incrementally");
    check(!Validator{}.validate(study, context).contains(ValidationCode::frequency_outside_material_validity),
          "all band diagnostics clear after both repairs");
}

void setup_commands_validate_dependencies_and_incremental_repairs()
{
    auto study = valid_study();
    const auto context = valid_context();
    auto boundary = study.boundaries.front();
    const auto initial = canonical_fingerprint(study);
    boundary.padding.value = -1.0;
    check(upsert_study_boundary(study, boundary, context, initial).status == StudyEditStatus::invalid_definition,
          "negative padding is rejected");
    boundary.padding = {0.03, SiDimension::time};
    check(upsert_study_boundary(study, boundary, context, initial).status == StudyEditStatus::invalid_definition,
          "padding has length units");
    boundary.padding = {0.04, SiDimension::length};
    boundary.background_material_id = "missing";
    check(upsert_study_boundary(study, boundary, context, initial).status == StudyEditStatus::invalid_definition,
          "unknown background material is rejected");
    boundary.background_material_id = "air";
    check(upsert_study_boundary(study, boundary, context, initial).status == StudyEditStatus::applied && study.boundaries.size() == 1,
          "same boundary ID replaces policy atomically");
    const auto updated = canonical_fingerprint(study);
    auto paired = study;
    auto paired_boundary = paired.boundaries.front();
    paired_boundary.id = "paired";
    paired_boundary.kind = BoundaryKind::periodic;
    paired_boundary.paired_boundary_id = boundary.id;
    paired.boundaries.push_back(paired_boundary);
    const auto before_paired_removal = canonical_fingerprint(paired);
    check(remove_study_boundary(paired, boundary.id, context, before_paired_removal).status == StudyEditStatus::invalid_definition &&
          canonical_fingerprint(paired) == before_paired_removal, "paired references protect boundary deletion");
    auto monitor = study.monitors.front();
    monitor.port_ids = {"missing"};
    check(upsert_study_monitor(study, monitor, context, updated).status == StudyEditStatus::invalid_definition,
          "monitor needs declared ports");
    monitor.port_ids = {study.ports.front().id};
    monitor.frequency_range = band(1e9, 3e9);
    check(upsert_study_monitor(study, monitor, context, updated).status == StudyEditStatus::invalid_definition,
          "out-of-solver-band monitor rejects");
    check(canonical_fingerprint(study) == updated, "invalid setup edits preserve content identity");
    monitor.frequency_range = study.solver_policy.frequency_range;
    monitor.kind = MonitorKind::impedance;
    check(upsert_study_monitor(study, monitor, context, updated).status == StudyEditStatus::applied,
          "monitor kind and band can replace an existing observable");
    const auto monitored = canonical_fingerprint(study);
    check(remove_study_port(study, study.ports.front().id, context, monitored).status == StudyEditStatus::invalid_definition,
          "monitors protect referenced port deletion");
    check(upsert_study_monitor(study, monitor, context, initial).status == StudyEditStatus::stale_snapshot,
          "stale setup command rejects");
    auto stale_context = context;
    stale_context.current_geometry_hash.reset();
    check(remove_study_monitor(study, monitor.id, stale_context, monitored).status == StudyEditStatus::stale_cad,
          "failed CAD blocks monitor deletion");
    check(remove_study_monitor(study, monitor.id, context, monitored).status == StudyEditStatus::applied,
          "explicit monitor removal succeeds");
    check(Validator{}.validate(study, context).contains(ValidationCode::invalid_monitor),
          "last-monitor removal exposes missing observable");
    check(remove_study_boundary(study, boundary.id, context, canonical_fingerprint(study)).status == StudyEditStatus::applied,
          "unpaired boundary removes");
    check(Validator{}.validate(study, context).contains(ValidationCode::invalid_boundary),
          "last-PML removal exposes incomplete open-domain setup");
    boundary.kind = BoundaryKind::perfect_electric;
    check(upsert_study_boundary(study, boundary, context, canonical_fingerprint(study)).status == StudyEditStatus::applied,
          "PEC policy may be authored while PML setup remains incomplete");
    check(Validator{}.validate(study, context).contains(ValidationCode::invalid_boundary),
          "PEC-only draft is not falsely treated as open-domain ready");
    boundary.kind = BoundaryKind::open_pml;
    check(upsert_study_boundary(study, boundary, context, canonical_fingerprint(study)).status == StudyEditStatus::applied,
          "PML policy repairs missing open-domain configuration");
    check(!Validator{}.validate(study, context).contains(ValidationCode::invalid_boundary), "PML diagnostics clear after repair");

    auto repair = valid_study();
    auto second = repair.monitors.front();
    second.id = "second-monitor";
    repair.monitors.push_back(second);
    check(set_study_frequency(repair, band(2.4e9, 2.5e9), canonical_fingerprint(repair)) == StudyEditStatus::applied,
          "narrowing solver band exposes multiple monitor range issues");
    const auto monitors_to_repair = repair.monitors;
    for (auto definition : monitors_to_repair) {
        definition.frequency_range = repair.solver_policy.frequency_range;
        check(upsert_study_monitor(repair, definition, context, canonical_fingerprint(repair)).status == StudyEditStatus::applied,
              "monitor ranges repair individually while other ranges remain invalid");
    }
    check(!Validator{}.validate(repair, context).contains(ValidationCode::invalid_monitor), "all monitor band issues clear after repairs");
}

void frequency_commands_preserve_snapshot_authority()
{
    auto study = valid_study();
    const auto original = canonical_fingerprint(study);
    check(set_study_frequency(study, band(2.0e9, 3.0e9), original) == StudyEditStatus::applied,
          "study-local band commits against content identity");
    const auto updated = canonical_fingerprint(study);
    check(updated != original, "band edit changes immutable identity");
    check(set_study_frequency(study, band(2.1e9, 3.1e9), original) == StudyEditStatus::stale_snapshot,
          "stale edit is rejected");
    check(set_study_frequency(study, band(-1.0, 3.0e9), updated) == StudyEditStatus::invalid_band,
          "negative frequency is rejected");
    check(set_study_frequency(study, band(3.0e9, 2.0e9), updated) == StudyEditStatus::invalid_band,
          "reversed frequency band is rejected");
    auto invalid = band(2.0e9, 3.0e9);
    invalid.minimum.dimension = SiDimension::length;
    check(set_study_frequency(study, invalid, updated) == StudyEditStatus::invalid_band,
          "dimension mismatch is rejected");
    invalid = band(std::numeric_limits<double>::quiet_NaN(), 3.0e9);
    check(set_study_frequency(study, invalid, updated) == StudyEditStatus::invalid_band,
          "non-finite frequency is rejected");
    check(canonical_fingerprint(study) == updated, "all rejected edits preserve study identity");
}

} // namespace

int main()
{
    project_recovery_is_complete_bounded_and_honest();
    cad_draft_retains_explicit_engine_authority();
    definition_commands_validate_identity_and_preserve_failed_edits();
    material_commands_validate_properties_bands_and_dependencies();
    material_bands_can_be_repaired_one_at_a_time();
    setup_commands_validate_dependencies_and_incremental_repairs();
    frequency_commands_preserve_snapshot_authority();
    valid_study_is_accepted();
    fingerprint_is_deterministic_and_complete();
    stale_or_failed_cad_state_is_rejected();
    invalid_units_references_and_ports_are_rejected();
    invalid_material_frequency_and_authority_are_rejected();
    std::cout << "simulation IR tests passed\n";
}
