#include "coord/codec.h"
#include "coord/json.h"
#include "coord/transport.h"
#include "coord/types.h"
#include "gentest/attributes.h"
#include "gentest/runner.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if COORD_ENABLE_JSON
#include <nlohmann/json.hpp>
#endif

using namespace gentest::asserts;

namespace coord_tests {

#ifndef COORDCTL_BIN_PATH
#define COORDCTL_BIN_PATH ""
#endif

#ifndef COORDD_BIN_PATH
#define COORDD_BIN_PATH ""
#endif

#ifndef _WIN32
static std::filesystem::path make_socket_path(const char *tag) {
    auto base = std::filesystem::temp_directory_path();
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto pid = static_cast<unsigned long>(::getpid());
    std::string name = "coord_" + std::to_string(pid) + "_" + std::to_string(stamp) + "_" + std::string(tag) + ".sock";
    return base / name;
}

static std::filesystem::path make_temp_path(const char *tag, const char *suffix = "") {
    auto base = std::filesystem::temp_directory_path();
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto pid = static_cast<unsigned long>(::getpid());
    std::string name = "coord_" + std::to_string(pid) + "_" + std::to_string(stamp) + "_" + std::string(tag) + std::string(suffix);
    return base / name;
}

struct ExecResult {
    int exit_code{-1};
    std::string stdout_text;
    std::string stderr_text;
};

static std::string read_file_text(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

static std::string trim_copy(std::string text) {
    auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

static ExecResult run_exec_capture(const std::vector<std::string> &argv) {
    ExecResult result{};
    auto stdout_path = make_temp_path("stdout", ".log");
    auto stderr_path = make_temp_path("stderr", ".log");
    pid_t pid = ::fork();
    if (pid < 0) {
        result.stderr_text = "fork failed";
        return result;
    }
    if (pid == 0) {
        int out_fd = ::open(stdout_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
        int err_fd = ::open(stderr_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
        if (out_fd >= 0) {
            ::dup2(out_fd, STDOUT_FILENO);
            ::close(out_fd);
        }
        if (err_fd >= 0) {
            ::dup2(err_fd, STDERR_FILENO);
            ::close(err_fd);
        }
        std::vector<char *> child_argv;
        child_argv.reserve(argv.size() + 1);
        for (const auto &arg : argv) {
            child_argv.push_back(const_cast<char *>(arg.c_str()));
        }
        child_argv.push_back(nullptr);
        ::execv(child_argv[0], child_argv.data());
        _exit(127);
    }

    int status = 0;
    (void)::waitpid(pid, &status, 0);
    result.stdout_text = read_file_text(stdout_path);
    result.stderr_text = read_file_text(stderr_path);
    std::filesystem::remove(stdout_path);
    std::filesystem::remove(stderr_path);
    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    }
    return result;
}

static bool wait_for_file(const std::filesystem::path &path, std::uint32_t timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (std::filesystem::exists(path)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return std::filesystem::exists(path);
}

static pid_t spawn_process(const std::vector<std::string> &argv, std::string &error) {
    pid_t pid = ::fork();
    if (pid < 0) {
        error = "fork failed";
        return -1;
    }
    if (pid == 0) {
        int null_fd = ::open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            ::dup2(null_fd, STDIN_FILENO);
            ::dup2(null_fd, STDOUT_FILENO);
            ::dup2(null_fd, STDERR_FILENO);
            if (null_fd > STDERR_FILENO) {
                ::close(null_fd);
            }
        }
        std::vector<char *> child_argv;
        child_argv.reserve(argv.size() + 1);
        for (const auto &arg : argv) {
            child_argv.push_back(const_cast<char *>(arg.c_str()));
        }
        child_argv.push_back(nullptr);
        ::execv(child_argv[0], child_argv.data());
        _exit(127);
    }
    return pid;
}

static bool wait_for_child_exit(pid_t pid, std::uint32_t timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    int status = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        pid_t rc = ::waitpid(pid, &status, WNOHANG);
        if (rc == pid) {
            return true;
        }
        if (rc < 0) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return false;
}

static pid_t read_pid_file(const std::filesystem::path &pid_file) {
    std::ifstream in(pid_file);
    long pid = -1;
    in >> pid;
    return static_cast<pid_t>(pid);
}

struct ChildProcessGuard {
    pid_t pid{-1};

    ~ChildProcessGuard() {
        if (pid <= 0) {
            return;
        }
        (void)::kill(pid, SIGKILL);
        int status = 0;
        (void)::waitpid(pid, &status, 0);
    }
};
#endif

[[using gentest: test("coord/cbor_roundtrip")]]
void cbor_roundtrip() {
    coord::SessionSpec spec{};
    spec.session_id = "session_1";
    spec.group = "group_a";
    spec.mode = coord::ExecMode::A;
    spec.artifact_dir = "artifacts";
    spec.timeouts.startup_ms = 1234;
    spec.timeouts.session_ms = 5678;
    spec.timeouts.shutdown_ms = 42;

    coord::PortRequest req{};
    req.name = "udp_server";
    req.count = 2;
    req.protocol = coord::Protocol::Udp;
    spec.network.ports.push_back(req);

    coord::NodeDef node{};
    node.name = "node1";
    node.exec = "./node1";
    node.args = {"--flag"};
    node.instances = 2;
    node.readiness.kind = coord::ReadinessKind::StdoutToken;
    node.readiness.value = "READY";
    spec.nodes.push_back(node);

    coord::Message msg{1, coord::MsgSessionSubmit{spec}};

    std::string error;
    auto encoded = coord::encode_message(msg, &error);
    ASSERT_FALSE(encoded.empty(), error);

    auto decoded = coord::decode_message(encoded);
    ASSERT_TRUE(decoded.ok, decoded.error);
    ASSERT_TRUE(std::holds_alternative<coord::MsgSessionSubmit>(decoded.message.payload));

    auto decoded_spec = std::get<coord::MsgSessionSubmit>(decoded.message.payload).spec;
    EXPECT_EQ(decoded_spec.group, spec.group);
    EXPECT_EQ(decoded_spec.mode, spec.mode);
    EXPECT_EQ(decoded_spec.nodes.size(), spec.nodes.size());
    EXPECT_EQ(decoded_spec.network.ports.size(), spec.network.ports.size());
    EXPECT_EQ(decoded_spec.network.ports[0].name, spec.network.ports[0].name);
    EXPECT_EQ(decoded_spec.network.ports[0].count, spec.network.ports[0].count);
    EXPECT_EQ(decoded_spec.network.ports[0].protocol, spec.network.ports[0].protocol);
}

[[using gentest: test("coord/cbor_manifest_status")]]
void cbor_manifest_status() {
    coord::SessionManifest manifest{};
    manifest.session_id = "s1";
    manifest.group = "g";
    manifest.mode = coord::ExecMode::A;
    manifest.result = coord::ResultCode::Failed;
    manifest.fail_reason = "boom";
    coord::InstanceInfo info{};
    info.node = "node";
    info.index = 0;
    info.exit_code = 12;
    info.term_signal = 0;
    info.log_path = "stdout.log";
    info.err_path = "stderr.log";
    info.addr = "127.0.0.1";
    coord::PortAssignment pa{};
    pa.name = "tcp";
    pa.protocol = coord::Protocol::Tcp;
    pa.ports = {1234, 5678};
    info.ports.push_back(pa);
    manifest.instances.push_back(info);

    coord::Message msg{2, coord::MsgSessionManifest{manifest}};
    std::string error;
    auto encoded = coord::encode_message(msg, &error);
    ASSERT_FALSE(encoded.empty(), error);
    auto decoded = coord::decode_message(encoded);
    ASSERT_TRUE(decoded.ok, decoded.error);
    ASSERT_TRUE(std::holds_alternative<coord::MsgSessionManifest>(decoded.message.payload));

    coord::SessionStatus status{};
    status.session_id = "s1";
    status.result = coord::ResultCode::Timeout;
    status.complete = true;
    coord::Message status_msg{3, coord::MsgSessionStatus{status}};
    encoded = coord::encode_message(status_msg, &error);
    ASSERT_FALSE(encoded.empty(), error);
    decoded = coord::decode_message(encoded);
    ASSERT_TRUE(decoded.ok, decoded.error);
    ASSERT_TRUE(std::holds_alternative<coord::MsgSessionStatus>(decoded.message.payload));
}

[[using gentest: test("coord/codec_decode_error")]]
void codec_decode_error() {
    std::vector<std::byte> empty;
    auto decoded = coord::decode_message(empty);
    EXPECT_FALSE(decoded.ok);
    EXPECT_FALSE(decoded.error.empty());
}

[[using gentest: test("coord/endpoint_parse")]]
void endpoint_parse() {
    std::string error;
    auto ep = coord::parse_endpoint("unix:///tmp/coord.sock", &error);
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(ep.kind, coord::Endpoint::Kind::Unix);
    EXPECT_EQ(ep.path, "/tmp/coord.sock");

    ep = coord::parse_endpoint("/tmp/raw.sock", &error);
    EXPECT_EQ(ep.kind, coord::Endpoint::Kind::Unix);
    EXPECT_EQ(ep.path, "/tmp/raw.sock");

    ep = coord::parse_endpoint("tcp://127.0.0.1:5555", &error);
    EXPECT_EQ(ep.kind, coord::Endpoint::Kind::Tcp);
    EXPECT_EQ(ep.host, "127.0.0.1");
    EXPECT_EQ(ep.port, 5555);

    ep = coord::parse_endpoint("localhost:1234", &error);
    EXPECT_EQ(ep.kind, coord::Endpoint::Kind::Tcp);
    EXPECT_EQ(ep.host, "localhost");
    EXPECT_EQ(ep.port, 1234);

    error.clear();
    ep = coord::parse_endpoint("bad_endpoint", &error);
    EXPECT_FALSE(error.empty());
}

[[using gentest: test("coord/endpoint_parse_invalid_port")]]
void endpoint_parse_invalid_port() {
    std::string error;
    coord::Endpoint ep{};

    bool threw = false;
    try {
        ep = coord::parse_endpoint("localhost:not-a-port", &error);
    } catch (...) {
        threw = true;
    }
    EXPECT_FALSE(threw);
    EXPECT_EQ(ep.kind, coord::Endpoint::Kind::Tcp);
    EXPECT_EQ(ep.host, "localhost");
    EXPECT_EQ(ep.port, 0);
    EXPECT_EQ(error, "tcp endpoint port must be numeric");

    threw = false;
    error.clear();
    try {
        ep = coord::parse_endpoint("tcp://127.0.0.1:70000", &error);
    } catch (...) {
        threw = true;
    }
    EXPECT_FALSE(threw);
    EXPECT_EQ(ep.kind, coord::Endpoint::Kind::Tcp);
    EXPECT_EQ(ep.host, "127.0.0.1");
    EXPECT_EQ(ep.port, 0);
    EXPECT_EQ(error, "tcp endpoint port out of range");
}

[[using gentest: test("coord/transport_frame_outgoing_oversized")]]
void transport_frame_outgoing_oversized() {
    coord::Connection conn{};
    std::string error;
    const auto too_large = static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) + std::size_t{1};
    bool ok = conn.write_frame(nullptr, too_large, &error);
    EXPECT_FALSE(ok);
    EXPECT_EQ(error, "outgoing frame too large");
}

#ifndef _WIN32
[[using gentest: test("coord/transport_frame_roundtrip")]]
void transport_frame_roundtrip() {
    auto path = make_socket_path("roundtrip");
    coord::Endpoint ep{};
    ep.kind = coord::Endpoint::Kind::Unix;
    ep.path = path.string();

    std::string error;
    coord::SocketHandle listener = coord::listen_endpoint(ep, &error);
    ASSERT_TRUE(listener != coord::kInvalidSocketHandle, error);

    coord::Connection client = coord::connect_endpoint(ep, {}, &error);
    bool client_ok = client.is_valid();

    std::vector<std::byte> server_payload;
    std::string server_error;
    std::thread server;
    if (client_ok) {
        server = std::thread([&]() {
            coord::Connection conn = coord::accept_connection(listener, {}, &server_error);
            if (!conn.is_valid()) {
                if (server_error.empty()) {
                    server_error = "accept failed";
                }
                return;
            }
            std::vector<std::byte> data;
            if (!conn.read_frame(data, &server_error)) {
                return;
            }
            server_payload = data;
            if (!conn.write_frame(data, &server_error)) {
                return;
            }
        });
    }

    std::vector<std::byte> payload{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    std::vector<std::byte> reply;
    if (client_ok) {
        client_ok = client.write_frame(payload, &error);
    }
    if (client_ok) {
        client_ok = client.read_frame(reply, &error);
    }

    if (server.joinable()) {
        server.join();
    }
    ::close(static_cast<int>(listener));
    std::filesystem::remove(path);

    ASSERT_TRUE(server_error.empty(), server_error);
    EXPECT_TRUE(client_ok, error);
    if (client_ok) {
        EXPECT_EQ(server_payload, payload);
        EXPECT_EQ(reply, payload);
    }
}

[[using gentest: test("coord/transport_frame_errors")]]
void transport_frame_errors() {
    auto path = make_socket_path("errors");
    coord::Endpoint ep{};
    ep.kind = coord::Endpoint::Kind::Unix;
    ep.path = path.string();

    std::string error;
    coord::SocketHandle listener = coord::listen_endpoint(ep, &error);
    ASSERT_TRUE(listener != coord::kInvalidSocketHandle, error);

    coord::Connection client = coord::connect_endpoint(ep, {}, &error);
    bool client_ok = client.is_valid();
    bool server_read_ok = false;
    std::string server_error;
    std::thread server;
    if (client_ok) {
        server = std::thread([&]() {
            coord::Connection conn = coord::accept_connection(listener, {}, &server_error);
            if (!conn.is_valid()) {
                if (server_error.empty()) {
                    server_error = "accept failed";
                }
                return;
            }
            std::vector<std::byte> data;
            server_read_ok = conn.read_frame(data, &server_error);
        });
    }
    if (client_ok) {
        std::uint32_t len_be = htonl(8);
        auto len_rc = ::write(static_cast<int>(client.fd()), &len_be, sizeof(len_be));
        client_ok = (len_rc > 0);
    }
    if (client_ok) {
        std::array<std::byte, 4> partial{std::byte{0xaa}, std::byte{0xbb}, std::byte{0xcc}, std::byte{0xdd}};
        auto data_rc = ::write(static_cast<int>(client.fd()), partial.data(), partial.size());
        client_ok = (data_rc > 0);
    }
    client = coord::Connection{};

    if (server.joinable()) {
        server.join();
    }
    ::close(static_cast<int>(listener));
    std::filesystem::remove(path);

    EXPECT_TRUE(client_ok, error);
    EXPECT_FALSE(server_read_ok);
    EXPECT_FALSE(server_error.empty());
}

[[using gentest: test("coord/transport_frame_incoming_oversized")]]
void transport_frame_incoming_oversized() {
    auto path = make_socket_path("oversized");
    coord::Endpoint ep{};
    ep.kind = coord::Endpoint::Kind::Unix;
    ep.path = path.string();

    std::string error;
    coord::SocketHandle listener = coord::listen_endpoint(ep, &error);
    ASSERT_TRUE(listener != coord::kInvalidSocketHandle, error);

    coord::Connection client = coord::connect_endpoint(ep, {}, &error);
    bool client_ok = client.is_valid();
    bool server_read_ok = false;
    std::string server_error;
    std::thread server;
    if (client_ok) {
        server = std::thread([&]() {
            coord::Connection conn = coord::accept_connection(listener, {}, &server_error);
            if (!conn.is_valid()) {
                if (server_error.empty()) {
                    server_error = "accept failed";
                }
                return;
            }
            std::vector<std::byte> data;
            server_read_ok = conn.read_frame(data, &server_error);
        });
    }
    if (client_ok) {
        std::uint32_t len_be = htonl(std::numeric_limits<std::uint32_t>::max());
        auto len_rc = ::write(static_cast<int>(client.fd()), &len_be, sizeof(len_be));
        client_ok = (len_rc == static_cast<ssize_t>(sizeof(len_be)));
    }
    client = coord::Connection{};

    if (server.joinable()) {
        server.join();
    }
    ::close(static_cast<int>(listener));
    std::filesystem::remove(path);

    EXPECT_TRUE(client_ok, error);
    EXPECT_FALSE(server_read_ok);
    EXPECT_EQ(server_error, "incoming frame too large");
}

[[using gentest: test("coord/coordctl_shutdown_msg_error_nonzero")]]
void coordctl_shutdown_msg_error_nonzero() {
    ASSERT_FALSE(std::string(COORDCTL_BIN_PATH).empty());
    auto path = make_socket_path("shutdown_msg_error");
    coord::Endpoint ep{};
    ep.kind = coord::Endpoint::Kind::Unix;
    ep.path = path.string();

    std::string error;
    coord::SocketHandle listener = coord::listen_endpoint(ep, &error);
    ASSERT_TRUE(listener != coord::kInvalidSocketHandle, error);

    std::string server_error;
    std::thread server([&]() {
        coord::Connection conn = coord::accept_connection(listener, {}, &server_error);
        if (!conn.is_valid()) {
            return;
        }
        std::vector<std::byte> frame;
        if (!conn.read_frame(frame, &server_error)) {
            return;
        }
        auto decoded = coord::decode_message(frame);
        if (!decoded.ok) {
            server_error = decoded.error;
            return;
        }
        if (!std::holds_alternative<coord::MsgShutdown>(decoded.message.payload)) {
            server_error = "expected shutdown message";
            return;
        }
        coord::Message reply{1, coord::MsgError{"forced shutdown failure"}};
        auto payload = coord::encode_message(reply, &server_error);
        if (payload.empty()) {
            return;
        }
        (void)conn.write_frame(payload, &server_error);
    });

    auto result = run_exec_capture({COORDCTL_BIN_PATH, "shutdown", "--connect", "unix://" + path.string(), "--token", "bad"});
    if (server.joinable()) {
        server.join();
    }
    ::close(static_cast<int>(listener));
    std::filesystem::remove(path);

    ASSERT_TRUE(server_error.empty(), server_error);
    EXPECT_EQ(result.exit_code, 1);
    EXPECT_NE(result.stderr_text.find("forced shutdown failure"), std::string::npos);
}

[[using gentest: test("coord/coordctl_shutdown_unexpected_payload_nonzero")]]
void coordctl_shutdown_unexpected_payload_nonzero() {
    ASSERT_FALSE(std::string(COORDCTL_BIN_PATH).empty());
    auto path = make_socket_path("shutdown_unexpected");
    coord::Endpoint ep{};
    ep.kind = coord::Endpoint::Kind::Unix;
    ep.path = path.string();

    std::string error;
    coord::SocketHandle listener = coord::listen_endpoint(ep, &error);
    ASSERT_TRUE(listener != coord::kInvalidSocketHandle, error);

    std::string server_error;
    std::thread server([&]() {
        coord::Connection conn = coord::accept_connection(listener, {}, &server_error);
        if (!conn.is_valid()) {
            return;
        }
        std::vector<std::byte> frame;
        if (!conn.read_frame(frame, &server_error)) {
            return;
        }
        coord::Message reply{1, coord::MsgSessionAccepted{"sid"}};
        auto payload = coord::encode_message(reply, &server_error);
        if (payload.empty()) {
            return;
        }
        (void)conn.write_frame(payload, &server_error);
    });

    auto result = run_exec_capture({COORDCTL_BIN_PATH, "shutdown", "--connect", "unix://" + path.string()});
    if (server.joinable()) {
        server.join();
    }
    ::close(static_cast<int>(listener));
    std::filesystem::remove(path);

    ASSERT_TRUE(server_error.empty(), server_error);
    EXPECT_EQ(result.exit_code, 1);
    EXPECT_NE(result.stderr_text.find("unexpected response to shutdown"), std::string::npos);
}

[[using gentest: test("coord/coordctl_shutdown_recv_failure_nonzero")]]
void coordctl_shutdown_recv_failure_nonzero() {
    ASSERT_FALSE(std::string(COORDCTL_BIN_PATH).empty());
    auto path = make_socket_path("shutdown_recv_fail");
    coord::Endpoint ep{};
    ep.kind = coord::Endpoint::Kind::Unix;
    ep.path = path.string();

    std::string error;
    coord::SocketHandle listener = coord::listen_endpoint(ep, &error);
    ASSERT_TRUE(listener != coord::kInvalidSocketHandle, error);

    std::string server_error;
    std::thread server([&]() {
        coord::Connection conn = coord::accept_connection(listener, {}, &server_error);
        if (!conn.is_valid()) {
            return;
        }
        std::vector<std::byte> frame;
        (void)conn.read_frame(frame, &server_error);
    });

    auto result = run_exec_capture({COORDCTL_BIN_PATH, "shutdown", "--connect", "unix://" + path.string()});
    if (server.joinable()) {
        server.join();
    }
    ::close(static_cast<int>(listener));
    std::filesystem::remove(path);

    EXPECT_TRUE(server_error.empty() || server_error.find("failed to read frame") != std::string::npos);
    EXPECT_EQ(result.exit_code, 1);
    EXPECT_FALSE(trim_copy(result.stderr_text).empty());
}

[[using gentest: test("coord/coordctl_daemonize_timeout_cleans_child")]]
void coordctl_daemonize_timeout_cleans_child() {
    ASSERT_FALSE(std::string(COORDCTL_BIN_PATH).empty());
    auto base_dir = make_temp_path("daemonize_timeout");
    std::filesystem::create_directories(base_dir);

    auto script_path = base_dir / "fake_coordd.sh";
    auto root_dir = base_dir / "root";
    auto ready_file = base_dir / "coordd.ready";
    auto pid_file = base_dir / "coordd.pid";
    auto sock_path = base_dir / "coordd.sock";
    std::filesystem::create_directories(root_dir);

    {
        std::ofstream script(script_path);
        script << "#!/bin/sh\n";
        script << "pid_file=\"\"\n";
        script << "while [ \"$#\" -gt 0 ]; do\n";
        script << "  if [ \"$1\" = \"--pid-file\" ] && [ \"$#\" -gt 1 ]; then\n";
        script << "    pid_file=\"$2\"\n";
        script << "    shift 2\n";
        script << "    continue\n";
        script << "  fi\n";
        script << "  shift\n";
        script << "done\n";
        script << "if [ -n \"$pid_file\" ]; then\n";
        script << "  echo $$ > \"$pid_file\"\n";
        script << "fi\n";
        script << "trap 'exit 0' TERM INT\n";
        script << "while true; do sleep 1; done\n";
    }

    std::filesystem::permissions(script_path,
                                 std::filesystem::perms::owner_read | std::filesystem::perms::owner_write
                                     | std::filesystem::perms::owner_exec,
                                 std::filesystem::perm_options::replace);

    auto endpoint = "unix://" + sock_path.string();
    auto result = run_exec_capture({COORDCTL_BIN_PATH,
                                    "daemonize",
                                    "--coordd",
                                    script_path.string(),
                                    "--listen",
                                    endpoint,
                                    "--root",
                                    root_dir.string(),
                                    "--ready-file",
                                    ready_file.string(),
                                    "--pid-file",
                                    pid_file.string(),
                                    "--ready-timeout-ms",
                                    "350"});

    EXPECT_EQ(result.exit_code, 1);
    EXPECT_NE(result.stderr_text.find("ready file did not appear"), std::string::npos);
    ASSERT_TRUE(wait_for_file(pid_file, 2000), "fake daemon did not write pid file");

    auto pid = read_pid_file(pid_file);
    ASSERT_TRUE(pid > 0);
    bool dead = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
    while (std::chrono::steady_clock::now() < deadline) {
        if (::kill(pid, 0) != 0 && errno == ESRCH) {
            dead = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    if (!dead) {
        (void)::kill(pid, SIGKILL);
    }

    EXPECT_TRUE(dead, "daemonize timeout left orphan process alive");
    std::filesystem::remove_all(base_dir);
}

[[using gentest: test("coord/coordd_status_wait_polling_consistent")]]
void coordd_status_wait_polling_consistent() {
    ASSERT_FALSE(std::string(COORDCTL_BIN_PATH).empty());
    ASSERT_FALSE(std::string(COORDD_BIN_PATH).empty());

    auto base_dir = make_temp_path("status_wait");
    std::filesystem::create_directories(base_dir);
    auto root_dir = base_dir / "root";
    std::filesystem::create_directories(root_dir);
    auto ready_file = base_dir / "coordd.ready";
    auto socket_path = base_dir / "coordd.sock";
    auto script_path = base_dir / "sleep_node.sh";
    auto spec_path = base_dir / "spec.json";
    const std::string shutdown_token = "poll_shutdown";
    const std::string endpoint = "unix://" + socket_path.string();

    {
        std::ofstream node_script(script_path);
        node_script << "#!/bin/sh\n";
        node_script << "sleep 2\n";
        node_script << "exit 0\n";
    }
    std::filesystem::permissions(script_path,
                                 std::filesystem::perms::owner_read | std::filesystem::perms::owner_write
                                     | std::filesystem::perms::owner_exec,
                                 std::filesystem::perm_options::replace);

    {
        std::ofstream spec(spec_path);
        spec << "{\n";
        spec << "  \"group\": \"coord_poll\",\n";
        spec << "  \"mode\": \"A\",\n";
        spec << "  \"artifact_dir\": \"artifacts\",\n";
        spec << "  \"timeouts\": { \"startup_ms\": 2000, \"session_ms\": 5000, \"shutdown_ms\": 2000 },\n";
        spec << "  \"nodes\": [\n";
        spec << "    { \"name\": \"worker\", \"exec\": \"" << script_path.string() << "\", \"instances\": 1 }\n";
        spec << "  ]\n";
        spec << "}\n";
    }

    std::string spawn_error;
    pid_t coordd_pid = spawn_process(
        {COORDD_BIN_PATH, "--listen", endpoint, "--root", root_dir.string(), "--ready-file", ready_file.string(), "--shutdown-token", shutdown_token},
        spawn_error);
    ASSERT_TRUE(coordd_pid > 0, spawn_error);
    ChildProcessGuard coordd_guard{coordd_pid};

    ASSERT_TRUE(wait_for_file(ready_file, 5000), "coordd did not become ready");

    auto submit =
        run_exec_capture({COORDCTL_BIN_PATH, "submit", "--spec", spec_path.string(), "--connect", endpoint, "--no-wait"});
    ASSERT_EQ(submit.exit_code, 0, submit.stderr_text);
    auto session_id = trim_copy(submit.stdout_text);
    ASSERT_FALSE(session_id.empty(), "submit returned empty session id");

    bool saw_incomplete = false;
    auto observe_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
    while (std::chrono::steady_clock::now() < observe_deadline) {
        auto status = run_exec_capture({COORDCTL_BIN_PATH, "status", "--session", session_id, "--connect", endpoint});
        EXPECT_EQ(status.exit_code, 0, status.stderr_text);
        if (status.stdout_text.find("complete=0") != std::string::npos) {
            saw_incomplete = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ASSERT_TRUE(saw_incomplete, "status never reported complete=0 before wait");

    std::atomic<bool> wait_done{false};
    ExecResult wait_result{};
    std::thread waiter([&]() {
        wait_result = run_exec_capture({COORDCTL_BIN_PATH, "wait", "--session", session_id, "--connect", endpoint});
        wait_done.store(true);
    });

    bool polled_after_waiter = false;
    auto poll_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(4000);
    while (!wait_done.load() && std::chrono::steady_clock::now() < poll_deadline) {
        polled_after_waiter = true;
        auto status = run_exec_capture({COORDCTL_BIN_PATH, "status", "--session", session_id, "--connect", endpoint});
        EXPECT_EQ(status.exit_code, 0, status.stderr_text);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (waiter.joinable()) {
        waiter.join();
    }

    EXPECT_TRUE(polled_after_waiter);
    EXPECT_EQ(wait_result.exit_code, 0, wait_result.stderr_text + wait_result.stdout_text);

    auto shutdown =
        run_exec_capture({COORDCTL_BIN_PATH, "shutdown", "--connect", endpoint, "--token", shutdown_token});
    EXPECT_EQ(shutdown.exit_code, 0, shutdown.stderr_text);
    EXPECT_TRUE(wait_for_child_exit(coordd_pid, 5000), "coordd did not exit after shutdown");
    coordd_guard.pid = -1;
    std::filesystem::remove_all(base_dir);
}
#endif

#if COORD_ENABLE_JSON
[[using gentest: test("coord/json_parse")]]
void json_parse() {
    const auto tmp = std::filesystem::temp_directory_path() / "coord_spec.json";
    std::ofstream out(tmp);
    out << R"JSON({
  "group": "coord_test",
  "mode": "A",
  "artifact_dir": "artifacts",
  "timeouts": { "startup_ms": 1000, "session_ms": 2000, "shutdown_ms": 3000 },
  "network": { "isolated": false, "ports": [ { "name": "udp_server", "count": 1, "protocol": "udp" } ] },
  "nodes": [
    { "name": "server", "exec": "server", "instances": 1, "readiness": { "type": "stdout", "value": "READY" } },
    { "name": "client", "exec": "client", "instances": 2 }
  ]
})JSON";
    out.close();

    coord::SessionSpec spec{};
    std::string error;
    ASSERT_TRUE(coord::load_session_spec_json(tmp.string(), spec, &error), error);

    EXPECT_EQ(spec.group, "coord_test");
    EXPECT_EQ(spec.mode, coord::ExecMode::A);
    EXPECT_EQ(spec.nodes.size(), std::size_t{2});
    EXPECT_EQ(spec.network.ports.size(), std::size_t{1});
    EXPECT_EQ(spec.network.ports[0].protocol, coord::Protocol::Udp);

    std::filesystem::remove(tmp);
}

[[using gentest: test("coord/json_errors")]]
void json_errors() {
    const auto base = std::filesystem::temp_directory_path();
    std::string error;

    {
        auto path = base / "coord_invalid_mode.json";
        std::ofstream out(path);
        out << R"JSON({ "group": "g", "mode": "Z", "nodes": [] })JSON";
        out.close();
        coord::SessionSpec spec{};
        EXPECT_FALSE(coord::load_session_spec_json(path.string(), spec, &error));
        EXPECT_EQ(error, "invalid mode");
        std::filesystem::remove(path);
    }

    {
        auto path = base / "coord_missing_nodes.json";
        std::ofstream out(path);
        out << R"JSON({ "group": "g", "mode": "A" })JSON";
        out.close();
        coord::SessionSpec spec{};
        error.clear();
        EXPECT_FALSE(coord::load_session_spec_json(path.string(), spec, &error));
        EXPECT_EQ(error, "spec missing nodes");
        std::filesystem::remove(path);
    }

    {
        auto path = base / "coord_bad_protocol.json";
        std::ofstream out(path);
        out << R"JSON({
  "group": "g",
  "mode": "A",
  "network": { "ports": [ { "name": "p", "protocol": "icmp" } ] },
  "nodes": [ { "name": "n", "exec": "x" } ]
})JSON";
        out.close();
        coord::SessionSpec spec{};
        error.clear();
        EXPECT_FALSE(coord::load_session_spec_json(path.string(), spec, &error));
        EXPECT_EQ(error, "invalid protocol");
        std::filesystem::remove(path);
    }

    {
        auto path = base / "coord_bad_readiness.json";
        std::ofstream out(path);
        out << R"JSON({
  "group": "g",
  "mode": "A",
  "nodes": [ { "name": "n", "exec": "x", "readiness": { "type": "bogus" } } ]
})JSON";
        out.close();
        coord::SessionSpec spec{};
        error.clear();
        EXPECT_FALSE(coord::load_session_spec_json(path.string(), spec, &error));
        EXPECT_EQ(error, "invalid readiness");
        std::filesystem::remove(path);
    }
}

[[using gentest: test("coord/manifest_write")]]
void manifest_write() {
    coord::SessionManifest manifest{};
    manifest.session_id = "manifest_session";
    manifest.group = "group";
    manifest.mode = coord::ExecMode::A;
    manifest.result = coord::ResultCode::Success;
    manifest.start_ms = 100;
    manifest.end_ms = 200;
    manifest.diagnostics = {"note"};

    coord::InstanceInfo info{};
    info.node = "node";
    info.index = 1;
    info.pid = 123;
    info.exit_code = 0;
    info.term_signal = 0;
    info.log_path = "stdout.log";
    info.err_path = "stderr.log";
    info.addr = "127.0.0.1";
    coord::PortAssignment pa{};
    pa.name = "svc";
    pa.protocol = coord::Protocol::Udp;
    pa.ports = {1111};
    info.ports.push_back(pa);
    manifest.instances.push_back(info);

    auto path = std::filesystem::temp_directory_path() / "coord_manifest.json";
    std::string error;
    ASSERT_TRUE(coord::write_manifest_json(manifest, path.string(), &error), error);

    std::ifstream in(path);
    nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    ASSERT_FALSE(j.is_discarded());
    EXPECT_EQ(j["session_id"], "manifest_session");
    EXPECT_EQ(j["group"], "group");
    EXPECT_EQ(j["instances"].size(), std::size_t{1});
    EXPECT_EQ(j["instances"][0]["node"], "node");
    EXPECT_EQ(j["instances"][0]["ports"][0]["protocol"], "udp");

    std::filesystem::remove(path);
}
#endif

} // namespace coord_tests
