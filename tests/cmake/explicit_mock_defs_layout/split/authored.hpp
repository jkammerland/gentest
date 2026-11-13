#pragma once

namespace layout_split {

struct Authored {
    virtual ~Authored() = default;
    virtual int value() = 0;
};

} // namespace layout_split
