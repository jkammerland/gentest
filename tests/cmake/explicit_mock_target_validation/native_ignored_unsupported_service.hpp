#pragma once

namespace fixture::validation {

struct IgnoredUnsupportedService {
    virtual ~IgnoredUnsupportedService() = default;

    virtual int value() = 0;

  private:
        operator bool() const  = delete;
    int log(char const *, ...) = delete;
    int load() volatile        = delete;
};

} // namespace fixture::validation
