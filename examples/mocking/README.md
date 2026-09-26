# Checkout with a generated payment mock

`payment.hpp` contains the production interface and checkout service.
`payment_mocks.hpp` declares the mock marker. CMake generates
`public/payment_mocks.hpp`; tests include that generated surface and call the
service through its `PaymentGateway&` dependency.

Four cases cover successful payment, a declined payment, and zero/negative
amounts. Valid requests must call the gateway exactly once with the expected
amount. Invalid requests must never call it (`times(0)`). A declined payment is
a normal service outcome, so its test passes when the service returns false.

The mock target must be linked before `gentest_attach_codegen()`. No handwritten
mock, custom main, network access, or third-party mock framework is required.

To inspect a failure, temporarily change the success expectation from
`times(1)` to `times(2)`, rebuild, and run `--run=mocking/payment_succeeds`.
The unmet expectation should fail the case and return nonzero. Restore the
expectation before running CTest again.

Build against an installed gentest package (CMake 3.31+, Ninja, C++20, and a
working host codegen tool):

```sh
cmake -S examples/mocking -B build/examples/mocking -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/gentest/install
cmake --build build/examples/mocking
ctest --test-dir build/examples/mocking --output-on-failure

./build/examples/mocking/gentest_mocking --list-tests
./build/examples/mocking/gentest_mocking --repeat=2 --shuffle --seed 123
```

[`expected_tests.txt`](expected_tests.txt) records the exact resolved case names
checked by the repository's installed-package smoke test. All cases pass by
default. See the [examples index](../README.md) for installation links.
