#pragma once

#include "third_party_nested_service.hpp"

namespace fixture::validation {

using NestedInnerMock = gentest::mock<Outer::Inner>;

} // namespace fixture::validation
