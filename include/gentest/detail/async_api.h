#pragma once

// Lightweight async ABI declarations shared by Case and generated synchronous
// registrations. Include gentest/async.h when implementing or invoking
// coroutine tests.

#include <memory>

namespace gentest::detail {

class AsyncTask;

using AsyncTaskPtr = std::unique_ptr<AsyncTask>;
using AsyncCaseFn  = AsyncTaskPtr (*)(void *);

} // namespace gentest::detail
