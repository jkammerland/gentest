"""Exercise one supervisor invocation per child and check the parent verdict."""

import os
from contextlib import ExitStack
from pathlib import Path
import signal
import socket
import subprocess
import sys
import tempfile


def child(args):
    name = next(a.split("=", 1)[1] for a in args if a.startswith("--run="))
    marker = Path(next(a.split("=", 1)[1] for a in args if a.startswith("--gentest-death-marker=")))
    if name == "absent":
        print("Case not found: absent")
        return 1
    if name in ("setup", "setup77"):
        return 3 if name == "setup" else 77
    marker.write_bytes(name.encode())
    if name == "ordinary failure":
        print("[ FAIL ] check failed")
    if name in ("teardown", "teardown77"):
        with marker.open("ab") as f:
            f.write(b"\ncompleted")
    if name == "skip":
        marker.write_bytes((name + "\nskipped").encode())
        return 77
    if name == "teardown77":
        return 77
    if name == "returned":
        return 0
    if name == "early text":
        print("x" * 16380, end="")
        print("first diagnostic")
        print("x" * 100_000)
    return 3


def wait_for_outer_cancellation(port, role):
    # Each process confirms it is alive and holds its own connection until killed.
    with socket.create_connection(("127.0.0.1", int(port)), timeout=10) as peer, ExitStack() as stack:
        if role == "child":
            stack.enter_context(subprocess.Popen([sys.executable, str(Path(__file__).resolve()),
                                                  "--outer-cancellation-child", port, "descendant"]))
        peer.settimeout(None)
        peer.sendall(b"C" if role == "child" else b"D")
        peer.recv(1)
    return 1


def check_outer_cancellation(supervisor):
    with socket.socket() as listener, tempfile.TemporaryDirectory() as work:
        listener.bind(("127.0.0.1", 0))
        listener.listen(2)
        listener.settimeout(10)
        port = str(listener.getsockname()[1])
        command = [supervisor, "--case", "cancel", "--", sys.executable,
                   str(Path(__file__).resolve()), "--outer-cancellation-child", port, "child"]
        env = os.environ | {"TEST_TMPDIR": work}
        with subprocess.Popen(command, start_new_session=(os.name == "posix"), env=env) as process, ExitStack() as peers:
            try:
                connections = [peers.enter_context(listener.accept()[0]) for _ in range(2)]
                for connection in connections:
                    connection.settimeout(10)
                if sorted(connection.recv(1) for connection in connections) != [b"C", b"D"]:
                    raise AssertionError("child and descendant did not confirm startup")

                if os.name == "posix":
                    os.killpg(process.pid, signal.SIGKILL)
                    expected_exit = -signal.SIGKILL
                else:
                    process.kill()
                    expected_exit = 1
                if process.wait(timeout=10) != expected_exit:
                    raise AssertionError("outer runner did not kill the supervisor")
                for connection in connections:
                    try:
                        if connection.recv(1):
                            raise AssertionError("unexpected output after cancellation")
                    except ConnectionResetError:
                        # Windows may report an abrupt close as a reset.
                        pass
            finally:
                if process.poll() is None:
                    process.kill()


def main(supervisor, probe, *emulator):
    fake = [sys.executable, str(Path(__file__).resolve()), "--child"]

    def check(name, opts, code, message, program=fake, env=None):
        run = subprocess.run(
            [supervisor, "--case", name, *opts, "--", *program],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=35, env=env,
        )
        if run.returncode != code or message.encode() not in run.stdout:
            raise AssertionError((name, opts, run.returncode, run.stdout[-3000:]))
        return run.stdout

    check("early text", ["--expect", "first diagnostic"], 0, "Death test passed")
    check('quote " and trailing backslash\\', [], 0, "Death test passed")
    check("ordinary failure", [], 1, "normal test failure")
    check("returned", [], 1, "exit code was 0")
    check("setup", [], 1, "did not reach")
    check("teardown", [], 1, "body completed")
    check("setup77", [], 1, "did not reach")
    check("teardown77", [], 1, "body completed")
    check("skip", [], 77, "[ SKIP ]")
    check("skip", ["--skip-as-pass"], 0, "[ SKIP ]")
    check("absent", [], 1, "not found")
    check("absent", ["--skip-missing"], 77, "[ SKIP ]")
    check("any", ["--expect", "missing message"], 1, "Expected substring")
    check("any", [], 1, "could not execute", [str(Path(supervisor).with_name("missing-test-executable"))])
    check_outer_cancellation(supervisor)

    real = [*emulator, probe]
    for name, message in (("be/death", "not found"), ("probe/ordinary", "requires the death attribute")):
        if b"fatal diagnostic" in check(name, [], 1, message, real):
            raise AssertionError(f"incorrect registration executed {name}")
    check("probe/death", ["--expect", "fatal diagnostic", "--inactivity-ms", "5000"], 0, "Death test passed", real)
    check("probe/async_body", ["--expect", "async body resumed"], 0, "Death test passed", real)
    for case in ("probe/async_teardown", "probe/async_canceled"):
        output = check(case, [], 1, "body completed", real)
        if b"async frame teardown" not in output:
            raise AssertionError((case, output))

    # Exercise the real runner's case name and marker path outside the Windows ANSI code page.
    with tempfile.TemporaryDirectory() as root:
        work = Path(root) / "監督"
        work.mkdir()
        check("probe/café", ["--inactivity-ms", "5000"], 0, "Death test passed", real, os.environ | {"TEST_TMPDIR": str(work)})
        if any(work.iterdir()):
            raise AssertionError("supervisor left its temporary files behind")
    check("probe/stall", ["--timeout-ms", "1000"], 1, "timed out", real)
    check("probe/stall", ["--inactivity-ms", "1000"], 1, "waiting for input", real)
    check("probe/silence", ["--inactivity-ms", "1000"], 0, "Death test passed", real)
    check("probe/malformed", ["--inactivity-ms", "5000"], 1, "watchdog notification failed", real)
    check("probe/invalid", ["--inactivity-ms", "5000"], 1, "watchdog", real)


if __name__ == "__main__":
    if sys.argv[1] == "--outer-cancellation-child":
        sys.exit(wait_for_outer_cancellation(*sys.argv[2:4]))
    if sys.argv[1] == "--child":
        sys.exit(child(sys.argv[2:]))
    main(*sys.argv[1:])
