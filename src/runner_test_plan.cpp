#include "runner_test_plan.h"

#include <algorithm>
#include <functional>
#include <random>

namespace gentest::runner {

namespace {

void shuffle_with_seed(std::vector<std::size_t> &order, std::uint64_t seed) {
    if (order.size() <= 1)
        return;
    std::mt19937_64 rng(static_cast<std::mt19937_64::result_type>(seed));
    std::shuffle(order.begin(), order.end(), rng);
}

} // namespace

std::vector<SuiteExecutionPlan> build_suite_execution_plan(std::span<const gentest::Case> cases, std::span<const std::size_t> idxs,
                                                           bool shuffle, std::uint64_t base_seed) {
    std::vector<std::string_view> suite_order;
    suite_order.reserve(idxs.size());
    for (auto i : idxs) {
        const auto &t = cases[i];
        if (std::find(suite_order.begin(), suite_order.end(), t.suite) == suite_order.end())
            suite_order.push_back(t.suite);
    }

    std::vector<SuiteExecutionPlan> plans;
    plans.reserve(suite_order.size());

    for (auto suite_name : suite_order) {
        SuiteExecutionPlan plan;
        plan.suite = suite_name;

        for (auto i : idxs) {
            const auto &t = cases[i];
            if (t.suite != suite_name)
                continue;

            if (t.fixture_lifetime == gentest::FixtureLifetime::None ||
                t.fixture_lifetime == gentest::FixtureLifetime::MemberEphemeral) {
                plan.free_like.push_back(i);
                continue;
            }

            auto &groups =
                (t.fixture_lifetime == gentest::FixtureLifetime::MemberSuite) ? plan.suite_groups : plan.global_groups;
            auto it = std::find_if(groups.begin(), groups.end(), [&](const FixtureGroupPlan &group) { return group.fixture == t.fixture; });
            if (it == groups.end()) {
                groups.push_back(FixtureGroupPlan{
                    .fixture          = t.fixture,
                    .fixture_lifetime = t.fixture_lifetime,
                    .idxs             = {i},
                });
            } else {
                it->idxs.push_back(i);
            }
        }

        if (shuffle) {
            const std::uint64_t suite_seed = base_seed ^ (std::hash<std::string_view>{}(suite_name) << 1);
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
