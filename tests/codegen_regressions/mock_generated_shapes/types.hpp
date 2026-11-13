#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>

// gentest has a namespace of the same name: inside namespace gentest the
// spelling `detail::kSlots` does not reach these.
namespace detail {
inline constexpr std::size_t kSlots = 3;
inline constexpr std::size_t kExtra = 1;

template <typename T> inline constexpr bool kNoThrowCall = !std::is_void_v<T>;
} // namespace detail

namespace fixture {

// Parameter names that used to collide with locals of the generated dispatch.
struct Authorizer {
    virtual ~Authorizer()                                                                    = default;
    virtual bool authorize(const std::string &token)                                         = 0;
    virtual bool refresh(const std::string &token, const std::string &fallback_token)        = 0;
    virtual int  internals(int gentest_mock_state_, int method_name, int id, int args) const = 0;
};

// Function parameter packs in member templates and template constructors.
struct Emitter {
    Emitter() = default;
    template <class... Ts> explicit Emitter(int seed, Ts &&...extra) : seed(seed + static_cast<int>(sizeof...(extra))) {}

    template <class... Ts> void emit(Ts &&...args) { ((void)args, ...); }
    template <class... Ts> int  count(int first, Ts... rest) {
        ((void)rest, ...);
        return first;
    }

    int seed = 0;
};

// Template argument lists directly after an operator name.
struct Bus {
    template <class T> Bus &operator<<(const T &value) {
        (void)value;
        return *this;
    }
    template <class T> bool operator<(const T &value) const {
        (void)value;
        return false;
    }
};

// Expressions inside as-written member template signatures.
struct Slots {
    template <class T> void fill(const std::array<T, detail::kSlots> &values) { (void)values; }
    template <class T> void grow(const std::array<T, detail::kSlots + detail::kExtra> &values) { (void)values; }
    template <class T> void call(void (*callback)(T) noexcept(detail::kNoThrowCall<T>), T value) { callback(value); }
};

} // namespace fixture
