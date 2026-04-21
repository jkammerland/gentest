#include "append_iota_workload.hpp"

#include <chrono>
#include <deque>
#include <list>
#include <nanobench.h>
#include <vector>

namespace {

template <typename Container> void appendIota() {
    auto checksum = compare_workload::appendIota1M<Container>();
    ankerl::nanobench::doNotOptimizeAway(checksum);
}

template <typename Container> void runAppendIota(ankerl::nanobench::Bench &bench, char const *name) {
    bench.run(name, [] { appendIota<Container>(); });
}

} // namespace

int main() {
    ankerl::nanobench::Bench bench;
    bench.title("append_iota_1m")
        .unit("element")
        .batch(compare_workload::kAppendIotaElementCount)
        .warmup(0)
        .epochs(1)
        .epochIterations(1)
        .minEpochIterations(1)
        .minEpochTime(std::chrono::nanoseconds{0});

    runAppendIota<std::vector<int>>(bench, "append_iota/vector_1m");
    runAppendIota<std::list<int>>(bench, "append_iota/list_1m");
    runAppendIota<std::deque<int>>(bench, "append_iota/deque_1m");
    return 0;
}
