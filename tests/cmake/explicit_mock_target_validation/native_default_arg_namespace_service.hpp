#pragma once

namespace fixture::validation {

inline constexpr int kNamespaceDefaultValue = 23;

struct NamespaceDefaultArgService {
    virtual ~NamespaceDefaultArgService() = default;

    virtual int value(int input = kNamespaceDefaultValue) = 0;
};

} // namespace fixture::validation
