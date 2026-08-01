#include "gentest/format_value.h"

#include <fmt/format.h>
#include <iostream>
#include <ostream>
#include <string>
#include <string_view>
#include <typeinfo>

struct FmtOnlyValue {
    int value;
};

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
    return failures == 0 ? 0 : 1;
}
