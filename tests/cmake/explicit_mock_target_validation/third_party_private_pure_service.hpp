#pragma once

namespace fixture::validation {

struct PrivatePureService {
    virtual ~PrivatePureService() = default;

    virtual int visible(int value) = 0;

  private:
    virtual int hidden() = 0;
};

} // namespace fixture::validation
