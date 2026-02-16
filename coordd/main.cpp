#include "coord/codec.h"
#include "coord/transport.h"
#include "coord/types.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cerrno>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#else
#include <io.h>
#endif

namespace coordd {

using namespace coord;
namespace fs = std::filesystem;

struct ServerConfig {
    Endpoint listen;
    std::vector<std::string> peers;
    std::string root_dir;
    TlsConfig tls;
    bool daemonize{false};
    std::string pid_file;
    std::string ready_file;
    std::string shutdown_token;
};

struct SessionState {
    SessionManifest manifest;
    bool complete{false};
    std::uint64_t completed_at_ms{0};
    std::uint64_t last_access_ms{0};
    std::mutex mutex;
    std::condition_variable cv;
};

struct OutputWatcher {
    std::thread thread;
    std::mutex mutex;
    std::condition_variable cv;
    bool token_found{false};
    bool done{false};
    std::string token;
};

struct ProcessInstance {
    InstanceInfo info;
    int stdout_fd{-1};
    int stderr_fd{-1};
    OutputWatcher stdout_watch;
    OutputWatcher stderr_watch;
    ReadinessSpec readiness{};
};

static std::atomic<bool> g_shutdown{false};

static std::uint64_t now_ms() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

static std::string sanitize_env_key(const std::string &name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        } else {
            out.push_back('_');
        }
    }
    return out;
}

static std::string join_paths(const std::string &a, const std::string &b) {
    fs::path p = fs::path(a) / b;
    return p.string();
}

static std::vector<PortAssignment> allocate_ports(const NetworkSpec &network, std::vector<std::string> &diagnostics) {
    std::vector<PortAssignment> out;
#ifndef _WIN32
    for (const auto &req : network.ports) {
        PortAssignment pa;
        pa.name = req.name;
        pa.protocol = req.protocol;
        for (std::uint32_t i = 0; i < req.count; ++i) {
            int fd = ::socket(AF_INET, req.protocol == Protocol::Udp ? SOCK_DGRAM : SOCK_STREAM, 0);
            if (fd < 0) {
                diagnostics.push_back("port allocation failed: socket() error");
                continue;
            }
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = 0;
            if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
                diagnostics.push_back("port allocation failed: bind() error");
                ::close(fd);
                continue;
            }
            socklen_t len = sizeof(addr);
            if (::getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &len) == 0) {
                pa.ports.push_back(ntohs(addr.sin_port));
            }
            ::close(fd);
        }
        out.push_back(std::move(pa));
    }
#else
    (void)network;
    diagnostics.push_back("port allocation not implemented on Windows");
#endif
    return out;
}

static void watch_pipe_to_file(int fd, const std::string &path, OutputWatcher *watcher) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return;
    }
    std::string buffer;
    buffer.resize(4096);
    std::string sliding;
    while (true) {
#ifndef _WIN32
        ssize_t n = ::read(fd, buffer.data(), buffer.size());
#else
        int n = _read(fd, buffer.data(), static_cast<unsigned int>(buffer.size()));
#endif
        if (n <= 0) {
            break;
        }
        out.write(buffer.data(), n);
        if (watcher && !watcher->token.empty() && !watcher->token_found) {
            sliding.append(buffer.data(), buffer.data() + n);
            if (sliding.find(watcher->token) != std::string::npos) {
                std::lock_guard<std::mutex> lock(watcher->mutex);
                watcher->token_found = true;
                watcher->cv.notify_all();
            }
            if (sliding.size() > watcher->token.size() * 2) {
                sliding.erase(0, sliding.size() - watcher->token.size());
            }
        }
    }
    if (watcher) {
        std::lock_guard<std::mutex> lock(watcher->mutex);
        watcher->done = true;
        watcher->cv.notify_all();
    }
#ifndef _WIN32
    ::close(fd);
#else
    _close(fd);
#endif
}

static bool wait_for_readiness(const ReadinessSpec &spec, OutputWatcher *stdout_watch, std::uint64_t deadline_ms) {
    if (spec.kind == ReadinessKind::None) {
        return true;
    }
    if (spec.kind == ReadinessKind::StdoutToken) {
        if (!stdout_watch || spec.value.empty()) {
            return true;
        }
        std::unique_lock<std::mutex> lock(stdout_watch->mutex);
        while (!stdout_watch->token_found && !stdout_watch->done) {
            auto now = now_ms();
            if (now >= deadline_ms) {
                return false;
            }
            stdout_watch->cv.wait_for(lock, std::chrono::milliseconds(50));
        }
        return stdout_watch->token_found;
    }
#ifndef _WIN32
    if (spec.kind == ReadinessKind::File) {
        while (now_ms() < deadline_ms) {
            if (fs::exists(spec.value)) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return false;
    }
    if (spec.kind == ReadinessKind::Socket) {
        auto pos = spec.value.rfind(':');
        if (pos == std::string::npos) {
            return false;
        }
        std::string host = spec.value.substr(0, pos);
        std::string port = spec.value.substr(pos + 1);
        while (now_ms() < deadline_ms) {
            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            addrinfo *res = nullptr;
            if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) == 0) {
                bool ok = false;
                for (addrinfo *it = res; it; it = it->ai_next) {
                    int fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
                    if (fd < 0) continue;
                    if (::connect(fd, it->ai_addr, it->ai_addrlen) == 0) {
                        ok = true;
                    }
                    ::close(fd);
                    if (ok) break;
                }
                freeaddrinfo(res);
                if (ok) return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return false;
    }
#else
    (void)spec;
#endif
    return true;
}

static void terminate_all(const std::deque<ProcessInstance> &instances, std::uint32_t shutdown_ms) {
#ifndef _WIN32
    for (const auto &inst : instances) {
        if (inst.info.pid > 0) {
            ::kill(static_cast<pid_t>(inst.info.pid), SIGTERM);
        }
    }
    auto deadline = now_ms() + shutdown_ms;
    while (now_ms() < deadline) {
        bool any_alive = false;
        for (const auto &inst : instances) {
            if (inst.info.pid > 0) {
                if (::kill(static_cast<pid_t>(inst.info.pid), 0) == 0) {
                    any_alive = true;
                }
            }
        }
        if (!any_alive) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    for (const auto &inst : instances) {
        if (inst.info.pid > 0) {
            ::kill(static_cast<pid_t>(inst.info.pid), SIGKILL);
        }
    }
#endif
}

static bool spawn_instance(const SessionSpec &spec,
                           const NodeDef &node,
                           std::uint32_t index,
                           const std::string &session_dir,
                           const std::vector<PortAssignment> &ports,
                           const std::unordered_map<std::string, std::string> &node_addrs,
                           ProcessInstance &out,
                           std::string &error) {
#ifdef _WIN32
    (void)spec; (void)node; (void)index; (void)session_dir; (void)ports; (void)node_addrs;
    error = "process spawning not implemented on Windows";
    return false;
#else
    fs::path inst_dir = fs::path(session_dir) / node.name / ("inst" + std::to_string(index));
    fs::create_directories(inst_dir);
    std::string stdout_path = (inst_dir / "stdout.log").string();
    std::string stderr_path = (inst_dir / "stderr.log").string();

    int stdout_pipe[2];
    int stderr_pipe[2];
    if (::pipe(stdout_pipe) != 0 || ::pipe(stderr_pipe) != 0) {
        error = "pipe failed";
        return false;
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        error = "fork failed";
        return false;
    }

    if (pid == 0) {
        ::dup2(stdout_pipe[1], STDOUT_FILENO);
        ::dup2(stderr_pipe[1], STDERR_FILENO);
        ::close(stdout_pipe[0]);
        ::close(stdout_pipe[1]);
        ::close(stderr_pipe[0]);
        ::close(stderr_pipe[1]);

        if (!node.cwd.empty()) {
            ::chdir(node.cwd.c_str());
        }

        std::vector<std::string> env_list;
        for (char **env = environ; *env; ++env) {
            env_list.emplace_back(*env);
        }
        env_list.push_back("COORD_SESSION_ID=" + spec.session_id);
        env_list.push_back("COORD_GROUP=" + spec.group);
        env_list.push_back("COORD_NODE_NAME=" + node.name);
        env_list.push_back("COORD_NODE_INDEX=" + std::to_string(index));

        for (const auto &addr : node_addrs) {
            env_list.push_back("COORD_NODE_ADDR_" + sanitize_env_key(addr.first) + "=" + addr.second);
        }

        for (const auto &pa : ports) {
            std::string base = "COORD_PORT_" + sanitize_env_key(pa.name);
            if (pa.ports.size() == 1) {
                env_list.push_back(base + "=" + std::to_string(pa.ports[0]));
            }
            for (std::size_t i = 0; i < pa.ports.size(); ++i) {
                env_list.push_back(base + "_" + std::to_string(i) + "=" + std::to_string(pa.ports[i]));
            }
        }

        for (const auto &env : node.env) {
            env_list.push_back(env.key + "=" + env.value);
        }

        std::vector<char *> envp;
        envp.reserve(env_list.size() + 1);
        for (auto &entry : env_list) {
            envp.push_back(entry.data());
        }
        envp.push_back(nullptr);

        std::vector<char *> argv;
        argv.reserve(node.args.size() + 2);
        argv.push_back(const_cast<char *>(node.exec.c_str()));
        for (const auto &arg : node.args) {
            argv.push_back(const_cast<char *>(arg.c_str()));
        }
        argv.push_back(nullptr);

        ::execve(node.exec.c_str(), argv.data(), envp.data());
        std::perror("execve");
        std::exit(127);
    }

    ::close(stdout_pipe[1]);
    ::close(stderr_pipe[1]);

    out.info.node = node.name;
    out.info.index = index;
    out.info.pid = pid;
    out.info.log_path = stdout_path;
    out.info.err_path = stderr_path;
    out.info.start_ms = now_ms();
    out.stdout_fd = stdout_pipe[0];
    out.stderr_fd = stderr_pipe[0];
    out.stdout_watch.token = node.readiness.kind == ReadinessKind::StdoutToken ? node.readiness.value : "";
    out.readiness = node.readiness;

    out.stdout_watch.thread = std::thread(watch_pipe_to_file, out.stdout_fd, stdout_path, &out.stdout_watch);
    out.stderr_watch.thread = std::thread(watch_pipe_to_file, out.stderr_fd, stderr_path, &out.stderr_watch);
    return true;
#endif
}

static SessionManifest run_session(const SessionSpec &spec, const std::string &root_dir) {
    SessionManifest manifest{};
    manifest.session_id = spec.session_id;
    manifest.group = spec.group;
    manifest.mode = spec.mode;
    manifest.result = ResultCode::Error;
    manifest.start_ms = now_ms();

    if (spec.mode != ExecMode::A) {
        manifest.fail_reason = "execution mode not implemented in this build";
        manifest.end_ms = now_ms();
        return manifest;
    }
    if (spec.nodes.empty()) {
        manifest.fail_reason = "session spec has no nodes";
        manifest.end_ms = now_ms();
        return manifest;
    }

    fs::path session_dir;
    if (!spec.artifact_dir.empty()) {
        fs::path adir = spec.artifact_dir;
        if (adir.is_absolute()) {
            session_dir = adir / spec.session_id;
        } else {
            session_dir = fs::path(root_dir) / adir / spec.session_id;
        }
    } else {
        session_dir = fs::path(root_dir) / spec.session_id;
    }
    fs::create_directories(session_dir);

    std::vector<std::string> diagnostics;
    auto ports = allocate_ports(spec.network, diagnostics);

    std::unordered_map<std::string, std::string> node_addrs;
    const std::string default_node_addr = spec.network.bridge.empty() ? "127.0.0.1" : spec.network.bridge;
    for (const auto &node : spec.nodes) {
        node_addrs[node.name] = default_node_addr;
    }

    std::deque<ProcessInstance> instances;
    std::string error;

    auto startup_deadline = now_ms() + spec.timeouts.startup_ms;
    bool aborted = false;
    std::uint64_t abort_deadline = 0;

    for (const auto &node : spec.nodes) {
        std::size_t start_index = instances.size();
        for (std::uint32_t idx = 0; idx < node.instances; ++idx) {
            instances.emplace_back();
            ProcessInstance &inst = instances.back();
            if (!spawn_instance(spec, node, idx, session_dir.string(), ports, node_addrs, inst, error)) {
                instances.pop_back();
                manifest.fail_reason = error;
                manifest.result = ResultCode::Error;
                terminate_all(instances, spec.timeouts.shutdown_ms);
                for (auto &running : instances) {
                    if (running.stdout_watch.thread.joinable()) running.stdout_watch.thread.join();
                    if (running.stderr_watch.thread.joinable()) running.stderr_watch.thread.join();
                }
                manifest.diagnostics = diagnostics;
                for (auto &running : instances) {
                    auto it = node_addrs.find(running.info.node);
                    running.info.addr = (it != node_addrs.end()) ? it->second : std::string{};
                    running.info.ports = ports;
                    manifest.instances.push_back(running.info);
                }
                manifest.end_ms = now_ms();
                return manifest;
            }
        }

        std::size_t end_index = instances.size();
        for (std::size_t i = start_index; i < end_index; ++i) {
            auto &inst = instances[i];
            if (!wait_for_readiness(inst.readiness, &inst.stdout_watch, startup_deadline)) {
                manifest.fail_reason = "startup readiness timeout";
                manifest.result = ResultCode::Failed;
                terminate_all(instances, spec.timeouts.shutdown_ms);
                aborted = true;
                abort_deadline = now_ms() + spec.timeouts.shutdown_ms;
                break;
            }
        }
        if (aborted) {
            break;
        }
    }

#ifndef _WIN32
    auto session_deadline = now_ms() + spec.timeouts.session_ms;
    std::size_t remaining = instances.size();
    while (remaining > 0) {
        if (!aborted && spec.timeouts.session_ms > 0 && now_ms() > session_deadline) {
            manifest.fail_reason = "session timeout";
            manifest.result = ResultCode::Timeout;
            terminate_all(instances, spec.timeouts.shutdown_ms);
            aborted = true;
            abort_deadline = now_ms() + spec.timeouts.shutdown_ms;
        }

        bool progress = false;
        for (auto &inst : instances) {
            if (inst.info.end_ms != 0 || inst.info.pid <= 0) {
                continue;
            }
            int status = 0;
            pid_t pid = ::waitpid(static_cast<pid_t>(inst.info.pid), &status, WNOHANG);
            if (pid == 0) {
                continue;
            }
            if (pid < 0) {
                if (errno == ECHILD) {
                    inst.info.failure_reason = "child reaped elsewhere";
                    inst.info.end_ms = now_ms();
                    manifest.result = ResultCode::Failed;
                    remaining--;
                    progress = true;
                }
                continue;
            }
            if (WIFEXITED(status)) {
                inst.info.exit_code = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                inst.info.term_signal = WTERMSIG(status);
            }
            inst.info.end_ms = now_ms();
            if (inst.info.exit_code != 0 || inst.info.term_signal != 0) {
                manifest.result = ResultCode::Failed;
            }
            remaining--;
            progress = true;
        }

        if (remaining == 0) {
            break;
        }
        if (aborted && abort_deadline > 0 && now_ms() > abort_deadline) {
            break;
        }
        if (!progress) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
#else
    (void)instances;
    manifest.fail_reason = "not implemented on Windows";
#endif

    for (auto &inst : instances) {
        if (inst.stdout_watch.thread.joinable()) inst.stdout_watch.thread.join();
        if (inst.stderr_watch.thread.joinable()) inst.stderr_watch.thread.join();
    }

    manifest.instances.clear();
    for (auto &inst : instances) {
        auto it = node_addrs.find(inst.info.node);
        inst.info.addr = (it != node_addrs.end()) ? it->second : std::string{};
        inst.info.ports = ports;
        manifest.instances.push_back(inst.info);
    }

    manifest.diagnostics = diagnostics;

    if (manifest.result == ResultCode::Error) {
        manifest.result = ResultCode::Success;
    }

    manifest.end_ms = now_ms();
    return manifest;
}

class SessionManager : public std::enable_shared_from_this<SessionManager> {
public:
    SessionManager(std::string root, TlsConfig tls)
        : root_dir_(std::move(root)), tls_(std::move(tls)) {}

    std::string submit(const SessionSpec &spec, const std::string &peer) {
        auto self = shared_from_this();
        std::string id = spec.session_id.empty() ? generate_session_id() : spec.session_id;
        auto state = std::make_shared<SessionState>();
        state->last_access_ms = now_ms();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            prune_stale_sessions_locked(now_ms());
            sessions_[id] = state;
        }
        SessionSpec spec_copy = spec;
        spec_copy.session_id = id;
        std::thread([self, state, spec_copy, peer]() {
            SessionManifest manifest{};
            if (!peer.empty()) {
                manifest = self->run_remote(spec_copy, peer);
            } else {
                manifest = run_session(spec_copy, self->root_dir_);
            }
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->manifest = std::move(manifest);
                state->complete = true;
                state->completed_at_ms = now_ms();
                state->last_access_ms = state->completed_at_ms;
            }
            state->cv.notify_all();
            self->prune_stale_sessions();
        }).detach();
        return id;
    }

    SessionManifest wait(const std::string &id) {
        std::shared_ptr<SessionState> state;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = sessions_.find(id);
            if (it == sessions_.end()) {
                SessionManifest man{};
                man.session_id = id;
                man.result = ResultCode::Error;
                man.fail_reason = "unknown session id";
                return man;
            }
            state = it->second;
        }
        std::unique_lock<std::mutex> lock(state->mutex);
        state->cv.wait(lock, [&state] { return state->complete; });
        state->last_access_ms = now_ms();
        SessionManifest manifest = state->manifest;
        lock.unlock();
        prune_stale_sessions();
        return manifest;
    }

    SessionStatus status(const std::string &id) {
        SessionStatus st{};
        st.session_id = id;
        std::shared_ptr<SessionState> state;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = sessions_.find(id);
            if (it == sessions_.end()) {
                st.result = ResultCode::Error;
                st.complete = true;
                return st;
            }
            state = it->second;
        }
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            st.complete = state->complete;
            st.result = state->manifest.result;
            state->last_access_ms = now_ms();
        }
        prune_stale_sessions();
        return st;
    }

private:
    static constexpr std::uint64_t kCompletedSessionRetentionMs = 60ULL * 60ULL * 1000ULL;

    void prune_stale_sessions() {
        std::lock_guard<std::mutex> lock(mutex_);
        prune_stale_sessions_locked(now_ms());
    }

    void prune_stale_sessions_locked(std::uint64_t now) {
        for (auto it = sessions_.begin(); it != sessions_.end();) {
            const auto &state = it->second;
            bool erase = false;
            {
                std::lock_guard<std::mutex> state_lock(state->mutex);
                if (state->complete) {
                    const std::uint64_t last_touch = state->last_access_ms == 0 ? state->completed_at_ms : state->last_access_ms;
                    if (last_touch > 0 && now >= last_touch && (now - last_touch) >= kCompletedSessionRetentionMs) {
                        erase = true;
                    }
                }
            }
            if (erase) {
                it = sessions_.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::string root_dir_;
    TlsConfig tls_;
    std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<SessionState>> sessions_;

    static std::string generate_session_id() {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        auto ticks = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        return "session_" + std::to_string(ticks);
    }

    SessionManifest run_remote(const SessionSpec &spec, const std::string &peer) {
        auto make_error_manifest = [&spec](std::string reason) {
            SessionManifest man{};
            man.session_id = spec.session_id;
            man.group = spec.group;
            man.mode = spec.mode;
            man.result = ResultCode::Error;
            man.fail_reason = std::move(reason);
            return man;
        };
        std::string error;
        Endpoint ep = parse_endpoint(peer, &error);
        if (!error.empty()) {
            return make_error_manifest(error);
        }
        Connection conn = connect_endpoint(ep, tls_, &error);
        if (!conn.is_valid()) {
            return make_error_manifest(error);
        }
        Message submit_msg{1, MsgSessionSubmit{spec}};
        auto buf = encode_message(submit_msg, &error);
        if (buf.empty()) {
            return make_error_manifest(error);
        }
        if (!conn.write_frame(buf, &error)) {
            if (error.empty()) {
                error = "failed to send submit message to peer";
            }
            return make_error_manifest(error);
        }
        std::vector<std::byte> reply;
        if (!conn.read_frame(reply, &error)) {
            if (error.empty()) {
                error = "failed to read submit reply from peer";
            }
            return make_error_manifest(error);
        }
        auto decoded = decode_message(reply);
        if (!decoded.ok) {
            return make_error_manifest(decoded.error);
        }
        if (std::holds_alternative<MsgError>(decoded.message.payload)) {
            auto msg = std::get<MsgError>(decoded.message.payload);
            return make_error_manifest(msg.message);
        }
        if (!std::holds_alternative<MsgSessionAccepted>(decoded.message.payload)) {
            return make_error_manifest("unexpected response from peer to submit");
        }
        std::string remote_id = std::get<MsgSessionAccepted>(decoded.message.payload).session_id;
        Message wait_msg{1, MsgSessionWait{remote_id}};
        buf = encode_message(wait_msg, &error);
        if (buf.empty()) {
            return make_error_manifest(error);
        }
        if (!conn.write_frame(buf, &error)) {
            if (error.empty()) {
                error = "failed to send wait message to peer";
            }
            return make_error_manifest(error);
        }
        reply.clear();
        if (!conn.read_frame(reply, &error)) {
            if (error.empty()) {
                error = "failed to read wait reply from peer";
            }
            return make_error_manifest(error);
        }
        auto decoded2 = decode_message(reply);
        if (!decoded2.ok) {
            return make_error_manifest(decoded2.error);
        }
        if (std::holds_alternative<MsgError>(decoded2.message.payload)) {
            auto msg = std::get<MsgError>(decoded2.message.payload);
            return make_error_manifest(msg.message);
        }
        if (!std::holds_alternative<MsgSessionManifest>(decoded2.message.payload)) {
            return make_error_manifest("unexpected response from peer to wait");
        }
        auto manifest = std::get<MsgSessionManifest>(decoded2.message.payload).manifest;
        return manifest;
    }
};

static void handle_connection(Connection conn, std::shared_ptr<SessionManager> sessions, const ServerConfig &cfg) {
    while (!g_shutdown.load()) {
        std::vector<std::byte> frame;
        std::string error;
        if (!conn.read_frame(frame, &error)) {
            return;
        }
        auto decoded = decode_message(frame);
        if (!decoded.ok) {
            Message err{1, MsgError{decoded.error}};
            auto buf = encode_message(err);
            conn.write_frame(buf, nullptr);
            continue;
        }
        if (std::holds_alternative<MsgSessionSubmit>(decoded.message.payload)) {
            auto msg = std::get<MsgSessionSubmit>(decoded.message.payload);
            std::string peer_target;
            if (!msg.spec.placement.target.empty() && msg.spec.placement.target.rfind("peer:", 0) == 0) {
                peer_target = msg.spec.placement.target.substr(5);
            }
            std::string id = sessions->submit(msg.spec, peer_target);
            Message accepted{1, MsgSessionAccepted{id}};
            auto buf = encode_message(accepted);
            conn.write_frame(buf, nullptr);
            continue;
        }
        if (std::holds_alternative<MsgSessionWait>(decoded.message.payload)) {
            auto msg = std::get<MsgSessionWait>(decoded.message.payload);
            auto manifest = sessions->wait(msg.session_id);
            Message reply{1, MsgSessionManifest{manifest}};
            auto buf = encode_message(reply);
            conn.write_frame(buf, nullptr);
            continue;
        }
        if (std::holds_alternative<MsgSessionStatusRequest>(decoded.message.payload)) {
            auto msg = std::get<MsgSessionStatusRequest>(decoded.message.payload);
            auto st = sessions->status(msg.session_id);
            Message reply{1, MsgSessionStatus{st}};
            auto buf = encode_message(reply);
            conn.write_frame(buf, nullptr);
            continue;
        }
        if (std::holds_alternative<MsgShutdown>(decoded.message.payload)) {
            auto msg = std::get<MsgShutdown>(decoded.message.payload);
            if (!cfg.shutdown_token.empty() && msg.token != cfg.shutdown_token) {
                Message err{1, MsgError{"invalid shutdown token"}};
                auto buf = encode_message(err);
                conn.write_frame(buf, nullptr);
                continue;
            }
            g_shutdown.store(true);
            Message ok{1, MsgSessionStatus{SessionStatus{"", ResultCode::Success, true}}};
            auto buf = encode_message(ok);
            conn.write_frame(buf, nullptr);
            std::string wake_error;
            (void)connect_endpoint(cfg.listen, cfg.tls, &wake_error);
            return;
        }
    }
}

#ifndef _WIN32
static void daemonize_process(const ServerConfig &cfg) {
    pid_t pid = fork();
    if (pid < 0) {
        std::perror("fork");
        std::exit(1);
    }
    if (pid > 0) {
        if (!cfg.ready_file.empty()) {
            auto deadline = now_ms() + 5000;
            while (now_ms() < deadline) {
                if (fs::exists(cfg.ready_file)) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
        std::exit(0);
    }
    if (setsid() < 0) {
        std::perror("setsid");
    }
}
#endif

static int run_server(const ServerConfig &cfg) {
    std::string error;
    SocketHandle listener = listen_endpoint(cfg.listen, &error);
    if (listener == kInvalidSocketHandle) {
        std::cerr << "coordd: " << error << "\n";
        return 1;
    }

#ifndef _WIN32
    if (cfg.daemonize) {
        daemonize_process(cfg);
    }
#endif

    if (!cfg.pid_file.empty()) {
        std::ofstream pid_file(cfg.pid_file);
        if (pid_file) {
            pid_file << getpid();
        }
    }

    if (!cfg.ready_file.empty()) {
        std::ofstream ready(cfg.ready_file);
        if (ready) {
            ready << "ready\n";
        }
    }

    auto manager = std::make_shared<SessionManager>(cfg.root_dir, cfg.tls);

    while (!g_shutdown.load()) {
        Connection conn = accept_connection(listener, cfg.tls, &error);
        if (!conn.is_valid()) {
            if (!error.empty()) {
                std::cerr << "coordd: accept error: " << error << "\n";
            }
            continue;
        }
        std::thread(handle_connection, std::move(conn), manager, std::cref(cfg)).detach();
    }

#ifndef _WIN32
    ::close(listener);
#endif
    return 0;
}

static void usage() {
    std::cout << "coordd --listen <endpoint> --root <dir> [--tls-ca <ca>] [--tls-cert <cert>] [--tls-key <key>] [--ready-file <path>] [--pid-file <path>] [--daemonize]\n";
}

static bool parse_args(int argc, char **argv, ServerConfig &cfg) {
    cfg.root_dir = "coordd_artifacts";
#ifndef _WIN32
    cfg.listen = parse_endpoint("unix://coordd.sock");
#else
    cfg.listen = parse_endpoint("tcp://127.0.0.1:7777");
#endif
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--listen" && i + 1 < argc) {
            cfg.listen = parse_endpoint(argv[++i]);
        } else if (arg == "--root" && i + 1 < argc) {
            cfg.root_dir = argv[++i];
        } else if (arg == "--peer" && i + 1 < argc) {
            cfg.peers.push_back(argv[++i]);
        } else if (arg == "--tls-ca" && i + 1 < argc) {
            cfg.tls.ca_file = argv[++i];
            cfg.tls.enabled = true;
        } else if (arg == "--tls-cert" && i + 1 < argc) {
            cfg.tls.cert_file = argv[++i];
            cfg.tls.enabled = true;
        } else if (arg == "--tls-key" && i + 1 < argc) {
            cfg.tls.key_file = argv[++i];
            cfg.tls.enabled = true;
        } else if (arg == "--ready-file" && i + 1 < argc) {
            cfg.ready_file = argv[++i];
        } else if (arg == "--pid-file" && i + 1 < argc) {
            cfg.pid_file = argv[++i];
        } else if (arg == "--shutdown-token" && i + 1 < argc) {
            cfg.shutdown_token = argv[++i];
        } else if (arg == "--daemonize") {
            cfg.daemonize = true;
        } else if (arg == "--help") {
            usage();
            return false;
        }
    }
    if (cfg.listen.kind == Endpoint::Kind::Tcp && cfg.tls.enabled) {
        if (cfg.tls.ca_file.empty() || cfg.tls.cert_file.empty() || cfg.tls.key_file.empty()) {
            std::cerr << "coordd: TLS enabled but missing CA/cert/key\n";
            return false;
        }
    }
    if (cfg.listen.kind == Endpoint::Kind::Tcp && !cfg.tls.enabled) {
        std::cerr << "coordd: TLS is required for TCP endpoints\n";
        return false;
    }
    return true;
}

} // namespace coordd

int main(int argc, char **argv) {
    coordd::ServerConfig cfg{};
    if (!coordd::parse_args(argc, argv, cfg)) {
        return 1;
    }
#ifndef _WIN32
    std::signal(SIGINT, [](int) { coordd::g_shutdown.store(true); });
    std::signal(SIGTERM, [](int) { coordd::g_shutdown.store(true); });
#endif
    return coordd::run_server(cfg);
}
