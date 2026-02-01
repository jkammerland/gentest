#pragma once

namespace mocking {

struct Calculator {
    virtual ~Calculator() = default;
    virtual int compute(int lhs, int rhs) = 0;
    virtual void reset() = 0;
};

struct RefProvider {
    virtual ~RefProvider() = default;
    virtual int& value() = 0;
};

struct Ticker {
    static int add(int lhs, int rhs) noexcept { return lhs + rhs; }
    void tick(int value) { (void)value; }
    template <typename T>
    void tadd(T value) { (void)value; }
};

struct NoDefault {
    explicit NoDefault(int seed) noexcept : seed(seed) {}
    NoDefault(int seed, long extra) : seed(seed + static_cast<int>(extra)) {}

    template <typename T>
    explicit NoDefault(T seed_like, int extra) noexcept : seed(static_cast<int>(seed_like) + extra) {}

    int  seed = 0;
    void work(int) {}
};

struct NeedsInit {
    explicit NeedsInit(int seed) noexcept : seed(seed) {}
    NeedsInit(int seed, long extra) : seed(seed + static_cast<int>(extra)) {}

    template <typename T>
    explicit NeedsInit(T seed_like) noexcept : seed(static_cast<int>(seed_like)) {}

    template <typename T>
    NeedsInit(T seed_like, int extra) noexcept : seed(static_cast<int>(seed_like) + extra) {}

    virtual ~NeedsInit() = default;
    virtual int now() = 0;

    int seed = 0;
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

template <typename T>
struct RefWrap {
    RefWrap() = default;
    RefWrap(RefWrap&&) = default;
    RefWrap& operator=(RefWrap&&) = default;
    RefWrap(const RefWrap&) = delete;
    RefWrap& operator=(const RefWrap&) = delete;
    friend bool operator==(const RefWrap&, const RefWrap&) { return true; }
};

struct RefWrapConsumer {
    void take(RefWrap<int&>) {}
};

struct Stringer {
    void put(std::string s) { (void)s; }
};

struct Floater {
    void feed(double v) { (void)v; }
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
