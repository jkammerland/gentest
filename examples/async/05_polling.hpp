#pragma once

#include "gentest/async.h"
#include "gentest/fixture.h"
#include "gentest/test.h"

#include <deque>
#include <functional>
#include <utility>

namespace async_examples::polling {

using namespace gentest;
using namespace gentest::async;

// This loop is entirely user-owned. Replace it with io_context::poll_one(),
// an engine tick, or another executor's progress function.
class Loop {
  public:
    void post(std::function<void()> work) { queue_.push_back(std::move(work)); }

    auto pollOne() -> bool {
        if (queue_.empty()) {
            return false;
        }
        auto work = std::move(queue_.front());
        queue_.pop_front();
        work();
        return true;
    }

  private:
    std::deque<std::function<void()>> queue_;
};

struct [[gentest::fixture(suite)]] Infrastructure {
    Loop loop;
};

[[gentest::test("drive_user_loop")]]
inline auto driveUserLoop(Infrastructure &infrastructure) -> async_test<void> {
    event<int> replies;
    infrastructure.loop.post([&replies] { replies.set("read", 42); });

    // The case chooses when the loop makes progress and when Gentest gets a turn.
    while (!replies.is_set("read")) {
        asserts::ASSERT_TRUE(infrastructure.loop.pollOne());
        co_await yield();
    }

    const int value = co_await replies.wait("read");
    expect_eq(value, 42);
}

} // namespace async_examples::polling
