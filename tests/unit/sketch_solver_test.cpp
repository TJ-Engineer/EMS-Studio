#include <oep/sketch/sketch.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

namespace {

using namespace oep::sketch;

void check(const bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

[[nodiscard]] Definition rectangle(const double width, const double height)
{
    return Definition{
        .entities = {
            PointEntity{EntityId{"p0"}, Point2d{0.2, -0.1}},
            PointEntity{EntityId{"p1"}, Point2d{width + 0.3, 0.2}},
            PointEntity{EntityId{"p2"}, Point2d{width - 0.2, height + 0.3}},
            PointEntity{EntityId{"p3"}, Point2d{-0.1, height - 0.2}},
            LineEntity{EntityId{"l0"}, EntityId{"p0"}, EntityId{"p1"}},
            LineEntity{EntityId{"l1"}, EntityId{"p1"}, EntityId{"p2"}},
            LineEntity{EntityId{"l2"}, EntityId{"p2"}, EntityId{"p3"}},
            LineEntity{EntityId{"l3"}, EntityId{"p3"}, EntityId{"p0"}},
        },
        .constraints = {
            FixedPointConstraint{ConstraintId{"origin"}, EntityId{"p0"}, Point2d{0.0, 0.0}},
            HorizontalConstraint{ConstraintId{"horizontal-bottom"}, EntityId{"l0"}},
            VerticalConstraint{ConstraintId{"vertical-right"}, EntityId{"l1"}},
            HorizontalConstraint{ConstraintId{"horizontal-top"}, EntityId{"l2"}},
            VerticalConstraint{ConstraintId{"vertical-left"}, EntityId{"l3"}},
            DistanceConstraint{ConstraintId{"width"}, EntityId{"p0"}, EntityId{"p1"}, width},
            DistanceConstraint{ConstraintId{"height"}, EntityId{"p1"}, EntityId{"p2"}, height},
        },
    };
}

void classification_cases()
{
    Solver solver;

    auto under = rectangle(10.0, 6.0);
    under.constraints.pop_back();
    const auto under_result = solver.solve(under);
    check(under_result.status == SolveStatus::under_constrained, "under-constrained sketch classified");
    check(under_result.degrees_of_freedom == 1, "one missing dimension leaves one DOF");

    const auto fully = solver.solve(rectangle(10.0, 6.0));
    check(fully.status == SolveStatus::fully_constrained, "fully constrained sketch classified");
    check(fully.degrees_of_freedom == 0, "fully constrained sketch has no DOF");

    auto over = rectangle(10.0, 6.0);
    over.constraints.push_back(
        DistanceConstraint{ConstraintId{"width-copy"}, EntityId{"p0"}, EntityId{"p1"}, 10.0});
    const auto over_result = solver.solve(over);
    check(over_result.status == SolveStatus::over_constrained, "redundant sketch classified");
    check(over_result.redundant_equations == 1, "redundant equation counted");

    auto inconsistent = rectangle(10.0, 6.0);
    inconsistent.constraints.push_back(
        DistanceConstraint{ConstraintId{"contradiction"}, EntityId{"p0"}, EntityId{"p1"}, 12.0});
    const auto inconsistent_result = solver.solve(inconsistent);
    check(inconsistent_result.status == SolveStatus::inconsistent, "contradictory sketch classified");
    check(!inconsistent_result.has_solution(), "inconsistent sketch is unusable");
}

void parameter_change_preserves_ids()
{
    Solver solver;
    const auto initial = solver.solve(rectangle(10.0, 6.0));
    const auto changed = solver.solve(rectangle(14.0, 6.0));
    check(initial.status == SolveStatus::fully_constrained, "initial rectangle solves");
    check(changed.status == SolveStatus::fully_constrained, "changed rectangle solves");
    check(initial.points.size() == changed.points.size(), "point count remains stable");
    for (const auto& point : initial.points) {
        check(changed.find_point(point.id) != nullptr, "stable point ID survives parameter change");
    }
    const auto* p0 = changed.find_point(EntityId{"p0"});
    const auto* p1 = changed.find_point(EntityId{"p1"});
    check(p0 != nullptr && p1 != nullptr, "dimension endpoints remain addressable");
    check(std::abs(std::hypot(p1->position.x - p0->position.x, p1->position.y - p0->position.y) - 14.0)
              < 1.0e-8,
          "changed width is solved");

    const auto repeated = solver.solve(rectangle(14.0, 6.0));
    check(repeated.deterministic_signature == changed.deterministic_signature,
          "repeated solve is deterministic");
}

void invalid_input_is_typed()
{
    auto invalid = rectangle(10.0, 6.0);
    invalid.constraints.push_back(
        RadiusConstraint{ConstraintId{"bad-radius"}, EntityId{"missing-circle"}, -1.0});
    const auto result = Solver{}.solve(invalid);
    check(result.status == SolveStatus::invalid, "invalid references reject the sketch");
    check(result.diagnostics.size() >= 2, "invalid reference and dimension are diagnosed");
}

} // namespace

int main()
{
    classification_cases();
    parameter_change_preserves_ids();
    invalid_input_is_typed();
    std::cout << "sketch solver tests passed\n";
}
