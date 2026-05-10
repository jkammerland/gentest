#pragma once

namespace fixture::validation {

struct OverloadHidingService {
    virtual ~OverloadHidingService() = default;

    virtual int stable(int value) final { return value; }
    virtual int stable(double value) = 0;

    void        disabled(int value)    = delete;
    virtual int disabled(double value) = 0;
};

} // namespace fixture::validation
