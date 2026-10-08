# SPDX-License-Identifier: Apache-2.0

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from qemu_config import QemuConfig, load_config

ROOT = Path(__file__).resolve().parents[2]


class QemuConfigTests(unittest.TestCase):
    def test_qemu_options_override_only_explicit_legacy_defaults(self):
        env = {"ACCEL": "tcg", "CPU": "cortex-a57", "QEMU_ARGS": "-cpu cortex-a72"}
        self.assertEqual(QemuConfig(accel="tcg", cpu="cortex-a72"), load_config(env))
        env["QEMU_ARGS"] = "-M zephyr-virt -accel zephyr"
        self.assertEqual(QemuConfig(accel="zephyr", cpu="cortex-a57"), load_config(env))
        env["QEMU_ARGS"] = ""
        self.assertEqual(QemuConfig(accel="tcg", cpu="cortex-a57"), load_config(env))

    def test_machine_properties_and_accelerator_spellings(self):
        for arguments in (
            "-M zephyr-virt,accel=tcg -cpu cortex-a72",
            "-machine type=zephyr-virt,accel=tcg -cpu cortex-a72",
            "-M accel=tcg,type=zephyr-virt -accel accel=tcg -cpu 'cortex-a72'",
        ):
            with self.subTest(arguments=arguments):
                self.assertEqual(QemuConfig(accel="tcg", cpu="cortex-a72"),
                                 load_config({"QEMU_ARGS": arguments}))

    def test_conflicting_or_unsupported_options_are_rejected(self):
        for arguments in (
            "-M zephyr-virt,accel=tcg -accel zephyr",
            "-M zephyr-virt,accel=tcg,accel=zephyr",
            "-M zephyr-virt,virtualization=on",
            "-M zephyr-virt,",
            "-M virt",
            "-accel kvm",
            "-accel zephyr:tcg",
            "-accel tcg -accel zephyr",
            "-accel accel=",
            "-cpu max",
            "-cpu cortex-a53 -cpu cortex-a57",
            "-M ''",
            "-accel ''",
            "-cpu ''",
            "-cpu",
            "-cpu 'cortex-a53",
            "-device virtio-net",
        ):
            with self.subTest(arguments=arguments), self.assertRaises(RuntimeError):
                load_config({"QEMU_ARGS": arguments})

    def test_tcg_is_rejected_for_the_native_diagnostic(self):
        config = load_config({"QEMU_ARGS": "-M zephyr-virt,accel=tcg"})
        with self.assertRaisesRegex(RuntimeError, "native-probe requires"):
            config.validate_profile("native-probe")


class MakeQemuArgsTests(unittest.TestCase):
    def run_make(self, arguments: str) -> subprocess.CompletedProcess[str]:
        env = dict(os.environ, ACCEL="zephyr", CPU="cortex-a53",
                   ZEPHYR_SDK_INSTALL_DIR=str(ROOT / "west/west.yml"))
        return subprocess.run(
            ["make", "--no-print-directory", "run", f"QEMU_ARGS={arguments}"],
            cwd=ROOT, env=env, capture_output=True, text=True,
        )

    def test_selection_help_exits_before_sdk_selection_or_build(self):
        for argument, selections in (
            ("-M help", ("zephyr-virt",)),
            ("-accel help", ("zephyr", "tcg")),
            ("-cpu help", ("cortex-a53", "cortex-a57", "cortex-a72")),
        ):
            with self.subTest(argument=argument):
                result = self.run_make(argument)
                self.assertEqual(0, result.returncode, result.stderr)
                for selection in selections:
                    self.assertIn(selection, result.stdout)
                self.assertNotIn("Prepared", result.stdout)

    def test_invalid_machine_fails_before_source_preparation(self):
        result = self.run_make("-M unsupported-machine")
        self.assertNotEqual(0, result.returncode)
        self.assertIn("Unsupported machine:", result.stderr)
        self.assertNotIn("Prepared", result.stdout)

    def test_shell_metacharacters_remain_argument_data(self):
        with tempfile.TemporaryDirectory(prefix="qemu-arguments-") as directory:
            marker = Path(directory) / "shell-executed"
            result = self.run_make(f"-M zephyr-virt -cpu 'cortex-a53; touch {marker}'")
            self.assertNotEqual(0, result.returncode)
            self.assertIn("Unsupported CPU:", result.stderr)
            self.assertFalse(marker.exists())


if __name__ == "__main__":
    unittest.main()
