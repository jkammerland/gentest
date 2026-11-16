# Notifications and progress watchdogs

A progress watchdog stops a test run, or any other process, that hangs. Code
reports progress through a `gentest::Notifier`. A watchdog tracks each piece of
work, called a subject, on its own monotonic clock, and fails when a subject
reports no progress for too long.

The portable library works without the test runner. The transport between local
processes and the process monitor are Linux-only; there is no macOS, Windows or
network transport.

| What | Header | CMake target |
| --- | --- | --- |
| `Notifier`, the wire format, the watchdog model | `gentest/notification.h` | `gentest::notifications` |
| Run configuration and framework events | `gentest/framework_events.h` | `gentest::gentest_runtime` |
| The current case's notifier, `gentest::currentNotifier()` | `gentest/context.h` | `gentest::gentest_runtime` |
| Linux transport and process monitor | `gentest/notification_linux.h` | `gentest::notifications_linux` (Linux only) |

`import gentest;` exports everything except the Linux transport.

## Watch a test run

Give the test executable its own `main()` that hands the runner a notifier from
the environment:

```cpp
// main.cpp
#include <gentest/framework_events.h>
#include <gentest/notification_linux.h>
#include <gentest/runner.h>

int main(int argc, char **argv) {
    gentest::RunConfiguration configuration;
    configuration.notifier = gentest::notifierFromEnvironment();
    return gentest::run_all_tests(argc, argv, configuration);
}
```

Link `gentest::gentest` instead of `gentest::gentest_main`, plus the Linux transport:

```cmake
target_link_libraries(my_tests PRIVATE gentest::gentest gentest::gentest_runtime gentest::notifications_linux)
```

Started on its own, the program runs as usual: without the notification
environment variables, `notifierFromEnvironment()` returns a disabled notifier.
Started by a launcher that provides a channel and a watchdog (see
[Launch a watched process](#launch-a-watched-process)), the runner reports each
case to the watchdog. When a case makes no progress for the configured time, the
watchdog stops the process. The `WatchdogFailure` it reports has the code
`inactivity`, the detail `no observed progress before inactivity deadline`, and
a label: the case name, or the label of the last progress record on the case's
subject. The default hooks report progress labelled `fixture start`,
`fixture finish`, `phase start`, `phase finish` and `suspend`, so after a
fixture hook, a measured phase or an async suspension the label is that hook's
name. Report progress with your own label to name the step that hung.

The runtime never reads the notification environment variables by itself; the
`main()` above opts in. A program that uses another transport passes its own
`Notifier`.

## Report progress from a case

The runner reports some progress by itself (see
[What the runner reports](#what-the-runner-reports)). A case that works for a
long time without suspending reports its own progress, or allows a quiet
period:

```cpp
#include "gentest/test.h"

#include <chrono>

[[gentest::test("transfer/bounded_quiet_period")]]
inline void bounded_quiet_period() {
    using namespace std::chrono_literals;

    const auto notifications = gentest::currentNotifier();
    (void)notifications.allowSilenceFor(8s, "waiting for a bounded transfer");
    // Perform the transfer with its own deadline and cancellation handling.
    (void)notifications.progress("transfer complete");
}
```

`gentest::currentNotifier()` returns a handle for the current case's subject
(in a shared fixture's setup or teardown, the fixture's subject):

- Use it for `progress()`, `notify()` and `allowSilenceFor()`. Don't call
  `start()` or `finish()`; the runner starts and finishes each case's subject.
- An [adopted](logging_and_threads.md) worker's handle reports for the owning
  case. A handle never reports for a sibling case.
- Without a notifier in the run configuration, or on a thread without a test
  context, the handle is disabled and its calls return `NotifyResult::disabled`.
- A handle kept after its case has finished returns `closed`, so it can't renew
  a later case. Pointing it at another subject with `forSubject()` is rejected.

You can discard the results inside a case, as above. The runner treats every
rejected notification except `closed` as an infrastructure failure, including
results the case ignored. It finishes teardown, prints `gentest: notification infrastructure failed`
and returns failure. `xfail` can't turn that into a passing run.

## What the runner reports

With a notifier in the run configuration:

- Each case [occurrence](command_line.md) gets a distinct subject, started
  before the case's setup and finished after the case, once its adopted
  contexts are released and its coroutine frames are destroyed.
- Shared fixture setup and teardown each get a subject of their own.
- Local fixtures, nested async work and benchmark phases report under the
  subject of the case or fixture phase that runs them.
- By default, fixture setup and teardown, the phases of a measured case, and
  each async suspension renew the subject's progress. Suspensions are
  rate-limited: see [Framework events](#framework-events).

Nothing else renews progress. A synchronous case that computes for longer than
the inactivity timeout expires unless it reports progress or allows a quiet
period. Measured-case phase hooks surround setup, the whole measurement and
teardown, so a long measurement needs its own progress reports or an allowance.

## Notifier operations

A `Notifier` binds a producer identity, a producer generation and a subject ID.
Copies report for the same subject.

| Operation | Watchdog effect |
| --- | --- |
| `start(label)` | Arms this subject with the normal inactivity timeout. |
| `progress(label)` | Renews this subject's deadline. A non-empty label replaces the subject's diagnostic label. |
| `notify(topic, payload)` | Delivers application data; doesn't renew progress. `topic` must not be empty. |
| `allowSilenceFor(duration, reason)` | Sets this subject's deadline to receipt time plus `duration`. |
| `finish()` | Disarms this subject. |

Each call returns a `gentest::NotifyResult`. `accepted` means admitted locally,
not received by the monitor. A default-constructed notifier returns `disabled`
and costs nothing. The strings in a notification are borrowed for the duration
of the call, so a custom sink must copy them.

The watchdog applies these rules:

- `gentest::log()`, stdout and stderr don't renew progress.
- Progress from one subject can't extend another subject's deadline.
- Delayed or duplicate sequence numbers and older producer generations can't
  renew current work.

Report progress when the work actually advances; a periodic heartbeat can keep
a broken operation alive.

## Quiet periods

A quiet period is an intentional, finite exception to the inactivity timeout.
Its duration must be positive, finite and at most the watchdog's
`WatchdogConfig::maximumSilence`.

An allowance replaces the subject's current deadline; it isn't added to the time
left. The next progress record restores the normal inactivity timeout. The
watchdog uses the time it receives a record, and a record that arrives exactly
at the deadline is too late.

An allowance doesn't cancel work when it ends, and it doesn't replace an
operation deadline such as `async::wait_until()`. Nothing grants an allowance
implicitly: not a timer wait, a benchmark duration or a log message.

## Launch a watched process

A launcher creates a channel, starts the watched program with one end of it,
and runs a `gentest::WatchdogMonitor` on the other end. The program finds its
end through two environment variables:

| Variable | Value |
| --- | --- |
| `GENTEST_NOTIFY_FDS` | `<socket>,<fault>`: the inherited descriptor numbers of the producer's `AF_UNIX` `SOCK_SEQPACKET` socket and of its fault eventfd. |
| `GENTEST_NOTIFY_ID` | `<id>,<generation>`: the producer identity, both non-zero. The monitor must use the same identity. |

`gentest::notifierFromEnvironment()` reads them in the watched program:

- With neither variable set, it returns a disabled notifier.
- With only one set, or with a malformed value, it throws
  `std::invalid_argument`.
- It checks that the descriptor numbers name an `AF_UNIX` `SOCK_SEQPACKET`
  socket and an eventfd. If not, for example because a wrapper closed the
  inherited descriptors and the numbers were reused, it throws and leaves those
  descriptors open and unmodified.
- On success, it takes over the descriptors, marks them close-on-exec and
  removes both variables. Call it once and keep the handle; a second call
  returns a disabled notifier.
- Its optional argument is the subject ID for the returned handle (default 1).
- Don't use a live Linux sink after `fork()`; attach fresh descriptors in the
  child instead.

The launcher in [examples/notifications/service.cpp](../examples/notifications/service.cpp)
does the following:

1. Creates the channel with `gentest::LinuxNotificationChannel::create()`.
2. Starts the child with `channel.producer()` mapped to known descriptor numbers
   and both variables set, after dropping any inherited values of them. Then it
   closes its own copies of the producer descriptors
   (`channel.releaseProducer()`).
3. Opens a pidfd for the child and starts the monitor:

   ```cpp
   gentest::WatchdogMonitor monitor(
       channel.releaseMonitor(), identity, {.inactivityTimeout = 500ms, .maximumSilence = 2s, .maximumSubjects = 16},
       std::make_shared<ConsoleObserver>(), {.observerCapacity = 256, .shutdownGrace = 100ms, .shutdownCutoff = 1s});
   monitor.setProcessOwner(pidfd);
   ::close(pidfd);
   monitor.start();
   ```

4. Waits for the child with `waitpid()`, then calls `monitor.finish()` and
   `monitor.close()`, and fails if `monitor.outcome().failure` is set or the
   child failed. A process that exits normally can fail its watchdog too, so
   check both.

The experimental [network harness](../experimental/harness/README.md) does this
for each of its programs when you set `Program::watchdog`.

`gentest::WatchdogConfig` sets the watchdog policy:

| Field | Meaning |
| --- | --- |
| `inactivityTimeout` | How long a started subject may go without progress. It has no usable default: set it to a positive duration, or the watchdog fails with `invalid_configuration` and the `WatchdogMonitor` constructor throws `std::invalid_argument`. |
| `maximumSilence` | The longest duration `allowSilenceFor()` may request. Default 5 minutes. A longer request fails the watchdog with `invalid_record`. |
| `maximumSubjects` | The most subjects tracked at once, and the most producer IDs remembered. Default 256. More fails with `capacity_exceeded`. |

`gentest::WatchdogMonitorOptions` sets how the monitor stops a process:

| Field | Meaning |
| --- | --- |
| `observerCapacity` | Length of the observer queue. Default 256. |
| `shutdownGrace` | Time between the stop request and `SIGKILL`. Default 1000 ms. |
| `shutdownCutoff` | Time after the stop request when supervision ends even if the process is alive, failing with `shutdown_timeout`. Default 2000 ms; it must be longer than `shutdownGrace`. |

## Report progress from any program

The same notifier works in a program that has nothing to do with the test
runner. A standalone program starts and finishes its own subjects:

```cpp
#include <gentest/notification_linux.h>

#include <cstdint>
#include <stdexcept>

void requireAdmission(gentest::NotifyResult result) {
    if (result != gentest::NotifyResult::accepted &&
        result != gentest::NotifyResult::disabled) {
        throw std::runtime_error("notification admission failed");
    }
}

const gentest::Notifier &processNotifier() {
    static const auto notifier = gentest::notifierFromEnvironment(); // attach once
    return notifier;
}

void handleRequest(std::uint64_t requestId) { // a fresh, non-zero ID per request
    const auto notifications = processNotifier().forSubject(requestId);
    requireAdmission(notifications.start("request"));

    requireAdmission(notifications.notify("status", "decoding request"));
    // Decode and validate the request here.
    requireAdmission(notifications.progress("request validated"));

    // Publish the result here.
    requireAdmission(notifications.finish());
}
```

Each request uses its own subject ID. To report without the environment
variables, attach a sink to descriptors you own:
`gentest::Notifier(gentest::LinuxNotificationSink::attach(descriptors, identity), identity, subject)`.

## How the watchdog decides

`gentest::WatchdogModel` is the portable, deterministic core. Its owner passes
monotonic time to `accept(record, now)` and `advance(now)`; the model never
reads a clock or creates a thread. `nextDeadline()` returns the next active
deadline, and `failure()` keeps the first failure.

A producer generation identifies one incarnation of a producer ID. When the
model accepts a `start()` from a newer generation, it retires every subject the
older generation left unfinished, so they neither expire nor fail the newer
generation. Only a start record advances the generation. The Linux `WatchdogMonitor` supervises exactly one
identity and treats records from any other as `invalid_record`.

## The Linux process monitor

`gentest::WatchdogMonitor` owns the receiving descriptors and enforces the model
on a thread of its own. It also watches startup and gaps with no active subject,
so a process that never starts a subject fails the inactivity check too. A
process that exits while subjects are active, or before it started any, fails
too: with `transport_error` (`notification channel closed before subjects
finished`) when its exit closes the channel, as with the launcher in
[Launch a watched process](#launch-a-watched-process), or with `process_exited`
when another process holds the producer descriptors.

`setProcessOwner(pidfd, stopEventFd)` registers the supervised process; the
monitor duplicates the pidfd, and the launcher keeps owning the child and must
reap it. On failure, the monitor requests stop: through the optional stop
eventfd, which hands cooperative cleanup to a session owner, or with `SIGTERM`.
After `shutdownGrace` it sends `SIGKILL` to the process pidfd or, if you
registered pidfds with `addEscalationPidfd()` (for example a process that
contains the service), to those instead. `shutdownCutoff` bounds the whole
shutdown.

A `gentest::NotificationObserver` receives what the monitor sees:

```cpp
struct Observer final : gentest::NotificationObserver {
    void onNotification(const gentest::Notification &event) override {
        // Copy needed fields into application-owned storage before returning.
        // This callback runs on the observer dispatcher, not the runner.
        (void)event;
    }
    void onExpired(const gentest::WatchdogFailure &failure) override {
        // Inspect failure.code, producer, subject, sequence, label, and detail.
        (void)failure;
    }
};
```

Observer callbacks run one at a time on a separate dispatcher with a bounded
queue, never on the enforcement thread. `onExpired` reports every terminal
monitor failure: inactivity, transport and observation failures. An observer
exception or a full observer queue is an observation failure. A blocked observer
can't delay enforcement, but `close()` waits for its callback while it drains
the dispatcher, so return promptly. Read `outcome().failure` for the verdict.

These calls end supervision:

- `disarm()` stops inactivity enforcement but keeps receiving, for a shutdown
  that the owner directs.
- `finish()`, after the owner has collected its process, requests a final
  receive drain.
- `close()` completes enforcement, then drains the observer callbacks.

Closing or destroying a started monitor without a preceding `finish()` is a stop
request: the registered process is asked to stop, and killed if it outlives
`shutdownGrace`. Call `finish()` first whenever the supervised process must
outlive the monitor.

## Framework events

`gentest::RunConfiguration`, from `gentest/framework_events.h`, tells the runner
where to report:

| Field | Meaning |
| --- | --- |
| `notifier` | The base handle. Leave it unstarted: the runner derives a distinct subject from it for each case occurrence and shared fixture phase. |
| `frameworkEvents` | An optional `std::shared_ptr<gentest::FrameworkEventVisitor>` that receives framework hooks. The run and its contexts keep it alive. |
| `suspendProgressInterval` | The minimum time between two progress records that the default `onSuspend` publishes for one subject. Default 1 ms; zero publishes every suspension. |

Pass it as the last argument of `run_cases(cases, arguments, configuration)`,
`run_all_tests(arguments, configuration)` or
`run_all_tests(argc, argv, configuration)`. The overloads without a
configuration run with no notifier and no visitor.

A `gentest::FrameworkEventVisitor` receives these hooks synchronously:
`onRunStart` and `onRunFinish`, which have no case subject, `onCaseStart` and
`onCaseFinish`, `onFixtureStart` and `onFixtureFinish`, `onPhaseStart` and
`onPhaseFinish`, and `onSuspend`. Each gets a `gentest::FrameworkEvent` with
`notifier`, `name`, `phase`, `suspension` and `location`; its string views are
valid only during the callback. The default fixture and phase hooks, and the
default `onSuspend`, publish progress on the event's notifier.

A polling loop such as `while (!ready) co_await yield();` suspends on every
iteration, so the default `onSuspend` publishes at most one progress record per
subject within `suspendProgressInterval`. A renewal can be up to one interval
older than the latest suspension, so keep the interval far below the inactivity
timeout. A custom visitor gets the same limit by calling
`FrameworkEventVisitor::onSuspend`. The runner always starts and finishes case
subjects, even when a visitor filters the hooks:

```cpp
#include <gentest/framework_events.h>
#include <memory>

struct SelectiveProgress final : gentest::FrameworkEventVisitor {
    void onSuspend(const gentest::FrameworkEvent &event) noexcept override {
        if (event.suspension == gentest::SuspensionKind::yield) {
            return;
        }
        FrameworkEventVisitor::onSuspend(event);
    }
};

void selectVisitor(gentest::RunConfiguration &configuration) {
    configuration.frameworkEvents = std::make_shared<SelectiveProgress>();
}
```

A suspension event means a coroutine reached a framework registration point in
`await_suspend`: yield, timer, event, value and void future, custom continuation
registration, composition, or the transfer to a child coroutine
(`gentest::SuspensionKind`). An `await_ready` that returns `true` produces no
event. The event doesn't mean the coroutine stayed suspended: a ready race or a
later `false` return counts too.

Hooks must be short, non-blocking and `noexcept`, and must not call back into
the runner or wait for delivery. Adopted threads may call hooks at the same
time, so synchronize any state the visitor owns. A visitor without a notifier
is supported and gets a distinct subject ID per case. With neither a notifier
nor a visitor, the runner creates no threads or queues for events.

## Transport limits and wire format

The Linux producer uses a bounded local queue and a writer thread of its own.
Admission never waits for queue space, and accepted records are written in
order. A separate fault eventfd reports a terminal producer failure even when
the socket is backpressured. Queue overflow, invalid records, oversized
payloads, transport failure and a failed bounded close are explicit errors;
the queue never grows unbounded or drops records silently.

The producer queue holds 256 records by default;
`LinuxNotificationSink::attach(descriptors, producer, queueCapacity)` sets it
(`notifierFromEnvironment()` uses the default).

While a progress record waits in the queue, another `progress()` for the same
subject with the same label is accepted without taking a second slot; the queued
record renews the deadline when it arrives. Any other record is queued
separately, so many distinct pending records can overflow the queue.
`LinuxNotificationSink::close(timeout)` drains with a bound (default 1 s); keep
the receiver running while it drains.

The versioned wire format is little-endian with fixed-width fields, and a record
is at most 4096 bytes including its 52-byte header. It carries no native
structures, descriptors, pointers or producer timestamps. Use `encodeNotification()` and
`decodeNotification()` rather than sending a C++ object's bytes. A custom
`NotificationSink` receives records with sequence zero and assigns a strictly
increasing sequence per producer incarnation when it serializes admitted
records. `NotificationSink::onError()` lets a sink latch validation failures
that happen before `submit()`.

## Run the standalone example

[examples/notifications/service.cpp](../examples/notifications/service.cpp)
links only `gentest::notifications_linux` and launches itself as a watched
child. A Linux build of this repository with tests enabled builds it; its
[README](../examples/notifications/README.md) shows the output:

```bash
cmake --build build/debug-system --target gentest_notifications_example
build/debug-system/examples/notifications/gentest_notifications_example
build/debug-system/examples/notifications/gentest_notifications_example --stall
```

The ordinary run succeeds. With `--stall`, the child stops reporting, and the
parent's watchdog ends it and exits with a failure.
