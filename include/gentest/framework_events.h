#pragma once

#include "gentest/detail/runtime_export.h"
#include "gentest/notification.h"

#include <memory>
#include <source_location>
#include <string_view>

namespace gentest {

// A suspension event means await_suspend reached a framework registration point.
// It also covers registration followed by a ready race, false return, or throw.
enum class SuspensionKind { yield, timer, event, future, continuation, composition, native_child };
enum class FrameworkPhase { setup, call, teardown };

struct FrameworkEvent {
    Notifier             notifier{};
    std::string_view     name{};
    FrameworkPhase       phase      = FrameworkPhase::call;
    SuspensionKind       suspension = SuspensionKind::yield;
    std::source_location location{};
};

// Source hooks run synchronously, outside framework locks. Implementations must
// be short, nonblocking, noexcept, and must not reenter the runner. Event views
// live for the callback only. Notifier copies may be retained by adopted work.
// Adopted threads may invoke hooks concurrently; visitors must synchronize their
// own state without blocking framework execution.
class GENTEST_RUNTIME_API FrameworkEventVisitor {
  public:
    virtual ~FrameworkEventVisitor() = default;
    virtual void onRunStart(const FrameworkEvent &) noexcept {}
    virtual void onRunFinish(const FrameworkEvent &) noexcept {}
    virtual void onCaseStart(const FrameworkEvent &) noexcept {}
    virtual void onCaseFinish(const FrameworkEvent &) noexcept {}
    virtual void onFixtureStart(const FrameworkEvent &event) noexcept;
    virtual void onFixtureFinish(const FrameworkEvent &event) noexcept;
    virtual void onPhaseStart(const FrameworkEvent &event) noexcept;
    virtual void onPhaseFinish(const FrameworkEvent &event) noexcept;
    virtual void onSuspend(const FrameworkEvent &event) noexcept;
};

struct RunConfiguration {
    Notifier                               notifier{};
    std::shared_ptr<FrameworkEventVisitor> frameworkEvents{};
};

namespace detail {
struct FrameworkRunState;
struct FrameworkSubject;
struct TestContextInfo;

class GENTEST_RUNTIME_API FrameworkRunScope {
  public:
    explicit FrameworkRunScope(const RunConfiguration &configuration);
    ~FrameworkRunScope();
    FrameworkRunScope(const FrameworkRunScope &)                                   = delete;
    auto               operator=(const FrameworkRunScope &) -> FrameworkRunScope & = delete;
    void               finish() noexcept;
    [[nodiscard]] auto failed() const noexcept -> bool;

  private:
    std::shared_ptr<FrameworkRunState> state_;
    std::shared_ptr<FrameworkRunState> previous_;
    std::shared_ptr<FrameworkSubject>  previous_subject_;
};

// Case scopes cover all phases. A fixture scope outside a case creates its own
// short-lived subject; nested fixture work inherits the current case subject.
class GENTEST_RUNTIME_API FrameworkSubjectScope {
  public:
    explicit FrameworkSubjectScope(std::string_view name, bool fixture = false);
    ~FrameworkSubjectScope();
    FrameworkSubjectScope(const FrameworkSubjectScope &)                     = delete;
    auto operator=(const FrameworkSubjectScope &) -> FrameworkSubjectScope & = delete;

  private:
    std::shared_ptr<FrameworkSubject> subject_;
    std::shared_ptr<FrameworkSubject> previous_;
};

class GENTEST_RUNTIME_API FrameworkPhaseScope {
  public:
    explicit FrameworkPhaseScope(FrameworkPhase phase, bool fixture = false, std::string_view name = {}) noexcept;
    ~FrameworkPhaseScope();
    FrameworkPhaseScope(const FrameworkPhaseScope &)                     = delete;
    auto operator=(const FrameworkPhaseScope &) -> FrameworkPhaseScope & = delete;

  private:
    std::shared_ptr<FrameworkSubject> subject_;
    FrameworkPhase                    phase_;
    bool                              fixture_;
    std::string_view                  name_;
};

GENTEST_RUNTIME_API void inherit_framework_context(TestContextInfo &context);
GENTEST_RUNTIME_API void start_framework_case(TestContextInfo &context, std::string_view name);
GENTEST_RUNTIME_API void finish_framework_case(TestContextInfo &context) noexcept;
GENTEST_RUNTIME_API void framework_suspend(const std::shared_ptr<TestContextInfo> &context, SuspensionKind kind,
                                           const std::source_location &location) noexcept;
GENTEST_RUNTIME_API auto context_notifier(const std::shared_ptr<TestContextInfo> &context) noexcept -> Notifier;
} // namespace detail
} // namespace gentest
