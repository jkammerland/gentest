#include "../src/common.h"

#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>

namespace {
void startDescendants() {
    using namespace gentest::harness::detail;
    int descriptors[2];
    systemCheck(::pipe2(descriptors, O_CLOEXEC) == 0, "create startup descendant barrier");
    Fd ready(descriptors[0]), notify(descriptors[1]);
    systemCheck(::signal(SIGTERM, SIG_IGN) != SIG_ERR, "ignore startup descendant TERM");
    const auto child = ::fork();
    systemCheck(child >= 0, "fork startup descendant");
    if (child == 0) {
        ready.reset();
        const auto grandchild = ::fork();
        if (grandchild < 0 || ::write(notify.value, grandchild == 0 ? "G" : "C", 1) != 1) {
            ::_exit(125);
        }
        notify.reset();
        for (;;) {
            ::pause();
        }
    }
    notify.reset();

    // Both descendants acknowledge readiness before configuration succeeds.
    std::array<char, 2> bytes{};
    for (auto &byte : bytes) {
        systemCheck(::read(ready.value, &byte, 1) == 1, "wait for startup descendants");
    }
    if (!((bytes[0] == 'C' && bytes[1] == 'G') || (bytes[0] == 'G' && bytes[1] == 'C'))) {
        throw std::runtime_error("startup descendant barrier payload differs");
    }
}
} // namespace

int main(int argc, char **argv) {
    using namespace gentest::harness;
    using namespace gentest::harness::detail;
    using namespace std::chrono_literals;

    try {
        const char            *setting = std::getenv("GENTEST_SESSION_RUN_MODE");
        const std::string_view mode    = setting ? setting : "success";
        auto                   control = Channel::attach({3, 4, 5});
        std::uint64_t          startupDeadline{};
        if (mode == "deadline_ceiling") {
            if (argc != 5) {
                throw std::runtime_error("deadline ceiling helper requires a finite startup deadline");
            }
            startupDeadline = std::stoull(argv[4]);
        }

        for (;;) {
            const auto message = control.receive(std::chrono::steady_clock::now() + 20s);
            Reader     request{message.payload};
            const auto command = static_cast<Command>(request.number());
            Writer     response;

            if (command == Command::configure) {
                if (mode == "startup_timeout") {
                    continue;
                }
                if (mode == "namespace_pidfd_failure") {
                    startDescendants();
                }
                const bool reject = mode == "partial_startup" || mode == "startup_dual";
                response.number(!reject);
                response.text(reject ? "injected configuration failure" : "");
            } else if (command == Command::cancel) {
                continue;
            } else if (command == Command::shutdown) {
                const auto deadline = request.wideNumber();
                request.end();
                if (mode == "namespace_pidfd_failure") {
                    continue;
                }
                const bool reject   = mode == "dual_failure" || mode == "startup_dual" || mode == "cleanup_failure" ||
                                      mode == "timeout_cleanup_failure" || mode == "nested_failure";
                const bool exceeded = mode == "deadline_ceiling" && (deadline == 0 || deadline > startupDeadline);
                response.number(!reject && !exceeded);
                response.text(exceeded ? "shutdown exceeded startup cleanup ceiling" : reject ? "injected cleanup failure" : "");
            } else {
                throw std::runtime_error("unexpected session runner test command");
            }

            control.send({.tag = message.tag, .payload = std::move(response.bytes)});
            if (command == Command::shutdown) {
                return 0;
            }
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
