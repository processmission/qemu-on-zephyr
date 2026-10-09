# SPDX-License-Identifier: Apache-2.0

import argparse
import asyncio
from dataclasses import replace
import hashlib
import json
from pathlib import Path
import re
import time

import pexpect
from PIL import Image
from qemu.qmp import QMPClient

from demo_terminal import TerminalRecording
from project import ROOT, build, prepare_guest_disk, qemu_command
from qemu_config import QemuConfig


def find_font(explicit: Path | None) -> Path:
    candidates = ([explicit] if explicit else []) + [
        Path("/System/Library/Fonts/Menlo.ttc"),
        Path("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"),
    ]
    for path in candidates:
        if path.is_file():
            return path
    raise RuntimeError("Set --font to a monospace TrueType font")


class DemoSession:
    def __init__(self, config: QemuConfig, directory: Path, font: Path, boot_speed: float):
        self.config = config
        self.directory = directory
        self.name = "system-desktop" if config.desktop else "linux-user"
        self.terminal = TerminalRecording(directory, self.name, font, desktop=config.desktop)
        self.boot_speed = boot_speed
        self.boot_frames: list[tuple[float, Image.Image]] = []
        self.booting = True
        self.socket = directory / f"{self.name}.sock"
        self.socket.unlink(missing_ok=True)
        command = qemu_command(config=config)
        command[command.index("-display") + 1] = "none"
        if config.desktop:
            command += ["-qmp", f"unix:{self.socket},server=on,wait=off"]
        self.command = command
        self.child = pexpect.spawn(command[0], command[1:], encoding="utf-8",
                                   codec_errors="replace", dimensions=(self.terminal.rows, 92), timeout=240)
        self.child.logfile_read = self.terminal
        self.qmp = QMPClient(self.name)
        self.frames: list[Image.Image] = []
        self.recording = False

    async def expect(self, text: str, timeout: float = 240) -> None:
        outcome = await asyncio.to_thread(self.child.expect_exact,
                                         [text, "Kernel panic", "FATAL ERROR", "DESKTOP_FAILED"],
                                         timeout=timeout)
        if outcome:
            raise RuntimeError(f"{self.name}: guest or host failed; see {self.directory}")

    async def command_line(self, text: str, prompt: str = "alpine# ") -> None:
        if prompt == "alpine# ":
            self.child.sendline(text + '; printf \'\\nQOZ_COMMAND_STATUS=%s\\n\' "$?"')
            await self.expect("\nQOZ_COMMAND_STATUS=")
            await asyncio.to_thread(self.child.expect, r"([0-9]+)\r")
            status = int(self.child.match.group(1))
            if status != 0:
                raise RuntimeError(f"{self.name}: command returned {status}: {text}; see {self.directory}")
        else:
            self.child.sendline(text)
        await self.expect(prompt)

    async def type_command(self, text: str, prompt: str = "zephyr> ") -> None:
        for character in text:
            self.child.send(character)
            await self.expect(character, timeout=10)
            await asyncio.sleep(0.025)
        self.child.sendline("")
        await self.expect(prompt)

    async def capture(self) -> None:
        while self.recording:
            started = time.monotonic()
            if self.config.desktop:
                path = self.directory / "desktop-frame.ppm"
                await self.qmp.execute("screendump", {"filename": str(path)})
                with Image.open(path) as image:
                    self.frames.append(image.convert("RGB"))
            else:
                self.frames.append(self.terminal.frame())
            await asyncio.sleep(max(0, 0.2 - (time.monotonic() - started)))

    async def capture_boot(self) -> None:
        title = f"QEMU ON ZEPHYR  /  Boot console  /  {self.boot_speed:g}x speed"
        while self.booting:
            self.boot_frames.append((time.monotonic(), self.terminal.frame(title)))
            await asyncio.sleep(0.5)
        self.boot_frames.append((time.monotonic(), self.terminal.frame(title)))

    async def desktop_actions(self) -> None:
        await asyncio.sleep(3)
        await self.command_line('xdotool search --name "Linux AArch64 inside Zephyr" windowactivate --sync')
        await self.command_line('xdotool type --delay 90 "uname -m" && xdotool key Return')
        await asyncio.sleep(2)
        await self.command_line('xdotool type --delay 65 "cat /etc/alpine-release" && xdotool key Return')
        await asyncio.sleep(2)
        await self.command_line('xdotool mousemove 690 240 click 1')
        await asyncio.sleep(3)
        await self.command_line(
            'xdotool key Escape && geometry=$(xdotool search --onlyvisible '
            '--name \'Linux AArch64 inside Zephyr\' getwindowgeometry --shell) && eval "$geometry" && '
            'drag_x=$((X + WIDTH / 2)) && drag_y=$((Y - 14)) && '
            'xdotool mousemove "$drag_x" "$drag_y" mousedown 1')
        for dx, dy in ((13, 9), (26, 18), (39, 27), (52, 36), (65, 45)):
            await self.command_line(f"xdotool mousemove $((drag_x + {dx})) $((drag_y + {dy}))")
            await asyncio.sleep(0.2)
        await self.command_line('xdotool mouseup 1')
        await asyncio.sleep(3)
        await self.command_line('kill -0 $(cat /run/desktop-*.pid) && xdotool search --onlyvisible --name "Linux AArch64 inside Zephyr" && test -c /dev/fb0 && echo DESKTOP_CHECK_OK')

    async def user_actions(self) -> None:
        await asyncio.sleep(1)
        await self.type_command("fs ls /images")
        await asyncio.sleep(1)
        await self.type_command("qemu-aarch64 /images/hello desktop-demo")
        await asyncio.sleep(2)
        await self.type_command('qemu-aarch64 -E MESSAGE="Running inside Zephyr" /images/hello')
        await asyncio.sleep(2)
        await self.type_command("qemu-aarch64 /images/hello run-again")
        await asyncio.sleep(3)

    async def run(self) -> None:
        capture_task = None
        boot_task = asyncio.create_task(self.capture_boot())
        try:
            await self.expect("zephyr> ")
            if self.config.desktop:
                await self.qmp.connect(str(self.socket))
                self.child.sendline('qemu-system-aarch64 -kernel /images/Image -initrd /images/initramfs.cpio.gz -append "console=ttyAMA0 rdinit=/init rootfstype=ramfs panic=-1"')
                await self.expect("DESKTOP_READY")
                await self.expect("alpine# ")
            self.booting = False
            await boot_task
            if not self.config.desktop:
                await self.command_line("clear", "zephyr> ")
            self.recording = True
            capture_task = asyncio.create_task(self.capture())
            if self.config.desktop:
                await self.desktop_actions()
            else:
                await self.user_actions()
            self.recording = False
            await capture_task
            if self.config.desktop:
                self.child.sendline("poweroff -f")
                await self.expect("QEMU guest exited: 0")
                await self.expect("zephyr> ")
                await self.command_line("qemu-system-aarch64 -status", "zephyr> ")
        finally:
            self.booting = False
            await boot_task
            self.recording = False
            if capture_task and not capture_task.done():
                await capture_task
            if self.qmp.runstate.name == "RUNNING":
                await self.qmp.disconnect()
            self.child.close(force=True)
            self.terminal.close()

    def save(self, output: Path) -> None:
        transcript = (self.directory / f"{self.name}.log").read_text().replace("\r", "")
        expected = (["DESKTOP_CHECK_OK", "QEMU state=exited"] if self.config.desktop else
                    ["argument: desktop-demo", "MESSAGE: Running inside Zephyr", "argument: run-again"])
        if not all(re.search(r"^" + re.escape(marker), transcript, re.MULTILINE) for marker in expected):
            raise RuntimeError(f"{self.name}: missing acceptance output")
        if any(marker in transcript for marker in ("Kernel panic", "FATAL ERROR", "ASSERTION FAIL",
                                                   "Initramfs unpacking failed")):
            raise RuntimeError(f"{self.name}: guest or host failure in transcript")
        if len(self.frames) < 2 or self.frames[0].tobytes() == self.frames[-1].tobytes():
            raise RuntimeError(f"{self.name}: no changing frames captured")
        durations = [max(20, round((next_time - timestamp) * 100 / self.boot_speed) * 10)
                     for (timestamp, _), (next_time, _) in zip(self.boot_frames, self.boot_frames[1:])]
        durations += [300] if self.boot_frames else []
        durations += [200] * len(self.frames)
        frames = [frame.quantize(colors=128, dither=Image.Dither.NONE)
                  for frame in [*[image for _, image in self.boot_frames], *self.frames]]
        output.mkdir(parents=True, exist_ok=True)
        path = output / f"{self.name}.gif"
        temporary = path.with_suffix(".gif.part")
        frames[0].save(temporary, format="GIF", save_all=True, append_images=frames[1:],
                       duration=durations, loop=0, optimize=True)
        temporary.replace(path)
        self.frames[-1].save(self.directory / f"{self.name}.png")
        with Image.open(path) as image:
            metadata = {"command": self.command, "frames": image.n_frames,
                        "size": image.size, "seconds": sum(durations) / 1000,
                        "boot_speed": self.boot_speed, "desktop_or_program_seconds": len(self.frames) / 5,
                        "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
        (self.directory / f"{self.name}.json").write_text(json.dumps(metadata, indent=2) + "\n")
        print(f"PASS: {self.name}; GIF plays for {sum(durations) / 1000:.1f}s: {path}", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description="Record real QEMU desktop and Linux user sessions as GIFs.")
    parser.add_argument("--mode", choices=("all", "desktop", "user"), default="all")
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--font", type=Path)
    parser.add_argument("--boot-speed", type=float, default=12,
                        help="startup playback speed multiplier (default: 12)")
    parser.add_argument("--output", type=Path, default=ROOT / "docs/images")
    args = parser.parse_args()
    if not 1 <= args.boot_speed <= 25:
        parser.error("--boot-speed must be between 1 and 25")
    font = find_font(args.font)
    directory = ROOT / "build/demo-recordings"
    directory.mkdir(parents=True, exist_ok=True)
    for mode in ("desktop", "user") if args.mode == "all" else (args.mode,):
        config = QemuConfig(manual_shell=True, desktop=mode == "desktop")
        if mode == "user":
            config = replace(config, mode="user", accel="tcg")
        if not args.no_build:
            build(config=config)
        prepare_guest_disk(config)
        session = DemoSession(config, directory, font, args.boot_speed)
        asyncio.run(session.run())
        session.save(args.output)


if __name__ == "__main__":
    main()
