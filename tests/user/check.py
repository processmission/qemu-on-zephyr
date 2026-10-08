# SPDX-License-Identifier: Apache-2.0

import os
from pathlib import Path
import pty
import re
import select
import signal
import subprocess
import sys
import time

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from environment import command_env
from guest_disk import prepare_disk
from project import build_directory, qemu_command
from qemu_config import load_config


def main() -> None:
    config = load_config()
    if config.mode != "user":
        raise RuntimeError("User acceptance requires QEMU_MODE=user")
    env = command_env()
    build = build_directory(config=config)
    files = build / "user-files"
    files.mkdir(parents=True, exist_ok=True)
    compiler = Path(env["ZEPHYR_SDK_INSTALL_DIR"]) / "gnu/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-gcc"
    program = files / "syscalls"
    subprocess.run([str(compiler), "-nostdlib", "-static", "-ffreestanding", "-fno-builtin",
                    "-O1", "-T", str(ROOT / "tests/user/link.ld"), "-Wl,--build-id=none",
                    str(ROOT / "tests/user/entry.S"), str(ROOT / "tests/user/program.c"),
                    "-lc", "-o", str(program)], check=True)
    with program.open("rb") as stream:
        elf = ELFFile(stream)
        if elf.header.e_machine != "EM_AARCH64" or elf.header.e_type != "ET_EXEC":
            raise RuntimeError("Expected a static AArch64 Linux executable")
    (files / "message.txt").write_text("Zephyr filesystem\n")
    (files / "invalid").write_text("Not an executable\n")
    os.environ["GUEST_FILES"] = str(files)
    os.environ["GUEST_DISK"] = str(build / "user-disk.img")
    prepare_disk(ROOT, create=True)
    command = qemu_command(config=config)
    log = build / "user-validation.log"
    pending = ""
    transcript = ""
    pid, terminal = pty.fork()
    if pid == 0:
        os.execvp(command[0], command)

    def expect(pattern: str, timeout: float = 45) -> re.Match[str]:
        nonlocal pending, transcript
        deadline = time.monotonic() + timeout
        while True:
            match = re.search(pattern, pending)
            if match:
                pending = pending[match.end():]
                return match
            if re.search(r"USER_FAIL[^\n]*\n|ASSERTION FAIL[^\n]*\n|FATAL ERROR[^\n]*\n|QEMU guest exited: -?\d+\n", pending):
                raise RuntimeError(f"Guest failed before {pattern!r}:\n{transcript[-4000:]}")
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([terminal], [], [], remaining)[0]:
                raise RuntimeError(f"Missing {pattern!r}:\n{transcript[-4000:]}")
            data = os.read(terminal, 65536)
            if not data:
                raise RuntimeError("QEMU closed its console")
            value = data.decode(errors="replace").replace("\r", "")
            pending += value
            transcript += value

    def send(command: str) -> None:
        os.write(terminal, (command + "\n").encode())

    def prompt() -> None:
        send("")
        expect("zephyr> ")

    try:
        expect("QEMU shell ready: qemu-aarch64 -help")
        if not config.manual_shell:
            expect("USER_OK argv env data bss files errno brk mmap smc clock entropy")
            expect(r"QEMU guest exited: 7\n")
        prompt()
        send("qemu-aarch64 -help")
        expect(r"/images/PROGRAM \[ARG\.\.\.\]")
        prompt()
        send("qemu-aarch64 /images/missing")
        expect("Cannot open program")
        prompt()
        send("qemu-aarch64 /images/invalid")
        expect(r"QEMU guest exited: -8\n")
        prompt()
        for mode, expected in (("check", 7), ("fault", 139), ("readonly", 139), ("readonly-zero", 139),
                               ("unmapped", 139), ("wait", 130), ("check", 7)):
            send(f'qemu-aarch64 -strace -E QOZ_USER=yes /images/syscalls {mode} "two words"')
            if mode == "check":
                expect("USER_OK argv env data bss files errno brk mmap smc clock entropy")
            elif mode == "wait":
                expect("USER_WAIT")
                os.write(terminal, b"\x1d")
            else:
                expect("qemu-aarch64: SIGSEGV")
            expect(rf"QEMU guest exited: {expected}\n")
            prompt()
        if any(marker in transcript for marker in ("USER_FAIL", "ASSERTION FAIL", "FATAL ERROR")):
            raise RuntimeError("User-mode acceptance reported a runtime failure")
        print(f"PASS: user/{config.cpu}; Linux ABI, filesystem, memory, faults, stop and repeat")
    finally:
        log.write_text(transcript)
        os.kill(pid, signal.SIGTERM)
        os.waitpid(pid, 0)
        os.close(terminal)
        print(f"Console: {log}")


if __name__ == "__main__":
    main()
