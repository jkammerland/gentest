#pragma once

#include "model.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gentest::codegen::render {

struct MockGeneratedFile {
    std::filesystem::path path;
    std::string           content;
};

struct MockOutputs {
    std::string                    registry_header;
    std::string                    implementation_unit;
    std::vector<MockGeneratedFile> additional_files;
};

struct MockRenderResult {
    std::optional<MockOutputs> outputs;
    std::string                error;
};

// Render generated mocks.
// - outputs == std::nullopt and error.empty(): no mocks discovered
// - outputs.has_value() and error.empty(): success
// - !error.empty(): rendering failed
MockRenderResult render_mocks(const CollectorOptions &options, const std::vector<MockClassInfo> &mocks);

// Render a single module-local mock attachment block suitable for textual
// injection into a generated named-module source wrapper.
std::string render_module_mock_attachment(const MockClassInfo &mock);

} // namespace gentest::codegen::render
