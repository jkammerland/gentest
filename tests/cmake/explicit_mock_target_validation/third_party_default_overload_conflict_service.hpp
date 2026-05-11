#pragma once

namespace fixture::validation {

struct DefaultOverloadConflictService {
    virtual ~DefaultOverloadConflictService() = default;

    virtual int compute(int value)                = 0;
    virtual int compute(int value, int scale = 2) = 0;
};

} // namespace fixture::validation
