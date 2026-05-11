#pragma once

namespace fixture::validation {

struct GenericAliasService {
    virtual ~GenericAliasService() = default;

    virtual int value() = 0;
};

} // namespace fixture::validation
