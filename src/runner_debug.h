#pragma once

namespace gentest::detail {

// Owner assertions execute on the runner thread, including coroutine resumes.
class AssertionBreakScope {
  public:
    explicit AssertionBreakScope(bool enabled) noexcept;
    AssertionBreakScope(const AssertionBreakScope &)            = delete;
    AssertionBreakScope &operator=(const AssertionBreakScope &) = delete;
    ~AssertionBreakScope();

  private:
    bool previous_ = false;
};

bool assertion_break_supported() noexcept;
void break_on_assertion_failure() noexcept;

} // namespace gentest::detail
