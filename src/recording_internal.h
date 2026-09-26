#pragma once

#include "gentest/record.h"
#include "gentest/registry.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gentest::detail {

struct RecordedPayload {
    std::string name;
    std::string content_type;
    std::string schema;
    std::string bytes;
};

struct RecordingBag {
    std::map<std::string, PropertyValue, std::less<>> properties;
    std::vector<RecordedPayload>                      records;
};

struct CaseRecording {
    RecordingBag             data;
    std::size_t              id = 0;
    std::string              name, suite, file, kind, owner;
    unsigned                 line = 0;
    std::vector<std::string> requirements, tags;
    std::string              outcome = "pending";
};

struct RecordingSession {
    RecordingBag                                     run;
    std::map<std::string, RecordingBag, std::less<>> suites;
    std::vector<std::shared_ptr<CaseRecording>>      cases;
    bool                                             active = true;
};

struct RecordingTarget {
    std::shared_ptr<RecordingSession> session;
    std::shared_ptr<CaseRecording>    occurrence;
    std::string                       suite;
    RecordScope                       current = RecordScope::Run;
};

std::shared_ptr<RecordingTarget> current_recording_target();
std::shared_ptr<RecordingTarget> make_case_recording(const Case &c);
std::shared_ptr<RecordingTarget> make_fixture_recording(std::string_view suite, bool global);

class RecordingRunScope {
  public:
    RecordingRunScope();
    ~RecordingRunScope();
    RecordingRunScope(const RecordingRunScope &)                           = delete;
    RecordingRunScope                &operator=(const RecordingRunScope &) = delete;
    std::shared_ptr<RecordingSession> session;

  private:
    std::shared_ptr<RecordingSession> previous_;
};

class RecordingTargetScope {
  public:
    explicit RecordingTargetScope(std::shared_ptr<RecordingTarget> target);
    ~RecordingTargetScope();
    RecordingTargetScope(const RecordingTargetScope &)            = delete;
    RecordingTargetScope &operator=(const RecordingTargetScope &) = delete;

  private:
    std::shared_ptr<RecordingTarget> previous_;
};

bool                                              recording_suite_matches(std::string_view parent, std::string_view child);
std::vector<const RecordingBag *>                 recording_bags(const RecordingSession &session, const CaseRecording &c);
std::map<std::string, PropertyValue, std::less<>> effective_properties(const RecordingSession &session, const CaseRecording &c);
std::string                                       property_text(const PropertyValue &value);
std::string                                       recording_json_string(std::string_view text);

} // namespace gentest::detail
