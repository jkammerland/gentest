#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gentest::process {

struct EnvVar {
    std::string key;
    std::string value;
};

struct SubprocessOptions {
    std::vector<std::string> argv;
    std::vector<EnvVar> env;
    std::chrono::milliseconds timeout{0};
    std::optional<std::string> working_dir;
};

struct SubprocessResult {
    int         exit_code = -1;
    bool        started   = false;
    bool        timed_out = false;
    bool        signaled  = false;
    int         signal    = 0;
    std::string stdout_text;
    std::string stderr_text;
    std::string error;
};

auto run_subprocess(const SubprocessOptions &options) -> SubprocessResult;
auto current_executable_path() -> std::string;

} // namespace gentest::process
