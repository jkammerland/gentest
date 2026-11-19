# Network harness design

This page explains how the network harness works and gives the exact behavior
of each session operation. Read the
[Experimental Linux network harness](README.md) README first: it covers
requirements, building the harness and writing a test.

## Components

| Part | Kind | Role |
| --- | --- | --- |
| `gentest::harness` | Static library, test side | `Session` and `run_session()`. Links gentest's runtime and `gentest::notifications_linux`. |
| `gentest::harness_client` | Static library, both sides | `Channel`, the shared-memory message queue. Needs only threads, not gentest's runtime. |
| `gentest_harness_supervisor` | Executable | Runs inside the private namespaces, builds the network, launches programs and runs commands. |

A started session forms this process tree:

```text
test process (runner thread, plus one monitor thread per session)
└── unshare --user --map-root-user --net --pid --fork --kill-child=KILL --mount-proc
    └── gentest_harness_supervisor (PID 1 of the private namespaces)
        ├── one namespace keeper per node, holding that node's network namespace
        └── the launched programs, each in its node's network namespace
```

## Design choices

- The harness is a library for ordinary async tests. It adds no codegen
  attributes and changes no runner scheduling rules or fixture lifetimes. The
  test owns the session.
- Control messages travel over shared memory, not over the network under test.
  A test can block or delay UDP while readiness, statistics and shutdown keep
  working.
- Everything runs rootless, in a user namespace with a single-ID mapping.
  Commands receive their argv directly: there is no shell, daemon, `sudo`, host
  firewall change or persistent `/run/netns` entry, and no `bwrap` or Podman.
- The test describes the network as data. The harness creates it and manages
  process lifetime; application protocols and traffic belong to the child
  programs.

## Session operations

Programs are numbered by their position in `SessionSpec::programs`. Indices
never shift or get reused, and each program can be launched once.

One monitor thread per session watches the shared-memory notifications and the
supervisor's pidfd, and delivers each completion on the owning test thread, so
session operations work with `when_all`, `when_any`, `wait_for` and
cancellation. `when_all` joins all its inputs and doesn't cancel the others
when one fails.

- `Session::start(spec)` starts the supervisor and builds the network. The
  overload `Session::start(spec, cleanup_deadline)` also bounds cleanup: if
  startup fails, it tears the session down by that deadline and throws
  `SessionRunError` when the cleanup fails too.
- `launch(program)` completes when the process exists. Readiness is an
  application message. A failed executable lookup is an exit result: exit code
  127, with a diagnostic on the program's stderr stream. Child setup errors,
  such as a failed `chdir()` or `setrlimit()`, fail `launch()` with a
  diagnostic even when stderr is redirected. A failed or cancelled launch
  closes the session and cleans up its children.
- `send(program, message)` queues a message for the program; `receive(program)`
  awaits the next one. Each program allows one pending `receive()`. Cancelling
  a receive unregisters it.
- `wait(program)` returns the exit code or signal and the last 1500 bytes of
  each output stream. `inspect(program)` returns the same snapshot without
  waiting; check `running` before you read the exit status. `outputTruncated`
  reports that captured output was discarded. A session allows 16 pending
  waits.
- `signal(program, signal)` targets the launched process through its pidfd,
  not its process group or descendants. It returns `SignalResult::sent` when
  the kernel accepts the signal (use `wait()` to see the process end), or
  `SignalResult::alreadyExited`, also when the exit races the call. Signal zero
  only checks that the process exists. Negative signals, values of `NSIG` or
  more, invalid indices and programs never launched are errors. Cancellation
  can't undo a submitted signal.
- `execute(node, argv, input)` runs `ip`, `nft` or `tc` inside a node, for
  extra policy, packet counters or netem, with a five-second deadline. On exit
  status 0 it returns the command's stdout; stdout larger than 4088 bytes fails
  the call. A failing command reports its exit status or signal and the last
  1500 bytes of stderr. Cancelling doesn't undo a command the supervisor has
  accepted.
- `stop()` tears down the whole topology with a five-second cleanup deadline;
  `stop_until(deadline)` uses yours. Both ignore inherited cancellation. When
  the deadline passes, cleanup escalates to `SIGKILL`, collects every process,
  and then reports the overrun as an error. Cancelling startup or a launch also
  tears the session down.

## Deadlines with run_session

`run_session(spec, options, body)` owns startup, the body and cleanup. It
stores the body callable by value; references it captures only borrow. The body
receives `Session&` and returns `gentest::awaitable<>`.

- The work deadline (`RunOptions::work_deadline`) covers startup and the body.
  On timeout, `run_session()` requests cancellation and joins the work before
  cleanup begins. Errors raised while responding to cancellation are kept as
  work exceptions.
- Cleanup ignores inherited cancellation and uses its own absolute deadline,
  also during a partial startup. `cleanup_deadline` must be finite and strictly
  after `work_deadline`; otherwise `run_session()` throws
  `std::invalid_argument` before it acquires any resources. An expired work
  deadline doesn't start a session.
- Failures throw `SessionRunError`. Its `timed_out()`, `work_error()` and
  `cleanup_error()` accessors report the timeout and both original exceptions
  (cancellation counts as a work exception), and its message includes both.
  Ordinary tests let it propagate.
- Successful cleanup doesn't mean the programs succeeded: the body checks each
  `ProcessResult`.
- Await `run_session()` directly. An outer `wait_until(run_session(...), deadline)`
  can hide errors raised during cleanup after the outer timeout wins.
- A session you own directly can call `stop_until(deadline)` for explicit
  cleanup with the same cancellation policy.

Keep a process-level timeout longer than the cleanup deadline, such as a CTest
`TIMEOUT`. Cancellation can't preempt blocking code or guarantee that the
kernel reaps a process immediately.

## Watchdogs

`Program::watchdog` opts a program into progress supervision. It adds a
notification socket and fault eventfd separate from the program's channel. A
program without a watchdog gets no notification descriptors or environment
variables.

The child links `gentest::notifications_linux`, next to any channel library,
and attaches once with `gentest::notifierFromEnvironment()`. The session
supplies the producer identity and the inherited descriptors. Keep and copy the
returned notifier: a successful attachment takes over those descriptors and
removes the environment variables. A child that runs gentest cases can pass the
notifier to `RunConfiguration` instead; the runner then owns per-case subjects
and reports progress automatically when the framework suspends. The default
`gentest_main` doesn't attach the Linux transport.

- **Deadlines.** Each subject has its own inactivity deadline. Another subject
  or another program making progress can't renew it. Application messages,
  logging, stdout and stderr are not progress reports. An explicit, finite
  `allowSilenceFor(duration, reason)` sets a temporary quiet deadline, limited
  by `maximumSilence`, until the next `progress()`. Use it only for intended
  quiet work; a timer wait or a long benchmark doesn't grant an allowance by
  itself.
- **Coverage.** The watchdog is armed when the program is launched. It covers
  startup before the first subject, the gaps between subjects, and every active
  subject, so a program that never attaches also fails. The monitor runs
  independently of the gentest runner and of the program's channel.
- **Failure.** Watchdog expiry, an invalid notification, queue overflow, a
  transport failure or an observer failure fails the session and cleans up the
  whole private process tree, including the other programs. Cleanup doesn't
  need the stalled program to respond: it requests termination, then escalates
  through pidfds to the namespace owner and the launcher if needed. An explicit
  cleanup deadline also bounds the watchdog's shutdown grace. `stop()` and
  `stop_until()` disarm inactivity checks while they collect the programs and
  keep a watchdog failure that was already recorded.
- **Acknowledgement.** Acknowledge a finished program promptly: await
  `Session::wait()`, read its terminal result with `Session::inspect()`, or get
  `SignalResult::alreadyExited` from `Session::signal()`. These calls finish
  its watchdog after it drains the program's notifications. An acknowledged
  program that never started a subject or left one open fails the session.
  The harness monitor can't see the program's exit by itself, so
  a program that sends `finish()` and exits without this acknowledgement can
  hit the idle timeout while the session stays open. End-of-stream on the
  program's channel is not an acknowledgement.
- **Observers.** `Program::notificationObserver` optionally supplies a shared
  `NotificationObserver`. Its callbacks run one at a time on a bounded
  dispatcher, not the enforcement thread, and must return promptly: a callback
  exception or a full queue fails the session. An observer can't prevent
  process termination.
- **Descriptors and environment.** A watchdog uses two of the program's eight
  inherited descriptor mappings, leaving six for `inheritedFds`. The session
  picks unused descriptor numbers and owns `GENTEST_NOTIFY_FDS` and
  `GENTEST_NOTIFY_ID`; setting either key in `Program::environment` is
  rejected. `ChildLimits::openFiles`, when set, must exceed every final
  descriptor number, including the two notification descriptors. Invalid
  budgets or configuration fail startup before the supervisor is launched.

A watchdog detects the absence of the progress reports you chose. A heartbeat
from unrelated code while the real work is stuck can hide that failure, so a
watchdog doesn't guarantee that the application is free of deadlocks. For the
notification API, see
[Notifications and progress watchdogs](../../docs/notifications.md).

## Network model

`Topology` is plain data: fill its `nodes`, `links`, `routes` and `nats` fields
and pass it to `Session::start()` through `SessionSpec`. The test chooses node
names, addresses, connections and programs.

- `Node::ipv4Forwarding` and `Node::ipv6Forwarding` turn on kernel IPv4 and
  IPv6 routing in that node's network namespace, independently. Both default
  to `false`, which leaves the kernel default unchanged rather than forcing
  forwarding off. Application-level UDP forwarding, as in the ring example,
  needs neither flag. If the IPv6 `sysctl` fails, session startup fails and
  tears down the partly configured topology.
- A node can own an IPv4 bridge (`Node::bridgeAddress`). A link end with an
  empty address joins that node's bridge.
- Each `Link` is a veth pair between two nodes; each `Route` adds a route in a
  node.
- `Nat::insideInterface` and `Nat::outsideInterface` name interfaces inside
  `Nat::node`'s network namespace. Each NAT gateway masquerades outbound
  packets and accepts established and related return traffic. Unmapped traffic
  arriving on the outside interface is dropped before conntrack confirmation,
  so an early UDP hole punch can't force the other peer's later outbound
  mapping to change ports. There is no static inbound forwarding.

The NAT tests build this topology with a
[test-local two-NAT recipe](examples/support/two_nat.h):

```text
peer_a 10.0.1.2 -- 10.0.1.1 nat_a 198.18.0.2 --+
                                             +-- internet bridge 198.18.0.1/24
peer_b 10.0.2.2 -- 10.0.2.1 nat_b 198.18.0.3 --+
```

## Shared-memory channels

Each program has a duplex `Channel`: a size-sealed anonymous memfd, two
eventfds and two bounded queues protected by robust, process-shared pthread
mutexes.

- Each direction holds 16 messages of up to 4096 payload bytes. A message
  copies its bytes and carries a 32-bit application tag.
- Oversized messages are rejected before locking. `send()` rejects a full
  queue without waiting for space, after taking the queue lock; `trySend()`
  returns `false` instead, so a caller can apply backpressure. A channel never
  drops messages silently and never buffers without bound.
- The session's control path uses `trySend()`. Commands and replies that don't
  fit wait in order in the sending process, so more than 16 concurrent requests
  are delayed rather than failing the session. The topology and each command
  must fit in one control message, including framing.
- `Channel::receive(deadline, stop_token)` is the child's blocking API;
  `Session::receive()` is the test's async API. Queued messages are delivered
  before closure is reported as `ChannelClosed`, also after a program exits.
  That says nothing about whether the process succeeded; check `wait()`.
- `Channel::tryReceive()` doesn't wait for a message, but it does take the
  queue mutex. It can throw on a lock failure or when an empty queue is closed.
- Owner death, corruption, lock failures and supervisor failure are separate
  errors and never turn into `ChannelClosed`. A dead mutex owner permanently
  invalidates its queue; a live but stalled holder is bounded by a one-second
  lock deadline.
- The supervisor detects a program's exit and wakes its receivers. A
  standalone channel needs an explicit `close()` or a deadline to detect
  abandonment.

The channel is a trusted local interface between processes from the same
build, not a persistent or network protocol.

## Isolation and cleanup

The supervisor is PID 1 inside an `unshare` user, network, PID and mount
namespace tree. Before configuring anything, it checks that it has a
single-ID rootless mapping and a network namespace different from the
caller's. As namespace init it also reaps orphaned descendants while the
session runs, such as a background process left by a program's shell, so they
don't pile up as zombies; exit results are reported only for launched
programs. Node namespaces are held by keeper processes and open descriptors.

The supervisor starts with an empty signal mask, so signals blocked by the
test thread are not blocked in the supervisor or launched programs. Each
child's `SIGTERM` and `SIGINT` dispositions are reset to the default before
`exec`.

Destroying a session closes a liveness pipe, requests child termination and
reaps the supervisor:

1. Programs and namespace keepers receive `SIGTERM` together and share one
   grace period of at most one second. An explicit cleanup deadline also caps
   that grace period.
2. Survivors receive `SIGKILL`.
3. The test process normally allows five seconds for cleanup before it kills
   the `unshare` wrapper. `--kill-child=KILL` and the teardown of the PID
   namespace kill any remaining descendants.

An inherited pidfd for the test runner detects runner exit even when a forked
child keeps the liveness pipe open. The supervisor watches both at all times.

## Child descriptors, output files and resource limits

**Inherited descriptors.** `Program::inheritedFds` takes up to eight
`InheritedFd{source, target}` mappings, or six when the program has a watchdog.

- Sources are the caller's descriptors, borrowed. Keep them open until the
  awaited `Session::start()` returns; startup keeps its own copies, which share
  file offsets and status flags with the originals.
- Targets must be unique integers from 3 through 255. The session reserves the
  channel descriptors 3 through `5 + 3 * programCount`, so explicit mappings
  can't use that range. Sources and targets may overlap or form cycles.
- The program's own channel and its explicit mappings survive `exec`; other
  programs' descriptors don't.
- Startup validates every program description, and its complete 4096-byte
  encoded message, before it starts the supervisor.

**Output files.** Set `Program::standardOutputFd` or `standardErrorFd` to a
writable regular-file descriptor to send that stream straight to the file. The
harness doesn't open, truncate, rotate or copy the file, and it keeps the
file's append mode. A redirected stream leaves its `ProcessResult` field empty;
other streams keep their 1500-byte tails.

**Resource limits.** `Program::childLimits` optionally holds `ChildLimits`,
whose optional `std::uint64_t` fields `coreBytes`, `openFiles`, `fileBytes` and
`cpuSeconds` set both the soft and the hard `RLIMIT_CORE`, `RLIMIT_NOFILE`,
`RLIMIT_FSIZE` and `RLIMIT_CPU` limits.

- An omitted field keeps the inherited soft and hard pair.
- Values must be finite and no greater than the inherited hard limit, and
  `cpuSeconds` must be positive.
- `openFiles` must exceed every final descriptor number, including the
  program's channel and any watchdog notification descriptors.
- Limits apply in the child after the descriptor mappings; the runner and the
  supervisor keep theirs.
- A file-size limit can end a logging child with `SIGXFSZ`; check the exit
  result to tell a bounded log from a successful run.

Descriptor and resource-limit validation errors fail startup.

## The ring example

The [ring example](examples/ring/cases.cpp) builds 3 to 16 peers with its
`makeRing()` helper. Each adjacent pair has its own veth link and `/30` subnet,
and each peer forwards UDP from its `prev` interface to its `next` one; kernel
IP forwarding stays off.

A test can also describe a three-peer ring directly:

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

The controller in [cases.cpp](examples/ring/cases.cpp) runs one
[Boost.Asio peer](examples/ring/peer.cpp) on each node:

1. Every peer binds UDP port 5000 in its own network namespace.
2. Peer zero seeds a fixed set of 256-byte tokens, which keep circulating until
   the controller asks for a drain. Each hop checks the source endpoint, the
   token sequence and the payload.
3. Once every token has completed at least 256 laps, the controller asks peer
   zero to consume the returning tokens, then stops the other peers and
   compares all send, receive and per-token counts.

Control messages go through shared memory. The peer waits on a duplicate of
`Channel::notificationFd()` with an Asio descriptor wait and consumes messages
with `Channel::tryReceive()`, so control and UDP traffic share one event loop
without a polling thread.

The example's cases cover 3-, 4- and 16-peer rings, the node-count bounds,
malformed packets, and an nftables rule that breaks the ring (the sender sees
`EPERM` and the firewall counts the drop). Every run has deadlines and fails on
unexpected packet loss or missing progress.

CMake looks for Boost (1.85 or newer) only when the example is enabled, so the
harness libraries don't depend on it.

## What the tests cover

Every harness test has the CTest label `harness`.

| CTest tests | What they check |
| --- | --- |
| `harness_ipc` | Channel queues, cross-process messages, mutex owner death, channel failures, pidfd signals |
| `harness_session` | Composition, signals and signal cancellation, failures, orphans, control backpressure, startup, descriptor limits, cleanup, runner death |
| `harness_network` | Stateful NAT (source translation, blocked unsolicited inbound traffic, ordered UDP hole punches), UDP dropped while shared-memory control keeps working, a netem delay, IPv4 and IPv6 forwarding, and a failed forwarding `sysctl` |
| `harness_signal_mask` | Signals blocked by the test thread don't reach the supervisor or programs |
| `harness_runner_inherited_writer`, `harness_runner_during_command` | A killed test runner whose forked child keeps the liveness pipe, while idle and during a command |
| `harness_shutdown_ack`, `harness_shutdown_missing_ack` | Shutdown with and without the supervisor's acknowledgement |
| `harness_descriptors_*` | Inherited descriptors, output files, resource limits and exec boundaries |
| `harness_watchdog_*` | Per-program watchdogs: normal progress, a frozen program, a frozen program next to a busy sibling, and acknowledgement through `inspect()` |
| `harness_session_run_*` | `run_session()` deadlines, startup and cleanup failures, cancellation and nested sessions |
| `harness_group_cleanup_*` | The `SIGTERM` and `SIGKILL` cleanup sequence with real pidfds |
| `harness_asio_ring` | The ring example, when `GENTEST_BUILD_RING_EXAMPLE=ON` |

The tests also cover partial setup, failed `exec`, abrupt peer death,
cancellation and descendants that ignore `SIGTERM`.
