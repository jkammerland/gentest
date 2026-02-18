#include "coord/codec.h"
#include "coord/json.h"
#include "coord/transport.h"
#include "coord/types.h"
#include "gentest/attributes.h"
#include "gentest/runner.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <unistd.h>
#endif

#if COORD_ENABLE_JSON
#include <boost/json.hpp>
#endif

using namespace gentest::asserts;

namespace coord_tests {

#ifndef _WIN32
static std::filesystem::path make_socket_path(const char *tag) {
    auto base = std::filesystem::temp_directory_path();
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto pid = static_cast<unsigned long>(::getpid());
    std::string name = "coord_" + std::to_string(pid) + "_" + std::to_string(stamp) + "_" + std::string(tag) + ".sock";
    return base / name;
}
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

#ifndef _WIN32
[[using gentest: test("coord/transport_frame_roundtrip")]]
void transport_frame_roundtrip() {
    auto path = make_socket_path("roundtrip");
    coord::Endpoint ep{};
    ep.kind = coord::Endpoint::Kind::Unix;
    ep.path = path.string();

    std::string error;
    int listener = coord::listen_endpoint(ep, &error);
    ASSERT_TRUE(listener >= 0, error);

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
    ::close(listener);
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
    int listener = coord::listen_endpoint(ep, &error);
    ASSERT_TRUE(listener >= 0, error);

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
        auto len_rc = ::write(client.fd(), &len_be, sizeof(len_be));
        client_ok = (len_rc > 0);
    }
    if (client_ok) {
        std::array<std::byte, 4> partial{std::byte{0xaa}, std::byte{0xbb}, std::byte{0xcc}, std::byte{0xdd}};
        auto data_rc = ::write(client.fd(), partial.data(), partial.size());
        client_ok = (data_rc > 0);
    }
    client = coord::Connection{};

    if (server.joinable()) {
        server.join();
    }
    ::close(listener);
    std::filesystem::remove(path);

    EXPECT_TRUE(client_ok, error);
    EXPECT_FALSE(server_read_ok);
    EXPECT_FALSE(server_error.empty());
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
    ASSERT_TRUE(in.good(), "failed to read manifest");
    std::string json_text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    boost::json::error_code parse_error;
    boost::json::value parsed = boost::json::parse(json_text, parse_error);
    ASSERT_FALSE(static_cast<bool>(parse_error), parse_error.message().c_str());

    const boost::json::object *j = parsed.if_object();
    ASSERT_TRUE(j != nullptr, "manifest root is not an object");

    auto as_string = [](const boost::json::value &value) {
        auto sv = value.as_string();
        return std::string(sv.data(), sv.size());
    };

    EXPECT_EQ(as_string(j->at("session_id")), "manifest_session");
    EXPECT_EQ(as_string(j->at("group")), "group");
    const auto &instances = j->at("instances").as_array();
    EXPECT_EQ(instances.size(), std::size_t{1});
    const auto &instance = instances[0].as_object();
    EXPECT_EQ(as_string(instance.at("node")), "node");
    const auto &ports = instance.at("ports").as_array();
    EXPECT_EQ(as_string(ports[0].as_object().at("protocol")), "udp");

    std::filesystem::remove(path);
}
#endif

} // namespace coord_tests
