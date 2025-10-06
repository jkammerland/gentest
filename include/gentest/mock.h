#pragma once

#include "gentest/runner.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <deque>
#include <exception>
#include <fmt/core.h>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <tuple>
#include <string>
#include <string_view>
#include <typeinfo>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gentest {

#ifdef GENTEST_CODEGEN
namespace detail::mocking {
template <typename T, typename = void>
struct PlaceholderMockBase {
    PlaceholderMockBase()  = default;
    ~PlaceholderMockBase() = default;
};

template <typename T>
struct PlaceholderMockBase<T, std::void_t<decltype(sizeof(T))>> : T {
    using T::T;
};
} // namespace detail::mocking

template <typename T> struct mock : detail::mocking::PlaceholderMockBase<T> {
    mock()  = default;
    ~mock() = default;
};
#else
template <typename T> struct mock;
#endif

namespace detail::mocking {

struct MethodIdentity {
    std::vector<std::byte> bytes;

    template <class MethodPtr> static MethodIdentity from(MethodPtr ptr) {
        MethodIdentity id;
        id.bytes.resize(sizeof(MethodPtr));
        std::memcpy(id.bytes.data(), &ptr, sizeof(MethodPtr));
        return id;
    }

    bool operator==(const MethodIdentity &other) const noexcept { return bytes == other.bytes; }
};

struct MethodIdentityHash {
    std::size_t operator()(const MethodIdentity &id) const noexcept {
        std::size_t value = 1469598103934665603ull; // FNV-1a offset basis
        for (const auto b : id.bytes) {
            value ^= static_cast<std::size_t>(std::to_integer<unsigned char>(b));
            value *= 1099511628211ull;
        }
        return value;
    }
};

struct ExpectationBase {
    virtual ~ExpectationBase()                        = default;
    virtual void verify(std::string_view method_name) = 0;
    bool         already_verified                     = false;
};

template <typename Signature> struct Expectation;

template <typename T>
concept Ostreamable = requires(std::ostream &os, const T &v) {
    os << v;
};

template <typename T>
static std::string to_string_fallback(const T &v) {
    if constexpr (Ostreamable<T>) {
        std::ostringstream oss;
        oss << v;
        return oss.str();
    } else {
        return std::string(typeid(T).name());
    }
}

template <typename Tuple, typename... A>
bool check_args_equal(const std::optional<Tuple> &expected, std::string_view method_name, const A &...actual) {
    if (!expected)
        return true;
    const auto actual_tuple = std::tuple<std::decay_t<A>...>(actual...);
    const bool matched      = [&]<std::size_t... I>(std::index_sequence<I...>) {
        return ((std::get<I>(*expected) == std::get<I>(actual_tuple)) && ...);
    }(std::make_index_sequence<sizeof...(A)>{});
    if (!matched) {
        // Report first mismatch with indices for clarity
        bool reported = false;
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (([&] {
                 if (reported) return;
                 if (!(std::get<I>(*expected) == std::get<I>(actual_tuple))) {
                     ::gentest::detail::record_failure(fmt::format(
                         "argument[{}] mismatch for {}: expected {}, got {}", I, method_name, to_string_fallback(std::get<I>(*expected)),
                         to_string_fallback(std::get<I>(actual_tuple))));
                     reported = true;
                 }
             }()),
             ...);
        }(std::make_index_sequence<sizeof...(A)>{});
    }
    return matched;
}

inline void verify_calls_or_fail(std::size_t expected, std::size_t observed, std::string_view method_name, bool &already_verified) {
    if (already_verified)
        return;
    already_verified = true;
    if (observed < expected) {
        ::gentest::detail::record_failure(
            fmt::format("expected {} call(s) to {} but observed {}", expected, method_name, observed));
    }
}

template <typename R, typename... Args> struct Expectation<R(Args...)> : ExpectationBase {
    std::size_t               expected_calls = 1;
    std::size_t               observed_calls = 0;
    bool                      allow_excess   = false;
    std::function<R(Args...)> action;
    std::optional<std::tuple<std::decay_t<Args>...>> expected_args;

    bool is_satisfied() const { return observed_calls >= expected_calls; }

    void verify(std::string_view method_name) override { verify_calls_or_fail(expected_calls, observed_calls, method_name, this->already_verified); }

    template <typename... X>
    void set_expected(X &&...values) {
        expected_args = std::tuple<std::decay_t<Args>...>(std::forward<X>(values)...);
    }

    bool check_args(std::string_view method_name, const std::decay_t<Args> &...actual) {
        return check_args_equal(expected_args, method_name, actual...);
    }

    R invoke(std::string_view method_name, Args... args) {
        if (!allow_excess && observed_calls >= expected_calls) {
            ::gentest::detail::record_failure(fmt::format("unexpected call to {}", method_name));
        }
        (void)check_args(method_name, std::forward<Args>(args)...);
        ++observed_calls;
        if (action) {
            return action(args...);
        }
        if constexpr (!std::is_void_v<R>) {
            if constexpr (std::is_reference_v<R>) {
                std::terminate();
            } else {
                return R{};
            }
        }
    }
};

template <typename... Args> struct Expectation<void(Args...)> : ExpectationBase {
    std::size_t                  expected_calls = 1;
    std::size_t                  observed_calls = 0;
    bool                         allow_excess   = false;
    std::function<void(Args...)> action;
    std::optional<std::tuple<std::decay_t<Args>...>> expected_args;

    bool is_satisfied() const { return observed_calls >= expected_calls; }

    void verify(std::string_view method_name) override { verify_calls_or_fail(expected_calls, observed_calls, method_name, this->already_verified); }

    template <typename... X>
    void set_expected(X &&...values) {
        expected_args = std::tuple<std::decay_t<Args>...>(std::forward<X>(values)...);
    }

    bool check_args(std::string_view method_name, const std::decay_t<Args> &...actual) {
        return check_args_equal(expected_args, method_name, actual...);
    }

    void invoke(std::string_view method_name, Args... args) {
        if (!allow_excess && observed_calls >= expected_calls) {
            ::gentest::detail::record_failure(fmt::format("unexpected call to {}", method_name));
        }
        (void)check_args(method_name, std::forward<Args>(args)...);
        ++observed_calls;
        if (action) {
            action(args...);
        }
    }
};

class InstanceState {
  public:
    InstanceState()                                 = default;
    InstanceState(const InstanceState &)            = delete;
    InstanceState &operator=(const InstanceState &) = delete;

    ~InstanceState() = default;

    void verify_all() {
        for (auto &[_, entry] : methods_) {
            for (auto &expectation : entry.queue) {
                expectation->verify(entry.method_name);
            }
        }
    }

    template <typename MethodPtr> MethodIdentity identify(MethodPtr ptr) { return MethodIdentity::from(ptr); }

    template <typename R, typename... Args>
    std::shared_ptr<Expectation<R(Args...)>> push_expectation(const MethodIdentity &id, std::string method_name) {
        auto &entry = methods_[id];
        if (entry.method_name.empty())
            entry.method_name = std::move(method_name);
        auto expectation = std::make_shared<Expectation<R(Args...)>>();
        entry.queue.push_back(expectation);
        return expectation;
    }

    template <typename R, typename... Args> R dispatch(const MethodIdentity &id, std::string_view method_name, Args &&...args) {
        auto it = methods_.find(id);
        if (it == methods_.end() || it->second.queue.empty()) {
            ::gentest::detail::record_failure(fmt::format("unexpected call to {}", method_name));
            if constexpr (!std::is_void_v<R>) {
                if constexpr (std::is_reference_v<R>) {
                    std::terminate();
                } else {
                    return R{};
                }
            } else {
                return;
            }
        }
        using ExpectationT = Expectation<R(std::decay_t<Args>...)>;
        auto expectation     = std::static_pointer_cast<ExpectationT>(it->second.queue.front());
        if constexpr (std::is_void_v<R>) {
            expectation->invoke(method_name, args...);
        } else {
            auto result = expectation->invoke(method_name, args...);
            if (expectation->is_satisfied() && !expectation->allow_excess) {
                it->second.queue.pop_front();
            }
            return result;
        }
        if (expectation->is_satisfied() && !expectation->allow_excess) {
            it->second.queue.pop_front();
        }
        if constexpr (!std::is_void_v<R>) {
            return R{};
        }
    }

  private:
    struct MethodEntry {
        std::string                                  method_name;
        std::deque<std::shared_ptr<ExpectationBase>> queue;
    };

    std::unordered_map<MethodIdentity, MethodEntry, MethodIdentityHash> methods_;
};

template <typename Signature> class ExpectationHandle;

template <typename R, typename... Args> class ExpectationHandle<R(Args...)> {
  public:
    ExpectationHandle() = default;
    ExpectationHandle(std::shared_ptr<Expectation<R(Args...)>> expectation, std::string name)
        : expectation_(std::move(expectation)), method_name_(std::move(name)) {}

    ExpectationHandle(ExpectationHandle &&other) noexcept            = default;
    ExpectationHandle &operator=(ExpectationHandle &&other) noexcept = default;

    ExpectationHandle(const ExpectationHandle &)            = delete;
    ExpectationHandle &operator=(const ExpectationHandle &) = delete;

    ~ExpectationHandle() = default;

    ExpectationHandle &times(std::size_t expected) {
        if (expectation_)
            expectation_->expected_calls = expected;
        return *this;
    }

    template <typename Callable> ExpectationHandle &invokes(Callable &&callable) {
        if (expectation_) {
            expectation_->action = std::function<R(Args...)>(std::forward<Callable>(callable));
        }
        return *this;
    }

    template <typename... X>
    ExpectationHandle &with(X &&... expected) {
        if (expectation_) {
            expectation_->set_expected(std::forward<X>(expected)...);
        }
        return *this;
    }

    template <typename Value> ExpectationHandle &returns(Value &&value) {
        if constexpr (std::is_void_v<R>) {
            static_assert(!std::is_void_v<R>, "returns() is not available for void-returning methods");
        } else {
            if (expectation_) {
                if constexpr (std::is_reference_v<R>) {
                    expectation_->action = [&value](Args... args) -> R {
                        (void)sizeof...(args);
                        return value;
                    };
                } else {
                    expectation_->action = [captured = std::forward<Value>(value)](Args... args) -> R {
                        (void)sizeof...(args);
                        return captured;
                    };
                }
            }
        }
        return *this;
    }

    ExpectationHandle &allow_more(bool enabled = true) {
        if (expectation_)
            expectation_->allow_excess = enabled;
        return *this;
    }

  private:
    std::shared_ptr<Expectation<R(Args...)>> expectation_;
    std::string                              method_name_;
};

template <typename T> struct MethodTraits;

template <typename R, typename... Args> struct MethodTraits<R(Args...)> {
    using Signature = R(Args...);
};

template <typename R, typename... Args> struct MethodTraits<R (*)(Args...)> : MethodTraits<R(Args...)> {};

template <typename Class, typename R, typename... Args> struct MethodTraits<R (Class::*)(Args...)> : MethodTraits<R(Args...)> {};

template <typename Class, typename R, typename... Args> struct MethodTraits<R (Class::*)(Args...) noexcept> : MethodTraits<R(Args...)> {};

template <typename Class, typename R, typename... Args> struct MethodTraits<R (Class::*)(Args...) const> : MethodTraits<R(Args...)> {};

template <typename Class, typename R, typename... Args>
struct MethodTraits<R (Class::*)(Args...) const noexcept> : MethodTraits<R(Args...)> {};

template <typename Class, typename R, typename... Args> struct MethodTraits<R (Class::*)(Args...) volatile> : MethodTraits<R(Args...)> {};

template <typename Class, typename R, typename... Args>
struct MethodTraits<R (Class::*)(Args...) volatile noexcept> : MethodTraits<R(Args...)> {};

template <typename Class, typename R, typename... Args>
struct MethodTraits<R (Class::*)(Args...) const volatile> : MethodTraits<R(Args...)> {};

template <typename Class, typename R, typename... Args>
struct MethodTraits<R (Class::*)(Args...) const volatile noexcept> : MethodTraits<R(Args...)> {};

#define GENTEST_DETAIL_MOCK_METHOD_TRAITS(refqual)                                                                                         \
    template <typename Class, typename R, typename... Args>                                                                                \
    struct MethodTraits<R (Class::*)(Args...) refqual> : MethodTraits<R(Args...)> {};                                                      \
    template <typename Class, typename R, typename... Args>                                                                                \
    struct MethodTraits<R (Class::*)(Args...) refqual noexcept> : MethodTraits<R(Args...)> {};                                             \
    template <typename Class, typename R, typename... Args>                                                                                \
    struct MethodTraits<R (Class::*)(Args...) const refqual> : MethodTraits<R(Args...)> {};                                                \
    template <typename Class, typename R, typename... Args>                                                                                \
    struct MethodTraits<R (Class::*)(Args...) const refqual noexcept> : MethodTraits<R(Args...)> {};                                       \
    template <typename Class, typename R, typename... Args>                                                                                \
    struct MethodTraits<R (Class::*)(Args...) volatile refqual> : MethodTraits<R(Args...)> {};                                             \
    template <typename Class, typename R, typename... Args>                                                                                \
    struct MethodTraits<R (Class::*)(Args...) volatile refqual noexcept> : MethodTraits<R(Args...)> {};                                    \
    template <typename Class, typename R, typename... Args>                                                                                \
    struct MethodTraits<R (Class::*)(Args...) const volatile refqual> : MethodTraits<R(Args...)> {};                                       \
    template <typename Class, typename R, typename... Args>                                                                                \
    struct MethodTraits<R (Class::*)(Args...) const volatile refqual noexcept> : MethodTraits<R(Args...)> {};

GENTEST_DETAIL_MOCK_METHOD_TRAITS(&)
GENTEST_DETAIL_MOCK_METHOD_TRAITS(&&)

#undef GENTEST_DETAIL_MOCK_METHOD_TRAITS

} // namespace detail::mocking

namespace detail::mocking {
// Helper to push expectations from a function signature type R(Args...).
template <typename Sig> struct ExpectationPusher;
template <typename R, typename... Args> struct ExpectationPusher<R(Args...)> {
    static std::shared_ptr<Expectation<R(Args...)>> push(InstanceState &st, const MethodIdentity &id, std::string name) {
        return st.template push_expectation<R, Args...>(id, std::move(name));
    }
};
} // namespace detail::mocking

namespace detail {

template <class Mock> struct MockAccess {
#ifndef GENTEST_CODEGEN
    static_assert(sizeof(Mock) == 0, "gentest::mock<T> specialization missing generated accessors");
#else
    template <class MethodPtr> static auto expect(Mock &, MethodPtr) {
        struct Stub {
            Stub &times(...) { return *this; }
            template <typename... Ts> Stub &returns(Ts &&...) { return *this; }
            template <typename... Ts> Stub &invokes(Ts &&...) { return *this; }
            Stub &allow_more(...) { return *this; }
        };
        return Stub{};
    }
#endif
};

} // namespace detail

template <class Mock, class MethodPtr> auto expect(Mock &instance, MethodPtr method) {
    return detail::MockAccess<std::remove_cvref_t<Mock>>::expect(instance, method);
}

#ifdef GENTEST_MOCK_REGISTRY_PATH
#define GENTEST_DETAIL_MOCK_STRINGIFY_IMPL(x) #x
#define GENTEST_DETAIL_MOCK_STRINGIFY(x) GENTEST_DETAIL_MOCK_STRINGIFY_IMPL(x)
#define GENTEST_MOCK_REGISTRY_HEADER GENTEST_DETAIL_MOCK_STRINGIFY(GENTEST_MOCK_REGISTRY_PATH)
#if __has_include(GENTEST_MOCK_REGISTRY_HEADER)
#include GENTEST_MOCK_REGISTRY_HEADER
#endif
#undef GENTEST_MOCK_REGISTRY_HEADER
#undef GENTEST_DETAIL_MOCK_STRINGIFY
#undef GENTEST_DETAIL_MOCK_STRINGIFY_IMPL
#endif

} // namespace gentest
