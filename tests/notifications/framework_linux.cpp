// Framework source events through the bounded Linux transport: a case that
// keeps suspending must neither overflow the producer queue nor lose its
// watchdog renewal, and a case that stops must still expire.
#include "gentest/async.h"
#include "gentest/detail/generated_runtime.h"
#include "gentest/framework_events.h"
#include "gentest/runner.h"

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <gentest/notification_linux.h>
#include <memory>
#include <optional>
#include <poll.h>
#include <span>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>

namespace {
using namespace std::chrono_literals;
constexpr gentest::ProducerIdentity identity{.id = 73, .generation = 1};

void check(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::size_t                         yieldCount = 0;
std::chrono::steady_clock::duration yieldFor{};
std::chrono::milliseconds           stallFor{};

auto countedYields() -> gentest::awaitable<> {
    for (std::size_t i = 0; i != yieldCount; ++i) {
        co_await gentest::async::yield();
    }
}

// The documented polling pattern: keep yielding while waiting for a condition.
auto timedYields() -> gentest::awaitable<> {
    const auto until = std::chrono::steady_clock::now() + yieldFor;
    while (std::chrono::steady_clock::now() < until) {
        co_await gentest::async::yield();
    }
    // A blocked case reaches no suspension boundary and reports nothing.
    std::this_thread::sleep_for(stallFor);
}

auto countedAdapter(void *) -> gentest::detail::AsyncTaskPtr { return gentest::detail::make_async_task(countedYields()); }
auto timedAdapter(void *) -> gentest::detail::AsyncTaskPtr { return gentest::detail::make_async_task(timedYields()); }

auto makeCase(std::string_view name, gentest::detail::AsyncTaskPtr (*body)(void *)) -> gentest::Case {
    gentest::Case test{.name             = name,
                       .fn               = nullptr,
                       .file             = __FILE__,
                       .line             = __LINE__,
                       .should_skip      = false,
                       .fixture_lifetime = gentest::FixtureLifetime::None,
                       .suite            = "notifications"};
    test.async_fn = body;
    test.is_async = true;
    return test;
}

auto runCase(const gentest::Case &test, const gentest::RunConfiguration &configuration) -> int {
    const char *arguments[] = {"framework-linux", "--no-color"};
    return gentest::run_cases(std::span<const gentest::Case>(&test, 1), arguments, configuration);
}

struct Received {
    std::size_t started  = 0;
    std::size_t progress = 0;
    std::size_t finished = 0;
    bool        valid    = true;
};

// Reads until the producer shuts its end down.
auto drain(int socket) -> Received {
    Received      received;
    std::uint64_t previous = 0;
    for (;;) {
        gentest::EncodedNotification record;
        const auto                   size = ::recv(socket, record.bytes.data(), record.bytes.size(), 0);
        if (size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            pollfd wait{.fd = socket, .events = POLLIN, .revents = 0};
            static_cast<void>(::poll(&wait, 1, -1));
            continue;
        }
        if (size <= 0) {
            received.valid = received.valid && size == 0;
            return received;
        }
        record.size = static_cast<std::size_t>(size);
        gentest::Notification notification;
        if (gentest::decodeNotification(record.view(), notification) != gentest::NotifyResult::accepted ||
            notification.sequence != ++previous) {
            received.valid = false;
            return received;
        }
        received.started += notification.kind == gentest::NotificationKind::subject_started ? 1 : 0;
        received.progress += notification.kind == gentest::NotificationKind::progress ? 1 : 0;
        received.finished += notification.kind == gentest::NotificationKind::subject_finished ? 1 : 0;
    }
}

// Nothing reads the socket while the case runs. The writer therefore stalls on
// a full socket and every further record has to fit the 256-slot queue: the
// outcome does not depend on how fast any thread happens to run. Before
// coalescing this overflowed after a few hundred yields.
void yieldLoopCannotOverflow(std::size_t yields, std::optional<std::chrono::nanoseconds> interval) {
    auto                      channel = gentest::LinuxNotificationChannel::create();
    auto                      sink    = gentest::LinuxNotificationSink::attach(channel.releaseProducer(), identity);
    gentest::RunConfiguration configuration{.notifier = gentest::Notifier(sink, identity, 0)};
    if (interval) {
        configuration.suspendProgressInterval = *interval;
    }
    yieldCount      = yields;
    const auto test = makeCase("notifications/yield_loop", &countedAdapter);
    const int  rc   = runCase(test, configuration);
    check(sink->status() == gentest::NotifyResult::accepted, "yield loop faulted the bounded producer queue");
    check(rc == 0, "yield loop failed a passing run");

    Received    received;
    const int   socket = channel.monitor().socket;
    std::thread reader([&] { received = drain(socket); });
    const bool  closed = sink->close(10s);
    reader.join();
    check(closed, "producer did not drain after the yield loop");
    check(received.valid && received.started == 1 && received.finished == 1, "lifecycle records were lost or reordered");
    check(received.progress >= 1, "a progressing case delivered no progress");
    check(received.progress < yields, "progress was not coalesced");
}

// The watchdog verdicts must survive coalescing: continuous suspension keeps
// renewing well past the timeout, and stopping afterwards still expires.
void watchdogSemantics(bool stall) {
    constexpr auto           timeout = 500ms;
    auto                     channel = gentest::LinuxNotificationChannel::create();
    gentest::WatchdogMonitor monitor(channel.releaseMonitor(), identity,
                                     {.inactivityTimeout = timeout, .maximumSilence = 1s, .maximumSubjects = 16});
    monitor.start();
    auto sink = gentest::LinuxNotificationSink::attach(channel.releaseProducer(), identity);

    yieldFor        = 3 * timeout;
    stallFor        = stall ? 3 * timeout : 0ms;
    const auto test = makeCase("notifications/watchdog", &timedAdapter);
    const int  rc   = runCase(test, {.notifier = gentest::Notifier(sink, identity, 0)});
    check(rc == 0 && sink->status() == gentest::NotifyResult::accepted, "watched case failed the notification infrastructure");

    if (!stall) {
        monitor.finish();
    }
    static_cast<void>(sink->close(3s));
    check(monitor.waitFor(5s), "watchdog did not complete");
    monitor.close();
    const auto outcome = monitor.outcome();
    if (stall) {
        if (!outcome.failure) {
            throw std::runtime_error("stalled case did not expire");
        }
        check(outcome.failure->code == gentest::WatchdogFailureCode::inactivity, "stalled case failed for another reason");
        check(outcome.failure->subject != 0 && outcome.failure->label == "suspend", "expiry lost the stalled subject or its last label");
    } else if (outcome.failure) {
        throw std::runtime_error("progressing case expired: code=" + std::to_string(static_cast<int>(outcome.failure->code)) +
                                 " detail=" + outcome.failure->detail);
    }
}
} // namespace

int main() {
    try {
        // Every suspension reaches the transport; the queue alone must hold.
        yieldLoopCannotOverflow(300000, 0ns);
        yieldLoopCannotOverflow(50000, std::nullopt);
        watchdogSemantics(false);
        watchdogSemantics(true);
    } catch (const std::exception &error) {
        if (std::fprintf(stderr, "framework Linux notification regression: %s\n", error.what()) < 0) {
            return 1;
        }
        return 1;
    }
    return 0;
}
