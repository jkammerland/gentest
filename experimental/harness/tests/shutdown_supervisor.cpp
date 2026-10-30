#include "../src/common.h"

#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <poll.h>

int main() {
    using namespace gentest::harness;
    using namespace gentest::harness::detail;
    using namespace std::chrono_literals;
    try {
        const char *directory = std::getenv("GENTEST_SHUTDOWN_DIRECTORY");
        const char *mode      = std::getenv("GENTEST_SHUTDOWN_MODE");
        if (!directory || !mode) {
            throw std::runtime_error("shutdown helper requires barrier paths and mode");
        }
        Fd ready(::open((std::string(directory) + "/ready").c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC));
        Fd release(::open((std::string(directory) + "/release").c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC));
        systemCheck(ready.value >= 0 && release.value >= 0, "open shutdown barriers");
        auto       control = Channel::attach({3, 4, 5});
        const auto reply   = [&](std::uint32_t tag) {
            Writer response;
            response.number(1);
            response.text({});
            control.send({.tag = tag, .payload = std::move(response.bytes)});
        };

        const auto configure = control.receive(std::chrono::steady_clock::now() + 5s);
        Reader     configuration{configure.payload};
        if (configuration.number() != static_cast<unsigned>(Command::configure)) {
            throw std::runtime_error("expected configuration request");
        }
        reply(configure.tag);
        const auto shutdown = control.receive(std::chrono::steady_clock::now() + 5s);
        Reader     request{shutdown.payload};
        if (request.number() != static_cast<unsigned>(Command::shutdown)) {
            throw std::runtime_error("expected shutdown request");
        }
        (void)request.wideNumber();
        request.end();

        // Hold the final reply until the parent's exit probe releases us.
        systemCheck(::write(ready.value, "1", 1) == 1, "publish shutdown readiness");
        std::array<pollfd, 2> pending{
            {{.fd = release.value, .events = POLLIN, .revents = 0}, {.fd = STDIN_FILENO, .events = POLLHUP, .revents = 0}}};
        char byte{};
        if (::poll(pending.data(), pending.size(), 5000) != 1 || !(static_cast<unsigned>(pending[0].revents) & POLLIN) ||
            ::read(release.value, &byte, 1) != 1 || byte != '1') {
            throw std::runtime_error("shutdown barrier was not released");
        }
        if (std::string_view(mode) == "ack") {
            reply(shutdown.tag);
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
