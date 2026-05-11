#pragma once

namespace fixture::validation {

struct DefaultArgOptions {
    int value = 17;

    bool operator==(const DefaultArgOptions &) const = default;
};

struct TypeDefaultArgService {
    virtual ~TypeDefaultArgService() = default;

    virtual int value(DefaultArgOptions options = DefaultArgOptions{}) = 0;
};

} // namespace fixture::validation
