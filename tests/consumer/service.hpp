#pragma once

namespace consumer {

struct Service {
    virtual ~Service()           = default;
    virtual int compute(int arg) = 0;
};

} // namespace consumer
