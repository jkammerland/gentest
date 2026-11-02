#include <ostream>

// Preserve ordinary lookup as well as ADL: this overload deliberately precedes
// the formatting header and has no user-defined callback argument type.
using GlobalCallback = void (*)(long);
std::ostream &operator<<(std::ostream &stream, GlobalCallback);

#include "gentest/assertions.h"
#include "gentest/format_value.h"
#include "gentest/mock.h"

#include <cmath>
#include <exception>
#include <fmt/format.h>
#include <iostream>
#include <map>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <typeinfo>
#include <variant>
#include <vector>

struct FmtOnlyValue {
    int value;
};

namespace tuple_protocol {
struct Value {
    int first;
    int second;
};
template <std::size_t Index> const int &get(const Value &value) {
    if constexpr (Index == 0)
        return value.first;
    else
        return value.second;
}
} // namespace tuple_protocol

namespace std {
template <> struct tuple_size<tuple_protocol::Value> : integral_constant<size_t, 2> {};
template <size_t Index> struct tuple_element<Index, tuple_protocol::Value> {
    using type = int;
};
} // namespace std

std::ostream &operator<<(std::ostream &stream, GlobalCallback) { return stream << "global-callback"; }

namespace callback_adl {
struct Argument {};
using Callback = void (*)(Argument);
std::ostream &operator<<(std::ostream &stream, Callback) { return stream << "adl-callback"; }
void          callback(Argument) {}
} // namespace callback_adl

template <> struct fmt::formatter<FmtOnlyValue> : fmt::formatter<std::string_view> {
    auto format(const FmtOnlyValue &value, fmt::format_context &ctx) const {
        return fmt::format_to(ctx.out(), "fmt-value({})", value.value);
    }
};

namespace {

struct StreamOnlyValue {
    int value;
};

std::ostream &operator<<(std::ostream &stream, const StreamOnlyValue &value) { return stream << "stream-value(" << value.value << ')'; }

struct UnprintableValue {};

int           ordinary_callback() { return 1; }
int           noexcept_callback() noexcept { return 2; }
void          global_callback(long) {}
std::ostream &noexcept_manipulator(std::ostream &stream) noexcept { return stream << "noexcept-manipulator"; }

#if defined(_WIN32)
int __stdcall native_callback(int value) { return value; }
#endif

static_assert(fmt::is_formattable<FmtOnlyValue, char>::value);
static_assert(!fmt::is_formattable<StreamOnlyValue, char>::value);
static_assert(!fmt::is_formattable<UnprintableValue, char>::value);

std::string expected_unprintable_value() {
#if defined(__clang__)
#if __has_feature(cxx_rtti)
    return fmt::format("{} (unprintable)", typeid(UnprintableValue).name());
#else
    return "(unprintable, enable RTTI)";
#endif
#elif defined(__GXX_RTTI) || defined(_CPPRTTI)
    return fmt::format("{} (unprintable)", typeid(UnprintableValue).name());
#else
    return "(unprintable, enable RTTI)";
#endif
}

int expect_equal(std::string_view actual, std::string_view expected, std::string_view label) {
    if (actual == expected) {
        return 0;
    }
    std::cerr << label << ": expected '" << expected << "', got '" << actual << "'\n";
    return 1;
}

} // namespace

int main() try {
    int failures = 0;

    // Structural representations are independent of a failed assertion's values.
    failures += expect_equal(gentest::format_value(std::vector{1, 2, 3}), "[1, 2, 3]", "range values");
    const int raw_values[]{1, 2};
    failures += expect_equal(gentest::format_value(raw_values), "[1, 2]", "raw array values");
    failures += expect_equal(gentest::format_value(std::vector<unsigned char>{1, 255}), "[1, 255]", "byte values");
    failures += expect_equal(gentest::format_value(std::byte{255}), "255", "standard byte");
    const auto [first_field, second_field] = tuple_protocol::Value{.first = 1, .second = 2};
    if (first_field != 1 || second_field != 2 ||
        gentest::format_value(tuple_protocol::Value{.first = 1, .second = 2}).find("unprintable") == std::string::npos)
        ++failures;
    failures += expect_equal(gentest::format_value(std::tuple{1, std::string("a\nb"), std::optional<int>{}}), R"((1, "a\nb", nullopt))",
                             "nested tuple");
    failures += expect_equal(gentest::format_value(std::vector<std::optional<int>>{1, std::nullopt}), "[optional(1), nullopt]",
                             "nested optional range");
    failures += expect_equal(gentest::format_value(std::map<std::string, int>{{"key", 9}}), "[(\"key\", 9)]", "map pairs");
    failures += expect_equal(gentest::format_value(std::variant<int, std::string>{std::string("value")}), "variant[1](\"value\")",
                             "variant alternative");
    failures += expect_equal(gentest::format_value(std::string_view("\n\t\0\\\"", 5)), R"("\n\t\0\\\"")", "escaped bytes");
    failures += expect_equal(gentest::format_value('\n'), "'\\n'", "quoted character");
    failures += expect_equal(gentest::format_value(static_cast<const char *>(nullptr)), "nullptr", "null string pointer");
    failures += expect_equal(gentest::format_value(nullptr), "nullptr", "null pointer literal");
    failures += expect_equal(gentest::format_value(gentest::approx::Approx(1).abs(0.25).rel(2)), "Approx(target=1, abs=0.25, rel=2%)",
                             "Approx configuration");

    // Distinct adjacent floating values must remain distinguishable and round trip.
    const auto adjacent = std::nextafter(1.0, 2.0);
    if (gentest::format_value(1.0) == gentest::format_value(adjacent) || std::stod(gentest::format_value(adjacent)) != adjacent)
        ++failures;
    if (gentest::format_value(std::vector<int>(10000, 1)).size() > 150 || gentest::format_value(std::string(10000, 'x')).size() > 550)
        ++failures;
    if (gentest::format_value(std::string(511, 'x') + "\xc3\xa9").find('\xc3') != std::string::npos)
        ++failures;
    std::istringstream source("1 2 3");
    auto               single_pass = std::ranges::istream_view<int>(source);
    (void)gentest::format_value(single_pass);
    int first = 0;
    source >> first;
    if (first != 1)
        ++failures;

    failures += expect_equal(gentest::format_value(FmtOnlyValue{42}), "fmt-value(42)", "fmt formatter");
    failures += expect_equal(gentest::format_value(StreamOnlyValue{7}), "stream-value(7)", "stream fallback");
    failures += expect_equal(gentest::format_value(UnprintableValue{}), expected_unprintable_value(), "unprintable fallback");
    failures += expect_equal(gentest::format_value(true), "true", "bool diagnostic spelling");
    using Callback          = int (*)();
    const Callback callback = &ordinary_callback;
    const Callback absent   = nullptr;
    failures += expect_equal(gentest::format_value(callback), "true", "function pointer diagnostic");
    failures += expect_equal(gentest::format_value(absent), "false", "null function pointer diagnostic");
    failures += expect_equal(gentest::format_value(&noexcept_callback), "true", "noexcept function pointer");
    failures += expect_equal(gentest::format_value(static_cast<decltype(&noexcept_callback)>(nullptr)), "false", "null noexcept pointer");
    const auto predicate = gentest::match::Eq(callback).make<Callback>();
    if (!predicate.test(callback) || predicate.test(absent)) {
        std::cerr << "function pointer predicate selected the wrong value\n";
        ++failures;
    }
    failures += expect_equal(predicate.describe(absent), "expected == 1, got 0", "function pointer mock diagnostic");
    failures += expect_equal(gentest::format_value(&global_callback), "global-callback", "ordinary custom insertion");
    failures += expect_equal(gentest::format_value(&callback_adl::callback), "adl-callback", "ADL custom insertion");
    failures += expect_equal(gentest::format_value(&std::hex), "", "ios_base manipulator");
    failures += expect_equal(gentest::format_value(&std::endl<char, std::char_traits<char>>), "\n", "ostream manipulator");
    failures += expect_equal(gentest::format_value(&noexcept_manipulator), "noexcept-manipulator", "noexcept manipulator");
#if defined(_WIN32)
    failures += expect_equal(gentest::format_value(&native_callback), "true", "Windows calling convention pointer");
    failures += expect_equal(gentest::format_value(static_cast<decltype(&native_callback)>(nullptr)), "false", "null Windows pointer");
#endif
    return failures == 0 ? 0 : 1;
} catch (const std::exception &error) {
    std::cerr << "unexpected formatting exception: " << error.what() << '\n';
    return 1;
} catch (...) {
    std::cerr << "unexpected nonstandard formatting exception\n";
    return 1;
}
