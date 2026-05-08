#pragma once

namespace fixture::validation {

struct InheritedPublicPureBase {
    virtual ~InheritedPublicPureBase()    = default;
    virtual int inherited(int value)      = 0;
    virtual int inherited_ref(int &value) = 0;
};

struct InheritedPublicPureService : InheritedPublicPureBase {
    virtual ~InheritedPublicPureService() = default;

    virtual int visible(int value) = 0;
};

} // namespace fixture::validation
