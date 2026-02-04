#include "coord/codec.h"
#include "coord/json.h"
#include "coord/types.h"
#include "gentest/attributes.h"
#include "gentest/runner.h"

#include <filesystem>
#include <fstream>

using namespace gentest::asserts;

namespace coord_tests {

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
#endif

} // namespace coord_tests
