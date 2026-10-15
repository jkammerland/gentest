#include "gentest/analyzer_assertions.h"

#include "gentest/runner.h"

#include <array>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>

namespace {

int condition_calls = 0;
int message_calls   = 0;
int continued       = 0;

bool condition(bool value) {
    ++condition_calls;
    return value;
}

std::string message() {
    ++message_calls;
    return "analyzer assertion message";
}

void passing(void *) {
    // Conditions run once, and messages stay unevaluated when assertions pass.
    ASSERT_TRUE(condition(true));
    ASSERT_FALSE(condition(false));
    ASSERT_TRUE(condition(true), message());
    ASSERT_FALSE(condition(false), message());
    ++continued;
}

template <bool AssertTrue, bool WithMessage> void failing(void *) {
    // Every spelling must record a fatal failure and stop the case body.
    if constexpr (AssertTrue) {
        if constexpr (WithMessage) {
            ASSERT_TRUE(condition(false), message());
        } else {
            ASSERT_TRUE(condition(false));
        }
    } else {
        if constexpr (WithMessage) {
            ASSERT_FALSE(condition(true), message());
        } else {
            ASSERT_FALSE(condition(true));
        }
    }
    ++continued;
}

} // namespace

int main() {
    struct Probe {
        std::string_view name;
        void (*run)(void *);
        bool fails;
        int  messages;
    };
    const std::array probes{
        Probe{.name = "passing", .run = passing, .fails = false, .messages = 0},
        Probe{.name = "true_without_message", .run = failing<true, false>, .fails = true, .messages = 0},
        Probe{.name = "true_with_message", .run = failing<true, true>, .fails = true, .messages = 1},
        Probe{.name = "false_without_message", .run = failing<false, false>, .fails = true, .messages = 0},
        Probe{.name = "false_with_message", .run = failing<false, true>, .fails = true, .messages = 1},
    };

    // Run real owner contexts; inspect counters outside the assertion machinery.
    for (const auto &probe : probes) {
        condition_calls = 0;
        message_calls   = 0;
        continued       = 0;

        const gentest::Case test{.name = probe.name, .fn = probe.run, .file = __FILE__, .line = __LINE__};
        const char         *args[]{"--no-color"};
        const int           result = gentest::run_cases(std::span(&test, 1), args);

        if (result != (probe.fails ? 1 : 0) || condition_calls != (probe.fails ? 1 : 4) || message_calls != probe.messages ||
            continued != (probe.fails ? 0 : 1)) {
            (void)std::fprintf(stderr, "Analyzer assertion contract failed: %.*s\n", static_cast<int>(probe.name.size()),
                               probe.name.data());
            return 1;
        }
    }
    return 0;
}
