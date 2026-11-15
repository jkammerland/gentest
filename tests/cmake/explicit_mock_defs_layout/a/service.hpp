#pragma once

namespace layout_a {

struct Service {
    virtual ~Service()  = default;
    virtual int value() = 0;
};

} // namespace layout_a
