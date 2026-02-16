#include "coord/json.h"

#include <fstream>
#include <limits>
#include <sstream>

#include <boost/json.hpp>

namespace coord {

namespace bjson = boost::json;

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

static std::string get_string_or(const bjson::object &obj, std::string_view key, const std::string &fallback) {
    if (const bjson::value *value = obj.if_contains(key)) {
        if (value->is_string()) {
            return std::string(value->as_string());
        }
    }
    return fallback;
}

static bool assign_u32_if_present(const bjson::object &obj, std::string_view key, std::uint32_t &target) {
    const bjson::value *value = obj.if_contains(key);
    if (value == nullptr) {
        return true;
    }
    if (value->is_uint64()) {
        const auto n = value->as_uint64();
        if (n > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        target = static_cast<std::uint32_t>(n);
        return true;
    }
    if (value->is_int64()) {
        const auto n = value->as_int64();
        if (n < 0 || static_cast<std::uint64_t>(n) > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        target = static_cast<std::uint32_t>(n);
        return true;
    }
    return false;
}

bool load_session_spec_json(const std::string &path, SessionSpec &out, std::string *error) {
    std::ifstream file(path);
    if (!file) {
        if (error) *error = "failed to open spec file";
        return false;
    }

    std::ostringstream input;
    input << file.rdbuf();

    boost::system::error_code ec;
    bjson::value root = bjson::parse(input.str(), ec);
    if (ec || !root.is_object()) {
        if (error) *error = "invalid JSON";
        return false;
    }
    const bjson::object &j = root.as_object();

    SessionSpec spec{};
    spec.session_id = get_string_or(j, "session_id", "");
    spec.group = get_string_or(j, "group", "");
    std::string mode_str = get_string_or(j, "mode", "A");
    if (!parse_exec_mode(mode_str, spec.mode)) {
        if (error) *error = "invalid mode";
        return false;
    }
    spec.artifact_dir = get_string_or(j, "artifact_dir", "");

    if (const bjson::value *timeouts_value = j.if_contains("timeouts")) {
        if (!timeouts_value->is_object()) {
            if (error) *error = "invalid timeouts";
            return false;
        }
        const bjson::object &jt = timeouts_value->as_object();
        if (!assign_u32_if_present(jt, "startup_ms", spec.timeouts.startup_ms)
            || !assign_u32_if_present(jt, "session_ms", spec.timeouts.session_ms)
            || !assign_u32_if_present(jt, "shutdown_ms", spec.timeouts.shutdown_ms)) {
            if (error) *error = "invalid timeouts";
            return false;
        }
    }

    if (const bjson::value *placement_value = j.if_contains("placement")) {
        if (!placement_value->is_object()) {
            if (error) *error = "invalid placement";
            return false;
        }
        const bjson::object &placement = placement_value->as_object();
        spec.placement.target = get_string_or(placement, "target", "");
    }

    if (const bjson::value *network_value = j.if_contains("network")) {
        if (!network_value->is_object()) {
            if (error) *error = "invalid network";
            return false;
        }
        const bjson::object &jn = network_value->as_object();
        if (const bjson::value *isolated_value = jn.if_contains("isolated")) {
            if (!isolated_value->is_bool()) {
                if (error) *error = "invalid network";
                return false;
            }
            spec.network.isolated = isolated_value->as_bool();
        }
        spec.network.bridge = get_string_or(jn, "bridge", "");
        if (const bjson::value *ports_value = jn.if_contains("ports")) {
            if (!ports_value->is_array()) {
                if (error) *error = "invalid network";
                return false;
            }
            for (const bjson::value &port_value : ports_value->as_array()) {
                if (!port_value.is_object()) {
                    if (error) *error = "invalid network";
                    return false;
                }
                const bjson::object &jp = port_value.as_object();
                PortRequest pr{};
                pr.name = get_string_or(jp, "name", "");
                if (!assign_u32_if_present(jp, "count", pr.count)) {
                    if (error) *error = "invalid network";
                    return false;
                }
                std::string proto = get_string_or(jp, "protocol", "tcp");
                if (!parse_protocol(proto, pr.protocol)) {
                    if (error) *error = "invalid protocol";
                    return false;
                }
                spec.network.ports.push_back(std::move(pr));
            }
        }
    }

    const bjson::value *nodes_value = j.if_contains("nodes");
    if (nodes_value == nullptr || !nodes_value->is_array()) {
        if (error) *error = "spec missing nodes";
        return false;
    }
    for (const bjson::value &node_value : nodes_value->as_array()) {
        if (!node_value.is_object()) {
            if (error) *error = "invalid node";
            return false;
        }
        const bjson::object &jn = node_value.as_object();
        NodeDef node{};
        node.name = get_string_or(jn, "name", "");
        node.exec = get_string_or(jn, "exec", "");
        node.cwd = get_string_or(jn, "cwd", "");
        if (!assign_u32_if_present(jn, "instances", node.instances)) {
            if (error) *error = "invalid node";
            return false;
        }
        if (const bjson::value *args_value = jn.if_contains("args")) {
            if (!args_value->is_array()) {
                if (error) *error = "invalid args";
                return false;
            }
            for (const bjson::value &arg : args_value->as_array()) {
                if (!arg.is_string()) {
                    if (error) *error = "invalid args";
                    return false;
                }
                node.args.emplace_back(arg.as_string());
            }
        }
        if (const bjson::value *env_value = jn.if_contains("env")) {
            if (!env_value->is_array()) {
                if (error) *error = "invalid env";
                return false;
            }
            for (const bjson::value &env_item : env_value->as_array()) {
                if (!env_item.is_object()) {
                    if (error) *error = "invalid env";
                    return false;
                }
                const bjson::object &je = env_item.as_object();
                EnvVar ev{};
                ev.key = get_string_or(je, "key", "");
                ev.value = get_string_or(je, "value", "");
                node.env.push_back(std::move(ev));
            }
        }
        if (const bjson::value *readiness_value = jn.if_contains("readiness")) {
            if (!readiness_value->is_object()) {
                if (error) *error = "invalid readiness";
                return false;
            }
            const bjson::object &jr = readiness_value->as_object();
            std::string kind = get_string_or(jr, "type", "none");
            if (!parse_readiness(kind, node.readiness.kind)) {
                if (error) *error = "invalid readiness";
                return false;
            }
            node.readiness.value = get_string_or(jr, "value", "");
        }
        spec.nodes.push_back(std::move(node));
    }

    out = std::move(spec);
    return true;
}

bool write_manifest_json(const SessionManifest &manifest, const std::string &path, std::string *error) {
    bjson::object j;
    j["session_id"] = manifest.session_id;
    j["group"] = manifest.group;
    j["mode"] = exec_mode_to_string(manifest.mode);
    j["result"] = static_cast<int>(manifest.result);
    j["fail_reason"] = manifest.fail_reason;
    j["start_ms"] = manifest.start_ms;
    j["end_ms"] = manifest.end_ms;
    bjson::array diagnostics;
    diagnostics.reserve(manifest.diagnostics.size());
    for (const auto &diag : manifest.diagnostics) {
        diagnostics.emplace_back(diag);
    }
    j["diagnostics"] = std::move(diagnostics);

    bjson::array inst;
    inst.reserve(manifest.instances.size());
    for (const auto &info : manifest.instances) {
        bjson::object ji;
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
        bjson::array ports;
        ports.reserve(info.ports.size());
        for (const auto &pa : info.ports) {
            bjson::object jp;
            jp["name"] = pa.name;
            jp["protocol"] = protocol_to_string(pa.protocol);
            bjson::array port_values;
            port_values.reserve(pa.ports.size());
            for (std::uint16_t port : pa.ports) {
                port_values.emplace_back(port);
            }
            jp["ports"] = std::move(port_values);
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
    out << bjson::serialize(j);
    return true;
}

} // namespace coord
