# SPDX-License-Identifier: Apache-2.0

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from project import ROOT


def prepare_nanojev() -> Path:
    if not shutil.which("docker"):
        raise RuntimeError("NanoJev image preparation requires Docker with Linux ARM64 support")
    destination = ROOT / "build/nanojev-files"
    destination.mkdir(parents=True, exist_ok=True)
    image = "qemu-on-zephyr-nanojev:cpu-v1"
    subprocess.run(["docker", "build", "--platform", "linux/arm64", "-t", image,
                    str(ROOT / "samples/nanojev")], check=True)
    with tempfile.TemporaryDirectory(prefix="nanojev-", dir=ROOT / "build") as temporary:
        output = Path(temporary)
        subprocess.run(["docker", "run", "--rm", "--platform", "linux/arm64",
                        "-v", f"{output}:/output", image], check=True)
        metadata = json.loads(subprocess.check_output(["docker", "image", "inspect", image]))
        checksums = {}
        for path in sorted(output.rglob("*")):
            if path.is_file():
                with path.open("rb") as contents:
                    checksums[str(path.relative_to(output))] = hashlib.file_digest(contents, "sha256").hexdigest()
        (output / "provenance.json").write_text(json.dumps(
            {"docker_image": metadata[0]["Id"], "sha256": checksums}, indent=2) + "\n")
        for path in sorted(output.rglob("*")):
            target = destination / path.relative_to(output)
            if path.is_dir():
                target.mkdir(exist_ok=True)
            else:
                path.replace(target)
    print(f"NanoJev assets: {destination}")
    return destination


if __name__ == "__main__":
    argparse.ArgumentParser(description="Build the NanoJev CPU Linux desktop image.").parse_args()
    prepare_nanojev()
