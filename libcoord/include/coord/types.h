#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace coord {

enum class ExecMode : std::uint8_t { A = 0, B = 1, C = 2, D = 3 };

enum class ResultCode : std::uint8_t { Success = 0, Failed = 1, Timeout = 2, Cancelled = 3, Error = 4 };

enum class ReadinessKind : std::uint8_t { None = 0, StdoutToken = 1, Socket = 2, File = 3 };

enum class Protocol : std::uint8_t { Tcp = 0, Udp = 1 };

struct EnvVar {
    static constexpr std::uint64_t cbor_tag = 3010;
    std::string key;
    std::string value;
};

struct ReadinessSpec {
    static constexpr std::uint64_t cbor_tag = 3011;
    ReadinessKind kind{ReadinessKind::None};
    std::string value;
};

struct NodeDef {
    static constexpr std::uint64_t cbor_tag = 3012;
    std::string name;
    std::string exec;
    std::vector<std::string> args;
    std::vector<EnvVar> env;
    std::string cwd;
    std::uint32_t instances{1};
    ReadinessSpec readiness{};
};

struct PortRequest {
    static constexpr std::uint64_t cbor_tag = 3013;
    std::string name;
    std::uint32_t count{1};
    Protocol protocol{Protocol::Tcp};
};

struct NetworkSpec {
    static constexpr std::uint64_t cbor_tag = 3014;
    bool isolated{false};
    std::string bridge;
    std::vector<PortRequest> ports;
};

struct Timeouts {
    static constexpr std::uint64_t cbor_tag = 3015;
    std::uint32_t startup_ms{30000};
    std::uint32_t session_ms{300000};
    std::uint32_t shutdown_ms{5000};
};

struct Placement {
    static constexpr std::uint64_t cbor_tag = 3016;
    std::string target; // "local" or "peer:<addr:port>"
};

struct SessionSpec {
    static constexpr std::uint64_t cbor_tag = 3001;
    std::string session_id;
    std::string group;
    ExecMode mode{ExecMode::A};
    std::vector<NodeDef> nodes;
    NetworkSpec network{};
    Timeouts timeouts{};
    std::string artifact_dir;
    Placement placement{};
};

struct PortAssignment {
    static constexpr std::uint64_t cbor_tag = 3017;
    std::string name;
    Protocol protocol{Protocol::Tcp};
    std::vector<std::uint16_t> ports;
};

struct InstanceInfo {
    static constexpr std::uint64_t cbor_tag = 3018;
    std::string node;
    std::uint32_t index{0};
    std::int64_t pid{-1};
    std::int32_t exit_code{0};
    std::int32_t term_signal{0};
    std::string log_path;
    std::string err_path;
    std::string addr;
    std::vector<PortAssignment> ports;
    std::uint64_t start_ms{0};
    std::uint64_t end_ms{0};
    std::string failure_reason;
};

struct SessionManifest {
    static constexpr std::uint64_t cbor_tag = 3002;
    std::string session_id;
    std::string group;
    ExecMode mode{ExecMode::A};
    ResultCode result{ResultCode::Error};
    std::string fail_reason;
    std::vector<InstanceInfo> instances;
    std::uint64_t start_ms{0};
    std::uint64_t end_ms{0};
    std::vector<std::string> diagnostics;
};

struct SessionStatus {
    static constexpr std::uint64_t cbor_tag = 3003;
    std::string session_id;
    ResultCode result{ResultCode::Error};
    bool complete{false};
};

struct MsgSessionSubmit {
    static constexpr std::uint64_t cbor_tag = 4001;
    SessionSpec spec;
};

struct MsgSessionAccepted {
    static constexpr std::uint64_t cbor_tag = 4002;
    std::string session_id;
};

struct MsgSessionWait {
    static constexpr std::uint64_t cbor_tag = 4003;
    std::string session_id;
};

struct MsgSessionManifest {
    static constexpr std::uint64_t cbor_tag = 4004;
    SessionManifest manifest;
};

struct MsgSessionStatus {
    static constexpr std::uint64_t cbor_tag = 4005;
    SessionStatus status;
};

struct MsgSessionStatusRequest {
    static constexpr std::uint64_t cbor_tag = 4008;
    std::string session_id;
};

struct MsgShutdown {
    static constexpr std::uint64_t cbor_tag = 4006;
    std::string token;
};

struct MsgError {
    static constexpr std::uint64_t cbor_tag = 4007;
    std::string message;
};

using MessagePayload = std::variant<MsgSessionSubmit,
                                   MsgSessionAccepted,
                                   MsgSessionWait,
                                   MsgSessionManifest,
                                   MsgSessionStatus,
                                   MsgSessionStatusRequest,
                                   MsgShutdown,
                                   MsgError>;

struct Message {
    std::uint32_t version{1};
    MessagePayload payload{};
};

} // namespace coord
