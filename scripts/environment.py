# SPDX-License-Identifier: Apache-2.0

import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
VENV = ROOT / ".venv"
TOOLS = ROOT / ".tools"
STATE = TOOLS / "environment.json"
SDK_VERSION = "1.0.1"
HOST_COMMANDS = ("git", "make", "patch", "tar", "xz", "wget", "file", "which", "cc", "pkg-config")


def sdk_problem(path):
    version = path / "sdk_version"
    compiler = path / "gnu/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-gcc"
    if not version.is_file() or version.read_text().strip() != SDK_VERSION:
        return f"expected Zephyr SDK {SDK_VERSION} at {path}"
    if not compiler.is_file() or not os.access(compiler, os.X_OK):
        return f"missing AArch64 GNU compiler at {path}"
    return None


def sdk_qemu(sdk: Path) -> Path:
    if platform.system() == "Darwin":
        return sdk / "hosttools/usr/bin/qemu-system-aarch64"
    arch = {"arm64": "aarch64", "AMD64": "x86_64"}.get(platform.machine(), platform.machine())
    return sdk / f"hosttools/sysroots/{arch}-pokysdk-linux/usr/bin/qemu-system-aarch64"


def find_sdk():
    explicit = os.environ.get("ZEPHYR_SDK_INSTALL_DIR")
    if explicit:
        path = Path(explicit).expanduser().resolve()
        problem = sdk_problem(path)
        if problem:
            raise RuntimeError(problem + "; fix or unset ZEPHYR_SDK_INSTALL_DIR")
        return path
    candidates = [TOOLS / f"zephyr-sdk-{SDK_VERSION}"]
    if STATE.exists():
        candidates.append(Path(json.loads(STATE.read_text())["sdk"]))
    candidates.append(Path.home() / f"zephyr-sdk-{SDK_VERSION}")
    registry = Path.home() / ".cmake/packages/Zephyr-sdk"
    if registry.exists():
        for entry in sorted(registry.iterdir()):
            if entry.is_file():
                location = Path(entry.read_text().strip())
                candidates.extend((location, location.parent))
    return next((p.resolve() for p in candidates if not sdk_problem(p)), None)


def qemu_path(sdk=None):
    explicit = os.environ.get("QEMU_SYSTEM_AARCH64")
    if explicit:
        found = shutil.which(explicit)
        if not found:
            raise RuntimeError(f"QEMU_SYSTEM_AARCH64 is not executable: {explicit}")
        return found
    sdk = sdk or find_sdk()
    if sdk and sdk_qemu(sdk).is_file() and os.access(sdk_qemu(sdk), os.X_OK):
        return str(sdk_qemu(sdk))
    found = shutil.which("qemu-system-aarch64")
    if found:
        return found
    raise RuntimeError("No AArch64 QEMU found; run make setup or set QEMU_SYSTEM_AARCH64")


def command_env(require_sdk=True):
    env = dict(os.environ)
    # requests accepts uppercase proxy variables, while the SDK's wget expects
    # lowercase ones. Preserve explicit lowercase settings when both are set.
    for key in ("http_proxy", "https_proxy", "no_proxy"):
        if key not in env and key.upper() in env:
            env[key] = env[key.upper()]
    env["PATH"] = str(VENV / "bin") + os.pathsep + env.get("PATH", "")
    env["VIRTUAL_ENV"] = str(VENV)
    env["PYTHONNOUSERSITE"] = "1"
    prepared = ROOT / "build/sources/zephyr"
    env["ZEPHYR_BASE"] = str(prepared if prepared.exists() else ROOT / "upstream/zephyr")
    # Make the generated tree authoritative even if the caller uses another workspace.
    env["ZEPHYR_TOOLCHAIN_VARIANT"] = "zephyr/gnu"
    if require_sdk:
        sdk = find_sdk()
        if sdk is None:
            raise RuntimeError("Zephyr SDK is not configured; run make setup")
        env["ZEPHYR_SDK_INSTALL_DIR"] = str(sdk)
        env["QEMU_SYSTEM_AARCH64"] = qemu_path(sdk)
        env["QEMU_BIN_PATH"] = str(Path(env["QEMU_SYSTEM_AARCH64"]).parent)
    return env


def host_problems() -> list[str]:
    problems = []
    system, machine = platform.system(), platform.machine()
    supported = {"Linux": ("x86_64", "aarch64", "arm64"), "Darwin": ("arm64", "aarch64")}
    if machine not in supported.get(system, ()):
        problems.append("supported hosts: Linux x86_64/AArch64 and macOS Apple Silicon "
                        f"with Zephyr SDK {SDK_VERSION}; found {system} {machine}")
    if sys.version_info < (3, 12):
        problems.append("Python 3.12 or newer is required by the pinned Zephyr")
    commands = HOST_COMMANDS + (("dtc", "gperf") if system == "Darwin" else ())
    for command in commands:
        if not shutil.which(command):
            problems.append(f"missing host command: {command}")
    return problems


def doctor():
    problems = host_problems()
    python = VENV / "bin/python"
    if not python.is_file():
        problems.append("project Python environment is missing; run make setup")
    else:
        for command in ("west", "cmake", "ninja"):
            path = VENV / "bin" / command
            if not path.is_file():
                problems.append(f"project tool missing: {command}; run make setup")
            else:
                result = subprocess.run([str(path), "--version"], capture_output=True, text=True)
                if result.returncode:
                    problems.append(f"{command} could not run: {result.stderr.strip()}")
                else:
                    print(result.stdout.splitlines()[0])
        result = subprocess.run([str(python), "-m", "pip", "check"], capture_output=True, text=True)
        if result.returncode:
            problems.append(result.stdout.strip())
    try:
        sdk = find_sdk()
        if sdk is None:
            problems.append("Zephyr SDK is missing; run make setup")
        else:
            print(f"Zephyr SDK {SDK_VERSION}: {sdk}")
            qemu = qemu_path(sdk)
            result = subprocess.run([qemu, "--version"], capture_output=True, text=True)
            if result.returncode:
                problems.append(f"QEMU could not run: {result.stderr.strip()}")
            else:
                print(f"{result.stdout.splitlines()[0]}: {qemu}")
    except RuntimeError as error:
        problems.append(str(error))
    if not (ROOT / ".west/config").is_file():
        problems.append("local west workspace is missing; run make setup")
    print("Host:", platform.system(), platform.machine(), f"Python {platform.python_version()}")
    for problem in problems:
        print("ERROR:", problem)
    if problems:
        raise RuntimeError("Environment incomplete. See above; host packages: make host-deps")
    print("PASS: build environment ready")
