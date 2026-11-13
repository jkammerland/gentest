#pragma once

namespace clash_b {

struct Service {
    virtual ~Service()  = default;
    virtual int value() = 0;
};

} // namespace clash_b
