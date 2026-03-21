#include "coord/codec.h"
#include "coord/transport.h"
#include "coord/types.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#define main coordd_embedded_main_for_peer_target_forwarding_test
#include "../../coordd/main.cpp"
#undef main

namespace fs = std::filesystem;

namespace {

struct PeerCapture {
    std::string error;
    std::string forwarded_target;
    bool        saw_submit{false};
    bool        saw_wait{false};
};

std::string make_socket_path() {
    fs::path root = fs::temp_directory_path() / "gentest_coord_regressions";
    fs::create_directories(root);
    auto stamp = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    return (root / ("peer_target_" + stamp + ".sock")).string();
}

void run_peer_server(int listener_fd, PeerCapture &capture) {
    coord::Connection conn = coord::accept_connection(listener_fd, coord::TlsConfig{}, &capture.error);
#ifndef _WIN32
    ::close(listener_fd);
#endif
    if (!conn.is_valid()) {
        return;
    }

    std::vector<std::byte> frame;
    if (!conn.read_frame(frame, &capture.error)) {
        return;
    }

    auto decoded = coord::decode_message(frame);
    if (!decoded.ok || !std::holds_alternative<coord::MsgSessionSubmit>(decoded.message.payload)) {
        capture.error = decoded.ok ? "expected submit message" : decoded.error;
        return;
    }

    capture.saw_submit = true;
    capture.forwarded_target = std::get<coord::MsgSessionSubmit>(decoded.message.payload).spec.placement.target;

    coord::Message accepted{1, coord::MsgSessionAccepted{"remote-session"}};
    auto reply = coord::encode_message(accepted, &capture.error);
    if (reply.empty()) {
        return;
    }
    if (!conn.write_frame(reply, &capture.error)) {
        return;
    }

    frame.clear();
    if (!conn.read_frame(frame, &capture.error)) {
        return;
    }

    auto decoded_wait = coord::decode_message(frame);
    if (!decoded_wait.ok || !std::holds_alternative<coord::MsgSessionWait>(decoded_wait.message.payload)) {
        capture.error = decoded_wait.ok ? "expected wait message" : decoded_wait.error;
        return;
    }

    capture.saw_wait = true;
    coord::SessionManifest manifest{};
    manifest.session_id = "remote-session";
    manifest.group = "regressions";
    manifest.mode = coord::ExecMode::A;
    manifest.result = coord::ResultCode::Success;

    coord::Message done{1, coord::MsgSessionManifest{manifest}};
    reply = coord::encode_message(done, &capture.error);
    if (reply.empty()) {
        return;
    }
    conn.write_frame(reply, &capture.error);
}

} // namespace

coord::Connection accept_once(int listener_fd, std::string &error) {
    coord::Connection conn = coord::accept_connection(listener_fd, coord::TlsConfig{}, &error);
#ifndef _WIN32
    ::close(listener_fd);
#endif
    return conn;
}

int main() {
    using namespace coord;

    const std::string peer_socket_path = make_socket_path();
    const std::string peer_listen_addr = "unix://" + peer_socket_path;
    const std::string origin_socket_path = make_socket_path();
    const std::string origin_listen_addr = "unix://" + origin_socket_path;
    fs::remove(peer_socket_path);
    fs::remove(origin_socket_path);

    PeerCapture capture{};
    std::string error;
    Endpoint peer_endpoint = parse_endpoint(peer_listen_addr, &error);
    if (!error.empty()) {
        std::cerr << error << '\n';
        return 1;
    }
    int peer_listener_fd = listen_endpoint(peer_endpoint, &error);
    if (peer_listener_fd < 0) {
        std::cerr << error << '\n';
        return 1;
    }
    std::thread peer_server([&] { run_peer_server(peer_listener_fd, capture); });

    Endpoint origin_endpoint = parse_endpoint(origin_listen_addr, &error);
    if (!error.empty()) {
        std::cerr << error << '\n';
        return 1;
    }
    int origin_listener_fd = listen_endpoint(origin_endpoint, &error);
    if (origin_listener_fd < 0) {
        std::cerr << error << '\n';
        return 1;
    }

    coordd::SessionManager sessions((fs::temp_directory_path() / "gentest_coord_root").string(), TlsConfig{});
    coordd::ServerConfig cfg{};
    cfg.listen = origin_endpoint;
    cfg.root_dir = (fs::temp_directory_path() / "gentest_coord_root").string();
    std::thread origin_server([&] {
        auto conn = accept_once(origin_listener_fd, error);
        if (!conn.is_valid()) {
            return;
        }
        coordd::handle_connection(std::move(conn), sessions, cfg);
    });

    SessionSpec spec{};
    spec.session_id = "origin";
    spec.group = "regressions";
    spec.mode = ExecMode::A;
    spec.placement.target = "peer:" + peer_listen_addr;

    Connection client = connect_endpoint(origin_endpoint, TlsConfig{}, &error);
    if (!client.is_valid()) {
        std::cerr << error << '\n';
        return 1;
    }

    Message submit_msg{1, MsgSessionSubmit{spec}};
    auto frame = encode_message(submit_msg, &error);
    if (frame.empty() || !client.write_frame(frame, &error)) {
        std::cerr << error << '\n';
        return 1;
    }

    frame.clear();
    if (!client.read_frame(frame, &error)) {
        std::cerr << error << '\n';
        return 1;
    }
    auto accepted = decode_message(frame);
    if (!accepted.ok || !std::holds_alternative<MsgSessionAccepted>(accepted.message.payload)) {
        std::cerr << (accepted.ok ? "expected accepted message" : accepted.error) << '\n';
        return 1;
    }

    const std::string session_id = std::get<MsgSessionAccepted>(accepted.message.payload).session_id;
    Message wait_msg{1, MsgSessionWait{session_id}};
    frame = encode_message(wait_msg, &error);
    if (frame.empty() || !client.write_frame(frame, &error)) {
        std::cerr << error << '\n';
        return 1;
    }

    frame.clear();
    if (!client.read_frame(frame, &error)) {
        std::cerr << error << '\n';
        return 1;
    }
    auto manifest_msg = decode_message(frame);
    if (!manifest_msg.ok || !std::holds_alternative<MsgSessionManifest>(manifest_msg.message.payload)) {
        std::cerr << (manifest_msg.ok ? "expected manifest message" : manifest_msg.error) << '\n';
        return 1;
    }

    client = Connection{};
    origin_server.join();
    peer_server.join();
    fs::remove(peer_socket_path);
    fs::remove(origin_socket_path);

    if (!capture.error.empty()) {
        std::cerr << capture.error << '\n';
        return 1;
    }
    if (!capture.saw_submit || !capture.saw_wait) {
        std::cerr << "peer server did not observe the expected remote submit/wait flow\n";
        return 1;
    }
    if (capture.forwarded_target.rfind("peer:", 0) == 0) {
        std::cerr << "forwarded spec still contains peer placement target: " << capture.forwarded_target << '\n';
        return 1;
    }

    return 0;
}
