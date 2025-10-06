#pragma once

namespace mocking {

struct Calculator {
    virtual ~Calculator() = default;
    virtual int compute(int lhs, int rhs) = 0;
    virtual void reset() = 0;
};

struct Ticker {
    void tick(int value) { (void)value; }
    template <typename T>
    void tadd(T value) { (void)value; }
};

template <typename Derived>
struct Runner {
    void run(int value) { static_cast<Derived *>(this)->handle(value); }
    void handle(int) {}
};

struct DerivedRunner : Runner<DerivedRunner> {
    void handle(int) {}
};

} // namespace mocking
