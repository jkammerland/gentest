#pragma once

#include "gentest/runner.h"

#include <thread>

namespace header_tidy_token_adoption {

inline void missing_header_thread_token() {
    std::thread worker([] { gentest::log("missing header adoption"); });
    worker.join();
}

} // namespace header_tidy_token_adoption
