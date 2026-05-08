#pragma once

namespace fixture::validation {

struct VolatileService {
    virtual ~VolatileService()  = default;
    virtual int load() volatile = 0;
};

} // namespace fixture::validation
