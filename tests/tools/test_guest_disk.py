# SPDX-License-Identifier: Apache-2.0

import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from guest_disk import disk_path, e2fsprogs_tool_path, prepare_disk
from user_programs import prepare_user_programs


class GuestDiskTests(unittest.TestCase):
    def test_user_disk_contains_the_built_elf_and_respects_explicit_inputs(self):
        original = os.environ.copy()
        self.addCleanup(os.environ.update, original)
        self.addCleanup(os.environ.clear)
        os.environ.pop("GUEST_FILES", None)
        os.environ.pop("GUEST_DISK", None)
        programs = prepare_user_programs(ROOT)
        binary = programs / "hello"
        before = binary.stat().st_mtime_ns
        self.assertEqual(programs, prepare_user_programs(ROOT))
        self.assertEqual(before, binary.stat().st_mtime_ns)
        with binary.open("rb") as stream:
            elf = ELFFile(stream)
            self.assertEqual("EM_AARCH64", elf.header.e_machine)
            self.assertEqual("ET_EXEC", elf.header.e_type)
            self.assertNotIn("PT_INTERP", [segment.header.p_type for segment in elf.iter_segments()])
        with tempfile.TemporaryDirectory(prefix="user-disk-", dir=ROOT / "build") as directory:
            root = Path(directory)
            disk = prepare_disk(root, mode="user", default_source=programs)
            self.assertEqual(root / "build/user-disk.img", disk)
            self.assertNotEqual(disk_path(root), disk)
            recovered = root / "hello"
            debugfs = e2fsprogs_tool_path("debugfs")
            subprocess.run([debugfs, "-R", f"dump /hello {recovered}", str(disk)],
                           check=True, capture_output=True)
            self.assertEqual(binary.read_bytes(), recovered.read_bytes())
            os.environ["GUEST_DISK"] = str(disk)
            os.environ["GUEST_FILES"] = str(root / "unavailable")
            self.assertEqual(disk, prepare_disk(root, mode="user"))
            self.assertEqual(disk, disk_path(root, mode="system"))
            custom = root / "custom"
            custom.mkdir()
            shutil.copyfile(binary, custom / "my-program")
            os.environ["GUEST_FILES"] = str(custom)
            os.environ["GUEST_DISK"] = str(root / "custom.img")
            disk = prepare_disk(root, mode="user", create=True, default_source=programs)
            recovered.unlink()
            subprocess.run([str(debugfs), "-R", f"dump /my-program {recovered}", str(disk)],
                           check=True, capture_output=True)
            self.assertEqual(binary.read_bytes(), recovered.read_bytes())

    def test_real_payloads_survive_ext2_creation_and_source_updates(self):
        original = os.environ.copy()
        self.addCleanup(os.environ.update, original)
        self.addCleanup(os.environ.clear)
        with tempfile.TemporaryDirectory(prefix="guest-disk-", dir=ROOT / "build") as directory:
            root = Path(directory)
            source = root / "files"
            source.mkdir()
            image = source / "Image"
            shutil.copyfile(ROOT / "downloads/tuxrun-arm64-Image", image)
            os.environ["GUEST_FILES"] = str(source)
            os.environ.pop("GUEST_DISK", None)
            disk = prepare_disk(root)
            debugfs = e2fsprogs_tool_path("debugfs")
            e2fsck = e2fsprogs_tool_path("e2fsck")
            before = disk.stat().st_mtime_ns
            self.assertEqual(disk, prepare_disk(root))
            self.assertEqual(before, disk.stat().st_mtime_ns)
            initrd = source / "initramfs.cpio.gz"
            shutil.copyfile(ROOT / "downloads/generic-arm64-rootfs.cpio.gz", initrd)
            prepare_disk(root)
            for path in (image, initrd):
                recovered = root / path.name
                subprocess.run([debugfs, "-R",
                                f"dump /{path.name} {recovered}", str(disk)],
                               check=True, capture_output=True)
                self.assertEqual(hashlib.sha256(path.read_bytes()).digest(),
                                 hashlib.sha256(recovered.read_bytes()).digest())
            subprocess.run([e2fsck, "-fn", str(disk)],
                           check=True, capture_output=True)
            os.environ["GUEST_DISK"] = str(disk)
            self.assertEqual(disk, prepare_disk(root))
