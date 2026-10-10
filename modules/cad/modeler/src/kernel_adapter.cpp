#include <oep/modeler/kernel_adapter.hpp>

#include <algorithm>
#include <utility>

namespace oep::modeler {

CancellationToken::CancellationToken()
    : state_(std::make_shared<std::atomic_bool>(false))
{
}

CancellationToken::CancellationToken(std::shared_ptr<std::atomic_bool> state)
    : state_(std::move(state))
{
}

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
