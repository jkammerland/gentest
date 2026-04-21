#pragma once

#include "gentest/detail/runtime_config.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace gentest {

class failure : public std::runtime_error {
  public:
    explicit failure(const std::string &message) : std::runtime_error(message) {}
};

// Fatal assertion exception that is NOT derived from std::exception.
// This is intentionally separate from `failure` so we fully control the
// exception boundary. Destructors still run during stack unwinding.
class assertion {
  public:
    explicit assertion(std::string message) : msg_(std::move(message)) {}
    const std::string &message() const { return msg_; }

  private:
    std::string msg_;
};

} // namespace gentest
