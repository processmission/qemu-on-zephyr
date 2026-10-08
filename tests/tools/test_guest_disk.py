# SPDX-License-Identifier: Apache-2.0

import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import shutil

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from guest_disk import mke2fs_path, prepare_disk


class GuestDiskTests(unittest.TestCase):
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
            tools = Path(mke2fs_path()).parent
            before = disk.stat().st_mtime_ns
            self.assertEqual(disk, prepare_disk(root))
            self.assertEqual(before, disk.stat().st_mtime_ns)
            initrd = source / "initramfs.cpio.gz"
            shutil.copyfile(ROOT / "downloads/generic-arm64-rootfs.cpio.gz", initrd)
            prepare_disk(root)
            for path in (image, initrd):
                recovered = root / path.name
                subprocess.run([str(tools / "debugfs"), "-R",
                                f"dump /{path.name} {recovered}", str(disk)],
                               check=True, capture_output=True)
                self.assertEqual(hashlib.sha256(path.read_bytes()).digest(),
                                 hashlib.sha256(recovered.read_bytes()).digest())
            subprocess.run([str(tools / "e2fsck"), "-fn", str(disk)],
                           check=True, capture_output=True)
            os.environ["GUEST_DISK"] = str(disk)
            self.assertEqual(disk, prepare_disk(root))
