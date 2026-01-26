// Simple helpers for running independent work items in parallel.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace gentest::codegen {

[[nodiscard]] inline std::size_t default_concurrency(std::size_t task_count) {
    const unsigned hw = std::thread::hardware_concurrency();
    std::size_t    jobs = hw == 0 ? 1u : static_cast<std::size_t>(hw);
    jobs               = std::max<std::size_t>(1, std::min(jobs, task_count));
    return jobs;
}

template <typename Func>
void parallel_for(std::size_t task_count, std::size_t jobs, Func &&func) {
    if (task_count == 0) {
        return;
    }
    jobs = std::max<std::size_t>(1, std::min(jobs, task_count));
    if (jobs == 1) {
        for (std::size_t i = 0; i < task_count; ++i) {
            func(i);
        }
        return;
    }

    std::atomic<std::size_t> next{0};
    std::vector<std::thread> threads;
    threads.reserve(jobs);
    for (std::size_t t = 0; t < jobs; ++t) {
        threads.emplace_back([&] {
            while (true) {
                const std::size_t idx = next.fetch_add(1, std::memory_order_relaxed);
                if (idx >= task_count) {
                    return;
                }
                func(idx);
            }
        });
    }
    for (auto &th : threads) {
        th.join();
    }
}

} // namespace gentest::codegen

