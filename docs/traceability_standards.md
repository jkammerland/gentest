# Traceability Workflow + Standards Sources

This note shows how to use `gentest` metadata to build a requirement-to-test trace matrix and points to source standards pages.

## gentest traceability examples

Test annotation with requirement ID:

```cpp
#include "gentest/attributes.h"

[[using gentest: test("math/add"), req("SSR-001"), owner("team-runtime")]]
void add_test();
```

List output includes requirement IDs:

```text
math/add [requires=SSR-001] [owner=team-runtime]
```

JUnit export contains requirement properties:

```xml
<property name="requirement" value="SSR-001"/>
```

## Example workflow

1. Define software safety requirements in your requirements document (for example `SSR-001`, `SSR-002`, ...).
2. Tag each test with `req("SSR-...")`.
3. Run with `--list` and/or `--junit=<file>`.
4. Build a trace matrix from requirement IDs to test cases and CI artifacts.

Example matrix columns:

| Requirement | Test case      | Evidence artifact |
|---|---|---|
| SSR-001 | math/add | `junit.xml` + CI run URL |
| SSR-002 | io/retry | `junit.xml` + CI run URL |

## Standards source links

- ISO 26262-6:2018 (Product development at the software level): <https://www.iso.org/standard/68388.html>
- ISO 26262-8:2018 (Supporting processes): <https://www.iso.org/standard/68390.html>
- IEC 61508-1:2010 (General requirements): <https://webstore.iec.ch/en/publication/5515>
- IEC 61508-3:2010 (Software requirements): <https://webstore.iec.ch/en/publication/5517>
- IEC TS 61508-3-1:2016 (Reuse/proven-in-use guidance): <https://webstore.iec.ch/en/publication/25410>

## Notes

- This page is a practical mapping aid, not a replacement for the standards text.
- For formal compliance claims, keep clause-level mappings in project-specific safety artifacts.
