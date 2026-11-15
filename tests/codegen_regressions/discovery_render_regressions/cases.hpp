#pragma once

#include "gentest/attributes.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#if 0
The users' guide explains why prose in skipped text must not hide the cases below.
#endif

// A fixture type in the global namespace, used from inside a namespace that
// also sees an unrelated type with the same name.
struct DrrGlobalFixture {
    int value = 11;
};

namespace discovery_render_regressions {

namespace shadow {
struct DrrGlobalFixture {
    int value = -1;
};
} // namespace shadow

// Reference returns of types that cannot be copied or instantiated.
struct NonCopyable {
    NonCopyable()                               = default;
    NonCopyable(const NonCopyable &)            = delete;
    NonCopyable &operator=(const NonCopyable &) = delete;
};

struct Abstract {
    virtual ~Abstract()                  = default;
    [[nodiscard]] virtual int id() const = 0;
};

[[using gentest: test("returns/noncopyable_ref")]]
NonCopyable &noncopyable_ref();

[[using gentest: test("returns/abstract_ref")]]
const Abstract &abstract_ref();

/* see https://example.com/a//b */ [[using gentest: test("fixtures/global_type")]]
void global_fixture_type(DrrGlobalFixture &fixture);

struct LocalA {
    int value = 1;
};
struct [[using gentest: fixture(suite)]] SuiteB {
    int value = 2;
};
using AliasA = LocalA;

template <typename T>
[[using gentest: test("fixtures/bound"), template(T, LocalA, discovery_render_regressions::SuiteB, AliasA)]]
void bound_fixture(T &fixture) {
    if (fixture.value != 1 && fixture.value != 2) {
        throw std::runtime_error("unexpected fixture value");
    }
}

struct Members {
    template <typename T>
    [[using gentest: test("members/typed"), template(T, std::string, std::pair<std::string, int>)]]
    inline void typed() {
        (void)T{};
    }

    [[using gentest: test("members/inline_specifier")]]
    inline void inline_specifier() {}
};

[[using gentest: test("values/escapes"), parameters(text, "a\x41", "nul\0x")]]
void escapes(std::string_view text);

[[using gentest: test("values/hex_range"), range(value, 0x10, 1, 0x12)]]
void hex_range(int value);

[[using gentest: test("values/long_double_range"), range(value, 0.0, 0.25, 0.5)]]
void long_double_range(long double value);

} // namespace discovery_render_regressions
