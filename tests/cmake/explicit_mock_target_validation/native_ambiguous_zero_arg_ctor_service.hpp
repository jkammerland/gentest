#pragma once

namespace fixture::validation {

struct AmbiguousZeroArgCtorService {
    int seed = 0;

    AmbiguousZeroArgCtorService() = default;
    explicit AmbiguousZeroArgCtorService(int value = 0) : seed(value) {}
    virtual ~AmbiguousZeroArgCtorService() = default;

    virtual int load() = 0;
};

} // namespace fixture::validation
