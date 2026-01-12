#pragma once

namespace benchmarks::demo {

struct Blob {
    int a;
    int b;
};

inline int work(const Blob &b) {
    return (b.a * 3) + (b.b * 5);
}

} // namespace benchmarks::demo
