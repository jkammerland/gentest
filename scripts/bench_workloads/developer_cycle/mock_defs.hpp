#pragma once

#include "gentest/mock_fwd.h"
#include "production.hpp"

namespace app::mocks {
using LedgerMock = gentest::mock<app::Ledger>;
} // namespace app::mocks
