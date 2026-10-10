#include <oep/persistence/checkpoint.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cstdint>
#include <fstream>
#include <limits>
#include <random>
#include <string>
#include <system_error>
#include <type_traits>
#include <unordered_set>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace oep::persistence {
namespace {

using document::CommittedTransaction;
using document::Diagnostic;
using document::DocumentRevision;
using document::DocumentState;
using document::EvaluationRecord;
using document::FeatureDefinition;
using document::FeatureDependency;
using document::FeatureDependencyAddition;
using document::FeatureEvaluation;
using document::FeatureGraph;
using document::FeatureId;
using document::FeatureState;
using document::GeometrySnapshot;
using document::ParameterChange;
using document::RevisionId;
using document::SemanticState;

constexpr std::array<std::byte, 8> checkpoint_magic{
    std::byte{'O'},
    std::byte{'E'},
    std::byte{'P'},
    std::byte{'C'},
    std::byte{'H'},
    std::byte{'K'},
    std::byte{'P'},
    std::byte{'T'},
};
constexpr std::uint32_t checkpoint_version = 2;
constexpr std::size_t checkpoint_header_size = 28;
constexpr std::uint64_t fnv_offset_basis = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;

class Encoder {
public:
    void write_u8(const std::uint8_t value)
    {
        bytes_.push_back(static_cast<std::byte>(value));
    }

    void write_u32(const std::uint32_t value)
    {
        write_unsigned(value);
    }

    void write_u64(const std::uint64_t value)
    {
        write_unsigned(value);
    }

    void write_bool(const bool value)
    {
        write_u8(value ? 1U : 0U);
    }

    void write_double(const double value)
    {
        write_u64(std::bit_cast<std::uint64_t>(value));
    }

    void write_size(const std::size_t value)
    {
        write_u64(static_cast<std::uint64_t>(value));
    }

    void write_string(const std::string_view value)
    {
        write_size(value.size());
        for (const auto character : value) {
            bytes_.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
        }
    }

    void write_bytes(const std::span<const std::byte> value)
    {
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    [[nodiscard]] std::vector<std::byte> take() &&
    {
        return std::move(bytes_);
    }

private:
    template <typename Unsigned>
    void write_unsigned(const Unsigned value)
    {
        static_assert(std::is_unsigned_v<Unsigned>);
        for (std::size_t index = 0; index < sizeof(Unsigned); ++index) {
            const auto shift = static_cast<unsigned int>(index * 8U);
            bytes_.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
        }
    }

    std::vector<std::byte> bytes_;
};

class Decoder {
public:
    Decoder(const std::span<const std::byte> bytes, const CheckpointDecodeLimits& limits)
        : bytes_(bytes)
        , limits_(limits)
    {
    }

    [[nodiscard]] std::uint8_t read_u8()
    {
        require(1);
        return std::to_integer<std::uint8_t>(bytes_[cursor_++]);
    }

    [[nodiscard]] std::uint32_t read_u32()
    {
        return read_unsigned<std::uint32_t>();
    }

    [[nodiscard]] std::uint64_t read_u64()
    {
        return read_unsigned<std::uint64_t>();
    }

    [[nodiscard]] bool read_bool()
    {
        const auto value = read_u8();
        if (value > 1U) {
            throw CheckpointFormatError("checkpoint contains an invalid boolean value");
        }
        return value == 1U;
    }

    [[nodiscard]] double read_double()
    {
        return std::bit_cast<double>(read_u64());
    }

    [[nodiscard]] std::size_t read_collection_size()
    {
        const auto value = read_u64();
        if (value > limits_.max_collection_entries
            || value > std::numeric_limits<std::size_t>::max()
            || entries_seen_ > limits_.max_collection_entries - value) {
            throw CheckpointFormatError("checkpoint collection limit exceeded");
        }
        entries_seen_ += value;
        return static_cast<std::size_t>(value);
    }

    [[nodiscard]] std::size_t read_index()
    {
        const auto value = read_u64();
        if (value > std::numeric_limits<std::size_t>::max()) {
            throw CheckpointFormatError("checkpoint index is too large");
        }
        return static_cast<std::size_t>(value);
    }

    [[nodiscard]] std::string read_string()
    {
        const auto length = read_u64();
        if (length > limits_.max_string_bytes
            || length > std::numeric_limits<std::size_t>::max()) {
            throw CheckpointFormatError("checkpoint string limit exceeded");
        }
        const auto size = static_cast<std::size_t>(length);
        require(size);
        std::string value;
        value.reserve(size);
        for (std::size_t index = 0; index < size; ++index) {
            value.push_back(static_cast<char>(std::to_integer<unsigned char>(bytes_[cursor_++])));
        }
        return value;
    }

    [[nodiscard]] bool done() const noexcept
    {
        return cursor_ == bytes_.size();
    }

private:
    template <typename Unsigned>
    [[nodiscard]] Unsigned read_unsigned()
    {
        static_assert(std::is_unsigned_v<Unsigned>);
        require(sizeof(Unsigned));
        Unsigned value = 0;
        for (std::size_t index = 0; index < sizeof(Unsigned); ++index) {
            const auto shift = static_cast<unsigned int>(index * 8U);
            value |= static_cast<Unsigned>(
                         std::to_integer<std::uint8_t>(bytes_[cursor_++]))
                << shift;
        }
        return value;
    }

    void require(const std::size_t byte_count) const
    {
        if (byte_count > bytes_.size() - cursor_) {
            throw CheckpointFormatError("checkpoint is truncated");
        }
    }

    std::span<const std::byte> bytes_;
    const CheckpointDecodeLimits& limits_;
    std::size_t cursor_{};
    std::uint64_t entries_seen_{};
};

[[nodiscard]] std::uint64_t checksum(const std::span<const std::byte> bytes) noexcept
{
    auto value = fnv_offset_basis;
    for (const auto byte : bytes) {
        value ^= std::to_integer<std::uint8_t>(byte);
        value *= fnv_prime;
    }
    return value;
}

void encode_revision_id(Encoder& encoder, const RevisionId revision_id)
{
    encoder.write_u64(revision_id.value);
}

[[nodiscard]] RevisionId decode_revision_id(Decoder& decoder)
{
    return RevisionId{decoder.read_u64()};
}

void encode_optional_revision_id(
    Encoder& encoder,
    const std::optional<RevisionId> revision_id)
{
    encoder.write_bool(revision_id.has_value());
    if (revision_id.has_value()) {
        encode_revision_id(encoder, *revision_id);
    }
}

[[nodiscard]] std::optional<RevisionId> decode_optional_revision_id(Decoder& decoder)
{
    if (!decoder.read_bool()) {
        return std::nullopt;
    }
    return decode_revision_id(decoder);
}

void encode_optional_string(Encoder& encoder, const std::optional<std::string>& value)
{
    encoder.write_bool(value.has_value());
    if (value.has_value()) {
        encoder.write_string(*value);
    }
}

void encode_feature_id(Encoder& encoder, const FeatureId& feature_id)
{
    encoder.write_string(feature_id.value);
}

[[nodiscard]] FeatureId decode_feature_id(Decoder& decoder)
{
    return FeatureId{decoder.read_string()};
}

void encode_optional_feature_id(
    Encoder& encoder,
    const std::optional<FeatureId>& feature_id)
{
    encoder.write_bool(feature_id.has_value());
    if (feature_id.has_value()) {
        encode_feature_id(encoder, *feature_id);
    }
}

[[nodiscard]] std::optional<FeatureId> decode_optional_feature_id(Decoder& decoder)
{
    if (!decoder.read_bool()) {
        return std::nullopt;
    }
    return decode_feature_id(decoder);
}

[[nodiscard]] std::optional<std::string> decode_optional_string(Decoder& decoder)
{
    if (!decoder.read_bool()) {
        return std::nullopt;
    }
    return decoder.read_string();
}

void encode_diagnostic(Encoder& encoder, const Diagnostic& diagnostic)
{
    encoder.write_string(diagnostic.code);
    encoder.write_string(diagnostic.message);
    encode_optional_feature_id(encoder, diagnostic.feature_id);
}

[[nodiscard]] Diagnostic decode_diagnostic(Decoder& decoder)
{
    return Diagnostic{
        .code = decoder.read_string(),
        .message = decoder.read_string(),
        .feature_id = decode_optional_feature_id(decoder),
    };
}

void encode_diagnostics(Encoder& encoder, const std::vector<Diagnostic>& diagnostics)
{
    encoder.write_size(diagnostics.size());
    for (const auto& diagnostic : diagnostics) {
        encode_diagnostic(encoder, diagnostic);
    }
}

[[nodiscard]] std::vector<Diagnostic> decode_diagnostics(Decoder& decoder)
{
    const auto count = decoder.read_collection_size();
    std::vector<Diagnostic> diagnostics;
    diagnostics.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        diagnostics.push_back(decode_diagnostic(decoder));
    }
    return diagnostics;
}

void encode_semantic_state(Encoder& encoder, const SemanticState& semantic_state)
{
    encoder.write_size(semantic_state.size());
    for (const auto& [parameter_id, value] : semantic_state) {
        encoder.write_string(parameter_id);
        encoder.write_double(value);
    }
}

[[nodiscard]] SemanticState decode_semantic_state(Decoder& decoder)
{
    const auto count = decoder.read_collection_size();
    SemanticState semantic_state;
    for (std::size_t index = 0; index < count; ++index) {
        auto parameter_id = decoder.read_string();
        const auto value = decoder.read_double();
        if (!semantic_state.emplace(std::move(parameter_id), value).second) {
            throw CheckpointFormatError("checkpoint contains a duplicate parameter ID");
        }
    }
    return semantic_state;
}

void encode_feature_graph(Encoder& encoder, const FeatureGraph& graph)
{
    encoder.write_size(graph.size());
    for (const auto& feature : graph) {
        encode_feature_id(encoder, feature.id);
        encoder.write_size(feature.parameter_ids.size());
        for (const auto& parameter_id : feature.parameter_ids) {
            encoder.write_string(parameter_id);
        }
        encoder.write_size(feature.dependencies.size());
        for (const auto& dependency : feature.dependencies) {
            encode_feature_id(encoder, dependency.upstream_feature_id);
            encoder.write_string(dependency.input_name);
        }
    }
}

[[nodiscard]] FeatureGraph decode_feature_graph(Decoder& decoder)
{
    const auto feature_count = decoder.read_collection_size();
    FeatureGraph graph;
    graph.reserve(feature_count);
    for (std::size_t feature_index = 0; feature_index < feature_count; ++feature_index) {
        FeatureDefinition feature;
        feature.id = decode_feature_id(decoder);
        const auto parameter_count = decoder.read_collection_size();
        feature.parameter_ids.reserve(parameter_count);
        for (std::size_t parameter_index = 0; parameter_index < parameter_count;
             ++parameter_index) {
            feature.parameter_ids.push_back(decoder.read_string());
        }
        const auto dependency_count = decoder.read_collection_size();
        feature.dependencies.reserve(dependency_count);
        for (std::size_t dependency_index = 0; dependency_index < dependency_count;
             ++dependency_index) {
            feature.dependencies.push_back(FeatureDependency{
                .upstream_feature_id = decode_feature_id(decoder),
                .input_name = decoder.read_string(),
            });
        }
        graph.push_back(std::move(feature));
    }
    return graph;
}

void encode_parameter_changes(
    Encoder& encoder,
    const std::vector<ParameterChange>& parameter_changes)
{
    encoder.write_size(parameter_changes.size());
    for (const auto& change : parameter_changes) {
        encoder.write_string(change.parameter_id);
        encoder.write_double(change.value);
    }
}

[[nodiscard]] std::vector<ParameterChange> decode_parameter_changes(Decoder& decoder)
{
    const auto count = decoder.read_collection_size();
    std::vector<ParameterChange> changes;
    changes.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        changes.push_back(ParameterChange{
            .parameter_id = decoder.read_string(),
            .value = decoder.read_double(),
        });
    }
    return changes;
}

void encode_dependency_additions(
    Encoder& encoder,
    const std::vector<FeatureDependencyAddition>& additions)
{
    encoder.write_size(additions.size());
    for (const auto& addition : additions) {
        encode_feature_id(encoder, addition.upstream_feature_id);
        encode_feature_id(encoder, addition.downstream_feature_id);
        encoder.write_string(addition.input_name);
    }
}

[[nodiscard]] std::vector<FeatureDependencyAddition> decode_dependency_additions(
    Decoder& decoder)
{
    const auto count = decoder.read_collection_size();
    std::vector<FeatureDependencyAddition> additions;
    additions.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        additions.push_back(FeatureDependencyAddition{
            .upstream_feature_id = decode_feature_id(decoder),
            .downstream_feature_id = decode_feature_id(decoder),
            .input_name = decoder.read_string(),
        });
    }
    return additions;
}

void encode_feature(Encoder& encoder, const FeatureEvaluation& feature)
{
    encode_feature_id(encoder, feature.feature_id);
    encoder.write_u8(static_cast<std::uint8_t>(feature.state));
    encode_optional_string(encoder, feature.output_hash);
    encode_diagnostics(encoder, feature.diagnostics);
}

[[nodiscard]] FeatureEvaluation decode_feature(Decoder& decoder)
{
    auto feature_id = decode_feature_id(decoder);
    const auto raw_state = decoder.read_u8();
    if (raw_state > static_cast<std::uint8_t>(FeatureState::blocked)) {
        throw CheckpointFormatError("checkpoint contains an unknown feature state");
    }
    return FeatureEvaluation{
        .feature_id = std::move(feature_id),
        .state = static_cast<FeatureState>(raw_state),
        .output_hash = decode_optional_string(decoder),
        .diagnostics = decode_diagnostics(decoder),
    };
}

void encode_state_payload(Encoder& encoder, const DocumentState& state)
{
    encoder.write_u64(state.next_revision_value);

    encoder.write_size(state.revisions.size());
    for (const auto& revision : state.revisions) {
        encode_revision_id(encoder, revision.id);
        encode_optional_revision_id(encoder, revision.parent_id);
        encode_semantic_state(encoder, revision.semantic_state);
        encode_feature_graph(encoder, revision.feature_graph);
    }

    encoder.write_size(state.timeline.size());
    for (const auto revision_id : state.timeline) {
        encode_revision_id(encoder, revision_id);
    }
    encoder.write_size(state.timeline_cursor);

    encoder.write_size(state.command_journal.size());
    for (const auto& transaction : state.command_journal) {
        encode_revision_id(encoder, transaction.base_revision_id);
        encode_revision_id(encoder, transaction.result_revision_id);
        encode_parameter_changes(encoder, transaction.parameter_changes);
        encode_dependency_additions(encoder, transaction.dependency_additions);
    }

    encoder.write_size(state.evaluations.size());
    for (const auto& evaluation : state.evaluations) {
        encode_revision_id(encoder, evaluation.revision_id);
        encoder.write_string(evaluation.evaluator_version);
        encoder.write_size(evaluation.features.size());
        for (const auto& feature : evaluation.features) {
            encode_feature(encoder, feature);
        }
        encode_diagnostics(encoder, evaluation.diagnostics);
        encode_optional_string(encoder, evaluation.geometry_hash);
    }

    encode_optional_revision_id(encoder, state.evaluated_revision_id);
    encoder.write_bool(state.last_good_geometry.has_value());
    if (state.last_good_geometry.has_value()) {
        encode_revision_id(encoder, state.last_good_geometry->revision_id);
        encoder.write_string(state.last_good_geometry->content_hash);
    }
}

[[nodiscard]] DocumentState decode_state_payload(Decoder& decoder)
{
    DocumentState state;
    state.next_revision_value = decoder.read_u64();

    const auto revision_count = decoder.read_collection_size();
    state.revisions.reserve(revision_count);
    for (std::size_t index = 0; index < revision_count; ++index) {
        state.revisions.push_back(DocumentRevision{
            .id = decode_revision_id(decoder),
            .parent_id = decode_optional_revision_id(decoder),
            .semantic_state = decode_semantic_state(decoder),
            .feature_graph = decode_feature_graph(decoder),
        });
    }

    const auto timeline_count = decoder.read_collection_size();
    state.timeline.reserve(timeline_count);
    for (std::size_t index = 0; index < timeline_count; ++index) {
        state.timeline.push_back(decode_revision_id(decoder));
    }
    state.timeline_cursor = decoder.read_index();

    const auto journal_count = decoder.read_collection_size();
    state.command_journal.reserve(journal_count);
    for (std::size_t index = 0; index < journal_count; ++index) {
        state.command_journal.push_back(CommittedTransaction{
            .base_revision_id = decode_revision_id(decoder),
            .result_revision_id = decode_revision_id(decoder),
            .parameter_changes = decode_parameter_changes(decoder),
            .dependency_additions = decode_dependency_additions(decoder),
        });
    }

    const auto evaluation_count = decoder.read_collection_size();
    state.evaluations.reserve(evaluation_count);
    for (std::size_t index = 0; index < evaluation_count; ++index) {
        EvaluationRecord record;
        record.revision_id = decode_revision_id(decoder);
        record.evaluator_version = decoder.read_string();
        const auto feature_count = decoder.read_collection_size();
        record.features.reserve(feature_count);
        for (std::size_t feature_index = 0; feature_index < feature_count; ++feature_index) {
            record.features.push_back(decode_feature(decoder));
        }
        record.diagnostics = decode_diagnostics(decoder);
        record.geometry_hash = decode_optional_string(decoder);
        state.evaluations.push_back(std::move(record));
    }

    state.evaluated_revision_id = decode_optional_revision_id(decoder);
    if (decoder.read_bool()) {
        state.last_good_geometry = GeometrySnapshot{
            .revision_id = decode_revision_id(decoder),
            .content_hash = decoder.read_string(),
        };
    }
    return state;
}

[[nodiscard]] std::filesystem::path temporary_sibling(const std::filesystem::path& path)
{
    std::random_device random_source;
    auto temporary = path;
    temporary += ".tmp-" + std::to_string(
                              (static_cast<std::uint64_t>(random_source()) << 32U)
                              ^ static_cast<std::uint64_t>(random_source()));
    return temporary;
}

#if defined(_WIN32)

void write_and_replace(
    const std::filesystem::path& path,
    const std::span<const std::byte> bytes)
{
    std::filesystem::path temporary;
    HANDLE file = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 16 && file == INVALID_HANDLE_VALUE; ++attempt) {
        temporary = temporary_sibling(path);
        file = CreateFileW(
            temporary.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS) {
            throw std::system_error(
                static_cast<int>(GetLastError()),
                std::system_category(),
                "cannot create checkpoint temporary file");
        }
    }
    if (file == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("cannot allocate a unique checkpoint temporary file");
    }

    auto offset = std::size_t{0};
    try {
        while (offset < bytes.size()) {
            const auto remaining = bytes.size() - offset;
            const auto chunk = static_cast<DWORD>(std::min<std::size_t>(
                remaining, std::numeric_limits<DWORD>::max()));
            DWORD written = 0;
            if (!WriteFile(file, bytes.data() + offset, chunk, &written, nullptr)) {
                throw std::system_error(
                    static_cast<int>(GetLastError()),
                    std::system_category(),
                    "cannot write checkpoint temporary file");
            }
            if (written == 0) {
                throw std::runtime_error("checkpoint write made no progress");
            }
            offset += written;
        }
        if (!FlushFileBuffers(file)) {
            throw std::system_error(
                static_cast<int>(GetLastError()),
                std::system_category(),
                "cannot flush checkpoint temporary file");
        }
        CloseHandle(file);
        file = INVALID_HANDLE_VALUE;
        if (!MoveFileExW(
                temporary.c_str(),
                path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::system_error(
                static_cast<int>(GetLastError()),
                std::system_category(),
                "cannot atomically replace checkpoint");
        }
    } catch (...) {
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

#else

void write_and_replace(
    const std::filesystem::path& path,
    const std::span<const std::byte> bytes)
{
    std::filesystem::path temporary;
    auto file_descriptor = -1;
    for (int attempt = 0; attempt < 16 && file_descriptor == -1; ++attempt) {
        temporary = temporary_sibling(path);
        file_descriptor = ::open(
            temporary.c_str(),
            O_WRONLY | O_CREAT | O_EXCL,
            S_IRUSR | S_IWUSR);
        if (file_descriptor == -1 && errno != EEXIST) {
            throw std::system_error(
                errno, std::generic_category(), "cannot create checkpoint temporary file");
        }
    }
    if (file_descriptor == -1) {
        throw std::runtime_error("cannot allocate a unique checkpoint temporary file");
    }

    auto offset = std::size_t{0};
    try {
        while (offset < bytes.size()) {
            const auto remaining = bytes.size() - offset;
            const auto chunk = std::min<std::size_t>(remaining, 1U << 30U);
            const auto written = ::write(file_descriptor, bytes.data() + offset, chunk);
            if (written < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::system_error(
                    errno, std::generic_category(), "cannot write checkpoint temporary file");
            }
            if (written == 0) {
                throw std::runtime_error("checkpoint write made no progress");
            }
            offset += static_cast<std::size_t>(written);
        }
        if (::fsync(file_descriptor) != 0) {
            throw std::system_error(
                errno, std::generic_category(), "cannot flush checkpoint temporary file");
        }
        if (::close(file_descriptor) != 0) {
            file_descriptor = -1;
            throw std::system_error(
                errno, std::generic_category(), "cannot close checkpoint temporary file");
        }
        file_descriptor = -1;
        if (::rename(temporary.c_str(), path.c_str()) != 0) {
            throw std::system_error(
                errno, std::generic_category(), "cannot atomically replace checkpoint");
        }

        const auto parent = path.parent_path().empty() ? std::filesystem::path{"."}
                                                       : path.parent_path();
        const auto directory_descriptor = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
        if (directory_descriptor == -1) {
            throw std::system_error(
                errno, std::generic_category(), "cannot open checkpoint directory");
        }
        const auto sync_result = ::fsync(directory_descriptor);
        const auto sync_error = errno;
        const auto close_result = ::close(directory_descriptor);
        if (sync_result != 0) {
            throw std::system_error(
                sync_error, std::generic_category(), "cannot flush checkpoint directory");
        }
        if (close_result != 0) {
            throw std::system_error(
                errno, std::generic_category(), "cannot close checkpoint directory");
        }
    } catch (...) {
        if (file_descriptor != -1) {
            ::close(file_descriptor);
        }
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

#endif

} // namespace

std::vector<std::byte> encode_document_checkpoint(const DocumentState& state)
{
    Encoder payload_encoder;
    encode_state_payload(payload_encoder, state);
    const auto payload = std::move(payload_encoder).take();

    Encoder checkpoint_encoder;
    checkpoint_encoder.write_bytes(checkpoint_magic);
    checkpoint_encoder.write_u32(checkpoint_version);
    checkpoint_encoder.write_size(payload.size());
    checkpoint_encoder.write_u64(checksum(payload));
    checkpoint_encoder.write_bytes(payload);
    return std::move(checkpoint_encoder).take();
}

DocumentState decode_document_checkpoint(
    const std::span<const std::byte> encoded,
    const CheckpointDecodeLimits& limits)
{
    if (encoded.size() < checkpoint_header_size) {
        throw CheckpointFormatError("checkpoint header is truncated");
    }

    Decoder header(encoded.first(checkpoint_header_size), limits);
    for (const auto expected : checkpoint_magic) {
        if (header.read_u8() != std::to_integer<std::uint8_t>(expected)) {
            throw CheckpointFormatError("checkpoint magic does not match");
        }
    }
    if (header.read_u32() != checkpoint_version) {
        throw CheckpointFormatError("checkpoint version is not supported");
    }
    const auto payload_size = header.read_u64();
    const auto expected_checksum = header.read_u64();
    if (payload_size > limits.max_payload_bytes
        || payload_size != encoded.size() - checkpoint_header_size) {
        throw CheckpointFormatError("checkpoint payload size is invalid");
    }

    const auto payload = encoded.subspan(checkpoint_header_size);
    if (checksum(payload) != expected_checksum) {
        throw CheckpointFormatError("checkpoint checksum does not match");
    }

    Decoder decoder(payload, limits);
    auto state = decode_state_payload(decoder);
    if (!decoder.done()) {
        throw CheckpointFormatError("checkpoint contains trailing payload data");
    }
    return state;
}

void save_document_checkpoint_atomic(
    const std::filesystem::path& path,
    const DocumentState& state)
{
    if (path.empty()) {
        throw std::invalid_argument("checkpoint path must not be empty");
    }
    const auto encoded = encode_document_checkpoint(state);
    write_and_replace(path, encoded);
}

DocumentState load_document_checkpoint(
    const std::filesystem::path& path,
    const CheckpointDecodeLimits& limits)
{
    std::error_code size_error;
    const auto file_size = std::filesystem::file_size(path, size_error);
    if (size_error) {
        throw std::system_error(size_error, "cannot inspect checkpoint file");
    }
    const auto payload_exceeds_limit = file_size >= checkpoint_header_size
        && file_size - checkpoint_header_size > limits.max_payload_bytes;
    if (payload_exceeds_limit || file_size > std::numeric_limits<std::size_t>::max()) {
        throw CheckpointFormatError("checkpoint file exceeds the configured size limit");
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open checkpoint file");
    }
    std::vector<std::byte> encoded(static_cast<std::size_t>(file_size));
    input.read(
        reinterpret_cast<char*>(encoded.data()),
        static_cast<std::streamsize>(encoded.size()));
    if (!input || input.peek() != std::ifstream::traits_type::eof()) {
        throw std::runtime_error("cannot read checkpoint file consistently");
    }
    return decode_document_checkpoint(encoded, limits);
}

} // namespace oep::persistence
