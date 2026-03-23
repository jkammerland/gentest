#pragma once

namespace fixture::validation {

struct ServiceA {
    virtual ~ServiceA() = default;
    virtual int compute(int value) = 0;
};

using ServiceAMock = gentest::mock<ServiceA>;

} // namespace fixture::validation
