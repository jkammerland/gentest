#pragma once

namespace fixture::validation {

struct ConversionService {
    virtual ~ConversionService()  = default;
    virtual operator bool() const = 0;
};

} // namespace fixture::validation
