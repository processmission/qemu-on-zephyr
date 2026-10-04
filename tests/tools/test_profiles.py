# SPDX-License-Identifier: Apache-2.0
"""Keep profile selection and outer CPU policy consistent with the runner."""
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import project


class ProfileTests(unittest.TestCase):
    def test_tcg_guest_model_does_not_require_matching_outer_cpu(self):
        with patch.dict(os.environ, {"ACCEL": "tcg", "CPU": "cortex-a72"}, clear=True), \
             patch.object(project, "qemu_path", return_value="/qemu"):
            command = project.qemu_command()
            self.assertIn("virtualization=off", command[command.index("-machine") + 1])
            self.assertEqual("cortex-a53", command[command.index("-cpu") + 1])
            self.assertIn("linux-tcg-cortex-a72", command[-1])

    def test_native_profile_uses_matching_outer_cpu_by_default(self):
        with patch.dict(os.environ, {"ACCEL": "zephyr", "CPU": "cortex-a57"}, clear=True), \
             patch.object(project, "qemu_path", return_value="/qemu"):
            command = project.qemu_command()
            self.assertIn("virtualization=on", command[command.index("-machine") + 1])
            self.assertEqual("cortex-a57", command[command.index("-cpu") + 1])
            self.assertIn("linux-zephyr-cortex-a57", command[-1])

    def test_invalid_profiles_fail_before_preparing_or_downloading(self):
        for values, profile in (({"ACCEL": "invalid"}, "linux"),
                                ({"CPU": "max"}, "linux"),
                                ({"ACCEL": "tcg"}, "native-probe")):
            with self.subTest(values=values), patch.dict(os.environ, values, clear=True), \
                 patch.object(project, "prepare") as prepare:
                with self.assertRaises(RuntimeError):
                    project.build(profile)
                prepare.assert_not_called()
