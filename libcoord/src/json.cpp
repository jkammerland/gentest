#include "coord/json.h"

#include <boost/json.hpp>

#include <fstream>
#include <iterator>
#include <limits>
#include <string_view>

namespace coord {

namespace {

using JsonObject = boost::json::object;
using JsonValue = boost::json::value;

static std::string to_std_string(boost::json::string_view value) {
    return std::string(value.data(), value.size());
}

static std::string get_string_or(const JsonObject &obj, std::string_view key, std::string_view fallback = {}) {
    if (const JsonValue *value = obj.if_contains(key)) {
        if (const boost::json::string *str = value->if_string()) {
            return to_std_string(*str);
        }
    }
    return std::string(fallback);
}

static bool get_bool_or(const JsonObject &obj, std::string_view key, bool fallback) {
    if (const JsonValue *value = obj.if_contains(key)) {
        if (value->is_bool()) {
            return value->as_bool();
        }
    }
    return fallback;
}

static std::uint32_t get_uint32_or(const JsonObject &obj, std::string_view key, std::uint32_t fallback) {
    constexpr std::uint64_t max_u32 = std::numeric_limits<std::uint32_t>::max();
    if (const JsonValue *value = obj.if_contains(key)) {
        if (const std::uint64_t *u = value->if_uint64()) {
            if (*u <= max_u32) {
                return static_cast<std::uint32_t>(*u);
            }
            return fallback;
        }
        if (const std::int64_t *i = value->if_int64()) {
            if (*i >= 0 && static_cast<std::uint64_t>(*i) <= max_u32) {
                return static_cast<std::uint32_t>(*i);
            }
        }
    }
    return fallback;
}

static const JsonObject *get_object_or_null(const JsonObject &obj, std::string_view key) {
    if (const JsonValue *value = obj.if_contains(key)) {
        return value->if_object();
    }
    return nullptr;
}

static const boost::json::array *get_array_or_null(const JsonObject &obj, std::string_view key) {
    if (const JsonValue *value = obj.if_contains(key)) {
        return value->if_array();
    }
    return nullptr;
}

} // namespace

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

    std::string json_text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    boost::json::error_code parse_error;
    JsonValue parsed = boost::json::parse(json_text, parse_error);
    if (parse_error) {
        if (error) *error = "invalid JSON";
        return false;
    }
    const JsonObject *root = parsed.if_object();
    if (!root) {
        if (error) *error = "invalid JSON";
        return false;
    }

    SessionSpec spec{};
    spec.session_id = get_string_or(*root, "session_id", "");
    spec.group = get_string_or(*root, "group", "");
    std::string mode_str = get_string_or(*root, "mode", "A");
    if (!parse_exec_mode(mode_str, spec.mode)) {
        if (error) *error = "invalid mode";
        return false;
    }
    spec.artifact_dir = get_string_or(*root, "artifact_dir", "");

    if (const JsonObject *jt = get_object_or_null(*root, "timeouts")) {
        spec.timeouts.startup_ms = get_uint32_or(*jt, "startup_ms", spec.timeouts.startup_ms);
        spec.timeouts.session_ms = get_uint32_or(*jt, "session_ms", spec.timeouts.session_ms);
        spec.timeouts.shutdown_ms = get_uint32_or(*jt, "shutdown_ms", spec.timeouts.shutdown_ms);
    }

    if (const JsonObject *placement = get_object_or_null(*root, "placement")) {
        spec.placement.target = get_string_or(*placement, "target", "");
    }

    if (const JsonObject *network = get_object_or_null(*root, "network")) {
        spec.network.isolated = get_bool_or(*network, "isolated", spec.network.isolated);
        spec.network.bridge = get_string_or(*network, "bridge", "");
        if (const boost::json::array *ports = get_array_or_null(*network, "ports")) {
            for (const JsonValue &port_value : *ports) {
                const JsonObject *jp = port_value.if_object();
                if (!jp) {
                    continue;
                }
                PortRequest pr{};
                pr.name = get_string_or(*jp, "name", "");
                pr.count = get_uint32_or(*jp, "count", pr.count);
                std::string proto = get_string_or(*jp, "protocol", "tcp");
                if (!parse_protocol(proto, pr.protocol)) {
                    if (error) *error = "invalid protocol";
                    return false;
                }
                spec.network.ports.push_back(std::move(pr));
            }
        }
    }

    const boost::json::array *nodes = get_array_or_null(*root, "nodes");
    if (!nodes) {
        if (error) *error = "spec missing nodes";
        return false;
    }
    for (const JsonValue &node_value : *nodes) {
        const JsonObject *jn = node_value.if_object();
        if (!jn) {
            if (error) *error = "invalid node";
            return false;
        }
        NodeDef node{};
        node.name = get_string_or(*jn, "name", "");
        node.exec = get_string_or(*jn, "exec", "");
        node.cwd = get_string_or(*jn, "cwd", "");
        node.instances = get_uint32_or(*jn, "instances", node.instances);
        if (const boost::json::array *args = get_array_or_null(*jn, "args")) {
            node.args.reserve(args->size());
            for (const JsonValue &arg : *args) {
                if (const boost::json::string *arg_str = arg.if_string()) {
                    node.args.push_back(to_std_string(*arg_str));
                }
            }
        }
        if (const boost::json::array *env = get_array_or_null(*jn, "env")) {
            node.env.reserve(env->size());
            for (const JsonValue &env_value : *env) {
                const JsonObject *je = env_value.if_object();
                if (!je) {
                    continue;
                }
                EnvVar ev{};
                ev.key = get_string_or(*je, "key", "");
                ev.value = get_string_or(*je, "value", "");
                node.env.push_back(std::move(ev));
            }
        }
        if (const JsonObject *jr = get_object_or_null(*jn, "readiness")) {
            std::string kind = get_string_or(*jr, "type", "none");
            if (!parse_readiness(kind, node.readiness.kind)) {
                if (error) *error = "invalid readiness";
                return false;
            }
            node.readiness.value = get_string_or(*jr, "value", "");
        }
        spec.nodes.push_back(std::move(node));
    }

    out = std::move(spec);
    return true;
}

bool write_manifest_json(const SessionManifest &manifest, const std::string &path, std::string *error) {
    JsonObject root;
    root["session_id"] = manifest.session_id;
    root["group"] = manifest.group;
    root["mode"] = exec_mode_to_string(manifest.mode);
    root["result"] = static_cast<std::int32_t>(manifest.result);
    root["fail_reason"] = manifest.fail_reason;
    root["start_ms"] = manifest.start_ms;
    root["end_ms"] = manifest.end_ms;
    boost::json::array diagnostics;
    diagnostics.reserve(manifest.diagnostics.size());
    for (const std::string &entry : manifest.diagnostics) {
        diagnostics.emplace_back(entry);
    }
    root["diagnostics"] = std::move(diagnostics);

    boost::json::array instances;
    instances.reserve(manifest.instances.size());
    for (const auto &info : manifest.instances) {
        JsonObject ji;
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
        boost::json::array ports;
        ports.reserve(info.ports.size());
        for (const auto &pa : info.ports) {
            JsonObject jp;
            jp["name"] = pa.name;
            jp["protocol"] = protocol_to_string(pa.protocol);
            boost::json::array port_values;
            port_values.reserve(pa.ports.size());
            for (std::uint16_t port : pa.ports) {
                port_values.emplace_back(port);
            }
            jp["ports"] = std::move(port_values);
            ports.push_back(std::move(jp));
        }
        ji["ports"] = std::move(ports);
        instances.push_back(std::move(ji));
    }
    root["instances"] = std::move(instances);

    std::ofstream out(path);
    if (!out) {
        if (error) *error = "failed to open manifest output";
        return false;
    }
    out << boost::json::serialize(root);
    return true;
}

} // namespace coord
