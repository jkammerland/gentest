#pragma once

#include "gentest/attributes.h"
#include "gentest/runner.h"
#include "ledger.hpp"
#include "public/pragma_once_mocks.hpp"

#include <app/account.hpp>

[[using gentest: test("explicit_mock_target/pragma_once_production_first")]]
inline void pragma_once_production_first() {
    using namespace gentest::asserts;

    app::mocks::LedgerMock ledger;
    gentest::expect(ledger, &app::Ledger::debit).times(1).with(100).returns(true);
    app::Ledger *ledger_api = &ledger;
    EXPECT_EQ(ledger_api->debit(100), true);

    app::mocks::AccountMock account;
    gentest::expect(account, &app::Account::balance).times(1).returns(42);
    const app::Account *account_api = &account;
    EXPECT_EQ(account_api->balance(), 42);
}
