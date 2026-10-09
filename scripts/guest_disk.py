# SPDX-License-Identifier: Apache-2.0

import fcntl
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def e2fsprogs_tool_path(name: str) -> str:
    brew = shutil.which("brew")
    if brew:
        result = subprocess.run([brew, "--prefix", "e2fsprogs"],
                                capture_output=True, text=True)
        candidate = Path(result.stdout.strip()) / "sbin" / name
        if result.returncode == 0 and candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate)
    found = shutil.which(name)
    if found:
        return found
    raise RuntimeError(f"{name} is missing; install e2fsprogs with make host-deps")


def mke2fs_path() -> str:
    return e2fsprogs_tool_path("mke2fs")


def disk_path(root: Path, *, mode: str = "system") -> Path:
    name = {"system": "guest-disk.img", "user": "user-disk.img",
            "desktop": "desktop-disk.img", "nanojev": "nanojev-boot.img"}[mode]
    return Path(os.environ.get("GUEST_DISK") or root / "build" / name).expanduser().resolve()


def prepare_disk(root: Path, *, create: bool = False, mode: str = "system",
                 default_source: Path | None = None) -> Path:
    destination = disk_path(root, mode=mode)
    if os.environ.get("GUEST_DISK") and not create:
        if not destination.is_file():
            raise RuntimeError(f"Missing GUEST_DISK: {destination}; run make guest-disk")
        return destination
    build = root / "build"
    build.mkdir(exist_ok=True)
    with (build / ".guest-disk.lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        with tempfile.TemporaryDirectory(prefix="guest-files-", dir=build) as directory:
            source = Path(os.environ.get("GUEST_FILES") or default_source or directory).expanduser().resolve()
            if not source.is_dir():
                raise RuntimeError(f"GUEST_FILES must be a directory: {source}")
            if source == destination or source in destination.parents:
                raise RuntimeError("GUEST_DISK must be outside GUEST_FILES")
            if not os.environ.get("GUEST_FILES") and default_source is None:
                if mode == "user":
                    raise RuntimeError("User mode requires a program directory for disk creation")
                shutil.copyfile(root / "downloads/tuxrun-arm64-Image", source / "Image")
                shutil.copyfile(root / "downloads/generic-arm64-rootfs.cpio.gz",
                                source / "initramfs.cpio.gz")
            signature = hashlib.sha256(b"qoz-ext2-v2-single-group-4096-128-filetype")
            total_size = 0
            for path in sorted(source.rglob("*")):
                if path.is_symlink():
                    raise RuntimeError(f"GUEST_FILES requires regular files and directories: {path}")
                signature.update(str(path.relative_to(source)).encode())
                if path.is_file():
                    total_size += path.stat().st_size
                    with path.open("rb") as contents:
                        signature.update(hashlib.file_digest(contents, "sha256").digest())
            if total_size > 112 << 20:
                raise RuntimeError("GUEST_FILES exceeds 112 MiB; Zephyr Ext2 supports a single "
                                   "128 MiB block group, including filesystem metadata")
            stamp = destination.with_name(destination.name + ".json")
            state = {"path": str(destination), "sha256": signature.hexdigest()}
            if destination.is_file() and stamp.is_file() and json.loads(stamp.read_text()) == state:
                return destination
            destination.parent.mkdir(parents=True, exist_ok=True)
            temporary = destination.with_name(destination.name + ".part")
            size_mib = min(128, max(64, (total_size * 5 // 4 + (16 << 20)) // (1 << 20) + 1))
            try:
                subprocess.run([mke2fs_path(), "-q", "-F", "-t", "ext2", "-b", "4096",
                                "-I", "128", "-O", "none,filetype", "-d", str(source),
                                str(temporary), str(size_mib * 256)], check=True)
                temporary.replace(destination)
            finally:
                temporary.unlink(missing_ok=True)
            stamp.write_text(json.dumps(state, indent=2) + "\n")
    print(f"Guest filesystem: {destination} (mounted at /images)", flush=True)
    return destination
