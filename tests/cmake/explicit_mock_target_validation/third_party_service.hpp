#pragma once

namespace fixture::validation {

struct ThirdPartyService {
    virtual ~ThirdPartyService()   = default;
    virtual int compute(int value) = 0;
};

} // namespace fixture::validation
