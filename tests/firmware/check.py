# SPDX-License-Identifier: Apache-2.0

import os
from pathlib import Path
import subprocess
import sys
import shutil

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from environment import command_env
from guest_disk import prepare_disk
from qemu_config import load_config
from project import build_directory


def main() -> None:
    env = command_env()
    directory = build_directory() / "firmware-files"
    directory.mkdir(parents=True, exist_ok=True)
    toolchain = Path(env["ZEPHYR_SDK_INSTALL_DIR"]) / "gnu/aarch64-zephyr-elf/bin"
    elf = directory / "firmware.elf"
    binary = directory / "firmware.bin"
    subprocess.run([str(toolchain / "aarch64-zephyr-elf-gcc"), "-nostdlib", "-static",
                    "-Wl,--build-id=none", "-T", str(ROOT / "tests/firmware/link.ld"),
                    str(ROOT / "tests/firmware/entry.S"), "-o", str(elf)], check=True)
    subprocess.run([str(toolchain / "aarch64-zephyr-elf-objcopy"), "-O", "binary",
                    str(elf), str(binary)], check=True)
    waiting = directory / "waiting-firmware.elf"
    subprocess.run([str(toolchain / "aarch64-zephyr-elf-gcc"), "-nostdlib", "-static",
                    "-DFIRMWARE_WAIT", "-Wl,--build-id=none", "-T",
                    str(ROOT / "tests/firmware/link.ld"), str(ROOT / "tests/firmware/entry.S"),
                    "-o", str(waiting)], check=True)
    wrong_arch = directory / "wrong-architecture.elf"
    shutil.copyfile(elf, wrong_arch)
    with wrong_arch.open("r+b") as stream:
        parsed = ELFFile(stream)
        header = parsed.header
        header["e_machine"] = "EM_X86_64"
        encoded = parsed.structs.Elf_Ehdr.build(header)
        stream.seek(0)
        stream.write(encoded)
    os.environ["GUEST_FILES"] = str(directory)
    os.environ["GUEST_DISK"] = str(build_directory() / "firmware-disk.img")
    prepare_disk(ROOT, create=True)
    env.update(GUEST_DISK=os.environ["GUEST_DISK"])
    cpu_options = ["--guest-cpu", "cortex-a72"] if load_config().accel == "tcg" else []
    for name, options in ((elf.name, ["--reboot-after-firmware"]), (binary.name, ["--bios"]),
                          (wrong_arch.name, ["--expect-load-error"]),
                          (waiting.name, ["--stop-firmware"])):
        subprocess.run([sys.executable, str(ROOT / "apps/qemu_linux/check.py"),
                        "--no-build", "--firmware", "/images/" + name, *options, *cpu_options],
                       env=env, cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
