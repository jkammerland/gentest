#pragma once

namespace fixture::validation {

struct InheritedConcreteBase {
    virtual ~InheritedConcreteBase() = default;

    virtual int inherited(int value) { return value + 1; }
};

struct InheritedConcreteService : InheritedConcreteBase {
    virtual ~InheritedConcreteService() = default;

    virtual int visible(int value) = 0;
};

} // namespace fixture::validation
