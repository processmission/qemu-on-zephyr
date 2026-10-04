#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Boot the actual port and verify Linux console, IRQs and host scheduling."""

import argparse
import os
from pathlib import Path
import pty
import re
import select
import signal
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from environment import qemu_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--no-build", action="store_true")
    args = parser.parse_args()
    workspace = Path(__file__).resolve().parents[2]
    build = workspace / "build/linux"
    if not args.no_build:
        subprocess.run(
            [sys.executable, str(workspace / "scripts/project.py"), "build"],
            cwd=workspace, check=True,
        )

    logfile = workspace / "build/linux-validation.log"
    command = [
        qemu_path(),
        "-machine", "virt,virtualization=on,secure=off,gic-version=3",
        "-accel", "tcg",
        "-cpu", "cortex-a53", "-m", "512M", "-smp", "1",
        "-display", "none", "-monitor", "none",
        "-chardev", f"stdio,id=hostconsole,signal=off,logfile={logfile}",
        "-serial", "chardev:hostconsole", "-kernel", str(build / "zephyr/zephyr.elf"),
    ]
    pid, terminal = pty.fork()
    if pid == 0:
        os.execvp(command[0], command)

    pending = ""
    transcript = ""

    def expect(pattern, timeout=45):
        nonlocal pending, transcript
        deadline = time.monotonic() + timeout
        while True:
            match = re.search(pattern, pending, re.MULTILINE)
            if match:
                consumed = pending[:match.end()]
                pending = pending[match.end():]
                return match, consumed
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([terminal], [], [], remaining)[0]:
                raise RuntimeError(f"Missing {pattern!r}; console tail:\n{transcript[-3000:]}")
            data = os.read(terminal, 65536)
            if not data:
                raise RuntimeError("QEMU console closed before acceptance completed")
            text = data.decode(errors="replace").replace("\r", "")
            pending += text
            transcript += text

    def send(text):
        os.write(terminal, (text + "\n").encode())

    def timer_count():
        send("cat /proc/interrupts")
        match, _ = expect(r"^\s*\d+:\s*(\d+)\s+GICv3\s+27\s+Level\s+arch_timer$")
        count = int(match.group(1))
        expect(r"^Err:\s+0$")
        expect(r"~ # ")
        return count

    try:
        expect(r"QEMU Linux host EL2")
        expect(r"cpu=cortex-a53-arm-cpu accel=zephyr-accel \(Zephyr\)")
        expect(r"Run /bin/sh as init process")
        expect(r"~ # ")
        send("uname -m")
        expect(r"^aarch64$")
        expect(r"~ # ")
        send("echo QEMU_LINUX_CONSOLE_OK")
        expect(r"^QEMU_LINUX_CONSOLE_OK$")
        expect(r"~ # ")
        send("mount -t proc proc /proc")
        expect(r"~ # ")
        before = timer_count()
        started = time.monotonic()
        send("sleep 1")
        expect(r"~ # ")
        if time.monotonic() - started < 0.8:
            raise RuntimeError("Guest sleep returned without waiting")
        after = timer_count()
        if after <= before:
            raise RuntimeError("Guest virtual timer IRQ count did not increase")

        # No timeout applet is required by the pinned minimal initramfs.
        send("echo VALID_BUSY_BEGIN; read up rest < /proc/uptime; "
             "end=$(( ${up%%.*} + 8 )); while :; do read up rest < /proc/uptime; "
             "[ ${up%%.*} -ge $end ] && break; done; echo VALID_BUSY_END")
        expect(r"^VALID_BUSY_BEGIN$")
        _, busy_output = expect(r"^VALID_BUSY_END$", timeout=20)
        heartbeats = re.findall(r"ZEPHYR_HOST_HEARTBEAT=(\d+)", busy_output)
        if not heartbeats:
            raise RuntimeError("No independent Zephyr thread progress during guest spin")
        expect(r"~ # ")
        el0 = max(map(int, re.findall(r"EL0=(\d+)", transcript)), default=0)
        mmu = max(map(int, re.findall(r"MMU-on=(\d+)", transcript)), default=0)
        if el0 == 0 or mmu == 0:
            raise RuntimeError("Native execution statistics did not prove EL0 and guest MMU use")

        send("poweroff -f")
        expect(r"reboot: Power down")
        expect(r"ZEPHYR_HOST_HEARTBEAT=\d+", timeout=10)
        print(f"PASS: real Zephyr accelerator; ARM64 Linux shell; timer IRQ {before}->{after}; "
              f"host heartbeat during busy guest; EL0={el0}, MMU-on={mmu}; host survives poweroff")
        print(f"Console: {logfile}")
    finally:
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        os.waitpid(pid, 0)
        os.close(terminal)


if __name__ == "__main__":
    main()
