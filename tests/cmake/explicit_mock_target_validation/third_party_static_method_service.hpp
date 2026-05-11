#pragma once

namespace fixture::validation {

struct StaticMethodService {
    static int compute(int value) { return value; }
};

} // namespace fixture::validation
