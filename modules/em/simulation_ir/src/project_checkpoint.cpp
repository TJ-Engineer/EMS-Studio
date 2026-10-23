// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#include <oep/simulation_ir/project_checkpoint.hpp>
#include <oep/ipc/frame.hpp>
#include <bit>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <tuple>
#include <type_traits>

namespace oep::simulation_ir {
namespace {
constexpr std::uint64_t maximum_bytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t maximum_entries = 100'000;
constexpr std::uint64_t maximum_text = 1024ULL * 1024ULL;
constexpr std::uint32_t project_type = 0x50524a01U;
using persistence::CheckpointFormatError;

// Explicit field order is the project schema, independent of C++ layout.
// Additions/reordering require a version change and round-trip/corruption tests.
template<class T> requires std::is_same_v<std::remove_const_t<T>, SiValue>
auto fields(T& value) { return std::tie(value.value, value.dimension); }
template<class T> requires std::is_same_v<std::remove_const_t<T>, FrequencyBand>
auto fields(T& value) { return std::tie(value.minimum, value.maximum); }
template<class T> requires std::is_same_v<std::remove_const_t<T>, Direction3d>
auto fields(T& value) { return std::tie(value.x, value.y, value.z); }
template<class T> requires std::is_same_v<std::remove_const_t<T>, PersistentEntityId>
auto fields(T& value) { return std::tie(value.feature_id, value.local_id); }
template<class T> requires std::is_same_v<std::remove_const_t<T>, EntityReference>
auto fields(T& value) { return std::tie(value.persistent_id, value.expected_kind); }
template<class T> requires std::is_same_v<std::remove_const_t<T>, StudyIdentity>
auto fields(T& value) { return std::tie(value.study_id, value.cad_document_id, value.semantic_revision_id, value.evaluated_revision_id, value.candidate_id, value.geometry_hash, value.version, value.solver_build, value.mesh_policy_hash); }
template<class T> requires std::is_same_v<std::remove_const_t<T>, CoordinateFrame>
auto fields(T& value) { return std::tie(value.id, value.origin, value.basis); }
template<class T> requires std::is_same_v<std::remove_const_t<T>, ExactBodyReference>
auto fields(T& value) { return std::tie(value.id, value.component_id, value.parent_component_id, value.exact_body_hash, value.frame_id); }
template<class T> requires std::is_same_v<std::remove_const_t<T>, ParameterBounds>
auto fields(T& value) { return std::tie(value.minimum, value.maximum); }
template<class T> requires std::is_same_v<std::remove_const_t<T>, ParameterDefinition>
auto fields(T& value) { return std::tie(value.id, value.display_name, value.nominal, value.bounds, value.discrete, value.authority); }
template<class T> requires std::is_same_v<std::remove_const_t<T>, MaterialDefinition>
auto fields(T& value) { return std::tie(value.id, value.display_name, value.relative_permittivity, value.relative_permeability, value.conductivity, value.loss_tangent, value.dispersion, value.thickness, value.roughness, value.validity_band, value.provenance); }
template<class T> requires std::is_same_v<std::remove_const_t<T>, RoleAssignment>
auto fields(T& value) { return std::tie(value.id, value.role, value.target, value.material_id); }
template<class T> requires std::is_same_v<std::remove_const_t<T>, PortDefinition>
auto fields(T& value) { return std::tie(value.id, value.kind, value.face, value.reference_plane, value.orientation, value.mode_index, value.reference_impedance, value.deembedding_length, value.amplitude, value.phase, value.termination); }
template<class T> requires std::is_same_v<std::remove_const_t<T>, BoundaryDefinition>
auto fields(T& value) { return std::tie(value.id, value.kind, value.background_material_id, value.padding, value.paired_boundary_id, value.scan_theta, value.scan_phi); }
template<class T> requires std::is_same_v<std::remove_const_t<T>, MeshPolicy>
auto fields(T& value) { return std::tie(value.minimum_cell_size, value.maximum_cell_size, value.cells_per_wavelength, value.adaptive_refinement, value.allow_symmetry); }
template<class T> requires std::is_same_v<std::remove_const_t<T>, SolverPolicy>
auto fields(T& value) { return std::tie(value.allowed_families, value.frequency_range, value.accuracy_target, value.mesh, value.precision, value.phasor_convention); }
template<class T> requires std::is_same_v<std::remove_const_t<T>, ResourceBudget>
auto fields(T& value) { return std::tie(value.maximum_cells, value.maximum_memory_bytes, value.maximum_runtime, value.maximum_storage_bytes, value.require_escalation_on_exceedance); }
template<class T> requires std::is_same_v<std::remove_const_t<T>, FidelityAndCostPolicy>
auto fields(T& value) { return std::tie(value.profile, value.budget); }
template<class T> requires std::is_same_v<std::remove_const_t<T>, MonitorDefinition>
auto fields(T& value) { return std::tie(value.id, value.kind, value.frequency_range, value.port_ids, value.region); }
template<class T> requires std::is_same_v<std::remove_const_t<T>, GoalDefinition>
auto fields(T& value) { return std::tie(value.id, value.expression, value.frequency_range, value.weight, value.tolerance, value.pass_threshold); }
template<class T> requires std::is_same_v<std::remove_const_t<T>, ApprovalRecord>
auto fields(T& value) { return std::tie(value.id, value.actor, value.action, value.timestamp_utc); }
template<class T> requires std::is_same_v<std::remove_const_t<T>, Provenance>
auto fields(T& value) { return std::tie(value.approvals, value.generated_operations, value.solver_build_checksum, value.host_fingerprint, value.created_at_utc, value.warnings, value.raw_artifact_references); }
template<class T> requires std::is_same_v<std::remove_const_t<T>, AntennaStudy>
auto fields(T& value) { return std::tie(value.identity, value.coordinate_frames, value.geometry, value.parameters, value.materials, value.roles, value.ports, value.boundaries, value.solver_policy, value.fidelity_and_cost, value.monitors, value.goals, value.provenance); }

template<class T> struct IsVector : std::false_type {};
template<class T> struct IsVector<std::vector<T>> : std::true_type {};
template<class T> struct IsOptional : std::false_type {};
template<class T> struct IsOptional<std::optional<T>> : std::true_type {};
template<class T> struct IsArray : std::false_type {};
template<class T, std::size_t N> struct IsArray<std::array<T, N>> : std::true_type {};

template<class T> constexpr std::uint64_t enum_maximum();
template<> constexpr std::uint64_t enum_maximum<SiDimension>() { return static_cast<std::uint64_t>(SiDimension::power); }
template<> constexpr std::uint64_t enum_maximum<EntityKind>() { return static_cast<std::uint64_t>(EntityKind::vertex); }
template<> constexpr std::uint64_t enum_maximum<ParameterAuthority>() { return static_cast<std::uint64_t>(ParameterAuthority::derived); }
template<> constexpr std::uint64_t enum_maximum<DispersionModel>() { return static_cast<std::uint64_t>(DispersionModel::lorentz); }
template<> constexpr std::uint64_t enum_maximum<ElectromagneticRole>() { return static_cast<std::uint64_t>(ElectromagneticRole::periodic_region); }
template<> constexpr std::uint64_t enum_maximum<PortKind>() { return static_cast<std::uint64_t>(PortKind::waveguide); }
template<> constexpr std::uint64_t enum_maximum<PortTermination>() { return static_cast<std::uint64_t>(PortTermination::short_circuit); }
template<> constexpr std::uint64_t enum_maximum<PhasorConvention>() { return static_cast<std::uint64_t>(PhasorConvention::exp_negative_jwt); }
template<> constexpr std::uint64_t enum_maximum<BoundaryKind>() { return static_cast<std::uint64_t>(BoundaryKind::periodic); }
template<> constexpr std::uint64_t enum_maximum<SolverFamily>() { return static_cast<std::uint64_t>(SolverFamily::method_of_moments); }
template<> constexpr std::uint64_t enum_maximum<PrecisionPolicy>() { return static_cast<std::uint64_t>(PrecisionPolicy::mixed); }
template<> constexpr std::uint64_t enum_maximum<FidelityProfile>() { return static_cast<std::uint64_t>(FidelityProfile::verification); }
template<> constexpr std::uint64_t enum_maximum<MonitorKind>() { return static_cast<std::uint64_t>(MonitorKind::polarization); }

class Writer {
    std::uint64_t entries_{};
public:
    std::vector<std::byte> bytes;
    void integer(std::uint64_t value) {
        for (unsigned int i = 0; i < 8; ++i) {
            bytes.push_back(static_cast<std::byte>(value & 0xffU));
            value >>= 8;
        }
    }
    template<class T> void value(const T& item) {
        if constexpr (std::is_same_v<T, std::string>) {
            if (item.size() > maximum_text) throw CheckpointFormatError("project text limit");
            integer(item.size());
            for (const char c : item) bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
        } else if constexpr (std::is_same_v<T, std::byte>) {
            bytes.push_back(item);
        } else if constexpr (std::is_same_v<T, double>) {
            if (!std::isfinite(item)) throw CheckpointFormatError("nonfinite study value");
            integer(std::bit_cast<std::uint64_t>(item));
        } else if constexpr (std::is_enum_v<T>) {
            if (static_cast<std::uint64_t>(item) > enum_maximum<T>()) throw CheckpointFormatError("invalid project enum");
            integer(static_cast<std::uint64_t>(item));
        } else if constexpr (std::is_integral_v<T>) {
            integer(static_cast<std::uint64_t>(item));
        } else if constexpr (std::is_same_v<T, document::RevisionId>) {
            integer(item.value);
        } else if constexpr (IsOptional<T>::value) {
            value(item.has_value());
            if (item) value(*item);
        } else if constexpr (IsVector<T>::value || IsArray<T>::value) {
            if constexpr (IsVector<T>::value) {
                if (item.size() > (std::is_same_v<typename T::value_type, std::byte> ? maximum_bytes : maximum_entries)) throw CheckpointFormatError("project collection limit");
                if constexpr (!std::is_same_v<typename T::value_type, std::byte>) {
                    if (item.size() > maximum_entries - entries_) throw CheckpointFormatError("project aggregate entry limit");
                    entries_ += item.size();
                }
                integer(item.size());
            }
            for (const auto& element : item) value(element);
        } else {
            std::apply([this](const auto&... members) { (value(members), ...); }, fields(item));
        }
        if (bytes.size() > maximum_bytes) throw CheckpointFormatError("project payload limit");
    }
};

class Reader {
    std::span<const std::byte> bytes_;
    std::size_t position_{};
    std::uint64_t entries_{};
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}
    std::uint64_t integer() {
        if (bytes_.size() - position_ < 8) throw CheckpointFormatError("truncated project value");
        std::uint64_t result{};
        for (unsigned int i = 0; i < 8; ++i)
            result |= static_cast<std::uint64_t>(std::to_integer<unsigned int>(bytes_[position_++])) << (i * 8);
        return result;
    }
    template<class T> void value(T& item) {
        if constexpr (std::is_same_v<T, std::string>) {
            const auto size = integer();
            if (size > maximum_text || size > bytes_.size() - position_) throw CheckpointFormatError("project text bound");
            item.assign(reinterpret_cast<const char*>(bytes_.data() + position_), static_cast<std::size_t>(size));
            position_ += static_cast<std::size_t>(size);
        } else if constexpr (std::is_same_v<T, std::byte>) {
            if (position_ == bytes_.size()) throw CheckpointFormatError("truncated project blob");
            item = bytes_[position_++];
        } else if constexpr (std::is_same_v<T, double>) {
            item = std::bit_cast<double>(integer());
            if (!std::isfinite(item)) throw CheckpointFormatError("nonfinite project value");
        } else if constexpr (std::is_same_v<T, bool>) {
            const auto bit = integer();
            if (bit > 1) throw CheckpointFormatError("invalid project boolean");
            item = bit == 1;
        } else if constexpr (std::is_integral_v<T>) {
            const auto number = integer();
            if (number > std::numeric_limits<T>::max()) throw CheckpointFormatError("project integer overflow");
            item = static_cast<T>(number);
        } else if constexpr (std::is_enum_v<T>) {
            const auto number = integer();
            if (number > enum_maximum<T>()) throw CheckpointFormatError("project enum overflow");
            item = static_cast<T>(number);
        } else if constexpr (std::is_same_v<T, document::RevisionId>) {
            item.value = integer();
        } else if constexpr (IsOptional<T>::value) {
            bool present{}; value(present);
            if (present) { item.emplace(); value(*item); } else item.reset();
        } else if constexpr (IsVector<T>::value) {
            const auto count = integer();
            // Every encoded element consumes at least one byte. Check before
            // resizing; aggregate entry limits prevent nested allocation bombs.
            if (count > bytes_.size() - position_) throw CheckpointFormatError("project collection bound");
            if constexpr (!std::is_same_v<typename T::value_type, std::byte>) {
                if (count > maximum_entries - entries_) throw CheckpointFormatError("project aggregate entry limit");
                entries_ += count;
            }
            item.resize(static_cast<std::size_t>(count));
            for (auto& element : item) value(element);
        } else if constexpr (IsArray<T>::value) {
            for (auto& element : item) value(element);
        } else {
            std::apply([this](auto&... members) { (value(members), ...); }, fields(item));
        }
    }
    bool done() const { return position_ == bytes_.size(); }
};

void check_identity(const ProjectCheckpoint& project) {
    if (project.cad_document_id.empty()) throw CheckpointFormatError("project requires document identity");
    if (project.worker_recovery && !project.study)
        throw CheckpointFormatError("worker recovery requires an ASIR study");
    if (project.study && (project.study->identity.version != asir_version ||
        project.study->identity.cad_document_id != project.cad_document_id))
        throw CheckpointFormatError("project study version/document identity mismatch");
    if (project.study) {
        const auto& identity = project.study->identity;
        const auto semantic = std::ranges::find(project.document.revisions, identity.semantic_revision_id, &document::DocumentRevision::id);
        const auto evaluation = std::ranges::find(project.document.evaluations, identity.evaluated_revision_id, &document::EvaluationRecord::revision_id);
        if (semantic == project.document.revisions.end() || evaluation == project.document.evaluations.end() ||
            !evaluation->geometry_hash || *evaluation->geometry_hash != identity.geometry_hash)
            throw CheckpointFormatError("project study snapshot is absent from CAD history");
    }
}
} // namespace

std::vector<std::byte> encode_project_checkpoint(const ProjectCheckpoint& project) {
    check_identity(project);
    Writer writer;
    writer.value(std::string("oep-project-checkpoint-2"));
    writer.value(project.cad_document_id);
    writer.value(persistence::encode_document_checkpoint(project.document));
    writer.value(project.study);
    writer.value(project.study ? canonical_fingerprint(*project.study) : std::string{});
    writer.value(project.worker_recovery);
    return ipc::encode_frame({project_type, 0, std::move(writer.bytes)});
}

ProjectCheckpoint decode_project_checkpoint(std::span<const std::byte> bytes) {
    const auto decoded = ipc::decode_frame(bytes, {.maximum_payload_bytes = maximum_bytes});
    if (!decoded.succeeded() || decoded.frame->message_type != project_type || decoded.frame->correlation_id != 0)
        throw CheckpointFormatError("project frame checksum/type/version is invalid");
    Reader reader(decoded.frame->payload);
    std::string version; reader.value(version);
    if (version != "oep-project-checkpoint-1" && version != "oep-project-checkpoint-2")
        throw CheckpointFormatError("unsupported project version");
    ProjectCheckpoint project;
    reader.value(project.cad_document_id);
    std::vector<std::byte> cad; reader.value(cad);
    project.document = persistence::decode_document_checkpoint(cad);
    reader.value(project.study);
    std::string fingerprint; reader.value(fingerprint);
    if (version == "oep-project-checkpoint-2") reader.value(project.worker_recovery);
    if (!reader.done()) throw CheckpointFormatError("trailing project data");
    check_identity(project);
    if (fingerprint != (project.study ? canonical_fingerprint(*project.study) : std::string{}))
        throw CheckpointFormatError("project study content identity mismatch");
    return project;
}

void save_project_checkpoint_atomic(const std::filesystem::path& path, const ProjectCheckpoint& project) {
    persistence::save_checkpoint_bytes_atomic(path, encode_project_checkpoint(project));
}

ProjectCheckpoint load_project_checkpoint(const std::filesystem::path& path) {
    const auto size = std::filesystem::file_size(path);
    if (size > maximum_bytes + 64) throw CheckpointFormatError("project file size limit");
    std::ifstream input(path, std::ios::binary);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!input || input.peek() != std::ifstream::traits_type::eof()) throw CheckpointFormatError("project file read failed");
    return decode_project_checkpoint(bytes);
}
} // namespace oep::simulation_ir
