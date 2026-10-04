#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Prepare pristine upstream sources plus local overlays, build and run."""

import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
SOURCES = BUILD / "sources"
DOWNLOADS = ROOT / "downloads"
PINS = json.loads((ROOT / "dependencies.json").read_text())
ASSETS = {
    "tuxrun-arm64-Image": (
        "https://storage.tuxboot.com/buildroot/20241119/arm64/Image",
        "b74743c5e89e1cea0f73368d24ae0ae85c5204ff84be3b5e9610417417d2f235",
    ),
    "generic-arm64-rootfs.cpio.gz": (
        "https://raw.githubusercontent.com/groeck/linux-build-test/"
        "86b2be1384d41c8c388e63078a847f1e1c4cb1de/rootfs/arm64/rootfs.cpio.gz",
        "7c0b16d1853772f6f4c3ca63e789b3b9ff4936efac9c8a01fb0c98c05c7a7648",
    ),
}


def run(*command, **kwargs):
    return subprocess.run(command, cwd=ROOT, check=True, **kwargs)


def initialize():
    run("git", "submodule", "update", "--init", "--depth", "1",
        *(f"upstream/{name}" for name in PINS))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare():
    BUILD.mkdir(exist_ok=True)
    with (BUILD / ".prepare.lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        prepare_locked()


def prepare_locked():
    if any(not (ROOT / "upstream" / name / ".git").exists() for name in PINS):
        initialize()
    for name, revision in PINS.items():
        repo = ROOT / "upstream" / name
        actual = run("git", "-C", str(repo), "rev-parse", "HEAD",
                     capture_output=True, text=True).stdout.strip()
        if actual != revision:
            raise RuntimeError(f"{repo}: expected {revision}, found {actual}; run make init")
        run("git", "-C", str(repo), "diff", "--exit-code", "HEAD", "--",
            stdout=subprocess.DEVNULL)

    fingerprint = hashlib.sha256()
    inputs = [ROOT / "dependencies.json", Path(__file__).resolve()]
    for directory in ("src", "patches"):
        inputs.extend(p for p in sorted((ROOT / directory).rglob("*")) if p.is_file())
    for path in inputs:
        fingerprint.update(str(path.relative_to(ROOT)).encode())
        fingerprint.update(path.read_bytes())
    signature = fingerprint.hexdigest()
    stamp = SOURCES / ".prepared"
    if stamp.exists() and stamp.read_text().strip() == signature:
        print("Prepared sources are current.", flush=True)
        return

    BUILD.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="prepare-", dir=BUILD) as temporary:
        staging = Path(temporary)
        for name, revision in PINS.items():
            destination = (staging / name if name in ("qemu", "zephyr")
                           else staging / "qemu/subprojects" / name)
            destination.mkdir(parents=True, exist_ok=True)
            archive = staging / "source.tar"
            run("git", "-C", str(ROOT / "upstream" / name), "archive",
                "--format=tar", f"--output={archive}", revision)
            run("tar", "-xf", str(archive), "-C", str(destination))
            archive.unlink()
            patch_dir = ROOT / "patches" / name
            if patch_dir.exists():
                for patch in sorted(patch_dir.glob("*.patch")):
                    run("patch", "--batch", "--forward", "-p1", "-d", str(destination),
                        "-i", str(patch))
                shutil.copytree(ROOT / "src" / name, destination, dirs_exist_ok=True)
        (staging / ".prepared").write_text(signature + "\n")
        if SOURCES.exists():
            shutil.rmtree(SOURCES)
        staging.rename(SOURCES)
    print(f"Prepared patched sources: {SOURCES}", flush=True)


def assets():
    DOWNLOADS.mkdir(exist_ok=True)
    for name, (url, expected) in ASSETS.items():
        destination = DOWNLOADS / name
        if destination.exists():
            if digest(destination) != expected:
                raise RuntimeError(f"SHA256 mismatch: {destination}; remove it and retry")
            print(f"Verified {name}", flush=True)
            continue
        temporary = destination.with_suffix(destination.suffix + ".part")
        print(f"Downloading {url}", flush=True)
        try:
            with urllib.request.urlopen(url, timeout=120) as source, temporary.open("wb") as output:
                shutil.copyfileobj(source, output)
            if digest(temporary) != expected:
                raise RuntimeError(f"SHA256 mismatch for downloaded {name}")
            temporary.replace(destination)
        finally:
            temporary.unlink(missing_ok=True)


def build(profile="linux"):
    prepare()
    if profile in ("linux", "native-probe"):
        assets()
    app = {"linux": "apps/qemu_linux", "native-probe": "apps/qemu_linux",
           "probe": "apps/qemu_probe", "payload": "tests/payload/app"}[profile]
    env = dict(os.environ, ZEPHYR_BASE=str(SOURCES / "zephyr"))
    command = ["cmake", "-S", str(ROOT / app), "-B", str(BUILD / profile), "-G", "Ninja",
               "-DBOARD=qemu_cortex_a53", f"-DPython3_EXECUTABLE={sys.executable}",
               f"-DZEPHYR_BASE={SOURCES / 'zephyr'}", f"-DZEPHYR_MODULES={ROOT}"]
    if profile == "native-probe":
        command.append("-DEXTRA_CONF_FILE=native-probe.conf")
    if not (BUILD / profile / "CMakeCache.txt").exists():
        run(*command, env=env)
    run("cmake", "--build", str(BUILD / profile), "--parallel", os.environ.get("JOBS", "8"))


def qemu_command(profile="linux", interactive=False):
    machine = "virt,gic-version=3" if profile == "payload" else (
        "virt,virtualization=on,secure=off,gic-version=3")
    command = [os.environ.get("QEMU_SYSTEM_AARCH64", "qemu-system-aarch64"),
               "-machine", machine, "-accel", "tcg", "-cpu", "cortex-a53",
               "-m", "128M" if profile in ("probe", "payload") else "512M", "-smp", "1",
               "-display", "none", "-monitor", "none"]
    if interactive:
        command += ["-chardev", "stdio,id=console,mux=on,signal=off", "-serial", "chardev:console"]
    else:
        command += ["-serial", "stdio"]
    return command + ["-kernel", str(BUILD / profile / "zephyr/zephyr.elf")]


def regression(profile):
    build(profile)
    try:
        result = subprocess.run(qemu_command(profile), stdin=subprocess.DEVNULL,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20)
        output = result.stdout
        if result.returncode != 0:
            raise RuntimeError(output.decode(errors="replace"))
    except subprocess.TimeoutExpired as error:
        output = error.stdout or b""
    logfile = BUILD / f"{profile}.log"
    logfile.write_bytes(output)
    text = output.decode(errors="replace")
    print(text)
    expected = {
        "probe": ("QEMU Zephyr host EL2", "PROBE_OK", "QEMU probe result=0"),
        "payload": ("PAYLOAD_FS_TEST_EL: EL1", "PAYLOAD_FS_TEST_FAILURES: 0",
                    "PAYLOAD_FS_TEST_RESULT: PASS"),
    }[profile]
    if not all(marker in text for marker in expected) or any(
            marker in text for marker in ("ASSERTION FAIL", "FATAL ERROR", "Kernel panic")):
        raise RuntimeError(f"{profile} regression failed; see {logfile}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("init", "prepare", "assets", "build", "run",
                                         "probe", "native-probe", "test-payload", "clean"))
    action = parser.parse_args().action
    if action == "init":
        initialize()
    elif action == "prepare":
        prepare()
    elif action == "assets":
        assets()
    elif action in ("build", "run", "native-probe"):
        profile = "native-probe" if action == "native-probe" else "linux"
        build(profile)
        if action != "build":
            os.execvp(qemu_command(profile, True)[0], qemu_command(profile, True))
    elif action in ("probe", "test-payload"):
        regression("probe" if action == "probe" else "payload")
    elif action == "clean" and BUILD.exists():
        shutil.rmtree(BUILD)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
