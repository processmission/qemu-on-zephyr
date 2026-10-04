# SPDX-License-Identifier: Apache-2.0
"""Regression coverage for SDK selection and dependency ownership."""

import configparser
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import environment
import project


class EnvironmentTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.tools = self.root / ".tools"
        self.tools.mkdir()
        for name, value in (("TOOLS", self.tools), ("STATE", self.tools / "environment.json")):
            handle = patch.object(environment, name, value)
            handle.start()
            self.addCleanup(handle.stop)
        handle = patch.dict(os.environ, {}, clear=True)
        handle.start()
        self.addCleanup(handle.stop)
        handle = patch.object(Path, "home", return_value=self.root)
        handle.start()
        self.addCleanup(handle.stop)

    def sdk(self, name, version=environment.SDK_VERSION):
        path = self.root / name
        path.mkdir(parents=True)
        (path / "sdk_version").write_text(version)
        compiler = path / "gnu/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-gcc"
        compiler.parent.mkdir(parents=True)
        compiler.write_text("#!/bin/sh\nexit 0\n")
        compiler.chmod(0o755)
        return path

    def test_explicit_invalid_sdk_is_not_silently_replaced(self):
        self.sdk(f"zephyr-sdk-{environment.SDK_VERSION}")
        os.environ["ZEPHYR_SDK_INSTALL_DIR"] = str(self.root / "wrong")
        with self.assertRaisesRegex(RuntimeError, "fix or unset"):
            environment.find_sdk()

    def test_saved_sdk_survives_a_new_shell(self):
        sdk = self.sdk("custom-sdk")
        environment.STATE.write_text(json.dumps({"sdk": str(sdk)}))
        self.assertEqual(sdk, environment.find_sdk())

    def test_incomplete_sdk_is_rejected(self):
        sdk = self.sdk("incomplete")
        (sdk / "gnu/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-gcc").unlink()
        self.assertIn("missing AArch64", environment.sdk_problem(sdk))

    def test_registered_sdk_is_reused(self):
        sdk = self.sdk("registered-sdk")
        registry = self.root / ".cmake/packages/Zephyr-sdk"
        registry.mkdir(parents=True)
        (registry / "entry").write_text(str(sdk / "cmake"))
        self.assertEqual(sdk, environment.find_sdk())

    def test_explicit_qemu_is_validated(self):
        os.environ["QEMU_SYSTEM_AARCH64"] = str(self.root / "missing-qemu")
        with self.assertRaisesRegex(RuntimeError, "not executable"):
            environment.qemu_path()

    def test_sdk_downloader_receives_proxy_without_overriding_explicit_settings(self):
        os.environ["HTTPS_PROXY"] = "http://proxy.invalid:8080"
        self.assertEqual(os.environ["HTTPS_PROXY"], environment.command_env(False)["https_proxy"])
        os.environ["https_proxy"] = "http://other.invalid:8080"
        self.assertEqual(os.environ["https_proxy"], environment.command_env(False)["https_proxy"])

    def test_sdk_qemu_precedes_system_qemu(self):
        sdk = self.sdk("sdk")
        qemu = environment.sdk_qemu(sdk)
        qemu.parent.mkdir(parents=True)
        qemu.write_text("#!/bin/sh\nexit 0\n")
        qemu.chmod(0o755)
        with patch.object(environment.shutil, "which", return_value="/system/qemu"):
            self.assertEqual(str(qemu), environment.qemu_path(sdk))


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
