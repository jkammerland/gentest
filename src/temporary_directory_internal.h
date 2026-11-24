#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gentest::detail {

struct TemporaryDirectoryState;
using TemporaryDirectories = std::vector<std::shared_ptr<TemporaryDirectoryState>>;

auto finishTemporaryDirectories(TemporaryDirectories &directories, bool failed, std::string_view case_name) -> std::vector<std::string>;

} // namespace gentest::detail
