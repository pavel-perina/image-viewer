#include "core/coverage.h"

#include <algorithm>
#include <cmath>

namespace iv {

Coverage spread(const Coverage& shape, double radius, double soft) {
    Coverage out(shape.width, shape.height);
    if (shape.values.empty()) {
        return out;
    }
    soft = std::max(soft, 0.01);
    // How covered a pixel is whose middle is a distance outside the shape's edge.
    const auto cover = [&](double outside) { return static_cast<float>(std::clamp((radius - outside) / soft + 0.5, 0.0, 1.0)); };
    // Further than this from an edge pixel nothing is covered.
    const int reach = static_cast<int>(std::ceil(radius + soft / 2 + 0.5));
    const int side = 2 * reach + 1;
    std::vector<float> distance(static_cast<std::size_t>(side) * static_cast<std::size_t>(side));
    for (int dy = -reach; dy <= reach; ++dy) {
        for (int dx = -reach; dx <= reach; ++dx) {
            distance[static_cast<std::size_t>((dy + reach) * side + dx + reach)] = static_cast<float>(std::hypot(dx, dy));
        }
    }
    const auto full = [&](int x, int y) {
        // Beyond the plane counts as more of the same, so a shape cut by it has no edge there.
        return x < 0 || y < 0 || x >= shape.width || y >= shape.height || shape.at(x, y) >= 1;
    };
    for (int y = 0; y < shape.height; ++y) {
        for (int x = 0; x < shape.width; ++x) {
            const float a = shape.at(x, y);
            if (a <= 0) {
                continue;
            }
            if (a >= 1 && full(x - 1, y) && full(x + 1, y) && full(x, y - 1) && full(x, y + 1)) {
                out.at(x, y) = 1;  // well inside: only pixels at the edge reach outwards
                continue;
            }
            // A pixel on the edge. Half covered, the edge runs through its middle; more, and
            // the edge is that much further out.
            const double edge = 0.5 - a;
            const int top = std::max(0, y - reach);
            const int bottom = std::min(shape.height - 1, y + reach);
            const int left = std::max(0, x - reach);
            const int right = std::min(shape.width - 1, x + reach);
            for (int v = top; v <= bottom; ++v) {
                const float* from = &distance[static_cast<std::size_t>((v - y + reach) * side + left - x + reach)];
                float* to = &out.at(left, v);
                for (int u = left; u <= right; ++u, ++from, ++to) {
                    *to = std::max(*to, cover(*from + edge));
                }
            }
        }
    }
    return out;
}

Coverage shifted(const Coverage& shape, double dx, double dy) {
    Coverage out(shape.width, shape.height);
    const int wholeX = static_cast<int>(std::floor(dx));
    const int wholeY = static_cast<int>(std::floor(dy));
    const auto partX = static_cast<float>(dx - wholeX);
    const auto partY = static_cast<float>(dy - wholeY);
    if (partX == 0 && partY == 0) {
        // By whole pixels, rows are only copied.
        const int first = std::max(0, wholeX);
        const int count = shape.width - std::abs(wholeX);
        for (int y = std::max(0, wholeY); count > 0 && y < std::min(out.height, out.height + wholeY); ++y) {
            std::copy_n(&shape.at(first - wholeX, y - wholeY), count, &out.at(first, y));
        }
        return out;
    }
    const auto from = [&](int x, int y) { return x < 0 || y < 0 || x >= shape.width || y >= shape.height ? 0.0F : shape.at(x, y); };
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            const int sx = x - wholeX;
            const int sy = y - wholeY;
            out.at(x, y) = (1 - partY) * ((1 - partX) * from(sx, sy) + partX * from(sx - 1, sy))
                           + partY * ((1 - partX) * from(sx, sy - 1) + partX * from(sx - 1, sy - 1));
        }
    }
    return out;
}

Coverage blurred(const Coverage& shape, double sigma) {
    if (sigma <= 0 || shape.values.empty()) {
        return shape;
    }
    const int reach = std::max(1, static_cast<int>(std::ceil(sigma * 3)));
    std::vector<float> weights(static_cast<std::size_t>(2 * reach + 1));
    float sum = 0;
    for (int i = -reach; i <= reach; ++i) {
        sum += weights[static_cast<std::size_t>(i + reach)] = static_cast<float>(std::exp(-0.5 * i * i / (sigma * sigma)));
    }
    for (float& weight : weights) {
        weight /= sum;
    }
    // Along rows, then along columns.
    Coverage rows(shape.width, shape.height);
    for (int y = 0; y < shape.height; ++y) {
        // Each covered pixel is spread along its row, so empty stretches cost nothing.
        const float* from = &shape.at(0, y);
        float* to = &rows.at(0, y);
        for (int x = 0; x < shape.width; ++x) {
            if (from[x] <= 0) {
                continue;
            }
            for (int i = std::max(-reach, -x); i <= std::min(reach, shape.width - 1 - x); ++i) {
                to[x + i] += weights[static_cast<std::size_t>(i + reach)] * from[x];
            }
        }
    }
    Coverage out(shape.width, shape.height);
    for (int i = -reach; i <= reach; ++i) {
        const float weight = weights[static_cast<std::size_t>(i + reach)];
        for (int y = std::max(0, -i); y < std::min(shape.height, shape.height - i); ++y) {
            const float* from = &rows.at(0, y + i);
            float* to = &out.at(0, y);
            for (int x = 0; x < shape.width; ++x) {
                to[x] += weight * from[x];
            }
        }
    }
    return out;
}

}  // namespace iv
