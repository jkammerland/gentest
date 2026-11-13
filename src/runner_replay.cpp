#include "runner_replay.h"

#include "runner_tag_utils.h"

#include <fmt/format.h>
#include <string>
#include <string_view>
#include <vector>

namespace gentest::runner {
namespace {

// PowerShell accepts four more single-quote delimiters besides ASCII U+0027:
// U+2018, U+2019, U+201A and U+201B, which are E2 80 98..9B in UTF-8.
auto powershell_quote_length(std::string_view value, std::size_t pos) -> std::size_t {
    if (value[pos] == '\'') {
        return 1;
    }
    if (value.size() - pos >= 3 && value[pos] == '\xE2' && value[pos + 1] == '\x80') {
        const auto last = static_cast<unsigned char>(value[pos + 2]);
        if (last >= 0x98U && last <= 0x9BU) {
            return 3;
        }
    }
    return 0;
}

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

} // namespace

std::string quote_replay_argument(std::string_view value, ReplayShell shell) {
    std::string quoted = "'";
    for (std::size_t pos = 0; pos < value.size();) {
        if (shell == ReplayShell::PowerShell) {
            // Any quote delimiter is escaped by doubling it; the doubled pair yields that character.
            if (const std::size_t length = powershell_quote_length(value, pos); length != 0) {
                quoted.append(value.substr(pos, length));
                quoted.append(value.substr(pos, length));
                pos += length;
                continue;
            }
        } else if (value[pos] == '\'') {
            quoted += "'\\''";
            ++pos;
            continue;
        }
        quoted += value[pos];
        ++pos;
    }
    quoted += '\'';
    return quoted;
}

std::string replay_command(std::string_view executable, const std::vector<std::string> &arguments, ReplayShell shell) {
    if (shell == ReplayShell::PowerShell) {
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
                           quote_replay_argument(executable, shell), quote_replay_argument(native_arguments, shell));
    }
    auto command = quote_replay_argument(executable, shell);
    for (const auto &argument : arguments) {
        command += ' ';
        command += quote_replay_argument(argument, shell);
    }
    return command;
}

namespace {

auto make_command(const CliOptions &opt, const std::vector<std::string> &arguments) -> std::string {
    if (!opt.executable) {
        return {};
    }
    return replay_command(opt.executable, arguments, kNativeReplayShell);
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
