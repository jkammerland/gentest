#include "../src/common.h"

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <iostream>
#include <set>
#include <sys/resource.h>

namespace {
using namespace gentest::harness;
using namespace gentest::harness::detail;

void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

auto contents(int fd) -> std::string {
    std::array<char, 32> bytes{};
    const auto           count = ::pread(fd, bytes.data(), bytes.size(), 0);
    systemCheck(count >= 0, "read inherited file");
    return {bytes.data(), static_cast<std::size_t>(count)};
}

void writeAll(int fd, std::string_view bytes) {
    while (!bytes.empty()) {
        const auto count = ::write(fd, bytes.data(), bytes.size());
        systemCheck(count > 0, "write output");
        bytes.remove_prefix(static_cast<std::size_t>(count));
    }
}

void descriptors(int argc, char **argv) {
    // Inspect the exec boundary before Channel::attach creates private copies.
    std::set<int> expected{0, 1, 2};
    std::string   channel = std::getenv("GENTEST_HARNESS_CHANNEL");
    std::replace(channel.begin(), channel.end(), ',', ' ');
    for (std::size_t begin = 0; begin < channel.size();) {
        std::size_t consumed{};
        expected.insert(std::stoi(channel.substr(begin), &consumed));
        begin += consumed;
    }
    for (int i = 2; i < argc; i += 2) {
        const int fd = std::stoi(argv[i]);
        expected.insert(fd);
        require(contents(fd) == argv[i + 1], "inherited descriptor content mismatch");
    }
    DIR *directory = ::opendir("/proc/self/fd");
    systemCheck(directory != nullptr, "inspect child descriptors");
    expected.insert(::dirfd(directory));
    std::set<int> actual;
    while (const auto *entry = ::readdir(directory)) {
        if (entry->d_name[0] != '.') {
            actual.insert(std::stoi(entry->d_name));
        }
    }
    ::closedir(directory);
    require(actual == expected, "unexpected descriptor survived exec");
}
} // namespace

int main(int argc, char **argv) {
    try {
        require(argc > 1, "missing descriptor fixture mode");
        const std::string_view mode(argv[1]);
        if (mode == "descriptors") {
            descriptors(argc, argv);
        } else if (mode == "anchor") {
            Fd file(::openat(std::stoi(argv[2]), "token", O_RDONLY | O_CLOEXEC));
            systemCheck(file.value >= 0, "open anchored file");
            require(contents(file.value) == "original", "directory anchor changed after replacement");
        } else if (mode == "output") {
            writeAll(STDOUT_FILENO, std::string(12000, 'a') + "stdout end\n");
            writeAll(STDERR_FILENO, std::string(9000, 'b') + "stderr end\n");
        } else if (mode == "limits") {
            for (int i = 0; i != 4; ++i) {
                rlimit limit{};
                systemCheck(::getrlimit(limitResources[static_cast<std::size_t>(i)], &limit) == 0, "read limit");
                require(limit.rlim_cur == std::stoull(argv[2 + 2 * i]) && limit.rlim_max == std::stoull(argv[3 + 2 * i]),
                        "child resource limit differs");
            }
        } else if (mode == "file-cap") {
            writeAll(STDOUT_FILENO, std::string(8192, 'x'));
        } else if (mode == "cpu-cap") {
            for (;;) {
                ::getpid();
            }
        } else {
            throw std::runtime_error("unknown descriptor fixture mode");
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
