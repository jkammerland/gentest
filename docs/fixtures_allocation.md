# Fixture Allocation Hooks

Gentest supports allocating fixtures via an optional static hook so you can
return heap-owned fixtures (unique/shared) instead of relying on stack
construction. This is useful for fixtures that require virtual inheritance,
custom allocation, or shared ownership across tests.

This document describes the allocation hook, supported return types, and how
fixture arguments are wired for free-function fixtures and member fixtures.

## Parameter-based fixture inference

- Fixture arguments are inferred from test/bench/jitter function signatures.
- Any parameter not listed by `parameters(...)`, `parameters_pack(...)`,
  `range(...)`, `linspace(...)`, `geom(...)`, or `logspace(...)` is treated as
  a fixture argument.
- Trailing C++ default-argument parameters are treated as normal defaulted
  values (not fixture-inferred).
- This rule applies to both free-function cases and member test methods.
- Legacy `[[using gentest: fixtures(...)]]` is removed and rejected by
  `gentest_codegen`.

## API: `gentest_allocate`

Define a static method on your fixture:

```
static auto gentest_allocate();
// Optional: suite-aware allocation for suite fixtures
static auto gentest_allocate(std::string_view suite);
```

Supported return types:
- `std::unique_ptr<Fixture>` (custom deleters supported)
- `std::shared_ptr<Fixture>`
- `Fixture*` (adopted into a `std::unique_ptr`)

If the hook is not present, gentest falls back to `std::make_unique<Fixture>()`.
If you only provide the suite-aware overload, non-suite fixtures pass an empty
`std::string_view` so you can reuse the same hook for all lifetimes.

Returning a null pointer (empty `unique_ptr` / `shared_ptr` or `nullptr`) causes
the test to fail with a fixture-allocation failure message.

## How arguments are provided

Gentest uses a single internal `FixtureHandle<T>` that stores either:
- a `std::unique_ptr<T>` (default), and
- optionally a `std::shared_ptr<T>` (when needed).

Fixture argument forms are:
- `T&`
- `T*`
- `std::shared_ptr<T>`

`std::unique_ptr<T>` is an allocation-hook return form, not a test-parameter
form.

When the test wrapper is invoked:
- `T&` arguments receive `*handle.get()`.
- `T*` arguments receive `handle.get()`.
- `std::shared_ptr<T>` arguments receive `handle.shared()`, which returns the
  existing shared pointer or promotes the unique pointer into a shared pointer.

This behavior is uniform across:
- free-function fixture arguments
- member test method fixture arguments
- suite fixtures (`[[using gentest: fixture(suite)]]`)
- global fixtures (`[[using gentest: fixture(global)]]`)

## Examples

### Unique allocation (default)

```cpp
struct MyFx {
    static std::unique_ptr<MyFx> gentest_allocate() {
        return std::make_unique<MyFx>();
    }
};

[[using gentest: test("free/unique")]]
void free_unique(MyFx& fx) {
    // uses the unique instance
}
```

### Shared allocation

```cpp
struct SharedFx {
    static std::shared_ptr<SharedFx> gentest_allocate() {
        return std::make_shared<SharedFx>();
    }
};

[[using gentest: test("free/shared")]]
void free_shared(std::shared_ptr<SharedFx> fx) {
    // shared ownership of the fixture instance
}
```

### Unique allocation with custom deleter

```cpp
struct CustomFx {
    struct Deleter {
        void operator()(CustomFx* ptr) const { delete ptr; }
    };

    static std::unique_ptr<CustomFx, Deleter> gentest_allocate() {
        return std::unique_ptr<CustomFx, Deleter>(new CustomFx(), Deleter{});
    }
};
```

### Suite-aware allocation

```cpp
struct [[using gentest: fixture(suite)]] SuiteFx {
    static std::unique_ptr<SuiteFx> gentest_allocate(std::string_view suite) {
        // suite is the prefix in test names (e.g. "suite_fx")
        return std::make_unique<SuiteFx>();
    }
};
```

### Raw pointer allocation (adopted)

```cpp
struct RawFx {
    static RawFx* gentest_allocate() {
        return new RawFx();
    }
};

[[using gentest: test("free/pointer")]]
void free_pointer(RawFx* fx) {
    // gentest adopts and owns the pointer
}
```

## Notes and constraints

- `gentest_allocate()` must be `static` and callable without arguments.
- If both overloads exist, suite fixtures use the suite-aware hook while global
  and ephemeral fixtures use the no-arg hook. If only the suite-aware overload
  exists, other lifetimes call it with an empty string.
- If you return `Fixture*`, it must be heap allocated. Gentest adopts it and
  deletes it via `std::unique_ptr` (same ownership/lifetime semantics as
  returning `std::unique_ptr<Fixture>` yourself).
- `std::shared_ptr<T>` arguments promote the unique ownership into shared
  ownership on first use.
- Fixture type aliases (e.g. `using Fx = RealFixture`, alias pointer/reference
  forms, and `using FxPtr = std::shared_ptr<RealFixture>`) resolve to the
  underlying fixture type.
- Suite/global fixtures are constructed once (per suite or per process) and
  reused across tests. Their `setUp`/`tearDown` hooks run once at the start/end
  of the test run, not per test.
- Shared fixture lookup is intentionally non-reentrant during setup:
  `get_shared_fixture(...)` returns `nullptr` and sets `error` to
  `"fixture initialization in progress"` until setup completes.
  Do not depend on partially initialized shared fixtures from setup paths.
