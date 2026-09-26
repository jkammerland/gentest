"""Verify actual serializer bytes, failures, and preflight in generated measured wrappers."""
import json
from pathlib import Path
import shutil
import subprocess
import sys

exe, root, format = sys.argv[1], Path(sys.argv[2]), sys.argv[3]
shutil.rmtree(root, ignore_errors=True)
for name, rc in [("success", 0), ("errors", 1), ("timed", 1)]:
    directory = root / name
    result = subprocess.run([exe, f"--run=adapter/{name}", f"--records={directory}",
                             "--bench-epochs=1", "--bench-warmup=0", "--bench-max-total-time-s=0.01"],
                            capture_output=True, text=True, timeout=60)
    assert result.returncode == rc, (result.stdout, result.stderr)
    index_path, = directory.glob("run-*/index.json")
    case, = json.loads(index_path.read_text())["cases"]
    assert case["outcome"] == ("pass" if rc == 0 else "fail"), case
    records = case["data"]["records"]
    payloads = {r["name"]: (index_path.parent / r["path"]).read_bytes() for r in records}
    assert all(r["contentType"] == f"application/{format}" for r in records)
    if name == "success":
        assert records[0]["schema"] == "snapshot/v1"
        if format == "json":
            assert json.loads(payloads["snapshot"]) == {"device": "simulator", "samples": [1, 2, 3]}
            assert json.loads(payloads["empty"]) == []
        else:
            # RFC 8949: ["simulator", [1,2,3]], [], tag(60000, h'00ff').
            assert payloads["snapshot"] == b"\x82\x69simulator\x83\x01\x02\x03", payloads
            assert payloads["empty"] == b"\x80"
            assert payloads["tagged"] == b"\xd9\xea\x60\x42\x00\xff", payloads
    elif name == "errors":
        assert set(payloads) == {"before", "after"}, payloads
        assert "serialization failed" in result.stderr
        if format == "json":
            assert "serialization threw" in result.stderr
    else:
        assert not records
        assert case["data"]["properties"]["encoded"] == 0
        assert "timed bench/jitter" in result.stderr
print(f"{format}: real serializer snapshots, errors, and timed preflight passed")
