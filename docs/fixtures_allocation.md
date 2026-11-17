# Fixture allocation hooks

By default gentest creates each fixture with `new T()` and owns the instance.
Give the fixture type a static `gentest_allocate()` function when you need to
construct it differently: pass constructor arguments, release it with your own
deleter, or share ownership. The hook decides how an instance is created and
released. The fixture's lifetime (local, suite or global) decides how long it
lives; see [Fixtures](fixtures.md).

## Add a hook

`Connection` has no default constructor, so gentest can't create it with
`new T()`. The hook constructs it instead:

```cpp
#pragma once

#include "gentest/runner.h"

#include <memory>
#include <string>
#include <utility>

namespace alloc {

struct Connection {
    explicit Connection(std::string host) : host(std::move(host)) {}

    static std::unique_ptr<Connection> gentest_allocate() { return std::make_unique<Connection>("localhost"); }

    std::string host;
};

[[gentest::test]]
inline void connects_to_localhost(Connection &connection) {
    gentest::expect_eq(connection.host, "localhost");
}

} // namespace alloc
```

## Hook signatures

The hook is a static member function of the fixture type, in one of two forms:

- `static R gentest_allocate();`
- `static R gentest_allocate(std::string_view suite);`

`R` is one of the return types in the next section. gentest looks the hook up by
calling `T::gentest_allocate()` or `T::gentest_allocate(std::string_view{})`. A
function it can't call that way, for example a non-static member function or
one that takes a `const std::string&`, is ignored without a diagnostic: gentest
falls back to `new T()`, which fails to compile only if `T` has no default
constructor.

## Return types

| Return type | Ownership |
|---|---|
| `std::unique_ptr<T>` | gentest owns the instance and deletes it. |
| `std::unique_ptr<T, D>` | gentest owns the instance and releases it with your deleter `D`. |
| `std::shared_ptr<T>` | gentest shares ownership with any other holder of the pointer. |
| `T*` | The object must come from `new`. gentest adopts it and deletes it, as if the hook had returned `std::unique_ptr<T>`. |

The pointer may point to a class derived from `T`. gentest destroys the object
through the type the hook returned, so a hook that returns `Derived*` for a
fixture `Base` deletes a `Derived`, even if `Base` has no virtual destructor.
Any other return type fails to compile with
`gentest_allocate must return std::unique_ptr<T, D>, std::shared_ptr<T>, or T*`.

## Which hook gentest calls

| Lifetime | Only `gentest_allocate()` | Only `gentest_allocate(std::string_view)` | Both |
|---|---|---|---|
| local | `gentest_allocate()` | called with an empty string | `gentest_allocate()` |
| suite | `gentest_allocate()` | called with the fixture's suite name | `gentest_allocate(std::string_view)`, with the suite name |
| global | `gentest_allocate()` | called with an empty string | `gentest_allocate()` |

So a single suite-aware hook serves every lifetime. The suite name is the
fixture's namespace path joined with `/`, for example `fx/shared` for a fixture
in `namespace fx::shared`; see [Writing tests](writing_tests.md).

## Null results and exceptions

A null result (an empty `std::unique_ptr` or `std::shared_ptr`, or `nullptr`) is
an allocation failure, and so is an exception from the hook or the constructor:

- For a local fixture, the case fails with
  `fixture allocation failed for '<type>': returned null`, or with
  `unexpected std::exception: <message>` for an exception.
- For a shared fixture, gentest prints
  `gentest: fixture '<type>' fixture allocation returned null` or
  `gentest: fixture '<type>' fixture construction threw std::exception: <message>`,
  reports every case that needs the fixture as blocked, and fails the run.

There is no instance, so nothing is torn down. [Fixtures](fixtures.md) lists
the other ways a fixture can fail.

## How parameters receive the instance

Every [parameter form](fixtures.md) works with every return type:

- `T&` and `T*` refer to the instance the hook created.
- `std::shared_ptr<T>` shares ownership of it. If the hook returned unique
  ownership (`std::unique_ptr` or `T*`), gentest converts it to shared
  ownership the first time a `std::shared_ptr<T>` parameter needs it, and keeps
  your deleter.

This is the same for local, suite and global fixtures, and for the fixture
class of a deprecated member test.

## Examples

### `shared_ptr` allocation

```cpp
#pragma once

#include "gentest/runner.h"

#include <memory>

namespace alloc_shared {

struct SharedFx {
    static std::shared_ptr<SharedFx> gentest_allocate() { return std::make_shared<SharedFx>(); }
};

[[gentest::test]]
inline void shared_instance(std::shared_ptr<SharedFx> fx) {
    gentest::expect(fx != nullptr, "the case shares ownership of the instance");
}

} // namespace alloc_shared
```

### `unique_ptr` with a custom deleter

```cpp
#pragma once

#include "gentest/runner.h"

#include <memory>

namespace alloc_deleter {

struct CustomFx {
    struct Deleter {
        // Runs when gentest releases the fixture.
        void operator()(CustomFx *ptr) const { delete ptr; }
    };

    static std::unique_ptr<CustomFx, Deleter> gentest_allocate() {
        return std::unique_ptr<CustomFx, Deleter>(new CustomFx(), Deleter{});
    }
};

[[gentest::test]]
inline void custom_deleter(CustomFx &fx) {
    static_cast<void>(fx);
}

} // namespace alloc_deleter
```

### Suite-aware allocation

A suite fixture must be declared in a named namespace; see
[Fixtures](fixtures.md).

```cpp
#pragma once

#include "gentest/runner.h"

#include <memory>
#include <string>
#include <string_view>

namespace suite_fx {

struct [[gentest::fixture(suite)]] SuiteFx {
    std::string created_for;

    static std::unique_ptr<SuiteFx> gentest_allocate(std::string_view suite) {
        // suite is the fixture's suite name, here "suite_fx".
        auto fx         = std::make_unique<SuiteFx>();
        fx->created_for = std::string(suite);
        return fx;
    }
};

[[gentest::test]]
inline void knows_its_suite(SuiteFx &fx) {
    gentest::expect_eq(fx.created_for, "suite_fx");
}

} // namespace suite_fx
```

### Raw pointer allocation

```cpp
#pragma once

#include "gentest/runner.h"

namespace alloc_raw {

struct RawFx {
    // gentest adopts the object and deletes it.
    static RawFx *gentest_allocate() { return new RawFx(); }
};

[[gentest::test]]
inline void raw_pointer(RawFx *fx) {
    gentest::expect(fx != nullptr, "gentest passes the adopted instance");
}

} // namespace alloc_raw
```
