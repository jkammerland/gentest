#pragma once

namespace fixture::validation {

template <typename T> using MethodValue = T;

struct FinalMethodService {
    virtual ~FinalMethodService() = default;

    virtual int stable(MethodValue<int> value) final { return value; }

    virtual int compute(int value) = 0;
};

} // namespace fixture::validation
