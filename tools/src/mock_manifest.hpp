#pragma once

#include "model.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace gentest::codegen::mock_manifest {

struct ReadResult {
    std::vector<MockClassInfo> mocks;
    std::vector<std::string>   mock_output_domain_modules;
    std::string                error;
};

[[nodiscard]] std::string serialize(std::vector<MockClassInfo> mocks, std::span<const std::string> mock_output_domain_modules = {});
[[nodiscard]] bool        write(const std::filesystem::path &path, const std::vector<MockClassInfo> &mocks,
                                std::span<const std::string> mock_output_domain_modules, std::string &error);
// Parse an in-memory manifest. This is shared with the textual parse cache so
// cached mocks use precisely the same schema validation as emitted manifests.
[[nodiscard]] ReadResult parse(std::string_view content);
[[nodiscard]] ReadResult read(const std::filesystem::path &path);

} // namespace gentest::codegen::mock_manifest
