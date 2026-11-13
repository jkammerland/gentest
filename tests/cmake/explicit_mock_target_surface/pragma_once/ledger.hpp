#pragma once

// Deliberately #pragma once without an include guard (#239).
namespace app {

struct Ledger {
    virtual ~Ledger()             = default;
    virtual bool debit(int cents) = 0;
};

} // namespace app
