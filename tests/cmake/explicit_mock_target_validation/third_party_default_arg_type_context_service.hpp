#pragma once

namespace fixture::validation {

struct DefaultArgTypeContextService {
    struct Options {
        explicit constexpr Options(int value) : value(value) {}
        int value;
    };

    virtual ~DefaultArgTypeContextService() = default;

    virtual int size(int value = sizeof(Options))               = 0;
    virtual int alignment(int value = alignof(Options))         = 0;
    virtual int cast(Options options = static_cast<Options>(3)) = 0;
};

} // namespace fixture::validation
