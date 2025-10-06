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

// Render generated mocks; returns empty optional if mocks.size()==0.
std::optional<MockOutputs> render_mocks(const CollectorOptions &options, const std::vector<MockClassInfo> &mocks);

} // namespace gentest::codegen::render
