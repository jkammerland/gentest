#pragma once

namespace smoke::narrow_preamble {

struct Service {
    virtual ~Service() = default;
    virtual void run() = 0;
};

} // namespace smoke::narrow_preamble
