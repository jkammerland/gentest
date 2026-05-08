#pragma once

#include <string>

namespace fixture::third_party {

struct Calculator {
    virtual ~Calculator()                     = default;
    virtual int         add(int lhs, int rhs) = 0;
    virtual std::string name() const noexcept = 0;
};

} // namespace fixture::third_party
