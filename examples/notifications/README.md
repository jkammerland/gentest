# Standalone progress watchdog (Linux)

This example is a plain program, not a test executable. It shows how to
supervise another process with gentest's Linux notification transport: the
child reports progress, and when the progress stops, the parent's watchdog
ends the child and reports a failure. It links only
`gentest::notifications_linux`, without the test runtime.

## Build and run

The directory has no `project()` of its own, so it builds inside the gentest
source tree. A Linux build with tests enabled includes it; the `debug-system`
preset enables tests:

```bash
cmake --preset=debug-system
cmake --build build/debug-system --target gentest_notifications_example
build/debug-system/examples/notifications/gentest_notifications_example
build/debug-system/examples/notifications/gentest_notifications_example --stall
```

The first run completes and exits with status 0. The parent prints each
notification it receives; the last one is the `finish()` record, whose payload
is empty:

```text
notification 1: example service
notification 2: processing a request
notification 3: request complete
notification 4:
```

With `--stall`, the child stops reporting and spins without yielding. After the
500 ms inactivity timeout, the watchdog terminates the child, and the parent
reports the failure and exits with status 1:

```text
notification 1: example service
notification 2: processing a request
watchdog failure 0: example service no observed progress before inactivity deadline
service failure 0: example service no observed progress before inactivity deadline
```

## How it works

[service.cpp](service.cpp) runs as both parent and child:

1. The parent creates a `LinuxNotificationChannel` and starts itself again with
   `--service`. It maps the channel's producer socket and fault eventfd to
   descriptors 200 and 201 in the child and names them in the child's
   environment (`GENTEST_NOTIFY_FDS=200,201`, `GENTEST_NOTIFY_ID=1,1`).
2. The child attaches with `gentest::notifierFromEnvironment()`, starts a
   subject, sends a status notification, reports progress and finishes.
3. The parent gives the child's pidfd to a `WatchdogMonitor` with a 500 ms
   inactivity timeout and a `ConsoleObserver` that prints each record. When
   the timeout expires, the monitor sends the child `SIGTERM`, and `SIGKILL`
   if it hasn't exited after the 100 ms shutdown grace. The parent waits for
   the child, then reads the monitor's outcome.

The parent owns the launch, the descriptor transfer and the reaping of its
child. A program that embeds the transport takes on the same jobs.

For the notification and watchdog API, see
[Notifications and progress watchdogs](../../docs/notifications.md). To
supervise programs inside a test, the
[Experimental Linux network harness](../../experimental/harness/README.md) can
attach a watchdog to each program it launches.
