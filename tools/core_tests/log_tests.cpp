#include "log.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fmt/format.h>
#include <iostream>
#include <llvm/Support/raw_ostream.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <cstdio>
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
int  temp_fd(std::FILE *file) { return _fileno(file); }
bool redirect_fd(int from, int to) { return _dup2(from, to) == 0; }
#else
int  close_fd(int fd) { return close(fd); }
int  dup_fd(int fd) { return dup(fd); }
int  temp_fd(std::FILE *file) { return fileno(file); }
bool redirect_fd(int from, int to) { return dup2(from, to) >= 0; }
#endif

template <typename Fn> std::string capture_stderr(Fn &&fn) {
    std::fflush(stderr);
    llvm::errs().flush();

    std::FILE *capture_file = std::tmpfile();
    if (!capture_file) {
        std::cerr << "capture_stderr: tmpfile failed: " << std::strerror(errno) << "\n";
        return {};
    }

    const int saved_stderr = dup_fd(2);
    if (saved_stderr < 0) {
        std::cerr << "capture_stderr: dup failed: " << std::strerror(errno) << "\n";
        std::fclose(capture_file);
        return {};
    }

    if (!redirect_fd(temp_fd(capture_file), 2)) {
        std::cerr << "capture_stderr: dup2 failed: " << std::strerror(errno) << "\n";
        close_fd(saved_stderr);
        std::fclose(capture_file);
        return {};
    }

    try {
        std::forward<Fn>(fn)();
        std::fflush(stderr);
        llvm::errs().flush();
        redirect_fd(saved_stderr, 2);
        close_fd(saved_stderr);
    } catch (...) {
        std::fflush(stderr);
        llvm::errs().flush();
        redirect_fd(saved_stderr, 2);
        close_fd(saved_stderr);
        std::fclose(capture_file);
        throw;
    }

    std::string out;
    char        buffer[512];
    if (std::fflush(capture_file) != 0 || std::fseek(capture_file, 0, SEEK_SET) != 0) {
        std::cerr << "capture_stderr: rewind failed: " << std::strerror(errno) << "\n";
        std::fclose(capture_file);
        return {};
    }
    while (const std::size_t n = std::fread(buffer, 1, sizeof(buffer), capture_file)) {
        out.append(buffer, n);
    }
    std::fclose(capture_file);
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
