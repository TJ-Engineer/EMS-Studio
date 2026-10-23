// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

// Kernel-neutral modeling boundary: handles identify adapter-owned exact bodies,
// not persistent document entities. Operation diagnostics and topology provenance
// allow callers to distinguish geometric failure from reference reconciliation.

#include <oep/modeler/kernel_adapter.hpp>

#include <algorithm>
#include <utility>

namespace oep::modeler {

TessellationResult GeometryKernel::tessellate(
    const TessellationRequest& request,
    const CancellationToken& cancellation) const
{
    if (cancellation.is_cancellation_requested()) {
        return {
            .vertices = {}, .triangles = {}, .edge_polylines = {}, .bounds = {},
            .diagnostics = {{
                .code = GeometryErrorCode::cancelled,
                .message = "display tessellation was cancelled",
                .entity = std::nullopt,
                .repair_hint = std::nullopt,
            }},
        };
    }
    return {
        .vertices = {}, .triangles = {}, .edge_polylines = {}, .bounds = {},
        .diagnostics = {{
            .code = GeometryErrorCode::unsupported_operation,
            .message = "geometry kernel does not provide display tessellation",
            .entity = TopologyEntity{request.body, {}, TopologyKind::solid},
            .repair_hint = "use a kernel adapter with display tessellation support",
        }},
    };
}

CancellationToken::CancellationToken()
    : state_(std::make_shared<std::atomic_bool>(false))
{
}

CancellationToken::CancellationToken(std::shared_ptr<std::atomic_bool> state)
    : state_(std::move(state))
{
}

// The atomic flag communicates cancellation only, not associated payload
// data, so relaxed ordering suffices. Tokens share the source flag lifetime.
bool CancellationToken::is_cancellation_requested() const noexcept
{
    return state_->load(std::memory_order_relaxed);
}

CancellationSource::CancellationSource()
    : state_(std::make_shared<std::atomic_bool>(false))
{
}

CancellationToken CancellationSource::token() const
{
    return CancellationToken(state_);
}

void CancellationSource::request_cancellation() noexcept
{
    state_->store(true, std::memory_order_relaxed);
}

const GeometryDiagnostic* first_diagnostic(
    const std::span<const GeometryDiagnostic> diagnostics,
    const GeometryErrorCode code) noexcept
{
    const auto found = std::ranges::find(diagnostics, code, &GeometryDiagnostic::code);
    return found == diagnostics.end() ? nullptr : &*found;
}

const GeneratedTopology* generated_topology(
    const TopologyProvenance& provenance,
    const std::string_view semantic_role) noexcept
{
    const auto found = std::ranges::find_if(
        provenance.generated,
        [semantic_role](const GeneratedTopology& topology) {
            return topology.semantic_role == semantic_role;
        });
    return found == provenance.generated.end() ? nullptr : &*found;
}

} // namespace oep::modeler
