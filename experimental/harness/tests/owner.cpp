#include "owner.hpp"

#include <charconv>
#include <cstdlib>
#include <unistd.h>

auto ownerVictim() -> gentest::awaitable<> {
    using namespace gentest::harness;
    SessionSpec spec;
    spec.supervisor = HARNESS_SUPERVISOR;
    spec.topology.nodes.push_back({.name = "host", .forwarding = false, .bridgeAddress = {}});
    spec.programs.push_back({.node = "host", .argv = {HARNESS_PEER, "fork"}, .environment = {}, .workingDirectory = {}});
    auto session = co_await Session::start(std::move(spec));
    co_await session.launch(0);
    auto message = co_await session.receive(0);
    message.payload += '\n';
    const char            *descriptor = std::getenv("GENTEST_OWNER_HANDOFF");
    const std::string_view number     = descriptor ? descriptor : "";
    int                    fd{};
    const auto             parsed = std::from_chars(number.data(), number.data() + number.size(), fd);
    if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size() || fd < 3 ||
        ::write(fd, message.payload.data(), message.payload.size()) != static_cast<ssize_t>(message.payload.size())) {
        throw std::runtime_error("owner handoff failed");
    }
    // The regression kills this runner after observing its private PID namespace.
    for (;;) {
        ::pause();
    }
}
