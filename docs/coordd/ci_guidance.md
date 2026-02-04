# CI Guidance

- Prefer per-session port allocation via `network.ports` to avoid collisions.
- Use `ctest -j` with a single `coordd` fixture to parallelize sessions.
- For TCP connections to coordd, configure mTLS using `--tls-ca/--tls-cert/--tls-key`.
