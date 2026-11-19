#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "process.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <windows.h>

namespace gentest::supervisor {
namespace {

void check(DWORD error, const char *operation) {
    if (error != ERROR_SUCCESS)
        throw std::system_error(static_cast<int>(error), std::system_category(), operation);
}

class Handle {
  public:
    Handle(HANDLE value, const char *operation) : value_(value) {
        if (value == nullptr || value == INVALID_HANDLE_VALUE)
            check(GetLastError(), operation);
    }
    ~Handle() { CloseHandle(value_); }
    Handle(const Handle &)            = delete;
    Handle &operator=(const Handle &) = delete;

    auto get() const -> HANDLE { return value_; }

  private:
    HANDLE value_;
};

auto wide(std::string_view argument) -> std::wstring {
    if (argument.empty())
        return {};
    if (argument.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::length_error("supervised command argument is too long");

    const auto length = static_cast<int>(argument.size());
    const int  count  = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argument.data(), length, nullptr, 0);
    if (count == 0)
        check(GetLastError(), "decode UTF-8 child argument");
    std::wstring result(count, L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argument.data(), length, result.data(), count) == 0)
        check(GetLastError(), "decode UTF-8 child argument");
    return result;
}

// The C runtime treats backslashes specially before a quote. Double each
// such run, including the one before the closing delimiter.
void appendArgument(std::wstring &line, std::wstring_view argument) {
    line += L" \"";
    std::size_t slashes = 0;
    for (const wchar_t ch : argument) {
        if (ch == L'\\') {
            ++slashes;
            continue;
        }
        line.append(ch == L'"' ? 2 * slashes + 1 : slashes, L'\\');
        line.push_back(ch);
        slashes = 0;
    }
    line.append(2 * slashes, L'\\');
    line.push_back(L'"');
}

auto childInput(SECURITY_ATTRIBUTES &attributes) -> Handle {
    const HANDLE source = GetStdHandle(STD_INPUT_HANDLE);
    if (source == nullptr || source == INVALID_HANDLE_VALUE)
        return Handle(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes, OPEN_EXISTING, 0, nullptr),
                      "open child stdin");

    HANDLE duplicate{};
    if (!DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), &duplicate, 0, TRUE, DUPLICATE_SAME_ACCESS))
        check(GetLastError(), "duplicate child stdin");
    return Handle(duplicate, "duplicate child stdin");
}

class StartupAttributes {
  public:
    StartupAttributes() {
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 2, 0, &bytes);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            check(GetLastError(), "size child startup attributes");
        storage_.resize((bytes + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
        list_ = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
        if (!InitializeProcThreadAttributeList(list_, 2, 0, &bytes))
            check(GetLastError(), "initialize child startup attributes");
    }
    ~StartupAttributes() { DeleteProcThreadAttributeList(list_); }
    StartupAttributes(const StartupAttributes &)            = delete;
    StartupAttributes &operator=(const StartupAttributes &) = delete;

    auto get() const -> LPPROC_THREAD_ATTRIBUTE_LIST { return list_; }

  private:
    std::vector<std::max_align_t> storage_;
    LPPROC_THREAD_ATTRIBUTE_LIST  list_{};
};

class ChildJob {
  public:
    ChildJob() : job_(CreateJobObjectW(nullptr, nullptr), "create supervised process job") {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job_.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
            check(GetLastError(), "configure supervised process job");
    }
    ~ChildJob() { stop(); }
    ChildJob(const ChildJob &)            = delete;
    ChildJob &operator=(const ChildJob &) = delete;

    auto get() const -> HANDLE { return job_.get(); }

    // Descendants may still hold output and marker files after the selected
    // process exits. Allow the caller to read and remove them after cleanup.
    auto stop() noexcept -> DWORD {
        if (stopped_)
            return ERROR_SUCCESS;
        stopped_ = true;
        if (!TerminateJobObject(job_.get(), 1))
            return GetLastError();

        for (;;) {
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
            if (!QueryInformationJobObject(job_.get(), JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), nullptr))
                return GetLastError();
            if (accounting.ActiveProcesses == 0)
                return ERROR_SUCCESS;
            Sleep(1);
        }
    }

  private:
    Handle job_;
    bool   stopped_ = false;
};

} // namespace

auto runChild(const std::vector<std::string> &command, const std::filesystem::path &output, const std::function<bool()> &expired,
              std::chrono::milliseconds pollInterval) -> ChildResult {
    if (command.empty() || command.front().empty())
        throw std::invalid_argument("empty supervised command");

    const auto executable = wide(command.front());
    if (executable.find(L'"') != std::wstring::npos)
        throw std::invalid_argument("invalid executable name in supervised command");
    std::wstring line = L"\"" + executable + L"\"";
    for (std::size_t i = 1; i < command.size(); ++i)
        appendArgument(line, wide(command[i]));

    SECURITY_ATTRIBUTES inheritance{.nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = nullptr, .bInheritHandle = TRUE};
    const Handle capture(CreateFileW(output.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &inheritance,
                                     CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr),
                         "create child output file");
    const Handle input = childInput(inheritance);

    // Associate the process with the job at creation. If the supervisor itself
    // is killed, closing its private job handle terminates the entire tree.
    ChildJob              job;
    HANDLE                jobHandle = job.get();
    std::array<HANDLE, 2> handles{input.get(), capture.get()};
    StartupAttributes     attributes;
    if (!UpdateProcThreadAttribute(attributes.get(), 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &jobHandle, sizeof(jobHandle), nullptr, nullptr))
        check(GetLastError(), "assign supervised process job at launch");
    if (!UpdateProcThreadAttribute(attributes.get(), 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles.data(), sizeof(handles), nullptr,
                                   nullptr))
        check(GetLastError(), "restrict inherited child handles");

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb         = sizeof(startup);
    startup.StartupInfo.dwFlags    = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput  = input.get();
    startup.StartupInfo.hStdOutput = capture.get();
    startup.StartupInfo.hStdError  = capture.get();
    startup.lpAttributeList        = attributes.get();

    // A bare command uses the system executable search. Explicit paths are
    // passed separately so spaces are unambiguous to CreateProcessW.
    const wchar_t      *application = executable.find_first_of(L"\\/:") == std::wstring::npos ? nullptr : executable.c_str();
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(application, line.data(), nullptr, nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT | CREATE_NEW_PROCESS_GROUP, nullptr,
                        nullptr, &startup.StartupInfo, &child))
        check(GetLastError(), "launch supervised command");
    const Handle process(child.hProcess, "open supervised process"), thread(child.hThread, "open supervised thread");

    const auto waitMs = std::clamp(pollInterval.count(), std::chrono::milliseconds::rep{0}, std::chrono::milliseconds::rep{INFINITE - 1});
    for (;;) {
        const DWORD wait = WaitForSingleObject(process.get(), static_cast<DWORD>(waitMs));
        if (wait == WAIT_OBJECT_0) {
            DWORD status{};
            if (!GetExitCodeProcess(process.get(), &status))
                check(GetLastError(), "get supervised process exit code");
            check(job.stop(), "clean up supervised process job");
            return {.abnormal = status != 0, .status = status, .timed_out = false};
        }
        if (wait != WAIT_TIMEOUT)
            check(GetLastError(), "wait for supervised command");

        if (expired()) {
            check(job.stop(), "terminate timed-out process job");
            return {.abnormal = false, .status = 0, .timed_out = true};
        }
    }
}

} // namespace gentest::supervisor
