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

template <typename T> struct ArgPredicate {
    std::function<bool(const T &)>        test;
    std::function<std::string(const T &)> describe; // optional; may be empty
};

template <typename T, typename P>
concept HasMakeFor = requires(const P &p) {
    { p.template make<T>() } -> std::same_as<ArgPredicate<T>>;
};

template <typename T, typename P>
ArgPredicate<T> to_arg_predicate(P &&p) {
    if constexpr (HasMakeFor<T, P>) {
        return p.template make<T>();
    } else {
        ArgPredicate<T> out;
        out.test     = std::function<bool(const T &)>{std::forward<P>(p)};
        out.describe = [](const T &) { return std::string("predicate mismatch"); };
        return out;
    }
}

template <typename Tuple, typename... A>
bool check_args_equal(const std::optional<Tuple> &expected, std::string_view method_name, const A &...actual) {
    if (!expected)
        return true;
    const auto actual_tuple = std::forward_as_tuple(actual...);
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
                        to_string_fallback(std::get<I>(actual_tuple))), std::source_location::current());
                     reported = true;
                 }
             }()),
             ...);
        }(std::make_index_sequence<sizeof...(A)>{});
    }
    return matched;
}

template <typename TuplePred, typename... A>
bool check_args_by_predicates(const std::optional<TuplePred> &preds, std::string_view method_name, const A &...actual) {
    if (!preds)
        return true;
    const auto actual_tuple = std::forward_as_tuple(actual...);
    bool       ok           = true;
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        ((ok = ok && ([&] {
              const auto &ap = std::get<I>(*preds);
              const auto &a  = std::get<I>(actual_tuple);
              if (!ap.test(a)) {
                  const std::string msg = ap.describe ? ap.describe(a) : std::string("predicate mismatch");
                  ::gentest::detail::record_failure(
                      fmt::format("argument[{}] mismatch for {}: {}", I, method_name, msg), std::source_location::current());
                  return false;
              }
              return true;
          }())),
         ...);
    }(std::make_index_sequence<sizeof...(A)>{});
    return ok;
}

inline void verify_calls_or_fail(std::size_t expected, std::size_t observed, std::string_view method_name, bool &already_verified) {
    if (already_verified)
        return;
    already_verified = true;
    if (observed < expected) {
        ::gentest::detail::record_failure(
            fmt::format("expected {} call(s) to {} but observed {}", expected, method_name, observed), std::source_location::current());
    }
}

template <typename... Args> struct ExpectationCommon : ExpectationBase {
    std::size_t                                                     expected_calls = 1;
    std::size_t                                                     observed_calls = 0;
    bool                                                            allow_excess   = false;
    std::optional<std::tuple<std::decay_t<Args>...>>                expected_args;
    std::optional<std::tuple<ArgPredicate<std::decay_t<Args>>...>>  arg_predicates;
    std::function<bool(const std::decay_t<Args>&...)>               call_predicate;

    bool is_satisfied() const { return observed_calls >= expected_calls; }

    void verify(std::string_view method_name) override {
        verify_calls_or_fail(expected_calls, observed_calls, method_name, this->already_verified);
    }

    template <typename... X>
    void set_expected(X &&...values) {
        expected_args = std::tuple<std::decay_t<Args>...>(std::forward<X>(values)...);
    }

    template <typename... P>
    void set_predicates(P &&...preds) {
        arg_predicates = std::tuple<ArgPredicate<std::decay_t<Args>>...>(
            to_arg_predicate<std::decay_t<Args>>(std::forward<P>(preds))...);
    }

    bool check_args(std::string_view method_name, const std::decay_t<Args> &...actual) {
        if (call_predicate) {
            if (!call_predicate(actual...)) {
                ::gentest::detail::record_failure(fmt::format("call predicate mismatch for {}", method_name));
                return false;
            }
            return true;
        }
        if (arg_predicates) return check_args_by_predicates(arg_predicates, method_name, actual...);
        return check_args_equal(expected_args, method_name, actual...);
    }
};

template <typename R, typename... Args> struct Expectation<R(Args...)> : ExpectationCommon<Args...> {
    std::function<R(const std::decay_t<Args>&...)> action;

    R invoke(std::string_view method_name, Args... args) {
        if (!this->allow_excess && this->observed_calls >= this->expected_calls) {
            ::gentest::detail::record_failure(fmt::format("unexpected call to {}", method_name), std::source_location::current());
        }
        (void)this->check_args(method_name, std::forward<Args>(args)...);
        ++this->observed_calls;
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

template <typename... Args> struct Expectation<void(Args...)> : ExpectationCommon<Args...> {
    std::function<void(const std::decay_t<Args>&...)> action;

    void invoke(std::string_view method_name, Args... args) {
        if (!this->allow_excess && this->observed_calls >= this->expected_calls) {
            ::gentest::detail::record_failure(fmt::format("unexpected call to {}", method_name), std::source_location::current());
        }
        (void)this->check_args(method_name, std::forward<Args>(args)...);
        ++this->observed_calls;
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

    void set_nice(bool v) { nice_mode_ = v; }
    bool nice() const { return nice_mode_; }

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
            if (!nice_mode_) {
                ::gentest::detail::record_failure(fmt::format("unexpected call to {}", method_name));
            }
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
            expectation->invoke(method_name, std::forward<Args>(args)...);
        } else {
            auto result = expectation->invoke(method_name, std::forward<Args>(args)...);
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
    bool nice_mode_ = false;
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
            using DArgs = std::tuple<std::decay_t<Args>...>;
            (void)sizeof(DArgs);
            auto wrapper = [fn = std::forward<Callable>(callable)](const std::decay_t<Args> &...a) -> R { return fn(a...); };
            expectation_->action = std::move(wrapper);
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

    template <typename... P>
    ExpectationHandle &where_args(P &&... predicates) {
        static_assert(sizeof...(P) == sizeof...(Args), "where_args arity must match mocked method");
        if (expectation_) {
            expectation_->set_predicates(std::forward<P>(predicates)...);
        }
        return *this;
    }

    // Convenience alias to mirror common matcher API naming
    template <typename... P>
    ExpectationHandle &where(P &&... predicates) {
        return where_args(std::forward<P>(predicates)...);
    }

    template <typename Callable>
    ExpectationHandle &where_call(Callable &&call_pred) {
        if (expectation_) {
            expectation_->call_predicate = std::function<bool(const std::decay_t<Args>&...)>(std::forward<Callable>(call_pred));
        }
        return *this;
    }

    template <typename Value> ExpectationHandle &returns(Value &&value) {
        if constexpr (std::is_void_v<R>) {
            static_assert(!std::is_void_v<R>, "returns() is not available for void-returning methods");
        } else {
            if (expectation_) {
                if constexpr (std::is_reference_v<R>) {
                    expectation_->action = [&value](const std::decay_t<Args> &... a) -> R {
                        (void)sizeof...(a);
                        return value;
                    };
                } else {
                    expectation_->action = [captured = std::forward<Value>(value)](const std::decay_t<Args> &... a) -> R {
                        (void)sizeof...(a);
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
    static void set_nice(Mock&, bool) {}
#endif
};

} // namespace detail

template <class Mock, class MethodPtr> auto expect(Mock &instance, MethodPtr method) {
    return detail::MockAccess<std::remove_cvref_t<Mock>>::expect(instance, method);
}

template <class Mock>
void make_nice(Mock &instance, bool v = true) {
    detail::MockAccess<std::remove_cvref_t<Mock>>::set_nice(instance, v);
}
template <class Mock>
void make_strict(Mock &instance) {
    detail::MockAccess<std::remove_cvref_t<Mock>>::set_nice(instance, false);
}

// Lightweight matcher helpers for predicate-based argument matching.
// Use with ExpectationHandle::where_args(...) or ::where(...), e.g.:
//   expect(mock, &T::fn).where(Eq(42), Any());
namespace match {
using ::gentest::detail::mocking::ArgPredicate;

struct AnyFactory {
    template <typename T> ArgPredicate<T> make() const {
        ArgPredicate<T> ap;
        ap.test = [](const T &) noexcept { return true; };
        return ap;
    }
};
inline auto Any() { return AnyFactory{}; }

template <typename V> struct EqFactory {
    using Value = std::decay_t<V>;
    Value expected;
    template <typename T> ArgPredicate<T> make() const {
        ArgPredicate<T> ap;
        const Value     exp = expected;
        ap.test             = [exp](const T &a) { return a == exp; };
        ap.describe         = [exp](const T &a) {
            return fmt::format("expected == {}, got {}", ::gentest::detail::mocking::to_string_fallback(exp),
                               ::gentest::detail::mocking::to_string_fallback(a));
        };
        return ap;
    }
};
template <typename V> inline auto Eq(V &&v) { return EqFactory<V>{std::forward<V>(v)}; }

template <typename A, typename B> struct InRangeFactory {
    using Lo = std::decay_t<A>;
    using Hi = std::decay_t<B>;
    Lo l;
    Hi h;
    template <typename T> ArgPredicate<T> make() const {
        ArgPredicate<T> ap;
        const Lo        lo = l;
        const Hi        hi = h;
        ap.test             = [lo, hi](const T &a) { return (a >= lo) && (a <= hi); };
        ap.describe         = [lo, hi](const T &a) {
            return fmt::format("expected in [{}, {}], got {}", ::gentest::detail::mocking::to_string_fallback(lo),
                               ::gentest::detail::mocking::to_string_fallback(hi),
                               ::gentest::detail::mocking::to_string_fallback(a));
        };
        return ap;
    }
};
template <typename A, typename B> inline auto InRange(A &&lo, B &&hi) { return InRangeFactory<A, B>{std::forward<A>(lo), std::forward<B>(hi)}; }

template <typename P> struct NotFactory {
    P inner;
    template <typename T> ArgPredicate<T> make() const {
        ArgPredicate<T> ip = inner.template make<T>();
        ArgPredicate<T> ap;
        ap.test     = [ip](const T &a) { return !ip.test(a); };
        ap.describe = [ip](const T &a) {
            std::string inner_desc = ip.describe ? ip.describe(a) : std::string("predicate matched");
            return std::string("not(") + inner_desc + ")";
        };
        return ap;
    }
};
template <typename P> inline auto Not(P &&p) { return NotFactory<std::decay_t<P>>{std::forward<P>(p)}; }

template <typename V> struct GeFactory {
    using Value = std::decay_t<V>;
    Value bound;
    template <typename T> ArgPredicate<T> make() const {
        ArgPredicate<T> ap;
        const Value     b = bound;
        ap.test            = [b](const T &a) { return a >= b; };
        ap.describe        = [b](const T &a) {
            return fmt::format("expected >= {}, got {}", ::gentest::detail::mocking::to_string_fallback(b),
                               ::gentest::detail::mocking::to_string_fallback(a));
        };
        return ap;
    }
};
template <typename V> inline auto Ge(V &&v) { return GeFactory<V>{std::forward<V>(v)}; }

template <typename V> struct LeFactory {
    using Value = std::decay_t<V>;
    Value bound;
    template <typename T> ArgPredicate<T> make() const {
        ArgPredicate<T> ap;
        const Value     b = bound;
        ap.test            = [b](const T &a) { return a <= b; };
        ap.describe        = [b](const T &a) {
            return fmt::format("expected <= {}, got {}", ::gentest::detail::mocking::to_string_fallback(b),
                               ::gentest::detail::mocking::to_string_fallback(a));
        };
        return ap;
    }
};
template <typename V> inline auto Le(V &&v) { return LeFactory<V>{std::forward<V>(v)}; }

template <typename V> struct GtFactory {
    using Value = std::decay_t<V>;
    Value bound;
    template <typename T> ArgPredicate<T> make() const {
        ArgPredicate<T> ap;
        const Value     b = bound;
        ap.test            = [b](const T &a) { return a > b; };
        ap.describe        = [b](const T &a) {
            return fmt::format("expected > {}, got {}", ::gentest::detail::mocking::to_string_fallback(b),
                               ::gentest::detail::mocking::to_string_fallback(a));
        };
        return ap;
    }
};
template <typename V> inline auto Gt(V &&v) { return GtFactory<V>{std::forward<V>(v)}; }

template <typename V> struct LtFactory {
    using Value = std::decay_t<V>;
    Value bound;
    template <typename T> ArgPredicate<T> make() const {
        ArgPredicate<T> ap;
        const Value     b = bound;
        ap.test            = [b](const T &a) { return a < b; };
        ap.describe        = [b](const T &a) {
            return fmt::format("expected < {}, got {}", ::gentest::detail::mocking::to_string_fallback(b),
                               ::gentest::detail::mocking::to_string_fallback(a));
        };
        return ap;
    }
};
template <typename V> inline auto Lt(V &&v) { return LtFactory<V>{std::forward<V>(v)}; }

template <typename V, typename E> struct NearFactory {
    using Value = std::decay_t<V>;
    using Eps   = std::decay_t<E>;
    Value expected;
    Eps   eps;
    template <typename T> ArgPredicate<T> make() const {
        ArgPredicate<T> ap;
        const Value     exp = expected;
        const Eps       e   = eps;
        ap.test             = [exp, e](const T &a) {
            using std::abs;
            return abs(static_cast<long double>(a) - static_cast<long double>(exp)) <= static_cast<long double>(e);
        };
        ap.describe = [exp, e](const T &a) {
            return fmt::format("expected near {} ± {}, got {}", ::gentest::detail::mocking::to_string_fallback(exp),
                               ::gentest::detail::mocking::to_string_fallback(e),
                               ::gentest::detail::mocking::to_string_fallback(a));
        };
        return ap;
    }
};
template <typename V, typename E> inline auto Near(V &&v, E &&eps) { return NearFactory<V, E>{std::forward<V>(v), std::forward<E>(eps)}; }

struct StrContainsFactory {
    std::string needle;
    template <typename T> ArgPredicate<T> make() const {
        ArgPredicate<T> ap;
        const std::string nd = needle;
        ap.test               = [nd](const T &a) {
            std::string_view s(a);
            return s.find(nd) != std::string_view::npos;
        };
        ap.describe = [nd](const T &a) {
            std::string_view s(a);
            return fmt::format("expected substring '{}', got '{}'", nd, s);
        };
        return ap;
    }
};
inline auto StrContains(std::string needle) { return StrContainsFactory{std::move(needle)}; }

struct StartsWithFactory {
    std::string prefix;
    template <typename T> ArgPredicate<T> make() const {
        ArgPredicate<T> ap;
        const std::string px = prefix;
        ap.test               = [px](const T &a) {
            std::string_view s(a);
            return s.rfind(px, 0) == 0;
        };
        ap.describe = [px](const T &a) {
            std::string_view s(a);
            return fmt::format("expected prefix '{}', got '{}'", px, s);
        };
        return ap;
    }
};
inline auto StartsWith(std::string prefix) { return StartsWithFactory{std::move(prefix)}; }

struct EndsWithFactory {
    std::string suffix;
    template <typename T> ArgPredicate<T> make() const {
        ArgPredicate<T> ap;
        const std::string sx = suffix;
        ap.test               = [sx](const T &a) {
            std::string_view s(a);
            return s.size() >= sx.size() && s.substr(s.size() - sx.size()) == sx;
        };
        ap.describe = [sx](const T &a) {
            std::string_view s(a);
            return fmt::format("expected suffix '{}', got '{}'", sx, s);
        };
        return ap;
    }
};
inline auto EndsWith(std::string suffix) { return EndsWithFactory{std::move(suffix)}; }

template <typename... M> struct AnyOfFactory {
    std::tuple<M...> subs;
    template <typename T> ArgPredicate<T> make() const {
        ArgPredicate<T> ap;
        const auto      subs_local = subs;
        ap.test                    = [subs_local](const T &a) {
            bool ok = false;
            std::apply([&](auto const &...m) { ((ok = ok || m.template make<T>().test(a)), ...); }, subs_local);
            return ok;
        };
        ap.describe = [subs_local](const T &a) {
            std::string msg = "expected any of: ";
            bool        first = true;
            std::apply(
                [&](auto const &...m) {
                    (([&] {
                         auto inner = m.template make<T>();
                         if (!first) msg += "; ";
                         first = false;
                         if (inner.describe) msg += inner.describe(a); else msg += "predicate";
                     }()),
                     ...);
                },
                subs_local);
            return msg;
        };
        return ap;
    }
};
template <typename... M> inline auto AnyOf(M &&...m) { return AnyOfFactory<std::decay_t<M>...>{std::tuple<std::decay_t<M>...>(std::forward<M>(m)...)}; }

template <typename... M> struct AllOfFactory {
    std::tuple<M...> subs;
    template <typename T> ArgPredicate<T> make() const {
        ArgPredicate<T> ap;
        const auto      subs_local = subs;
        ap.test                    = [subs_local](const T &a) {
            bool ok = true;
            std::apply([&](auto const &...m) { ((ok = ok && m.template make<T>().test(a)), ...); }, subs_local);
            return ok;
        };
        ap.describe = [subs_local](const T &a) {
            std::string msg = "expected all of: ";
            bool        first = true;
            std::apply(
                [&](auto const &...m) {
                    (([&] {
                         auto inner = m.template make<T>();
                         if (!first) msg += "; ";
                         first = false;
                         if (inner.describe) msg += inner.describe(a); else msg += "predicate";
                     }()),
                     ...);
                },
                subs_local);
            return msg;
        };
        return ap;
    }
};
template <typename... M> inline auto AllOf(M &&...m) { return AllOfFactory<std::decay_t<M>...>{std::tuple<std::decay_t<M>...>(std::forward<M>(m)...)}; }
} // namespace match

#ifdef GENTEST_MOCK_REGISTRY_PATH
#define GENTEST_DETAIL_MOCK_STRINGIFY_IMPL(x) #x
#define GENTEST_DETAIL_MOCK_STRINGIFY(x) GENTEST_DETAIL_MOCK_STRINGIFY_IMPL(x)
#define GENTEST_MOCK_REGISTRY_HEADER GENTEST_DETAIL_MOCK_STRINGIFY(GENTEST_MOCK_REGISTRY_PATH)
#if __has_include(GENTEST_MOCK_REGISTRY_HEADER)
#include GENTEST_MOCK_REGISTRY_HEADER
#endif
#undef GENTEST_MOCK_REGISTRY_HEADER
#endif

} // namespace gentest

// Include generated mock inline implementations at global scope, so fully
// qualified definitions like `inline auto gentest::mock<T>::method(...)` are
// declared in the correct namespace context.
#ifdef GENTEST_MOCK_IMPL_PATH
#ifndef GENTEST_DETAIL_MOCK_STRINGIFY_IMPL
#define GENTEST_DETAIL_MOCK_STRINGIFY_IMPL(x) #x
#define GENTEST_DETAIL_MOCK_STRINGIFY(x) GENTEST_DETAIL_MOCK_STRINGIFY_IMPL(x)
#endif
#define GENTEST_MOCK_IMPL_HEADER GENTEST_DETAIL_MOCK_STRINGIFY(GENTEST_MOCK_IMPL_PATH)
#if __has_include(GENTEST_MOCK_IMPL_HEADER)
#include GENTEST_MOCK_IMPL_HEADER
#endif
#undef GENTEST_MOCK_IMPL_HEADER
#endif

#if defined(GENTEST_DETAIL_MOCK_STRINGIFY)
#undef GENTEST_DETAIL_MOCK_STRINGIFY
#endif
#if defined(GENTEST_DETAIL_MOCK_STRINGIFY_IMPL)
#undef GENTEST_DETAIL_MOCK_STRINGIFY_IMPL
#endif
