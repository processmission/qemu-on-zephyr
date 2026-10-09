# SPDX-License-Identifier: Apache-2.0

import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

import pandas as pd


ROOT = Path(__file__).resolve().parent.parent
QEMU = ROOT / "build/sources/qemu"
PINS = {
    "dtc": "b6910bec11614980a21e46fbccc35934b671bd81",
    "zlib": "51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf",
}
LIBRARIES = {
    "GLib API subset": QEMU / "ports/zephyr/glib",
    "libfdt": QEMU / "subprojects/dtc/libfdt",
    "zlib": QEMU / "subprojects/zlib",
}


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def upstream_match(name: str, suffix: str = "") -> dict:
    upstream = ROOT / "upstream" / name
    revision = subprocess.check_output(
        ["git", "-C", str(upstream), "rev-parse", "HEAD"], text=True,
    ).strip()
    if revision != PINS[name]:
        raise ValueError(f"Review the inventory for the changed {name} revision")
    state = subprocess.check_output(
        ["git", "-C", str(upstream), "status", "--porcelain", "--untracked-files=all",
         "--", suffix or "."],
    )
    if state:
        raise ValueError(f"The inspected {name} source differs from its clean revision")
    if (ROOT / "patches" / name).exists() or (ROOT / "src" / name).exists():
        raise ValueError(f"Review the new {name} patch or overlay")
    origin = upstream / suffix
    prepared = QEMU / "subprojects" / name / suffix
    files = sorted(path for path in origin.rglob("*")
                   if path.is_file() and path.suffix in {".c", ".h"})
    prepared_files = {path.relative_to(prepared) for path in prepared.rglob("*")
                      if path.is_file() and path.suffix in {".c", ".h"}}
    if {path.relative_to(origin) for path in files} != prepared_files:
        raise ValueError(f"Source file sets differ for {name}")
    hashes = {}
    for path in files:
        relative = path.relative_to(origin)
        if path.read_bytes() != (prepared / relative).read_bytes():
            raise ValueError(f"Prepared source differs: {name}/{relative}")
        hashes[str(relative)] = digest(path)
    return {"revision": revision, "project_patch_count": 0,
            "changed_c_or_h_files": 0, "comparison_root": str(origin.relative_to(ROOT)),
            "matched_files": hashes}


def compile_profile(directory: Path, mode: str) -> dict:
    database = directory / "compile_commands.json"
    config = directory / "zephyr/.config"
    frame = pd.read_json(database)
    files = {name: [] for name in LIBRARIES}
    selected_definitions = {name: set() for name in LIBRARIES}
    for entry in frame.to_dict("records"):
        path = Path(entry["file"])
        if not path.is_absolute():
            path = Path(entry["directory"]) / path
        path = path.resolve()
        arguments = entry.get("arguments")
        if not isinstance(arguments, list):
            arguments = shlex.split(entry["command"])
        definitions = {argument for argument in arguments if argument.startswith("-D")}
        for name, prefix in LIBRARIES.items():
            if path.is_relative_to(prefix) and path.suffix == ".c":
                files[name].append(str(path.relative_to(QEMU)))
                selected_definitions[name].update(definitions)
                if name == "zlib" and "-DZ_SOLO" not in definitions:
                    raise ValueError("The zlib profile no longer selects Z_SOLO")
    required = "-DCONFIG_SOFTMMU" if mode == "system" else "-DCONFIG_USER_ONLY"
    if required not in selected_definitions["GLib API subset"]:
        raise ValueError(f"The supplied {mode} build has a different QEMU mode")
    for flag in ("CONFIG_PICOLIBC=y", "CONFIG_PICOLIBC_USE_TOOLCHAIN=y",
                 f"CONFIG_QEMU_{mode.upper()}=y"):
        subprocess.run(["rg", "-q", "-x", flag, str(config)], check=True)
    files = {name: sorted(set(values)) for name, values in files.items()}
    return {
        "build_directory": str(directory.relative_to(ROOT)),
        "compile_database_sha256": digest(database),
        "config_sha256": digest(config),
        "source_files": files,
        "source_counts": {name: len(values) for name, values in files.items()},
        "zlib_define": "Z_SOLO",
        "c_library": "toolchain Picolibc",
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Record QEMU C-library integration in built firmware profiles")
    parser.add_argument("--system-build", type=Path, default=ROOT / "build/linux")
    parser.add_argument("--user-build", type=Path, default=ROOT / "build/user-shell-tcg-cortex-a53")
    parser.add_argument("--output", type=Path, default=ROOT / "docs/data/c-dependencies.json")
    args = parser.parse_args()
    profiles = {
        "system": compile_profile(args.system_build.resolve(), "system"),
        "user": compile_profile(args.user_build.resolve(), "user"),
    }
    matches = {"libfdt": upstream_match("dtc", "libfdt"),
               "zlib": upstream_match("zlib")}
    source_inputs = [
        "west/west.yml", "zephyr/CMakeLists.txt", "zephyr/user.cmake", "zephyr/tcg.cmake",
        "apps/qemu_linux/prj.conf", "src/qemu/ports/zephyr/glib/glib.c",
        "src/qemu/ports/zephyr/os.c", "src/qemu/ports/zephyr/file.c",
    ]
    result = {
        "scope": "C libraries selected for QEMU inside Zephyr; development-host tools are outside the count",
        "implementation_revision": "2b1161806afe14fe57dec97234f37aac1ea84e5e",
        "profiles": profiles,
        "upstream_source_comparison": matches,
        "glib": {
            "treatment": "project GLib-compatible API subset",
            "upstream_source_patch_count": None,
            "shared_bridge_files": ["src/qemu/ports/zephyr/os.c", "src/qemu/ports/zephyr/file.c"],
            "scope_limit": "one local core C unit plus shared bridges; not a full upstream GLib port",
        },
        "picolibc": {
            "treatment": "toolchain C runtime selected by Zephyr",
            "source_unit_count": None,
            "scope_limit": "SDK library and Zephyr hooks are outside QEMU-side library source counts",
        },
        "qemu_in_tree_components": ["fpu/softfloat.c", "crypto/aes.c", "crypto/sm4.c", "crypto/clmul.c"],
        "qemu_optional_libraries_outside_profile": ["Pixman", "libslirp", "GnuTLS", "nettle", "SDL"],
        "source_sha256": {name: digest(ROOT / name) for name in source_inputs},
        "json_parser": f"pandas {pd.__version__}",
        "interpretation": "source selection and integration treatment; no POSIX maturity percentage",
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({mode: profile["source_counts"] for mode, profile in profiles.items()}, indent=2))
    print("Verified unchanged libfdt and zlib C/header sources at the pinned revisions")


if __name__ == "__main__":
    main()
