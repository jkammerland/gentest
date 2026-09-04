#include "gentest/detail/runtime_context.h"
#include "recording_internal.h"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>

namespace gentest::detail {
namespace {
thread_local std::shared_ptr<RecordingSession> active_session;
thread_local std::shared_ptr<RecordingTarget>  active_target;

RecordingBag *destination(RecordScope scope, const std::source_location &loc) {
    require_owner_context("recording called");
    if (bench_phase() == BenchPhase::Call) {
        record_error("recording is forbidden in a timed bench/jitter call; use fixture setup/teardown", loc);
        return nullptr;
    }
    const auto target = current_test()->recording;
    if (!target || !target->session || !target->session->active) {
        record_error("recording requires an active runner invocation", loc);
        return nullptr;
    }
    if (scope == RecordScope::Current)
        scope = target->current;
    switch (scope) {
    case RecordScope::Case:
        if (target->occurrence)
            return &target->occurrence->data;
        break;
    case RecordScope::Suite: return &target->session->suites[target->suite];
    case RecordScope::Run: return &target->session->run;
    case RecordScope::Current: break;
    }
    record_error("recording scope is unavailable in this hook", loc);
    return nullptr;
}
} // namespace

RecordingRunScope::RecordingRunScope() : session(std::make_shared<RecordingSession>()), previous_(active_session) {
    active_session = session;
}
RecordingRunScope::~RecordingRunScope() {
    session->active = false;
    active_session  = std::move(previous_);
}
RecordingTargetScope::RecordingTargetScope(std::shared_ptr<RecordingTarget> target) : previous_(active_target) {
    active_target = std::move(target);
}
RecordingTargetScope::~RecordingTargetScope() { active_target = std::move(previous_); }
std::shared_ptr<RecordingTarget> current_recording_target() { return active_target; }

std::shared_ptr<RecordingTarget> make_case_recording(const Case &c) {
    if (!active_session)
        return {};
    auto occurrence   = std::make_shared<CaseRecording>();
    occurrence->id    = active_session->cases.size();
    occurrence->name  = c.name;
    occurrence->suite = c.suite;
    occurrence->file  = c.file;
    occurrence->line  = c.line;
    occurrence->kind  = c.is_benchmark ? "bench" : c.is_jitter ? "jitter" : "test";
    occurrence->owner = c.owner;
    for (auto v : c.tags)
        occurrence->tags.emplace_back(v);
    for (auto v : c.requirements)
        occurrence->requirements.emplace_back(v);
    active_session->cases.push_back(occurrence);
    return std::make_shared<RecordingTarget>(RecordingTarget{active_session, occurrence, std::string(c.suite), RecordScope::Case});
}

std::shared_ptr<RecordingTarget> make_fixture_recording(std::string_view suite, bool global) {
    if (!active_session)
        return {};
    if (!global)
        active_session->suites.try_emplace(std::string(suite));
    return std::make_shared<RecordingTarget>(
        RecordingTarget{active_session, {}, std::string(suite), global ? RecordScope::Run : RecordScope::Suite});
}

bool recording_suite_matches(std::string_view parent, std::string_view child) {
    if (parent.empty() || parent == child)
        return true;
    if (!child.starts_with(parent))
        return false;
    const auto suffix = child.substr(parent.size());
    return suffix.starts_with('/') || suffix.starts_with("::");
}

std::vector<const RecordingBag *> recording_bags(const RecordingSession &session, const CaseRecording &c) {
    std::vector<std::pair<std::string_view, const RecordingBag *>> scopes;
    for (const auto &[suite, data] : session.suites)
        if (recording_suite_matches(suite, c.suite))
            scopes.emplace_back(suite, &data);
    std::sort(scopes.begin(), scopes.end(), [](const auto &a, const auto &b) { return a.first.size() < b.first.size(); });
    std::vector<const RecordingBag *> result{&session.run};
    for (const auto &scope : scopes)
        result.push_back(scope.second);
    result.push_back(&c.data);
    return result;
}

auto effective_properties(const RecordingSession &session, const CaseRecording &c) -> std::map<std::string, PropertyValue, std::less<>> {
    std::map<std::string, PropertyValue, std::less<>> result;
    for (auto bag : recording_bags(session, c))
        for (const auto &[key, value] : bag->properties)
            result.insert_or_assign(key, value);
    return result;
}

std::string property_text(const PropertyValue &property) {
    return std::visit(
        [](const auto &v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>)
                return "null";
            else if constexpr (std::is_same_v<T, std::string>)
                return v;
            else
                return fmt::format("{}", v);
        },
        property.value);
}

void record_error(std::string_view message, const std::source_location &loc) { record_failure(fmt::format("recording: {}", message), loc); }

bool record_ready(std::string_view name, std::string_view content_type, RecordOptions options, const std::source_location &loc) {
    if (!destination(options.scope, loc))
        return false;
    if (name.empty() || content_type.empty()) {
        record_error("name and content type must be nonempty", loc);
        return false;
    }
    return true;
}
} // namespace gentest::detail

namespace gentest {
void record_property(std::string_view key, PropertyValue value, RecordScope scope, const std::source_location &loc) {
    auto *bag = detail::destination(scope, loc);
    if (!bag)
        return;
    if (key.empty()) {
        detail::record_error("property key must be nonempty", loc);
        return;
    }
    if (const auto *number = std::get_if<double>(&value.value); number && !std::isfinite(*number)) {
        detail::record_error("scalar floating-point properties must be finite", loc);
        return;
    }
    bag->properties.insert_or_assign(std::string(key), std::move(value));
}

void record_data(std::string_view name, std::span<const std::byte> bytes, std::string_view content_type, RecordOptions options,
                 const std::source_location &loc) {
    if (!detail::record_ready(name, content_type, options, loc))
        return;
    auto       *bag = detail::destination(options.scope, loc);
    std::string owned;
    if (!bytes.empty())
        owned.assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    bag->records.push_back({std::string(name), std::string(content_type), std::string(options.schema), std::move(owned)});
}
} // namespace gentest
