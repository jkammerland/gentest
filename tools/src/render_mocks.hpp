#pragma once

#include "model.hpp"

#include <optional>
#include <string>
#include <vector>

namespace gentest::codegen::render {

struct MockOutputs {
    std::string registry_header;
    std::string implementation_unit;
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

} // namespace gentest::codegen::render
