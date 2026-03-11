#pragma once

namespace legacy {

struct Service {
    virtual ~Service()                = default;
    virtual int compute(int argument) = 0;
};

} // namespace legacy
