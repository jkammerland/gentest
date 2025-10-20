#if defined(__linux__)
// Work around LLVM distributions that ship only the legacy libtinfo.so.5
// symbol versions by forwarding the expected symbols to whichever terminfo
// library is available on the host. This keeps clang-cpp happy on CI images
// (Fedora 42, Ubuntu runners) without needing extra system packages.
#include <dlfcn.h>

#include <array>
#include <mutex>
#include <string_view>

struct TERMINAL;

namespace {
class TerminfoSymbolResolver {
public:
    TerminfoSymbolResolver() {
        static constexpr std::array<std::string_view, 5> kCandidates = {
            "libtinfo.so.5",
            "libtinfo.so.6",
            "libncursesw.so.6",
            "libncurses.so.6",
            "libncurses.so.5"};
        handles_.fill(nullptr);
        std::size_t index = 0;
        for (const auto &name : kCandidates) {
            void *handle = dlopen(name.data(), RTLD_LAZY | RTLD_LOCAL);
            handles_[index++] = handle;
        }
    }

    template <typename Fn>
    Fn lookup(const char *symbol) {
        for (void *handle : handles_) {
            if (!handle) {
                continue;
            }
            if (auto fn = reinterpret_cast<Fn>(dlsym(handle, symbol))) {
                return fn;
            }
        }
        return nullptr;
    }

private:
    std::array<void *, 5> handles_{};
};

TerminfoSymbolResolver &resolver() {
    static TerminfoSymbolResolver instance;
    return instance;
}

template <typename Fn>
Fn resolve(const char *symbol) {
    static std::once_flag once;
    static Fn cached = nullptr;
    std::call_once(once, [&] { cached = resolver().lookup<Fn>(symbol); });
    return cached;
}

constexpr int kErrValue = -1;
}

extern "C" int setupterm(const char *term, int fileDescriptor, int *errret) {
    using Fn = int (*)(const char *, int, int *);
    if (auto fn = resolve<Fn>("setupterm")) {
        return fn(term, fileDescriptor, errret);
    }
    if (errret) {
        *errret = -1;
    }
    return kErrValue;
}
__asm__(".symver setupterm,setupterm@NCURSES_TINFO_5.0.19991023");

extern "C" int del_curterm(TERMINAL *terminal) {
    using Fn = int (*)(TERMINAL *);
    if (auto fn = resolve<Fn>("del_curterm")) {
        return fn(terminal);
    }
    return kErrValue;
}
__asm__(".symver del_curterm,del_curterm@NCURSES_TINFO_5.0.19991023");

extern "C" TERMINAL *set_curterm(TERMINAL *terminal) {
    using Fn = TERMINAL *(*)(TERMINAL *);
    if (auto fn = resolve<Fn>("set_curterm")) {
        return fn(terminal);
    }
    return nullptr;
}
__asm__(".symver set_curterm,set_curterm@NCURSES_TINFO_5.0.19991023");

extern "C" int tigetnum(const char *capname) {
    using Fn = int (*)(const char *);
    if (auto fn = resolve<Fn>("tigetnum")) {
        return fn(capname);
    }
    return kErrValue;
}
__asm__(".symver tigetnum,tigetnum@NCURSES_TINFO_5.0.19991023");

#endif  // defined(__linux__)
