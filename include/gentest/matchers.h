#pragma once

#include "gentest/format_value.h"

#include <cmath>
#include <concepts>
#include <functional>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace gentest::detail::matching {
struct MatchResult {
    bool        matched = true;
    std::string explanation;
};

template <typename T> struct ArgPredicate {
    std::function<bool(const T &)>        test;
    std::function<std::string(const T &)> describe; // optional; may be empty
    std::function<MatchResult(const T &)> evaluate; // optional single evaluation with explanation
};

template <typename T, typename P>
concept HasMakeFor = requires(const P &p) {
    { p.template make<T>() } -> std::same_as<ArgPredicate<T>>;
};

template <typename T, typename P> ArgPredicate<T> to_arg_predicate(P &&p) {
    if constexpr (std::is_same_v<std::remove_cvref_t<P>, ArgPredicate<T>>) {
        return std::forward<P>(p);
    } else if constexpr (HasMakeFor<T, P>) {
        return p.template make<T>();
    } else {
        ArgPredicate<T> out;
        out.test     = std::function<bool(const T &)>{std::forward<P>(p)};
        out.describe = [](const T &) { return std::string("predicate mismatch"); };
        return out;
    }
}

template <typename T> auto evaluate_predicate(const ArgPredicate<T> &predicate, const T &actual) -> MatchResult {
    if (predicate.evaluate)
        return predicate.evaluate(actual);
    if (!predicate.test)
        return {false, "matcher has no test callback"};
    if (predicate.test(actual))
        return {};
    return {false, predicate.describe ? predicate.describe(actual) : "predicate mismatch"};
}

template <typename T, bool All, typename Tuple> auto evaluate_composition(const Tuple &matchers, const T &actual) -> MatchResult {
    MatchResult result{All, {}};
    std::size_t index = 0;
    const auto  check = [&](const auto &matcher) {
        const auto current = index++;
        if (result.matched != All)
            return;
        const auto next = evaluate_predicate(to_arg_predicate<T>(matcher), actual);
        if constexpr (All) {
            if (!next.matched)
                result = {false, fmt::format("matcher[{}]: {}", current, next.explanation)};
        } else {
            if (next.matched)
                result = {};
            else
                result.explanation +=
                    fmt::format("{}alternative[{}]: {}", result.explanation.empty() ? "" : "; ", current, next.explanation);
        }
    };
    std::apply([&](const auto &...items) { (check(items), ...); }, matchers);
    if (!result.matched && result.explanation.empty())
        result.explanation = "no alternative matchers";
    return result;
}

template <typename T> auto format_match_value(const T &value) -> std::string {
    if constexpr (std::is_function_v<std::remove_pointer_t<T>> && ::gentest::detail::ValueFormattable<T>)
        return ::gentest::detail::format_printable_value(value, false);
    return ::gentest::format_value(value);
}

} // namespace gentest::detail::matching

namespace gentest {
namespace match {
using ::gentest::detail::matching::ArgPredicate;
using ::gentest::detail::matching::MatchResult;

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
    Value                                 expected;
    template <typename T> ArgPredicate<T> make() const {
        ArgPredicate<T> ap;
        const Value     exp = expected;
        ap.test             = [exp](const T &a) { return a == exp; };
        ap.describe         = [exp](const T &a) {
            return fmt::format("expected == {}, got {}", ::gentest::detail::matching::format_match_value(exp),
                               ::gentest::detail::matching::format_match_value(a));
        };
        return ap;
    }
};
template <typename V> inline auto Eq(V &&v) { return EqFactory<V>{std::forward<V>(v)}; }

template <typename A, typename B> struct InRangeFactory {
    using Lo = std::decay_t<A>;
    using Hi = std::decay_t<B>;
    Lo                                    l;
    Hi                                    h;
    template <typename T> ArgPredicate<T> make() const {
        ArgPredicate<T> ap;
        const Lo        lo = l;
        const Hi        hi = h;
        ap.test            = [lo, hi](const T &a) { return (a >= lo) && (a <= hi); };
        ap.describe        = [lo, hi](const T &a) {
            return fmt::format("expected in [{}, {}], got {}", ::gentest::format_value(lo), ::gentest::format_value(hi),
                               ::gentest::detail::matching::format_match_value(a));
        };
        return ap;
    }
};
template <typename A, typename B> inline auto InRange(A &&lo, B &&hi) {
    return InRangeFactory<A, B>{std::forward<A>(lo), std::forward<B>(hi)};
}

template <typename P> struct NotFactory {
    P                                     inner;
    template <typename T> ArgPredicate<T> make() const {
        ArgPredicate<T> ip = detail::matching::to_arg_predicate<T>(inner);
        ArgPredicate<T> ap;
        ap.evaluate = [ip](const T &a) -> detail::matching::MatchResult {
            if (!detail::matching::evaluate_predicate(ip, a).matched)
                return {};
            return {false, "negated matcher matched " + gentest::format_value(a)};
        };
        ap.test     = [evaluate = ap.evaluate](const T &a) { return evaluate(a).matched; };
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
    Value                                 bound;
    template <typename T> ArgPredicate<T> make() const {
        ArgPredicate<T> ap;
        const Value     b = bound;
        ap.test           = [b](const T &a) { return a >= b; };
        ap.describe       = [b](const T &a) {
            return fmt::format("expected >= {}, got {}", ::gentest::format_value(b), ::gentest::detail::matching::format_match_value(a));
        };
        return ap;
    }
};
template <typename V> inline auto Ge(V &&v) { return GeFactory<V>{std::forward<V>(v)}; }

template <typename V> struct LeFactory {
    using Value = std::decay_t<V>;
    Value                                 bound;
    template <typename T> ArgPredicate<T> make() const {
        ArgPredicate<T> ap;
        const Value     b = bound;
        ap.test           = [b](const T &a) { return a <= b; };
        ap.describe       = [b](const T &a) {
            return fmt::format("expected <= {}, got {}", ::gentest::format_value(b), ::gentest::detail::matching::format_match_value(a));
        };
        return ap;
    }
};
template <typename V> inline auto Le(V &&v) { return LeFactory<V>{std::forward<V>(v)}; }

template <typename V> struct GtFactory {
    using Value = std::decay_t<V>;
    Value                                 bound;
    template <typename T> ArgPredicate<T> make() const {
        ArgPredicate<T> ap;
        const Value     b = bound;
        ap.test           = [b](const T &a) { return a > b; };
        ap.describe       = [b](const T &a) {
            return fmt::format("expected > {}, got {}", ::gentest::format_value(b), ::gentest::detail::matching::format_match_value(a));
        };
        return ap;
    }
};
template <typename V> inline auto Gt(V &&v) { return GtFactory<V>{std::forward<V>(v)}; }

template <typename V> struct LtFactory {
    using Value = std::decay_t<V>;
    Value                                 bound;
    template <typename T> ArgPredicate<T> make() const {
        ArgPredicate<T> ap;
        const Value     b = bound;
        ap.test           = [b](const T &a) { return a < b; };
        ap.describe       = [b](const T &a) {
            return fmt::format("expected < {}, got {}", ::gentest::format_value(b), ::gentest::detail::matching::format_match_value(a));
        };
        return ap;
    }
};
template <typename V> inline auto Lt(V &&v) { return LtFactory<V>{std::forward<V>(v)}; }

template <typename V, typename E> struct NearFactory {
    using Value = std::decay_t<V>;
    using Eps   = std::decay_t<E>;
    Value                                 expected;
    Eps                                   eps;
    template <typename T> ArgPredicate<T> make() const {
        ArgPredicate<T> ap;
        const Value     exp = expected;
        const Eps       e   = eps;
        ap.test             = [exp, e](const T &a) {
            using std::abs;
            return abs(static_cast<long double>(a) - static_cast<long double>(exp)) <= static_cast<long double>(e);
        };
        ap.describe = [exp, e](const T &a) {
            return fmt::format("expected near {} ± {}, got {}", ::gentest::detail::matching::format_match_value(exp),
                               ::gentest::format_value(e), ::gentest::detail::matching::format_match_value(a));
        };
        return ap;
    }
};
template <typename V, typename E> inline auto Near(V &&v, E &&eps) { return NearFactory<V, E>{std::forward<V>(v), std::forward<E>(eps)}; }

template <typename T> inline std::optional<std::string_view> to_string_view_safe(const T &a) {
    using D = std::decay_t<T>;
    if constexpr (std::is_null_pointer_v<D>) {
        return std::nullopt;
    } else if constexpr (std::is_array_v<T>) {
        return detail::diagnostic_string_view(a);
    } else if constexpr (std::is_same_v<D, const char *> || std::is_same_v<D, char *>) {
        if (a == nullptr)
            return std::nullopt;
        return std::string_view(a);
    } else {
        return std::string_view(a);
    }
}

struct StrContainsFactory {
    std::string                           needle;
    template <typename T> ArgPredicate<T> make() const {
        ArgPredicate<T>   ap;
        const std::string nd = needle;
        ap.test              = [nd](const T &a) {
            auto s = to_string_view_safe(a);
            if (!s)
                return false;
            return s->find(nd) != std::string_view::npos;
        };
        ap.describe = [nd](const T &a) {
            auto s = to_string_view_safe(a);
            if (!s)
                return fmt::format("expected substring '{}', got <null>", nd);
            return fmt::format("expected substring '{}', got '{}'", nd, *s);
        };
        return ap;
    }
};
inline auto StrContains(std::string needle) { return StrContainsFactory{std::move(needle)}; }

struct StartsWithFactory {
    std::string                           prefix;
    template <typename T> ArgPredicate<T> make() const {
        ArgPredicate<T>   ap;
        const std::string px = prefix;
        ap.test              = [px](const T &a) {
            auto s = to_string_view_safe(a);
            if (!s)
                return false;
            return s->rfind(px, 0) == 0;
        };
        ap.describe = [px](const T &a) {
            auto s = to_string_view_safe(a);
            if (!s)
                return fmt::format("expected prefix '{}', got <null>", px);
            return fmt::format("expected prefix '{}', got '{}'", px, *s);
        };
        return ap;
    }
};
inline auto StartsWith(std::string prefix) { return StartsWithFactory{std::move(prefix)}; }

struct EndsWithFactory {
    std::string                           suffix;
    template <typename T> ArgPredicate<T> make() const {
        ArgPredicate<T>   ap;
        const std::string sx = suffix;
        ap.test              = [sx](const T &a) {
            auto s = to_string_view_safe(a);
            if (!s)
                return false;
            return s->size() >= sx.size() && s->substr(s->size() - sx.size()) == sx;
        };
        ap.describe = [sx](const T &a) {
            auto s = to_string_view_safe(a);
            if (!s)
                return fmt::format("expected suffix '{}', got <null>", sx);
            return fmt::format("expected suffix '{}', got '{}'", sx, *s);
        };
        return ap;
    }
};
inline auto EndsWith(std::string suffix) { return EndsWithFactory{std::move(suffix)}; }

template <typename... M> struct AnyOfFactory {
    std::tuple<M...>                      subs;
    template <typename T> ArgPredicate<T> make() const {
        ArgPredicate<T> out;
        out.evaluate = [matchers = subs](const T &actual) { return detail::matching::evaluate_composition<T, false>(matchers, actual); };
        out.test     = [evaluate = out.evaluate](const T &actual) { return evaluate(actual).matched; };
        out.describe = [evaluate = out.evaluate](const T &actual) { return evaluate(actual).explanation; };
        return out;
    }
};
template <typename... M> inline auto AnyOf(M &&...m) {
    return AnyOfFactory<std::decay_t<M>...>{std::tuple<std::decay_t<M>...>(std::forward<M>(m)...)};
}

template <typename... M> struct AllOfFactory {
    std::tuple<M...>                      subs;
    template <typename T> ArgPredicate<T> make() const {
        ArgPredicate<T> out;
        out.evaluate = [matchers = subs](const T &actual) { return detail::matching::evaluate_composition<T, true>(matchers, actual); };
        out.test     = [evaluate = out.evaluate](const T &actual) { return evaluate(actual).matched; };
        out.describe = [evaluate = out.evaluate](const T &actual) { return evaluate(actual).explanation; };
        return out;
    }
};
template <typename... M> inline auto AllOf(M &&...m) {
    return AllOfFactory<std::decay_t<M>...>{std::tuple<std::decay_t<M>...>(std::forward<M>(m)...)};
}

template <typename... Expected> struct ElementsAreFactory {
    std::tuple<Expected...> expected;

    template <typename Range> ArgPredicate<Range> make() const {
        static_assert(std::ranges::forward_range<const Range>, "ElementsAre requires a forward range");
        using Element = std::remove_cvref_t<std::ranges::range_reference_t<const Range>>;
        using Value   = std::ranges::range_value_t<const Range>;
        ArgPredicate<Range> out;
        // Keep diagnostics runtime-only when a module importer has different fmt consteval settings.
        out.evaluate = [values = expected](const Range &actual) -> detail::matching::MatchResult {
            auto                          current = std::ranges::begin(actual);
            const auto                    end     = std::ranges::end(actual);
            std::size_t                   index   = 0;
            detail::matching::MatchResult result;
            const auto                    check = [&](const auto &item) {
                if (!result.matched)
                    return;
                if (current == end) {
                    result = {.matched = false, .explanation = fmt::format(fmt::runtime("missing element at index {}"), index)};
                    return;
                }
                const auto &value = *current;
                result            = [&] {
                    // Generic matchers see the iterator's proxy/reference, without copying move-only values.
                    if constexpr (!std::is_same_v<Element, Value> &&
                                  std::is_same_v<std::remove_cvref_t<decltype(item)>, ArgPredicate<Value>>) {
                        return detail::matching::evaluate_predicate<Value>(item, value);
                    } else {
                        const auto predicate = [&] {
                            if constexpr (std::is_same_v<std::remove_cvref_t<decltype(item)>, ArgPredicate<Element>> ||
                                          detail::matching::HasMakeFor<Element, decltype(item)> ||
                                          std::invocable<decltype(item), const Element &>)
                                return detail::matching::to_arg_predicate<Element>(item);
                            else
                                return Eq(item).template make<Element>();
                        }();
                        return detail::matching::evaluate_predicate<Element>(predicate, value);
                    }
                }();
                if (!result.matched) {
                    result.explanation = fmt::format(fmt::runtime("element[{}]: {}"), index, result.explanation);
                    return;
                }
                ++current;
                ++index;
            };
            std::apply([&](const auto &...items) { (check(items), ...); }, values);
            if (result.matched && current != end)
                result = {.matched = false,
                          .explanation =
                              fmt::format(fmt::runtime("unexpected element at index {}: {}"), index, gentest::format_value(*current))};
            return result;
        };
        out.test     = [evaluate = out.evaluate](const Range &actual) { return evaluate(actual).matched; };
        out.describe = [evaluate = out.evaluate](const Range &actual) { return evaluate(actual).explanation; };
        return out;
    }
};

template <typename... Expected> auto ElementsAre(Expected &&...expected) {
    return ElementsAreFactory<std::decay_t<Expected>...>{std::tuple<std::decay_t<Expected>...>(std::forward<Expected>(expected)...)};
}

} // namespace match

} // namespace gentest
