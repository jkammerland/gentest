#pragma once

namespace depcase {

struct Iface {
    virtual ~Iface()             = default;
    virtual void ping(int value) = 0;
};

} // namespace depcase
