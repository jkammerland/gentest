#pragma once

namespace fixture {

inline constexpr int kServiceSentinel = 9;

struct Service {
    virtual ~Service() = default;
    virtual int compute(int value) = 0;
};

} // namespace fixture
