#pragma once

namespace downstream {

struct Service {
    virtual ~Service() = default;
    virtual int compute(int arg) = 0;
};

} // namespace downstream
