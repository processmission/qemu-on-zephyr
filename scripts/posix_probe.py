# SPDX-License-Identifier: Apache-2.0
import argparse
import hashlib
import json
import os
from pathlib import Path
import selectors
import subprocess
import sys
import time
from environment import command_env
from guest_disk import mke2fs_path


ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "docs/experiments/posix-native"
BUILD = ROOT / "build/posix-native"


SOURCE_FILES = (
    "subsys/portability/posix/options/device_io.c",
    "subsys/portability/posix/options/fd_mgmt.c",
    "subsys/portability/posix/options/fs.c",
    "subsys/portability/posix/options/clock.c",
    "subsys/portability/posix/options/cond.c",
    "subsys/portability/posix/options/mutex.c",
    "subsys/portability/posix/options/pthread.c",
    "subsys/portability/posix/options/signal.c",
    "subsys/portability/posix/options/mprotect.c",
    "subsys/portability/posix/options/multi_process.c",
    "lib/os/zvfs/zvfs_fdtable.c",
    "include/zephyr/posix/sys/mman.h",
    "include/zephyr/posix/unistd.h",
)


def capture_runtime(command: list[str], env: dict[str, str], timeout: float) -> tuple[bytes, int]:
    process = subprocess.Popen(command, cwd=ROOT, env=env, stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    output = bytearray()
    try:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for key, events in selector.select(timeout=0.2):
                block = os.read(key.fileobj.fileno(), 65536)
                if not block:
                    break
                output.extend(block)
            if b"POSIX_PROBE_DONE" in output or process.poll() is not None:
                break
        if process.poll() is None:
            process.stdin.write(b"\x01x")
            process.stdin.flush()
            process.wait(timeout=5)
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        selector.close()
        process.stdin.close()
        process.stdout.close()
        (BUILD / "runtime.log").write_bytes(output)
    return bytes(output), process.returncode


def main() -> None:
    parser = argparse.ArgumentParser(description="Run the native Zephyr POSIX research fixture")
    parser.add_argument("--disk", type=Path, default=ROOT / "build/user-disk.img")
    parser.add_argument("--timeout", type=float, default=45.0)
    options = parser.parse_args()
    disk = options.disk.expanduser().resolve()
    if not disk.is_file():
        parser.error("Disk missing; run make guest-disk QEMU_MODE=user from the project root")
    if options.timeout <= 0:
        parser.error("--timeout must be positive")
    if not (ROOT / "build/sources/.prepared").is_file():
        parser.error("Prepared sources missing; run make prepare from the project root")

    env = command_env()
    scratch = BUILD / "tmp"
    scratch.mkdir(parents=True, exist_ok=True)
    env["TMPDIR"] = str(scratch)
    env["CMAKE_BUILD_PARALLEL_LEVEL"] = os.environ.get("JOBS", "8")
    sdk = Path(env["ZEPHYR_SDK_INSTALL_DIR"])
    zephyr = Path(env["ZEPHYR_BASE"])
    build = [
        sys.executable, "-m", "west", "build", "-b", "qemu_cortex_a53",
        "-d", str(BUILD), str(APP), "--",
        f"-DPython3_EXECUTABLE={sys.executable}",
        f"-DZEPHYR_BASE={zephyr}", f"-DZEPHYR_SDK_INSTALL_DIR={sdk}",
        f"-DZEPHYR_MODULES={ROOT}", "-DBUILD_VERSION=posix-native-probe",
    ]
    with (BUILD / "build.log").open("w") as output:
        subprocess.run(build, cwd=ROOT, env=env, stdout=output,
                       stderr=subprocess.STDOUT, check=True)
    disk_argument = str(disk).replace(",", ",,")
    runtime = [
        env["QEMU_SYSTEM_AARCH64"], "-machine",
        "virt,virtualization=on,secure=off,gic-version=3", "-accel", "tcg",
        "-cpu", "cortex-a53", "-m", "512M", "-smp", "1", "-display", "none",
        "-monitor", "none", "-chardev", "stdio,id=console,mux=on,signal=off",
        "-serial", "chardev:console", "-global", "virtio-mmio.force-legacy=false",
        "-drive", f"if=none,id=guestfiles,file={disk_argument},format=raw,readonly=on",
        "-device", "virtio-blk-device,bus=virtio-mmio-bus.4,drive=guestfiles",
        "-kernel", str(BUILD / "zephyr/zephyr.elf"),
    ]
    output, returncode = capture_runtime(runtime, env, options.timeout)
    records = [json.loads(line.removeprefix("POSIX_PROBE "))
               for line in output.decode(errors="replace").splitlines()
               if line.startswith("POSIX_PROBE ")]
    (BUILD / "records.json").write_text(json.dumps(records, indent=2) + "\n")

    def portable(argument: str) -> str:
        return argument.replace(str(ROOT), "$PROJECT_ROOT").replace(str(sdk), "$ZEPHYR_SDK_INSTALL_DIR")

    def version(command: list[str]) -> str:
        return subprocess.check_output(command, env=env, text=True).splitlines()[0]

    artifacts = [APP / name for name in ("main.c", "prj.conf", "CMakeLists.txt", "app.overlay")]
    artifacts.append(Path(__file__).resolve())
    artifacts += [BUILD / "zephyr/.config", BUILD / "zephyr/zephyr.elf", disk]
    artifacts += [zephyr / filename for filename in SOURCE_FILES]
    hashes = {}
    for filename in artifacts:
        label = str(filename.relative_to(ROOT)) if filename.is_relative_to(ROOT) else filename.name
        with filename.open("rb") as source:
            hashes[label] = hashlib.file_digest(source, "sha256").hexdigest()
    debugfs = Path(mke2fs_path()).with_name("debugfs")
    disk_inspection = subprocess.check_output(
        [str(debugfs), "-R", "stat /hello", str(disk)], env=env,
        stderr=subprocess.STDOUT, text=True)
    metadata = {
        "zephyr_revision": subprocess.check_output(
            ["git", "-C", str(ROOT / "upstream/zephyr"), "rev-parse", "HEAD"], text=True).strip(),
        "prepared_sources_signature": (ROOT / "build/sources/.prepared").read_text().strip(),
        "build_command": [portable(argument) for argument in build],
        "runtime_command": [portable(argument) for argument in runtime],
        "qemu_version": version([env["QEMU_SYSTEM_AARCH64"], "--version"]),
        "compiler_version": version([str(sdk / "gnu/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-gcc"), "--version"]),
        "qemu_exit_status": returncode,
        "sha256": hashes,
        "disk_inode_inspection": "\n".join(disk_inspection.splitlines()[:5]),
    }
    (BUILD / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    if b"POSIX_PROBE_DONE" not in output or returncode:
        raise RuntimeError("Probe did not finish cleanly; inspect build/posix-native/runtime.log")
    names = {record["name"] for record in records}
    if not {"file_metadata", "thread_result", "sigwait"}.issubset(names):
        raise RuntimeError("A probe phase did not complete; inspect build/posix-native/records.json")
    print(f"Recorded {len(records)} observations in build/posix-native/records.json; QEMU exited {returncode}")


if __name__ == "__main__":
    main()
