// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

// Deterministic sketch constraints use stable entity IDs and parameter bindings.
// Solver classification distinguishes remaining freedom from contradictory
// constraints; callers must inspect the result before constructing exact geometry.

#pragma once

#include <compare>
#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace oep::sketch {

struct EntityId {
    std::string value;

    auto operator<=>(const EntityId&) const = default;
};

struct ConstraintId {
    std::string value;

    auto operator<=>(const ConstraintId&) const = default;
};

struct Point2d {
    double x{};
    double y{};

    bool operator==(const Point2d&) const = default;
};

struct PointEntity {
    EntityId id;
    Point2d initial_position;
};

struct LineEntity {
    EntityId id;
    EntityId start_point_id;
    EntityId end_point_id;
};

struct CircleEntity {
    EntityId id;
    EntityId center_point_id;
    double initial_radius{};
};

using Entity = std::variant<PointEntity, LineEntity, CircleEntity>;

struct CoincidentConstraint {
    ConstraintId id;
    EntityId first_point_id;
    EntityId second_point_id;
};

struct HorizontalConstraint {
    ConstraintId id;
    EntityId line_id;
};

struct VerticalConstraint {
    ConstraintId id;
    EntityId line_id;
};

struct DistanceConstraint {
    ConstraintId id;
    EntityId first_point_id;
    EntityId second_point_id;
    double distance{};
};

struct RadiusConstraint {
    ConstraintId id;
    EntityId circle_id;
    double radius{};
};

struct FixedPointConstraint {
    ConstraintId id;
    EntityId point_id;
    Point2d position;
};

using Constraint = std::variant<
    CoincidentConstraint,
    HorizontalConstraint,
    VerticalConstraint,
    DistanceConstraint,
    RadiusConstraint,
    FixedPointConstraint>;

struct Definition {
    std::vector<Entity> entities;
    std::vector<Constraint> constraints;
};

enum class SolveStatus {
    invalid,
    under_constrained,
    fully_constrained,
    over_constrained,
    inconsistent,
};

enum class DiagnosticCode {
    empty_id,
    duplicate_entity_id,
    duplicate_constraint_id,
    missing_entity,
    wrong_entity_kind,
    invalid_dimension,
    degenerate_entity,
    under_constrained,
    over_constrained,
    inconsistent_constraints,
    solver_did_not_converge,
};

struct Diagnostic {
    DiagnosticCode code{DiagnosticCode::solver_did_not_converge};
    std::string message;
    std::optional<EntityId> entity_id;
    std::optional<ConstraintId> constraint_id;
};

struct SolvedPoint {
    EntityId id;
    Point2d position;
};

struct SolvedCircle {
    EntityId id;
    EntityId center_point_id;
    double radius{};
};

struct SolveResult {
    SolveStatus status{SolveStatus::invalid};
    std::vector<SolvedPoint> points;
    std::vector<SolvedCircle> circles;
    std::size_t degrees_of_freedom{};
    std::size_t redundant_equations{};
    std::size_t iterations{};
    double maximum_residual{};
    std::string deterministic_signature;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool has_solution() const noexcept;
    [[nodiscard]] const SolvedPoint* find_point(const EntityId& id) const noexcept;
    [[nodiscard]] const SolvedCircle* find_circle(const EntityId& id) const noexcept;
};

class Solver {
public:
    [[nodiscard]] SolveResult solve(const Definition& definition) const;
};

} // namespace oep::sketch
