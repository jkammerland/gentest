#pragma once

#include "gentest/runner.h"

#include <thread>

namespace header_tidy_context_lease {

inline void missing_header_thread_context() {
    std::thread worker([] { gentest::log("missing header lease"); });
    worker.join();
}

} // namespace header_tidy_context_lease
