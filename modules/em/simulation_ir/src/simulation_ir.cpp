#include <oep/simulation_ir/simulation_ir.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <functional>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace oep::simulation_ir {
namespace {

constexpr double frame_tolerance = 1.0e-9;

void add_issue(
    ValidationReport& report,
    const ValidationCode code,
    std::string path,
    std::string message,
    const ValidationSeverity severity = ValidationSeverity::error)
{
    report.issues.push_back(ValidationIssue{
        .severity = severity,
        .code = code,
        .path = std::move(path),
        .message = std::move(message),
    });
}

void require_identifier(
    ValidationReport& report,
    const std::string_view value,
    const std::string_view path)
{
    if (value.empty()) {
        add_issue(
            report,
            ValidationCode::empty_identifier,
            std::string(path),
            "a stable identifier must not be empty");
    }
}

template <typename Range, typename Projection>
void require_unique_ids(
    ValidationReport& report,
    const Range& range,
    const std::string_view path,
    Projection projection)
{
    std::set<std::string, std::less<>> ids;
    for (const auto& item : range) {
        const auto& id = std::invoke(projection, item);
        require_identifier(report, id, std::string(path) + ".id");
        if (!id.empty() && !ids.insert(id).second) {
            add_issue(
                report,
                ValidationCode::duplicate_identifier,
                std::string(path) + '[' + id + ']',
                "identifiers must be unique within their ASIR collection");
        }
    }
}

[[nodiscard]] bool validate_si_value(
    ValidationReport& report,
    const SiValue& value,
    const SiDimension expected,
    const std::string_view path)
{
    auto valid = true;
    if (!std::isfinite(value.value)) {
        add_issue(
            report,
            ValidationCode::non_finite_value,
            std::string(path),
            "physical values must be finite");
        valid = false;
    }
    if (value.dimension == SiDimension::unspecified) {
        add_issue(
            report,
            ValidationCode::unspecified_dimension,
            std::string(path),
            "every physical value requires an explicit SI dimension");
        valid = false;
    } else if (value.dimension != expected) {
        add_issue(
            report,
            ValidationCode::dimension_mismatch,
            std::string(path),
            "the physical value has the wrong SI dimension");
        valid = false;
    }
    return valid;
}

[[nodiscard]] bool validate_band(
    ValidationReport& report,
    const FrequencyBand& band,
    const std::string_view path)
{
    const auto minimum_valid = validate_si_value(
        report, band.minimum, SiDimension::frequency, std::string(path) + ".minimum");
    const auto maximum_valid = validate_si_value(
        report, band.maximum, SiDimension::frequency, std::string(path) + ".maximum");
    if (minimum_valid && maximum_valid
        && (band.minimum.value <= 0.0 || band.maximum.value < band.minimum.value)) {
        add_issue(
            report,
            ValidationCode::invalid_range,
            std::string(path),
            "frequency bands require 0 < minimum <= maximum");
        return false;
    }
    return minimum_valid && maximum_valid;
}

[[nodiscard]] bool band_contains(
    const FrequencyBand& outer,
    const FrequencyBand& inner)
{
    return outer.minimum.value <= inner.minimum.value
        && outer.maximum.value >= inner.maximum.value;
}

[[nodiscard]] double row_dot(
    const std::array<double, 9>& basis,
    const std::size_t first,
    const std::size_t second)
{
    auto value = 0.0;
    for (std::size_t column = 0; column < 3; ++column) {
        value += basis[first * 3 + column] * basis[second * 3 + column];
    }
    return value;
}

[[nodiscard]] bool valid_basis(const std::array<double, 9>& basis)
{
    if (!std::ranges::all_of(basis, [](const double value) {
            return std::isfinite(value);
        })) {
        return false;
    }
    for (std::size_t row = 0; row < 3; ++row) {
        if (std::abs(row_dot(basis, row, row) - 1.0) > frame_tolerance) {
            return false;
        }
        for (auto other = row + 1; other < 3; ++other) {
            if (std::abs(row_dot(basis, row, other)) > frame_tolerance) {
                return false;
            }
        }
    }
    const auto determinant =
        basis[0] * (basis[4] * basis[8] - basis[5] * basis[7])
        - basis[1] * (basis[3] * basis[8] - basis[5] * basis[6])
        + basis[2] * (basis[3] * basis[7] - basis[4] * basis[6]);
    return std::abs(determinant - 1.0) <= frame_tolerance;
}

[[nodiscard]] std::string persistent_id_string(const PersistentEntityId& id)
{
    return id.feature_id + ":topology:" + std::to_string(id.local_id);
}

using AvailableTopology =
    std::map<PersistentEntityId, EntityKind, std::less<>>;

void validate_entity_reference(
    ValidationReport& report,
    const EntityReference& reference,
    const AvailableTopology& topology,
    const std::string_view path)
{
    if (reference.persistent_id.feature_id.empty()
        || reference.persistent_id.local_id == 0) {
        add_issue(
            report,
            ValidationCode::empty_identifier,
            std::string(path),
            "persistent topology references require a feature ID and nonzero local ID");
        return;
    }
    const auto found = topology.find(reference.persistent_id);
    if (found == topology.end()) {
        add_issue(
            report,
            ValidationCode::unknown_reference,
            std::string(path),
            "persistent topology reference "
                + persistent_id_string(reference.persistent_id)
                + " is absent from the evaluated CAD revision");
    } else if (found->second != reference.expected_kind) {
        add_issue(
            report,
            ValidationCode::unknown_reference,
            std::string(path),
            "persistent topology reference resolves to a different entity kind");
    }
}

[[nodiscard]] bool role_requires_material(const ElectromagneticRole role)
{
    return role == ElectromagneticRole::conductor
        || role == ElectromagneticRole::dielectric
        || role == ElectromagneticRole::ground
        || role == ElectromagneticRole::radome
        || role == ElectromagneticRole::air_or_vacuum;
}

[[nodiscard]] bool direction_is_valid(const Direction3d& direction)
{
    if (!std::isfinite(direction.x) || !std::isfinite(direction.y)
        || !std::isfinite(direction.z)) {
        return false;
    }
    return std::hypot(direction.x, std::hypot(direction.y, direction.z))
        > frame_tolerance;
}

class CanonicalWriter {
public:
    void string(const std::string_view value)
    {
        stream_ << value.size() << ':' << value << ';';
    }

    void integer(const std::uint64_t value) { stream_ << value << ';'; }
    void integer(const std::uint32_t value) { stream_ << value << ';'; }
    void integer(const int value) { stream_ << value << ';'; }
    void boolean(const bool value) { stream_ << (value ? '1' : '0') << ';'; }

    void real(const double value)
    {
        if (value == 0.0) {
            stream_ << "0;";
        } else {
            stream_ << std::setprecision(17) << value << ';';
        }
    }

    void si(const SiValue& value)
    {
        real(value.value);
        integer(static_cast<int>(value.dimension));
    }

    void band(const FrequencyBand& value)
    {
        si(value.minimum);
        si(value.maximum);
    }

    void entity(const EntityReference& value)
    {
        string(value.persistent_id.feature_id);
        integer(value.persistent_id.local_id);
        integer(static_cast<int>(value.expected_kind));
    }

    [[nodiscard]] std::string take() && { return std::move(stream_).str(); }

private:
    std::ostringstream stream_;
};

template <typename Value, typename Projection>
[[nodiscard]] std::vector<const Value*> sorted_by_id(
    const std::vector<Value>& values,
    Projection projection)
{
    std::vector<const Value*> sorted;
    sorted.reserve(values.size());
    for (const auto& value : values) {
        sorted.push_back(&value);
    }
    std::ranges::sort(sorted, [&](const Value* left, const Value* right) {
        return std::invoke(projection, *left) < std::invoke(projection, *right);
    });
    return sorted;
}

void write_string_vector(CanonicalWriter& writer, std::vector<std::string> values)
{
    std::ranges::sort(values);
    writer.integer(values.size());
    for (const auto& value : values) {
        writer.string(value);
    }
}

void write_study(CanonicalWriter& writer, const AntennaStudy& study)
{
    writer.string(study.identity.study_id);
    writer.string(study.identity.cad_document_id);
    writer.integer(study.identity.semantic_revision_id.value);
    writer.integer(study.identity.evaluated_revision_id.value);
    writer.boolean(study.identity.candidate_id.has_value());
    if (study.identity.candidate_id.has_value()) {
        writer.string(*study.identity.candidate_id);
    }
    writer.string(study.identity.geometry_hash);
    writer.string(study.identity.version);
    writer.string(study.identity.solver_build);
    writer.string(study.identity.mesh_policy_hash);

    const auto frames = sorted_by_id(
        study.coordinate_frames, [](const CoordinateFrame& item) { return item.id; });
    writer.integer(frames.size());
    for (const auto* frame : frames) {
        writer.string(frame->id);
        for (const auto& origin : frame->origin) {
            writer.si(origin);
        }
        for (const auto value : frame->basis) {
            writer.real(value);
        }
    }

    const auto geometry = sorted_by_id(
        study.geometry, [](const ExactBodyReference& item) { return item.id; });
    writer.integer(geometry.size());
    for (const auto* body : geometry) {
        writer.string(body->id);
        writer.string(body->component_id);
        writer.boolean(body->parent_component_id.has_value());
        if (body->parent_component_id.has_value()) {
            writer.string(*body->parent_component_id);
        }
        writer.string(body->exact_body_hash);
        writer.string(body->frame_id);
    }

    const auto parameters = sorted_by_id(
        study.parameters, [](const ParameterDefinition& item) { return item.id; });
    writer.integer(parameters.size());
    for (const auto* parameter : parameters) {
        writer.string(parameter->id);
        writer.string(parameter->display_name);
        writer.si(parameter->nominal);
        writer.boolean(parameter->bounds.has_value());
        if (parameter->bounds.has_value()) {
            writer.si(parameter->bounds->minimum);
            writer.si(parameter->bounds->maximum);
        }
        writer.boolean(parameter->discrete);
        writer.integer(static_cast<int>(parameter->authority));
    }

    const auto materials = sorted_by_id(
        study.materials, [](const MaterialDefinition& item) { return item.id; });
    writer.integer(materials.size());
    for (const auto* material : materials) {
        writer.string(material->id);
        writer.string(material->display_name);
        writer.si(material->relative_permittivity);
        writer.si(material->relative_permeability);
        writer.si(material->conductivity);
        writer.si(material->loss_tangent);
        writer.integer(static_cast<int>(material->dispersion));
        writer.boolean(material->thickness.has_value());
        if (material->thickness.has_value()) {
            writer.si(*material->thickness);
        }
        writer.boolean(material->roughness.has_value());
        if (material->roughness.has_value()) {
            writer.si(*material->roughness);
        }
        writer.boolean(material->validity_band.has_value());
        if (material->validity_band.has_value()) {
            writer.band(*material->validity_band);
        }
        writer.string(material->provenance);
    }

    const auto roles = sorted_by_id(
        study.roles, [](const RoleAssignment& item) { return item.id; });
    writer.integer(roles.size());
    for (const auto* role : roles) {
        writer.string(role->id);
        writer.integer(static_cast<int>(role->role));
        writer.entity(role->target);
        writer.boolean(role->material_id.has_value());
        if (role->material_id.has_value()) {
            writer.string(*role->material_id);
        }
    }

    const auto ports = sorted_by_id(
        study.ports, [](const PortDefinition& item) { return item.id; });
    writer.integer(ports.size());
    for (const auto* port : ports) {
        writer.string(port->id);
        writer.integer(static_cast<int>(port->kind));
        writer.entity(port->face);
        writer.entity(port->reference_plane);
        writer.real(port->orientation.x);
        writer.real(port->orientation.y);
        writer.real(port->orientation.z);
        writer.integer(port->mode_index);
        writer.si(port->reference_impedance);
        writer.si(port->deembedding_length);
        writer.si(port->amplitude);
        writer.si(port->phase);
        writer.integer(static_cast<int>(port->termination));
    }

    const auto boundaries = sorted_by_id(
        study.boundaries, [](const BoundaryDefinition& item) { return item.id; });
    writer.integer(boundaries.size());
    for (const auto* boundary : boundaries) {
        writer.string(boundary->id);
        writer.integer(static_cast<int>(boundary->kind));
        writer.string(boundary->background_material_id);
        writer.si(boundary->padding);
        writer.boolean(boundary->paired_boundary_id.has_value());
        if (boundary->paired_boundary_id.has_value()) {
            writer.string(*boundary->paired_boundary_id);
        }
        writer.boolean(boundary->scan_theta.has_value());
        if (boundary->scan_theta.has_value()) {
            writer.si(*boundary->scan_theta);
        }
        writer.boolean(boundary->scan_phi.has_value());
        if (boundary->scan_phi.has_value()) {
            writer.si(*boundary->scan_phi);
        }
    }

    auto families = study.solver_policy.allowed_families;
    std::ranges::sort(families);
    writer.integer(families.size());
    for (const auto family : families) {
        writer.integer(static_cast<int>(family));
    }
    writer.band(study.solver_policy.frequency_range);
    writer.si(study.solver_policy.accuracy_target);
    writer.si(study.solver_policy.mesh.minimum_cell_size);
    writer.si(study.solver_policy.mesh.maximum_cell_size);
    writer.si(study.solver_policy.mesh.cells_per_wavelength);
    writer.boolean(study.solver_policy.mesh.adaptive_refinement);
    writer.boolean(study.solver_policy.mesh.allow_symmetry);
    writer.integer(static_cast<int>(study.solver_policy.precision));
    writer.integer(static_cast<int>(study.solver_policy.phasor_convention));

    writer.integer(static_cast<int>(study.fidelity_and_cost.profile));
    writer.integer(study.fidelity_and_cost.budget.maximum_cells);
    writer.integer(study.fidelity_and_cost.budget.maximum_memory_bytes);
    writer.si(study.fidelity_and_cost.budget.maximum_runtime);
    writer.integer(study.fidelity_and_cost.budget.maximum_storage_bytes);
    writer.boolean(study.fidelity_and_cost.budget.require_escalation_on_exceedance);

    const auto monitors = sorted_by_id(
        study.monitors, [](const MonitorDefinition& item) { return item.id; });
    writer.integer(monitors.size());
    for (const auto* monitor : monitors) {
        writer.string(monitor->id);
        writer.integer(static_cast<int>(monitor->kind));
        writer.band(monitor->frequency_range);
        write_string_vector(writer, monitor->port_ids);
        writer.boolean(monitor->region.has_value());
        if (monitor->region.has_value()) {
            writer.entity(*monitor->region);
        }
    }

    const auto goals = sorted_by_id(
        study.goals, [](const GoalDefinition& item) { return item.id; });
    writer.integer(goals.size());
    for (const auto* goal : goals) {
        writer.string(goal->id);
        writer.string(goal->expression);
        writer.band(goal->frequency_range);
        writer.si(goal->weight);
        writer.si(goal->tolerance);
        writer.si(goal->pass_threshold);
    }

    const auto approvals = sorted_by_id(
        study.provenance.approvals, [](const ApprovalRecord& item) { return item.id; });
    writer.integer(approvals.size());
    for (const auto* approval : approvals) {
        writer.string(approval->id);
        writer.string(approval->actor);
        writer.string(approval->action);
        writer.string(approval->timestamp_utc);
    }
    write_string_vector(writer, study.provenance.generated_operations);
    writer.string(study.provenance.solver_build_checksum);
    writer.string(study.provenance.host_fingerprint);
    writer.string(study.provenance.created_at_utc);
    write_string_vector(writer, study.provenance.warnings);
    write_string_vector(writer, study.provenance.raw_artifact_references);
}

[[nodiscard]] std::string sha256(const std::string_view input)
{
    constexpr std::array<std::uint32_t, 64> constants{
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
        0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
        0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
        0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
        0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
        0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
        0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
    };
    std::array<std::uint32_t, 8> state{
        0x6a09e667U,
        0xbb67ae85U,
        0x3c6ef372U,
        0xa54ff53aU,
        0x510e527fU,
        0x9b05688cU,
        0x1f83d9abU,
        0x5be0cd19U,
    };

    std::vector<std::uint8_t> message;
    message.reserve(input.size() + 72);
    for (const auto character : input) {
        message.push_back(static_cast<std::uint8_t>(
            static_cast<unsigned char>(character)));
    }
    const auto bit_length = static_cast<std::uint64_t>(input.size()) * 8U;
    message.push_back(0x80U);
    while (message.size() % 64 != 56) {
        message.push_back(0U);
    }
    for (auto shift = 56; shift >= 0; shift -= 8) {
        message.push_back(static_cast<std::uint8_t>(bit_length >> shift));
    }

    for (std::size_t offset = 0; offset < message.size(); offset += 64) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16; ++index) {
            const auto base = offset + index * 4;
            words[index] = (static_cast<std::uint32_t>(message[base]) << 24U)
                | (static_cast<std::uint32_t>(message[base + 1]) << 16U)
                | (static_cast<std::uint32_t>(message[base + 2]) << 8U)
                | static_cast<std::uint32_t>(message[base + 3]);
        }
        for (std::size_t index = 16; index < words.size(); ++index) {
            const auto sigma0 = std::rotr(words[index - 15], 7)
                ^ std::rotr(words[index - 15], 18)
                ^ (words[index - 15] >> 3U);
            const auto sigma1 = std::rotr(words[index - 2], 17)
                ^ std::rotr(words[index - 2], 19)
                ^ (words[index - 2] >> 10U);
            words[index] = words[index - 16] + sigma0 + words[index - 7] + sigma1;
        }

        auto a = state[0];
        auto b = state[1];
        auto c = state[2];
        auto d = state[3];
        auto e = state[4];
        auto f = state[5];
        auto g = state[6];
        auto h = state[7];
        for (std::size_t index = 0; index < words.size(); ++index) {
            const auto sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            const auto choice = (e & f) ^ (~e & g);
            const auto temporary1 = h + sum1 + choice + constants[index] + words[index];
            const auto sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temporary2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temporary1;
            d = c;
            c = b;
            b = a;
            a = temporary1 + temporary2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    std::ostringstream encoded;
    encoded << std::hex << std::setfill('0');
    for (const auto word : state) {
        encoded << std::setw(8) << word;
    }
    return encoded.str();
}

} // namespace

bool ValidationReport::valid() const noexcept
{
    return std::ranges::none_of(issues, [](const ValidationIssue& issue) {
        return issue.severity == ValidationSeverity::error;
    });
}

bool ValidationReport::contains(const ValidationCode code) const noexcept
{
    return std::ranges::any_of(
        issues, [code](const ValidationIssue& issue) { return issue.code == code; });
}

ValidationReport Validator::validate(
    const AntennaStudy& study,
    const ValidationContext& context) const
{
    ValidationReport report;

    require_identifier(report, study.identity.study_id, "identity.study_id");
    require_identifier(report, study.identity.cad_document_id, "identity.cad_document_id");
    require_identifier(report, study.identity.geometry_hash, "identity.geometry_hash");
    require_identifier(report, study.identity.solver_build, "identity.solver_build");
    require_identifier(report, study.identity.mesh_policy_hash, "identity.mesh_policy_hash");
    if (study.identity.version != asir_version) {
        add_issue(
            report,
            ValidationCode::unsupported_version,
            "identity.version",
            "this validator accepts ASIR version 0.1 only");
    }
    if (study.identity.cad_document_id != context.cad_document_id) {
        add_issue(
            report,
            ValidationCode::document_mismatch,
            "identity.cad_document_id",
            "the study belongs to a different CAD document");
    }
    if (study.identity.semantic_revision_id.value == 0
        || study.identity.evaluated_revision_id.value == 0
        || study.identity.semantic_revision_id != study.identity.evaluated_revision_id
        || study.identity.semantic_revision_id != context.semantic_revision_id
        || !context.evaluated_revision_id.has_value()
        || study.identity.evaluated_revision_id != context.evaluated_revision_id) {
        add_issue(
            report,
            ValidationCode::revision_mismatch,
            "identity.revisions",
            "ASIR requires one matching, successfully evaluated CAD semantic revision");
    }
    if (!context.current_geometry_hash.has_value()) {
        add_issue(
            report,
            ValidationCode::current_geometry_unavailable,
            "identity.geometry_hash",
            "the CAD semantic head has no matching current geometry");
    } else if (study.identity.geometry_hash != *context.current_geometry_hash) {
        add_issue(
            report,
            ValidationCode::geometry_hash_mismatch,
            "identity.geometry_hash",
            "the ASIR geometry hash does not match the evaluated CAD body");
    }

    AvailableTopology topology;
    for (const auto& available : context.available_topology) {
        const auto [found, inserted] = topology.emplace(
            available.persistent_id, available.expected_kind);
        if (!inserted && found->second != available.expected_kind) {
            add_issue(
                report,
                ValidationCode::duplicate_identifier,
                "context.available_topology",
                "one persistent ID is advertised with multiple entity kinds");
        }
    }
    const std::set<std::string, std::less<>> cad_parameter_ids(
        context.cad_parameter_ids.begin(), context.cad_parameter_ids.end());

    require_unique_ids(
        report, study.coordinate_frames, "coordinate_frames", &CoordinateFrame::id);
    std::set<std::string, std::less<>> frame_ids;
    for (const auto& frame : study.coordinate_frames) {
        frame_ids.insert(frame.id);
        for (std::size_t index = 0; index < frame.origin.size(); ++index) {
            static_cast<void>(validate_si_value(
                report,
                frame.origin[index],
                SiDimension::length,
                "coordinate_frames[" + frame.id + "].origin["
                    + std::to_string(index) + ']'));
        }
        if (!valid_basis(frame.basis)) {
            add_issue(
                report,
                ValidationCode::invalid_coordinate_frame,
                "coordinate_frames[" + frame.id + "].basis",
                "coordinate-frame basis must be finite, right-handed, and orthonormal");
        }
    }
    if (study.coordinate_frames.empty()) {
        add_issue(
            report,
            ValidationCode::unknown_reference,
            "coordinate_frames",
            "ASIR geometry requires at least one coordinate frame");
    }

    require_unique_ids(report, study.geometry, "geometry", &ExactBodyReference::id);
    std::set<std::string, std::less<>> component_ids;
    for (const auto& body : study.geometry) {
        require_identifier(
            report, body.component_id, "geometry[" + body.id + "].component_id");
        require_identifier(
            report, body.exact_body_hash, "geometry[" + body.id + "].exact_body_hash");
        if (!body.component_id.empty() && !component_ids.insert(body.component_id).second) {
            add_issue(
                report,
                ValidationCode::duplicate_identifier,
                "geometry[" + body.id + "].component_id",
                "component IDs must be unique");
        }
        if (!frame_ids.contains(body.frame_id)) {
            add_issue(
                report,
                ValidationCode::unknown_reference,
                "geometry[" + body.id + "].frame_id",
                "exact body references must identify a declared coordinate frame");
        }
    }
    for (const auto& body : study.geometry) {
        if (body.parent_component_id.has_value()
            && !component_ids.contains(*body.parent_component_id)) {
            add_issue(
                report,
                ValidationCode::unknown_reference,
                "geometry[" + body.id + "].parent_component_id",
                "parent component is not declared by this study");
        }
    }
    if (study.geometry.empty()) {
        add_issue(
            report,
            ValidationCode::unknown_reference,
            "geometry",
            "an antenna study requires at least one exact CAD body reference");
    }

    require_unique_ids(report, study.parameters, "parameters", &ParameterDefinition::id);
    for (const auto& parameter : study.parameters) {
        const auto path = "parameters[" + parameter.id + ']';
        if (parameter.nominal.dimension == SiDimension::unspecified) {
            add_issue(
                report,
                ValidationCode::unspecified_dimension,
                path + ".nominal",
                "parameter values require an explicit SI dimension");
        }
        if (!std::isfinite(parameter.nominal.value)) {
            add_issue(
                report,
                ValidationCode::non_finite_value,
                path + ".nominal",
                "parameter values must be finite");
        }
        if (parameter.authority == ParameterAuthority::cad
            && !cad_parameter_ids.contains(parameter.id)) {
            add_issue(
                report,
                ValidationCode::parameter_authority_mismatch,
                path + ".authority",
                "CAD-authoritative parameter is absent from the evaluated CAD document");
        }
        if (parameter.authority == ParameterAuthority::derived
            && parameter.bounds.has_value()) {
            add_issue(
                report,
                ValidationCode::parameter_authority_mismatch,
                path + ".bounds",
                "derived parameters are read-only and cannot declare optimization bounds");
        }
        if (parameter.bounds.has_value()) {
            const auto minimum_valid = validate_si_value(
                report,
                parameter.bounds->minimum,
                parameter.nominal.dimension,
                path + ".bounds.minimum");
            const auto maximum_valid = validate_si_value(
                report,
                parameter.bounds->maximum,
                parameter.nominal.dimension,
                path + ".bounds.maximum");
            if (minimum_valid && maximum_valid
                && (parameter.bounds->minimum.value > parameter.bounds->maximum.value
                    || parameter.nominal.value < parameter.bounds->minimum.value
                    || parameter.nominal.value > parameter.bounds->maximum.value)) {
                add_issue(
                    report,
                    ValidationCode::invalid_range,
                    path + ".bounds",
                    "parameter bounds must be ordered and contain the nominal value");
            }
        }
    }

    require_unique_ids(report, study.materials, "materials", &MaterialDefinition::id);
    std::set<std::string, std::less<>> material_ids;
    for (const auto& material : study.materials) {
        material_ids.insert(material.id);
        const auto path = "materials[" + material.id + ']';
        const auto permittivity_valid = validate_si_value(
            report,
            material.relative_permittivity,
            SiDimension::relative_permittivity,
            path + ".relative_permittivity");
        const auto permeability_valid = validate_si_value(
            report,
            material.relative_permeability,
            SiDimension::relative_permeability,
            path + ".relative_permeability");
        const auto conductivity_valid = validate_si_value(
            report,
            material.conductivity,
            SiDimension::conductivity,
            path + ".conductivity");
        const auto loss_valid = validate_si_value(
            report,
            material.loss_tangent,
            SiDimension::loss_tangent,
            path + ".loss_tangent");
        if ((permittivity_valid && material.relative_permittivity.value <= 0.0)
            || (permeability_valid && material.relative_permeability.value <= 0.0)
            || (conductivity_valid && material.conductivity.value < 0.0)
            || (loss_valid && material.loss_tangent.value < 0.0)) {
            add_issue(
                report,
                ValidationCode::invalid_range,
                path,
                "material properties require positive relative constants and nonnegative loss values");
        }
        if (material.dispersion != DispersionModel::nondispersive) {
            add_issue(
                report,
                ValidationCode::unsupported_material_model,
                path + ".dispersion",
                "ASIR 0.1 does not yet define coefficients for dispersive material models");
        }
        if (material.thickness.has_value()) {
            if (validate_si_value(
                    report,
                    *material.thickness,
                    SiDimension::length,
                    path + ".thickness")
                && material.thickness->value < 0.0) {
                add_issue(
                    report,
                    ValidationCode::invalid_range,
                    path + ".thickness",
                    "material thickness cannot be negative");
            }
        }
        if (material.roughness.has_value()) {
            if (validate_si_value(
                    report,
                    *material.roughness,
                    SiDimension::length,
                    path + ".roughness")
                && material.roughness->value < 0.0) {
                add_issue(
                    report,
                    ValidationCode::invalid_range,
                    path + ".roughness",
                    "material roughness cannot be negative");
            }
        }
        if (material.validity_band.has_value()) {
            static_cast<void>(validate_band(
                report, *material.validity_band, path + ".validity_band"));
        }
        require_identifier(report, material.provenance, path + ".provenance");
    }

    require_unique_ids(report, study.roles, "roles", &RoleAssignment::id);
    for (const auto& role : study.roles) {
        const auto path = "roles[" + role.id + ']';
        validate_entity_reference(report, role.target, topology, path + ".target");
        if (role_requires_material(role.role) && !role.material_id.has_value()) {
            add_issue(
                report,
                ValidationCode::missing_material,
                path + ".material_id",
                "this electromagnetic role requires an explicit material");
        } else if (role.material_id.has_value()
            && !material_ids.contains(*role.material_id)) {
            add_issue(
                report,
                ValidationCode::missing_material,
                path + ".material_id",
                "role refers to an unknown material");
        }
    }
    if (study.roles.empty()) {
        add_issue(
            report,
            ValidationCode::unknown_reference,
            "roles",
            "an antenna study requires at least one persistent electromagnetic role");
    }

    require_unique_ids(report, study.ports, "ports", &PortDefinition::id);
    std::set<std::string, std::less<>> port_ids;
    for (const auto& port : study.ports) {
        port_ids.insert(port.id);
        const auto path = "ports[" + port.id + ']';
        validate_entity_reference(report, port.face, topology, path + ".face");
        validate_entity_reference(
            report, port.reference_plane, topology, path + ".reference_plane");
        if (port.face.expected_kind != EntityKind::face
            || port.reference_plane.expected_kind != EntityKind::face
            || !direction_is_valid(port.orientation)) {
            add_issue(
                report,
                ValidationCode::invalid_port,
                path,
                "ports require face references and a finite nonzero orientation");
        }
        const auto impedance_valid = validate_si_value(
            report,
            port.reference_impedance,
            SiDimension::impedance,
            path + ".reference_impedance");
        const auto deembedding_valid = validate_si_value(
            report,
            port.deembedding_length,
            SiDimension::length,
            path + ".deembedding_length");
        static_cast<void>(validate_si_value(
            report, port.amplitude, SiDimension::electric_potential, path + ".amplitude"));
        static_cast<void>(validate_si_value(
            report, port.phase, SiDimension::angle, path + ".phase"));
        if ((impedance_valid && port.reference_impedance.value <= 0.0)
            || (deembedding_valid && port.deembedding_length.value < 0.0)) {
            add_issue(
                report,
                ValidationCode::invalid_port,
                path,
                "port impedance must be positive and de-embedding length nonnegative");
        }
    }
    if (study.ports.empty()) {
        add_issue(
            report,
            ValidationCode::invalid_port,
            "ports",
            "ASIR 0.1 antenna studies require at least one explicit port");
    }

    require_unique_ids(report, study.boundaries, "boundaries", &BoundaryDefinition::id);
    std::set<std::string, std::less<>> boundary_ids;
    auto has_open_boundary = false;
    for (const auto& boundary : study.boundaries) {
        boundary_ids.insert(boundary.id);
        const auto path = "boundaries[" + boundary.id + ']';
        has_open_boundary = has_open_boundary || boundary.kind == BoundaryKind::open_pml;
        if (!material_ids.contains(boundary.background_material_id)) {
            add_issue(
                report,
                ValidationCode::missing_material,
                path + ".background_material_id",
                "boundary background material is not declared");
        }
        const auto padding_valid = validate_si_value(
            report, boundary.padding, SiDimension::length, path + ".padding");
        if (padding_valid && boundary.padding.value < 0.0) {
            add_issue(
                report,
                ValidationCode::invalid_boundary,
                path + ".padding",
                "boundary padding cannot be negative");
        }
        if (boundary.scan_theta.has_value()) {
            static_cast<void>(validate_si_value(
                report, *boundary.scan_theta, SiDimension::angle, path + ".scan_theta"));
        }
        if (boundary.scan_phi.has_value()) {
            static_cast<void>(validate_si_value(
                report, *boundary.scan_phi, SiDimension::angle, path + ".scan_phi"));
        }
        if (boundary.kind == BoundaryKind::periodic
            && !boundary.paired_boundary_id.has_value()) {
            add_issue(
                report,
                ValidationCode::invalid_boundary,
                path + ".paired_boundary_id",
                "periodic boundaries require an explicit paired boundary");
        }
    }
    for (const auto& boundary : study.boundaries) {
        if (boundary.paired_boundary_id.has_value()
            && !boundary_ids.contains(*boundary.paired_boundary_id)) {
            add_issue(
                report,
                ValidationCode::invalid_boundary,
                "boundaries[" + boundary.id + "].paired_boundary_id",
                "paired boundary is not declared by this study");
        }
    }
    if (!has_open_boundary) {
        add_issue(
            report,
            ValidationCode::invalid_boundary,
            "boundaries",
            "an open antenna study requires at least one explicit PML boundary policy");
    }

    const auto solver_band_valid = validate_band(
        report, study.solver_policy.frequency_range, "solver_policy.frequency_range");
    if (study.solver_policy.allowed_families.empty()
        || std::ranges::any_of(
            study.solver_policy.allowed_families,
            [](const SolverFamily family) { return family != SolverFamily::fdtd; })) {
        add_issue(
            report,
            ValidationCode::unsupported_solver,
            "solver_policy.allowed_families",
            "ASIR 0.1 execution currently supports the FDTD family only");
    }
    const auto accuracy_valid = validate_si_value(
        report,
        study.solver_policy.accuracy_target,
        SiDimension::dimensionless,
        "solver_policy.accuracy_target");
    if (accuracy_valid && (study.solver_policy.accuracy_target.value <= 0.0
                          || study.solver_policy.accuracy_target.value > 1.0)) {
        add_issue(
            report,
            ValidationCode::invalid_range,
            "solver_policy.accuracy_target",
            "accuracy target must lie in (0, 1]");
    }
    const auto minimum_cell_valid = validate_si_value(
        report,
        study.solver_policy.mesh.minimum_cell_size,
        SiDimension::length,
        "solver_policy.mesh.minimum_cell_size");
    const auto maximum_cell_valid = validate_si_value(
        report,
        study.solver_policy.mesh.maximum_cell_size,
        SiDimension::length,
        "solver_policy.mesh.maximum_cell_size");
    const auto cells_per_wavelength_valid = validate_si_value(
        report,
        study.solver_policy.mesh.cells_per_wavelength,
        SiDimension::dimensionless,
        "solver_policy.mesh.cells_per_wavelength");
    if ((minimum_cell_valid && study.solver_policy.mesh.minimum_cell_size.value <= 0.0)
        || (maximum_cell_valid && study.solver_policy.mesh.maximum_cell_size.value <= 0.0)
        || (minimum_cell_valid && maximum_cell_valid
            && study.solver_policy.mesh.minimum_cell_size.value
                > study.solver_policy.mesh.maximum_cell_size.value)
        || (cells_per_wavelength_valid
            && study.solver_policy.mesh.cells_per_wavelength.value < 4.0)) {
        add_issue(
            report,
            ValidationCode::invalid_mesh_policy,
            "solver_policy.mesh",
            "mesh policy requires ordered positive cell sizes and at least four cells per wavelength");
    } else if (cells_per_wavelength_valid
        && study.solver_policy.mesh.cells_per_wavelength.value < 10.0) {
        add_issue(
            report,
            ValidationCode::invalid_mesh_policy,
            "solver_policy.mesh.cells_per_wavelength",
            "fewer than ten cells per wavelength requires convergence evidence",
            ValidationSeverity::warning);
    }

    if (solver_band_valid) {
        for (const auto& material : study.materials) {
            if (material.validity_band.has_value()
                && !band_contains(
                    *material.validity_band, study.solver_policy.frequency_range)) {
                add_issue(
                    report,
                    ValidationCode::frequency_outside_material_validity,
                    "materials[" + material.id + "].validity_band",
                    "solver frequency range exceeds the material model validity band");
            }
        }
    }

    const auto runtime_valid = validate_si_value(
        report,
        study.fidelity_and_cost.budget.maximum_runtime,
        SiDimension::time,
        "fidelity_and_cost.budget.maximum_runtime");
    if (study.fidelity_and_cost.budget.maximum_cells == 0
        || study.fidelity_and_cost.budget.maximum_memory_bytes == 0
        || study.fidelity_and_cost.budget.maximum_storage_bytes == 0
        || (runtime_valid
            && study.fidelity_and_cost.budget.maximum_runtime.value <= 0.0)) {
        add_issue(
            report,
            ValidationCode::invalid_resource_budget,
            "fidelity_and_cost.budget",
            "resource budgets require positive cell, memory, runtime, and storage limits");
    }

    require_unique_ids(report, study.monitors, "monitors", &MonitorDefinition::id);
    for (const auto& monitor : study.monitors) {
        const auto path = "monitors[" + monitor.id + ']';
        if (validate_band(report, monitor.frequency_range, path + ".frequency_range")
            && solver_band_valid
            && !band_contains(study.solver_policy.frequency_range, monitor.frequency_range)) {
            add_issue(
                report,
                ValidationCode::invalid_monitor,
                path + ".frequency_range",
                "monitor frequency range must be inside the solver range");
        }
        for (const auto& port_id : monitor.port_ids) {
            if (!port_ids.contains(port_id)) {
                add_issue(
                    report,
                    ValidationCode::invalid_monitor,
                    path + ".port_ids",
                    "monitor refers to an unknown port");
            }
        }
        if ((monitor.kind == MonitorKind::s_parameters
             || monitor.kind == MonitorKind::impedance)
            && monitor.port_ids.empty()) {
            add_issue(
                report,
                ValidationCode::invalid_monitor,
                path + ".port_ids",
                "network and impedance monitors require explicit port IDs");
        }
        if (monitor.region.has_value()) {
            validate_entity_reference(report, *monitor.region, topology, path + ".region");
        }
    }
    if (study.monitors.empty()) {
        add_issue(
            report,
            ValidationCode::invalid_monitor,
            "monitors",
            "a study requires at least one requested observable");
    }

    require_unique_ids(report, study.goals, "goals", &GoalDefinition::id);
    for (const auto& goal : study.goals) {
        const auto path = "goals[" + goal.id + ']';
        require_identifier(report, goal.expression, path + ".expression");
        if (validate_band(report, goal.frequency_range, path + ".frequency_range")
            && solver_band_valid
            && !band_contains(study.solver_policy.frequency_range, goal.frequency_range)) {
            add_issue(
                report,
                ValidationCode::invalid_goal,
                path + ".frequency_range",
                "goal frequency range must be inside the solver range");
        }
        const auto weight_valid = validate_si_value(
            report, goal.weight, SiDimension::dimensionless, path + ".weight");
        static_cast<void>(validate_si_value(
            report, goal.tolerance, SiDimension::dimensionless, path + ".tolerance"));
        static_cast<void>(validate_si_value(
            report,
            goal.pass_threshold,
            SiDimension::dimensionless,
            path + ".pass_threshold"));
        if (weight_valid && goal.weight.value < 0.0) {
            add_issue(
                report,
                ValidationCode::invalid_goal,
                path + ".weight",
                "goal weights cannot be negative");
        }
    }

    require_unique_ids(
        report, study.provenance.approvals, "provenance.approvals", &ApprovalRecord::id);
    if (study.provenance.approvals.empty()
        || study.provenance.solver_build_checksum.empty()
        || study.provenance.host_fingerprint.empty()
        || study.provenance.created_at_utc.empty()) {
        add_issue(
            report,
            ValidationCode::missing_provenance,
            "provenance",
            "ASIR requires approval, solver checksum, host, and creation-time provenance");
    }
    for (const auto& approval : study.provenance.approvals) {
        if (approval.actor.empty() || approval.action.empty()
            || approval.timestamp_utc.empty()) {
            add_issue(
                report,
                ValidationCode::missing_provenance,
                "provenance.approvals[" + approval.id + ']',
                "approval records require actor, action, and timestamp");
        }
    }

    return report;
}

std::string canonical_fingerprint(const AntennaStudy& study)
{
    CanonicalWriter writer;
    write_study(writer, study);
    const auto canonical = std::move(writer).take();
    return "asir-0.1-sha256:" + sha256(canonical);
}

} // namespace oep::simulation_ir
