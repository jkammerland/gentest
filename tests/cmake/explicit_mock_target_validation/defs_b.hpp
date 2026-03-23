#pragma once

namespace fixture::validation {

struct ServiceB {
    virtual ~ServiceB() = default;
    virtual int scale(int value) = 0;
};

using ServiceBMock = gentest::mock<ServiceB>;

} // namespace fixture::validation
