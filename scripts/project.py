# SPDX-License-Identifier: Apache-2.0

import argparse
import configparser
import fcntl
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import urllib.request

import yaml

from environment import command_env, qemu_path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
SOURCES = BUILD / "sources"
DOWNLOADS = ROOT / "downloads"
MANIFEST = ROOT / "west/west.yml"
PROJECTS = yaml.safe_load(MANIFEST.read_text())["manifest"]["projects"]
PINS = {project["name"]: project["revision"] for project in PROJECTS}
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
    kwargs.setdefault("cwd", ROOT)
    return subprocess.run(command, check=True, **kwargs)


def require_clean_upstream(repo):
    # These also work for an unborn HEAD left by an interrupted fetch.
    run("git", "-C", str(repo), "diff", "--exit-code", "--")
    run("git", "-C", str(repo), "diff", "--cached", "--exit-code", "--")


def initialize():
    env = command_env(require_sdk=False)
    west = [sys.executable, "-m", "west"]
    if not (ROOT / ".west/config").exists():
        init_env = env.copy()
        init_env.pop("ZEPHYR_BASE", None)
        # Start outside ancestor workspaces; the absolute manifest selects ROOT.
        run(*west, "init", "-l", str(ROOT / "west"), cwd=ROOT.anchor, env=init_env)
    else:
        path = run(*west, "config", "--local", "manifest.path", env=env,
                   capture_output=True, text=True).stdout.strip()
        manifest = run(*west, "config", "--local", "manifest.file", env=env,
                       capture_output=True, text=True).stdout.strip()
        if (path, manifest) not in (("west", "west.yml"), (".", "west/west.yml")):
            raise RuntimeError("Existing .west uses another manifest; refusing to replace it")
    run(*west, "config", "--local", "manifest.path", ".", env=env)
    run(*west, "config", "--local", "manifest.file", "west/west.yml", env=env)
    run(*west, "config", "--local", "update.narrow", "true", env=env)
    # Never update a developer's modified upstream checkout implicitly.
    for name in PINS:
        repo = ROOT / "upstream" / name
        if (repo / ".git").exists():
            require_clean_upstream(repo)
    run(*west, "update", env=env)
    # Register existing west checkouts as Git submodules without moving their
    # .git directories (directory renames can fail on layered filesystems).
    run("git", "submodule", "init")
    configure_west(env)


def configure_west(env=None):
    env = env or command_env(require_sdk=False)
    base = "build/sources/zephyr" if (SOURCES / "zephyr").exists() else "upstream/zephyr"
    config = configparser.ConfigParser()
    config.read(ROOT / ".west/config")
    for key, value in (("base", base), ("base-prefer", "configfile")):
        if config.get("zephyr", key, fallback=None) != value:
            run(sys.executable, "-m", "west", "config", "--local", "zephyr." + key, value, env=env)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def patch_series(directory):
    names = [line.strip() for line in (directory / "series").read_text().splitlines()
             if line.strip() and not line.lstrip().startswith("#")]
    if len(names) != len(set(names)) or any(
            Path(name).name != name or not name.endswith(".patch") for name in names):
        raise RuntimeError(f"Invalid patch series: {directory}")
    if set(names) != {p.name for p in directory.glob("*.patch")}:
        raise RuntimeError(f"Patch series must list every patch exactly once: {directory}")
    return [directory / name for name in names]


def prepare():
    BUILD.mkdir(exist_ok=True)
    with (BUILD / ".prepare.lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        prepare_locked()


def prepare_locked():
    if not (ROOT / ".west/config").exists() or any(
            not (ROOT / "upstream" / name / ".git").exists() for name in PINS):
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
    inputs = [MANIFEST, Path(__file__).resolve()]
    for directory in ("src", "patches"):
        inputs.extend(p for p in sorted((ROOT / directory).rglob("*")) if p.is_file())
    for path in inputs:
        fingerprint.update(str(path.relative_to(ROOT)).encode())
        fingerprint.update(path.read_bytes())
    signature = fingerprint.hexdigest()
    stamp = SOURCES / ".prepared"
    if stamp.exists() and stamp.read_text().strip() == signature:
        configure_west()
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
                for patch in patch_series(patch_dir):
                    run("patch", "--batch", "--forward", "-p1", "-d", str(destination),
                        "-i", str(patch))
                shutil.copytree(ROOT / "src" / name, destination, dirs_exist_ok=True)
        (staging / ".prepared").write_text(signature + "\n")
        if SOURCES.exists():
            shutil.rmtree(SOURCES)
        staging.rename(SOURCES)
    print(f"Prepared patched sources: {SOURCES}", flush=True)
    configure_west()


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
    accel = os.environ.get("ACCEL", "zephyr")
    cpu = os.environ.get("CPU", "cortex-a53")
    if accel not in ("zephyr", "tcg") or cpu not in ("cortex-a53", "cortex-a57", "cortex-a72"):
        raise RuntimeError("Use ACCEL=zephyr|tcg and CPU=cortex-a53|cortex-a57|cortex-a72")
    if profile == "native-probe" and accel != "zephyr":
        raise RuntimeError("native-probe requires ACCEL=zephyr")
    prepare()
    if profile in ("linux", "native-probe"):
        assets()
    app = {"linux": "apps/qemu_linux", "native-probe": "apps/qemu_linux",
           "probe": "apps/qemu_probe", "payload": "tests/payload/app"}[profile]
    build_dir = build_directory(profile)
    env = command_env()
    command = [sys.executable, "-m", "west", "build", "-b", "qemu_cortex_a53",
               "-d", str(build_dir), str(ROOT / app), "--",
               f"-DPython3_EXECUTABLE={sys.executable}", f"-DZEPHYR_MODULES={ROOT}",
               f"-DZEPHYR_BASE={SOURCES / 'zephyr'}",
               f"-DZEPHYR_SDK_INSTALL_DIR={env['ZEPHYR_SDK_INSTALL_DIR']}",
               f"-DBUILD_VERSION={PINS['zephyr'][:12]}-qoz"]
    if profile in ("linux", "native-probe"):
        configs = ["native.conf" if accel == "zephyr" else "tcg.conf"]
        if profile == "native-probe":
            configs.append("native-probe.conf")
        command.append("-DEXTRA_CONF_FILE=" + ";".join(configs))
        command.append("-DDTC_OVERLAY_FILE=" + ("tcg.overlay" if accel == "tcg" else "app.overlay"))
        command.append(f'-DCONFIG_QEMU_CPU_MODEL="{cpu}"')
    env["CMAKE_BUILD_PARALLEL_LEVEL"] = os.environ.get("JOBS", "8")
    # Native west owns configure/build decisions. Reconfigure on an SDK or Python
    # change, otherwise keep its incremental build path.
    cache = build_dir / "CMakeCache.txt"
    signature = "\n".join((str(sys.executable), env["ZEPHYR_SDK_INSTALL_DIR"], PINS["zephyr"]))
    signature += f"\n{accel}\n{cpu}"
    stamp = build_dir / ".qoz-config"
    if cache.exists() and stamp.exists() and stamp.read_text() == signature:
        command = command[:command.index("--")]
    run(*command, env=env)
    stamp.write_text(signature)


def build_directory(profile="linux"):
    if profile in ("linux", "native-probe"):
        accel = os.environ.get("ACCEL", "zephyr")
        cpu = os.environ.get("CPU", "cortex-a53")
        if accel != "zephyr" or cpu != "cortex-a53":
            return BUILD / f"{profile}-{accel}-{cpu}"
    return BUILD / profile


def qemu_command(profile="linux", interactive=False):
    machine = "virt,gic-version=3" if profile == "payload" else (
        "virt,virtualization=on,secure=off,gic-version=3")
    accel = os.environ.get("ACCEL", "zephyr")
    if accel == "tcg" and profile in ("linux", "native-probe"):
        machine = "virt,virtualization=off,secure=off,gic-version=3"
    host_cpu = os.environ.get("CPU", "cortex-a53") if accel == "zephyr" else "cortex-a53"
    host_cpu = os.environ.get("HOST_CPU", host_cpu)
    if host_cpu not in ("cortex-a53", "cortex-a57", "cortex-a72"):
        raise RuntimeError("HOST_CPU must be cortex-a53, cortex-a57 or cortex-a72")
    command = [qemu_path(),
               "-machine", machine, "-accel", "tcg", "-cpu", host_cpu,
               "-m", "128M" if profile in ("probe", "payload") else "512M", "-smp", "1",
               "-display", "none", "-monitor", "none"]
    if interactive:
        command += ["-chardev", "stdio,id=console,mux=on,signal=off", "-serial", "chardev:console"]
    else:
        command += ["-serial", "stdio"]
    return command + ["-kernel", str(build_directory(profile) / "zephyr/zephyr.elf")]


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
    parser = argparse.ArgumentParser(description="Prepare sources, build and run QEMU on Zephyr.")
    parser.add_argument("action", choices=("init", "prepare", "assets", "build", "run", "check",
                                         "probe", "native-probe", "test-payload", "test-arch", "clean"))
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
    elif action == "check":
        build()
        run(sys.executable, "apps/qemu_linux/check.py", "--no-build", env=command_env())
    elif action == "test-arch":
        prepare()
        run(sys.executable, "-m", "west", "twister", "-p", "qemu_cortex_a53",
            "-T", str(SOURCES / "zephyr/tests/arch/arm64/arm64_el2"),
            "-T", str(SOURCES / "zephyr/tests/arch/arm64/fpu_sharing"),
            "-T", str(SOURCES / "zephyr/tests/subsys/virtualization/zhv"),
            "--outdir", str(BUILD / "twister"), "--inline-logs", "-j", os.environ.get("JOBS", "8"),
            env=command_env())
    elif action == "clean" and BUILD.exists():
        shutil.rmtree(BUILD)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
