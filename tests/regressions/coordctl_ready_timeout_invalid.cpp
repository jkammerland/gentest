#include <iostream>

#define main coordctl_embedded_main_for_ready_timeout_invalid_test
#include "../../coordctl/main.cpp"
#undef main

int main() {
    Args args{};
    char arg0[] = "coordctl";
    char arg1[] = "daemonize";
    char arg2[] = "--coordd";
    char arg3[] = "coordd";
    char arg4[] = "--listen";
    char arg5[] = "unix://coordd.sock";
    char arg6[] = "--root";
    char arg7[] = "coordd-root";
    char arg8[] = "--ready-timeout-ms";
    char arg9[] = "oops";
    char *argv[] = {arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9};

    try {
        if (parse_args(static_cast<int>(sizeof(argv) / sizeof(argv[0])), argv, args)) {
            std::cerr << "coordctl accepted an invalid --ready-timeout-ms value\n";
            return 1;
        }
    } catch (const std::exception &ex) {
        std::cerr << "coordctl threw while parsing --ready-timeout-ms: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
