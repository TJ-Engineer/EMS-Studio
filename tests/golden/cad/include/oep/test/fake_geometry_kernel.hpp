#pragma once

#include <oep/modeler/kernel_adapter.hpp>

#include <cstdint>
#include <map>
#include <string>

namespace oep::test {

class FakeGeometryKernel final : public modeler::GeometryKernel {
public:
    [[nodiscard]] modeler::KernelIdentity identity() const override;
    [[nodiscard]] modeler::OperationResult extrude(
        const modeler::ExtrudeRequest& request,
        const modeler::CancellationToken& cancellation) override;
    [[nodiscard]] modeler::OperationResult cut(
        const modeler::BooleanCutRequest& request,
        const modeler::CancellationToken& cancellation) override;
    [[nodiscard]] modeler::OperationResult fillet(
        const modeler::FilletRequest& request,
        const modeler::CancellationToken& cancellation) override;
    [[nodiscard]] modeler::OperationResult linear_pattern(
        const modeler::LinearPatternRequest& request,
        const modeler::CancellationToken& cancellation) override;
    [[nodiscard]] modeler::OperationResult heal(
        const modeler::HealRequest& request,
        const modeler::CancellationToken& cancellation) override;
    [[nodiscard]] modeler::PlanarFaceResult planar_face(
        const modeler::PlanarFaceRequest& request,
        const modeler::CancellationToken& cancellation) const override;
    [[nodiscard]] modeler::BodySummaryResult summarize(
        modeler::ExactBodyHandle body,
        const modeler::CancellationToken& cancellation) const override;
    [[nodiscard]] modeler::TopologyInspectionResult inspect_topology(
        modeler::ExactBodyHandle body,
        const modeler::CancellationToken& cancellation) const override;
    [[nodiscard]] modeler::BodySnapshotResult snapshot(
        modeler::ExactBodyHandle body,
        const modeler::CancellationToken& cancellation) const override;
    [[nodiscard]] modeler::OperationResult restore_snapshot(
        const modeler::ExactBodySnapshot& snapshot,
        const modeler::CancellationToken& cancellation) override;

    struct BodyData {
        modeler::BodySummary summary;
        std::map<std::string, modeler::TopologyEntity, std::less<>> entities;
        std::map<std::string, modeler::PlaneFrame, std::less<>> face_frames;
    };

private:
    [[nodiscard]] const BodyData* body(modeler::ExactBodyHandle handle) const;

    std::map<std::uint64_t, BodyData> bodies_;
    std::uint64_t next_handle_{1};
};

} // namespace oep::test
