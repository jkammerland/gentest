#pragma once

// GMock/Trompeloeil mock targets compile their DEFS without the gentest runtime.
// There, this header only declares the gentest::mock<T> marker.
#ifdef GENTEST_MOCK_FORWARD_ONLY
#include "gentest/mock_fwd.h"
#else

#include "gentest/format_value.h"
#include "gentest/matchers.h"
#include "gentest/mock_fwd.h"
#include "gentest/runner.h"

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <deque>
#include <exception>
#include <fmt/core.h>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gentest {

namespace detail::mocking {

constexpr std::size_t hash_text(std::string_view text) noexcept {
    auto value = static_cast<std::size_t>(14695981039346656037ull); // FNV-1a offset basis
    for (const char c : text) {
        value ^= static_cast<std::size_t>(static_cast<unsigned char>(c));
        value *= static_cast<std::size_t>(1099511628211ull);
    }
    return value;
}

// Compiler-spelled name of T with static storage. Identities compare the text,
// so they need no RTTI and stay equal across shared-library boundaries.
template <class T> constexpr std::string_view type_token() noexcept {
#if defined(__clang__) || defined(__GNUC__)
    return __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
    return __FUNCSIG__;
#else
    return {};
#endif
}

template <class T> struct TypeToken {
    static constexpr std::string_view text = type_token<T>();
    static constexpr std::size_t      hash = hash_text(text);
};

constexpr bool same_token(std::string_view lhs, std::string_view rhs) noexcept {
    return lhs.size() == rhs.size() && (lhs.data() == rhs.data() || lhs == rhs);
}

// Identifies one mocked method: the member-pointer type (declaring class and
// signature) plus either the pointer's object representation or a
// compile-time token. The type keeps methods apart whose pointer values
// coincide, such as the first virtual of two different base classes.
struct MethodIdentity {
    static constexpr std::size_t word_count = 4;

    std::string_view                    type;
    std::string_view                    name;
    std::array<std::size_t, word_count> words{};
    std::size_t                         hash = 0;

    template <class MethodPtr> static MethodIdentity from(MethodPtr ptr) noexcept {
        static_assert(sizeof(MethodPtr) <= sizeof(std::size_t) * word_count, "unsupported member pointer representation");
        MethodIdentity id;
        id.type = TypeToken<MethodPtr>::text;
        std::memcpy(static_cast<void *>(id.words.data()), static_cast<const void *>(std::addressof(ptr)), sizeof(MethodPtr));
        id.hash = TypeToken<MethodPtr>::hash;
        for (const std::size_t word : id.words) {
            id.hash = (id.hash ^ word) * static_cast<std::size_t>(1099511628211ull);
        }
        return id;
    }

    // `name` must have static storage duration.
    template <class MethodPtr> static constexpr MethodIdentity named(std::string_view name) noexcept {
        MethodIdentity id;
        id.type = TypeToken<MethodPtr>::text;
        id.name = name;
        id.hash = TypeToken<MethodPtr>::hash ^ hash_text(name);
        return id;
    }

    bool operator==(const MethodIdentity &other) const noexcept {
        return hash == other.hash && words == other.words && same_token(type, other.type) && same_token(name, other.name);
    }
};

// Use a compile-time method token when the exact selected method is known.
// MSVC-family Windows toolchains need this to avoid identical-code-folding
// collisions; other toolchains keep the raw member-pointer bytes so distinct
// template instantiations remain distinct identities.
template <auto Method> inline MethodIdentity method_constant_identity() noexcept {
#if defined(_MSC_VER)
    static constexpr MethodIdentity id = MethodIdentity::named<decltype(Method)>(__FUNCSIG__);
    return id;
#else
    return MethodIdentity::from(Method);
#endif
}

// Selects the member of an overload set by its function type and yields the
// pointer typed on the class that declares it. Generated mocks name inherited
// methods this way: a base-to-derived member-pointer conversion is not
// accepted as a template argument by every compiler.
template <class Function> struct DeclaredMethod {
    template <class Class> static constexpr auto select(Function Class::*method) noexcept -> Function Class::* { return method; }
};

struct MethodIdentityHash {
    std::size_t operator()(const MethodIdentity &id) const noexcept { return id.hash; }
};

template <typename Lhs, typename Rhs> inline constexpr bool same_v = std::is_same_v<Lhs, Rhs>;

inline void record_mock_failure(std::string message, const std::source_location &location = {}) {
    if (location.line() != 0)
        message += fmt::format(" (expectation at {})", ::gentest::detail::loc_to_string(location));
    ::gentest::detail::record_failure(std::move(message), location);
}

struct ExpectationBase {
    virtual ~ExpectationBase()                                = default;
    virtual void         verify(std::string_view method_name) = 0;
    bool                 already_verified                     = false;
    std::source_location location{};
    // TypeToken of the R(Args...) the expectation was created for; dispatch
    // checks it before downcasting.
    std::string_view signature;
};

template <typename Signature> struct Expectation;

template <typename T> inline std::string to_string_fallback(const T &v) {
    if constexpr (std::is_function_v<std::remove_pointer_t<T>> && ::gentest::detail::ValueFormattable<T>) {
        return ::gentest::detail::format_printable_value(v, false);
    } else {
        return ::gentest::format_value(v);
    }
}

using ::gentest::detail::matching::ArgPredicate;
using ::gentest::detail::matching::to_arg_predicate;

// Narrow-string parameters and expected values compare by content.
template <typename T>
concept TextParameter =
    std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view> || std::is_same_v<T, const char *> || std::is_same_v<T, char *>;
template <typename T>
concept TextValue = !std::is_null_pointer_v<T> && std::is_convertible_v<const T &, std::string_view>;

template <typename T> std::optional<std::string_view> text_of(const T &value) {
    if constexpr (std::is_pointer_v<T>) {
        if (value == nullptr)
            return std::nullopt;
    }
    return std::string_view(value);
}

inline std::string describe_text(const std::optional<std::string_view> &text) {
    return text ? to_string_fallback(*text) : std::string("nullptr");
}

template <typename Param, typename Value>
concept EqualityComparableWithValue = requires(const Param &actual, const Value &expected) {
    { actual == expected } -> std::convertible_to<bool>;
};

// Holds a `.with(...)` value for the copyable std::function callbacks of an
// ArgPredicate; move-only values are shared between the copies.
template <typename T> struct ExpectedValue {
    T        value;
    const T &get() const noexcept { return value; }
};
template <typename T>
    requires(!std::is_copy_constructible_v<T>)
struct ExpectedValue<T> {
    std::shared_ptr<const T> value;
    const T                 &get() const noexcept { return *value; }
};
template <typename T, typename X> ExpectedValue<T> make_expected_value(X &&value) {
    if constexpr (std::is_copy_constructible_v<T>) {
        return ExpectedValue<T>{T(std::forward<X>(value))};
    } else {
        return ExpectedValue<T>{std::make_shared<const T>(std::forward<X>(value))};
    }
}

template <typename Param, typename Stored> ArgPredicate<Param> expected_value_predicate(const ExpectedValue<Stored> &expected) {
    ArgPredicate<Param> out;
    out.test     = [expected](const Param &actual) -> bool { return actual == expected.get(); };
    out.describe = [expected](const Param &actual) {
        return fmt::format("expected {}, got {}", to_string_fallback(expected.get()), to_string_fallback(actual));
    };
    return out;
}

// The value can be converted to the parameter type and compared as such.
template <typename Param, typename X>
concept ConvertsToParameter = std::is_constructible_v<Param, X> && EqualityComparableWithValue<Param, Param>;
// The caller's decayed value can be kept and compared to the parameter.
template <typename Param, typename X>
concept StoresCallerValue = EqualityComparableWithValue<Param, std::decay_t<X>> && std::is_constructible_v<std::decay_t<X>, X>;

// Builds the matcher for one `.with(value)` argument. Equality is only
// required here, so methods whose parameters cannot be compared can still be
// mocked. The caller's value is stored (decayed) unless converting it to the
// parameter type is the only way to compare or cannot dangle.
template <typename Param, typename X> ArgPredicate<Param> expected_argument(X &&value) {
    using Value = std::decay_t<X>;
    if constexpr (TextParameter<Param> && TextValue<Value>) {
        const std::decay_t<X>     &source = value;
        const auto                 view   = text_of(source);
        std::optional<std::string> expected;
        if (view)
            expected.emplace(*view);
        ArgPredicate<Param> out;
        out.test = [expected](const Param &actual) -> bool {
            const auto text = text_of(actual);
            return expected.has_value() == text.has_value() && (!text || *text == std::string_view(*expected));
        };
        out.describe = [expected](const Param &actual) {
            return fmt::format("expected {}, got {}", describe_text(expected ? std::optional<std::string_view>(*expected) : std::nullopt),
                               describe_text(text_of(actual)));
        };
        return out;
    } else if constexpr (ConvertsToParameter<Param, X> &&
                         (std::is_same_v<Value, Param> || std::is_scalar_v<Param> || !StoresCallerValue<Param, X>)) {
        return expected_value_predicate<Param, Param>(make_expected_value<Param>(std::forward<X>(value)));
    } else if constexpr (StoresCallerValue<Param, X>) {
        return expected_value_predicate<Param, Value>(make_expected_value<Value>(std::forward<X>(value)));
    } else {
        static_assert(StoresCallerValue<Param, X>,
                      "with(): the mocked parameter cannot be compared to this value with ==, or the value cannot be stored; use "
                      "where()/where_call() instead");
        return {};
    }
}

template <std::size_t I = 0, typename PredicateTuple, typename ActualTuple>
bool tuple_args_match_expected(const PredicateTuple &preds, std::string_view method_name, const ActualTuple &actual,
                               const std::source_location &location) {
    if constexpr (I == std::tuple_size_v<std::remove_reference_t<PredicateTuple>>) {
        return true;
    } else {
        const auto &ap = std::get<I>(preds);
        const auto &a  = std::get<I>(actual);
        if (!ap.test(a)) {
            record_mock_failure(fmt::format("argument[{}] mismatch for {}: {}", I, method_name, ap.describe(a)), location);
            return false;
        }
        return tuple_args_match_expected<I + 1>(preds, method_name, actual, location);
    }
}

template <std::size_t I = 0, typename PredicateTuple, typename ActualTuple>
bool tuple_args_match_predicates(const PredicateTuple &preds, std::string_view method_name, const ActualTuple &actual,
                                 const std::source_location &location) {
    if constexpr (I == std::tuple_size_v<std::remove_reference_t<PredicateTuple>>) {
        return true;
    } else {
        const auto &ap = std::get<I>(preds);
        const auto &a  = std::get<I>(actual);
        if (const auto result = ::gentest::detail::matching::evaluate_predicate(ap, a); !result.matched) {
            const auto &msg = result.explanation;
            record_mock_failure(fmt::format("argument[{}] mismatch for {}: {}", I, method_name, msg), location);
            return false;
        }
        return tuple_args_match_predicates<I + 1>(preds, method_name, actual, location);
    }
}

template <typename TuplePred, typename... A>
bool check_args_equal(const std::optional<TuplePred> &expected, std::string_view method_name, const std::source_location &location,
                      const A &...actual) {
    if (!expected)
        return true;
    const auto actual_tuple = std::forward_as_tuple(actual...);
    return tuple_args_match_expected(*expected, method_name, actual_tuple, location);
}

template <typename TuplePred, typename... A>
bool check_args_by_predicates(const std::optional<TuplePred> &preds, std::string_view method_name, const std::source_location &location,
                              const A &...actual) {
    if (!preds)
        return true;
    const auto actual_tuple = std::forward_as_tuple(actual...);
    return tuple_args_match_predicates(*preds, method_name, actual_tuple, location);
}

inline void verify_calls_or_fail(std::size_t expected, std::size_t observed, std::string_view method_name, bool &already_verified,
                                 const std::source_location &location) {
    if (already_verified)
        return;
    already_verified = true;
    if (observed < expected) {
        record_mock_failure(fmt::format("expected {} call(s) to {} but observed {}", expected, method_name, observed), location);
    }
}

// A call must produce a value but nothing can: the method returns a reference
// or a type that cannot be value-initialized and no action supplies it. The
// failure recorded so far would be lost with the process, so print it first.
[[noreturn]] inline void terminate_without_result(std::string_view method_name, std::string_view cause, bool returns_reference,
                                                  const std::source_location &location) {
    std::string message = fmt::format("gentest: {} {}, and no result can be produced: the method returns {}; configure {} or invokes()",
                                      cause, method_name, returns_reference ? "a reference" : "a type that is not default-constructible",
                                      returns_reference ? "returns_ref()" : "returns()");
    if (location.line() != 0)
        message += fmt::format(" (expectation at {})", ::gentest::detail::loc_to_string(location));
    fmt::print(stderr, "{}; terminating\n", message);
    (void)std::fflush(stderr);
    std::terminate();
}

template <typename R>
concept ValueInitializableResult = requires { R{}; };

template <typename R> R default_result(std::string_view method_name, std::string_view cause, const std::source_location &location) {
    if constexpr (std::is_void_v<R>) {
        (void)method_name;
        (void)cause;
        (void)location;
    } else if constexpr (!std::is_reference_v<R> && ValueInitializableResult<R>) {
        (void)method_name;
        (void)cause;
        (void)location;
        return R{};
    } else {
        terminate_without_result(method_name, cause, std::is_reference_v<R>, location);
    }
}

template <typename... Args> struct ExpectationCommon : ExpectationBase {
    using ArgPredicates = std::tuple<ArgPredicate<std::decay_t<Args>>...>;

    std::size_t expected_calls = 1;
    std::size_t observed_calls = 0;
    bool        allow_excess   = false;
    // `.with(...)` values and `.where(...)` matchers are both stored
    // type-erased per argument, so mocking a method never requires its
    // parameters to be copyable, comparable, or even complete objects.
    std::optional<ArgPredicates>                       expected_args;
    std::optional<ArgPredicates>                       arg_predicates;
    std::function<bool(const std::decay_t<Args> &...)> call_predicate;
    std::shared_ptr<std::atomic<bool>>                 runtime_started;
    // Configure expectations before worker threads start. Runtime dispatch and
    // verification keep queue/counter mutation in InstanceState and never hold
    // this mutex across user actions.
    mutable std::recursive_mutex state_mtx_;

    void verify(std::string_view method_name) override {
        verify_calls_or_fail(expected_calls, observed_calls, method_name, this->already_verified, this->location);
    }

    bool allow_mutation(std::string_view method_name) const {
        if (runtime_started && runtime_started->load(std::memory_order_acquire)) {
            record_mock_failure(
                fmt::format(
                    "Cannot configure mock {} after its first invocation or verification. Configure expectations before using the mock.",
                    method_name),
                this->location);
            return false;
        }
        return true;
    }

    void set_expected_calls(std::string_view method_name, std::size_t expected) {
        std::lock_guard<std::recursive_mutex> lk(state_mtx_);
        if (!allow_mutation(method_name))
            return;
        expected_calls = expected;
    }

    template <typename... X> void set_expected(std::string_view method_name, X &&...values) {
        static_assert(sizeof...(X) == sizeof...(Args), "with() arity must match mocked method");
        std::lock_guard<std::recursive_mutex> lk(state_mtx_);
        if (!allow_mutation(method_name))
            return;
        expected_args = ArgPredicates(expected_argument<std::decay_t<Args>>(std::forward<X>(values))...);
    }

    template <typename... P> void set_predicates(std::string_view method_name, P &&...preds) {
        std::lock_guard<std::recursive_mutex> lk(state_mtx_);
        if (!allow_mutation(method_name))
            return;
        arg_predicates = ArgPredicates(to_arg_predicate<std::decay_t<Args>>(std::forward<P>(preds))...);
    }

    void set_call_predicate(std::string_view method_name, std::function<bool(const std::decay_t<Args> &...)> predicate) {
        std::lock_guard<std::recursive_mutex> lk(state_mtx_);
        if (!allow_mutation(method_name))
            return;
        call_predicate = std::move(predicate);
    }

    void set_allow_excess(std::string_view method_name, bool enabled) {
        std::lock_guard<std::recursive_mutex> lk(state_mtx_);
        if (!allow_mutation(method_name))
            return;
        allow_excess = enabled;
    }

    bool check_args(std::string_view method_name, const std::decay_t<Args> &...actual) {
        std::lock_guard<std::recursive_mutex> lk(state_mtx_);
        if (call_predicate) {
            if (!call_predicate(actual...)) {
                record_mock_failure(fmt::format("call predicate mismatch for {}", method_name), this->location);
                return false;
            }
            return true;
        }
        if (arg_predicates)
            return check_args_by_predicates(arg_predicates, method_name, this->location, actual...);
        return check_args_equal(expected_args, method_name, this->location, actual...);
    }
};

template <typename R, typename... Args> struct Expectation<R(Args...)> : ExpectationCommon<Args...> {
    // Actions receive each argument as the mocked method declares it, so they
    // can write through non-const references and take ownership of by-value
    // or rvalue arguments.
    using Action = std::function<R(Args &&...)>;

    Action action;

    void set_action(std::string_view method_name, Action next_action) {
        std::lock_guard<std::recursive_mutex> lk(this->state_mtx_);
        if (!this->allow_mutation(method_name))
            return;
        action = std::move(next_action);
    }

    // Expectations are frozen before the first dispatch, so the action is
    // invoked in place: a stateful action keeps its state across calls.
    R invoke(std::string_view method_name, Args &&...args) {
        (void)this->check_args(method_name, args...);
        if (action) {
            return action(std::forward<Args>(args)...);
        }
        return default_result<R>(method_name, "no action is configured for", this->location);
    }
};

// Adapts a user callable to an expectation action. The callable is tried with
// the declared argument types first and with const references otherwise.
template <typename R, typename... Args> struct ActionCall {
    template <typename Callable> static R call(Callable &callable, Args &&...args) {
        if constexpr (std::is_invocable_v<Callable &, Args...>) {
            if constexpr (std::is_void_v<R>) {
                (void)callable(std::forward<Args>(args)...);
            } else {
                return callable(std::forward<Args>(args)...);
            }
        } else {
            static_assert(std::is_invocable_v<Callable &, const std::decay_t<Args> &...>,
                          "invokes(): the callable cannot be called with the mocked method's arguments");
            if constexpr (std::is_void_v<R>) {
                (void)callable(static_cast<const std::decay_t<Args> &>(args)...);
            } else {
                return callable(static_cast<const std::decay_t<Args> &>(args)...);
            }
        }
    }
};

class InstanceState {
  public:
    InstanceState();
    InstanceState(const InstanceState &)            = delete;
    InstanceState &operator=(const InstanceState &) = delete;

    ~InstanceState();

    void set_nice(bool v) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (frozen_) {
            record_mock_failure(
                "Cannot configure mock mode after its first invocation or verification. Configure the mock before using it.",
                ::gentest::detail::mock_configuration_location_storage());
            return;
        }
        nice_mode_ = v;
    }
    bool nice() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return nice_mode_;
    }

    // Called from the generated (implicitly noexcept) mock destructor, so
    // failures are recorded without throwing, including during measured calls.
    void verify_all() {
        ::gentest::detail::DeferredFailureScope deferred;
        std::lock_guard<std::mutex>             lk(mtx_);
        runtime_started_->store(true, std::memory_order_release);
        frozen_ = true;
        for (auto &[_, entry] : methods_) {
            for (auto &expectation : entry.expectations) {
                expectation->verify(entry.method_name);
            }
        }
    }

    template <typename MethodPtr> MethodIdentity identify(MethodPtr ptr) { return MethodIdentity::from(ptr); }

    template <typename R, typename... Args>
    std::shared_ptr<Expectation<R(Args...)>> push_expectation(const MethodIdentity &id, std::string method_name) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (frozen_) {
            record_mock_failure(
                fmt::format(
                    "Cannot configure mock {} after its first invocation or verification. Configure expectations before using the mock.",
                    method_name),
                ::gentest::detail::mock_configuration_location_storage());
            return {};
        }
        auto &entry = methods_[id];
        if (entry.method_name.empty())
            entry.method_name = std::move(method_name);
        auto expectation             = std::make_shared<Expectation<R(Args...)>>();
        expectation->signature       = TypeToken<R(Args...)>::text;
        expectation->runtime_started = runtime_started_;
        expectation->location        = ::gentest::detail::mock_configuration_location_storage();
        entry.expectations.push_back(expectation);
        return expectation;
    }

    template <typename R, typename... Args> R dispatch(const MethodIdentity &id, std::string_view method_name, Args &&...args) {
        return dispatch_with_fallback<R, Args...>(id, id, method_name, std::forward<Args>(args)...);
    }

    template <typename R, typename... Args>
    R dispatch_with_fallback(const MethodIdentity &id, const MethodIdentity &fallback_id, std::string_view method_name, Args &&...args) {
        using ExpectationT                    = Expectation<R(Args...)>;
        bool                 nice_mode        = false;
        bool                 unexpected       = false;
        bool                 has_expectations = false;
        bool                 foreign          = false;
        ExpectationT        *expectation      = nullptr;
        std::source_location last_expectation_location;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            runtime_started_->store(true, std::memory_order_release);
            frozen_                         = true;
            const auto try_take_expectation = [&](const MethodIdentity &candidate) -> bool {
                auto it = methods_.find(candidate);
                if (it != methods_.end() && !it->second.expectations.empty()) {
                    has_expectations          = true;
                    last_expectation_location = it->second.expectations.back()->location;
                }
                if (it == methods_.end() || it->second.next_expectation >= it->second.expectations.size()) {
                    return false;
                }
                // Reuse the stored expectation object directly. Once a mock is
                // frozen, the owning deque keeps entries alive for the rest of
                // the mock lifetime, so a raw pointer avoids fragile shared_ptr
                // copy/assignment emission in downstream module consumers.
                ExpectationBase *const candidate_expectation = it->second.expectations[it->second.next_expectation].get();
                // The cast below is only valid for an expectation created for
                // this exact signature.
                if (!same_token(candidate_expectation->signature, TypeToken<R(Args...)>::text)) {
                    foreign                   = true;
                    last_expectation_location = candidate_expectation->location;
                    return false;
                }
                expectation = static_cast<ExpectationT *>(candidate_expectation);
                unexpected  = !expectation->allow_excess && expectation->observed_calls >= expectation->expected_calls;
                ++expectation->observed_calls;
                if (!expectation->allow_excess && expectation->observed_calls >= expectation->expected_calls) {
                    ++it->second.next_expectation;
                }
                return true;
            };

            const bool matched = try_take_expectation(id) || (!(fallback_id == id) && try_take_expectation(fallback_id));
            // Nice mode only relaxes methods without expectations. Calls beyond
            // exhausted expectations fail as in strict mode.
            if (!matched && !has_expectations) {
                nice_mode = nice_mode_;
            }
        }

        if (!expectation) {
            if (foreign) {
                record_mock_failure(
                    fmt::format("expectation for {} was configured for a different method signature and cannot receive this call",
                                method_name),
                    last_expectation_location);
            } else if (!nice_mode) {
                record_mock_failure(fmt::format("unexpected call to {}", method_name), last_expectation_location);
            }
            return default_result<R>(method_name, nice_mode ? "no expectation is configured for" : "unexpected call to",
                                     last_expectation_location);
        }
        if (unexpected) {
            record_mock_failure(fmt::format("unexpected call to {}", method_name), expectation->location);
        }
        return expectation->invoke(method_name, std::forward<Args>(args)...);
    }

  private:
    struct MethodEntry {
        std::string method_name;
        // Keep a single stable container for both dispatch and verification.
        std::deque<std::shared_ptr<ExpectationBase>> expectations;
        std::size_t                                  next_expectation = 0;
    };

    mutable std::mutex                                                  mtx_;
    std::unordered_map<MethodIdentity, MethodEntry, MethodIdentityHash> methods_;
    std::shared_ptr<std::atomic<bool>>                                  runtime_started_ = std::make_shared<std::atomic<bool>>(false);
    bool                                                                nice_mode_       = false;
    bool                                                                frozen_          = false;
};

// Keep these out-of-class so GCC module consumers emit concrete special-member
// definitions for imported mocks instead of referencing a missing defaulted
// constructor symbol from downstream package builds.
inline InstanceState::InstanceState()  = default;
inline InstanceState::~InstanceState() = default;

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
        require_configurable();
        if (expectation_)
            expectation_->set_expected_calls(method_name_, expected);
        return *this;
    }

    template <typename Callable> ExpectationHandle &invokes(Callable &&callable) {
        require_configurable();
        if (expectation_) {
            using Fn = std::decay_t<Callable>;
            if constexpr (std::is_copy_constructible_v<Fn>) {
                expectation_->set_action(method_name_, [fn = Fn(std::forward<Callable>(callable))](Args &&...a) mutable -> R {
                    return ActionCall<R, Args...>::call(fn, std::forward<Args>(a)...);
                });
            } else {
                // std::function needs a copyable target; share the move-only callable.
                expectation_->set_action(method_name_, [fn = std::make_shared<Fn>(std::forward<Callable>(callable))](Args &&...a) -> R {
                    return ActionCall<R, Args...>::call(*fn, std::forward<Args>(a)...);
                });
            }
        }
        return *this;
    }

    template <typename... X> ExpectationHandle &with(X &&...expected) {
        require_configurable();
        if (expectation_) {
            expectation_->set_expected(method_name_, std::forward<X>(expected)...);
        }
        return *this;
    }

    template <typename... P> ExpectationHandle &where_args(P &&...predicates) {
        static_assert(sizeof...(P) == sizeof...(Args), "where_args arity must match mocked method");
        require_configurable();
        if (expectation_) {
            expectation_->set_predicates(method_name_, std::forward<P>(predicates)...);
        }
        return *this;
    }

    // Convenience alias to mirror common matcher API naming
    template <typename... P> ExpectationHandle &where(P &&...predicates) { return where_args(std::forward<P>(predicates)...); }

    template <typename Callable> ExpectationHandle &where_call(Callable &&call_pred) {
        require_configurable();
        if (expectation_) {
            expectation_->set_call_predicate(method_name_,
                                             std::function<bool(const std::decay_t<Args> &...)>(std::forward<Callable>(call_pred)));
        }
        return *this;
    }

    template <typename Value> ExpectationHandle &returns(Value &&value) {
        require_configurable();
        if constexpr (std::is_void_v<R>) {
            static_assert(!std::is_void_v<R>, "returns() is not available for void-returning methods");
        } else if constexpr (std::is_reference_v<R>) {
            static_assert(!std::is_reference_v<R>, "returns() is not available for reference-returning methods; use returns_ref()");
        } else {
            using Stored = std::decay_t<Value>;
            if constexpr (std::is_copy_constructible_v<Stored> && std::is_convertible_v<const Stored &, R>) {
                if (expectation_) {
                    expectation_->set_action(method_name_, [captured = Stored(std::forward<Value>(value))](Args &&...a) -> R {
                        (void)sizeof...(a);
                        return captured;
                    });
                }
            } else {
                // A move-only result is handed to the first call that returns it.
                static_assert(std::is_convertible_v<Stored &&, R>, "returns(): the value is not convertible to the method's return type");
                if (expectation_) {
                    expectation_->set_action(method_name_,
                                             [captured = std::make_shared<Stored>(std::forward<Value>(value))](Args &&...a) -> R {
                                                 (void)sizeof...(a);
                                                 return std::move(*captured);
                                             });
                }
            }
        }
        return *this;
    }

    template <typename RR = R> ExpectationHandle &returns_ref(std::remove_reference_t<RR> &value) {
        require_configurable();
        if constexpr (!std::is_lvalue_reference_v<RR>) {
            static_assert(std::is_lvalue_reference_v<RR>, "returns_ref() is only available for lvalue-reference returning methods");
        } else {
            if (expectation_) {
                auto *ptr = std::addressof(value);
                expectation_->set_action(method_name_, [ptr](Args &&...a) -> R {
                    (void)sizeof...(a);
                    return *ptr;
                });
            }
        }
        return *this;
    }

    ExpectationHandle &allow_more(bool enabled = true) {
        require_configurable();
        if (expectation_)
            expectation_->set_allow_excess(method_name_, enabled);
        return *this;
    }

  private:
    static void require_configurable() { ::gentest::detail::require_not_adopted_context("mock expectation configured"); }

    std::shared_ptr<Expectation<R(Args...)>> expectation_;
    std::string                              method_name_;
};

template <typename T> struct MethodTraits;

template <typename R, typename... Args> struct MethodTraits<R(Args...)> {
    using Signature = R(Args...);
};

template <typename R, typename... Args> struct MethodTraits<R (*)(Args...)> : MethodTraits<R(Args...)> {};
template <typename R, typename... Args> struct MethodTraits<R (*)(Args...) noexcept> : MethodTraits<R(Args...)> {};

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

template <typename Class, typename R, typename... Args> struct MethodTraits<R (Class::*)(Args...) &> : MethodTraits<R(Args...)> {};
template <typename Class, typename R, typename... Args> struct MethodTraits<R (Class::*)(Args...) & noexcept> : MethodTraits<R(Args...)> {};
template <typename Class, typename R, typename... Args> struct MethodTraits<R (Class::*)(Args...) const &> : MethodTraits<R(Args...)> {};
template <typename Class, typename R, typename... Args>
struct MethodTraits<R (Class::*)(Args...) const & noexcept> : MethodTraits<R(Args...)> {};
template <typename Class, typename R, typename... Args> struct MethodTraits<R (Class::*)(Args...) volatile &> : MethodTraits<R(Args...)> {};
template <typename Class, typename R, typename... Args>
struct MethodTraits<R (Class::*)(Args...) volatile & noexcept> : MethodTraits<R(Args...)> {};
template <typename Class, typename R, typename... Args>
struct MethodTraits<R (Class::*)(Args...) const volatile &> : MethodTraits<R(Args...)> {};
template <typename Class, typename R, typename... Args>
struct MethodTraits<R (Class::*)(Args...) const volatile & noexcept> : MethodTraits<R(Args...)> {};

template <typename Class, typename R, typename... Args> struct MethodTraits<R (Class::*)(Args...) &&> : MethodTraits<R(Args...)> {};
template <typename Class, typename R, typename... Args>
struct MethodTraits<R (Class::*)(Args...) && noexcept> : MethodTraits<R(Args...)> {};
template <typename Class, typename R, typename... Args> struct MethodTraits<R (Class::*)(Args...) const &&> : MethodTraits<R(Args...)> {};
template <typename Class, typename R, typename... Args>
struct MethodTraits<R (Class::*)(Args...) const && noexcept> : MethodTraits<R(Args...)> {};
template <typename Class, typename R, typename... Args>
struct MethodTraits<R (Class::*)(Args...) volatile &&> : MethodTraits<R(Args...)> {};
template <typename Class, typename R, typename... Args>
struct MethodTraits<R (Class::*)(Args...) volatile && noexcept> : MethodTraits<R(Args...)> {};
template <typename Class, typename R, typename... Args>
struct MethodTraits<R (Class::*)(Args...) const volatile &&> : MethodTraits<R(Args...)> {};
template <typename Class, typename R, typename... Args>
struct MethodTraits<R (Class::*)(Args...) const volatile && noexcept> : MethodTraits<R(Args...)> {};

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
        using Signature = typename mocking::MethodTraits<MethodPtr>::Signature;
        return mocking::ExpectationHandle<Signature>{};
    }
    template <auto Method> static auto expect_constant(Mock &, std::string_view method_name) {
        using Signature = typename mocking::MethodTraits<decltype(Method)>::Signature;
        return mocking::ExpectationHandle<Signature>{{}, std::string(method_name)};
    }
    static void set_nice(Mock &, bool) {}
#endif
};

} // namespace detail

template <class Mock, class MethodPtr>
auto expect(Mock &instance, MethodPtr method, const std::source_location &location = std::source_location::current()) {
    ::gentest::detail::require_not_adopted_context("mock expectation configured");
    ::gentest::detail::MockConfigurationLocationScope source(location);
    return detail::MockAccess<std::remove_cvref_t<Mock>>::expect(instance, method);
}

template <auto Method, class Mock>
auto expect(Mock &instance, std::string_view method_name, const std::source_location &location = std::source_location::current()) {
    ::gentest::detail::require_not_adopted_context("mock expectation configured");
    ::gentest::detail::MockConfigurationLocationScope source(location);
    return detail::MockAccess<std::remove_cvref_t<Mock>>::template expect_constant<Method>(instance, method_name);
}

template <class Mock>
void make_nice(Mock &instance, bool v = true, const std::source_location &location = std::source_location::current()) {
    ::gentest::detail::require_not_adopted_context("mock mode configured");
    ::gentest::detail::MockConfigurationLocationScope source(location);
    detail::MockAccess<std::remove_cvref_t<Mock>>::set_nice(instance, v);
}
template <class Mock> void make_strict(Mock &instance, const std::source_location &location = std::source_location::current()) {
    ::gentest::detail::require_not_adopted_context("mock mode configured");
    ::gentest::detail::MockConfigurationLocationScope source(location);
    detail::MockAccess<std::remove_cvref_t<Mock>>::set_nice(instance, false);
}

// Convenience macros to configure expectations with a terse syntax.
// Usage: EXPECT_CALL(mock, method).times(2)...
#ifndef GENTEST_NO_EXPECT_CALL_MACROS
#define EXPECT_CALL(instance, method)                                                                                                      \
    (::gentest::expect<&std::remove_reference_t<decltype(instance)>::GentestTarget::method>((instance), #method))
#define ASSERT_CALL(instance, method) EXPECT_CALL(instance, method)
#endif

} // namespace gentest

#ifndef GENTEST_NO_AUTO_MOCK_INCLUDE
#include "gentest/mock_codegen.h"
#endif

#endif // GENTEST_MOCK_FORWARD_ONLY
