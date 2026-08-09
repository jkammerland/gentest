// Emission for generated registration/mock artifacts.
#pragma once

#include "model.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace gentest::codegen {

// Validate the complete generated-artifact output plan before any files are
// written. Returns false and sets `error` for invalid slots or path collisions.
bool validate_generated_artifact_outputs(const CollectorOptions &options, std::string &error);

// An input or derived path discovered after the static artifact plan is
// validated. These paths are reserved only for the optional timing sidecar so
// existing generated-output behavior remains unchanged.
struct TimingJsonProtectedPath {
    std::filesystem::path path;
    std::string           description;
};

// Validate that the optional timing sidecar does not replace a dependency
// discovered while parsing or a derived tool/module artifact. This
// intentionally leaves non-timing output behavior unchanged.
bool validate_timing_json_dependency_collision(const CollectorOptions &options, const std::vector<std::string> &dependencies,
                                               const std::vector<TimingJsonProtectedPath> &protected_paths, std::string &error);

// Atomically replace an output only when its contents differ. Returns false
// after logging a write failure.
bool write_file_atomic_if_changed(const std::filesystem::path &path, std::string_view content);

struct ModuleMockRenderTiming {
    std::chrono::steady_clock::time_point api_include_started;
    std::chrono::steady_clock::time_point api_include_finished;
    std::chrono::steady_clock::time_point attachment_started;
    std::chrono::steady_clock::time_point attachment_finished;
    std::filesystem::path                 registration_output;
    std::string                           module_name;
    bool                                  recorded_api_include = false;
    bool                                  recorded_attachments = false;
};

// Write generated registration artifacts (TU headers, wrappers, and manifests).
// When requested, reports only the mock-attachment render spans embedded in
// same-module registration units. Returns 0 on success.
int emit_registration_outputs(const CollectorOptions &options, const std::vector<TestCaseInfo> &cases,
                              const std::vector<FixtureDeclInfo> &fixtures, const std::vector<MockClassInfo> &mocks,
                              std::vector<ModuleMockRenderTiming> *module_mock_render_timings = nullptr);

// Write generated mock artifacts. Returns 0 on success.
int emit_mock_outputs(const CollectorOptions &options, const std::vector<MockClassInfo> &mocks);

// Write all generated registration and mock artifacts. Returns 0 on success.
int emit(const CollectorOptions &options, const std::vector<TestCaseInfo> &cases, const std::vector<FixtureDeclInfo> &fixtures,
         const std::vector<MockClassInfo> &mocks);

} // namespace gentest::codegen
