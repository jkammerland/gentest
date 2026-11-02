#include "runner_replay.h"

#include "runner_tag_utils.h"

#include <fmt/format.h>
#include <string>
#include <string_view>
#include <vector>

namespace gentest::runner {
namespace {

auto quote_argument(std::string_view value) -> std::string {
    std::string quoted = "'";
    for (const char ch : value) {
        if (ch == '\'') {
#if defined(_WIN32)
            quoted += "''";
#else
            quoted += "'\\''";
#endif
        } else {
            quoted += ch;
        }
    }
    quoted += '\'';
    return quoted;
}

#if defined(_WIN32)
auto quote_windows_argument(std::string_view value) -> std::string {
    std::string quoted      = "\"";
    std::size_t backslashes = 0;
    for (const char ch : value) {
        if (ch == '\\') {
            ++backslashes;
            continue;
        }
        quoted.append(ch == '"' ? 2 * backslashes + 1 : backslashes, '\\');
        quoted += ch;
        backslashes = 0;
    }
    quoted.append(2 * backslashes, '\\');
    quoted += '"';
    return quoted;
}
#endif

auto make_command(const CliOptions &opt, const std::vector<std::string> &arguments) -> std::string {
    if (!opt.executable) {
        return {};
    }
#if defined(_WIN32)
    std::string native_arguments;
    for (const auto &argument : arguments) {
        if (!native_arguments.empty()) {
            native_arguments += ' ';
        }
        native_arguments += quote_windows_argument(argument);
    }
    // Start-Process bypasses legacy PowerShell native argument re-quoting.
    // Pass one CRT-encoded argument string, preserving embedded quotes/backslashes.
    return fmt::format("& {{ $gentestRerun = Start-Process -FilePath {} -ArgumentList {} -NoNewWindow -Wait -PassThru; "
                       "$global:LASTEXITCODE = $gentestRerun.ExitCode }}",
                       quote_argument(opt.executable), quote_argument(native_arguments));
#else
    auto command = quote_argument(opt.executable);
    for (const auto &argument : arguments) {
        command += ' ';
        command += quote_argument(argument);
    }
    return command;
#endif
}

} // namespace

std::uint64_t iteration_shuffle_seed(std::uint64_t base_seed, std::size_t iteration) noexcept {
    // Odd increments give distinct seeds throughout the bounded repeat count.
    // Iteration one uses the supplied seed directly, so any printed seed replays alone.
    return base_seed + 0x9e3779b97f4a7c15ULL * static_cast<std::uint64_t>(iteration);
}

std::string failure_rerun_command(const CliOptions &opt, const gentest::Case &test) {
    std::vector<std::string> arguments{fmt::format("--run={}", test.name),
                                       test.is_benchmark ? "--kind=bench" : (test.is_jitter ? "--kind=jitter" : "--kind=test")};
    if (has_tag_ci(test, "death")) {
        arguments.emplace_back("--include-death");
    }
    arguments.emplace_back("--no-color");
    return make_command(opt, arguments);
}

std::string iteration_replay_command(const CliOptions &opt, std::uint64_t seed) {
    // Preserve selectors (including tags/exclusions) and execution options.
    // Seed and repetition alone change when replaying one completed iteration.
    auto arguments = opt.invocation_arguments;
    arguments.push_back(fmt::format("--seed={}", seed));
    arguments.emplace_back("--repeat=1");
    return make_command(opt, arguments);
}

} // namespace gentest::runner
