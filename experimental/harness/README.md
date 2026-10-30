# Experimental Linux network harness

This opt-in POC runs real UDP programs on one Linux machine with separate network
stacks, stateful NAT, firewall rules and traffic shaping. Test coordination uses
shared memory, so blocking the tested network does not block readiness messages,
ICE signalling or shutdown. The API is experimental, source-build only, and is
not installed with Gentest 2.0.0.

## Build and run

Requirements: C++20 with exceptions, Linux 5.11 or newer, glibc 2.34 or newer,
unprivileged user namespaces, `unshare` from util-linux, `ip`/`tc` from iproute2,
`nft` from nftables, and `sysctl` from procps. The kernel must support veth,
bridges, conntrack/NAT, nftables and netem. All tools must be on `PATH`.

```sh
cmake --preset=debug-system -B build/harness \
  -DGENTEST_BUILD_HARNESS=ON -DGENTEST_ENABLE_PACKAGE_TESTS=OFF \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build/harness --target gentest_harness_tests
ctest --test-dir build/harness -R '^harness_' --output-on-failure --no-tests=error
```

`GENTEST_BUILD_HARNESS` and `GENTEST_BUILD_RING_EXAMPLE` default to `OFF`.
Enabling the harness on another OS fails at configure time. Explicitly selected
network tests fail if prerequisites are missing; they do not silently skip.
Building only the libraries does not require codegen. Tests/examples require
`GENTEST_BUILD_CODEGEN=ON` or an external `GENTEST_CODEGEN_EXECUTABLE`.

The sanitizer lane uses the same targets with `--preset=alusan-system` and a
separate build directory. The ordinary `debug-system` build remains unchanged
when these options are off.

## Owning a session

Link the test to `gentest::harness` and its child program to
`gentest::harness_client`. Only the parent target depends on Gentest's async
runtime. Supply the helper path using a CMake target-file expression:

```cmake
target_link_libraries(network_tests PRIVATE gentest::gentest_main gentest::harness)
target_compile_definitions(network_tests PRIVATE
    HARNESS_SUPERVISOR="$<TARGET_FILE:gentest_harness_supervisor>"
    PEER="$<TARGET_FILE:udp_peer>")
add_dependencies(network_tests udp_peer)
target_link_libraries(udp_peer PRIVATE gentest::harness_client)
gentest_attach_codegen(network_tests)
```

An ordinary local fixture can hold a `gentest::harness::Session`. Start it from
the async test, and keep the fixture alive until its operations finish. A local
variable provides the same ownership. Describe the network in the test with
ordinary nodes and links, then pass that data and the child programs to the session:

```cpp
using namespace gentest;
using namespace gentest::async;
using namespace gentest::harness;
using namespace std::chrono_literals;

auto exercise() -> awaitable<> {
    SessionSpec spec;
    spec.supervisor = HARNESS_SUPERVISOR;
    spec.topology.nodes = {{.name = "peer_a"}, {.name = "peer_b"}};
    spec.topology.links = {{
        .left = {.node = "peer_a", .interface = "eth0", .address = "10.0.0.1/30"},
        .right = {.node = "peer_b", .interface = "eth0", .address = "10.0.0.2/30"},
    }};
    spec.programs = {{"peer_a", {PEER}, {}, {}}, {"peer_b", {PEER}, {}, {}}};

    auto session = co_await Session::start(std::move(spec));
    co_await when_all(session.launch(0), session.launch(1));

    auto ready = co_await wait_for(when_all(session.receive(0), session.receive(1)), 5s);
    assert_true(ready.ready(), "peers did not become ready");
    session.send(0, {7, "begin"});
    session.send(1, {7, "begin"});
    auto [a, b] = co_await when_all(session.wait(0), session.wait(1));
    expect_true(a.succeeded(), a.standardError);
    expect_true(b.succeeded(), b.standardError);
    co_await session.stop();
}
```

The child opens `Channel::fromEnvironment()`, sends a readiness message, receives
the start message with a steady-clock deadline, exercises its real network code,
then exits. See [the UDP helper](tests/peer.cpp) for a complete program.

`launch()` acknowledges process creation; readiness is an application message and
exec failure is an exit result. `wait()` returns exit/signal status and the last
1500 bytes of each output stream. `inspect()` returns the same snapshot without
waiting; inspect `running` before interpreting exit status. `outputTruncated`
reports discarded output. There is one launch per program and at most 16 programs
and 16 topology nodes per session.

`signal(program, signal)` targets the original launched process through its
retained pidfd. It returns `SignalResult::sent` when the kernel accepts the
submission, or `SignalResult::alreadyExited` when the process has exited,
including an exit racing submission. The acknowledgement does not mean the
process terminated: use `wait()` to observe termination. Signal zero is an
existence probe and delivers no signal. Negative signals, values at least `NSIG`,
invalid indices and unlaunched programs are errors, including after another
program exits. Program indices never shift or get reused. Cancellation stops
waiting for acknowledgement; it cannot undo a submitted signal. Process groups
and descendants are not signal targets; session cleanup still handles the whole
private process tree.

## Running a session with deadlines

Include `gentest/harness_run.h` to let `run_session` own startup, a scenario and
cleanup. Its callable is stored by value until the invocation and cleanup finish;
references captured by the callable still borrow their objects. The callback
returns `gentest::awaitable<>` and receives `Session&`.

```cpp
auto checkWorkers(gentest::harness::SessionSpec spec) -> gentest::awaitable<> {
    using namespace gentest::harness;
    using namespace std::chrono_literals;
    const auto started = std::chrono::steady_clock::now();

    co_await run_session(
        std::move(spec), RunOptions{started + 50s, started + 55s},
        [](Session& session) -> gentest::awaitable<> {
            co_await session.launch(0);
            auto result = co_await session.wait(0);
            if (!result.succeeded()) {
                throw std::runtime_error("worker failed");
            }
        });
}
```

The work deadline covers startup and the callback. Timeout requests cancellation
and joins work before cleanup begins. Work exceptions, including errors raised
while responding to cancellation, are retained outside the timed operation.
Cleanup ignores inherited cancellation and uses its own absolute deadline,
including during partial startup. `cleanup_deadline` must be finite and strictly
after `work_deadline`; invalid options throw `std::invalid_argument` before any
resources are acquired. An already-expired work deadline does not start a session.

Operational failures throw `SessionRunError`. Its `timed_out()`, `work_error()`
and `cleanup_error()` accessors independently expose timeout and both original
exception pointers; its diagnostic includes both errors. Expected-failure tests
can rethrow either pointer to check its original type. Ordinary tests should let
the error propagate. Cancellation is retained as the work exception. Successful
cleanup does not imply successful child exit: the callback checks `ProcessResult`.

Await `run_session` directly. An outer `wait_until(run_session(...), deadline)`
can hide errors raised during cleanup after that outer timeout wins. Standalone
session owners can call `stop_until(deadline)` for explicit cleanup with the same
cancellation policy. Keep an external process watchdog beyond the cleanup
deadline: cancellation cannot preempt blocking application code or guarantee
immediate kernel reaping. `when_all` still joins all inputs without cancelling
siblings on failure.

## Per-program progress watchdogs

Set `Program::watchdog` to opt a child into progress supervision. This adds a
separate notification socket and fault eventfd; the application control channel
continues to carry its existing messages. Programs without a watchdog retain
their existing behavior.

```cpp
using namespace std::chrono_literals;

gentest::harness::Program worker{
    .node = "worker",
    .argv = {WORKER},
    .watchdog = gentest::WatchdogConfig{
        .inactivityTimeout = 2s,
        .maximumSilence = 10s,
        .maximumSubjects = 16,
    },
};
```

The child links `gentest::notifications_linux` in addition to any application
channel library and attaches once with `gentest::notifierFromEnvironment()`.
The session supplies its producer identity and inherited descriptors. Keep and
copy the resulting handle; a successful attachment consumes those descriptors
and removes the environment variables.

```cpp
#include <gentest/notification_linux.h>

int main() {
    auto notifications = gentest::notifierFromEnvironment();
    const auto accepted = [](gentest::NotifyResult result) {
        return result == gentest::NotifyResult::accepted ||
               result == gentest::NotifyResult::disabled;
    };
    if (!accepted(notifications.start("worker request"))) {
        return 1;
    }

    // Advance the application operation, then report that specific progress.
    if (!accepted(notifications.progress("request processed"))) {
        return 1;
    }
    return accepted(notifications.finish()) ? 0 : 1;
}
```

A child running Gentest cases can pass this attached notifier to
`RunConfiguration` instead; the runner then owns per-case subjects and automatic
framework suspension progress. The default Gentest main does not automatically
attach the Linux transport. See the complete
[notification and source-event API](../../docs/notifications.md).

Each subject has its own inactivity deadline. Another subject or another
program making progress cannot renew it. Ordinary application messages, logging,
stdout, and stderr are not progress reports. An explicit, finite
`allowSilenceFor(duration, reason)` temporarily sets a quiet deadline from
monitor receipt time, subject to `maximumSilence`; the next `progress()` restores
the normal inactivity timeout. Use this only for intended quiet work and keep
that operation's own deadline. A timer wait or long benchmark does not grant an
allowance automatically.

A watchdog is armed when the program is launched. It covers startup before the
first subject and idle gaps between subjects, as well as active subjects. A child
that never attaches or stops reporting can therefore fail without yielding or
sending an application message. The monitor operates independently of the
Gentest runner and application channel.

Watchdog expiry, invalid notifications, queue overflow, transport failure, or
observer failure fail the session and request cleanup of the entire private
process tree, including other programs. Cleanup does not depend on the stalled
child responding. It requests cooperative termination, then escalates through
pidfds to the namespace owner and launcher if necessary. Explicit session
cleanup deadlines also bound the watchdog shutdown grace and cutoff. Normal
`stop()`/`stop_until()` disarms inactivity during collection, drains notification
results, and preserves an already-recorded watchdog failure.

A completed child must be acknowledged promptly by awaiting `Session::wait()`,
inspecting its terminal result with `Session::inspect()`, or receiving
`SignalResult::alreadyExited` from `Session::signal()`. These operations finish
its watchdog. The harness monitor owns a pidfd for the containing namespace,
so a child that sends `finish()` and exits without a parent acknowledgement can
still hit the idle timeout while the parent keeps the session open. An
application-channel EOF is not that acknowledgement. The standalone Linux
monitor can instead own the individual service pidfd and observe its exit
directly.

`Program::notificationObserver` optionally supplies a shared
`NotificationObserver`. Its callbacks are serialized on a separate bounded
dispatcher, never on the enforcement thread. They must return promptly: callback
exceptions or queue overflow are terminal observation failures, and a callback
that never returns can delay observer drainage. It cannot prevent process
termination. Source visitors are a separate synchronous interface inside the
child runtime.

Watchdog configuration consumes two of the eight inherited descriptor mappings,
leaving at most six caller-supplied `inheritedFds` for that program. The session
selects unused descriptor targets and owns `GENTEST_NOTIFY_FDS` and
`GENTEST_NOTIFY_ID`; specifying either environment key yourself is rejected.
`ChildLimits::openFiles`, when supplied, must exceed every final target,
including the two automatically added notification descriptors. Invalid budgets
or configuration fail startup before the supervisor is launched.

A watchdog detects absence of the progress signals you selected. Reporting from
an unrelated heartbeat while application work is stuck can conceal that failure;
it is not a general proof that the application is free of deadlocks.

## Isolation and cleanup

One parent monitor thread watches shared-memory notifications and the supervisor
pidfd. Guarded Gentest continuations deliver completions on the owning test
thread, supporting `when_all`, `when_any`, `wait_for` and cancellation. One active
receive per program is supported. Cancelling a wait unregisters it; cancelling
startup or launch tears down the session. Cancelling an `execute()` wait does not
undo a command already accepted by the supervisor; commands have a five-second
execution deadline. Use `stop()` to tear down the whole topology.

The supervisor is PID 1 inside an `unshare` user/network/PID/mount namespace tree.
It checks that it has a single-ID rootless mapping and a different network
namespace before configuring anything. Node namespaces are held by child
processes and open descriptors. Commands receive argv directly; there is no
shell, daemon, sudo, host firewall modification or persistent `/run/netns` entry.
`bwrap` and Podman are unnecessary for this network/process isolation POC.

Supervisor startup uses an empty signal mask, so signals blocked by the calling
test thread do not remain blocked in the supervisor or launched programs. The
caller's signal mask is unchanged; child SIGTERM and SIGINT dispositions are
reset before exec.

Session destruction closes a liveness pipe, requests child termination and reaps
the supervisor. Programs and namespace keepers receive TERM together and share
one grace period of at most one second, followed by KILL for survivors. An
explicit cleanup deadline also caps that grace period. The parent normally
allows five seconds for cleanup before killing the `unshare` wrapper;
`--kill-child=KILL` and PID-namespace teardown kill remaining descendants. An
inherited runner pidfd independently detects runner exit, even if a forked child
keeps the liveness pipe open. Both signals are watched during setup, command
execution and normal operation. Tests cover partial setup, failed exec, abrupt
peer death, cancellation, TERM-ignoring descendants and a killed test runner
whose forked child retains the pipe writer.
Only explicitly selected channels and program descriptor mappings survive child exec.

Run trusted test binaries: they retain the calling user's filesystem access.
This POC isolates network stacks and process lifetime, not hostile code or files.

## Network model

`Topology` is user-supplied data: populate its `nodes`, `links`, `routes` and
`nats` fields, then pass it through `SessionSpec` to `Session::start()`. Your test
chooses node names, addresses, connections and programs. The harness creates the
network, applies its configuration and manages process lifetime; application
protocols and traffic patterns belong to the child programs.

`Node::ipv4Forwarding` and `Node::ipv6Forwarding` independently enable kernel
IPv4 and IPv6 routing in that node's network namespace. Both flags default to
`false`, which leaves the new namespace's kernel defaults unchanged; it does not
force forwarding off. Setting both flags enables dual-stack routing. Application-level
UDP forwarding, such as the ring example, does not require either flag.
Enabling IPv6 forwarding requires kernel IPv6 support; a failed sysctl fails
session startup and tears down the partially configured topology.

`Nat::insideInterface` and `Nat::outsideInterface` name interfaces within
`Nat::node`'s network namespace.

Callers using the earlier POC field names should update their initializers:

| Earlier field | Current field |
| --- | --- |
| `Node::forwarding` | `Node::ipv4Forwarding` |
| `Nat::inside` | `Nat::insideInterface` |
| `Nat::outside` | `Nat::outsideInterface` |

These source renames preserve the network configuration and private IPC layout.

A node may own an IPv4 bridge. An empty link-end address attaches that interface
to the node's bridge. The NAT tests use a
[test-local two-NAT recipe](examples/support/two_nat.h) built from these structures:

```text
peer_a 10.0.1.2 -- 10.0.1.1 nat_a 198.18.0.2 --+
                                             +-- internet bridge 198.18.0.1/24
peer_b 10.0.2.2 -- 10.0.2.1 nat_b 198.18.0.3 --+
```

Each gateway masquerades outbound packets and accepts established/related return
traffic. Unmapped WAN traffic is dropped before conntrack confirmation, so an
early UDP hole punch cannot reserve a reverse tuple and force the other peer's
later outbound mapping to change ports. There is no static inbound forwarding.
This models one useful IPv4 NAT/firewall configuration, not every consumer router
or symmetric NAT. `execute(node, argv, stdin)` supports `ip`, `nft` and `tc` for
additional test policy, packet counters and netem. It always runs inside a node.

[Network tests](tests/cases.cpp) bind port 5000 in multiple nodes, verify observed
source translations and blocked unsolicited inbound traffic, deliberately order
hole punches, drop UDP while shared-memory control still works, and deliver a
datagram through netem delay.

## Shared-memory control

Each duplex channel has a size-sealed anonymous memfd, two eventfds, and two
bounded queues protected by robust process-shared pthread mutexes. Each direction
holds 16 messages of up to 4096 payload bytes. Messages copy their bytes and carry
a 32-bit application tag. Oversized messages are rejected before locking. Full
queues are rejected without waiting for capacity, after acquiring the queue lock;
there is no silent loss or unbounded buffering. Topology/command descriptions must
also fit a single control message, including framing.

`Channel::receive(deadline, stop_token)` provides a blocking child API;
`Session::receive()` provides the parent's async API. Already queued messages are
read before normal closure is reported as `ChannelClosed`, including after a
program exits. This EOF says nothing about process success: check `wait()`'s
exit code and signal separately. `Channel::tryReceive()` does not wait for message
arrival, but still acquires the queue mutex and can throw on lock failure or
closure of an empty queue. Owner death, corruption, lock failures and supervisor
failure remain separate errors and are never converted into `ChannelClosed`.
A dead mutex owner permanently invalidates that queue; a
live but stalled holder is bounded by a one-second lock deadline. Child exit is
detected by the supervisor and wakes receivers. Standalone channels require
explicit `close()` or a deadline to detect abandonment. This is a trusted,
same-build local ABI, not a persistent or network protocol.

The old `coordd` POC at commit `b03123e2` supplied useful process-spec and readiness
ideas. Its long-lived coordinator, socket transport, manifests and remote modes
are not needed here; its `netd-helper` was an unimplemented stub. Session lifetime
and tagged shared-memory messages with application-defined payloads now stay with
the test. No codegen attributes,
runner scheduling rules or fixture lifetimes change.

## Boost.Asio UDP ring

The [ring example](examples/ring/cases.cpp) constructs its own topology using
the public nodes and links. Its local `makeRing()` helper supports 3 through 16
peers. Each adjacent pair has its own veth link and IPv4 `/30` subnet. Every peer
has a `prev` and `next` interface. The application receives UDP from its predecessor
and forwards it to its successor; kernel IP forwarding is off.

For example, a test can describe a three-peer ring directly:

```cpp
Topology topology{
    .nodes = {{.name = "a"}, {.name = "b"}, {.name = "c"}},
    .links = {
        {{"a", "next", "10.0.1.1/30"}, {"b", "prev", "10.0.1.2/30"}},
        {{"b", "next", "10.0.2.1/30"}, {"c", "prev", "10.0.2.2/30"}},
        {{"c", "next", "10.0.3.1/30"}, {"a", "prev", "10.0.3.2/30"}},
    },
};

SessionSpec spec;
spec.topology = std::move(topology);
// Add your programs for nodes a, b and c, then start the session.
```

The [controller](examples/ring/cases.cpp) assigns one
[Boost.Asio peer](examples/ring/peer.cpp) to each node. Every peer binds UDP port
5000 in its own network namespace. Peer zero seeds a fixed set of 256-byte tokens
that keep circulating until the controller requests a drain. Each hop validates
the source endpoint, token sequence and payload. After every token completes at
least 256 laps, the controller asks peer zero to consume the returning tokens,
then stops the other peers and compares all send/receive and per-token counts.
Progress, readiness, statistics and shutdown use shared memory throughout.

The example requires Boost 1.85 or newer headers. Boost is only discovered when
this example is enabled; it adds no dependency to the harness libraries. With
vcpkg, select the optional `harness-asio` manifest feature (for example,
`-DVCPKG_MANIFEST_FEATURES=harness-asio`); its version uses the repository's
pinned registry baseline. With system packages, install the Boost development
headers.

```sh
cmake --preset=debug-system -B build/harness \
  -DGENTEST_BUILD_HARNESS=ON -DGENTEST_BUILD_RING_EXAMPLE=ON \
  -DGENTEST_ENABLE_PACKAGE_TESTS=OFF \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build/harness --target gentest_ring_example
ctest --test-dir build/harness -R '^harness_asio_ring$' --output-on-failure --no-tests=error
```

The suite exercises 3-, 4- and 16-peer rings, the example's node-count bounds,
malformed packets, and an nftables rule that breaks the ring. That fault case
verifies the sender reports Linux's `EPERM` send error, the firewall counts a
dropped packet, and the remaining peers stop through shared memory. Runs have
deadlines and fail on unexpected packet loss or missing progress. The peer uses
an Asio descriptor wait on a duplicate of `Channel::notificationFd()`; only
`Channel::tryReceive()` consumes that notification, keeping control coordination
on the same event loop as UDP without a polling thread.

## Child descriptors, output files and limits

`Program::inheritedFds` accepts up to eight `InheritedFd{source, target}` mappings.
An enabled `Program::watchdog` reserves two of those mappings, leaving six for
caller-supplied descriptors.
Sources are borrowed caller descriptors. Keep them open until the awaited
`Session::start()` returns; startup retains owned copies for every program,
including programs launched later. Closing the originals afterwards is safe.
The copies share file offsets and status flags with the originals. Directory
sources can serve as `openat()` anchors even if their pathname is replaced.

Targets must be unique integers from 3 through 255. The session reserves channel
descriptors 3 through `5 + 3 * programCount`; explicit mappings cannot use that
range. The selected program's own channel and explicit mappings survive exec;
other programs' descriptors do not. Sources and targets may overlap or form
cycles. Startup validates all program descriptions and their complete 4096-byte
encoded messages before starting the supervisor.

Set `Program::standardOutputFd` or `standardErrorFd` to a writable regular-file
descriptor to send that stream directly to the file. The harness does not open,
truncate, rotate or copy the file, and preserves its append mode. A redirected
stream has an empty `ProcessResult` capture field. Unredirected streams retain
the usual 1500-byte tails; `outputTruncated` describes discarded captured output
only. No supervisor thread writes or tees log data.

`Program::childLimits` optionally holds `ChildLimits`, whose optional
`std::uint64_t` fields are `coreBytes`, `openFiles`, `fileBytes` and `cpuSeconds`.
They set both the soft and hard Linux `RLIMIT_CORE`, `RLIMIT_NOFILE`,
`RLIMIT_FSIZE` and `RLIMIT_CPU` limits, respectively. Omitted fields preserve the
inherited soft/hard pair. Requested values must be finite and no greater than
the inherited hard limit; `cpuSeconds` must be positive. `openFiles` must exceed
every final descriptor number, including the selected channel and any watchdog
notification descriptors. Limits apply in
the child after descriptor mappings, without changing the runner or supervisor.
A file-size limit can terminate a logging child with `SIGXFSZ`; inspect the exit
result to distinguish a bounded log from a successful run.

Descriptor or resource-limit validation errors fail startup. Child setup errors
(such as failed `chdir()` or `setrlimit()`) fail `launch()` with a diagnostic even
when stderr is redirected. Executable lookup failures remain exit results with
code 127 and a diagnostic on the selected stderr stream. A launch failure closes
the session and cleans up its children.
