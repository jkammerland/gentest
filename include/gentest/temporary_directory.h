#pragma once

#include "gentest/detail/fixture_api.h"
#include "gentest/detail/runtime_export.h"

#include <filesystem>
#include <memory>

namespace gentest {
namespace detail {
struct TemporaryDirectoryState;
}

// Owns a private temporary directory for one test or measurement.
class TemporaryDirectory final : public FixtureSetup, public FixtureTearDown {
  public:
    TemporaryDirectory()                                      = default;
    TemporaryDirectory(const TemporaryDirectory &)            = delete;
    TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;
    GENTEST_RUNTIME_API ~TemporaryDirectory() override;

    GENTEST_RUNTIME_API void               setUp() override;
    GENTEST_RUNTIME_API void               tearDown() override;
    [[nodiscard]] GENTEST_RUNTIME_API auto path() const noexcept -> const std::filesystem::path &;

    void keepOnFailure() noexcept { keep_on_failure_ = true; }

  private:
    std::shared_ptr<detail::TemporaryDirectoryState> directory_;
    bool                                             keep_on_failure_ = false;
};

} // namespace gentest
