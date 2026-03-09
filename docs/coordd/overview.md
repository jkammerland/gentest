# coordd Overview

`coordd` is a long-lived test session coordinator. Clients (`coordctl`) submit session specs, wait for completion, and retrieve manifests that include per-node process results and artifact locations.

Key responsibilities:
- Spawn processes with explicit argv/env/cwd.
- Per-session artifact directories and log capture.
- Readiness gating and timeouts (nodes are started in order; readiness of a node gates subsequent nodes).
- Always produce a manifest (even on failure).

See `modes.md` for isolation/networking modes and `ctest.md` for CTest integration.
