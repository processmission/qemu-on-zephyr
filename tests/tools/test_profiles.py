# SPDX-License-Identifier: Apache-2.0
import os
from pathlib import Path
import subprocess
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import project
from qemu_config import load_config


class ProfileTests(unittest.TestCase):
    def setUp(self):
        original = os.environ.copy()
        self.addCleanup(os.environ.update, original)
        self.addCleanup(os.environ.clear)
        for key in ("ACCEL", "CPU", "HOST_CPU", "QEMU_ARGS"):
            os.environ.pop(key, None)

    def test_tcg_guest_model_does_not_require_matching_outer_cpu(self):
        os.environ.update(ACCEL="tcg", CPU="cortex-a72")
        command = project.qemu_command()
        self.assertIn("virtualization=off", command[command.index("-machine") + 1])
        self.assertEqual("cortex-a53", command[command.index("-cpu") + 1])
        self.assertIn("linux-tcg-cortex-a72", command[-1])
        subprocess.run([command[0], "--version"], check=True, capture_output=True)

    def test_native_profile_uses_matching_outer_cpu_by_default(self):
        os.environ.update(ACCEL="zephyr", CPU="cortex-a57")
        command = project.qemu_command()
        self.assertIn("virtualization=on", command[command.index("-machine") + 1])
        self.assertEqual("cortex-a57", command[command.index("-cpu") + 1])
        self.assertIn("linux-zephyr-cortex-a57", command[-1])

    def test_qemu_options_select_the_build_and_outer_cpu_policy(self):
        os.environ.update(ACCEL="zephyr", CPU="cortex-a53",
                          QEMU_ARGS="-M type=zephyr-virt,accel=tcg -cpu cortex-a72")
        config = load_config()
        command = project.qemu_command(config=config)
        self.assertEqual("virt,virtualization=off,secure=off,gic-version=3",
                         command[command.index("-machine") + 1])
        self.assertEqual("cortex-a53", command[command.index("-cpu") + 1])
        self.assertEqual(project.BUILD / "linux-tcg-cortex-a72",
                         project.build_directory(config=config))
        self.assertEqual(str(project.build_directory(config=config) / "zephyr/zephyr.elf"),
                         command[-1])

        os.environ["QEMU_ARGS"] = "-M zephyr-virt -accel zephyr -cpu cortex-a57"
        command = project.qemu_command()
        self.assertEqual("virt,virtualization=on,secure=off,gic-version=3",
                         command[command.index("-machine") + 1])
        self.assertEqual("cortex-a57", command[command.index("-cpu") + 1])
        self.assertIn("linux-zephyr-cortex-a57", command[-1])

    def test_invalid_profiles_fail_before_preparing_or_downloading(self):
        for values, profile in (({"ACCEL": "invalid", "CPU": "cortex-a53"}, "build"),
                                ({"ACCEL": "zephyr", "CPU": "max"}, "build"),
                                ({"ACCEL": "tcg", "CPU": "cortex-a53"}, "native-probe")):
            with self.subTest(values=values):
                env = dict(os.environ, **values)
                env["ZEPHYR_SDK_INSTALL_DIR"] = str(project.MANIFEST)
                result = subprocess.run(
                    [sys.executable, str(project.ROOT / "scripts/project.py"), profile],
                    env=env, capture_output=True, text=True,
                )
                self.assertNotEqual(0, result.returncode)
                self.assertEqual("", result.stdout)
                self.assertRegex(result.stderr, "Unsupported accelerator:|Unsupported CPU:|"
                                 "native-probe requires ACCEL=")
