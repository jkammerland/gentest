// Header-only Cartesian product utility
#pragma once

#include <vector>

namespace gentest::codegen::util {

// Compute the Cartesian product of a list of axes. Each axis is a vector of T.
// Returns a vector of combinations; each combination is a vector<T> with one
// element from each axis, in axis order. When axes is empty, returns one empty
// combination to simplify callers that append to arguments.
template <typename T>
inline std::vector<std::vector<T>> cartesian(const std::vector<std::vector<T>>& axes) {
    std::vector<std::vector<T>> out;
    if (axes.empty()) { out.push_back({}); return out; }
    out.emplace_back();
    for (const auto& axis : axes) {
        std::vector<std::vector<T>> next;
        next.reserve(out.size() * axis.size());
        for (const auto& acc : out) {
            for (const auto& v : axis) {
                auto w = acc; w.push_back(v); next.push_back(std::move(w));
            }
        }
        out = std::move(next);
    }
    if (out.empty()) out.push_back({});
    return out;
}

} // namespace gentest::codegen::util

