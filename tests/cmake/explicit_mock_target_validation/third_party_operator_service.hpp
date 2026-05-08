#pragma once

namespace fixture::validation {

struct OperatorService {
    virtual ~OperatorService()                                = default;
    virtual bool operator==(OperatorService const &rhs) const = 0;
};

} // namespace fixture::validation
