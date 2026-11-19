#include "death_watchdog.h"
#include "process.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {
namespace fs       = std::filesystem;
using Clock        = std::chrono::steady_clock;
using Milliseconds = std::chrono::milliseconds;

constexpr std::string_view usage = "gentest_supervise --case NAME [--expect TEXT] [--timeout-ms N] [--inactivity-ms N] -- PROGRAM [ARG...]";

struct Options {
    std::string                 name;
    std::string                 expected;
    std::optional<Milliseconds> timeout;
    std::optional<Milliseconds> inactivity;
    bool                        skip_missing{};
    bool                        skip_as_pass{};
    std::vector<std::string>    command;
};

auto duration(std::string_view value) -> Milliseconds {
    int count{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), count);
    if (error != std::errc{} || end != value.data() + value.size() || count <= 0)
        throw std::runtime_error("expected a positive timeout in milliseconds");
    return Milliseconds(count);
}

auto parse(std::span<const std::string> args) -> Options {
    Options options;
    while (!args.empty()) {
        const auto &key = args.front();
        args            = args.subspan(1);
        if (key == "--") {
            options.command.assign(args.begin(), args.end());
            break;
        }
        if (key == "--skip-missing") {
            options.skip_missing = true;
            continue;
        }
        if (key == "--skip-as-pass") {
            options.skip_as_pass = true;
            continue;
        }
        if (args.empty())
            throw std::runtime_error(std::string(usage));
        const auto &value = args.front();
        args              = args.subspan(1);
        if (key == "--case")
            options.name = value;
        else if (key == "--expect")
            options.expected = value;
        else if (key == "--timeout-ms")
            options.timeout = duration(value);
        else if (key == "--inactivity-ms")
            options.inactivity = duration(value);
        else
            throw std::runtime_error("unknown option: " + key);
    }
    if (options.name.empty() || options.command.empty())
        throw std::runtime_error(std::string(usage));
    return options;
}

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
#if defined(_WIN32)
        auto base = fs::temp_directory_path();
        auto size = GetEnvironmentVariableW(L"TEST_TMPDIR", nullptr, 0);
        if (size) {
            std::wstring value(size, L'\0');
            const auto   length = GetEnvironmentVariableW(L"TEST_TMPDIR", value.data(), size);
            if (!length || length >= size)
                throw std::runtime_error("cannot read TEST_TMPDIR");
            value.resize(length);
            base = fs::absolute(value);
        }
        std::random_device random;
        for (int i = 0; i < 32; ++i) {
            path = base / ("gentest-death-" + std::to_string(random()) + "-" + std::to_string(random()));
            if (fs::create_directory(path))
                return;
        }
        throw std::runtime_error("cannot create temporary death-test directory");
#else
        const char *test_dir = std::getenv("TEST_TMPDIR");
        const auto  base     = test_dir && *test_dir ? fs::absolute(test_dir) : fs::temp_directory_path();
        auto        name     = (base / "gentest-death-XXXXXX").string();
        if (!::mkdtemp(name.data()))
            throw std::system_error(errno, std::generic_category(), "create death-test directory");
        path = std::move(name);
#endif
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
    TemporaryDirectory(const TemporaryDirectory &)            = delete;
    TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;
    fs::path            path;
};

auto readFile(const fs::path &path) -> std::string {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("cannot read death-test result");
    return {std::istreambuf_iterator<char>(input), {}};
}

// Search all output, including matches spanning a read boundary. Keep only the
// overlapping bytes in memory even when the child writes a very large log.
auto contains(const fs::path &path, std::string_view needle, bool line = false) -> bool {
    if (needle.empty())
        return true;
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("cannot open death-test output");
    std::array<char, 16384> buffer;
    std::string             window = line ? "\n" : "";
    for (;;) {
        input.read(buffer.data(), buffer.size());
        window.append(buffer.data(), static_cast<std::size_t>(input.gcount()));
        if (input.bad())
            throw std::runtime_error("cannot read death-test output");
        if (input.eof() && line)
            window += '\n';
        if (window.find(needle) != window.npos)
            return true;
        if (input.eof())
            return false;
        if (window.size() >= needle.size())
            window.erase(0, window.size() - needle.size() + 1);
    }
}

auto hasLine(const fs::path &path, const std::string &message) -> bool {
    return contains(path, "\n" + message + "\n", true) || contains(path, "\n" + message + "\r\n", true);
}

void printOutput(const fs::path &path) {
    std::ifstream log(path, std::ios::binary);
    if (log && log.peek() != std::ifstream::traits_type::eof())
        std::cerr << log.rdbuf();
}

auto execute(Options options) -> int {
    TemporaryDirectory tmp;
    const auto         marker      = tmp.path / "body";
    const auto         output      = tmp.path / "output";
    const auto         marker_utf8 = marker.u8string();
    options.command.emplace_back("--include-death");
    options.command.push_back("--run=" + options.name);
    options.command.emplace_back("--gentest-death-marker=" + std::string(marker_utf8.begin(), marker_utf8.end()));
    options.command.emplace_back("--skip-return-code=77");

#if defined(_WIN32)
    // A narrow child main can lose characters when converting the original
    // UTF-16 command line to its ANSI code page.
    options.command.emplace_back("--gentest-death-wide");
#endif
    std::optional<gentest::detail::DeathWatchdog> watchdog;
    if (options.inactivity) {
        watchdog.emplace(tmp.path / "body.watchdog", *options.inactivity);
        options.command.emplace_back("--gentest-death-watchdog");
    }

    const auto start    = Clock::now();
    const auto timedOut = [&] { return options.timeout && Clock::now() - start >= *options.timeout; };
    const auto expired  = [&] {
        if (watchdog)
            watchdog->check();
        return timedOut();
    };
    const auto shortest     = std::min(options.timeout.value_or(Milliseconds(40)), options.inactivity.value_or(Milliseconds(40)));
    const auto pollInterval = std::clamp(shortest / 4, Milliseconds(1), Milliseconds(10));
    gentest::supervisor::ChildResult child;
    try {
        child = gentest::supervisor::runChild(options.command, output, expired, pollInterval);
    } catch (const std::system_error &e) { throw std::runtime_error(std::string("Death test process could not execute: ") + e.what()); }
    if (child.timed_out || timedOut())
        throw std::runtime_error("Death test timed out: " + options.name);
    // Drain progress published immediately before death. The selected body can end with an active subject.
    if (watchdog)
        watchdog->finish();

    const auto phase     = fs::exists(marker) ? readFile(marker) : std::string{};
    const bool entered   = phase == options.name;
    const bool completed = phase.starts_with(options.name + "\ncompleted");
    const auto fail      = [&](const std::string &reason) {
        std::cerr << reason << "\n";
        printOutput(output);
        return 1;
    };

    if (contains(output, "\n[ FAIL ]", true))
        return fail("Death test exited non-zero but reported a normal test failure.");
    const bool missing = !entered && !completed &&
                         (hasLine(output, "Case not found: " + options.name) || hasLine(output, "Test not found: " + options.name));
    if (missing && !options.skip_missing)
        return fail("Death test case not found: " + options.name);
    if (missing || (child.status == 77 && phase == options.name + "\nskipped")) {
        std::cout << "[ SKIP ] " << options.name << (missing ? " is not present in this build\n" : "\n");
        return options.skip_as_pass ? 0 : 77;
    }
    if (!child.abnormal)
        return fail("Expected process to abort/exit non-zero, but exit code was 0.");
    if (completed)
        return fail("Death test body completed without the expected termination.");
    if (!entered)
        return fail("Death test did not reach the selected test body.");
    if (watchdog && !watchdog->attached())
        return fail("Death test did not attach its watchdog.");
    if (!contains(output, options.expected))
        return fail("Expected substring not found in output: " + options.expected);
    std::cout << "Death test passed: " << options.name << '\n';
    return 0;
}

#if defined(_WIN32)
auto arguments(int, char **) -> std::vector<std::string> { return gentest::detail::wideProcessArguments(); }
#else
auto arguments(int argc, char **argv) -> std::vector<std::string> { return {argv + 1, argv + argc}; }
#endif
} // namespace

int main(int argc, char **argv) {
    try {
        auto args = arguments(argc, argv);
        if (args.size() == 1 && args.front() == "--help") {
            std::cout << usage << '\n';
            return 0;
        }
        return execute(parse(args));
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
