# CI Guidance

- Prefer per-session port allocation via `network.ports` to avoid collisions.
- Use `ctest -j` with a single `coordd` fixture to parallelize sessions.
- For TCP connections to coordd, configure mTLS using `--tls-ca/--tls-cert/--tls-key`.
- TLS backend selection:
  - Default: system OpenSSL (`-DCOORD_TLS_BACKEND=openssl`).
  - WolfSSL: `-DCOORD_TLS_BACKEND=wolfssl -DCOORD_WOLFSSL_SOURCE_DIR=/path/to/wolfssl` (or place `../wolfssl` next to the repo).
