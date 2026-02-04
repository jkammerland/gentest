# CTest Integration

Use `cmake/CoordCTest.cmake` to add fixture-based CTest runs:

- `coord_add_fixture_daemon(...)` starts `coordd` via `coordctl daemonize` as a `FIXTURES_SETUP` test.
- `coord_add_session_test(...)` runs `coordctl submit --wait` (add `REPORT_DIR` to emit a JUnit XML file).
- `coord_add_fixture_cleanup(...)` shuts down the daemon.

Example usage is in `examples/udp_multi_node/CMakeLists.txt`.
