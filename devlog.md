# Dev Log

## Goal

Integrate the coord feature set from `feat/coordd-v3` onto the `modules-v5` base in small, tested, atomic commits.

## Plan

1. Create an integration branch from `modules-v5`.
2. Port coord sources and top-level CMake wiring.
3. Fix local build/test regressions introduced by the port.
4. Update CI and buildsystem workflows for coord dependencies.
5. Run broader verification and finish with a clean tested branch.

## Progress

### Step 1

- Created branch `coordd-on-modules-v5` from `modules-v5`.
- Transplanted coord sources, docs, tests, and build wiring from `feat/coordd-v3`.
- Disabled coord by default for `host-codegen` and `aarch64-qemu` presets so existing cross/codegen workflows stay dependency-light.
- Fixed the Boost JSON parse error type usage in the coord library and coord tests.
- Next: update CI/buildsystem workflows so the integrated branch works in the affected lanes.

### Tests

- `cmake --preset=debug-system`
- `cmake --build --preset=debug-system --target coordd coordctl gentest_coord_tests`
- `./build/debug-system/tests/gentest_coord_tests --no-color`
- `ctest --preset=debug-system -R '^coord_(counts|list_counts)$' --output-on-failure`

### Step 2

- Switched `COORD_BUILD` to opt-in by default so the existing `modules-v5` CI/buildsystem matrix remains focused on gentest/module coverage.
- Added a dedicated Linux coord smoke job in `.github/workflows/cmake.yml`.
- Added `openssl` to `vcpkg.json` so opt-in coord builds have an explicit manifest dependency path in vcpkg-driven configurations.
- Verified that the `host-codegen` preset still configures/builds with coord disabled by default.
- Re-verified the coord targets and tests with `COORD_BUILD=ON` explicitly enabled on the `debug-system` path.
- Local note: a fresh `debug`/vcpkg configure hit a stale local `patchelf` extraction cache under `/home/ai-dev1/vcpkg`; that is local cache state, not a branch regression, and should not affect fresh CI runners.

### Tests

- `cmake --preset=host-codegen`
- `cmake --build --preset=host-codegen`
- `cmake --preset=debug-system -DCOORD_BUILD=ON`
- `cmake --build --preset=debug-system --target coordd coordctl gentest_coord_tests`
- `./build/debug-system/tests/gentest_coord_tests --no-color`
- `ctest --preset=debug-system -R '^coord_(counts|list_counts)$' --output-on-failure`

### Step 3

- Ran a broader sanity pass against the integrated branch.
- Verified that representative `modules-v5` regression coverage still passes alongside the coord integration.
- The branch is ready for follow-up review or push.

### Tests

- `ctest --preset=debug-system -R '^(gentest_module_auto_discovery|gentest_package_consumer|coord_counts|coord_list_counts)$' --output-on-failure`
