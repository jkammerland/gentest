#pragma once

namespace fixture::validation {

struct OverloadOrderService {
    virtual ~OverloadOrderService() = default;
    virtual int value(long value)   = 0;
    virtual int value(double value) = 0;
    virtual int value(int value)    = 0;
    virtual int value(float value)  = 0;
};

} // namespace fixture::validation
