#include "coord/codec.h"
#include "coord/json.h"
#include "coord/transport.h"
#include "coord/types.h"

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor.h>

#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace coord;

static std::string read_file_text(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static std::vector<std::byte> read_file_bytes(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<char> chars((std::istreambuf_iterator<char>(in)), {});
    std::vector<std::byte> out(chars.size());
    std::memcpy(out.data(), chars.data(), chars.size());
    return out;
}

static bool decode_spec_cbor(const std::vector<std::byte> &data, SessionSpec &out, std::string &error) {
    std::vector<std::byte> buffer = data;
    auto dec = cbor::tags::make_decoder(buffer);
    auto result = dec(out);
    if (!result) {
        error = std::string(cbor::tags::status_message(result.error()));
        return false;
    }
    return true;
}

static std::string xml_escape(const std::string &input) {
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

static bool write_junit(const SessionManifest &manifest, const std::string &path, const std::string &manifest_ref, std::string &error) {
    std::ofstream out(path);
    if (!out) {
        error = "failed to open junit output";
        return false;
    }
    std::size_t failures = 0;
    for (const auto &inst : manifest.instances) {
        if (inst.exit_code != 0 || inst.term_signal != 0 || !inst.failure_reason.empty()) {
            failures++;
        }
    }
    double total_time = 0.0;
    if (manifest.end_ms > manifest.start_ms) {
        total_time = (manifest.end_ms - manifest.start_ms) / 1000.0;
    }
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<testsuite name=\"coordd\" tests=\"" << manifest.instances.size() << "\" failures=\"" << failures
        << "\" time=\"" << total_time << "\">\n";

    for (const auto &inst : manifest.instances) {
        std::string name = inst.node + "/" + std::to_string(inst.index);
        double time = 0.0;
        if (inst.end_ms > inst.start_ms) {
            time = (inst.end_ms - inst.start_ms) / 1000.0;
        }
        out << "  <testcase classname=\"" << xml_escape(manifest.group) << "\" name=\"" << xml_escape(name)
            << "\" time=\"" << time << "\">\n";
        if (inst.exit_code != 0 || inst.term_signal != 0 || !inst.failure_reason.empty()) {
            std::string msg = inst.failure_reason;
            if (msg.empty()) {
                if (inst.term_signal != 0) {
                    msg = "terminated by signal " + std::to_string(inst.term_signal);
                } else {
                    msg = "exit code " + std::to_string(inst.exit_code);
                }
            }
            out << "    <failure message=\"" << xml_escape(msg) << "\"/>\n";
        }
        std::string stdout_content = read_file_text(inst.log_path);
        std::string stderr_content = read_file_text(inst.err_path);
        if (!manifest_ref.empty()) {
            stdout_content = std::string("Manifest: ") + manifest_ref + "\n" + stdout_content;
        }
        out << "    <system-out>" << xml_escape(stdout_content) << "</system-out>\n";
        out << "    <system-err>" << xml_escape(stderr_content) << "</system-err>\n";
        out << "  </testcase>\n";
    }
    out << "</testsuite>\n";
    return true;
}

static bool wait_for_ready_file(const std::string &path, std::uint32_t timeout_ms) {
    if (path.empty()) {
        return true;
    }
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (fs::exists(path)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return fs::exists(path);
}

struct Args {
    std::string command;
    std::string spec_path;
    std::string session_id;
    std::string connect;
    std::string report;
    std::string shutdown_token;
    bool wait{true};
    TlsConfig tls;
    std::string coordd_path;
    std::string listen;
    std::string root;
    std::string ready_file;
    std::string pid_file;
    std::uint32_t ready_timeout_ms{5000};
};

static std::vector<std::string> build_coordd_args(const Args &args) {
    std::vector<std::string> out;
    out.push_back(args.coordd_path);
    out.push_back("--listen");
    out.push_back(args.listen);
    out.push_back("--root");
    out.push_back(args.root);
    if (!args.ready_file.empty()) {
        out.push_back("--ready-file");
        out.push_back(args.ready_file);
    }
    if (!args.pid_file.empty()) {
        out.push_back("--pid-file");
        out.push_back(args.pid_file);
    }
    if (!args.shutdown_token.empty()) {
        out.push_back("--shutdown-token");
        out.push_back(args.shutdown_token);
    }
    if (args.tls.enabled) {
        if (!args.tls.ca_file.empty()) {
            out.push_back("--tls-ca");
            out.push_back(args.tls.ca_file);
        }
        if (!args.tls.cert_file.empty()) {
            out.push_back("--tls-cert");
            out.push_back(args.tls.cert_file);
        }
        if (!args.tls.key_file.empty()) {
            out.push_back("--tls-key");
            out.push_back(args.tls.key_file);
        }
    }
    return out;
}

#ifdef _WIN32
static std::string quote_windows_arg(const std::string &arg) {
    if (arg.empty()) {
        return "\"\"";
    }
    bool need_quotes = arg.find_first_of(" \t\"") != std::string::npos;
    if (!need_quotes) {
        return arg;
    }
    std::string out = "\"";
    for (char c : arg) {
        if (c == '"') {
            out += "\\\"";
        } else {
            out += c;
        }
    }
    out += "\"";
    return out;
}
#endif

static bool spawn_coordd(const Args &args, std::string &error) {
    auto coordd_args = build_coordd_args(args);
#ifdef _WIN32
    std::string cmdline;
    for (std::size_t i = 0; i < coordd_args.size(); ++i) {
        if (i > 0) cmdline += " ";
        cmdline += quote_windows_arg(coordd_args[i]);
    }
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    DWORD flags = DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;
    if (!CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, FALSE, flags, nullptr, nullptr, &si, &pi)) {
        error = "CreateProcess failed";
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
#else
    pid_t pid = fork();
    if (pid < 0) {
        error = "fork failed";
        return false;
    }
    if (pid == 0) {
        if (setsid() < 0) {
            // Keep going; daemon will still run even without a new session.
        }
        int null_fd = ::open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            ::dup2(null_fd, STDIN_FILENO);
            ::dup2(null_fd, STDOUT_FILENO);
            ::dup2(null_fd, STDERR_FILENO);
            if (null_fd > STDERR_FILENO) {
                ::close(null_fd);
            }
        }
        std::vector<char *> argv;
        argv.reserve(coordd_args.size() + 1);
        for (auto &arg : coordd_args) {
            argv.push_back(const_cast<char *>(arg.c_str()));
        }
        argv.push_back(nullptr);
        ::execv(coordd_args[0].c_str(), argv.data());
        std::perror("execv");
        _exit(127);
    }
#endif
    if (!wait_for_ready_file(args.ready_file, args.ready_timeout_ms)) {
        error = "ready file did not appear";
        return false;
    }
    return true;
}

static void usage() {
    std::cout << "coordctl submit --spec <file> [--connect <endpoint>] [--report <path>] [--no-wait] [--tls-ca <ca> --tls-cert <cert> --tls-key <key>]\n";
    std::cout << "coordctl wait --session <id> [--connect <endpoint>]\n";
    std::cout << "coordctl status --session <id> [--connect <endpoint>]\n";
    std::cout << "coordctl shutdown [--connect <endpoint>] [--token <t>]\n";
    std::cout << "coordctl daemonize --coordd <path> --listen <endpoint> --root <dir> [--ready-file <path>] [--pid-file <path>] [--shutdown-token <t>] [--ready-timeout-ms <ms>] [--tls-ca <ca> --tls-cert <cert> --tls-key <key>]\n";
}

static bool parse_args(int argc, char **argv, Args &args) {
    if (argc < 2) {
        usage();
        return false;
    }
    args.command = argv[1];
#ifndef _WIN32
    args.connect = "unix://coordd.sock";
#else
    args.connect = "tcp://127.0.0.1:7777";
#endif
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--spec" && i + 1 < argc) {
            args.spec_path = argv[++i];
        } else if (arg == "--session" && i + 1 < argc) {
            args.session_id = argv[++i];
        } else if (arg == "--connect" && i + 1 < argc) {
            args.connect = argv[++i];
        } else if (arg == "--coordd" && i + 1 < argc) {
            args.coordd_path = argv[++i];
        } else if (arg == "--listen" && i + 1 < argc) {
            args.listen = argv[++i];
        } else if (arg == "--root" && i + 1 < argc) {
            args.root = argv[++i];
        } else if (arg == "--ready-file" && i + 1 < argc) {
            args.ready_file = argv[++i];
        } else if (arg == "--pid-file" && i + 1 < argc) {
            args.pid_file = argv[++i];
        } else if (arg == "--ready-timeout-ms" && i + 1 < argc) {
            args.ready_timeout_ms = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--report" && i + 1 < argc) {
            args.report = argv[++i];
        } else if (arg == "--no-wait") {
            args.wait = false;
        } else if (arg == "--token" && i + 1 < argc) {
            args.shutdown_token = argv[++i];
        } else if (arg == "--shutdown-token" && i + 1 < argc) {
            args.shutdown_token = argv[++i];
        } else if (arg == "--tls-ca" && i + 1 < argc) {
            args.tls.ca_file = argv[++i];
            args.tls.enabled = true;
        } else if (arg == "--tls-cert" && i + 1 < argc) {
            args.tls.cert_file = argv[++i];
            args.tls.enabled = true;
        } else if (arg == "--tls-key" && i + 1 < argc) {
            args.tls.key_file = argv[++i];
            args.tls.enabled = true;
        } else if (arg == "--help") {
            usage();
            return false;
        }
    }
    return true;
}

static bool send_message(Connection &conn, const Message &msg, std::string &error) {
    auto buf = encode_message(msg, &error);
    if (buf.empty()) {
        return false;
    }
    return conn.write_frame(buf, &error);
}

static bool recv_message(Connection &conn, Message &msg, std::string &error) {
    std::vector<std::byte> frame;
    if (!conn.read_frame(frame, &error)) {
        return false;
    }
    auto decoded = decode_message(frame);
    if (!decoded.ok) {
        error = decoded.error;
        return false;
    }
    msg = std::move(decoded.message);
    return true;
}

static bool ensure_tls_if_tcp(const Endpoint &endpoint, const TlsConfig &tls, std::string &error) {
    if (endpoint.kind != Endpoint::Kind::Tcp || tls.enabled) {
        return true;
    }

    std::string host = endpoint.host;
    for (char &ch : host) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    const bool is_loopback = (host == "127.0.0.1" || host == "localhost" || host == "::1");
    if (!is_loopback) {
        error = "TLS required for non-loopback TCP endpoints; provide --tls-ca/--tls-cert/--tls-key";
        return false;
    }
    return true;
}

static int handle_submit(const Args &args) {
    if (args.spec_path.empty()) {
        std::cerr << "coordctl: missing --spec\n";
        return 1;
    }
    SessionSpec spec{};
    std::string error;
    if (args.spec_path.size() >= 5 && args.spec_path.substr(args.spec_path.size() - 5) == ".json") {
        if (!load_session_spec_json(args.spec_path, spec, &error)) {
            std::cerr << "coordctl: " << error << "\n";
            return 1;
        }
    } else {
        auto data = read_file_bytes(args.spec_path);
        if (!decode_spec_cbor(data, spec, error)) {
            std::cerr << "coordctl: " << error << "\n";
            return 1;
        }
    }

    Endpoint endpoint = parse_endpoint(args.connect, &error);
    if (!error.empty()) {
        std::cerr << "coordctl: " << error << "\n";
        return 1;
    }
    if (!ensure_tls_if_tcp(endpoint, args.tls, error)) {
        std::cerr << "coordctl: " << error << "\n";
        return 1;
    }
    Connection conn = connect_endpoint(endpoint, args.tls, &error);
    if (!conn.is_valid()) {
        std::cerr << "coordctl: " << error << "\n";
        return 1;
    }

    Message submit{1, MsgSessionSubmit{spec}};
    if (!send_message(conn, submit, error)) {
        std::cerr << "coordctl: " << error << "\n";
        return 1;
    }
    Message reply;
    if (!recv_message(conn, reply, error)) {
        std::cerr << "coordctl: " << error << "\n";
        return 1;
    }
    if (!std::holds_alternative<MsgSessionAccepted>(reply.payload)) {
        std::cerr << "coordctl: unexpected response to submit\n";
        return 1;
    }
    auto session_id = std::get<MsgSessionAccepted>(reply.payload).session_id;

    if (!args.wait) {
        std::cout << session_id << "\n";
        return 0;
    }

    Message wait_msg{1, MsgSessionWait{session_id}};
    if (!send_message(conn, wait_msg, error)) {
        std::cerr << "coordctl: " << error << "\n";
        return 1;
    }
    if (!recv_message(conn, reply, error)) {
        std::cerr << "coordctl: " << error << "\n";
        return 1;
    }
    if (!std::holds_alternative<MsgSessionManifest>(reply.payload)) {
        std::cerr << "coordctl: unexpected response to wait\n";
        return 1;
    }
    auto manifest = std::get<MsgSessionManifest>(reply.payload).manifest;

    if (!args.report.empty()) {
        fs::path report_path = args.report;
        if (report_path.extension() != ".xml") {
            fs::create_directories(report_path);
            report_path /= ("coordd_session_" + session_id + ".xml");
        } else if (report_path.has_parent_path()) {
            fs::create_directories(report_path.parent_path());
        }
        fs::path manifest_path = report_path;
        manifest_path.replace_extension("manifest.json");
        std::string json_error;
        write_manifest_json(manifest, manifest_path.string(), &json_error);
        std::string junit_error;
        if (!write_junit(manifest, report_path.string(), manifest_path.string(), junit_error)) {
            std::cerr << "coordctl: " << junit_error << "\n";
        }
    }

    std::cout << "session " << session_id << " result=" << static_cast<int>(manifest.result) << "\n";
    if (!manifest.fail_reason.empty()) {
        std::cout << "reason: " << manifest.fail_reason << "\n";
    }

    return manifest.result == ResultCode::Success ? 0 : 1;
}

static int handle_wait(const Args &args) {
    if (args.session_id.empty()) {
        std::cerr << "coordctl: missing --session\n";
        return 1;
    }
    std::string error;
    Endpoint endpoint = parse_endpoint(args.connect, &error);
    if (!ensure_tls_if_tcp(endpoint, args.tls, error)) {
        std::cerr << "coordctl: " << error << "\n";
        return 1;
    }
    Connection conn = connect_endpoint(endpoint, args.tls, &error);
    if (!conn.is_valid()) {
        std::cerr << "coordctl: " << error << "\n";
        return 1;
    }
    Message wait_msg{1, MsgSessionWait{args.session_id}};
    if (!send_message(conn, wait_msg, error)) {
        std::cerr << "coordctl: " << error << "\n";
        return 1;
    }
    Message reply;
    if (!recv_message(conn, reply, error)) {
        std::cerr << "coordctl: " << error << "\n";
        return 1;
    }
    if (!std::holds_alternative<MsgSessionManifest>(reply.payload)) {
        std::cerr << "coordctl: unexpected response to wait\n";
        return 1;
    }
    auto manifest = std::get<MsgSessionManifest>(reply.payload).manifest;
    std::cout << "session " << args.session_id << " result=" << static_cast<int>(manifest.result) << "\n";
    return manifest.result == ResultCode::Success ? 0 : 1;
}

static int handle_status(const Args &args) {
    if (args.session_id.empty()) {
        std::cerr << "coordctl: missing --session\n";
        return 1;
    }
    std::string error;
    Endpoint endpoint = parse_endpoint(args.connect, &error);
    if (!ensure_tls_if_tcp(endpoint, args.tls, error)) {
        std::cerr << "coordctl: " << error << "\n";
        return 1;
    }
    Connection conn = connect_endpoint(endpoint, args.tls, &error);
    if (!conn.is_valid()) {
        std::cerr << "coordctl: " << error << "\n";
        return 1;
    }
    Message status_msg{1, MsgSessionStatusRequest{args.session_id}};
    if (!send_message(conn, status_msg, error)) {
        std::cerr << "coordctl: " << error << "\n";
        return 1;
    }
    Message reply;
    if (!recv_message(conn, reply, error)) {
        std::cerr << "coordctl: " << error << "\n";
        return 1;
    }
    if (!std::holds_alternative<MsgSessionStatus>(reply.payload)) {
        std::cerr << "coordctl: unexpected response to status\n";
        return 1;
    }
    auto status = std::get<MsgSessionStatus>(reply.payload).status;
    std::cout << "session " << status.session_id << " complete=" << status.complete
              << " result=" << static_cast<int>(status.result) << "\n";
    return 0;
}

static int handle_shutdown(const Args &args) {
    std::string error;
    Endpoint endpoint = parse_endpoint(args.connect, &error);
    if (!ensure_tls_if_tcp(endpoint, args.tls, error)) {
        std::cerr << "coordctl: " << error << "\n";
        return 1;
    }
    Connection conn = connect_endpoint(endpoint, args.tls, &error);
    if (!conn.is_valid()) {
        std::cerr << "coordctl: " << error << "\n";
        return 1;
    }
    Message msg{1, MsgShutdown{args.shutdown_token}};
    send_message(conn, msg, error);
    Message reply;
    recv_message(conn, reply, error);
    return 0;
}

static int handle_daemonize(const Args &args) {
    if (args.coordd_path.empty() || args.listen.empty() || args.root.empty()) {
        std::cerr << "coordctl: daemonize requires --coordd, --listen, and --root\n";
        return 1;
    }
    std::string error;
    Endpoint endpoint = parse_endpoint(args.listen, &error);
    if (!error.empty()) {
        std::cerr << "coordctl: " << error << "\n";
        return 1;
    }
    if (!ensure_tls_if_tcp(endpoint, args.tls, error)) {
        std::cerr << "coordctl: " << error << "\n";
        return 1;
    }
    if (!spawn_coordd(args, error)) {
        std::cerr << "coordctl: " << error << "\n";
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    Args args{};
    if (!parse_args(argc, argv, args)) {
        return 1;
    }
    if (args.command == "submit") {
        return handle_submit(args);
    }
    if (args.command == "wait") {
        return handle_wait(args);
    }
    if (args.command == "status") {
        return handle_status(args);
    }
    if (args.command == "shutdown") {
        return handle_shutdown(args);
    }
    if (args.command == "daemonize") {
        return handle_daemonize(args);
    }
    usage();
    return 1;
}
