#pragma once

namespace fixture {

struct Service {
    virtual ~Service()             = default;
    virtual int compute(int value) = 0;
};

} // namespace fixture
