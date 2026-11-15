#pragma once

namespace fixture::collision_a {

struct Service {
    virtual ~Service()  = default;
    virtual int value() = 0;
};

using ServiceMock = gentest::mock<Service>;

} // namespace fixture::collision_a
