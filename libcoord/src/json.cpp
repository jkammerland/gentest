#include "coord/json.h"

#include <fstream>

#include <nlohmann/json.hpp>

namespace coord {

using json = nlohmann::json;

static std::string exec_mode_to_string(ExecMode mode) {
    switch (mode) {
    case ExecMode::A: return "A";
    case ExecMode::B: return "B";
    case ExecMode::C: return "C";
    case ExecMode::D: return "D";
    }
    return "A";
}

static bool parse_exec_mode(const std::string &value, ExecMode &out) {
    if (value == "A" || value == "a") { out = ExecMode::A; return true; }
    if (value == "B" || value == "b") { out = ExecMode::B; return true; }
    if (value == "C" || value == "c") { out = ExecMode::C; return true; }
    if (value == "D" || value == "d") { out = ExecMode::D; return true; }
    return false;
}

static std::string readiness_to_string(ReadinessKind kind) {
    switch (kind) {
    case ReadinessKind::None: return "none";
    case ReadinessKind::StdoutToken: return "stdout";
    case ReadinessKind::Socket: return "socket";
    case ReadinessKind::File: return "file";
    }
    return "none";
}

static bool parse_readiness(const std::string &value, ReadinessKind &out) {
    if (value == "none") { out = ReadinessKind::None; return true; }
    if (value == "stdout") { out = ReadinessKind::StdoutToken; return true; }
    if (value == "socket") { out = ReadinessKind::Socket; return true; }
    if (value == "file") { out = ReadinessKind::File; return true; }
    return false;
}

static std::string protocol_to_string(Protocol p) {
    return p == Protocol::Udp ? "udp" : "tcp";
}

static bool parse_protocol(const std::string &value, Protocol &out) {
    if (value == "udp") { out = Protocol::Udp; return true; }
    if (value == "tcp") { out = Protocol::Tcp; return true; }
    return false;
}

bool load_session_spec_json(const std::string &path, SessionSpec &out, std::string *error) {
    std::ifstream file(path);
    if (!file) {
        if (error) *error = "failed to open spec file";
        return false;
    }
    json j = json::parse(file, nullptr, false);
    if (j.is_discarded()) {
        if (error) *error = "invalid JSON";
        return false;
    }

    SessionSpec spec{};
    spec.session_id = j.value("session_id", "");
    spec.group = j.value("group", "");
    std::string mode_str = j.value("mode", "A");
    if (!parse_exec_mode(mode_str, spec.mode)) {
        if (error) *error = "invalid mode";
        return false;
    }
    spec.artifact_dir = j.value("artifact_dir", "");

    if (j.contains("timeouts")) {
        auto jt = j["timeouts"];
        spec.timeouts.startup_ms = jt.value("startup_ms", spec.timeouts.startup_ms);
        spec.timeouts.session_ms = jt.value("session_ms", spec.timeouts.session_ms);
        spec.timeouts.shutdown_ms = jt.value("shutdown_ms", spec.timeouts.shutdown_ms);
    }

    if (j.contains("placement")) {
        spec.placement.target = j["placement"].value("target", "");
    }

    if (j.contains("network")) {
        auto jn = j["network"];
        spec.network.isolated = jn.value("isolated", spec.network.isolated);
        spec.network.bridge = jn.value("bridge", "");
        if (jn.contains("ports")) {
            for (auto &jp : jn["ports"]) {
                PortRequest pr{};
                pr.name = jp.value("name", "");
                pr.count = jp.value("count", pr.count);
                std::string proto = jp.value("protocol", "tcp");
                if (!parse_protocol(proto, pr.protocol)) {
                    if (error) *error = "invalid protocol";
                    return false;
                }
                spec.network.ports.push_back(std::move(pr));
            }
        }
    }

    if (!j.contains("nodes") || !j["nodes"].is_array()) {
        if (error) *error = "spec missing nodes";
        return false;
    }
    for (auto &jn : j["nodes"]) {
        NodeDef node{};
        node.name = jn.value("name", "");
        node.exec = jn.value("exec", "");
        node.cwd = jn.value("cwd", "");
        node.instances = jn.value("instances", node.instances);
        if (jn.contains("args")) {
            node.args = jn["args"].get<std::vector<std::string>>();
        }
        if (jn.contains("env")) {
            for (auto &je : jn["env"]) {
                EnvVar ev{};
                ev.key = je.value("key", "");
                ev.value = je.value("value", "");
                node.env.push_back(std::move(ev));
            }
        }
        if (jn.contains("readiness")) {
            auto jr = jn["readiness"];
            std::string kind = jr.value("type", "none");
            if (!parse_readiness(kind, node.readiness.kind)) {
                if (error) *error = "invalid readiness";
                return false;
            }
            node.readiness.value = jr.value("value", "");
        }
        spec.nodes.push_back(std::move(node));
    }

    out = std::move(spec);
    return true;
}

bool write_manifest_json(const SessionManifest &manifest, const std::string &path, std::string *error) {
    json j;
    j["session_id"] = manifest.session_id;
    j["group"] = manifest.group;
    j["mode"] = exec_mode_to_string(manifest.mode);
    j["result"] = static_cast<int>(manifest.result);
    j["fail_reason"] = manifest.fail_reason;
    j["start_ms"] = manifest.start_ms;
    j["end_ms"] = manifest.end_ms;
    j["diagnostics"] = manifest.diagnostics;

    json inst = json::array();
    for (const auto &info : manifest.instances) {
        json ji;
        ji["node"] = info.node;
        ji["index"] = info.index;
        ji["pid"] = info.pid;
        ji["exit_code"] = info.exit_code;
        ji["term_signal"] = info.term_signal;
        ji["log_path"] = info.log_path;
        ji["err_path"] = info.err_path;
        ji["addr"] = info.addr;
        ji["start_ms"] = info.start_ms;
        ji["end_ms"] = info.end_ms;
        ji["failure_reason"] = info.failure_reason;
        json ports = json::array();
        for (const auto &pa : info.ports) {
            json jp;
            jp["name"] = pa.name;
            jp["protocol"] = protocol_to_string(pa.protocol);
            jp["ports"] = pa.ports;
            ports.push_back(std::move(jp));
        }
        ji["ports"] = std::move(ports);
        inst.push_back(std::move(ji));
    }
    j["instances"] = std::move(inst);

    std::ofstream out(path);
    if (!out) {
        if (error) *error = "failed to open manifest output";
        return false;
    }
    out << j.dump(2);
    return true;
}

} // namespace coord
