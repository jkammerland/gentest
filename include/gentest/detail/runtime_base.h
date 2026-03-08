#pragma once

#include <stdexcept>
#include <string>
#include <utility>

#ifndef GENTEST_RUNTIME_API
#if defined(GENTEST_RUNTIME_SHARED)
#if defined(_WIN32)
#if defined(GENTEST_RUNTIME_BUILDING)
#define GENTEST_RUNTIME_API __declspec(dllexport)
#else
#define GENTEST_RUNTIME_API __declspec(dllimport)
#endif
#else
#define GENTEST_RUNTIME_API __attribute__((visibility("default")))
#endif
#else
#define GENTEST_RUNTIME_API
#endif
#endif

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#define GENTEST_EXCEPTIONS_ENABLED 1
#else
#define GENTEST_EXCEPTIONS_ENABLED 0
#endif

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
