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

`GENTEST_BUILD_HARNESS` defaults to `OFF`.
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
variable provides the same ownership:

```cpp
using namespace gentest;
using namespace gentest::async;
using namespace gentest::harness;
using namespace std::chrono_literals;

auto exercise() -> awaitable<> {
    SessionSpec spec;
    spec.supervisor = HARNESS_SUPERVISOR;
    spec.topology = Topology::twoNat();
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

Session destruction closes a liveness pipe, requests child termination and reaps
the supervisor. Each child gets a one-second TERM grace period, then KILL. The
parent allows five seconds for cleanup before killing the `unshare` wrapper;
`--kill-child=KILL` and PID-namespace teardown kill remaining descendants. An
inherited runner pidfd independently detects runner exit, even if a forked child
keeps the liveness pipe open. Both signals are watched during setup, command
execution and normal operation. Tests cover partial setup, failed exec, abrupt
peer death, cancellation, TERM-ignoring descendants and a killed test runner
whose forked child retains the pipe writer.
Only explicitly selected channel descriptors survive child exec.

Run trusted test binaries: they retain the calling user's filesystem access.
This POC isolates network stacks and process lifetime, not hostile code or files.

## Network model

`Topology` contains nodes, veth links, routes and NAT gateways. A node may own an
IPv4 bridge. An empty link-end address attaches that interface to the node's
bridge. The supplied topology is:

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
a 32-bit application tag. Full queues and oversized messages fail immediately;
there is no silent loss or unbounded buffering. Topology/command descriptions must
also fit a single control message, including framing.

`Channel::receive(deadline, stop_token)` provides a blocking child API;
`Session::receive()` provides the parent's async API. Already queued messages are
read before closure is reported. A dead mutex owner invalidates that queue; a
live but stalled holder is bounded by a one-second lock deadline. Child exit is
detected by the supervisor and wakes receivers. Standalone channels require
explicit `close()` or a deadline to detect abandonment. This is a trusted,
same-build local ABI, not a persistent or network protocol.

The old `coordd` POC at commit `b03123e2` supplied useful process-spec and readiness
ideas. Its long-lived coordinator, socket transport, manifests and remote modes
are not needed here; its `netd-helper` was an unimplemented stub. Session lifetime
and typed shared-memory messages now stay with the test. No codegen attributes,
runner scheduling rules or fixture lifetimes change.
