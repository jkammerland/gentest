#include <cerrno>
#include <cstdio>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

// The linker --wrap contract requires these two reserved symbol names.
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
extern "C" int __real_execvp(const char *, char *const[]);
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
extern "C" int __wrap_execvp(const char *file, char *const argv[]) {
    if (std::string_view(file) == "sysctl" && argv[1] && argv[2] && argv[3] && !argv[4] && std::string_view(argv[1]) == "-q" &&
        std::string_view(argv[2]) == "-w" && std::string_view(argv[3]) == "net.ipv6.conf.all.forwarding=1") {
        struct stat info{};
        if (::stat("/proc/self/ns/pid", &info) == 0) {
            ::dprintf(STDERR_FILENO, "injected IPv6 forwarding failure in PID namespace %llu\n",
                      static_cast<unsigned long long>(info.st_ino));
        }
        errno = EACCES;
        return -1;
    }
    return __real_execvp(file, argv);
}
