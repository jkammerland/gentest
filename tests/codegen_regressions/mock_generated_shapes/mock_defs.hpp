#pragma once

#include "gentest/mock.h"
#include "types.hpp"

namespace fixture::mocks {
using AuthorizerMock = gentest::mock<fixture::Authorizer>;
using EmitterMock    = gentest::mock<fixture::Emitter>;
using BusMock        = gentest::mock<fixture::Bus>;
using SlotsMock      = gentest::mock<fixture::Slots>;
} // namespace fixture::mocks
