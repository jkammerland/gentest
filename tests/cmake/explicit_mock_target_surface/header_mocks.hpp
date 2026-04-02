#pragma once

#include "service.hpp"

namespace fixture::mocks {

using ServiceMock            = gentest::mock<fixture::Service>;
using QuoteTaggedServiceMock = gentest::mock<fixture::TaggedService<'\"'>>;
using SlashTaggedServiceMock = gentest::mock<fixture::TaggedService<'\\'>>;

} // namespace fixture::mocks
