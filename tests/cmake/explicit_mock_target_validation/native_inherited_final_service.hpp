#pragma once

namespace fixture::validation {

struct InheritedFinalBase {
    virtual ~InheritedFinalBase() = default;

    virtual int inherited_final() final { return 11; }
};

struct InheritedFinalService : InheritedFinalBase {
    virtual ~InheritedFinalService() = default;

    virtual int visible() = 0;
};

} // namespace fixture::validation
