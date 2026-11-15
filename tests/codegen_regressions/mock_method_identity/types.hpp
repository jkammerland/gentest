#pragma once

namespace fixture {

struct IReader {
    virtual ~IReader() = default;
    virtual int read() = 0;
};

struct IWriter {
    virtual ~IWriter()            = default;
    virtual void write(int value) = 0;
};

// read() and write() are the first virtual of their own base, so the member
// pointers &IStream::read and &IStream::write have the same representation.
struct IStream : IReader, IWriter {};

struct ILeft {
    virtual ~ILeft()   = default;
    virtual int left() = 0;
};

struct IRight {
    virtual ~IRight()   = default;
    virtual int right() = 0;
};

// Same representation and same signature; only the declaring class differs.
struct IBoth : ILeft, IRight {};

struct InheritedBase {
    virtual ~InheritedBase() = default;
    virtual int  inherited(int value) { return value + 1; }
    virtual long inherited(long value) { return value + 2; }
    virtual int  plain(int value) const noexcept { return value + 3; }
};

// A mock of this target dispatches methods it only inherits.
struct InheritedService : InheritedBase {
    virtual int visible(int value) = 0;
};

} // namespace fixture
