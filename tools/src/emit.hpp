// Emission for generated registration/mock artifacts.
#pragma once

#include "model.hpp"

#include <vector>

namespace gentest::codegen {

// Write generated registration/mock artifacts. Returns 0 on success.
int emit(const CollectorOptions &options, const std::vector<TestCaseInfo> &cases, const std::vector<FixtureDeclInfo> &fixtures,
         const std::vector<MockClassInfo> &mocks);

} // namespace gentest::codegen
