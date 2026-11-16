# Experimental Linux network harness

The harness lets an async test run real UDP programs on one Linux machine. Each
node gets its own network stack, and you can add stateful NAT, firewall rules
and traffic shaping between nodes. The test talks to its programs over shared
memory, so blocking the network under test never blocks readiness messages,
signalling such as Interactive Connectivity Establishment (ICE) candidate
exchange, or shutdown.

**Status:** experimental and Linux only; the API can change. The harness is
built from source and is not installed, so `gentest::harness` and
`gentest::harness_client` exist only in a build of the gentest source tree with
`GENTEST_BUILD_HARNESS=ON`. [Network harness design](DESIGN.md) explains how it
works and documents each operation in detail.

## Requirements

- Linux 5.11 or newer, glibc 2.34 or newer, and unprivileged user namespaces.
- A C++20 compiler with exceptions enabled.
- On `PATH`: `unshare` (util-linux), `ip` and `tc` (iproute2), `nft`
  (nftables) and `sysctl` (procps).
- Kernel support for veth, bridges, conntrack and NAT, nftables and netem.
- To build the harness tests or your own tests: everything a gentest source
  build with codegen needs, including the LLVM and Clang development packages
  (see [Install gentest on Linux](../../docs/install/linux.md)).

## Build and run the harness tests

From the gentest source tree:

```bash
cmake --preset=debug-system -B build/harness -DGENTEST_BUILD_HARNESS=ON -DGENTEST_ENABLE_PACKAGE_TESTS=OFF -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build/harness --target experimental/harness/all
ctest --test-dir build/harness -R '^harness_' --output-on-failure --no-tests=error
```

`experimental/harness/all` builds everything in this directory, including the
helper programs the tests launch. Building only `gentest_harness_tests` leaves
most `harness_*` tests "Not Run".

- `GENTEST_BUILD_HARNESS` and `GENTEST_BUILD_RING_EXAMPLE` default to `OFF`.
  With both off, the harness adds nothing to the build.
- `GENTEST_BUILD_HARNESS=ON` fails at configure time on any OS other than
  Linux. `GENTEST_BUILD_RING_EXAMPLE=ON` requires `GENTEST_BUILD_HARNESS=ON`.
- The libraries alone don't need codegen. The harness tests and the ring
  example do: build codegen with `GENTEST_BUILD_CODEGEN=ON` (the default when
  tests are enabled) or select a host build with `GENTEST_CODEGEN_EXECUTABLE`.
- A selected network test fails when a prerequisite is missing; it never
  skips. Without `tc`, for example, the NAT test fails with
  `namespace command failed: tc: exit status 127: exec tc: No such file or directory`.
- To run the tests under AddressSanitizer, LeakSanitizer and
  UndefinedBehaviorSanitizer, use the same commands with
  `--preset=alusan-system` and a separate build directory.

## Write a test

A test describes the network as data, starts a `gentest::harness::Session`,
launches programs on the nodes and exchanges messages with them. Add the
gentest source tree to your project with the harness and codegen enabled:

```cmake
include(CTest)
set(GENTEST_BUILD_HARNESS ON)
set(GENTEST_BUILD_CODEGEN ON)
add_subdirectory(path/to/gentest gentest)

add_executable(udp_peer peer.cpp)
target_link_libraries(udp_peer PRIVATE gentest::harness_client)

add_executable(network_tests cases.hpp)
target_link_libraries(network_tests PRIVATE gentest::gentest_main gentest::harness)
target_compile_definitions(network_tests PRIVATE
    HARNESS_SUPERVISOR="$<TARGET_FILE:gentest_harness_supervisor>"
    PEER="$<TARGET_FILE:udp_peer>")
add_dependencies(network_tests udp_peer)
gentest_attach_codegen(network_tests)
gentest_discover_tests(network_tests)
```

The child program links only `gentest::harness_client`, which doesn't depend on
gentest's runtime. The compile definitions pass the paths of the supervisor and
the child program.

The test in `cases.hpp` connects two nodes, launches a program on each, waits
until both report ready, tells them to start and checks how they exit:

```cpp
#pragma once

#include "gentest/harness.h"
#include "gentest/test.h"

#include <chrono>

namespace network {

using namespace gentest;
using namespace gentest::async;
using namespace gentest::harness;
using namespace std::chrono_literals;

[[gentest::test("two_peers")]]
inline auto twoPeers() -> awaitable<> {
    SessionSpec spec;
    spec.supervisor     = HARNESS_SUPERVISOR;
    spec.topology.nodes = {{.name = "peer_a"}, {.name = "peer_b"}};
    spec.topology.links = {{
        .left  = {.node = "peer_a", .interface = "eth0", .address = "10.0.0.1/30"},
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

} // namespace network
```

The child program in `peer.cpp` opens its channel, reports that it is ready,
waits for the start message with a deadline, runs its network code and exits:

```cpp
#include "gentest/harness_client.h"

#include <chrono>

int main() {
    using namespace std::chrono_literals;
    auto channel = gentest::harness::Channel::fromEnvironment();
    channel.send({1, "ready"});
    const auto start = channel.receive(std::chrono::steady_clock::now() + 10s);
    // Exercise the application's real network code here.
    return start.tag == 7 ? 0 : 1;
}
```

[tests/peer.cpp](tests/peer.cpp) is a fuller child program with UDP send and
receive modes.

The rules most tests need:

- Programs are numbered by their position in `SessionSpec::programs`.
- `launch()` completes when the process exists. Readiness is an application
  message, and a failed `exec` shows up as exit code 127 from `wait()`.
- `wait()` returns the exit code or signal and the last 1500 bytes of stdout
  and stderr. `inspect()` returns the same snapshot without waiting; check its
  `running` field first.
- Each program allows one pending `receive()` at a time.
- A local variable or a local fixture can own the session. Keep it alive until
  its operations finish. End with `stop()`: it reports cleanup errors, which
  the destructor can't.

[Session operations](DESIGN.md#session-operations) covers `signal()`,
`execute()`, cancellation and the other details.

## Run a session with deadlines

`run_session()`, from `gentest/harness_run.h`, starts a session, runs your
callback and always cleans up, each with an absolute deadline:

```cpp
#pragma once

#include "gentest/harness_run.h"

#include <chrono>
#include <stdexcept>

inline auto checkWorkers(gentest::harness::SessionSpec spec) -> gentest::awaitable<> {
    using namespace gentest::harness;
    using namespace std::chrono_literals;
    const auto started = std::chrono::steady_clock::now();

    co_await run_session(
        std::move(spec), RunOptions{started + 50s, started + 55s},
        [](Session &session) -> gentest::awaitable<> {
            co_await session.launch(0);
            auto result = co_await session.wait(0);
            if (!result.succeeded()) {
                throw std::runtime_error("worker failed");
            }
        });
}
```

`run_session()` cancels the callback at `RunOptions::work_deadline`, always
cleans up by `cleanup_deadline`, and throws `SessionRunError` if either step
fails. Await it directly, not inside `wait_until()`, and check each
`ProcessResult` yourself.
[Deadlines with run_session](DESIGN.md#deadlines-with-run_session) has the full
rules.

## Per-program progress watchdogs

Set `Program::watchdog` to fail the session when a program stops reporting
progress. Here `WORKER` is the path of the child program, passed like `PEER`
above:

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

The child links `gentest::notifications_linux`, attaches once with
`gentest::notifierFromEnvironment()`, and reports progress on its own
operation:

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

The watchdog runs from launch. When a subject stops reporting progress, the
session fails and every program is torn down. Acknowledge a finished program
with `wait()` or `inspect()`, and don't set `GENTEST_NOTIFY_FDS` or
`GENTEST_NOTIFY_ID` yourself. A program that runs gentest cases can pass the
notifier to `RunConfiguration`. [Watchdogs](DESIGN.md#watchdogs) has the
details, and
[Notifications and progress watchdogs](../../docs/notifications.md) covers the
notification API.

## Limits

- Run trusted programs only. They keep the calling user's filesystem access:
  the harness isolates network stacks and process lifetime, not hostile code or
  files.
- A session has 1 to 16 nodes and at most 16 programs, and launches each
  program once. Node and interface names are 1 to 15 lowercase letters, digits
  or underscores.
- At most 16 `wait()` calls can be pending at once in a session.
- Channel messages carry a 32-bit tag and up to 4096 bytes; each direction
  queues up to 16 messages. The topology and each program description must fit
  in one 4096-byte control message.
- `wait()` and `inspect()` keep the last 1500 bytes of each output stream.
- `execute()` runs only `ip`, `nft` and `tc`, inside a node, with a 5-second
  deadline. Output larger than 4088 bytes fails the call.
- Each program takes up to eight inherited descriptors, six with a watchdog.
- `signal()` reaches the launched process only, not its process group or
  descendants.
- The NAT recipe in the tests models one IPv4 NAT and firewall setup, not every
  consumer router or symmetric NAT, and has no inbound port forwarding.
- Cancellation can't interrupt code that blocks, and a deadline can't force
  the kernel to reap a process. Give the test an outer timeout, such as a CTest
  `TIMEOUT`, longer than its cleanup deadline.

## Tests and examples in this directory

- [tests/](tests/) holds the harness tests. The network tests in
  [tests/cases.cpp](tests/cases.cpp) build two NAT gateways with
  [examples/support/two_nat.h](examples/support/two_nat.h).
- [examples/ring/](examples/ring/) is a Boost.Asio UDP ring with 3 to 16 peers.
  It needs Boost 1.85 or newer headers, but only when you enable it. With
  vcpkg, select the `harness-asio` manifest feature
  (`-DVCPKG_MANIFEST_FEATURES=harness-asio`); otherwise install the Boost
  development headers. Build and run it with:

```bash
cmake --preset=debug-system -B build/harness -DGENTEST_BUILD_HARNESS=ON -DGENTEST_BUILD_RING_EXAMPLE=ON -DGENTEST_ENABLE_PACKAGE_TESTS=OFF -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build/harness --target gentest_ring_example
ctest --test-dir build/harness -R '^harness_asio_ring$' --output-on-failure --no-tests=error
```

[Network harness design](DESIGN.md) describes what the tests and the ring
example check.
