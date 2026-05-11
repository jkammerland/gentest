#pragma once

namespace fixture::validation {

struct FinalTargetService final {
    virtual ~FinalTargetService()  = default;
    virtual int compute(int value) = 0;
};

} // namespace fixture::validation
