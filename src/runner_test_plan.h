#pragma once

#include "gentest/runner.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace gentest::runner {

struct FixtureGroupPlan {
    std::string_view         fixture;
    gentest::FixtureLifetime fixture_lifetime = gentest::FixtureLifetime::None;
    std::vector<std::size_t> idxs;
};

struct SuiteExecutionPlan {
    std::string_view                suite;
    std::vector<std::size_t>        free_like;
    std::vector<FixtureGroupPlan>   suite_groups;
    std::vector<FixtureGroupPlan>   global_groups;
};

std::vector<SuiteExecutionPlan> build_suite_execution_plan(std::span<const gentest::Case> cases, std::span<const std::size_t> idxs,
                                                           bool shuffle, std::uint64_t base_seed);

} // namespace gentest::runner
