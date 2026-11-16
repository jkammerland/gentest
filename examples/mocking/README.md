# Checkout with a generated payment mock

This example tests a checkout service against a generated mock of its payment
gateway. It needs no hand-written mock, no custom `main()`, no network access
and no third-party mock framework.

Build it against an installed gentest (see the [examples index](../README.md)
for requirements):

```bash
cmake -S examples/mocking -B build/examples/mocking -G Ninja -DCMAKE_PREFIX_PATH=/path/to/gentest/install
cmake --build build/examples/mocking
ctest --test-dir build/examples/mocking --output-on-failure
build/examples/mocking/gentest_mocking --list-tests
build/examples/mocking/gentest_mocking --repeat=2 --shuffle --seed 123
```

## How it fits together

- [payment.hpp](payment.hpp) holds the production code: the `PaymentGateway`
  interface and the `Checkout` service that calls it.
- [payment_mocks.hpp](payment_mocks.hpp) is the mock definitions header
  (`DEFS`). It declares `shop::mocks::PaymentGatewayMock` as
  `gentest::mock<shop::PaymentGateway>`.
- [CMakeLists.txt](CMakeLists.txt) creates the mock target `payment_mocks`
  with `gentest_add_mocks()`, which generates the mock header
  `public/payment_mocks.hpp`. It links the mock target to the test target with
  [`gentest_link_mocks()`](../../docs/mock_generation.md#link-mock-targets),
  so the test's codegen runs after the generated mock header exists.
- [cases.hpp](cases.hpp) includes `public/payment_mocks.hpp` and passes the mock
  to `Checkout` through its `PaymentGateway&` dependency.

## What the cases check

The four cases cover a successful payment, a declined payment, and zero and
negative amounts. A valid request must call the gateway exactly once with the
expected amount. An invalid request must never call it (`times(0)`). A declined
payment is a normal service outcome, so its case passes when `pay()` returns
`false`.

To see a failure, change the expectation in `payment_succeeds` from `times(1)`
to `times(2)`, rebuild, and run that case:

```bash
build/examples/mocking/gentest_mocking --run=mocking/payment_succeeds
```

The case fails with a message that starts
`expected 2 call(s) to ::shop::PaymentGateway::charge but observed 1`, and the
executable exits with a nonzero status. Restore the expectation before you run
CTest again.

[expected_tests.txt](expected_tests.txt) lists the exact case names, which the
repository's installed-package test checks. All cases pass.

For expectations, matchers, the gMock and Trompeloeil backends and the rest of
the mock API, see [Mocks](../../docs/mock_generation.md).
