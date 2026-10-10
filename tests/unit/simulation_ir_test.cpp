#include <oep/simulation_ir/simulation_ir.hpp>

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

} // namespace

int main()
{
    valid_study_is_accepted();
    fingerprint_is_deterministic_and_complete();
    stale_or_failed_cad_state_is_rejected();
    invalid_units_references_and_ports_are_rejected();
    invalid_material_frequency_and_authority_are_rejected();
    std::cout << "simulation IR tests passed\n";
}
