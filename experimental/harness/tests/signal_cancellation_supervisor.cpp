#include "../src/process_signal.h"

#include <iostream>
#include <sys/wait.h>

namespace {
using namespace gentest::harness;
using namespace gentest::harness::detail;
using namespace std::chrono_literals;

struct Target {
    pid_t pid{-1};
    Fd    pidfd;
    bool  done{};
    int   status{};

    ~Target() {
        if (pid > 0 && !done) {
            if (pidfd.value >= 0) {
                ::syscall(SYS_pidfd_send_signal, pidfd.value, SIGKILL, nullptr, 0);
            } else {
                ::kill(pid, SIGKILL);
            }
            while (::waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
        }
    }
};

void supervise() {
    auto                         control = Channel::attach({3, 4, 5});
    auto                         program = Channel::attach({6, 7, 8});
    Target                       target;
    std::optional<std::uint32_t> pending;
    const auto                   reply = [&](std::uint32_t tag, std::string value = {}) {
        Writer body;
        body.number(1);
        body.text(value);
        control.send({.tag = tag, .payload = std::move(body.bytes)});
    };

    for (;;) {
        const auto message = control.receive(std::chrono::steady_clock::now() + 5s);
        Reader     reader{message.payload};
        const auto command = static_cast<Command>(reader.number());
        if (command == Command::configure) {
            reply(message.tag);
        } else if (command == Command::launch) {
            if (target.pid > 0 || reader.number() != 0) {
                throw std::runtime_error("unexpected signal test launch");
            }
            target.pid = ::fork();
            systemCheck(target.pid >= 0, "fork signal test target");
            if (target.pid == 0) {
                for (;;) {
                    ::pause();
                }
            }
            target.pidfd.reset(static_cast<int>(::syscall(SYS_pidfd_open, target.pid, 0)));
            systemCheck(target.pidfd.value >= 0, "open signal test target pidfd");
            reply(message.tag);
        } else if (command == Command::signal) {
            const auto index  = reader.number();
            const auto signal = reader.number();
            reader.end();
            if (index != 0 || signal != SIGTERM || target.pid <= 0) {
                throw std::runtime_error("unexpected signal test submission");
            }
            Writer result;
            result.number(submitSignal(target.pidfd.value, target.done, static_cast<int>(signal)));
            if (target.done) {
                reply(message.tag, std::move(result.bytes));
                continue;
            }

            // Hold the acknowledgement until cancellation. The application
            // message proves the real signal took effect before that request.
            systemCheck(::waitpid(target.pid, &target.status, 0) == target.pid, "reap signal test target");
            target.done = true;
            if (!WIFSIGNALED(target.status) || WTERMSIG(target.status) != SIGTERM) {
                throw std::runtime_error("signal test target did not receive TERM");
            }
            pending = message.tag;
            program.send({.tag = 1, .payload = "terminated"});
        } else if (command == Command::cancel) {
            const auto tag = reader.number();
            reader.end();
            if (!pending || tag != *pending) {
                throw std::runtime_error("unexpected signal test cancellation");
            }
            Writer result;
            result.number(1);
            reply(tag, std::move(result.bytes));
            pending.reset();
        } else if (command == Command::wait) {
            if (reader.number() != 0 || pending || !target.done) {
                throw std::runtime_error("signal operation was not retired before wait");
            }
            reader.end();
            Writer result;
            result.number(0);
            result.number(0);
            result.number(WTERMSIG(target.status));
            result.text({});
            result.text({});
            result.number(0);
            reply(message.tag, std::move(result.bytes));
        } else if (command == Command::shutdown) {
            (void)reader.wideNumber();
            reader.end();
            reply(message.tag);
            return;
        } else {
            throw std::runtime_error("unexpected signal test command");
        }
    }
}
} // namespace

int main() {
    try {
        systemCheck(::signal(SIGTERM, SIG_DFL) != SIG_ERR, "reset signal test target disposition");
        supervise();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
