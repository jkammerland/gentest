#pragma once

#include "gentest/mock_fwd.h"
#include "ledger.hpp"

#include <app/account.hpp>

namespace app::mocks {

using LedgerMock  = gentest::mock<app::Ledger>;
using AccountMock = gentest::mock<app::Account>;

} // namespace app::mocks
