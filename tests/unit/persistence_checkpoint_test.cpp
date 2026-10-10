// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#include <oep/document/document.hpp>
#include <oep/persistence/checkpoint.hpp>

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

using oep::document::CommandTransaction;
using oep::document::Document;
using oep::document::Evaluator;
using oep::document::FeatureDefinition;
using oep::document::FeatureEvaluation;
using oep::document::FeatureEvaluationRequest;
using oep::document::FeatureEvaluationResult;
using oep::document::FeatureGraph;
using oep::document::FeatureId;
using oep::document::ParameterChange;
using oep::document::RevisionId;
using oep::document::SemanticState;
using oep::document::SetParameterCommand;
using oep::persistence::CheckpointDecodeLimits;
using oep::persistence::CheckpointFormatError;
using oep::persistence::decode_document_checkpoint;
using oep::persistence::encode_document_checkpoint;
using oep::persistence::load_document_checkpoint;
using oep::persistence::save_document_checkpoint_atomic;

class TestEvaluator final : public Evaluator {
public:
    [[nodiscard]] std::string_view version() const noexcept override
    {
        return "persistence-test-evaluator-v1";
    }

    [[nodiscard]] FeatureEvaluationResult evaluate_feature(
        const FeatureEvaluationRequest& request) override
    {
        const auto width = request.semantic_state.at("width");
        if (width <= 0.0) {
            return FeatureEvaluationResult{
                .output_hash = std::nullopt,
                .diagnostics = {},
            };
        }
        return FeatureEvaluationResult{
            .output_hash = "feature:" + std::to_string(request.revision_id.value),
            .diagnostics = {},
        };
    }

    [[nodiscard]] std::optional<std::string> compose_geometry(
        const RevisionId revision_id,
        const SemanticState&,
        std::span<const FeatureEvaluation>) override
    {
        return "geometry:" + std::to_string(revision_id.value);
    }
};

[[nodiscard]] FeatureGraph feature_graph()
{
    return {FeatureDefinition{
        .id = FeatureId{"body"},
        .parameter_ids = {"height", "width"},
        .dependencies = {},
    }};
}

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        std::random_device random_source;
        for (int attempt = 0; attempt < 16; ++attempt) {
            path_ = std::filesystem::temp_directory_path()
                / ("oep-checkpoint-test-" + std::to_string(random_source()));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
            if (error && error != std::errc::file_exists) {
                throw std::system_error(error, "cannot create test directory");
            }
        }
        throw std::runtime_error("cannot allocate a unique test directory");
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

int failures{};

void check(const bool condition, const std::string_view description)
{
    if (!condition) {
        std::cerr << "FAILED: " << description << '\n';
        ++failures;
    }
}

template <typename Operation>
void check_format_error(Operation&& operation, const std::string_view description)
{
    auto rejected = false;
    try {
        std::forward<Operation>(operation)();
    } catch (const CheckpointFormatError&) {
        rejected = true;
    }
    check(rejected, description);
}

[[nodiscard]] Document make_failed_document(TestEvaluator& evaluator)
{
    Document document({{"height", 2.0}, {"width", 10.0}}, feature_graph(), evaluator);
    static_cast<void>(document.apply(CommandTransaction{
        .expected_revision = RevisionId{1},
        .parameter_changes = {
            ParameterChange{.parameter_id = "width", .value = 20.0},
            ParameterChange{.parameter_id = "height", .value = 4.0},
        },
        .dependency_additions = {},
    }));
    static_cast<void>(document.apply(SetParameterCommand{
        .expected_revision = RevisionId{2},
        .parameter_id = "width",
        .value = -1.0,
    }));
    return document;
}

void bytes_round_trip_failed_evaluation_state()
{
    TestEvaluator evaluator;
    const auto document = make_failed_document(evaluator);
    const auto encoded = encode_document_checkpoint(document.snapshot());
    auto restored = Document::restore(decode_document_checkpoint(encoded), evaluator);

    check(restored.semantic_head_revision_id() == RevisionId{3}, "codec preserves semantic head");
    check(restored.evaluated_revision_id() == RevisionId{2}, "codec preserves last good revision");
    check(!restored.current_geometry().available(), "codec preserves geometry denial");
    check(restored.command_journal().size() == 2, "codec preserves command journal");
    check(restored.semantic_state().at("width") == -1.0, "codec preserves failed edit");
    check(restored.semantic_state().at("height") == 4.0, "codec preserves transaction state");
}

void corruption_and_limits_are_rejected()
{
    TestEvaluator evaluator;
    const auto document = make_failed_document(evaluator);
    auto corrupted = encode_document_checkpoint(document.snapshot());
    corrupted.back() ^= std::byte{0x01};
    check_format_error(
        [&] { static_cast<void>(decode_document_checkpoint(corrupted)); },
        "checksum rejects corrupted payload");

    auto truncated = encode_document_checkpoint(document.snapshot());
    truncated.pop_back();
    check_format_error(
        [&] { static_cast<void>(decode_document_checkpoint(truncated)); },
        "payload length rejects truncation");

    const auto encoded = encode_document_checkpoint(document.snapshot());
    check_format_error(
        [&] {
            static_cast<void>(decode_document_checkpoint(
                encoded,
                CheckpointDecodeLimits{
                    .max_payload_bytes = 1,
                    .max_collection_entries = 100,
                    .max_string_bytes = 100,
                }));
        },
        "configured payload limit is enforced");
}

void atomic_file_replacement_reopens_latest_state()
{
    TestEvaluator evaluator;
    auto document = make_failed_document(evaluator);
    TemporaryDirectory directory;
    const auto checkpoint_path = directory.path() / "document.chk";

    save_document_checkpoint_atomic(checkpoint_path, document.snapshot());
    auto failed_reopen = Document::restore(
        load_document_checkpoint(checkpoint_path), evaluator);
    check(!failed_reopen.current_geometry().available(), "file preserves failed evaluation");

    const auto repaired = document.apply(SetParameterCommand{
        .expected_revision = RevisionId{3},
        .parameter_id = "width",
        .value = 5.0,
    });
    check(repaired.evaluation_succeeded, "repair succeeds before replacement save");

    save_document_checkpoint_atomic(checkpoint_path, document.snapshot());
    auto repaired_reopen = Document::restore(
        load_document_checkpoint(checkpoint_path), evaluator);
    check(repaired_reopen.semantic_head_revision_id() == RevisionId{4}, "replacement is latest");
    check(repaired_reopen.current_geometry().available(), "replacement preserves repaired geometry");

    auto file_count = std::size_t{0};
    for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator(directory.path())) {
        ++file_count;
    }
    check(file_count == 1, "successful replacement leaves no temporary file");
}

} // namespace

int main()
{
    bytes_round_trip_failed_evaluation_state();
    corruption_and_limits_are_rejected();
    atomic_file_replacement_reopens_latest_state();

    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed\n";
        return 1;
    }

    std::cout << "persistence checkpoint tests passed\n";
    return 0;
}
