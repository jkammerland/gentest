# Fixture Allocation Hooks

Gentest allocates fixtures on the heap with unique ownership by default. An
optional static allocation hook customizes construction, allocation, deleters,
or shared ownership. Fixture lifetime is selected separately by the local,
suite, or global lifetime policy.

This document describes the allocation hook, supported return types, and how
fixture arguments are wired for free-function fixtures (recommended) and
deprecated member fixtures.

## Parameter-based fixture inference

- Prefer free-function tests/benches/jitters in new code. Member tests are
  deprecated and should be avoided.
- Fixture arguments are inferred from test/bench/jitter function signatures.
- Any parameter not listed by `parameters(...)`, `parameters_pack(...)`,
  `range(...)`, `linspace(...)`, `geom(...)`, or `logspace(...)` is treated as
  a fixture argument.
- Trailing C++ default-argument parameters are treated as normal defaulted
  values (not fixture-inferred).
- This rule applies to free-function cases and deprecated member test methods.
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
- deprecated member test method fixture arguments
- suite fixtures (`[[gentest::fixture(suite)]]`)
- global fixtures (`[[gentest::fixture(global)]]`)

## Examples

These ordinary textual examples are header contents, so their free-function
definitions are `inline`.

### Unique allocation (default)

```cpp
struct MyFx {
    static std::unique_ptr<MyFx> gentest_allocate() {
        return std::make_unique<MyFx>();
    }
};

[[gentest::test("free/unique")]]
inline void free_unique(MyFx& fx) {
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

[[gentest::test("free/shared")]]
inline void free_shared(std::shared_ptr<SharedFx> fx) {
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
struct [[gentest::fixture(suite)]] SuiteFx {
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

[[gentest::test("free/pointer")]]
inline void free_pointer(RawFx* fx) {
    // gentest adopts and owns the pointer
}
```

## Notes and constraints

- Allocation hooks must be `static` and accept either no arguments or one
  `std::string_view` suite argument.
- If both overloads exist, suite fixtures use the suite-aware hook while global
  and local fixtures use the no-arg hook. If only the suite-aware overload
  exists, other lifetimes call it with an empty string.
- If you return `Fixture*`, it must be heap allocated. Gentest adopts it and
  deletes it via `std::unique_ptr` (same ownership/lifetime semantics as
  returning `std::unique_ptr<Fixture>` yourself).
- `std::shared_ptr<T>` arguments promote the unique ownership into shared
  ownership on first use.
- Fixture type aliases (e.g. `using Fx = RealFixture`, alias pointer/reference
  forms, and `using FxPtr = std::shared_ptr<RealFixture>`) resolve to the
  underlying fixture type.
- Shared fixtures activate on first acquisition by a selected runnable case.
  Unused fixture registrations perform no allocation, setup, or teardown.
  Suite fixtures are constructed once per suite within a run; global fixtures
  are constructed once per run. Instances persist across repeats. A later run
  in the same process creates fresh fixtures.
- Setup finishes before the dependent case runs, outside benchmark and jitter
  timing loops. Activated fixtures tear down once at the end of the run, in
  reverse activation order. Dependencies remain alive through their dependent's
  teardown and destruction. An instance whose setup fails still tears down;
  a failed allocation has no instance to tear down.
- Static skips do not acquire fixtures. A runtime skip after acquisition still
  tears down the acquired fixtures. Fixtures inferred by generated wrappers or
  acquired through a manual case's fixture metadata follow the same rules.
- A shared factory or setup callback can acquire another shared fixture with
  `get_shared_fixture(...)`; that dependency activates first. Looking up the
  fixture currently being initialized returns `nullptr` with
  `"fixture initialization in progress"`, which also prevents dependency cycles
  from exposing partially initialized instances. No new fixture activates during
  teardown. A first acquisition inside a measured call is rejected; acquire
  fixtures in setup or provide the manual case's fixture metadata before timing.
- A fixture setup failure blocks its dependent cases and fails the run. An
  unrelated failed or expensive fixture does not affect a selection that never
  acquires it. Registration conflicts remain errors for the run.
- For application environment setup required on every invocation regardless of
  selection, perform it explicitly in the application's `main()` before calling
  `run_all_tests()` or `run_cases()`.
