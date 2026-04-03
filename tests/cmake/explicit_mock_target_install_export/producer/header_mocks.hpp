#pragma once

#include <fixture/a+b.hpp>
#include <fixture/generated_support.hpp>
#include <fixture/service.hpp>

namespace fixture::mocks {

using ServiceMock = gentest::mock<fixture::Service>;

} // namespace fixture::mocks
