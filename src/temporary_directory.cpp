#include "gentest/temporary_directory.h"

#include "benchmark_internal.h"
#include "gentest/detail/runtime_context.h"
#include "temporary_directory_internal.h"

#include <cstdio>
#include <exception>
#include <fmt/format.h>
#include <random>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <windows.h>
#include <sddl.h>
// clang-format on
#else
#include <cerrno>
#include <sys/stat.h>
#endif

namespace gentest {
namespace {

namespace fs = std::filesystem;

auto createTemporaryDirectory() -> fs::path {
    const auto root = fs::absolute(fs::temp_directory_path());
#ifdef _WIN32
    // Grant access to the owner and SYSTEM. Children inherit these permissions.
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;FA;;;OW)(A;OICI;FA;;;SY)", SDDL_REVISION_1, &descriptor,
                                                                nullptr)) {
        throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), "set temporary directory permissions");
    }
    const std::unique_ptr<void, decltype(&::LocalFree)> security(descriptor, &::LocalFree);
    SECURITY_ATTRIBUTES attributes{.nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = security.get(), .bInheritHandle = FALSE};
#endif

    // Exclusive creation gives ownership to exactly one process, even on a collision.
    std::random_device random;
    for (int attempt = 0; attempt < 32; ++attempt) {
        auto directory = root / fmt::format("gentest-{:x}-{:x}", random(), random());
#ifdef _WIN32
        if (::CreateDirectoryW(directory.c_str(), &attributes))
            return directory;
        const auto error = ::GetLastError();
        if (error != ERROR_ALREADY_EXISTS && error != ERROR_FILE_EXISTS)
            throw fs::filesystem_error("create temporary directory", directory, {static_cast<int>(error), std::system_category()});
#else
        if (::mkdir(directory.c_str(), S_IRWXU) == 0)
            return directory;
        const int error = errno;
        if (error != EEXIST)
            throw fs::filesystem_error("create temporary directory", directory, {error, std::generic_category()});
#endif
    }
    throw fs::filesystem_error("create a unique temporary directory", root, std::make_error_code(std::errc::file_exists));
}

} // namespace

namespace detail {

struct TemporaryDirectoryState {
    fs::path path;
    bool     keep_on_failure = false;

    // A context abandoned during cancellation still owns its unprocessed paths.
    ~TemporaryDirectoryState() {
        try {
            remove();
        } catch (const std::exception &error) {
            (void)std::fprintf(stderr, "gentest: temporary directory cleanup failed: %s\n", error.what());
        } catch (...) { (void)std::fputs("gentest: temporary directory cleanup failed\n", stderr); }
    }

    void remove() {
        auto directory = std::exchange(path, {});
        if (!directory.empty())
            fs::remove_all(directory);
    }
};

auto finishTemporaryDirectories(TemporaryDirectories &directories, bool failed, std::string_view case_name) -> std::vector<std::string> {
    std::vector<std::string> failures;
    for (const auto &directory : directories) {
        if (failed && directory->keep_on_failure) {
            const auto path = std::exchange(directory->path, {}).u8string();
            fmt::print(stderr, "gentest: kept temporary directory for {} at {}\n", case_name,
                       std::string_view(reinterpret_cast<const char *>(path.data()), path.size()));
            continue;
        }

        try {
            directory->remove();
        } catch (const fs::filesystem_error &error) {
            failures.push_back(fmt::format("temporary directory cleanup failed: {}", error.what()));
        }
    }
    directories.clear();
    return failures;
}

void finish_fatal_temporary_directories() noexcept {
    // The exception-disabled runner terminates after its teardown hooks.
    try {
        if (const auto context = current_test()) {
            auto *measurement = measurement_state();
            auto &directories = measurement ? measurement->temporary_directories : context->temporary_directories;
            for (const auto &issue : finishTemporaryDirectories(directories, true, context->display_name))
                fmt::print(stderr, "{}\n", issue);
        }
    } catch (...) { (void)std::fputs("gentest: temporary directory cleanup failed\n", stderr); }
}

} // namespace detail

TemporaryDirectory::~TemporaryDirectory() = default;

void TemporaryDirectory::setUp() {
    auto directory  = std::make_shared<detail::TemporaryDirectoryState>();
    directory->path = createTemporaryDirectory();
    directory_      = std::move(directory);
}

void TemporaryDirectory::tearDown() {
    if (!directory_)
        return;

    // Decide retention after the remaining fixture hooks have run. Measurements
    // use separate contexts for setup, timed calls, and teardown.
    directory_->keep_on_failure = keep_on_failure_;
    if (auto *measurement = detail::measurement_state()) {
        measurement->temporary_directories.push_back(std::move(directory_));
    } else if (const auto context = detail::current_test()) {
        context->temporary_directories.push_back(std::move(directory_));
    } else {
        directory_->remove();
        directory_.reset();
    }
}

auto TemporaryDirectory::path() const noexcept -> const fs::path & {
    static const fs::path empty;
    return directory_ ? directory_->path : empty;
}

} // namespace gentest
