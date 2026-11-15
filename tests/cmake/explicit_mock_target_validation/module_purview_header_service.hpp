#pragma once

namespace fixture {

struct PurviewService {
    virtual ~PurviewService() = default;
    virtual int value()       = 0;
};

} // namespace fixture
