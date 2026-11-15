#pragma once

namespace clash_a {

struct Service {
    virtual ~Service()  = default;
    virtual int value() = 0;
};

} // namespace clash_a
