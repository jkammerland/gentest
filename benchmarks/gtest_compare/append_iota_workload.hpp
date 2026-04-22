#pragma once

#include <cstddef>
#include <stdexcept>

namespace compare_workload {

inline constexpr std::size_t kAppendIotaElementCount = 1'000'000;

template <typename Container> std::size_t appendIota1M() {
    Container values;
    if constexpr (requires { values.reserve(kAppendIotaElementCount); }) {
        values.reserve(kAppendIotaElementCount);
    }

    for (std::size_t i = 0; i < kAppendIotaElementCount; ++i) {
        values.push_back(static_cast<typename Container::value_type>(i));
    }

    if (values.size() != kAppendIotaElementCount) {
        throw std::runtime_error("append_iota workload produced an unexpected size");
    }

    return values.size() + static_cast<std::size_t>(values.front()) + static_cast<std::size_t>(values.back());
}

} // namespace compare_workload
