#include "append_iota_workload.hpp"

#include <benchmark/benchmark.h>
#include <deque>
#include <list>
#include <vector>

namespace {

template <typename Container> void appendIota(benchmark::State &state) {
    for ([[maybe_unused]] auto _ : state) {
        auto checksum = compare_workload::appendIota1M<Container>();
        benchmark::DoNotOptimize(checksum);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(compare_workload::kAppendIotaElementCount));
}

} // namespace

BENCHMARK_TEMPLATE(appendIota, std::vector<int>)->Name("append_iota/vector_1m")->Iterations(1);
BENCHMARK_TEMPLATE(appendIota, std::list<int>)->Name("append_iota/list_1m")->Iterations(1);
BENCHMARK_TEMPLATE(appendIota, std::deque<int>)->Name("append_iota/deque_1m")->Iterations(1);
