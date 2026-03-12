#include "runner_test_plan.h"

#include <algorithm>
#include <functional>
#include <random>
#include <unordered_map>

namespace gentest::runner {

namespace {

void shuffle_with_seed(std::vector<std::size_t> &order, std::uint64_t seed) {
    if (order.size() <= 1)
        return;
    std::mt19937_64 rng(static_cast<std::mt19937_64::result_type>(seed));
    std::shuffle(order.begin(), order.end(), rng);
}

using FixtureIndexMap = std::unordered_map<std::string_view, std::size_t>;

struct SuitePlanBuilder {
    SuiteExecutionPlan plan;
    FixtureIndexMap    suite_group_indices;
    FixtureIndexMap    global_group_indices;
};

void append_fixture_group_case(std::vector<FixtureGroupPlan> &groups, FixtureIndexMap &indices, std::string_view fixture,
                               gentest::FixtureLifetime lifetime, std::size_t idx) {
    const auto [it, inserted] = indices.try_emplace(fixture, groups.size());
    if (inserted) {
        groups.push_back(FixtureGroupPlan{
            .fixture          = fixture,
            .fixture_lifetime = lifetime,
            .idxs             = {idx},
        });
        return;
    }
    groups[it->second].idxs.push_back(idx);
}

} // namespace

std::vector<SuiteExecutionPlan> build_suite_execution_plan(std::span<const gentest::Case> cases, std::span<const std::size_t> idxs,
                                                           bool shuffle, std::uint64_t base_seed) {
    std::vector<SuitePlanBuilder> builders;
    builders.reserve(idxs.size());

    std::unordered_map<std::string_view, std::size_t> suite_indices;
    suite_indices.reserve(idxs.size());

    for (auto i : idxs) {
        const auto           &t = cases[i];
        const auto [it, inserted] = suite_indices.try_emplace(t.suite, builders.size());
        if (inserted) {
            builders.push_back(SuitePlanBuilder{});
            builders.back().plan.suite = t.suite;
        }

        auto &builder = builders[it->second];
        if (t.fixture_lifetime == gentest::FixtureLifetime::None ||
            t.fixture_lifetime == gentest::FixtureLifetime::MemberEphemeral) {
            builder.plan.free_like.push_back(i);
            continue;
        }

        if (t.fixture_lifetime == gentest::FixtureLifetime::MemberSuite) {
            append_fixture_group_case(builder.plan.suite_groups, builder.suite_group_indices, t.fixture, t.fixture_lifetime, i);
            continue;
        }
        append_fixture_group_case(builder.plan.global_groups, builder.global_group_indices, t.fixture, t.fixture_lifetime, i);
    }

    std::vector<SuiteExecutionPlan> plans;
    plans.reserve(builders.size());
    for (auto &builder : builders) {
        auto &plan = builder.plan;
        if (shuffle) {
            const std::uint64_t suite_seed = base_seed ^ (std::hash<std::string_view>{}(plan.suite) << 1);
            shuffle_with_seed(plan.free_like, suite_seed);

            const auto shuffle_groups = [&](std::vector<FixtureGroupPlan> &groups) {
                for (auto &group : groups) {
                    const std::uint64_t group_seed = suite_seed + std::hash<std::string_view>{}(group.fixture);
                    shuffle_with_seed(group.idxs, group_seed);
                }
            };
            shuffle_groups(plan.suite_groups);
            shuffle_groups(plan.global_groups);
        }

        plans.push_back(std::move(plan));
    }

    return plans;
}

} // namespace gentest::runner
