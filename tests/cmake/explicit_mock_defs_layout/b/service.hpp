#pragma once

namespace layout_b {

struct Service {
    virtual ~Service()  = default;
    virtual int value() = 0;
};

} // namespace layout_b
