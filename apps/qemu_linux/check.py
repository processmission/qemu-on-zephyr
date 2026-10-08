# SPDX-License-Identifier: Apache-2.0

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
from project import build_directory, qemu_command
from qemu_config import load_config


def main():
    parser = argparse.ArgumentParser(description="Verify Linux console, IRQs and host scheduling.")
    parser.add_argument("--no-build", action="store_true")
    args = parser.parse_args()
    workspace = Path(__file__).resolve().parents[2]
    config = load_config()
    build = build_directory(config=config)
    accel, cpu = config.accel, config.cpu
    if not args.no_build:
        subprocess.run(
            [sys.executable, str(workspace / "scripts/project.py"), "build"],
            cwd=workspace, check=True,
        )

    logfile = workspace / "build" / f"{build.name}-validation.log"
    command = qemu_command(config=config)
    serial = command.index("-serial")
    command[serial:serial + 2] = [
        "-chardev", f"stdio,id=hostconsole,signal=off,logfile={logfile}",
        "-serial", "chardev:hostconsole",
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
        expect(r"QEMU Linux host EL" + ("1" if accel == "tcg" else "2"))
        if accel == "tcg":
            aliases, _ = expect(r"QEMU_TCG_JIT RW=(0x[0-9a-f]+) RX=(0x[0-9a-f]+)")
            if aliases.group(1) == aliases.group(2):
                raise RuntimeError("TCG must use distinct write and execute aliases")
        expect(r"QEMU machine=" + re.escape(config.machine) + r"-machine cpu=" +
               re.escape(cpu) + r"-arm-cpu accel=" + accel + r"-accel ")
        expect(r"Run /bin/sh as init process", timeout=180 if accel == "tcg" else 45)
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
            raise RuntimeError("Execution statistics did not prove EL0 and guest MMU use")
        if accel == "tcg" and not re.search(r"TCG-runs=[1-9]\d*", transcript):
            raise RuntimeError("TCG execution was not observed")

        send("poweroff -f")
        expect(r"reboot: Power down")
        expect(r"ZEPHYR_HOST_HEARTBEAT=\d+", timeout=10)
        print(f"PASS: {accel}/{cpu}; ARM64 Linux shell; timer IRQ {before}->{after}; "
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
