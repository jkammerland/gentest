// Emission for generated registration/mock artifacts.
#pragma once

#include "model.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace gentest::codegen {

// Validate the complete generated-artifact output plan before any files are
// written. Returns false and sets `error` for invalid slots or path collisions.
bool validate_generated_artifact_outputs(const CollectorOptions &options, std::string &error);

// Atomically replace an output only when its contents differ. Returns false
// after logging a write failure.
bool write_file_atomic_if_changed(const std::filesystem::path &path, std::string_view content);

// Write generated registration/mock artifacts. Returns 0 on success.
int emit(const CollectorOptions &options, const std::vector<TestCaseInfo> &cases, const std::vector<FixtureDeclInfo> &fixtures,
         const std::vector<MockClassInfo> &mocks);

} // namespace gentest::codegen
