#pragma once

#include "model.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace gentest::codegen::mock_manifest {

struct ReadResult {
    std::vector<MockClassInfo> mocks;
    std::string                error;
};

[[nodiscard]] std::string serialize(std::vector<MockClassInfo> mocks);
[[nodiscard]] bool        write(const std::filesystem::path &path, const std::vector<MockClassInfo> &mocks, std::string &error);
[[nodiscard]] ReadResult  read(const std::filesystem::path &path);

} // namespace gentest::codegen::mock_manifest
