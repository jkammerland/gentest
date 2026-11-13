#pragma once

#include <functional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>

namespace fixture {

struct NoEq {
    int value = 0;
};

struct IAbstract {
    virtual ~IAbstract()   = default;
    virtual int id() const = 0;
};

struct ConcreteShape : IAbstract {
    explicit ConcreteShape(int id) : id_(id) {}
    int id() const override { return id_; }
    int id_ = 0;
};

// An explicit single-argument constructor: with(7) can only compare after
// converting the value to the parameter type.
struct Strong {
    explicit Strong(int value) : value(value) {}
    int         value                                      = 0;
    friend bool operator==(const Strong &, const Strong &) = default;
};

// None of these parameters can be stored by value or compared with ==.
struct Sink {
    virtual ~Sink()                                        = default;
    virtual void subscribe(std::function<void()> callback) = 0;
    virtual void send(std::span<const int> values)         = 0;
    virtual void dump(std::ostream &out)                   = 0;
    virtual void put(const NoEq &value)                    = 0;
    virtual void handle(const IAbstract &shape)            = 0;
    virtual void mixed(const NoEq &value, int count)       = 0;
};

struct Text {
    virtual ~Text()                                = default;
    virtual void write(std::string_view text)      = 0;
    virtual void label(const char *text)           = 0;
    virtual void name(const std::string &text)     = 0;
    virtual void strong(Strong value)              = 0;
    virtual void scaled(double factor, unsigned n) = 0;
};

} // namespace fixture
