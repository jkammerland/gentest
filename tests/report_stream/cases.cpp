#include "cases.hpp"

#include <thread>

namespace report_stream {

void Shared::setUp() { gentest::log("shared setup marker"); }
void Shared::tearDown() { gentest::log("shared teardown marker"); }
void Local::setUp() { gentest::log("local setup marker"); }

void Local::tearDown() {
    gentest::log("local teardown marker");
    gentest::asserts::EXPECT_FALSE(fail_teardown, "deliberate teardown failure");
}

void success(Local &) {
    gentest::log("body marker");

    // Adopted workers must use the same diagnostic channel as their owner.
    const auto  context = gentest::get_current_context();
    std::thread worker([context] {
        auto lease = gentest::set_current_context(context);
        gentest::log("worker marker");
    });
    worker.join();
}

void failure(Local &sample) {
    success(sample);
    sample.fail_teardown = true;
}

void ordinary() { gentest::log("ordinary marker"); }

} // namespace report_stream
