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

struct MoveOnly {
    int value{0};
    explicit MoveOnly(int v) : value(v) {}
    MoveOnly(MoveOnly&&)            = default;
    MoveOnly& operator=(MoveOnly&&) = default;
    MoveOnly(const MoveOnly&)       = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
    friend bool operator==(const MoveOnly& a, const MoveOnly& b) { return a.value == b.value; }
};

struct MOConsumer {
    void accept(MoveOnly) {}
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
