#pragma once

namespace fixture::validation {

struct Outer {
    struct Inner {
        virtual ~Inner()               = default;
        virtual int compute(int value) = 0;
    };
};

} // namespace fixture::validation
