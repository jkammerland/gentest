#include "owner.hpp"

#include "../src/common.h"

#include <charconv>
#include <cstdlib>
#include <poll.h>
#include <unistd.h>

auto ownerVictim() -> gentest::awaitable<> {
    using namespace gentest::harness;
    SessionSpec spec;
    spec.supervisor = HARNESS_SUPERVISOR;
    spec.topology.nodes.push_back({.name = "host", .ipv4Forwarding = false, .bridgeAddress = {}});
    spec.programs.push_back({.node = "host", .argv = {HARNESS_PEER, "fork"}, .environment = {}, .workingDirectory = {}});
    auto session = co_await Session::start(std::move(spec));
    co_await session.launch(0);
    auto                   message    = co_await session.receive(0);
    const char            *descriptor = std::getenv("GENTEST_OWNER_HANDOFF");
    const std::string_view number     = descriptor ? descriptor : "";
    int                    fd{};
    const auto             parsed = std::from_chars(number.data(), number.data() + number.size(), fd);
    if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size() || fd < 3) {
        throw std::runtime_error("invalid owner handoff descriptor");
    }

    if (std::getenv("GENTEST_OWNER_FORK_HOLDER")) {
        using namespace gentest::harness::detail;
        int ready[2];
        systemCheck(::pipe(ready) == 0, "create holder readiness pipe");
        Fd         input(ready[0]), output(ready[1]);
        const auto holder = ::fork();
        systemCheck(holder >= 0, "fork inherited writer holder");
        if (holder == 0) {
            // Fork after session startup deliberately retains its liveness
            // writer. Only async-signal-safe calls run in this forked child.
            ::close(input.value);
            ::close(fd);
            if (::write(output.value, "1", 1) != 1) {
                ::_exit(125);
            }
            ::close(output.value);
            for (;;) {
                ::pause();
            }
        }
        output.reset();
        pollfd pending{.fd = input.value, .events = POLLIN, .revents = 0};
        char   byte{};
        if (::poll(&pending, 1, 5000) != 1 || ::read(input.value, &byte, 1) != 1 || byte != '1') {
            throw std::runtime_error("inherited writer holder did not start");
        }
        message.payload += ' ' + std::to_string(holder);
    }

    message.payload += '\n';
    if (::write(fd, message.payload.data(), message.payload.size()) != static_cast<ssize_t>(message.payload.size())) {
        throw std::runtime_error("owner handoff failed");
    }
    if (std::getenv("GENTEST_OWNER_EXECUTE")) {
        // The regression supplies a private tc fixture which announces entry
        // and blocks, so runner death occurs inside the supervisor command wait.
        (void)co_await session.execute("host", {"tc", "--stall-command"});
        throw std::runtime_error("stalled command unexpectedly completed");
    }

    // The regression kills this runner after observing its private PID namespace.
    for (;;) {
        ::pause();
    }
}
