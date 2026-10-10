# SPDX-License-Identifier: Apache-2.0

import argparse
import asyncio
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import time

import pexpect
from PIL import Image
from qemu.qmp import QMPClient

from demo_terminal import TerminalRecording
from demo_video import DemoVideo
from project import ROOT, build, prepare_guest_disk, qemu_command
from qemu_config import CPUS, QemuConfig
from record_demos import find_font


async def validate(no_build: bool, steps: int, timeout: int, output: Path, boot_speed: float, maze_size: int, cpu: str) -> None:
    config = QemuConfig(desktop=True, nanojev=True, manual_shell=True, cpu=cpu)
    if not no_build:
        build(config=config)
    prepare_guest_disk(config)
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    socket = output / "display.sock"
    socket.unlink(missing_ok=True)
    command = qemu_command(config=config)
    command[command.index("-display") + 1] = "none"
    command += ["-qmp", f"unix:{socket},server=on,wait=off"]
    version = subprocess.check_output([command[0], "--version"], text=True).splitlines()[0]
    (output / "runtime.json").write_text(json.dumps({"qemu": version, "command": command}, indent=2) + "\n")
    print(f"Outer QEMU: {version}", flush=True)
    qmp = QMPClient("nanojev")
    terminal = TerminalRecording(output, "serial", find_font(None), desktop=True)
    video = DemoVideo(output)
    child = pexpect.spawn(command[0], command[1:], encoding="utf-8", codec_errors="replace", timeout=timeout)
    child.logfile_read = terminal
    frames: list[Image.Image] = []
    captures: list[dict] = []
    started = time.monotonic()
    preview_task = None
    recording = True
    maze = False
    boot_seconds = None

    async def expect(marker: str, limit: int = timeout) -> None:
        index = await asyncio.to_thread(child.expect_exact,
                                       [marker, "NANOJEV_FAILED", "Kernel panic", "FATAL ERROR"], timeout=limit)
        if index:
            raise RuntimeError(f"NanoJev guest failed; inspect {output / 'serial.log'}")

    async def shell(text: str) -> None:
        child.sendline(text + '; printf \'\\nNANOJEV_COMMAND_STATUS=%s\\n\' "$?"')
        await expect("\nNANOJEV_COMMAND_STATUS=")
        await asyncio.to_thread(child.expect, r"([0-9]+)\r")
        if int(child.match.group(1)):
            raise RuntimeError(f"Guest command failed: {text}")
        await expect("nanojev# ")

    async def capture(name: str) -> None:
        path = output / f"{name}.ppm"
        await asyncio.wait_for(qmp.execute("screendump", {"filename": str(path)}), timeout=30)
        with Image.open(path) as image:
            frame = image.convert("RGB")
            frames.append(frame)
            frame.save(output / f"{name}.png")
        captures.append({"name": name, "seconds_since_start": time.monotonic() - started})

    async def preview() -> None:
        nonlocal boot_seconds
        last_preview = 0.0
        while recording:
            before = time.monotonic()
            if maze:
                path = output / "current.ppm"
                await asyncio.wait_for(qmp.execute("screendump", {"filename": str(path)}), timeout=30)
                with Image.open(path) as source:
                    frame = source.convert("RGB")
                if boot_seconds is None:
                    boot_seconds = max(video.frames, int((before - started) * video.fps)) / video.fps
                if before - last_preview >= 5:
                    frame.save(output / "current.png")
                    last_preview = before
            else:
                frame = terminal.frame("QEMU ON ZEPHYR / Boot and NanoJev initialization")
            await asyncio.to_thread(video.append, frame, before - started)
            await asyncio.sleep(max(0, 1 / video.fps - (time.monotonic() - before)))

    try:
        preview_task = asyncio.create_task(preview())
        await expect("zephyr> ", 180)
        await qmp.connect(str(socket))
        child.sendline(f'qemu-system-aarch64 -M zephyr-virt -accel zephyr -cpu {config.cpu} '
                       '-kernel /images/Image -initrd /images/initramfs.cpio.gz '
                       '-append "console=ttyAMA0 rdinit=/init rootfstype=ramfs panic=-1"')
        await expect("DESKTOP_READY", 600)
        print("NanoJev Linux desktop ready; loading the CPU model", flush=True)
        await expect("NANOJEV_READY", timeout)
        ready_details = await asyncio.to_thread(child.readline)
        print("NANOJEV_READY" + ready_details.rstrip(), flush=True)
        await shell('xdotool search --onlyvisible --name "NanoJev - QEMU on Zephyr" windowactivate --sync')
        size_key = {5: "F5", 8: "F8", 16: "F9"}[maze_size]
        child.sendline(f"xdotool key {size_key}")
        await expect(f"NANOJEV_MAZE_READY size={maze_size}")
        await shell("sleep 1")
        await capture("ready")
        maze = True
        await asyncio.sleep(0.5)
        if steps:
            for index in range(steps):
                child.sendline('xdotool key Right')
                await expect(f"NANOJEV_STEP index={index + 1} ", timeout)
                await shell("sleep 2")
                await capture(f"step-{index + 1:02d}")
        else:
            child.sendline('xdotool key space')
            deadline = time.monotonic() + timeout
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("NanoJev did not complete the maze within the runtime limit")
                outcome = await asyncio.to_thread(
                    child.expect,
                    [r"NANOJEV_(?:INFERENCE|STEP|FINISHED) [^\r\n]*\r*\n",
                     "NANOJEV_FAILED", "Kernel panic", "FATAL ERROR"], timeout=remaining)
                if outcome:
                    raise RuntimeError(f"NanoJev guest failed; inspect {output / 'serial.log'}")
                progress = child.match.group(0).rstrip()
                print(progress, flush=True)
                if progress.startswith("NANOJEV_FINISHED "):
                    break
            await shell("sleep 3")
            await capture("complete")
        recording = False
        await preview_task
        child.sendline("printf '\\nNANOJEV_REPORT_BEGIN\\n'; cat /run/nanojev/report.json; printf '\\nNANOJEV_REPORT_END\\n'")
        await expect("\nNANOJEV_REPORT_BEGIN")
        await expect("\nNANOJEV_REPORT_END")
        report = json.loads(child.before)
        if (report["device"] != "cpu" or report["precision"] != "fp32" or
                report["guest_machine"] != "QEMU Zephyr ARM virt profile" or
                report["model_loads"] != 1 or not report["steps"] or
                report["initial_state"]["size"] != maze_size or
                report["initial_state"]["topology"] != "loops" or
                (steps and len(report["steps"]) != steps) or (not steps and not report["finished"]) or
                report["quantization"] != "dynamic-int8" or report["quantized_linear_modules"] <= 0 or
                report["inference_calls"] < 1 or not report["inferences"]):
            raise RuntimeError("Missing real guest CPU inference evidence")
        if report["model"] != json.loads((ROOT / "build/nanojev-files/model.json").read_text()):
            raise RuntimeError("Guest checkpoint identity differs from the image provenance")
        walls = set(map(tuple, report["initial_state"]["walls"]))
        passages = {(row, column) for row in range(maze_size) for column in range(maze_size)} - walls
        degrees = [sum((row + dr, column + dc) in passages
                       for dr, dc in ((-1, 0), (0, 1), (1, 0), (0, -1))) for row, column in passages]
        junctions = sum(degree >= 3 for degree in degrees)
        dead_ends = sum(degree == 1 for degree in degrees)
        loops = sum(degrees) // 2 - len(passages) + 1
        if loops < 1 or (maze_size >= 8 and (junctions < 1 or dead_ends < 1)):
            raise RuntimeError("The recorded maze lacks the expected branches, loops or dead ends")
        print(f"Maze geometry: {junctions} junctions, {loops} loops, {dead_ends} dead ends", flush=True)
        for inference in report["inferences"]:
            execution = inference["response"]["execution"]
            if (execution["device"] != "cpu" or execution["forward_passes"] <= 0 or
                    execution["network_model_calls"] != 0 or inference["seconds"] <= 0):
                raise RuntimeError("Invalid CPU execution evidence")
            for state in inference["response"]["states"]:
                for answer in state["answers"].values():
                    value = answer["p_true"]
                    if not math.isfinite(value) or not 0 <= value <= 1:
                        raise RuntimeError("Invalid NanoJev model probability")
        await expect("nanojev# ")
        await shell("kill -0 $(cat /run/desktop-*.pid) && test -c /dev/fb0 && test -b /dev/pmem0")
        child.sendline("busybox poweroff -f")
        await expect("QEMU guest exited: 0", 120)
        await expect("zephyr> ")
        child.sendline("qemu-system-aarch64 -status")
        await expect("zephyr> ")
    finally:
        recording = False
        try:
            if preview_task:
                await preview_task
        finally:
            try:
                await asyncio.to_thread(video.close)
            finally:
                try:
                    if qmp.runstate.name == "RUNNING":
                        await asyncio.wait_for(qmp.disconnect(), timeout=10)
                finally:
                    child.close(force=True)
                    terminal.close()
    transcript = (output / "serial.log").read_text()
    if not all(marker in transcript for marker in (
            "QEMU Linux host EL2", "zephyr-accel", "RAM=3072MiB", "QEMU state=exited")):
        raise RuntimeError("Missing ZHV host or guest lifecycle evidence")
    if re.search(r"FATAL ERROR|ASSERTION FAIL|Kernel panic|NANOJEV_FAILED|Out of memory: Killed", transcript):
        raise RuntimeError("A guest or host failure occurred")
    if frames[0].tobytes() == frames[-1].tobytes():
        raise RuntimeError("The desktop did not display the model decisions")
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    timing = await asyncio.to_thread(video.edit, boot_seconds, boot_speed)
    with (output / "nanojev.gif").open("rb") as image:
        checksum = hashlib.file_digest(image, "sha256").hexdigest()
    metadata = {"command": command, "captures": captures, "timing": timing, "gif_sha256": checksum}
    (output / "recording.json").write_text(json.dumps(metadata, indent=2) + "\n")
    if not steps and not report["goal_reached"]:
        raise RuntimeError(f"NanoJev finished without reaching the goal; see {output / 'report.json'}")
    print(f"PASS: NanoJev CPU inference and ZHV desktop interaction; {output}", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description="Validate NanoJev inference through the ZHV Linux desktop.")
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--steps", type=int, default=0, help="0 runs the complete maze; positive values limit diagnostics")
    parser.add_argument("--timeout", type=int, default=3600)
    parser.add_argument("--boot-speed", type=float, default=12)
    parser.add_argument("--maze-size", type=int, choices=(5, 8, 16), default=16)
    parser.add_argument("--cpu", choices=CPUS, default="cortex-a53")
    parser.add_argument("--output", type=Path, default=ROOT / "build/nanojev-validation")
    args = parser.parse_args()
    if args.steps < 0 or args.timeout < 1 or not 1 <= args.boot_speed <= 25:
        parser.error("Use a nonnegative step count, a positive timeout and boot speed between 1 and 25")
    asyncio.run(validate(args.no_build, args.steps, args.timeout, args.output, args.boot_speed, args.maze_size, args.cpu))


if __name__ == "__main__":
    main()
