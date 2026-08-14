#include "gentest_append_iota_bench.hpp"

namespace gentest_compare_benchmarks {

template <typename Container> void appendIota() {
    auto checksum = compare_workload::appendIota1M<Container>();
    gentest::doNotOptimizeAway(checksum);
    gentest::clobberMemory();
}

} // namespace gentest_compare_benchmarks

namespace gentest_compare_benchmarks {

void append_iota_vector_1m() { appendIota<std::vector<int>>(); }

} // namespace gentest_compare_benchmarks

namespace gentest_compare_benchmarks {

void append_iota_list_1m() { appendIota<std::list<int>>(); }

} // namespace gentest_compare_benchmarks

namespace gentest_compare_benchmarks {

void append_iota_deque_1m() { appendIota<std::deque<int>>(); }

} // namespace gentest_compare_benchmarks
