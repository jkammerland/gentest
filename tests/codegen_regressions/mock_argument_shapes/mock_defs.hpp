#pragma once

#include "gentest/mock.h"
#include "types.hpp"

namespace fixture::mocks {
using SinkMock = gentest::mock<fixture::Sink>;
using TextMock = gentest::mock<fixture::Text>;
} // namespace fixture::mocks
