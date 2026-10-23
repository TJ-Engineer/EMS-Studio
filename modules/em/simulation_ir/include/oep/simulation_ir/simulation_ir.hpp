// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

// ASIR is the solver-neutral study contract, including dimensioned SI values,
// CAD authority, persistent references, and execution provenance. Structural
// validation and canonical content identity are separate from solving a study.

#pragma once

#include <oep/document/document.hpp>

#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oep::simulation_ir {

inline constexpr std::string_view asir_version = "0.1";

enum class SiDimension {
    unspecified,
    dimensionless,
    length,
    time,
    frequency,
    angle,
    conductivity,
    relative_permittivity,
    relative_permeability,
    loss_tangent,
    impedance,
    electric_potential,
    power,
};

// The dimension tag is part of the contract; a numerically plausible value
// with an unspecified or wrong dimension must not pass validation.
struct SiValue {
    double value{};
    SiDimension dimension{SiDimension::unspecified};

    bool operator==(const SiValue&) const = default;
};

struct FrequencyBand {
    SiValue minimum;
    SiValue maximum;
};

struct Direction3d {
    double x{};
    double y{};
    double z{};

    bool operator==(const Direction3d&) const = default;
};

struct PersistentEntityId {
    std::string feature_id;
    std::uint64_t local_id{};

    auto operator<=>(const PersistentEntityId&) const = default;
};

enum class EntityKind {
    body,
    solid,
    shell,
    face,
    edge,
    vertex,
};

struct EntityReference {
    PersistentEntityId persistent_id;
    EntityKind expected_kind{EntityKind::face};

    bool operator==(const EntityReference&) const = default;
};

struct StudyIdentity {
    std::string study_id;
    std::string cad_document_id;
    document::RevisionId semantic_revision_id;
    document::RevisionId evaluated_revision_id;
    std::optional<std::string> candidate_id;
    std::string geometry_hash;
    std::string version{asir_version};
    std::string solver_build;
    std::string mesh_policy_hash;
};

struct CoordinateFrame {
    std::string id;
    std::array<SiValue, 3> origin;
    // Row-major orthonormal direction-cosine matrix. Its values are unitless.
    std::array<double, 9> basis{
        1.0, 0.0, 0.0,
        0.0, 1.0, 0.0,
        0.0, 0.0, 1.0,
    };
};

struct ExactBodyReference {
    std::string id;
    std::string component_id;
    std::optional<std::string> parent_component_id;
    std::string exact_body_hash;
    std::string frame_id;
};

enum class ParameterAuthority {
    cad,
    study_local,
    derived,
};

struct ParameterBounds {
    SiValue minimum;
    SiValue maximum;
};

struct ParameterDefinition {
    std::string id;
    std::string display_name;
    SiValue nominal;
    std::optional<ParameterBounds> bounds;
    bool discrete{};
    ParameterAuthority authority{ParameterAuthority::study_local};
};

enum class DispersionModel {
    nondispersive,
    debye,
    drude,
    lorentz,
};

struct MaterialDefinition {
    std::string id;
    std::string display_name;
    SiValue relative_permittivity;
    SiValue relative_permeability;
    SiValue conductivity;
    SiValue loss_tangent;
    DispersionModel dispersion{DispersionModel::nondispersive};
    std::optional<SiValue> thickness;
    std::optional<SiValue> roughness;
    std::optional<FrequencyBand> validity_band;
    std::string provenance;
};

enum class ElectromagneticRole {
    conductor,
    dielectric,
    ground,
    radome,
    feed_structure,
    air_or_vacuum,
    keep_out,
    symmetry_region,
    periodic_region,
};

struct RoleAssignment {
    std::string id;
    ElectromagneticRole role{ElectromagneticRole::conductor};
    EntityReference target;
    std::optional<std::string> material_id;
};

enum class PortKind {
    discrete,
    coaxial,
    waveguide,
};

enum class PortTermination {
    matched,
    open,
    short_circuit,
};

struct PortDefinition {
    std::string id;
    PortKind kind{PortKind::discrete};
    EntityReference face;
    EntityReference reference_plane;
    Direction3d orientation;
    std::uint32_t mode_index{};
    SiValue reference_impedance;
    SiValue deembedding_length;
    SiValue amplitude;
    SiValue phase;
    PortTermination termination{PortTermination::matched};
};

enum class PhasorConvention {
    exp_positive_jwt,
    exp_negative_jwt,
};

enum class BoundaryKind {
    open_pml,
    perfect_electric,
    perfect_magnetic,
    symmetry_electric,
    symmetry_magnetic,
    periodic,
};

struct BoundaryDefinition {
    std::string id;
    BoundaryKind kind{BoundaryKind::open_pml};
    std::string background_material_id;
    SiValue padding;
    std::optional<std::string> paired_boundary_id;
    std::optional<SiValue> scan_theta;
    std::optional<SiValue> scan_phi;
};

enum class SolverFamily {
    fdtd,
    fem,
    method_of_moments,
};

enum class PrecisionPolicy {
    fp64_reference,
    fp32,
    mixed,
};

struct MeshPolicy {
    SiValue minimum_cell_size;
    SiValue maximum_cell_size;
    SiValue cells_per_wavelength;
    bool adaptive_refinement{};
    bool allow_symmetry{};
};

struct SolverPolicy {
    std::vector<SolverFamily> allowed_families;
    FrequencyBand frequency_range;
    SiValue accuracy_target;
    MeshPolicy mesh;
    PrecisionPolicy precision{PrecisionPolicy::fp64_reference};
    PhasorConvention phasor_convention{PhasorConvention::exp_positive_jwt};
};

enum class FidelityProfile {
    preview,
    exploration,
    optimization,
    verification,
};

struct ResourceBudget {
    std::uint64_t maximum_cells{};
    std::uint64_t maximum_memory_bytes{};
    SiValue maximum_runtime;
    std::uint64_t maximum_storage_bytes{};
    bool require_escalation_on_exceedance{true};
};

struct FidelityAndCostPolicy {
    FidelityProfile profile{FidelityProfile::preview};
    ResourceBudget budget;
};

enum class MonitorKind {
    s_parameters,
    impedance,
    near_field,
    far_field,
    surface_current,
    efficiency,
    realized_gain,
    directivity,
    axial_ratio,
    polarization,
};

struct MonitorDefinition {
    std::string id;
    MonitorKind kind{MonitorKind::s_parameters};
    FrequencyBand frequency_range;
    std::vector<std::string> port_ids;
    std::optional<EntityReference> region;
};

struct GoalDefinition {
    std::string id;
    std::string expression;
    FrequencyBand frequency_range;
    SiValue weight;
    SiValue tolerance;
    SiValue pass_threshold;
};

struct ApprovalRecord {
    std::string id;
    std::string actor;
    std::string action;
    std::string timestamp_utc;
};

struct Provenance {
    std::vector<ApprovalRecord> approvals;
    std::vector<std::string> generated_operations;
    std::string solver_build_checksum;
    std::string host_fingerprint;
    std::string created_at_utc;
    std::vector<std::string> warnings;
    std::vector<std::string> raw_artifact_references;
};

struct AntennaStudy {
    StudyIdentity identity;
    std::vector<CoordinateFrame> coordinate_frames;
    std::vector<ExactBodyReference> geometry;
    std::vector<ParameterDefinition> parameters;
    std::vector<MaterialDefinition> materials;
    std::vector<RoleAssignment> roles;
    std::vector<PortDefinition> ports;
    std::vector<BoundaryDefinition> boundaries;
    SolverPolicy solver_policy;
    FidelityAndCostPolicy fidelity_and_cost;
    std::vector<MonitorDefinition> monitors;
    std::vector<GoalDefinition> goals;
    Provenance provenance;
};

struct ValidationContext {
    std::string cad_document_id;
    document::RevisionId semantic_revision_id;
    std::optional<document::RevisionId> evaluated_revision_id;
    std::optional<std::string> current_geometry_hash;
    std::vector<EntityReference> available_topology;
    std::vector<std::string> cad_parameter_ids;
};

enum class ValidationSeverity {
    error,
    warning,
};

enum class ValidationCode {
    unsupported_version,
    empty_identifier,
    duplicate_identifier,
    document_mismatch,
    revision_mismatch,
    current_geometry_unavailable,
    geometry_hash_mismatch,
    non_finite_value,
    unspecified_dimension,
    dimension_mismatch,
    invalid_range,
    invalid_coordinate_frame,
    unknown_reference,
    missing_material,
    unsupported_material_model,
    frequency_outside_material_validity,
    parameter_authority_mismatch,
    invalid_port,
    invalid_boundary,
    unsupported_solver,
    invalid_mesh_policy,
    invalid_resource_budget,
    invalid_monitor,
    invalid_goal,
    missing_provenance,
};

struct ValidationIssue {
    ValidationSeverity severity{ValidationSeverity::error};
    ValidationCode code{ValidationCode::empty_identifier};
    std::string path;
    std::string message;
};

struct ValidationReport {
    std::vector<ValidationIssue> issues;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool contains(ValidationCode code) const noexcept;
};

class Validator {
public:
    [[nodiscard]] ValidationReport validate(
        const AntennaStudy& study,
        const ValidationContext& context) const;
};

// Returns a deterministic content fingerprint over a canonical ordering of all
// ASIR 0.1 fields. The algorithm identifier is included in the returned value.
[[nodiscard]] std::string canonical_fingerprint(const AntennaStudy& study);

} // namespace oep::simulation_ir
