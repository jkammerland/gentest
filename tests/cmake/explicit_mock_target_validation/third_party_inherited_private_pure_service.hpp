#pragma once

namespace fixture::validation {

struct InheritedPrivatePureBase {
    virtual ~InheritedPrivatePureBase() = default;

  private:
    virtual int hidden() = 0;
};

struct InheritedPrivatePureService : InheritedPrivatePureBase {
    virtual ~InheritedPrivatePureService() = default;

    virtual int visible(int value) = 0;
};

} // namespace fixture::validation
