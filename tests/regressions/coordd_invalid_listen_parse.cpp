#include <iostream>

#define main coordd_embedded_main_for_invalid_listen_parse_test
#include "../../coordd/main.cpp"
#undef main

int main() {
    coordd::ServerConfig cfg{};
    char arg0[] = "coordd";
    char arg1[] = "--listen";
    char arg2[] = "tcp://127.0.0.1:notaport";
    char arg3[] = "--tls-ca";
    char arg4[] = "ca.pem";
    char arg5[] = "--tls-cert";
    char arg6[] = "cert.pem";
    char arg7[] = "--tls-key";
    char arg8[] = "key.pem";
    char *argv[] = {arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8};

    if (coordd::parse_args(static_cast<int>(sizeof(argv) / sizeof(argv[0])), argv, cfg)) {
        std::cerr << "coordd accepted an invalid --listen endpoint\n";
        return 1;
    }

    return 0;
}
