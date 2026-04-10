#include "log.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fmt/format.h>
#include <iostream>
#include <llvm/Support/raw_ostream.h>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

struct ThrowingFormatValue {
    bool should_throw = false;
};

template <> struct fmt::formatter<ThrowingFormatValue> : fmt::formatter<std::string_view> {
    auto format(const ThrowingFormatValue &value, fmt::format_context &ctx) const {
        if (value.should_throw) {
            throw fmt::format_error("intentional formatter failure");
        }
        return fmt::formatter<std::string_view>::format("formatted", ctx);
    }
};

namespace {

struct Run {
    int  failures = 0;
    void expect(bool ok, std::string_view msg) {
        if (!ok) {
            ++failures;
            std::cerr << "FAIL: " << msg << "\n";
        }
    }
};

#if defined(_WIN32)
int  close_fd(int fd) { return _close(fd); }
int  dup_fd(int fd) { return _dup(fd); }
int  make_pipe(int fds[2]) { return _pipe(fds, 4096, _O_BINARY); }
int  read_fd(int fd, char *buffer, unsigned int size) { return _read(fd, buffer, size); }
bool redirect_fd(int from, int to) { return _dup2(from, to) == 0; }
#else
int  close_fd(int fd) { return close(fd); }
int  dup_fd(int fd) { return dup(fd); }
int  make_pipe(int fds[2]) { return pipe(fds); }
int  read_fd(int fd, char *buffer, std::size_t size) { return static_cast<int>(read(fd, buffer, size)); }
bool redirect_fd(int from, int to) { return dup2(from, to) >= 0; }
#endif

template <typename Fn> std::string capture_stderr(Fn &&fn) {
    static_cast<void>(std::fflush(stderr));
    llvm::errs().flush();

    int pipe_fds[2] = {-1, -1};
    if (make_pipe(pipe_fds) != 0) {
        std::cerr << "capture_stderr: pipe failed: " << std::strerror(errno) << "\n";
        return {};
    }

    const int saved_stderr = dup_fd(2);
    if (saved_stderr < 0) {
        std::cerr << "capture_stderr: dup failed: " << std::strerror(errno) << "\n";
        close_fd(pipe_fds[0]);
        close_fd(pipe_fds[1]);
        return {};
    }

    if (!redirect_fd(pipe_fds[1], 2)) {
        std::cerr << "capture_stderr: dup2 failed: " << std::strerror(errno) << "\n";
        close_fd(saved_stderr);
        close_fd(pipe_fds[0]);
        close_fd(pipe_fds[1]);
        return {};
    }
    close_fd(pipe_fds[1]);
    pipe_fds[1] = -1;

    std::string out;
    std::thread reader([&] {
        char buffer[512];
        while (true) {
            const int n = read_fd(pipe_fds[0], buffer, sizeof(buffer));
            if (n <= 0) {
                break;
            }
            out.append(buffer, static_cast<std::size_t>(n));
        }
        close_fd(pipe_fds[0]);
        pipe_fds[0] = -1;
    });

    const auto restore_stderr = [&] {
        static_cast<void>(std::fflush(stderr));
        llvm::errs().flush();
        redirect_fd(saved_stderr, 2);
        close_fd(saved_stderr);
    };

    try {
        std::forward<Fn>(fn)();
        restore_stderr();
    } catch (...) {
        restore_stderr();
        if (reader.joinable()) {
            reader.join();
        }
        throw;
    }
    if (reader.joinable()) {
        reader.join();
    }
    return out;
}

} // namespace

int main() {
    Run t;

    {
        const std::string first  = "alpha";
        const std::string second = "beta";
        std::string       third  = "gamma";
        const std::string output = capture_stderr([&] { gentest::codegen::log_err("{} {} {}\n", first, second, third); });
        t.expect(output == "alpha beta gamma\n", "formatted logging writes payload");
    }

    {
        std::string output = capture_stderr([&] { gentest::codegen::log_err_raw("raw-message\n"); });
        t.expect(output == "raw-message\n", "raw logging writes payload");

        output = capture_stderr([&] { gentest::codegen::log_err_raw(""); });
        t.expect(output.empty(), "raw logging accepts empty payload");

        const std::string large_payload(8192, 'x');
        output = capture_stderr([&] { gentest::codegen::log_err_raw(large_payload); });
        t.expect(output == large_payload, "raw logging handles payloads larger than a pipe buffer");
    }

    {
        const std::string output = capture_stderr([&] { gentest::codegen::log_err("{}\n", ThrowingFormatValue{}); });
        t.expect(output == "formatted\n", "custom formatter success path");

        bool threw = false;
        try {
            static_cast<void>(capture_stderr([&] { gentest::codegen::log_err("{}\n", ThrowingFormatValue{.should_throw = true}); }));
        } catch (const fmt::format_error &) { threw = true; }
        t.expect(threw, "custom formatter throw path");

        const std::string restored = capture_stderr([&] { gentest::codegen::log_err_raw("after-throw\n"); });
        t.expect(restored == "after-throw\n", "stderr is restored after formatter exceptions");
    }

    if (t.failures != 0) {
        std::cerr << "Total failures: " << t.failures << "\n";
        return 1;
    }
    return 0;
}
