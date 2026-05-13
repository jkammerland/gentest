#include "gentest/log_sink.h"

#include "gentest/detail/runtime_context.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>
#include <version>

#if defined(__has_include)
#if __has_include(<syncstream>)
#include <syncstream>
#endif
#endif

#if defined(__cpp_lib_syncbuf) && __cpp_lib_syncbuf >= 201803L
#define GENTEST_HAS_OSYNCSTREAM 1
#else
#define GENTEST_HAS_OSYNCSTREAM 0
#include <mutex>
#endif

namespace gentest {
namespace {

void write_line(std::ostream &out, std::string_view message) noexcept {
    try {
#if GENTEST_HAS_OSYNCSTREAM
        std::osyncstream synced(out);
        synced << message << '\n';
#else
        static std::mutex           fallback_mtx;
        std::lock_guard<std::mutex> lk(fallback_mtx);
        out << message << '\n';
        out.flush();
#endif
    } catch (...) { return; }
}

class OstreamLogSink final : public LogSink {
  public:
    explicit OstreamLogSink(std::ostream &out) noexcept : out_(&out) {}

    void write(std::string_view message) noexcept override { write_line(*out_, message); }

  private:
    std::ostream *out_ = nullptr;
};

class StdoutLogSink final : public LogSink {
  public:
    void write(std::string_view message) noexcept override {
        auto ctx = detail::current_test_storage();
        if (ctx && ctx->suppress_stdout_log.load(std::memory_order_acquire)) {
            return;
        }
        detail::write_default_stdout_log(message);
    }
};

struct SinkEntry {
    std::size_t              id = 0;
    std::shared_ptr<LogSink> sink;
};

struct SinkRegistry {
    SinkRegistry() : sinks{SinkEntry{.id = next_id++, .sink = std::make_shared<StdoutLogSink>()}} {}

    std::size_t            next_id = 1;
    std::vector<SinkEntry> sinks;
};

auto registry() -> SinkRegistry & {
    static SinkRegistry instance;
    return instance;
}

struct DefaultStdoutWriterState {
    void                            *state  = nullptr;
    detail::DefaultStdoutLogWriterFn writer = nullptr;
};

auto default_stdout_writer_state() -> DefaultStdoutWriterState & {
    static DefaultStdoutWriterState state;
    return state;
}

void write_stdout_fallback(std::string_view message) noexcept { write_line(std::cout, message); }

auto sink_active(std::size_t id) noexcept -> bool {
    if (id == 0) {
        return false;
    }
    auto &reg = registry();
    return std::ranges::any_of(reg.sinks, [&](const SinkEntry &entry) { return entry.id == id; });
}

auto remove_sink(std::size_t id) noexcept -> bool {
    if (id == 0) {
        return false;
    }
    auto      &reg    = registry();
    auto      &sinks  = reg.sinks;
    const auto before = sinks.size();
    std::erase_if(sinks, [&](const SinkEntry &entry) { return entry.id == id; });
    return sinks.size() != before;
}

[[noreturn]] void abort_active_handle_overwrite() noexcept {
    (void)std::fputs("gentest: fatal: move-assigned over an active LogSinkHandle; call remove() first.\n", stderr);
    std::abort();
}

} // namespace

namespace detail {

void write_default_stdout_log(std::string_view message) noexcept {
    auto &state = default_stdout_writer_state();
    if (state.writer) {
        state.writer(state.state, message);
        return;
    }
    write_stdout_fallback(message);
}

void dispatch_log_to_sinks(std::string_view message) noexcept {
    auto &reg = registry();
    for (const auto &entry : reg.sinks) {
        if (entry.sink) {
            entry.sink->write(message);
        }
    }
}

void install_default_stdout_log_writer(void *state, DefaultStdoutLogWriterFn writer) noexcept {
    auto &writer_state  = default_stdout_writer_state();
    writer_state.state  = state;
    writer_state.writer = writer;
}

void remove_default_stdout_log_writer(void *state, DefaultStdoutLogWriterFn writer) noexcept {
    auto &writer_state = default_stdout_writer_state();
    if (writer_state.state == state && writer_state.writer == writer) {
        writer_state.state  = nullptr;
        writer_state.writer = nullptr;
    }
}

DefaultStdoutLogWriterScope::DefaultStdoutLogWriterScope(void *state, DefaultStdoutLogWriterFn writer) noexcept
    : state_(state), writer_(writer) {
    if (state_ && writer_) {
        install_default_stdout_log_writer(state_, writer_);
    }
}

DefaultStdoutLogWriterScope::~DefaultStdoutLogWriterScope() {
    if (state_ && writer_) {
        remove_default_stdout_log_writer(state_, writer_);
    }
}

} // namespace detail

LogSinkHandle::LogSinkHandle(LogSinkHandle &&other) noexcept : id_(std::exchange(other.id_, 0)) {}

auto LogSinkHandle::operator=(LogSinkHandle &&other) noexcept -> LogSinkHandle & {
    if (this != &other) {
        if (id_ != 0) {
            abort_active_handle_overwrite();
        }
        id_ = std::exchange(other.id_, 0);
    }
    return *this;
}

auto LogSinkHandle::active() const noexcept -> bool { return sink_active(id_); }

auto LogSinkHandle::remove() noexcept -> bool { return remove_log_sink(*this); }

auto add_log_sink(std::shared_ptr<LogSink> sink) -> LogSinkHandle {
    if (!sink) {
        return {};
    }
    auto      &reg = registry();
    const auto id  = reg.next_id++;
    reg.sinks.push_back(SinkEntry{.id = id, .sink = std::move(sink)});
    return LogSinkHandle(id);
}

auto remove_log_sink(LogSinkHandle &handle) noexcept -> bool {
    const auto id = std::exchange(handle.id_, 0);
    return remove_sink(id);
}

void remove_all_log_sinks() noexcept {
    auto &reg = registry();
    reg.sinks.clear();
}

void restore_default_log_sink() {
    auto  default_sink = std::make_shared<StdoutLogSink>();
    auto &reg          = registry();
    reg.sinks.clear();
    reg.sinks.push_back(SinkEntry{.id = reg.next_id++, .sink = std::move(default_sink)});
}

auto make_ostream_log_sink(std::ostream &out) -> std::shared_ptr<LogSink> { return std::make_shared<OstreamLogSink>(out); }

} // namespace gentest
