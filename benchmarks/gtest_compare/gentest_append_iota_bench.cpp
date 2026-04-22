#include "append_iota_workload.hpp"
#include "gentest/attributes.h"
#include "gentest/bench_util.h"

#include <deque>
#include <list>
#include <vector>

namespace gentest_compare_benchmarks {

template <typename Container> void appendIota() {
    auto checksum = compare_workload::appendIota1M<Container>();
    gentest::doNotOptimizeAway(checksum);
    gentest::clobberMemory();
}

[[using gentest: bench("append_iota/vector_1m")]]
void append_iota_vector_1m() {
    appendIota<std::vector<int>>();
}

[[using gentest: bench("append_iota/list_1m")]]
void append_iota_list_1m() {
    appendIota<std::list<int>>();
}

[[using gentest: bench("append_iota/deque_1m")]]
void append_iota_deque_1m() {
    appendIota<std::deque<int>>();
}

} // namespace gentest_compare_benchmarks
