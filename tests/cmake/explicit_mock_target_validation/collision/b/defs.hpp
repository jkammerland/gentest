#pragma once

namespace fixture::collision_b {

struct Service {
    virtual ~Service()  = default;
    virtual int value() = 0;
};

using ServiceMock = gentest::mock<Service>;

} // namespace fixture::collision_b
