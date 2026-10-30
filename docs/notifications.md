# Notifications and progress watchdogs

Gentest separates reporting an event from deciding whether work has stopped
making progress. Applications publish through `Notifier`; a watchdog tracks
individual subjects using its own monotonic clock. The portable notification
library can be used without the test runner. Linux transport and process
supervision are optional.

| Surface | Header | CMake target |
| --- | --- | --- |
| Notifications, wire codec, deterministic watchdog model | `gentest/notification.h` | `gentest::notifications` |
| Gentest source events and run configuration | `gentest/framework_events.h` | `gentest::gentest_runtime` |
| Current case's notifier | `gentest/context.h` | `gentest::gentest_runtime` |
| Linux transport and process monitor | `gentest/notification_linux.h` | `gentest::notifications_linux` |

The portable notification types and framework events are also exported by
`import gentest;`. The Linux transport uses its separate header and target.

## Reporting progress

A `Notifier` binds a producer identity, producer generation, and subject ID.
Copying it preserves that identity. Standalone applications explicitly start and
finish their subjects; Gentest owns those lifecycle operations for its cases.

```cpp
#include <gentest/notification_linux.h>
#include <stdexcept>

void requireAdmission(gentest::NotifyResult result) {
    if (result != gentest::NotifyResult::accepted &&
        result != gentest::NotifyResult::disabled) {
        throw std::runtime_error("notification admission failed");
    }
}

void handleRequest() {
    auto notifications = gentest::notifierFromEnvironment();
    requireAdmission(notifications.start("request"));

    requireAdmission(notifications.notify("status", "decoding request"));
    // Decode and validate the request here.
    requireAdmission(notifications.progress("request validated"));

    // Publish the result here.
    requireAdmission(notifications.finish());
}
```

`notifierFromEnvironment()` attaches once to inherited Linux descriptors. With
neither notification environment variable present, it returns a disabled
notifier. Malformed or incomplete configuration throws. Retain and copy the
returned handle: successful attachment consumes the descriptors, marks them
close-on-exec, and removes the environment variables. Do not reuse a live Linux
sink after `fork`; attach fresh endpoints in the new process.

| Operation | Watchdog effect |
| --- | --- |
| `start(label)` | Arms this subject with the normal inactivity timeout. |
| `progress(label)` | Renews this subject's deadline and optionally updates its diagnostic label. |
| `notify(topic, payload)` | Delivers ordinary application data; does not renew progress. |
| `allowSilenceFor(duration, reason)` | Sets this subject's temporary deadline to receipt time plus the supplied duration. |
| `finish()` | Disarms this subject. |

Logging is separate: `gentest::log`, stdout, and stderr do not renew progress.
Progress from another active subject cannot extend this subject's deadline.
Delayed or duplicate sequence numbers and older producer generations cannot
renew current work. Use a fresh subject ID for each standalone work occurrence.

`NotifyResult::accepted` means admitted locally, not acknowledged by the monitor.
A default-constructed notifier returns `disabled` without creating threads,
queues, or per-call allocations. Notification views borrow their strings only
for the call; a sink must copy or own the bytes before accepting them.

## Explicit quiet periods and deadlines

A quiet period is an intentional, finite exception to the normal inactivity
policy. Its duration must be positive, finite, and no greater than the monitor's
`WatchdogConfig::maximumSilence`. For example, inside a Gentest case:

```cpp
using namespace std::chrono_literals;

auto notifications = gentest::currentNotifier();
requireAdmission(notifications.allowSilenceFor(8s, "waiting for a bounded transfer"));
// Perform the transfer with its own deadline and cancellation handling.
requireAdmission(notifications.progress("transfer complete"));
```

An allowance replaces the current subject deadline; it is not added to the
remaining timeout. The next progress record restores the normal inactivity
timeout. The monitor uses receipt time rather than a timestamp supplied by the
producer. A record received exactly at the existing deadline is too late: expiry
is evaluated first.

An allowance does not cancel work when its duration ends and does not replace an
operation deadline such as `async::wait_until`. No allowance is inferred from a
timer wait, a benchmark duration, or a log message. A process watchdog provides
an independent response when cooperative cancellation or the runner cannot
execute.

Report progress when the monitored work actually advances. A periodic heartbeat
or an unrelated busy loop can keep a broken operation alive. Receiving progress
is evidence of the selected reporting points, not proof that the application is
free of deadlocks or that its result is correct.

## Enabling framework source events

The existing runner overloads keep their behavior. Supply `RunConfiguration`
when embedding the runner:

```cpp
#include <gentest/framework_events.h>
#include <gentest/notification_linux.h>
#include <gentest/runner.h>

int main(int argc, char **argv) {
    gentest::RunConfiguration configuration;
    configuration.notifier = gentest::notifierFromEnvironment();
    return gentest::run_all_tests(argc, argv, configuration);
}
```

Link this Linux entry point to `gentest::gentest_runtime` and
`gentest::notifications_linux`. The portable runtime does not inspect Linux
notification environment variables automatically. Applications using another
transport supply their own `Notifier` instead.

Configuration overloads are available for:

```cpp
run_cases(cases, arguments, configuration);
run_all_tests(arguments, configuration);
run_all_tests(argc, argv, configuration);
```

The supplied notifier is an unstarted base handle. The runner creates a distinct
subject for each case occurrence, including repeats, and starts and finishes it
automatically. Do not call `start()` or `finish()` on `currentNotifier()` inside
a case. Use that handle for explicit progress, ordinary notifications, and
allowances. It follows `CurrentContext` adoption and never borrows a sibling
case's identity. Outside a bound context it is disabled. A retained transport-backed terminal
handle returns `closed` and cannot renew a later case.

Observation begins before shared-fixture setup and remains active through final
teardown. Shared-fixture setup and teardown each have their own active work
subject. Local fixtures, nested blocking async work, native coroutine children,
and benchmark phases inherit their owning subject. A case's terminal event
follows adopted-context release and coroutine-frame retirement, including
canceled local cleanup and coroutine parameter destruction. Completed subject
registry entries are removed instead of accumulating across repeats.

Benchmark and jitter phase hooks surround setup, the complete measurement
operation, and teardown. They run outside the timed call loops. Long measured
operations need explicit application progress or a bounded allowance appropriate
to the configured watchdog policy.

### Source visitors

`FrameworkEventVisitor` receives synchronous source hooks. Its default
`onSuspend` publishes progress on the event's context-bound notifier. Default
fixture and measurement phase hooks also publish progress. Case subject
start/finish records remain runner-owned even when a custom visitor filters
source hooks.

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

The visitor is retained by the run and its contexts. `FrameworkEvent` exposes
`notifier`, `name`, `phase`, `suspension`, and `location`; string views live only
for the callback. Run start/finish hooks have no case subject. Other hook pairs
are `onCaseStart`/`onCaseFinish`, `onFixtureStart`/`onFixtureFinish`, and
`onPhaseStart`/`onPhaseFinish`.

A suspension event means a framework registration boundary was reached in
`await_suspend`. The covered boundaries are yield, timer, event, value and void
future, custom continuation registration, composition, and native child
transfer. An `await_ready` result of `true` produces no suspension event. A ready
race, a subsequent `false` return, or a throw after registration still counts as
reaching that boundary. It does not mean the coroutine necessarily remained
suspended. Scheduler rearming does not generate another source event.

Hooks execute outside framework locks. They must be short, nonblocking,
`noexcept`, and must not reenter the runner. Adopted threads may invoke hooks
concurrently, so any visitor-owned state needs suitable synchronization. A hook
can invoke the notifier directly; it must not wait for delivery or perform slow
observer work. Supplying a visitor without a transport is supported and still
provides distinct subject IDs. With neither a notifier nor a visitor configured,
source observation creates no threads, queues, or allocations per event.

Rejected runtime notifications are infrastructure failures, including ignored
return values from explicit current-context notifications. The runner continues
teardown and returns failure. `xfail` cannot turn notification infrastructure
failure into a successful run.

## Monitoring and observers

`WatchdogModel` is portable and deterministic. Its owner supplies monotonic time
to `accept(record, now)` and `advance(now)`; the model does not read a clock or
create a thread. `nextDeadline()` exposes the next active deadline, and
`failure()` retains the first failure. Producer and subject tracking are bounded
by configuration.

On Linux, `WatchdogMonitor` owns the receiving descriptors and enforces the model
on a dedicated thread. It also guards startup and gaps with no active subjects,
so merely never starting a subject does not disable inactivity detection. A
registered pidfd identifies the process owner. `setProcessOwner(pidfd)` duplicates
the pidfd; the launcher still owns the child and must reap it. On failure, the
monitor requests stop, then escalates through pidfds after `shutdownGrace`, with
a finite `shutdownCutoff`. An optional stop eventfd can hand cooperative cleanup
to a session owner before escalation.

`NotificationObserver` is a separate delivery interface:

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

Observer callbacks are serialized on a separate dispatcher with a bounded queue.
They never execute on the enforcement thread. `onExpired` reports terminal
monitor failures, including transport or observation failures, as well as
inactivity. An observer exception or queue overflow is an observation failure;
it is not silently discarded. A blocked observer cannot prevent deadline
processing or process termination, but `close()` can wait for that callback to
return while draining the dispatcher. Observer callbacks must therefore return
promptly. Read `outcome().failure` for the authoritative verdict even when the
observer itself failed.

`disarm()` stops inactivity enforcement while retaining transport drainage for
owner-directed shutdown. After the owner has collected its process, `finish()`
requests a final receive drain. `close()` completes enforcement and then drains
callbacks. Process exit alone does not imply success: inspect both the process
status and the monitor outcome.

## Bounded transport and failures

The Linux producer uses a bounded local queue and a dedicated writer. Admission
does not wait for queue capacity, and accepted records are written in sequence.
A separate fault eventfd reports terminal producer failure even when the packet
socket is backpressured. Queue overflow, invalid records, oversized payloads,
transport failure, and bounded close failure remain explicit errors.

The versioned wire format is little-endian, uses fixed-width fields, and has a
4096-byte record limit including framing. It carries no native structures,
pointers, or producer clock timestamps. Use `encodeNotification` and
`decodeNotification` instead of sending a C++ structure's object representation.
Custom sinks receive sequence zero and assign a strictly increasing sequence per
producer incarnation when serializing admitted records. `NotificationSink::onError`
allows a sink to latch validation failures that occur before `submit`.

The Linux producer queue and observer queue default to 256 records each; the
watchdog defaults to at most 256 tracked subjects. Size these independently for
the expected workload. Overflow does not switch to unbounded buffering or drop
liveness records while claiming success. `LinuxNotificationSink::close(timeout)`
performs a bounded drain; keep the receiver running while it drains.

## Standalone Linux example

[The standalone service example](../examples/notifications/service.cpp) links
only `gentest::notifications_linux`, without the test runtime. It launches its
own child, passes a fresh socket and fault eventfd through exec, installs a
pidfd-backed monitor, and reaps the child after supervision.

With a normal testing build configured:

```sh
cmake --build build/debug-system --target gentest_notifications_example
build/debug-system/examples/notifications/gentest_notifications_example
build/debug-system/examples/notifications/gentest_notifications_example --stall
```

The ordinary run completes successfully. `--stall` stops reporting while spinning
without cooperative yields; the parent enforces inactivity and exits with a
failure diagnostic. The example's launcher handles descriptor transfer and child
collection; applications embedding the transport retain the same responsibilities.

For network test programs, see the harness's
[per-program watchdog configuration](../experimental/harness/README.md#per-program-progress-watchdogs).

The implemented transport uses inherited local Linux endpoints. Multi-host
transport remains future work: it must preserve producer generations, ordered
admission, finite allowances, and explicit overload failure, and authenticate
remote producers. The canonical record format carries no local descriptor or
native clock representation. Portable core and framework APIs do not depend on
the Linux adapter; native macOS and Windows transports remain pending.
