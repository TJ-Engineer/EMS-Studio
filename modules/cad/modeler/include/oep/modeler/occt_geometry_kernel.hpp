// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <oep/modeler/kernel_adapter.hpp>

#include <memory>

namespace oep::modeler {

class OcctGeometryKernel final : public GeometryKernel {
public:
    OcctGeometryKernel();
    ~OcctGeometryKernel() override;

    OcctGeometryKernel(const OcctGeometryKernel&) = delete;
    OcctGeometryKernel& operator=(const OcctGeometryKernel&) = delete;
    OcctGeometryKernel(OcctGeometryKernel&&) noexcept;
    OcctGeometryKernel& operator=(OcctGeometryKernel&&) noexcept;

    [[nodiscard]] KernelIdentity identity() const override;
    [[nodiscard]] OperationResult extrude(
        const ExtrudeRequest& request,
        const CancellationToken& cancellation) override;
    [[nodiscard]] OperationResult cut(
        const BooleanCutRequest& request,
        const CancellationToken& cancellation) override;
    [[nodiscard]] OperationResult fillet(
        const FilletRequest& request,
        const CancellationToken& cancellation) override;
    [[nodiscard]] OperationResult linear_pattern(
        const LinearPatternRequest& request,
        const CancellationToken& cancellation) override;
    [[nodiscard]] OperationResult heal(
        const HealRequest& request,
        const CancellationToken& cancellation) override;
    [[nodiscard]] PlanarFaceResult planar_face(
        const PlanarFaceRequest& request,
        const CancellationToken& cancellation) const override;
    [[nodiscard]] BodySummaryResult summarize(
        ExactBodyHandle body,
        const CancellationToken& cancellation) const override;
    [[nodiscard]] TopologyInspectionResult inspect_topology(
        ExactBodyHandle body,
        const CancellationToken& cancellation) const override;
    [[nodiscard]] BodySnapshotResult snapshot(
        ExactBodyHandle body,
        const CancellationToken& cancellation) const override;
    [[nodiscard]] OperationResult restore_snapshot(
        const ExactBodySnapshot& snapshot,
        const CancellationToken& cancellation) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace oep::modeler
