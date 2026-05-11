#include "gentest/log_sink.h"

#include "gentest/detail/runtime_context.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <syncstream>
#include <utility>
#include <vector>

namespace gentest {
namespace {

class OstreamLogSink final : public LogSink {
  public:
    explicit OstreamLogSink(std::ostream &out) noexcept : out_(&out) {}

    void write(std::string_view message) noexcept override {
        try {
            std::osyncstream synced(*out_);
            synced << message << '\n';
        } catch (...) {}
    }

  private:
    std::ostream *out_ = nullptr;
};

class StdoutLogSink final : public LogSink {
  public:
    void write(std::string_view message) noexcept override { detail::write_default_stdout_log(message); }
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

void write_stdout_fallback(std::string_view message) noexcept {
    try {
        std::osyncstream synced(std::cout);
        synced << message << '\n';
    } catch (...) {}
}

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
