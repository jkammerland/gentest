# CI Guidance

- Prefer per-session port allocation via `network.ports` to avoid collisions.
- Use `ctest -j` with a single `coordd` fixture to parallelize sessions.
- For TCP connections to coordd, configure mTLS using `--tls-ca/--tls-cert/--tls-key`.
- TLS backend selection:
  - Default: system OpenSSL (`-DCOORD_TLS_BACKEND=openssl`).
  - WolfSSL: `-DCOORD_TLS_BACKEND=wolfssl -DCOORD_WOLFSSL_SOURCE_DIR=/path/to/wolfssl` (or place `../wolfssl` next to the repo).
- `cbor_tags` source selection:
  - Default: pinned FetchContent dependency (`v0.9.5`) for reproducible builds.
  - Explicit local override: `-DCOORD_CBOR_TAGS_SOURCE_DIR=/path/to/cbor_tags`.
  - Optional sibling checkout: `-DCOORD_USE_SIBLING_CBOR_TAGS=ON` to allow `../cbor_tags` when no explicit source dir is set.
