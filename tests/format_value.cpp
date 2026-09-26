#include <ostream>

// Preserve ordinary lookup as well as ADL: this overload deliberately precedes
// the formatting header and has no user-defined callback argument type.
using GlobalCallback = void (*)(long);
std::ostream &operator<<(std::ostream &stream, GlobalCallback);

#include "gentest/format_value.h"
#include "gentest/mock.h"

#include <fmt/format.h>
#include <iostream>
#include <string>
#include <string_view>
#include <typeinfo>

struct FmtOnlyValue {
    int value;
};

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

int main() {
    int failures = 0;
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
}
