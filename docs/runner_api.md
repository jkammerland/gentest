# Embedding the Gentest runner

Use `gentest::run_cases` when an embedding program owns a `gentest::Case` table and wants Gentest's normal CLI selection,
execution, reporting, fixture, benchmark, and jitter behavior without adding those cases to the global registry.

```cpp
#include "gentest/runner.h"

#include <cstddef>
#include <span>
#include <vector>

void check_service(void *) {
    // The Gentest assertion APIs are available while this callable runs.
}

static const gentest::Case cases[] = {
    .name = "embedded/check_service",
    .fn = &check_service,
    .file = __FILE__,
    .line = __LINE__,
    .fixture_lifetime = gentest::FixtureLifetime::None,
    .suite = "embedded",
}};

int main(int argc, char **argv) {
    std::vector<const char *> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i)
        args.push_back(argv[i]);
    return gentest::run_cases(cases, args);
}
```

`run_cases` is also exported by `import gentest;`. Its argument parsing is identical to `run_all_tests`: an optional first program
name is ignored, null pointers are skipped, and an empty argument span requests the default run.

The runner makes a private, shallow copy of the descriptors and sorts it by name, file, and line, matching registered-case order.
It never mutates the caller's table or adds it to `registered_cases()`. Keep the callables and all storage referenced by `Case`
string views and spans alive until `run_cases` returns.

Return codes match `run_all_tests` for the same cases and arguments: `0` means the requested operation completed successfully
(including an ordinary filter that selects no cases), `1` reports invalid CLI input or a test/runtime failure, and `3` means an
exact `--run` case was not found. Benchmark- or jitter-only filters that match nothing retain their existing failure semantics.
