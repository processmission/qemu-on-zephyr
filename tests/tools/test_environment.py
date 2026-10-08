# SPDX-License-Identifier: Apache-2.0

import configparser
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import environment
import project


class EnvironmentTests(unittest.TestCase):
    def setUp(self):
        original = os.environ.copy()
        self.addCleanup(os.environ.update, original)
        self.addCleanup(os.environ.clear)
        for key in ("ZEPHYR_SDK_INSTALL_DIR", "QEMU_SYSTEM_AARCH64", "https_proxy", "HTTPS_PROXY"):
            os.environ.pop(key, None)
        self.sdk = environment.find_sdk()
        self.assertIsNotNone(self.sdk, "Run make setup before make test-tools")

    def test_explicit_invalid_sdk_is_not_silently_replaced(self):
        os.environ["ZEPHYR_SDK_INSTALL_DIR"] = str(self.sdk / "sdk_version")
        with self.assertRaisesRegex(RuntimeError, "fix or unset"):
            environment.find_sdk()

    def test_saved_sdk_survives_a_new_shell(self):
        saved = Path(json.loads(environment.STATE.read_text())["sdk"])
        self.assertEqual(saved, self.sdk)
        result = subprocess.run(
            [sys.executable, str(environment.ROOT / "scripts/setup.py"), "--doctor"],
            capture_output=True, text=True, check=True,
        )
        self.assertIn(f"Zephyr SDK {environment.SDK_VERSION}: {saved}", result.stdout)

    def test_incomplete_sdk_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            incomplete = Path(directory)
            os.symlink(self.sdk / "sdk_version", incomplete / "sdk_version")
            self.assertIn("missing AArch64", environment.sdk_problem(incomplete))

    def test_explicit_sdk_compiler_runs(self):
        os.environ["ZEPHYR_SDK_INSTALL_DIR"] = str(self.sdk)
        self.assertEqual(self.sdk, environment.find_sdk())
        compiler = self.sdk / "gnu/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-gcc"
        target = subprocess.check_output([str(compiler), "-dumpmachine"], text=True).strip()
        self.assertEqual("aarch64-zephyr-elf", target)

    def test_explicit_qemu_is_validated(self):
        os.environ["QEMU_SYSTEM_AARCH64"] = str(self.sdk / "sdk_version")
        with self.assertRaisesRegex(RuntimeError, "not executable"):
            environment.qemu_path()

    def test_sdk_downloader_receives_proxy_without_overriding_explicit_settings(self):
        os.environ["HTTPS_PROXY"] = "http://proxy.invalid:8080"
        self.assertEqual(os.environ["HTTPS_PROXY"], environment.command_env(False)["https_proxy"])
        os.environ["https_proxy"] = "http://other.invalid:8080"
        self.assertEqual(os.environ["https_proxy"], environment.command_env(False)["https_proxy"])

    def test_sdk_qemu_precedes_system_qemu(self):
        qemu = environment.sdk_qemu(self.sdk)
        self.assertEqual(str(qemu), environment.qemu_path(self.sdk))
        version = subprocess.check_output([str(qemu), "--version"], text=True)
        self.assertIn("QEMU emulator version", version)

    def test_explicit_qemu_path_with_spaces_runs(self):
        with tempfile.TemporaryDirectory(prefix="qemu path ") as directory:
            executable = Path(directory) / "qemu-system-aarch64"
            executable.symlink_to(environment.sdk_qemu(self.sdk))
            os.environ["QEMU_SYSTEM_AARCH64"] = str(executable)
            configured = environment.command_env()
            self.assertEqual(str(executable), configured["QEMU_SYSTEM_AARCH64"])
            self.assertEqual(directory, configured["QEMU_BIN_PATH"])
            subprocess.run([configured["QEMU_SYSTEM_AARCH64"], "--version"],
                           check=True, capture_output=True)


class ManifestTests(unittest.TestCase):
    def test_west_workspace_stays_inside_repository(self):
        root = Path(__file__).resolve().parents[2]
        topdir = subprocess.check_output(
            [sys.executable, "-m", "west", "topdir"], cwd=root, text=True,
        ).strip()
        self.assertEqual(root, Path(topdir))

    def test_west_matches_submodule_urls_paths_and_commits(self):
        root = Path(__file__).resolve().parents[2]
        modules = configparser.ConfigParser()
        modules.read(root / ".gitmodules")
        projects = yaml.safe_load((root / "west/west.yml").read_text())["manifest"]["projects"]
        self.assertEqual({"qemu", "zephyr", "dtc", "zlib"}, {p["name"] for p in projects})
        for project in projects:
            section = f'submodule "{project["path"]}"'
            self.assertEqual(project["url"], modules[section]["url"])
            self.assertEqual(project["path"], modules[section]["path"])
            index = subprocess.check_output(
                ["git", "ls-files", "--stage", project["path"]], cwd=root, text=True,
            ).split()
            self.assertEqual("160000", index[0])
            self.assertEqual(project["revision"], index[1])


class InterruptedFetchTests(unittest.TestCase):
    def test_unborn_checkout_can_be_retried_but_staged_work_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            subprocess.run(["git", "init", "-q", str(repo)], check=True)
            project.require_clean_upstream(repo)
            (repo / "local-work").write_text("keep me\n")
            subprocess.run(["git", "-C", str(repo), "add", "local-work"], check=True)
            with self.assertRaises(subprocess.CalledProcessError):
                project.require_clean_upstream(repo)
            self.assertEqual("keep me\n", (repo / "local-work").read_text())


if __name__ == "__main__":
    unittest.main()
