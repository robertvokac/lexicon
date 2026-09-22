#pragma once
// Places the nodes of a relationship graph: rings by distance from the centre
// to start with, then a Fruchterman-Reingold pass that pushes every pair apart
// and pulls linked items together. Deterministic, so the same graph always
// looks the same; the web and Android clients use the same steps.
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace graphlayout {
struct Point {
    double x = 0;
    double y = 0;
};

// depths[i] is node i's distance from the centre, node 0; edges index nodes.
inline std::vector<Point> layout(const std::vector<int>& depths,
                                 const std::vector<std::pair<int, int>>& edges) {
    const int count = static_cast<int>(depths.size());
    std::vector<Point> points(count);
    constexpr double pi = 3.14159265358979323846;
    constexpr double ring = 150;
    constexpr double ideal = 120;
    constexpr int iterations = 250;
    std::vector<int> perDepth(4, 0), seen(4, 0);
    for (int depth : depths) ++perDepth[std::clamp(depth, 0, 3)];
    for (int i = 1; i < count; ++i) {
        const int depth = std::clamp(depths[i], 1, 3);
        const double angle = 2 * pi * seen[depth]++ / std::max(1, perDepth[depth]) + depth * 0.7;
        points[i] = {ring * depth * std::cos(angle), ring * depth * std::sin(angle)};
    }
    std::vector<Point> shift(count);
    for (int step = 0; step < iterations; ++step) {
        const double temperature = 80.0 * (1.0 - static_cast<double>(step) / iterations) + 2.0;
        std::fill(shift.begin(), shift.end(), Point{});
        for (int i = 0; i < count; ++i) {
            for (int j = i + 1; j < count; ++j) {
                const double dx = points[i].x - points[j].x, dy = points[i].y - points[j].y;
                const double distance = std::max(std::hypot(dx, dy), 0.01);
                const double force = ideal * ideal / distance;
                shift[i].x += dx / distance * force; shift[i].y += dy / distance * force;
                shift[j].x -= dx / distance * force; shift[j].y -= dy / distance * force;
            }
        }
        for (const auto& [a, b] : edges) {
            if (a == b) continue;
            const double dx = points[a].x - points[b].x, dy = points[a].y - points[b].y;
            const double distance = std::max(std::hypot(dx, dy), 0.01);
            const double force = distance * distance / ideal;
            shift[a].x -= dx / distance * force; shift[a].y -= dy / distance * force;
            shift[b].x += dx / distance * force; shift[b].y += dy / distance * force;
        }
        for (int i = 1; i < count; ++i) { // The centre stays where it is.
            const double length = std::hypot(shift[i].x, shift[i].y);
            if (length < 1e-9) continue;
            const double move = std::min(length, temperature);
            points[i].x += shift[i].x / length * move;
            points[i].y += shift[i].y / length * move;
        }
    }
    return points;
}
} // namespace graphlayout
