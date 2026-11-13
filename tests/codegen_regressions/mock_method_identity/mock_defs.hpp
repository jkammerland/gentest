#pragma once

#include "gentest/mock.h"
#include "types.hpp"

namespace fixture::mocks {
using StreamMock           = gentest::mock<fixture::IStream>;
using BothMock             = gentest::mock<fixture::IBoth>;
using InheritedServiceMock = gentest::mock<fixture::InheritedService>;
} // namespace fixture::mocks
