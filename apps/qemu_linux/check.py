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
from guest_disk import prepare_disk


def main():
    parser = argparse.ArgumentParser(description="Verify Linux console, IRQs and host scheduling.")
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--firmware", help="Zephyr filesystem path to the acceptance firmware")
    parser.add_argument("--bios", action="store_true", help="load acceptance firmware as raw bytes")
    parser.add_argument("--guest-cpu", choices=("cortex-a53", "cortex-a57", "cortex-a72", "host"))
    parser.add_argument("--expect-load-error", action="store_true")
    parser.add_argument("--stop-firmware", action="store_true")
    parser.add_argument("--reboot-after-firmware", action="store_true")
    args = parser.parse_args()
    if (args.bios or args.expect_load_error or args.stop_firmware or args.reboot_after_firmware) and not args.firmware:
        parser.error("firmware validation options require --firmware")
    workspace = Path(__file__).resolve().parents[2]
    config = load_config()
    build = build_directory(config=config)
    accel, cpu = config.accel, args.guest_cpu or config.cpu
    if not args.no_build:
        subprocess.run(
            [sys.executable, str(workspace / "scripts/project.py"), "build"],
            cwd=workspace, check=True,
        )
        prepare_disk(workspace)

    suffix = ("firmware-stop" if args.stop_firmware else "firmware-error" if args.expect_load_error else "firmware-bios" if args.bios
              else "firmware-elf" if args.firmware else "validation")
    logfile = workspace / "build" / f"{build.name}-{suffix}.log"
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
        expect(r"Guest filesystem mounted at /images")
        expect(r"QEMU shell ready: qemu-system-aarch64 -help")
        if config.manual_shell:
            send("")
            expect(r"zephyr> ")
            send("qemu-system-aarch64 -accel help")
            expect(r"Compiled accelerator: " + accel)
            expect(r"zephyr> ")
            send("fs ls /images")
            expect(r"firmware" if args.firmware else r"Image")
            expect(r"zephyr> ")
            send("qemu-system-aarch64 -kernel /images/missing-image")
            expect(r"Cannot read /images/missing-image")
            expect(r"zephyr> ")
        command_line = (f"qemu-system-aarch64 -M zephyr-virt -accel {accel} -cpu {cpu} ")
        if args.firmware:
            command_line += ("-bios " if args.bios else "-kernel ") + args.firmware
        else:
            command_line += ('-kernel /images/Image -initrd /images/initramfs.cpio.gz '
                             '-append "console=ttyAMA0 earlycon=pl011,0x09000000 '
                             'rdinit=/bin/sh nokaslr panic=-1 qoz.shell=1"')
        if config.manual_shell:
            send(command_line)
        if accel == "tcg":
            aliases, _ = expect(r"QEMU_TCG_JIT RW=(0x[0-9a-f]+) RX=(0x[0-9a-f]+)")
            if aliases.group(1) == aliases.group(2):
                raise RuntimeError("TCG must use distinct write and execute aliases")
        expect(r"QEMU machine=" + re.escape(config.machine) + r"-machine cpu=" +
               re.escape(cpu) + r"-arm-cpu accel=" + accel + r"-accel ")
        if args.firmware:
            if args.expect_load_error:
                expect(r"Couldn't load elf .*: The image is from incompatible architecture")
                expect(r"QEMU guest exited: -1")
            else:
                expect(r"QEMU_FIRMWARE_OK EL1 DATA BSS")
                if args.stop_firmware:
                    os.write(terminal, b"\x1d")
                expect(r"QEMU guest exited: 0")
            send("")
            expect(r"zephyr> ")
            send("fs ls /images")
            expect(r"firmware")
            expect(r"zephyr> ")
            if args.reboot_after_firmware:
                send("kernel reboot cold")
                expect(r"QEMU shell ready: qemu-system-aarch64 -help")
                send("")
                expect(r"zephyr> ")
                send(command_line)
                expect(r"QEMU_FIRMWARE_OK EL1 DATA BSS")
                expect(r"QEMU guest exited: 0")
                send("")
                expect(r"zephyr> ")
            print(f"PASS: {accel}/{cpu}; filesystem firmware ({suffix}); shell restored")
            print(f"Console: {logfile}")
            return
        if config.manual_shell:
            expect(r"qoz\.shell=1")
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
        expect(r"^VALID_BUSY_END$", timeout=20)
        expect(r"~ # ")
        send("poweroff -f")
        expect(r"reboot: Power down")
        expect(r"QEMU guest exited: 0", timeout=10)
        send("")
        expect(r"zephyr> ")
        send("fs ls /images")
        expect(r"Image")
        expect(r"zephyr> ")
        if any(marker in transcript for marker in ("QEMU_ACCEL_STATS", "QEMU execution:",
                                                    "ZEPHYR_HOST_HEARTBEAT=")):
            raise RuntimeError("Unexpected unsolicited execution or host observer output")
        send("qemu-system-aarch64 -status")
        observer, _ = expect(r"QEMU state=exited observer_ticks=(\d+) observer_max_gap_ms=(\d+)\n")
        if int(observer.group(1)) == 0 or int(observer.group(2)) >= 4000:
            raise RuntimeError("Independent Zephyr observer stalled while the guest was running")
        execution, _ = expect(r"QEMU execution: ([^\n]+)\n")
        el0 = max(map(int, re.findall(r"EL0=(\d+)", execution.group(1))), default=0)
        mmu = max(map(int, re.findall(r"MMU-on=(\d+)", execution.group(1))), default=0)
        if el0 == 0 or mmu == 0:
            raise RuntimeError("Execution statistics did not prove EL0 and guest MMU use")
        if accel == "tcg" and not re.search(r"TCG-runs=[1-9]\d*", execution.group(1)):
            raise RuntimeError("TCG execution was not observed")
        expect(r"zephyr> ")
        send(command_line)
        expect(r"QEMU already initialized; use kernel reboot cold before another guest")
        expect(r"zephyr> ")
        print(f"PASS: {accel}/{cpu}; ARM64 Linux shell; timer IRQ {before}->{after}; "
              f"host observer max gap {observer.group(2)}ms; EL0={el0}, MMU-on={mmu}; "
              "shell survives poweroff")
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
