#include "gentest/framework_events.h"

#include "gentest/detail/runtime_context.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace gentest {
void FrameworkEventVisitor::onFixtureStart(const FrameworkEvent &event) noexcept { (void)event.notifier.progress("fixture start"); }
void FrameworkEventVisitor::onFixtureFinish(const FrameworkEvent &event) noexcept { (void)event.notifier.progress("fixture finish"); }
void FrameworkEventVisitor::onPhaseStart(const FrameworkEvent &event) noexcept { (void)event.notifier.progress("phase start"); }
void FrameworkEventVisitor::onPhaseFinish(const FrameworkEvent &event) noexcept { (void)event.notifier.progress("phase finish"); }
void FrameworkEventVisitor::onSuspend(const FrameworkEvent &event) noexcept { (void)event.notifier.progress("suspend"); }

namespace detail {
struct FrameworkRunState {
    Notifier                                     notifier;
    std::shared_ptr<FrameworkEventVisitor>       visitor;
    std::atomic<bool>                            failed{false};
    std::vector<std::weak_ptr<FrameworkSubject>> subjects;
    bool                                         finished = false;
};

namespace {
thread_local std::shared_ptr<FrameworkRunState> active_run;
thread_local std::shared_ptr<FrameworkSubject>  active_subject;
thread_local bool                               in_callback = false;
std::atomic<std::uint64_t>                      next_subject{1};
FrameworkEventVisitor                           default_visitor;

void note_result(FrameworkRunState &run, NotifyResult result) noexcept {
    if (result != NotifyResult::accepted && result != NotifyResult::disabled) {
        run.failed.store(true, std::memory_order_release);
    }
}

// Every runtime-issued handle retains this sink. This catches ignored return
// values from explicit application notifications as well as visitor callbacks.
class ContextNotificationSink final : public NotificationSink {
  public:
    ContextNotificationSink(std::shared_ptr<FrameworkRunState> run, std::uint64_t subject)
        : run_(std::move(run)), upstream_(run_->notifier.forSubject(subject)) {}

    void onError(NotifyResult result) noexcept override { note_result(*run_, result); }

    auto submit(const Notification &record) noexcept -> NotifyResult override {
        if (closed_.load(std::memory_order_acquire)) {
            return NotifyResult::closed;
        }
        if (record.subject != upstream_.subject()) {
            note_result(*run_, NotifyResult::invalid_record);
            return NotifyResult::invalid_record;
        }
        NotifyResult result = NotifyResult::invalid_record;
        switch (record.kind) {
        case NotificationKind::subject_started: result = upstream_.start(record.payload); break;
        case NotificationKind::subject_finished:
            closed_.store(true, std::memory_order_release);
            result = upstream_.finish();
            break;
        case NotificationKind::progress: result = upstream_.progress(record.payload); break;
        case NotificationKind::custom: result = upstream_.notify(record.topic, record.payload); break;
        case NotificationKind::silence_allowance: result = upstream_.allowSilenceFor(record.allowance, record.payload); break;
        }
        note_result(*run_, result);
        return result;
    }

  private:
    std::shared_ptr<FrameworkRunState> run_;
    Notifier                           upstream_;
    std::atomic<bool>                  closed_{false};
};
} // namespace

struct FrameworkSubject {
    std::shared_ptr<FrameworkRunState> run;
    Notifier                           notifier;
    std::string                        name;
    bool                               fixture  = false;
    bool                               finished = false;
};

namespace {
using Hook = void (FrameworkEventVisitor::*)(const FrameworkEvent &) noexcept;

void dispatch(const std::shared_ptr<FrameworkRunState> &run, Hook hook, const FrameworkEvent &event) noexcept {
    if (!run || run->finished) {
        return;
    }
    if (in_callback) {
        run->failed.store(true, std::memory_order_release);
        return;
    }
    in_callback   = true;
    auto &visitor = run->visitor ? *run->visitor : default_visitor;
    (visitor.*hook)(event);
    in_callback = false;
}

auto current_subject() -> std::shared_ptr<FrameworkSubject> {
    if (const auto &context = current_test_storage();
        context && context->framework_subject && (!active_run || context->framework_subject->run == active_run)) {
        return context->framework_subject;
    }
    return active_subject;
}

auto start_subject(std::string_view name, bool fixture) -> std::shared_ptr<FrameworkSubject> {
    if (!active_run) {
        return {};
    }
    const auto id = next_subject.fetch_add(1, std::memory_order_relaxed);
    if (id == 0) {
        active_run->failed.store(true, std::memory_order_release);
        return {};
    }
    auto subject      = std::make_shared<FrameworkSubject>();
    subject->run      = active_run;
    subject->name     = name;
    subject->fixture  = fixture;
    subject->notifier = active_run->notifier.forSubject(id);
    if (active_run->notifier.active()) {
        subject->notifier = Notifier(std::make_shared<ContextNotificationSink>(active_run, id), active_run->notifier.producer(), id);
    }
    active_run->subjects.emplace_back(subject);
    note_result(*active_run, subject->notifier.start(name));
    dispatch(active_run, fixture ? &FrameworkEventVisitor::onFixtureStart : &FrameworkEventVisitor::onCaseStart,
             {.notifier = subject->notifier, .name = subject->name});
    return subject;
}

void finish_subject(const std::shared_ptr<FrameworkSubject> &subject) noexcept {
    if (!subject || subject->finished) {
        return;
    }
    subject->finished = true;
    dispatch(subject->run, subject->fixture ? &FrameworkEventVisitor::onFixtureFinish : &FrameworkEventVisitor::onCaseFinish,
             {.notifier = subject->notifier, .name = subject->name});
    note_result(*subject->run, subject->notifier.finish());

    // Retained context tokens must not make registry memory grow with repeats.
    std::erase_if(subject->run->subjects, [&](const auto &weak) {
        const auto candidate = weak.lock();
        return !candidate || candidate == subject;
    });
}
} // namespace

FrameworkRunScope::FrameworkRunScope(const RunConfiguration &configuration)
    : previous_(std::move(active_run)), previous_subject_(std::move(active_subject)) {
    if (configuration.notifier.active() || configuration.frameworkEvents) {
        state_           = std::make_shared<FrameworkRunState>();
        state_->notifier = configuration.notifier;
        state_->visitor  = configuration.frameworkEvents;
    }
    active_run = state_;
    dispatch(state_, &FrameworkEventVisitor::onRunStart, {});
}

FrameworkRunScope::~FrameworkRunScope() {
    finish();
    active_run     = std::move(previous_);
    active_subject = std::move(previous_subject_);
}

void FrameworkRunScope::finish() noexcept {
    if (!state_ || state_->finished) {
        return;
    }
    while (!state_->subjects.empty()) {
        auto subject = state_->subjects.back().lock();
        state_->subjects.pop_back();
        finish_subject(subject);
    }
    dispatch(state_, &FrameworkEventVisitor::onRunFinish, {});
    state_->finished = true;
    if (failed()) {
        (void)std::fputs("gentest: notification infrastructure failed\n", stderr);
    }
}

auto FrameworkRunScope::failed() const noexcept -> bool { return state_ && state_->failed.load(std::memory_order_acquire); }

FrameworkSubjectScope::FrameworkSubjectScope(std::string_view name, bool fixture) : previous_(active_subject) {
    subject_       = start_subject(name, fixture);
    active_subject = subject_;
}
FrameworkSubjectScope::~FrameworkSubjectScope() {
    finish_subject(subject_);
    active_subject = std::move(previous_);
}

FrameworkPhaseScope::FrameworkPhaseScope(FrameworkPhase phase, bool fixture, std::string_view name) noexcept
    : subject_(current_subject()), phase_(phase), fixture_(fixture), name_(name) {
    if (subject_) {
        dispatch(subject_->run, fixture_ ? &FrameworkEventVisitor::onFixtureStart : &FrameworkEventVisitor::onPhaseStart,
                 {.notifier = subject_->notifier, .name = name_.empty() ? std::string_view(subject_->name) : name_, .phase = phase_});
    }
}
FrameworkPhaseScope::~FrameworkPhaseScope() {
    if (subject_) {
        dispatch(subject_->run, fixture_ ? &FrameworkEventVisitor::onFixtureFinish : &FrameworkEventVisitor::onPhaseFinish,
                 {.notifier = subject_->notifier, .name = name_.empty() ? std::string_view(subject_->name) : name_, .phase = phase_});
    }
}

void inherit_framework_context(TestContextInfo &context) { context.framework_subject = current_subject(); }
void start_framework_case(TestContextInfo &context, std::string_view name) {
    context.framework_subject       = start_subject(name, false);
    context.framework_subject_owner = true;
}
void finish_framework_case(TestContextInfo &context) noexcept {
    if (context.framework_subject_owner) {
        finish_subject(context.framework_subject);
    }
}
void framework_suspend(const std::shared_ptr<TestContextInfo> &context, SuspensionKind kind,
                       const std::source_location &location) noexcept {
    if (!context || !context->framework_subject || context->framework_subject->finished) {
        return;
    }
    const auto &subject = context->framework_subject;
    dispatch(subject->run, &FrameworkEventVisitor::onSuspend,
             {.notifier = subject->notifier, .name = subject->name, .suspension = kind, .location = location});
}
auto context_notifier(const std::shared_ptr<TestContextInfo> &context) noexcept -> Notifier {
    return context && context->framework_subject ? context->framework_subject->notifier : Notifier{};
}
} // namespace detail
} // namespace gentest
