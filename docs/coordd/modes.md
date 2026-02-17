# Execution Modes

## Mode A (Plain multiprocess)
- No namespaces.
- Per-instance working directories and log capture.
- Host networking.

## Mode B/C/D
These modes require Linux namespaces and/or privileged helpers. The initial implementation surfaces clear errors if a non-A mode is requested. A follow-up change will wire Linux isolation and networking (userns/mountns/netns and netd-helper).
