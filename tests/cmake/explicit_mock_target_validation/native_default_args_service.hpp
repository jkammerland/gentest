#pragma once

namespace fixture::validation {

struct DefaultArgsService {
    virtual ~DefaultArgsService() = default;

    virtual int compute(int value, int scale = 3) = 0;
};

struct CtorDefaultArgsService {
    int seed;

    explicit CtorDefaultArgsService(int value, int multiplier = 2) : seed(value * multiplier) {}
    virtual ~CtorDefaultArgsService() = default;

    virtual int load(int offset = 4) = 0;
};

} // namespace fixture::validation
