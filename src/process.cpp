#include "gentest/process.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <thread>
#include <utility>

#ifdef _WIN32
#  include <cwchar>
#  include <cwctype>
#  include <windows.h>
#else
#  include <cerrno>
#  include <csignal>
#  include <cstring>
#  include <fcntl.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  ifdef __APPLE__
#    include <mach-o/dyld.h>
#  endif
#endif

namespace gentest::process {
namespace {
#ifdef _WIN32
std::wstring to_wstring(std::string_view input, std::string &error) {
    if (input.empty()) {
        return std::wstring();
    }
    int size = MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
    if (size <= 0) {
        error = "utf8_to_utf16 failed";
        return std::wstring();
    }
    std::wstring output(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), output.data(), size);
    return output;
}

std::string to_utf8(std::wstring_view input, std::string &error) {
    if (input.empty()) {
        return std::string();
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        error = "utf16_to_utf8 failed";
        return std::string();
    }
    std::string output(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), output.data(), size, nullptr, nullptr);
    return output;
}

std::wstring quote_windows_arg(std::wstring_view arg) {
    if (arg.empty()) {
        return L"\"\"";
    }
    const bool need_quotes = arg.find_first_of(L" \t\"") != std::wstring_view::npos;
    if (!need_quotes) {
        return std::wstring(arg);
    }
    std::wstring result;
    result.push_back(L'\"');
    size_t backslashes = 0;
    for (wchar_t ch : arg) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'\"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'\"');
            backslashes = 0;
            continue;
        }
        if (backslashes > 0) {
            result.append(backslashes, L'\\');
            backslashes = 0;
        }
        result.push_back(ch);
    }
    if (backslashes > 0) {
        result.append(backslashes * 2, L'\\');
    }
    result.push_back(L'\"');
    return result;
}

std::wstring build_command_line(const std::vector<std::string> &argv, std::string &error) {
    std::wstring command_line;
    for (size_t idx = 0; idx < argv.size(); ++idx) {
        std::wstring arg = to_wstring(argv[idx], error);
        if (!error.empty()) {
            return std::wstring();
        }
        if (idx > 0) {
            command_line.push_back(L' ');
        }
        command_line += quote_windows_arg(arg);
    }
    return command_line;
}

bool build_environment_block(const std::vector<EnvVar> &env, std::vector<wchar_t> &out, std::string &error) {
    std::wstring block;
    const wchar_t *raw = GetEnvironmentStringsW();
    if (!raw) {
        error = "GetEnvironmentStringsW failed";
        return false;
    }
    std::map<std::wstring, std::wstring> entries;
    for (const wchar_t *cursor = raw; *cursor != L'\0'; cursor += wcslen(cursor) + 1) {
        std::wstring entry(cursor);
        const size_t pos = entry.find(L'=');
        if (pos == std::wstring::npos) {
            continue;
        }
        std::wstring key = entry.substr(0, pos);
        std::wstring key_upper = key;
        std::transform(key_upper.begin(), key_upper.end(), key_upper.begin(), ::towupper);
        entries[key_upper] = entry;
    }
    FreeEnvironmentStringsW(raw);

    for (const auto &override_var : env) {
        std::wstring key = to_wstring(override_var.key, error);
        if (!error.empty()) {
            return false;
        }
        std::wstring value = to_wstring(override_var.value, error);
        if (!error.empty()) {
            return false;
        }
        std::wstring key_upper = key;
        std::transform(key_upper.begin(), key_upper.end(), key_upper.begin(), ::towupper);
        entries[key_upper] = key + L"=" + value;
    }

    for (const auto &entry : entries) {
        block += entry.second;
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    out.assign(block.begin(), block.end());
    return true;
}

void read_handle(HANDLE handle, std::string &out) {
    constexpr DWORD kBufferSize = 4096;
    char buffer[kBufferSize];
    DWORD bytes_read = 0;
    while (ReadFile(handle, buffer, kBufferSize, &bytes_read, nullptr) && bytes_read > 0) {
        out.append(buffer, buffer + bytes_read);
    }
    CloseHandle(handle);
}
#else
void read_fd(int fd, std::string &out) {
    constexpr size_t kBufferSize = 4096;
    char buffer[kBufferSize];
    while (true) {
        const ssize_t bytes_read = read(fd, buffer, kBufferSize);
        if (bytes_read > 0) {
            out.append(buffer, buffer + bytes_read);
            continue;
        }
        break;
    }
    close(fd);
}
#endif
} // namespace

SubprocessResult run_subprocess(const SubprocessOptions &options) {
    SubprocessResult result;
    if (options.argv.empty()) {
        result.error = "argv is empty";
        return result;
    }

#ifdef _WIN32
    std::string error;
    std::wstring command_line = build_command_line(options.argv, error);
    if (!error.empty()) {
        result.error = error;
        return result;
    }

    SECURITY_ATTRIBUTES security_attrs{};
    security_attrs.nLength = sizeof(security_attrs);
    security_attrs.bInheritHandle = TRUE;

    HANDLE stdout_read = nullptr;
    HANDLE stdout_write = nullptr;
    HANDLE stderr_read = nullptr;
    HANDLE stderr_write = nullptr;

    if (!CreatePipe(&stdout_read, &stdout_write, &security_attrs, 0)) {
        result.error = "CreatePipe stdout failed";
        return result;
    }
    if (!SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(stdout_read);
        CloseHandle(stdout_write);
        result.error = "SetHandleInformation stdout failed";
        return result;
    }

    if (!CreatePipe(&stderr_read, &stderr_write, &security_attrs, 0)) {
        CloseHandle(stdout_read);
        CloseHandle(stdout_write);
        result.error = "CreatePipe stderr failed";
        return result;
    }
    if (!SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(stdout_read);
        CloseHandle(stdout_write);
        CloseHandle(stderr_read);
        CloseHandle(stderr_write);
        result.error = "SetHandleInformation stderr failed";
        return result;
    }

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdOutput = stdout_write;
    startup_info.hStdError = stderr_write;
    startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION process_info{};

    std::vector<wchar_t> env_block;
    LPVOID env_ptr = nullptr;
    if (!options.env.empty()) {
        if (!build_environment_block(options.env, env_block, error)) {
            CloseHandle(stdout_read);
            CloseHandle(stdout_write);
            CloseHandle(stderr_read);
            CloseHandle(stderr_write);
            result.error = error;
            return result;
        }
        env_ptr = env_block.data();
    }

    std::wstring working_dir;
    const wchar_t *working_dir_ptr = nullptr;
    if (options.working_dir.has_value()) {
        working_dir = to_wstring(options.working_dir.value(), error);
        if (!error.empty()) {
            CloseHandle(stdout_read);
            CloseHandle(stdout_write);
            CloseHandle(stderr_read);
            CloseHandle(stderr_write);
            result.error = error;
            return result;
        }
        working_dir_ptr = working_dir.c_str();
    }

    std::wstring command_line_copy = command_line;
    if (!CreateProcessW(nullptr,
            command_line_copy.data(),
            nullptr,
            nullptr,
            TRUE,
            0,
            env_ptr,
            working_dir_ptr,
            &startup_info,
            &process_info)) {
        CloseHandle(stdout_read);
        CloseHandle(stdout_write);
        CloseHandle(stderr_read);
        CloseHandle(stderr_write);
        result.error = "CreateProcessW failed";
        return result;
    }

    result.started = true;
    CloseHandle(stdout_write);
    CloseHandle(stderr_write);

    std::thread stdout_thread(read_handle, stdout_read, std::ref(result.stdout_text));
    std::thread stderr_thread(read_handle, stderr_read, std::ref(result.stderr_text));

    DWORD wait_ms = options.timeout.count() > 0
        ? static_cast<DWORD>(options.timeout.count())
        : INFINITE;
    DWORD wait_result = WaitForSingleObject(process_info.hProcess, wait_ms);
    if (wait_result == WAIT_TIMEOUT) {
        result.timed_out = true;
        TerminateProcess(process_info.hProcess, 1);
        WaitForSingleObject(process_info.hProcess, INFINITE);
    }

    DWORD exit_code = 0;
    if (GetExitCodeProcess(process_info.hProcess, &exit_code)) {
        result.exit_code = static_cast<int>(exit_code);
    }

    CloseHandle(process_info.hProcess);
    CloseHandle(process_info.hThread);

    stdout_thread.join();
    stderr_thread.join();
#else
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    if (pipe(stdout_pipe) != 0) {
        result.error = "pipe stdout failed";
        return result;
    }
    if (pipe(stderr_pipe) != 0) {
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        result.error = "pipe stderr failed";
        return result;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        result.error = "fork failed";
        return result;
    }

    if (pid == 0) {
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);

        if (options.working_dir.has_value()) {
            if (chdir(options.working_dir.value().c_str()) != 0) {
                _exit(127);
            }
        }

        for (const auto &env : options.env) {
            setenv(env.key.c_str(), env.value.c_str(), 1);
        }

        std::vector<char*> argv_c;
        argv_c.reserve(options.argv.size() + 1);
        for (const auto &arg : options.argv) {
            argv_c.push_back(const_cast<char*>(arg.c_str()));
        }
        argv_c.push_back(nullptr);
        execvp(argv_c[0], argv_c.data());
        _exit(127);
    }

    result.started = true;
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    std::thread stdout_thread(read_fd, stdout_pipe[0], std::ref(result.stdout_text));
    std::thread stderr_thread(read_fd, stderr_pipe[0], std::ref(result.stderr_text));

    int status = 0;
    bool finished = false;
    const bool has_timeout = options.timeout.count() > 0;
    const auto deadline = std::chrono::steady_clock::now() + options.timeout;

    while (!finished) {
        const pid_t wait_result = waitpid(pid, &status, has_timeout ? WNOHANG : 0);
        if (wait_result == pid) {
            finished = true;
            break;
        }
        if (wait_result == 0) {
            if (!has_timeout) {
                continue;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                result.timed_out = true;
                kill(pid, SIGTERM);
                waitpid(pid, &status, 0);
                finished = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (wait_result < 0 && errno == EINTR) {
            continue;
        }
        result.error = "waitpid failed";
        break;
    }

    if (result.timed_out) {
        if (WIFSIGNALED(status) || WIFEXITED(status)) {
            // status already set
        } else {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
        }
    }

    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.signaled = true;
        result.signal = WTERMSIG(status);
        result.exit_code = 128 + result.signal;
    }

    stdout_thread.join();
    stderr_thread.join();
#endif

    return result;
}

std::string current_executable_path() {
#ifdef _WIN32
    std::wstring path;
    DWORD size = MAX_PATH;
    std::string error;
    while (true) {
        path.resize(size);
        DWORD written = GetModuleFileNameW(nullptr, path.data(), size);
        if (written == 0) {
            return std::string();
        }
        if (written < size - 1) {
            path.resize(written);
            break;
        }
        size *= 2;
    }
    return to_utf8(path, error);
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return std::string();
    }
    if (!buffer.empty() && buffer.back() == '\0') {
        buffer.pop_back();
    }
    return buffer;
#else
    std::string buffer(1024, '\0');
    while (true) {
        ssize_t count = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
        if (count < 0) {
            return std::string();
        }
        if (static_cast<size_t>(count) < buffer.size() - 1) {
            buffer.resize(static_cast<size_t>(count));
            return buffer;
        }
        buffer.resize(buffer.size() * 2);
    }
#endif
}

} // namespace gentest::process
