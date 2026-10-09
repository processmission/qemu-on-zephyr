# SPDX-License-Identifier: Apache-2.0

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from project import ROOT


def prepare_desktop() -> Path:
    if not shutil.which("docker"):
        raise RuntimeError("Desktop image preparation requires Docker with Linux ARM64 support")
    destination = ROOT / "build/desktop-files"
    destination.mkdir(parents=True, exist_ok=True)
    image = "qemu-on-zephyr-desktop:alpine-3.23.4"
    subprocess.run(["docker", "build", "--platform", "linux/arm64", "-t", image,
                    str(ROOT / "samples/linux-desktop")], check=True)
    with tempfile.TemporaryDirectory(prefix="desktop-", dir=ROOT / "build") as temporary:
        output = Path(temporary)
        subprocess.run(["docker", "run", "--rm", "--platform", "linux/arm64",
                        "-v", f"{output}:/output", image], check=True)
        metadata = json.loads(subprocess.check_output(["docker", "image", "inspect", image]))
        checksums = {}
        for name in ("Image", "initramfs.cpio.gz", "packages.txt", "kernel.config"):
            with (output / name).open("rb") as contents:
                checksums[name] = hashlib.file_digest(contents, "sha256").hexdigest()
        (output / "provenance.json").write_text(json.dumps(
            {"docker_image": metadata[0]["Id"], "sha256": checksums}, indent=2) + "\n")
        for path in output.iterdir():
            path.replace(destination / path.name)
    print(f"Desktop assets: {destination}")
    return destination


if __name__ == "__main__":
    argparse.ArgumentParser(description="Build the Alpine Linux ARM64 desktop initramfs.").parse_args()
    prepare_desktop()
