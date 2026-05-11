#pragma once

namespace fixture::validation {

struct ThirdPartyMethodDefaultArgsService {
    virtual ~ThirdPartyMethodDefaultArgsService() = default;

    virtual int compute(int value = 7) = 0;
};

} // namespace fixture::validation
