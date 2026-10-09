# SPDX-License-Identifier: Apache-2.0

import hashlib
import json
from pathlib import Path

from huggingface_hub import snapshot_download

REVISION = "047b927b30882a1138fc504821b82ac145a4b81a"
VARIANT = "variants/local_atomic_seed17"
WEIGHTS_SHA256 = "2b06e4423861f47da7ca53ad9c23a26aebdbfe79de3a238b36c2954ae9c6abf0"
destination = Path("/opt/nanojev/checkpoint")
source = Path("/opt/model-source")
snapshot_download("C-Tianyu/NanoJev", revision=REVISION, local_dir=source,
                  allow_patterns=[f"{VARIANT}/{name}" for name in
                                  ("best.safetensors", "config.json", "tokenizer/*", "backbone_config/*")])
(source / VARIANT).rename(destination)
with (destination / "best.safetensors").open("rb") as source:
    checksum = hashlib.file_digest(source, "sha256").hexdigest()
if checksum != WEIGHTS_SHA256:
    raise RuntimeError("NanoJev checkpoint checksum mismatch")
(destination / "provenance.json").write_text(json.dumps({
    "repository": "C-Tianyu/NanoJev", "revision": REVISION,
    "variant": VARIANT, "weights_sha256": checksum,
}, indent=2) + "\n")
