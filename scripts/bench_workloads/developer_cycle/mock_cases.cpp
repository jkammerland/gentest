#include "mock_cases.hpp"

#include "gentest/test.h"
#include "public/cycle_mocks.hpp"

namespace cycle {

void mockCharge() {
    app::mocks::LedgerMock ledger;
    gentest::expect(ledger, &app::Ledger::debit).times(1).with(100).returns(true);

    gentest::expect_true(app::charge(ledger, 200));
}

} // namespace cycle
