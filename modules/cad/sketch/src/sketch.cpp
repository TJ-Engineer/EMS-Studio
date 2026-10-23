// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

// Deterministic sketch constraints use stable entity IDs and parameter bindings.
// Solver classification distinguishes remaining freedom from contradictory
// constraints; callers must inspect the result before constructing exact geometry.

#include <oep/sketch/sketch.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>

namespace oep::sketch {
namespace {

constexpr double solve_tolerance = 1.0e-9;
constexpr double rank_tolerance = 1.0e-8;
constexpr std::size_t maximum_iterations = 80;

struct IndexedPoint {
    PointEntity entity;
    std::size_t x_index{};
    std::size_t y_index{};
};

struct IndexedCircle {
    CircleEntity entity;
    std::size_t radius_index{};
};

struct Model {
    std::map<std::string, IndexedPoint, std::less<>> points;
    std::map<std::string, LineEntity, std::less<>> lines;
    std::map<std::string, IndexedCircle, std::less<>> circles;
    std::vector<double> initial_values;
    std::vector<Diagnostic> diagnostics;
};

struct Linearization {
    std::vector<double> residuals;
    std::vector<std::vector<double>> jacobian;
};

[[nodiscard]] Diagnostic diagnostic(
    const DiagnosticCode code,
    std::string message,
    std::optional<EntityId> entity_id = std::nullopt,
    std::optional<ConstraintId> constraint_id = std::nullopt)
{
    return Diagnostic{
        .code = code,
        .message = std::move(message),
        .entity_id = std::move(entity_id),
        .constraint_id = std::move(constraint_id),
    };
}

[[nodiscard]] bool finite(const Point2d point)
{
    return std::isfinite(point.x) && std::isfinite(point.y);
}

template <typename Value>
[[nodiscard]] const auto& stable_id(const Value& value)
{
    return value.id;
}

[[nodiscard]] Model index_model(const Definition& definition)
{
    Model model;
    std::set<std::string, std::less<>> entity_ids;
    for (const auto& entity : definition.entities) {
        std::visit(
            [&](const auto& value) {
                const auto& id = stable_id(value);
                if (id.value.empty()) {
                    model.diagnostics.push_back(diagnostic(
                        DiagnosticCode::empty_id, "sketch entity IDs must not be empty"));
                    return;
                }
                if (!entity_ids.insert(id.value).second) {
                    model.diagnostics.push_back(diagnostic(
                        DiagnosticCode::duplicate_entity_id,
                        "sketch entity IDs must be unique",
                        id));
                    return;
                }

                using Value = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Value, PointEntity>) {
                    if (!finite(value.initial_position)) {
                        model.diagnostics.push_back(diagnostic(
                            DiagnosticCode::degenerate_entity,
                            "point coordinates must be finite",
                            id));
                        return;
                    }
                    const auto offset = model.initial_values.size();
                    model.initial_values.push_back(value.initial_position.x);
                    model.initial_values.push_back(value.initial_position.y);
                    model.points.emplace(
                        id.value,
                        IndexedPoint{.entity = value, .x_index = offset, .y_index = offset + 1});
                } else if constexpr (std::is_same_v<Value, LineEntity>) {
                    model.lines.emplace(id.value, value);
                } else {
                    if (!std::isfinite(value.initial_radius) || value.initial_radius <= 0.0) {
                        model.diagnostics.push_back(diagnostic(
                            DiagnosticCode::degenerate_entity,
                            "circle radius must be positive and finite",
                            id));
                        return;
                    }
                    const auto radius_index = model.initial_values.size();
                    model.initial_values.push_back(value.initial_radius);
                    model.circles.emplace(
                        id.value,
                        IndexedCircle{.entity = value, .radius_index = radius_index});
                }
            },
            entity);
    }

    const auto require_point = [&](const EntityId& id, const ConstraintId* constraint_id) {
        if (!model.points.contains(id.value)) {
            model.diagnostics.push_back(diagnostic(
                entity_ids.contains(id.value) ? DiagnosticCode::wrong_entity_kind
                                              : DiagnosticCode::missing_entity,
                "constraint must reference a point",
                id,
                constraint_id == nullptr ? std::nullopt
                                         : std::optional<ConstraintId>{*constraint_id}));
        }
    };
    for (const auto& [unused_id, line] : model.lines) {
        static_cast<void>(unused_id);
        require_point(line.start_point_id, nullptr);
        require_point(line.end_point_id, nullptr);
        if (line.start_point_id == line.end_point_id) {
            model.diagnostics.push_back(diagnostic(
                DiagnosticCode::degenerate_entity,
                "line endpoints must have different stable IDs",
                line.id));
        }
    }
    for (const auto& [unused_id, circle] : model.circles) {
        static_cast<void>(unused_id);
        require_point(circle.entity.center_point_id, nullptr);
    }

    std::set<std::string, std::less<>> constraint_ids;
    for (const auto& constraint : definition.constraints) {
        std::visit(
            [&](const auto& value) {
                if (value.id.value.empty()) {
                    model.diagnostics.push_back(diagnostic(
                        DiagnosticCode::empty_id,
                        "sketch constraint IDs must not be empty"));
                    return;
                }
                if (!constraint_ids.insert(value.id.value).second) {
                    model.diagnostics.push_back(diagnostic(
                        DiagnosticCode::duplicate_constraint_id,
                        "sketch constraint IDs must be unique",
                        std::nullopt,
                        value.id));
                    return;
                }

                using Value = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Value, CoincidentConstraint>
                              || std::is_same_v<Value, DistanceConstraint>) {
                    require_point(value.first_point_id, &value.id);
                    require_point(value.second_point_id, &value.id);
                } else if constexpr (std::is_same_v<Value, FixedPointConstraint>) {
                    require_point(value.point_id, &value.id);
                    if (!finite(value.position)) {
                        model.diagnostics.push_back(diagnostic(
                            DiagnosticCode::invalid_dimension,
                            "fixed point coordinates must be finite",
                            value.point_id,
                            value.id));
                    }
                } else if constexpr (std::is_same_v<Value, HorizontalConstraint>
                                     || std::is_same_v<Value, VerticalConstraint>) {
                    if (!model.lines.contains(value.line_id.value)) {
                        model.diagnostics.push_back(diagnostic(
                            entity_ids.contains(value.line_id.value)
                                ? DiagnosticCode::wrong_entity_kind
                                : DiagnosticCode::missing_entity,
                            "horizontal and vertical constraints must reference a line",
                            value.line_id,
                            value.id));
                    }
                } else {
                    if (!model.circles.contains(value.circle_id.value)) {
                        model.diagnostics.push_back(diagnostic(
                            entity_ids.contains(value.circle_id.value)
                                ? DiagnosticCode::wrong_entity_kind
                                : DiagnosticCode::missing_entity,
                            "radius constraints must reference a circle",
                            value.circle_id,
                            value.id));
                    }
                }

                if constexpr (std::is_same_v<Value, DistanceConstraint>) {
                    if (!std::isfinite(value.distance) || value.distance <= 0.0) {
                        model.diagnostics.push_back(diagnostic(
                            DiagnosticCode::invalid_dimension,
                            "distance dimensions must be positive and finite",
                            std::nullopt,
                            value.id));
                    }
                } else if constexpr (std::is_same_v<Value, RadiusConstraint>) {
                    if (!std::isfinite(value.radius) || value.radius <= 0.0) {
                        model.diagnostics.push_back(diagnostic(
                            DiagnosticCode::invalid_dimension,
                            "radius dimensions must be positive and finite",
                            value.circle_id,
                            value.id));
                    }
                }
            },
            constraint);
    }
    return model;
}

void add_row(
    Linearization& result,
    const std::size_t variable_count,
    const double residual,
    std::initializer_list<std::pair<std::size_t, double>> derivatives)
{
    result.residuals.push_back(residual);
    result.jacobian.emplace_back(variable_count, 0.0);
    for (const auto& [index, value] : derivatives) {
        result.jacobian.back()[index] += value;
    }
}

[[nodiscard]] Linearization linearize(
    const Definition& definition,
    const Model& model,
    const std::vector<double>& values)
{
    Linearization result;
    for (const auto& constraint : definition.constraints) {
        std::visit(
            [&](const auto& value) {
                using Value = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Value, CoincidentConstraint>) {
                    const auto& first = model.points.at(value.first_point_id.value);
                    const auto& second = model.points.at(value.second_point_id.value);
                    add_row(result, values.size(), values[first.x_index] - values[second.x_index],
                            {{first.x_index, 1.0}, {second.x_index, -1.0}});
                    add_row(result, values.size(), values[first.y_index] - values[second.y_index],
                            {{first.y_index, 1.0}, {second.y_index, -1.0}});
                } else if constexpr (std::is_same_v<Value, HorizontalConstraint>
                                     || std::is_same_v<Value, VerticalConstraint>) {
                    const auto& line = model.lines.at(value.line_id.value);
                    const auto& first = model.points.at(line.start_point_id.value);
                    const auto& second = model.points.at(line.end_point_id.value);
                    if constexpr (std::is_same_v<Value, HorizontalConstraint>) {
                        add_row(result, values.size(), values[second.y_index] - values[first.y_index],
                                {{second.y_index, 1.0}, {first.y_index, -1.0}});
                    } else {
                        add_row(result, values.size(), values[second.x_index] - values[first.x_index],
                                {{second.x_index, 1.0}, {first.x_index, -1.0}});
                    }
                } else if constexpr (std::is_same_v<Value, DistanceConstraint>) {
                    const auto& first = model.points.at(value.first_point_id.value);
                    const auto& second = model.points.at(value.second_point_id.value);
                    const auto dx = values[second.x_index] - values[first.x_index];
                    const auto dy = values[second.y_index] - values[first.y_index];
                    const auto length = std::hypot(dx, dy);
                    const auto inverse = length > solve_tolerance ? 1.0 / length : 0.0;
                    add_row(result, values.size(), length - value.distance,
                            {{first.x_index, -dx * inverse},
                             {first.y_index, -dy * inverse},
                             {second.x_index, dx * inverse},
                             {second.y_index, dy * inverse}});
                } else if constexpr (std::is_same_v<Value, RadiusConstraint>) {
                    const auto& circle = model.circles.at(value.circle_id.value);
                    add_row(result, values.size(), values[circle.radius_index] - value.radius,
                            {{circle.radius_index, 1.0}});
                } else {
                    const auto& point = model.points.at(value.point_id.value);
                    add_row(result, values.size(), values[point.x_index] - value.position.x,
                            {{point.x_index, 1.0}});
                    add_row(result, values.size(), values[point.y_index] - value.position.y,
                            {{point.y_index, 1.0}});
                }
            },
            constraint);
    }
    return result;
}

[[nodiscard]] double squared_norm(const std::vector<double>& values)
{
    return std::inner_product(values.begin(), values.end(), values.begin(), 0.0);
}

[[nodiscard]] std::optional<std::vector<double>> solve_linear_system(
    std::vector<std::vector<double>> matrix,
    std::vector<double> right_hand_side)
{
    const auto size = right_hand_side.size();
    for (std::size_t column = 0; column < size; ++column) {
        auto pivot = column;
        for (auto row = column + 1; row < size; ++row) {
            if (std::abs(matrix[row][column]) > std::abs(matrix[pivot][column])) {
                pivot = row;
            }
        }
        if (std::abs(matrix[pivot][column]) < std::numeric_limits<double>::epsilon()) {
            return std::nullopt;
        }
        std::swap(matrix[column], matrix[pivot]);
        std::swap(right_hand_side[column], right_hand_side[pivot]);
        const auto divisor = matrix[column][column];
        for (auto entry = column; entry < size; ++entry) {
            matrix[column][entry] /= divisor;
        }
        right_hand_side[column] /= divisor;
        for (std::size_t row = 0; row < size; ++row) {
            if (row == column) {
                continue;
            }
            const auto factor = matrix[row][column];
            for (auto entry = column; entry < size; ++entry) {
                matrix[row][entry] -= factor * matrix[column][entry];
            }
            right_hand_side[row] -= factor * right_hand_side[column];
        }
    }
    return right_hand_side;
}

[[nodiscard]] std::size_t matrix_rank(std::vector<std::vector<double>> matrix)
{
    if (matrix.empty() || matrix.front().empty()) {
        return 0;
    }
    const auto row_count = matrix.size();
    const auto column_count = matrix.front().size();
    auto rank = std::size_t{0};
    for (std::size_t column = 0; column < column_count && rank < row_count; ++column) {
        auto pivot = rank;
        for (auto row = rank + 1; row < row_count; ++row) {
            if (std::abs(matrix[row][column]) > std::abs(matrix[pivot][column])) {
                pivot = row;
            }
        }
        if (std::abs(matrix[pivot][column]) <= rank_tolerance) {
            continue;
        }
        std::swap(matrix[rank], matrix[pivot]);
        const auto divisor = matrix[rank][column];
        for (auto entry = column; entry < column_count; ++entry) {
            matrix[rank][entry] /= divisor;
        }
        for (std::size_t row = 0; row < row_count; ++row) {
            if (row == rank) {
                continue;
            }
            const auto factor = matrix[row][column];
            for (auto entry = column; entry < column_count; ++entry) {
                matrix[row][entry] -= factor * matrix[rank][entry];
            }
        }
        ++rank;
    }
    return rank;
}

[[nodiscard]] std::string signature(const SolveResult& result)
{
    std::ostringstream stream;
    stream << std::setprecision(17) << "sketch-v1";
    for (const auto& point : result.points) {
        stream << ";p:" << point.id.value << ':' << point.position.x << ':' << point.position.y;
    }
    for (const auto& circle : result.circles) {
        stream << ";c:" << circle.id.value << ':' << circle.center_point_id.value << ':'
               << circle.radius;
    }
    stream << ";status:" << static_cast<int>(result.status) << ";dof:"
           << result.degrees_of_freedom << ";redundant:" << result.redundant_equations;
    return stream.str();
}

} // namespace

bool SolveResult::has_solution() const noexcept
{
    return status == SolveStatus::under_constrained
        || status == SolveStatus::fully_constrained
        || status == SolveStatus::over_constrained;
}

const SolvedPoint* SolveResult::find_point(const EntityId& id) const noexcept
{
    const auto found = std::ranges::find_if(
        points, [&id](const SolvedPoint& point) { return point.id == id; });
    return found == points.end() ? nullptr : &*found;
}

const SolvedCircle* SolveResult::find_circle(const EntityId& id) const noexcept
{
    const auto found = std::ranges::find_if(
        circles, [&id](const SolvedCircle& circle) { return circle.id == id; });
    return found == circles.end() ? nullptr : &*found;
}

SolveResult Solver::solve(const Definition& definition) const
{
    auto model = index_model(definition);
    if (!model.diagnostics.empty() || model.initial_values.empty()) {
        if (model.initial_values.empty() && model.diagnostics.empty()) {
            model.diagnostics.push_back(diagnostic(
                DiagnosticCode::degenerate_entity,
                "a sketch requires at least one point or circle variable"));
        }
        SolveResult result;
        result.status = SolveStatus::invalid;
        result.diagnostics = std::move(model.diagnostics);
        return result;
    }

    auto values = model.initial_values;
    auto iterations = std::size_t{0};
    auto damping = 1.0e-9;
    for (; iterations < maximum_iterations; ++iterations) {
        const auto current = linearize(definition, model, values);
        if (current.residuals.empty()
            || std::ranges::all_of(current.residuals, [](const double residual) {
                   return std::abs(residual) <= solve_tolerance;
               })) {
            break;
        }

        // Damped normal equations regularize rank-deficient constraint systems.
        // Backtracking below accepts only a residual decrease; convergence alone
        // does not establish that all degrees of freedom are constrained.
        const auto variable_count = values.size();
        std::vector normal(variable_count, std::vector<double>(variable_count, 0.0));
        std::vector gradient(variable_count, 0.0);
        for (std::size_t row = 0; row < current.residuals.size(); ++row) {
            for (std::size_t column = 0; column < variable_count; ++column) {
                gradient[column] -= current.jacobian[row][column] * current.residuals[row];
                for (std::size_t other = 0; other < variable_count; ++other) {
                    normal[column][other] += current.jacobian[row][column]
                        * current.jacobian[row][other];
                }
            }
        }
        for (std::size_t diagonal = 0; diagonal < variable_count; ++diagonal) {
            normal[diagonal][diagonal] += damping;
        }
        const auto step = solve_linear_system(std::move(normal), std::move(gradient));
        if (!step.has_value()) {
            damping *= 100.0;
            continue;
        }

        const auto current_error = squared_norm(current.residuals);
        auto accepted = false;
        auto scale = 1.0;
        for (auto attempt = 0; attempt < 12; ++attempt) {
            auto candidate = values;
            for (std::size_t index = 0; index < candidate.size(); ++index) {
                candidate[index] += scale * (*step)[index];
            }
            const auto candidate_error = squared_norm(
                linearize(definition, model, candidate).residuals);
            if (candidate_error < current_error) {
                values = std::move(candidate);
                damping = std::max(1.0e-15, damping * 0.1);
                accepted = true;
                break;
            }
            scale *= 0.5;
        }
        if (!accepted) {
            damping *= 100.0;
            if (damping > 1.0e15) {
                break;
            }
        }
    }

    const auto final = linearize(definition, model, values);
    const auto rank = matrix_rank(final.jacobian);
    const auto maximum_residual = final.residuals.empty()
        ? 0.0
        : std::abs(*std::ranges::max_element(
              final.residuals,
              [](const double left, const double right) {
                  return std::abs(left) < std::abs(right);
              }));

    SolveResult result;
    result.iterations = iterations;
    result.maximum_residual = maximum_residual;
    result.degrees_of_freedom = values.size() - rank;
    result.redundant_equations = final.residuals.size() > rank
        ? final.residuals.size() - rank
        : 0;

    if (maximum_residual > solve_tolerance) {
        result.status = SolveStatus::inconsistent;
        result.diagnostics.push_back(diagnostic(
            iterations == maximum_iterations ? DiagnosticCode::solver_did_not_converge
                                             : DiagnosticCode::inconsistent_constraints,
            "the sketch constraints cannot be satisfied together"));
    } else if (result.degrees_of_freedom != 0) {
        result.status = SolveStatus::under_constrained;
        result.diagnostics.push_back(diagnostic(
            DiagnosticCode::under_constrained,
            "the sketch has unconstrained degrees of freedom"));
    } else if (result.redundant_equations != 0) {
        result.status = SolveStatus::over_constrained;
        result.diagnostics.push_back(diagnostic(
            DiagnosticCode::over_constrained,
            "the sketch contains redundant constraints"));
    } else {
        result.status = SolveStatus::fully_constrained;
    }

    for (const auto& [unused_id, point] : model.points) {
        static_cast<void>(unused_id);
        result.points.push_back(SolvedPoint{
            .id = point.entity.id,
            .position = Point2d{values[point.x_index], values[point.y_index]},
        });
    }
    for (const auto& [unused_id, circle] : model.circles) {
        static_cast<void>(unused_id);
        result.circles.push_back(SolvedCircle{
            .id = circle.entity.id,
            .center_point_id = circle.entity.center_point_id,
            .radius = values[circle.radius_index],
        });
    }
    result.deterministic_signature = signature(result);
    return result;
}

} // namespace oep::sketch
