#pragma once

namespace fixture::validation {

struct ConversionService {
    virtual ~ConversionService() = default;

    virtual operator bool() const = 0;
};

struct VariadicService {
    virtual ~VariadicService() = default;

    virtual int log(char const *format, ...) = 0;
};

struct VolatileService {
    virtual ~VolatileService() = default;

    virtual int load() volatile = 0;
};

struct AssignmentService {
    virtual ~AssignmentService() = default;

    virtual AssignmentService &operator=(AssignmentService const &) = 0;
    virtual int                value()                              = 0;
};

struct NoDefaultField {
    explicit NoDefaultField(int) {}
};

struct DeletedDefaultCtorService {
    NoDefaultField field;

    virtual ~DeletedDefaultCtorService() = default;

    virtual int value() = 0;
};

} // namespace fixture::validation
