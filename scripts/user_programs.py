# SPDX-License-Identifier: Apache-2.0

import fcntl
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

from environment import command_env


def prepare_user_programs(root: Path) -> Path:
    env = command_env()
    compiler = Path(env["ZEPHYR_SDK_INSTALL_DIR"]) / "gnu/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-gcc"
    source = root / "samples/linux-user/hello"
    build = root / "build"
    programs = build / "user-programs"
    programs.mkdir(parents=True, exist_ok=True)
    destination = programs / "hello"
    stamp = build / ".user-programs.json"
    command = [str(compiler), "-nostdlib", "-static", "-ffreestanding", "-fno-builtin",
               "-mcpu=cortex-a53", "-O2", "-T", str(source / "link.ld"),
               "-Wl,--build-id=none", str(source / "start.S"), str(source / "main.c"), "-lc"]
    fingerprint = hashlib.sha256(json.dumps(command).encode())
    fingerprint.update(subprocess.check_output([str(compiler), "--version"], env=env))
    for path in (source / "start.S", source / "main.c", source / "link.ld"):
        fingerprint.update(path.read_bytes())
    signature = fingerprint.hexdigest()
    with (build / ".user-programs.lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if destination.is_file() and stamp.is_file():
            state = json.loads(stamp.read_text())
            if (state.get("source") == signature and
                    state.get("binary") == hashlib.sha256(destination.read_bytes()).hexdigest()):
                return programs
        with tempfile.TemporaryDirectory(prefix="user-programs-", dir=build) as temporary:
            output = Path(temporary) / "hello"
            subprocess.run([*command, "-o", str(output)], env=env, check=True)
            state = {"source": signature, "binary": hashlib.sha256(output.read_bytes()).hexdigest()}
            output.replace(destination)
        stamp.write_text(json.dumps(state, indent=2) + "\n")
    return programs
