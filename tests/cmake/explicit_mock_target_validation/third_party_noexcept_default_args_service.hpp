#pragma once

namespace fixture::validation {

inline int may_throw_default_value() { return 7; }

struct NoexceptDefaultArgsService {
    int compute(int value = may_throw_default_value()) noexcept;
};

} // namespace fixture::validation
