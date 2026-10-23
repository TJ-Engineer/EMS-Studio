// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#pragma once
#include <oep/simulation_ir/simulation_ir.hpp>
#include <oep/persistence/checkpoint.hpp>
#include <filesystem>

namespace oep::simulation_ir {
// Internal project recovery bundles CAD history and optional ASIR atomically.
// Incomplete/stale drafts are preserved; preflight is a separate live-state gate.
struct ProjectCheckpoint {
    document::DocumentState document;
    std::string cad_document_id;
    std::optional<AntennaStudy> study;
    // Opaque nested solver-protocol bytes keep simulation_ir independent of a
    // particular worker. The desktop validates them before session activation.
    std::optional<std::vector<std::byte>> worker_recovery;
};
// Throws CheckpointFormatError on bounds, version, content, or identity failures.
// Document::restore and exact-kernel hydration remain separate activation gates.
[[nodiscard]] std::vector<std::byte> encode_project_checkpoint(const ProjectCheckpoint& project);
[[nodiscard]] ProjectCheckpoint decode_project_checkpoint(std::span<const std::byte> bytes);
void save_project_checkpoint_atomic(const std::filesystem::path& path, const ProjectCheckpoint& project);
[[nodiscard]] ProjectCheckpoint load_project_checkpoint(const std::filesystem::path& path);
} // namespace oep::simulation_ir
