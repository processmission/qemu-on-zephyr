# SPDX-License-Identifier: Apache-2.0

import argparse
from dataclasses import replace
import configparser
import fcntl
import hashlib
import os
import platform
from pathlib import Path
import shutil
import shlex
import subprocess
import sys
import tempfile
import urllib.request

import yaml

from environment import command_env, qemu_path
from qemu_config import QemuConfig, load_config
from guest_disk import disk_path, prepare_disk
from user_programs import prepare_user_programs

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


def build(profile: str = "linux", *, config: QemuConfig | None = None) -> None:
    config = config or load_config()
    config.validate_profile(profile)
    accel, cpu = config.accel, config.cpu
    prepare()
    if profile in ("linux", "native-probe") and config.mode == "system":
        assets()
    app = {"linux": "apps/qemu_linux", "native-probe": "apps/qemu_linux",
           "probe": "apps/qemu_probe", "payload": "tests/payload/app"}[profile]
    build_dir = build_directory(profile, config=config)
    env = command_env()
    command = [sys.executable, "-m", "west", "build", "-b", "qemu_cortex_a53",
               "-d", str(build_dir), str(ROOT / app), "--",
               f"-DPython3_EXECUTABLE={sys.executable}", f"-DZEPHYR_MODULES={ROOT}",
               f"-DZEPHYR_BASE={SOURCES / 'zephyr'}",
               f"-DZEPHYR_SDK_INSTALL_DIR={env['ZEPHYR_SDK_INSTALL_DIR']}",
               f"-DBUILD_VERSION={PINS['zephyr'][:12]}-qoz"]
    if profile in ("linux", "native-probe"):
        configs = ["native.conf" if accel == "zephyr" else "tcg.conf"]
        if cpu == "host":
            configs.append("host.conf")
        if config.mode == "user":
            configs.append("user.conf")
        if config.desktop:
            configs.append("desktop.conf")
        if config.nanojev:
            configs.append("nanojev.conf")
        if profile == "native-probe":
            configs.append("native-probe.conf")
        else:
            configs.append("shell.conf")
            command.append("-DCONFIG_QEMU_AUTOSTART=" + ("n" if config.manual_shell else "y"))
            startup = (f"qemu-system-aarch64 -M {config.machine} -accel {accel} -cpu {cpu} "
                       "-kernel /images/Image -initrd /images/initramfs.cpio.gz")
            if config.desktop:
                startup += ' -append "console=ttyAMA0 rdinit=/init rootfstype=ramfs panic=-1"'
            if config.mode == "user":
                if not config.manual_shell and not config.program:
                    raise RuntimeError("QEMU_MODE=user requires a program in QEMU_ARGS or QEMU_SHELL=1")
                arguments = ["qemu-aarch64", "-cpu", cpu]
                if config.strace:
                    arguments.append("-strace")
                for setting in config.user_env:
                    arguments.extend(("-E", setting))
                startup = shlex.join((*arguments, *config.program))
            startup = startup.replace("\\", "\\\\").replace('"', '\\"')
            command.append(f'-DCONFIG_QEMU_STARTUP_COMMAND="{startup}"')
        command.append("-DEXTRA_CONF_FILE=" + ";".join(configs))
        overlay = "tcg.overlay" if accel == "tcg" else "app.overlay"
        if config.mode == "user":
            overlay += ";user.overlay"
        if config.desktop:
            overlay += ";desktop.overlay"
        if config.nanojev:
            overlay += ";nanojev.overlay"
        command.append("-DDTC_OVERLAY_FILE=" + overlay)
        command.append(f'-DCONFIG_QEMU_CPU_MODEL="{cpu}"')
        if config.mode == "system":
            command.append(f'-DCONFIG_QEMU_MACHINE_MODEL="{config.machine}"')
    env["CMAKE_BUILD_PARALLEL_LEVEL"] = os.environ.get("JOBS", "8")
    # Native west owns configure/build decisions. Reconfigure on an SDK or Python
    # change, otherwise keep its incremental build path.
    cache = build_dir / "CMakeCache.txt"
    signature = "\n".join(command)
    stamp = build_dir / ".qoz-config"
    if cache.exists() and stamp.exists() and stamp.read_text() == signature:
        command = command[:command.index("--")]
    run(*command, env=env)
    stamp.write_text(signature)


def build_directory(profile: str = "linux", *, config: QemuConfig | None = None) -> Path:
    config = config or load_config()
    config.validate_profile(profile)
    if profile in ("linux", "native-probe"):
        accel, cpu = config.accel, config.cpu
        if profile == "linux" and config.mode == "user":
            profile = "user"
        if profile in ("linux", "user") and config.manual_shell:
            profile += "-shell"
        if config.nanojev:
            profile += "-nanojev"
        elif config.desktop:
            profile += "-desktop"
        if accel != "zephyr" or cpu != "cortex-a53":
            return BUILD / f"{profile}-{accel}-{cpu}"
    return BUILD / profile


def prepare_guest_disk(config: QemuConfig, *, create: bool = False) -> Path:
    source = None
    if (config.desktop and not os.environ.get("GUEST_FILES") and
            (create or not os.environ.get("GUEST_DISK"))):
        source = ROOT / ("build/nanojev-files/boot" if config.nanojev else "build/desktop-files")
        if not source.is_dir():
            raise RuntimeError("Desktop assets are missing; run make " +
                               ("nanojev-assets" if config.nanojev else "desktop-assets"))
    if (config.mode == "user" and not os.environ.get("GUEST_FILES") and
            (create or not os.environ.get("GUEST_DISK"))):
        source = prepare_user_programs(ROOT)
    mode = "nanojev" if config.nanojev else "desktop" if config.desktop else config.mode
    return prepare_disk(ROOT, create=create, mode=mode,
                        default_source=source)


def qemu_command(profile: str = "linux", interactive: bool = False,
                 *, config: QemuConfig | None = None) -> list[str]:
    config = config or load_config()
    config.validate_profile(profile)
    machine = "virt,gic-version=3" if profile == "payload" else (
        "virt,virtualization=on,secure=off,gic-version=3")
    if config.nanojev:
        machine += ",memory-backend=guest-memory"
    accel = config.accel
    if accel == "tcg" and profile in ("linux", "native-probe"):
        machine = "virt,virtualization=off,secure=off,gic-version=3"
    outer_accel = os.environ.get("HOST_ACCEL", "tcg")
    if outer_accel not in ("tcg", "hvf"):
        raise RuntimeError("HOST_ACCEL must be tcg or hvf")
    if outer_accel == "hvf" and (platform.system() != "Darwin" or platform.machine() not in ("arm64", "aarch64") or
                                 config.cpu != "host" or accel != "zephyr"):
        raise RuntimeError("HOST_ACCEL=hvf requires macOS and the native CPU=host profile")
    host_cpu = config.cpu if accel == "zephyr" else "cortex-a53"
    if host_cpu == "host" and outer_accel == "tcg":
        host_cpu = "cortex-a53"
    host_cpu = os.environ.get("HOST_CPU", host_cpu)
    if host_cpu not in ("cortex-a53", "cortex-a57", "cortex-a72", "host"):
        raise RuntimeError("HOST_CPU must be cortex-a53, cortex-a57, cortex-a72 or host")
    if (host_cpu == "host") != (outer_accel == "hvf"):
        raise RuntimeError("Outer HVF requires HOST_CPU=host; outer TCG requires a Cortex model")
    binary = qemu_path()
    if outer_accel == "hvf" and not os.environ.get("QEMU_SYSTEM_AARCH64"):
        binary = shutil.which("qemu-system-aarch64")
        if binary is None:
            raise RuntimeError("Install a QEMU build with HVF nested virtualization support")
    command = [binary,
               "-machine", machine, "-accel", ("hvf,kernel-irqchip=on" if outer_accel == "hvf" else "tcg"), "-cpu", host_cpu,
               "-m", ("10G" if config.nanojev else
                      "128M" if profile in ("probe", "payload") else "512M"), "-smp", "1",
               "-display", "none", "-monitor", "none"]
    if interactive:
        command += ["-chardev", "stdio,id=console,mux=on,signal=off", "-serial", "chardev:console"]
    else:
        command += ["-serial", "stdio"]
    if profile == "linux":
        if config.desktop:
            command += ["-device", "ramfb"]
            command[command.index("-display") + 1] = os.environ.get("QEMU_DISPLAY", "default")
        mode = "nanojev" if config.nanojev else "desktop" if config.desktop else config.mode
        disk = str(disk_path(ROOT, mode=mode)).replace(",", ",,")
        command += ["-global", "virtio-mmio.force-legacy=false",
                    "-drive", f"if=none,id=guestfiles,file={disk},format=raw,readonly=on",
                    "-device", "virtio-blk-device,bus=virtio-mmio-bus.4,drive=guestfiles"]
        if config.nanojev:
            image = ROOT / "build/nanojev-files/memory.img"
            if not image.is_file() or image.stat().st_size != 10 << 30:
                raise RuntimeError("NanoJev requires its 10 GiB sparse memory image; run make nanojev-assets")
            escaped = str(image).replace(",", ",,")
            command += ["-object", f"memory-backend-file,id=guest-memory,size=10G,mem-path={escaped},share=off"]
        if config.mode == "user":
            command += ["-object", "rng-random,id=entropy,filename=/dev/urandom",
                        "-device", "virtio-rng-device,bus=virtio-mmio-bus.5,rng=entropy"]
    return command + ["-kernel", str(build_directory(profile, config=config) / "zephyr/zephyr.elf")]


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
    parser.add_argument("action", choices=("init", "prepare", "assets", "guest-disk", "build", "run", "check", "check-firmware", "check-user",
                                         "probe", "native-probe", "test-payload", "test-arch", "clean"))
    action = parser.parse_args().action
    if action == "init":
        initialize()
    elif action == "prepare":
        prepare()
    elif action == "assets":
        assets()
    elif action == "guest-disk":
        config = load_config()
        if config.mode == "system" and not os.environ.get("GUEST_FILES"):
            assets()
        prepare_guest_disk(config, create=True)
    elif action in ("build", "run", "native-probe"):
        config = load_config()
        profile = "native-probe" if action == "native-probe" else "linux"
        build(profile, config=config)
        if action != "build":
            if profile == "linux":
                prepare_guest_disk(config)
            command = qemu_command(profile, True, config=config)
            os.execvp(command[0], command)
    elif action in ("probe", "test-payload"):
        regression("probe" if action == "probe" else "payload")
    elif action == "check":
        if load_config().mode == "user":
            raise RuntimeError("Use make check-user for QEMU_MODE=user")
        build()
        prepare_disk(ROOT)
        run(sys.executable, "apps/qemu_linux/check.py", "--no-build", env=command_env())
    elif action == "check-firmware":
        config = replace(load_config(), manual_shell=True)
        if config.mode != "system":
            raise RuntimeError("check-firmware requires QEMU_MODE=system")
        build(config=config)
        env = command_env()
        env["QEMU_SHELL"] = "1"
        run(sys.executable, "tests/firmware/check.py", env=env)
    elif action == "check-user":
        config = load_config({**os.environ, "QEMU_MODE": "user", "ACCEL": "tcg", "QEMU_SHELL": "1"})
        build(config=config)
        env = command_env()
        env.update(QEMU_MODE="user", QEMU_SHELL="1", ACCEL="tcg")
        run(sys.executable, "tests/user/check_default.py", env=env)
        run(sys.executable, "tests/user/check.py", env=env)
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
