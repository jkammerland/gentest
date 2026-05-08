#pragma once

namespace fixture::validation {

struct VariadicService {
    virtual ~VariadicService()               = default;
    virtual int log(char const *format, ...) = 0;
};

} // namespace fixture::validation
