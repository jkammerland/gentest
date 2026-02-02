# Fixture Allocation Hooks

Gentest supports allocating fixtures via an optional static hook so you can
return heap-owned fixtures (unique/shared) instead of relying on stack
construction. This is useful for fixtures that require virtual inheritance,
custom allocation, or shared ownership across tests.

This document describes the allocation hook, supported return types, and how
fixture arguments are wired for free-function fixtures and member fixtures.

## API: `gentest_allocate`

Define a static method on your fixture:

```
static auto gentest_allocate();
```

Supported return types:
- `std::unique_ptr<Fixture>`
- `std::shared_ptr<Fixture>`
- `Fixture*` (adopted into a `std::unique_ptr`)

If the hook is not present, gentest falls back to `std::make_unique<Fixture>()`.

## How arguments are provided

Gentest uses a single internal `FixtureHandle<T>` that stores either:
- a `std::unique_ptr<T>` (default), and
- optionally a `std::shared_ptr<T>` (when needed).

When the test wrapper is invoked:
- `T&` arguments receive `*handle.get()`.
- `T*` arguments receive `handle.get()`.
- `std::shared_ptr<T>` arguments receive `handle.shared()`, which returns the
  existing shared pointer or promotes the unique pointer into a shared pointer.

This behavior is uniform across:
- free-function fixtures (`[[using gentest: fixtures(A, B, ...)]]`)
- member fixtures (ephemeral per test)
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

[[using gentest: test("free/unique"), fixtures(MyFx)]]
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

[[using gentest: test("free/shared"), fixtures(SharedFx)]]
void free_shared(std::shared_ptr<SharedFx> fx) {
    // shared ownership of the fixture instance
}
```

### Raw pointer allocation (adopted)

```cpp
struct RawFx {
    static RawFx* gentest_allocate() {
        return new RawFx();
    }
};

[[using gentest: test("free/pointer"), fixtures(RawFx)]]
void free_pointer(RawFx* fx) {
    // gentest adopts and owns the pointer
}
```

## Notes and constraints

- `gentest_allocate()` must be `static` and callable without arguments.
- If you return `Fixture*`, it must be heap allocated. Gentest adopts it and
  deletes it via `std::unique_ptr`.
- `std::shared_ptr<T>` arguments promote the unique ownership into shared
  ownership on first use.
- Suite/global fixtures are constructed once (per suite or per process) and
  reused across tests.
