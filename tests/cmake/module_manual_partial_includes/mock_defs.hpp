#pragma once

namespace partial {

struct Service {
    virtual ~Service()             = default;
    virtual int compute(int value) = 0;
};

} // namespace partial
