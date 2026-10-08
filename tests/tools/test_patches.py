# SPDX-License-Identifier: Apache-2.0
"""Patch-series completeness, ordering and path validation."""
from pathlib import Path
from email import policy
from email.parser import Parser
import sys
import subprocess
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from project import patch_series


class PatchSeriesTests(unittest.TestCase):
    def test_explicit_order(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("first.patch", "second.patch"):
                (root / name).touch()
            (root / "series").write_text("# prerequisites first\nsecond.patch\nfirst.patch\n")
            self.assertEqual(["second.patch", "first.patch"],
                             [p.name for p in patch_series(root)])

    def test_invalid_or_incomplete_series_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "change.patch").touch()
            for content in ("", "change.patch\nchange.patch\n", "../change.patch\n", "absent.patch\n"):
                with self.subTest(content=content):
                    (root / "series").write_text(content)
                    with self.assertRaises(RuntimeError):
                        patch_series(root)

    def test_repository_patches_have_signed_messages(self):
        root = Path(__file__).resolve().parents[2]
        for project in ("qemu", "zephyr"):
            patches = patch_series(root / "patches" / project)
            self.assertGreater(len(patches), 1)
            for path in patches:
                message = Parser(policy=policy.default).parsestr(path.read_text())
                self.assertTrue(str(message["Subject"]).startswith("[PATCH] "))
                trailers = subprocess.check_output(
                    ["git", "interpret-trailers", "--parse"],
                    input=str(message["Subject"]) + "\n\n" + message.get_payload(), text=True,
                ).splitlines()
                self.assertIn(f"Signed-off-by: {message['From']}", trailers)
