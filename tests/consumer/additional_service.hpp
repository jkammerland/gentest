#pragma once

namespace consumer {

struct AdditionalService {
    virtual ~AdditionalService()           = default;
    virtual int transform(int value) const = 0;
};

} // namespace consumer
