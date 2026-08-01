# Test inventory

Use `--list-json` when an IDE, dashboard, or CI tool needs a stable, machine-readable view of the tests compiled into an executable:

```sh
./my_tests --list-json
```

The command writes one JSON array to standard output. Each entry includes its name, source location, kind, tags, requirements, owner, skip state, fixture scope, suite, async/baseline flags, and `itemsPerCall`. Entries follow the normal deterministic registry order.

`owner` is a dedicated string field. It is empty when no `owner("...")` attribute is present; the compatibility `owner=...` value remains in `tags` for existing tag consumers.
