#pragma once

namespace fixture::validation {

struct ThirdPartyCtorDefaultArgsService {
    int seed;

    explicit ThirdPartyCtorDefaultArgsService(int value, int multiplier = 2) : seed(value * multiplier) {}
    virtual ~ThirdPartyCtorDefaultArgsService() = default;

    virtual int compute() = 0;
};

} // namespace fixture::validation
