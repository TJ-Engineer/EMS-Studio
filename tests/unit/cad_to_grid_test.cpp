// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#include <oep/em_geometry/computational_geometry.hpp>
#include <oep/em_mesh/cartesian_grid.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

namespace geometry = oep::em_geometry;
namespace mesh = oep::em_mesh;
namespace ir = oep::simulation_ir;

void check(const bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

[[nodiscard]] bool approximately_equal(
    const double left,
    const double right,
    const double tolerance = 1.0e-12)
{
    const auto scale = std::max({1.0, std::abs(left), std::abs(right)});
    return std::abs(left - right) <= tolerance * scale;
}

[[nodiscard]] ir::SiValue si(const double value, const ir::SiDimension dimension)
{
    return ir::SiValue{.value = value, .dimension = dimension};
}

[[nodiscard]] ir::EntityReference entity(
    std::string feature_id,
    const std::uint64_t local_id,
    const ir::EntityKind kind)
{
    return ir::EntityReference{
        .persistent_id = ir::PersistentEntityId{
            .feature_id = std::move(feature_id),
            .local_id = local_id,
        },
        .expected_kind = kind,
    };
}

[[nodiscard]] ir::MaterialDefinition material(
    std::string id,
    const double relative_permittivity,
    const double conductivity)
{
    return ir::MaterialDefinition{
        .id = id,
        .display_name = std::move(id),
        .relative_permittivity = si(
            relative_permittivity, ir::SiDimension::relative_permittivity),
        .relative_permeability = si(1.0, ir::SiDimension::relative_permeability),
        .conductivity = si(conductivity, ir::SiDimension::conductivity),
        .loss_tangent = si(0.0, ir::SiDimension::loss_tangent),
        .dispersion = ir::DispersionModel::nondispersive,
        .thickness = std::nullopt,
        .roughness = std::nullopt,
        .validity_band = std::nullopt,
        .provenance = "CAD-to-grid unit fixture",
    };
}

[[nodiscard]] ir::AntennaStudy study_fixture()
{
    ir::AntennaStudy study;
    study.identity = ir::StudyIdentity{
        .study_id = "cad-grid-spike",
        .cad_document_id = "cad-document",
        .semantic_revision_id = oep::document::RevisionId{7},
        .evaluated_revision_id = oep::document::RevisionId{7},
        .candidate_id = std::nullopt,
        .geometry_hash = "geometry-sha256-grid-spike",
        .version = "0.1",
        .solver_build = "reference-fdtd",
        .mesh_policy_hash = "uniform-1mm-grid",
    };
    study.materials = {
        material("air", 1.0, 0.0),
        material("fr4", 4.2, 0.0),
        material("copper", 1.0, 5.8e7),
    };
    study.roles = {
        ir::RoleAssignment{
            .id = "background-role",
            .role = ir::ElectromagneticRole::air_or_vacuum,
            .target = entity("domain", 1, ir::EntityKind::solid),
            .material_id = "air",
        },
        ir::RoleAssignment{
            .id = "substrate-role",
            .role = ir::ElectromagneticRole::dielectric,
            .target = entity("substrate", 2, ir::EntityKind::solid),
            .material_id = "fr4",
        },
        ir::RoleAssignment{
            .id = "ground-role",
            .role = ir::ElectromagneticRole::ground,
            .target = entity("ground", 3, ir::EntityKind::face),
            .material_id = "copper",
        },
        ir::RoleAssignment{
            .id = "symmetry-role",
            .role = ir::ElectromagneticRole::symmetry_region,
            .target = entity("symmetry", 6, ir::EntityKind::face),
            .material_id = std::nullopt,
        },
    };
    study.ports = {ir::PortDefinition{
        .id = "feed-port",
        .kind = ir::PortKind::waveguide,
        .face = entity("feed", 4, ir::EntityKind::face),
        .reference_plane = entity("feed-reference", 5, ir::EntityKind::face),
        .orientation = ir::Direction3d{1.0, 0.0, 0.0},
        .mode_index = 0,
        .reference_impedance = si(50.0, ir::SiDimension::impedance),
        .deembedding_length = si(0.002, ir::SiDimension::length),
        .amplitude = si(1.0, ir::SiDimension::electric_potential),
        .phase = si(0.0, ir::SiDimension::angle),
        .termination = ir::PortTermination::matched,
    }};
    return study;
}

[[nodiscard]] geometry::CadGeometrySnapshot snapshot_fixture()
{
    return geometry::CadGeometrySnapshot{
        .geometry_hash = "geometry-sha256-grid-spike",
        .entities = {
            geometry::ResolvedCadEntity{
                .source = entity("domain", 1, ir::EntityKind::solid),
                .geometry = geometry::AxisAlignedBox{
                    .minimum = {0.0, 0.0, 0.0},
                    .maximum = {0.004, 0.003, 0.002},
                },
            },
            geometry::ResolvedCadEntity{
                .source = entity("substrate", 2, ir::EntityKind::solid),
                .geometry = geometry::AxisAlignedBox{
                    .minimum = {0.00075, 0.0005, 0.0},
                    .maximum = {0.00225, 0.0025, 0.002},
                },
            },
            geometry::ResolvedCadEntity{
                .source = entity("ground", 3, ir::EntityKind::face),
                .geometry = geometry::PlanarPatch{
                    .normal_axis = geometry::Axis::z,
                    .inward_normal_sign = 1,
                    .plane_coordinate_m = 0.0,
                    .first_minimum_m = 0.0,
                    .first_maximum_m = 0.004,
                    .second_minimum_m = 0.0,
                    .second_maximum_m = 0.003,
                },
            },
            geometry::ResolvedCadEntity{
                .source = entity("feed", 4, ir::EntityKind::face),
                .geometry = geometry::PlanarPatch{
                    .normal_axis = geometry::Axis::x,
                    .inward_normal_sign = 1,
                    .plane_coordinate_m = 0.0,
                    .first_minimum_m = 0.001,
                    .first_maximum_m = 0.002,
                    .second_minimum_m = 0.0005,
                    .second_maximum_m = 0.0015,
                },
            },
            geometry::ResolvedCadEntity{
                .source = entity("feed-reference", 5, ir::EntityKind::face),
                .geometry = geometry::PlanarPatch{
                    .normal_axis = geometry::Axis::x,
                    .inward_normal_sign = 1,
                    .plane_coordinate_m = 0.002,
                    .first_minimum_m = 0.001,
                    .first_maximum_m = 0.002,
                    .second_minimum_m = 0.0005,
                    .second_maximum_m = 0.0015,
                },
            },
            geometry::ResolvedCadEntity{
                .source = entity("symmetry", 6, ir::EntityKind::face),
                .geometry = geometry::PlanarPatch{
                    .normal_axis = geometry::Axis::y,
                    .inward_normal_sign = 1,
                    .plane_coordinate_m = 0.0,
                    .first_minimum_m = 0.0,
                    .first_maximum_m = 0.004,
                    .second_minimum_m = 0.0,
                    .second_maximum_m = 0.002,
                },
            },
        },
    };
}

[[nodiscard]] mesh::CartesianGridSpec grid_spec()
{
    return mesh::CartesianGridSpec{
        .origin_m = {0.0, 0.0, 0.0},
        .cell_counts = {4, 3, 2},
        .cell_size_m = {0.001, 0.001, 0.001},
        .material_assignment_threshold = 0.5,
        .maximum_cells = 1'000,
    };
}

[[nodiscard]] geometry::ComputationalGeometry prepared_fixture()
{
    const auto prepared = geometry::GeometryPreparer{}.prepare(
        study_fixture(), snapshot_fixture());
    check(prepared.succeeded(), "valid CAD role snapshot must prepare");
    return *prepared.geometry;
}

void persistent_roles_and_ports_are_resolved()
{
    const auto prepared = geometry::GeometryPreparer{}.prepare(
        study_fixture(), snapshot_fixture());
    check(prepared.succeeded(), "valid CAD role snapshot must prepare");
    check(prepared.diagnostics.empty(), "valid CAD role snapshot must be diagnostic-free");
    check(
        prepared.geometry->study_fingerprint.starts_with("asir-0.1-sha256:"),
        "computational geometry must retain the ASIR content identity");
    check(
        prepared.geometry->geometry_hash == "geometry-sha256-grid-spike",
        "computational geometry must retain the exact CAD geometry hash");
    check(
        prepared.geometry->background.source.persistent_id.feature_id == "domain",
        "background mapping must retain its persistent CAD ID");
    check(
        prepared.geometry->material_regions.size() == 1
            && prepared.geometry->material_regions.front().source.persistent_id.feature_id
                == "substrate",
        "dielectric volume must retain its persistent CAD ID");
    check(prepared.geometry->surface_regions.size() == 2,
        "ground and symmetry surfaces must both transfer");
    check(
        std::ranges::any_of(
            prepared.geometry->surface_regions, [](const geometry::SurfaceRegion& value) {
                return value.role_assignment_id == "ground-role"
                    && value.source.persistent_id.feature_id == "ground"
                    && value.material_id == "copper";
            }),
        "ground surface must retain its persistent CAD and material IDs");
    check(
        std::ranges::any_of(
            prepared.geometry->surface_regions, [](const geometry::SurfaceRegion& value) {
                return value.role_assignment_id == "symmetry-role"
                    && value.source.persistent_id.feature_id == "symmetry"
                    && !value.material_id.has_value();
            }),
        "nonmaterial symmetry role must retain its persistent CAD ID");
    check(
        prepared.geometry->ports.size() == 1
            && prepared.geometry->ports.front().face_source.persistent_id.feature_id
                == "feed"
            && prepared.geometry->ports.front()
                    .reference_plane_source.persistent_id.feature_id
                == "feed-reference",
        "port face and reference plane must retain independent persistent CAD IDs");

    auto reordered_study = study_fixture();
    auto reordered_snapshot = snapshot_fixture();
    std::ranges::reverse(reordered_study.materials);
    std::ranges::reverse(reordered_study.roles);
    std::ranges::reverse(reordered_snapshot.entities);
    const auto reordered = geometry::GeometryPreparer{}.prepare(
        reordered_study, reordered_snapshot);
    check(reordered.succeeded(), "equivalent reordered CAD input must prepare");
    check(
        reordered.geometry->study_fingerprint == prepared.geometry->study_fingerprint,
        "ASIR collection order must not change prepared content identity");
    check(
        reordered.geometry->material_regions.front().role_assignment_id
                == prepared.geometry->material_regions.front().role_assignment_id
            && reordered.geometry->surface_regions.front().role_assignment_id
                == prepared.geometry->surface_regions.front().role_assignment_id,
        "prepared role mappings must use stable ID order");
    const auto original_grid = mesh::CartesianGridBuilder{}.build(
        *prepared.geometry, grid_spec());
    const auto reordered_grid = mesh::CartesianGridBuilder{}.build(
        *reordered.geometry, grid_spec());
    check(
        original_grid.succeeded() && reordered_grid.succeeded(),
        "equivalent prepared geometry must produce grids");
    check(
        original_grid.grid->material_cells.size()
            == reordered_grid.grid->material_cells.size(),
        "equivalent reordered input must preserve cell count");
    for (std::size_t index = 0; index < original_grid.grid->material_cells.size(); ++index) {
        const auto& original = original_grid.grid->material_cells[index];
        const auto& reordered_cell = reordered_grid.grid->material_cells[index];
        check(
            original.material_id == reordered_cell.material_id
                && original.role_assignment_id == reordered_cell.role_assignment_id
                && original.source_entity == reordered_cell.source_entity,
            "equivalent reordered input must preserve every material-cell assignment");
    }
}

void unresolved_and_stale_geometry_is_rejected()
{
    auto stale = snapshot_fixture();
    stale.geometry_hash = "stale-geometry";
    const auto stale_result = geometry::GeometryPreparer{}.prepare(study_fixture(), stale);
    check(!stale_result.succeeded(), "stale geometry snapshot must fail preparation");
    check(
        stale_result.contains(geometry::DiagnosticCode::geometry_hash_mismatch),
        "stale geometry must report its hash mismatch");

    auto missing = snapshot_fixture();
    missing.entities.erase(missing.entities.begin() + 1);
    const auto missing_result = geometry::GeometryPreparer{}.prepare(study_fixture(), missing);
    check(!missing_result.succeeded(), "missing role target must fail preparation");
    check(
        missing_result.contains(geometry::DiagnosticCode::unresolved_entity),
        "missing role target must report its persistent identity");

    auto invalid_orientation = study_fixture();
    invalid_orientation.ports.front().orientation = {0.0, 1.0, 0.0};
    const auto orientation_result = geometry::GeometryPreparer{}.prepare(
        invalid_orientation, snapshot_fixture());
    check(!orientation_result.succeeded(), "misoriented port must fail preparation");
    check(
        orientation_result.contains(geometry::DiagnosticCode::invalid_port_orientation),
        "misoriented port must produce a typed diagnostic");
}

void cells_preserve_material_and_port_provenance()
{
    const auto result = mesh::CartesianGridBuilder{}.build(prepared_fixture(), grid_spec());
    check(result.succeeded(), "resolved CAD geometry must voxelize");
    check(
        result.contains(mesh::DiagnosticCode::staircasing_risk),
        "under-resolved substrate and port must report staircasing risk");
    const auto& grid = *result.grid;
    check(grid.material_cells.size() == 24, "4 by 3 by 2 grid must contain 24 cells");
    check(
        std::ranges::any_of(grid.materials, [](const ir::MaterialDefinition& value) {
            return value.id == "fr4" && value.relative_permittivity.value == 4.2;
        }),
        "grid must retain the material catalog referenced by cell IDs");
    check(grid.region_mappings.size() == 1, "dielectric must have one region mapping");
    const auto& substrate = grid.region_mappings.front();
    check(substrate.role_assignment_id == "substrate-role", "role ID must reach grid mapping");
    check(substrate.material_id == "fr4", "material ID must reach grid mapping");
    check(
        substrate.source_entity.persistent_id.feature_id == "substrate",
        "region mapping must retain persistent CAD identity");
    check(substrate.cells.size() == 18, "offset substrate must intersect 18 cells");
    check(
        std::ranges::any_of(substrate.cells, [](const mesh::CellOccupancy& sample) {
            return sample.fraction > 0.0 && sample.fraction < 1.0;
        }),
        "subcell fractions must preserve cut-cell occupancy");

    const auto fr4_cells = static_cast<std::size_t>(std::ranges::count_if(
        grid.material_cells, [](const mesh::MaterialCell& cell) {
            return cell.material_id == "fr4"
                && cell.source_entity.persistent_id.feature_id == "substrate";
        }));
    check(fr4_cells == 6, "dominant-volume assignment must select six FR4 cells");
    const auto ground = std::ranges::find_if(
        grid.surface_mappings, [](const mesh::SurfaceCellMapping& mapping) {
            return mapping.role_assignment_id == "ground-role";
        });
    check(
        ground != grid.surface_mappings.end() && ground->cells.size() == 12
            && ground->source_entity.persistent_id.feature_id == "ground"
            && ground->role == ir::ElectromagneticRole::ground,
        "ground plane must map to twelve traceable surface cells");
    check(
        std::ranges::any_of(
            grid.surface_mappings, [](const mesh::SurfaceCellMapping& mapping) {
                return mapping.role_assignment_id == "symmetry-role"
                    && mapping.role == ir::ElectromagneticRole::symmetry_region
                    && mapping.source_entity.persistent_id.feature_id == "symmetry";
            }),
        "symmetry surface cells must retain role and CAD identity");
    check(
        grid.port_mappings.size() == 1
            && grid.port_mappings.front().face_cells.size() == 2
            && grid.port_mappings.front().reference_plane_cells.size() == 2
            && grid.port_mappings.front().kind == ir::PortKind::waveguide
            && grid.port_mappings.front().mode_index == 0
            && grid.port_mappings.front().reference_impedance_ohm == 50.0
            && grid.port_mappings.front().deembedding_length_m == 0.002,
        "port face and reference plane must map independently");
    check(
        grid.port_mappings.front().face_source.persistent_id.feature_id == "feed"
            && grid.port_mappings.front()
                    .reference_plane_source.persistent_id.feature_id
                == "feed-reference",
        "port cell mappings must retain both persistent CAD identities");

    const auto substrate_report = std::ranges::find_if(
        grid.staircasing, [](const mesh::StaircasingReport& report) {
            return report.mapping_id == "substrate-role";
        });
    check(substrate_report != grid.staircasing.end(), "substrate risk report must exist");
    check(
        substrate_report->risk == mesh::StaircasingRisk::high
            && approximately_equal(substrate_report->minimum_feature_cells, 1.5),
        "one-and-a-half-cell substrate dimension must be high risk");
    check(
        approximately_equal(
            substrate_report->exact_measure, substrate_report->subcell_measure, 1.0e-10),
        "subcell volume must conserve the exact box volume");

    auto threw = false;
    try {
        static_cast<void>(grid.cell({4, 0, 0}));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    check(threw, "out-of-range Cartesian cell access must fail explicitly");
}

void invalid_grid_and_material_conflicts_are_rejected()
{
    auto limited = grid_spec();
    limited.maximum_cells = 10;
    const auto too_large = mesh::CartesianGridBuilder{}.build(prepared_fixture(), limited);
    check(!too_large.succeeded(), "grid above hard cell limit must fail");
    check(
        too_large.contains(mesh::DiagnosticCode::cell_limit_exceeded),
        "cell-limit failure must be typed");

    auto invalid_threshold = grid_spec();
    invalid_threshold.material_assignment_threshold = 0.0;
    const auto invalid_policy = mesh::CartesianGridBuilder{}.build(
        prepared_fixture(), invalid_threshold);
    check(!invalid_policy.succeeded(), "implicit material threshold must not be accepted");
    check(
        invalid_policy.contains(mesh::DiagnosticCode::invalid_grid),
        "invalid material-assignment policy must be typed");

    auto outside = prepared_fixture();
    outside.material_regions.front().bounds.maximum.x = 0.005;
    const auto outside_result = mesh::CartesianGridBuilder{}.build(outside, grid_spec());
    check(!outside_result.succeeded(), "material outside domain must fail");
    check(
        outside_result.contains(mesh::DiagnosticCode::geometry_outside_grid),
        "out-of-domain material must be diagnosed");

    auto overlapping = prepared_fixture();
    auto duplicate = overlapping.material_regions.front();
    duplicate.role_assignment_id = "overlapping-role";
    duplicate.material_id = "copper";
    overlapping.material_regions.push_back(std::move(duplicate));
    const auto overlap_result = mesh::CartesianGridBuilder{}.build(overlapping, grid_spec());
    check(!overlap_result.succeeded(), "overlapping material volumes must fail");
    check(
        overlap_result.contains(mesh::DiagnosticCode::overlapping_material_regions),
        "overlapping material volumes must request explicit precedence");
}

} // namespace

int main()
{
    persistent_roles_and_ports_are_resolved();
    unresolved_and_stale_geometry_is_rejected();
    cells_preserve_material_and_port_provenance();
    invalid_grid_and_material_conflicts_are_rejected();
    std::cout << "CAD-to-grid identity, occupancy, port mapping, and staircasing checks passed\n";
}
