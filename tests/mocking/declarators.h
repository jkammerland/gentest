#pragma once

// Regression coverage for generated mock signatures whose parameter or return
// types wrap the declarator: pointers and references to functions or arrays and
// member-function pointers. The parameter name must be placed inside the
// declarator (`void (*cb)(int)`, `const int (&values)[3]`), and such return
// types cannot be written in front of the method name (#276).

namespace mocking {

struct DeclaratorTarget {
    int value = 0;

    [[nodiscard]] int scaled(int factor) const { return value * factor; }
};

using DeclaratorCallback = void (*)(int);

struct Declarators {
    virtual ~Declarators() = default;

    virtual void set_callback(void (*cb)(int))                              = 0;
    virtual int  sum(const int (&values)[3])                                = 0;
    virtual int  apply(int (DeclaratorTarget::*method)(int) const, int arg) = 0;
    virtual void (*get_callback())(int)                                     = 0;
    virtual int (*row(int index))[2]                                        = 0;
    virtual int (DeclaratorTarget::*pick() const)(int) const                = 0;
    virtual DeclaratorCallback aliased(DeclaratorCallback cb)               = 0;
    virtual void               unnamed(void (*)(int), int (&)[2])           = 0;

    // Member function template with an array-reference parameter and a
    // deducible bound.
    template <typename T, int N> T first(T (&values)[N]) { return values[0]; }
};

// Constructor parameters are rendered through the same declaration path.
struct DeclaratorCtor {
    DeclaratorCtor(void (*init)(int), const int (&seed)[2]) { init(seed[0] + seed[1]); }
    virtual ~DeclaratorCtor() = default;

    virtual int get() const = 0;
};

} // namespace mocking
