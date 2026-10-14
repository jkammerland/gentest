#include "cases.hpp"
#include "gentest/runner.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <optional>
#include <string_view>

void once_supplied() {}
void once_runner() {}
void calibrated_warmup() {}
void default_benchmark() {}
void default_jitter() {}
void ordinary_test() {}

int main() {
    struct Expected {
        std::string_view             name;
        bool                         bench;
        bool                         jitter;
        gentest::BenchmarkExecution  execution;
        gentest::BenchmarkTimeSource time_source;
        std::optional<std::size_t>   warmup;
    };

    using enum gentest::BenchmarkExecution;
    using enum gentest::BenchmarkTimeSource;
    const std::array expected{
        Expected{"codegen/once_supplied", true, false, Once, Supplied, 0},
        Expected{"codegen/once_runner", true, false, Once, Runner, std::nullopt},
        Expected{"codegen/calibrated_warmup", true, false, Calibrated, Runner, 7},
        Expected{"codegen/default", true, false, Calibrated, Runner, std::nullopt},
        Expected{"codegen/jitter", false, true, Calibrated, Runner, std::nullopt},
        Expected{"codegen/test", false, false, Calibrated, Runner, std::nullopt},
    };

    // Inspect actual linked registrations: discovery, rendering, and compilation must all preserve the policy.
    const auto cases = gentest::registered_cases();
    if (cases.size() != expected.size()) {
        std::cerr << "Unexpected generated case count: " << cases.size() << '\n';
        return 1;
    }
    for (const auto &item : expected) {
        const auto found = std::ranges::find(cases, item.name, &gentest::Case::name);
        if (found == cases.end() || found->is_benchmark != item.bench || found->is_jitter != item.jitter ||
            found->benchmark.execution != item.execution || found->benchmark.time_source != item.time_source ||
            found->benchmark.warmup_epochs != item.warmup) {
            std::cerr << "Incorrect generated benchmark policy: " << item.name << '\n';
            return 1;
        }
        if (item.name == "codegen/once_supplied" &&
            (found->owner != "codegen" || found->tags.size() != 2 || found->tags[0] != "fast" || found->tags[1] != "owner=codegen")) {
            std::cerr << "Benchmark controls changed ordinary tags or owner metadata\n";
            return 1;
        }
    }
    return 0;
}
