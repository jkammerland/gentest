#pragma once

namespace fixture {

inline constexpr int kServiceSentinel = 9;

struct Service {
    virtual ~Service()             = default;
    virtual int compute(int value) = 0;
};

template <char Tag> struct TaggedService {
    virtual ~TaggedService()      = default;
    virtual int adjust(int value) = 0;
};

} // namespace fixture
