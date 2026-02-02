#pragma once

#include <memory>
#include <type_traits>
#include <utility>

namespace gentest {

// Optional setup/teardown interfaces for fixtures.
//
// Implement these in your fixture type if you need explicit hooks that may
// fail (avoid throwing from destructors). The generated runner detects these
// interfaces at compile-time and invokes them around each member test call
// (ephemeral fixtures) or around each call on the shared instance when using
// suite/global fixture lifetimes.
struct FixtureSetup {
    virtual ~FixtureSetup() = default;
    virtual void setUp()    = 0;
};

struct FixtureTearDown {
    virtual ~FixtureTearDown() = default;
    virtual void tearDown()    = 0;
};

namespace detail {

template <typename T> struct FixtureAllocation {
    std::unique_ptr<T> unique;
    std::shared_ptr<T> shared;
};

template <typename T, typename = void> struct HasGentestAllocate : std::false_type {};
template <typename T>
struct HasGentestAllocate<T, std::void_t<decltype(T::gentest_allocate())>> : std::true_type {};

template <typename T>
inline constexpr bool kHasGentestAllocate = HasGentestAllocate<T>::value;

template <typename> inline constexpr bool kAlwaysFalse = false;

template <typename T>
FixtureAllocation<T> allocate_fixture() {
    FixtureAllocation<T> out;
    if constexpr (kHasGentestAllocate<T>) {
        auto result = T::gentest_allocate();
        using Result = decltype(result);
        if constexpr (std::is_constructible_v<std::shared_ptr<T>, Result>) {
            out.shared = std::shared_ptr<T>(std::move(result));
        } else if constexpr (std::is_constructible_v<std::unique_ptr<T>, Result>) {
            out.unique = std::unique_ptr<T>(std::move(result));
        } else if constexpr (std::is_pointer_v<Result> && std::is_convertible_v<Result, T*>) {
            out.unique.reset(result);
        } else {
            static_assert(kAlwaysFalse<Result>,
                          "gentest_allocate must return std::unique_ptr<T>, std::shared_ptr<T>, or T*");
        }
    } else {
        out.unique = std::make_unique<T>();
    }
    return out;
}

template <typename T>
class FixtureHandle {
  public:
    FixtureHandle() : storage_(allocate_fixture<T>()) {}

    T* get() {
        if (storage_.shared) return storage_.shared.get();
        return storage_.unique.get();
    }

    T& ref() { return *get(); }

    std::shared_ptr<T> shared() {
        if (!storage_.shared) {
            storage_.shared = std::shared_ptr<T>(std::move(storage_.unique));
        }
        return storage_.shared;
    }

    operator T&() { return ref(); }
    operator T*() { return get(); }
    operator std::shared_ptr<T>() { return shared(); }

  private:
    FixtureAllocation<T> storage_;
};

} // namespace detail

} // namespace gentest
