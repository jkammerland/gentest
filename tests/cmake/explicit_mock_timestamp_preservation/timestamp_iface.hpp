#pragma once

namespace timestamp_fixture {

struct Interface {
    virtual ~Interface()         = default;
    virtual int value(int input) = 0;
};

} // namespace timestamp_fixture
