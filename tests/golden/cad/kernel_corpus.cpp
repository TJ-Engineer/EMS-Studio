// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#include <oep/test/kernel_corpus.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace oep::test {
namespace {

using namespace modeler;

constexpr double tolerance = 1.0e-8;

class CaseCheck {
public:
    void require(const bool condition, std::string message)
    {
        if (!condition && passed_) {
            passed_ = false;
            detail_ = std::move(message);
        }
    }

    [[nodiscard]] bool passed() const noexcept { return passed_; }
    [[nodiscard]] std::string detail() const
    {
        return passed_ ? "passed" : detail_;
    }

private:
    bool passed_{true};
    std::string detail_;
};

[[nodiscard]] PlanarProfile rectangle_profile(
    const double width,
    const double height,
    const Point3d origin = {})
{
    return PlanarProfile{
        .frame = PlaneFrame{
            .origin = origin,
            .normal = Vector3d{0.0, 0.0, 1.0},
            .x_direction = Vector3d{1.0, 0.0, 0.0},
        },
        .loops = {ProfileLoop{
            .vertices = {
                Point2d{0.0, 0.0},
                Point2d{width, 0.0},
                Point2d{width, height},
                Point2d{0.0, height},
            },
            .is_hole = false,
        }},
    };
}

[[nodiscard]] ExtrudeRequest box_request(
    const double width,
    const double height,
    const double depth,
    const Point3d origin = {})
{
    return ExtrudeRequest{
        .profile = rectangle_profile(width, height, origin),
        .distance = depth,
    };
}

[[nodiscard]] bool nearly_equal(const double left, const double right)
{
    return std::abs(left - right) <= tolerance * std::max({1.0, std::abs(left), std::abs(right)});
}

void append_case(
    KernelCorpusReport& report,
    std::string id,
    std::string category,
    const CaseCheck& check)
{
    report.cases.push_back(GoldenCaseResult{
        .case_id = std::move(id),
        .category = std::move(category),
        .passed = check.passed(),
        .detail = check.detail(),
    });
}

void valid_extrusion_case(GeometryKernel& kernel, KernelCorpusReport& report)
{
    CaseCheck check;
    const CancellationToken cancellation;
    const auto result = kernel.extrude(box_request(10.0, 5.0, 2.0), cancellation);
    check.require(result.succeeded(), "valid rectangle extrusion failed");
    if (result.succeeded()) {
        const auto summary = kernel.summarize(*result.body, cancellation);
        check.require(summary.succeeded(), "extruded body could not be summarized");
        if (summary.succeeded()) {
            check.require(nearly_equal(summary.summary->volume, 100.0), "extrusion volume differs");
            check.require(summary.summary->solid_count == 1, "extrusion is not one solid");
            check.require(summary.summary->face_count >= 6, "extrusion has too few faces");
            check.require(
                nearly_equal(summary.summary->bounds.maximum.z, 2.0),
                "extrusion bounds differ");
        }
        check.require(
            generated_topology(result.provenance, "extrude.cap.end") != nullptr,
            "extrusion omitted end-cap provenance");
        check.require(
            generated_topology(result.provenance, "extrude.edge.0") != nullptr,
            "extrusion omitted edge provenance");
    }
    append_case(report, "extrude-rectangular-prism", "valid", check);
}

void degenerate_profile_case(GeometryKernel& kernel, KernelCorpusReport& report)
{
    CaseCheck check;
    const CancellationToken cancellation;
    const auto result = kernel.extrude(box_request(0.0, 5.0, 2.0), cancellation);
    check.require(!result.succeeded(), "zero-area profile unexpectedly succeeded");
    check.require(
        first_diagnostic(result.diagnostics, GeometryErrorCode::degenerate_geometry) != nullptr,
        "degenerate profile did not return a typed geometry diagnostic");
    append_case(report, "extrude-zero-area-profile", "degenerate", check);
}

void topology_changing_cut_case(GeometryKernel& kernel, KernelCorpusReport& report)
{
    CaseCheck check;
    const CancellationToken cancellation;
    const auto target = kernel.extrude(box_request(10.0, 8.0, 4.0), cancellation);
    const auto tool = kernel.extrude(
        box_request(2.0, 2.0, 6.0, Point3d{4.0, 3.0, -1.0}), cancellation);
    check.require(target.succeeded() && tool.succeeded(), "cut fixture construction failed");
    if (target.succeeded() && tool.succeeded()) {
        const auto before = kernel.summarize(*target.body, cancellation);
        const auto cut = kernel.cut(
            BooleanCutRequest{.target = *target.body, .tool = *tool.body}, cancellation);
        check.require(cut.succeeded(), "through cut failed");
        if (before.succeeded() && cut.succeeded()) {
            const auto after = kernel.summarize(*cut.body, cancellation);
            const auto original_after = kernel.summarize(*target.body, cancellation);
            check.require(after.succeeded(), "cut body could not be summarized");
            if (after.succeeded()) {
                check.require(
                    nearly_equal(after.summary->volume, 304.0),
                    "through-cut volume differs");
                check.require(
                    after.summary->face_count > before.summary->face_count,
                    "cut did not change topology counts");
            }
            check.require(
                original_after.succeeded()
                    && original_after.summary->content_hash == before.summary->content_hash,
                "cut mutated its input body");
            check.require(!cut.provenance.generated.empty(), "cut omitted generated provenance");
            check.require(!cut.provenance.modified.empty(), "cut omitted modified provenance");
        }
    }
    append_case(report, "cut-through-rectangular-prism", "topology-changing", check);
}

void fillet_and_failure_case(GeometryKernel& kernel, KernelCorpusReport& report)
{
    CaseCheck check;
    const CancellationToken cancellation;
    const auto base = kernel.extrude(box_request(10.0, 8.0, 4.0), cancellation);
    check.require(base.succeeded(), "fillet fixture construction failed");
    if (base.succeeded()) {
        const auto* edge = generated_topology(base.provenance, "extrude.edge.0");
        check.require(edge != nullptr, "fillet fixture has no selectable edge");
        if (edge != nullptr) {
            const auto valid = kernel.fillet(
                FilletRequest{
                    .body = *base.body,
                    .edges = {edge->result},
                    .radius = 1.0,
                },
                cancellation);
            check.require(valid.succeeded(), "valid fillet failed");
            check.require(
                !valid.provenance.generated.empty() && !valid.provenance.modified.empty(),
                "fillet omitted topology provenance");

            const auto invalid = kernel.fillet(
                FilletRequest{
                    .body = *base.body,
                    .edges = {edge->result},
                    .radius = 3.0,
                },
                cancellation);
            check.require(!invalid.succeeded(), "oversized fillet unexpectedly succeeded");
            check.require(
                first_diagnostic(
                    invalid.diagnostics, GeometryErrorCode::degenerate_geometry)
                    != nullptr,
                "oversized fillet did not return a typed failure");
        }
    }
    append_case(report, "fillet-valid-and-oversized", "topology-changing", check);
}

void linear_pattern_case(GeometryKernel& kernel, KernelCorpusReport& report)
{
    CaseCheck check;
    const CancellationToken cancellation;
    const auto base = kernel.extrude(box_request(1.0, 1.0, 1.0), cancellation);
    check.require(base.succeeded(), "pattern fixture construction failed");
    if (base.succeeded()) {
        const auto pattern = kernel.linear_pattern(
            LinearPatternRequest{
                .body = *base.body,
                .direction = Vector3d{1.0, 0.0, 0.0},
                .instance_count = 3,
                .spacing = 2.0,
                .fuse = false,
            },
            cancellation);
        check.require(pattern.succeeded(), "linear pattern failed");
        if (pattern.succeeded()) {
            const auto summary = kernel.summarize(*pattern.body, cancellation);
            check.require(summary.succeeded(), "pattern body could not be summarized");
            if (summary.succeeded()) {
                check.require(summary.summary->solid_count == 3, "pattern solid count differs");
                check.require(nearly_equal(summary.summary->volume, 3.0), "pattern volume differs");
                check.require(
                    nearly_equal(summary.summary->bounds.maximum.x, 5.0),
                    "pattern bounds differ");
            }
        }
    }
    append_case(report, "linear-pattern-three-instances", "valid", check);
}

void attached_sketch_plane_case(GeometryKernel& kernel, KernelCorpusReport& report)
{
    CaseCheck check;
    const CancellationToken cancellation;
    const auto base = kernel.extrude(
        box_request(4.0, 3.0, 2.0, Point3d{1.0, 2.0, 3.0}), cancellation);
    check.require(base.succeeded(), "attached-sketch fixture construction failed");
    if (base.succeeded()) {
        const auto* face = generated_topology(base.provenance, "extrude.cap.end");
        check.require(face != nullptr, "end-cap reference is missing");
        if (face != nullptr) {
            const auto plane = kernel.planar_face(
                PlanarFaceRequest{.face = face->result}, cancellation);
            check.require(plane.succeeded(), "planar face could not provide a sketch frame");
            if (plane.succeeded()) {
                check.require(nearly_equal(plane.frame->origin.x, 1.0), "sketch frame X differs");
                check.require(nearly_equal(plane.frame->origin.y, 2.0), "sketch frame Y differs");
                check.require(nearly_equal(plane.frame->origin.z, 5.0), "sketch frame Z differs");
                check.require(nearly_equal(plane.frame->normal.z, 1.0), "sketch frame normal differs");
            }
        }
    }
    append_case(report, "resolve-extrude-end-cap-plane", "attached-sketch", check);
}

void parameter_sweep_case(GeometryKernel& kernel, KernelCorpusReport& report)
{
    CaseCheck check;
    const CancellationToken cancellation;
    const std::vector<double> distances{0.5, 1.0, 2.0};
    std::vector<BodySummary> summaries;
    for (const auto distance : distances) {
        const auto result = kernel.extrude(box_request(2.0, 3.0, distance), cancellation);
        check.require(result.succeeded(), "parameter-sweep extrusion failed");
        if (result.succeeded()) {
            const auto summary = kernel.summarize(*result.body, cancellation);
            check.require(summary.succeeded(), "parameter-sweep summary failed");
            if (summary.succeeded()) {
                summaries.push_back(*summary.summary);
            }
        }
    }
    if (summaries.size() == distances.size()) {
        check.require(
            summaries[0].volume < summaries[1].volume
                && summaries[1].volume < summaries[2].volume,
            "parameter sweep is not monotonic");
        check.require(nearly_equal(summaries[0].volume, 3.0), "first sweep volume differs");
        check.require(nearly_equal(summaries[2].volume, 12.0), "last sweep volume differs");

        const auto repeat = kernel.extrude(box_request(2.0, 3.0, 1.0), cancellation);
        check.require(repeat.succeeded(), "repeat extrusion failed");
        if (repeat.succeeded()) {
            const auto repeated_summary = kernel.summarize(*repeat.body, cancellation);
            check.require(
                repeated_summary.succeeded()
                    && repeated_summary.summary->content_hash == summaries[1].content_hash,
                "identical input did not produce a stable content hash");
        }
    }
    append_case(report, "extrude-depth-parameter-sweep", "parameter-sweep", check);
}

void healing_and_snapshot_case(GeometryKernel& kernel, KernelCorpusReport& report)
{
    CaseCheck check;
    const CancellationToken cancellation;
    const auto base = kernel.extrude(box_request(4.0, 3.0, 2.0), cancellation);
    check.require(base.succeeded(), "snapshot fixture construction failed");
    if (base.succeeded()) {
        const auto before = kernel.summarize(*base.body, cancellation);
        const auto healed = kernel.heal(
            HealRequest{
                .body = *base.body,
                .tolerance = 1.0e-7,
                .maximum_tolerance = 1.0e-3,
            },
            cancellation);
        check.require(healed.succeeded(), "healing a valid exact body failed");
        if (before.succeeded() && healed.succeeded()) {
            const auto after = kernel.summarize(*healed.body, cancellation);
            check.require(
                after.succeeded() && nearly_equal(after.summary->volume, before.summary->volume),
                "healing changed valid-body volume");

            const auto first_snapshot = kernel.snapshot(*healed.body, cancellation);
            const auto second_snapshot = kernel.snapshot(*healed.body, cancellation);
            check.require(
                first_snapshot.succeeded() && second_snapshot.succeeded(),
                "exact-body snapshot failed");
            if (first_snapshot.succeeded() && second_snapshot.succeeded()) {
                check.require(
                    !first_snapshot.snapshot->payload.empty(),
                    "exact-body snapshot payload is empty");
                check.require(
                    first_snapshot.snapshot->payload == second_snapshot.snapshot->payload,
                    "repeated exact-body snapshots are not deterministic");
                const auto restored = kernel.restore_snapshot(
                    *first_snapshot.snapshot, cancellation);
                check.require(restored.succeeded(), "exact-body snapshot did not restore");
                if (restored.succeeded()) {
                    const auto restored_summary = kernel.summarize(*restored.body, cancellation);
                    check.require(
                        restored_summary.succeeded()
                            && restored_summary.summary->content_hash
                                == first_snapshot.snapshot->content_hash,
                        "restored exact body differs from its snapshot");
                }
            }
        }
    }
    append_case(report, "heal-and-exact-snapshot-roundtrip", "recovery", check);
}

void cancellation_case(GeometryKernel& kernel, KernelCorpusReport& report)
{
    CaseCheck check;
    CancellationSource source;
    const auto cancellation = source.token();
    source.request_cancellation();
    const auto result = kernel.extrude(box_request(2.0, 3.0, 1.0), cancellation);
    check.require(!result.succeeded(), "cancelled operation unexpectedly produced a body");
    check.require(
        first_diagnostic(result.diagnostics, GeometryErrorCode::cancelled) != nullptr,
        "cancelled operation did not return a typed diagnostic");
    append_case(report, "pre-cancelled-extrusion", "cancellation", check);
}

} // namespace

bool KernelCorpusReport::passed() const noexcept
{
    return std::ranges::all_of(
        cases, [](const GoldenCaseResult& result) { return result.passed; });
}

KernelCorpusReport run_kernel_corpus(GeometryKernel& kernel)
{
    KernelCorpusReport report{
        .kernel = kernel.identity(),
        .cases = {},
    };
    valid_extrusion_case(kernel, report);
    degenerate_profile_case(kernel, report);
    topology_changing_cut_case(kernel, report);
    fillet_and_failure_case(kernel, report);
    linear_pattern_case(kernel, report);
    attached_sketch_plane_case(kernel, report);
    parameter_sweep_case(kernel, report);
    healing_and_snapshot_case(kernel, report);
    cancellation_case(kernel, report);
    return report;
}

} // namespace oep::test
