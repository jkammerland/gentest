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
