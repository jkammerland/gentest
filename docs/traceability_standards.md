# Requirements traceability

Record which requirements each test case verifies with `req(...)`, then build a
requirement-to-test trace matrix from the listings and reports that the runner
writes.

## Link cases to requirement IDs

Add `req(...)` with one or more requirement IDs to each case that verifies
them. `owner(...)` records who is responsible for the case:

```cpp
#pragma once

#include "gentest/test.h"

using namespace gentest::asserts;

namespace math {

[[using gentest: test("add"), req("SSR-001"), owner("team-runtime")]]
inline void add() {
    EXPECT_EQ(1 + 1, 2);
}

[[using gentest: test("divide"), req("SSR-002", "SSR-003")]]
inline void divide() {
    EXPECT_EQ(8 / 2, 4);
}

} // namespace math
```

Requirement IDs and owners are free-form strings. A case can list several IDs,
and duplicate IDs are dropped. See [Writing tests](writing_tests.md) for the
other attributes.

## Where the IDs appear

| Output | How it shows requirements |
|---|---|
| `--list` | `requires=<id>,...` in the bracketed metadata of each case |
| `--list-json` | the `requirements` array of each case; see [Test inventory](test_inventory.md) |
| `--junit=<file>` | one `<property name="requirement" value="<id>"/>` per ID |
| `--allure-dir=<dir>` | one `requirement` label per ID; see [Reports](reports.md) |

For the cases above, `./my_tests --list` prints:

```text
math/add [gentest:tags=owner=team-runtime;requires=SSR-001;owner=team-runtime] (cases.hpp:10)
math/divide [gentest:requires=SSR-002,SSR-003] (cases.hpp:15)
```

The owner appears both as the `owner=` entry and as an `owner=team-runtime`
tag.

The JUnit report holds the IDs as properties of each test case:

```xml
<testcase classname="math" name="math/divide" time="0.000001095">
  <properties>
    <property name="requirement" value="SSR-002"/>
    <property name="requirement" value="SSR-003"/>
  </properties>
</testcase>
```

JUnit has no property for the owner or the tags. To combine them with the
results, join the `--list-json` inventory with the JUnit report on the case
name.

## Build a trace matrix

1. Give each requirement an ID in your requirements document, for example
   `SSR-001`, `SSR-002` and so on.
2. Add `req("SSR-...")` to every case that verifies a requirement.
3. In CI, save the inventory and the results of the run:

   ```bash
   ./my_tests --list-json > inventory.json
   ./my_tests --junit=junit.xml
   ```

4. Join the two files on the case name, and keep the CI run's URL with the
   files as evidence.

This script does the join and prints a Markdown matrix. Save it as
`trace_matrix.py` and run `python3 trace_matrix.py` in the directory that holds
them. Its outcome names follow the
[outcome table](reports.md#outcomes-in-reports) in Reports:

```python
import json
import xml.etree.ElementTree as ET


def outcome(testcase):
    if testcase.find("failure") is not None:
        return "fail"
    if testcase.find("error") is not None:
        return "blocked"
    skipped = testcase.find("skipped")
    if skipped is not None:
        return "xfail" if skipped.get("message", "").startswith("xfail") else "skip"
    return "pass"


with open("inventory.json", encoding="utf-8") as f:
    inventory = json.load(f)
results = {tc.get("name"): outcome(tc) for tc in ET.parse("junit.xml").getroot().iter("testcase")}

rows = sorted(
    (requirement, case["name"], case["owner"], results.get(case["name"], "not run"))
    for case in inventory
    for requirement in case["requirements"]
)
print("| Requirement | Case | Owner | Result |")
print("|---|---|---|---|")
for row in rows:
    print("| " + " | ".join(row) + " |")
```

For the cases above it prints:

```text
| Requirement | Case | Owner | Result |
|---|---|---|---|
| SSR-001 | math/add | team-runtime | pass |
| SSR-002 | math/divide |  | pass |
| SSR-003 | math/divide |  | pass |
```

The inventory lists only the IDs that some case references. To find
requirements that no case verifies, compare the IDs in the matrix with your
requirements document.

## Standards source links

- ISO 26262-6:2018 (Product development at the software level): <https://www.iso.org/standard/68388.html>
- ISO 26262-8:2018 (Supporting processes): <https://www.iso.org/standard/68390.html>
- IEC 61508-1:2010 (General requirements): <https://webstore.iec.ch/en/publication/5515>
- IEC 61508-3:2010 (Software requirements): <https://webstore.iec.ch/en/publication/5517>
- IEC TS 61508-3-1:2016 (Reuse/proven-in-use guidance): <https://webstore.iec.ch/en/publication/25410>

## Limits

- This page is a practical mapping aid, not a replacement for the standards
  text.
- For formal compliance claims, keep clause-level mappings in project-specific
  safety artifacts.
