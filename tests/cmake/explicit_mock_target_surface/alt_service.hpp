#pragma once

namespace fixture {

struct AltService {
    virtual ~AltService()        = default;
    virtual int scale(int value) = 0;
};

} // namespace fixture
