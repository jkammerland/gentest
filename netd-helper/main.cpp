#include "coord/codec.h"
#include "coord/transport.h"
#include "coord/types.h"

#include <iostream>
#include <string>
#include <thread>

using namespace coord;

static void usage() {
    std::cout << "netd-helper --listen <unix://path>\n";
}

int main(int argc, char **argv) {
    std::string listen = "unix://netd-helper.sock";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--listen" && i + 1 < argc) {
            listen = argv[++i];
        } else if (arg == "--help") {
            usage();
            return 0;
        }
    }
    std::string error;
    Endpoint ep = parse_endpoint(listen, &error);
    if (!error.empty()) {
        std::cerr << "netd-helper: " << error << "\n";
        return 1;
    }
    int fd = listen_endpoint(ep, &error);
    if (fd < 0) {
        std::cerr << "netd-helper: " << error << "\n";
        return 1;
    }
    while (true) {
        Connection conn = accept_connection(fd, TlsConfig{}, &error);
        if (!conn.is_valid()) {
            continue;
        }
        std::thread([c = std::move(conn)]() mutable {
            std::vector<std::byte> frame;
            std::string err;
            if (!c.read_frame(frame, &err)) {
                return;
            }
            Message reply{1, MsgError{"netd-helper not implemented"}};
            auto buf = encode_message(reply);
            c.write_frame(buf, nullptr);
        }).detach();
    }
    return 0;
}
