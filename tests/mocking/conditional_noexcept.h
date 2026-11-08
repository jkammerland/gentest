#pragma once

// Regression coverage for mocks of class template specializations whose
// members use a conditional exception specification (#266). Members of an
// implicitly instantiated specialization carry an unresolved exception
// specification until something forces it; the generated override must still
// reproduce the resolved specification.

// Kept outside namespace mocking: generated code names template arguments from
// inside namespace gentest, where `mocking::` would find gentest::detail::mocking.
namespace noexcept_policy {
struct NoThrow {
    static constexpr bool value = true;
};

struct Throw {
    static constexpr bool value = false;
};
} // namespace noexcept_policy

namespace mocking {

template <bool NoThrow> struct CondSink {
    virtual ~CondSink()                                = default;
    virtual void put() noexcept(NoThrow)               = 0;
    virtual int  peek() const noexcept(NoThrow)        = 0;
    virtual int  drain()  &&noexcept(NoThrow)          = 0;
    virtual int  inspect() const  &noexcept(!!NoThrow) = 0;
    virtual void plain() noexcept                      = 0;
    virtual void throwing() noexcept(false)            = 0;
};

template <typename Traits> struct TraitSink {
    virtual ~TraitSink()                                     = default;
    virtual void put(int value) noexcept(Traits::value)      = 0;
    virtual int  get() const noexcept(Traits::value && true) = 0;
};

// The template parameter also appears in the signatures.
template <typename T, bool NoThrow> struct ValueSource {
    virtual ~ValueSource()                      = default;
    virtual T    get() const noexcept(NoThrow)  = 0;
    virtual void set(T value) noexcept(NoThrow) = 0;
};

template <bool NoThrow> struct CondCtor {
    explicit CondCtor(int seed) noexcept(NoThrow) : seed(seed) {}
    virtual ~CondCtor() = default;
    virtual int ping()  = 0;
    int         seed    = 0;
};

} // namespace mocking
