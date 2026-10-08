# SPDX-License-Identifier: Apache-2.0

import os
from pathlib import Path
import re
import select
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from environment import command_env
from project import build_directory
from qemu_config import load_config


def main() -> None:
    env = command_env()
    env.update(QEMU_MODE="user", QEMU_SHELL="1")
    env.pop("GUEST_FILES", None)
    env.pop("GUEST_DISK", None)
    config = load_config(env)
    log = build_directory(config=config) / "default-user-validation.log"
    pending = ""
    transcript = ""
    process = subprocess.Popen(
        ["make", "--no-print-directory", "run", "QEMU_MODE=user", "QEMU_SHELL=1",
         "GUEST_FILES=", "GUEST_DISK="],
        cwd=ROOT, env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, start_new_session=True,
    )
    assert process.stdin is not None and process.stdout is not None
    terminal = process.stdout.fileno()

    def expect(pattern: str, timeout: float = 45) -> None:
        nonlocal pending, transcript
        deadline = time.monotonic() + timeout
        while True:
            match = re.search(pattern, pending, re.MULTILINE)
            if match:
                pending = pending[match.end():]
                return
            if re.search(r"Cannot open program|ASSERTION FAIL|FATAL ERROR|QEMU guest exited: [^0]", pending):
                raise RuntimeError(f"Default user launch failed:\n{transcript[-4000:]}")
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([terminal], [], [], remaining)[0]:
                raise RuntimeError(f"Missing {pattern!r}:\n{transcript[-4000:]}")
            data = os.read(terminal, 65536)
            if not data:
                raise RuntimeError("Default user console closed")
            value = data.decode(errors="replace").replace("\r", "")
            pending += value
            transcript += value

    def send(command: str) -> None:
        process.stdin.write((command + "\n").encode())
        process.stdin.flush()

    def prompt() -> None:
        send("")
        expect(r"zephyr> ")

    try:
        expect(r"QEMU shell ready: qemu-aarch64 -help", timeout=300)
        prompt()
        send("fs ls /images")
        expect(r"^\s*\d+ hello$")
        prompt()
        send("qemu-aarch64 /images/hello arg1")
        expect(r"^Hello from Linux AArch64 on Zephyr!$")
        expect(r"^argument: arg1$")
        expect(r"QEMU guest exited: 0\n")
        prompt()
        send('qemu-aarch64 -E MESSAGE=ready /images/hello "two words"')
        expect(r"^Hello from Linux AArch64 on Zephyr!$")
        expect(r"^argument: two words$")
        expect(r"^MESSAGE: ready$")
        expect(r"QEMU guest exited: 0\n")
        prompt()
        send("qemu-aarch64 -strace /images/hello")
        expect(r"^Hello from Linux AArch64 on Zephyr!$")
        expect(r"syscall 64\([^\n]+\) = [1-9]\d*\n")
        expect(r"QEMU guest exited: 0\n")
        prompt()
        process.stdin.write(b"\x01x")
        process.stdin.flush()
        expect(r"QEMU: Terminated")
        if process.wait(timeout=10) != 0:
            raise RuntimeError("make run failed after the documented QEMU exit sequence")
        print(f"PASS: default user/{config.cpu}; make run provides hello, arguments, environment and syscall tracing")
    finally:
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text(transcript)
        if process.poll() is None:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait(timeout=10)
        process.stdin.close()
        process.stdout.close()
        print(f"Console: {log}")


if __name__ == "__main__":
    main()
