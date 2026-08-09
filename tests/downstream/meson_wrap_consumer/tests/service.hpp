#pragma once

#include "shared_service_value.hpp"

namespace downstream {

struct Service {
    virtual ~Service()           = default;
    virtual int compute(int arg) = 0;
};

} // namespace downstream
