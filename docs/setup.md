# Reproducing the workspace

## Host requirements

Use Linux x86_64/AArch64 or macOS on Apple Silicon, with Python **3.12+** as
required by the pinned Zephyr source. The reference Linux setup is Ubuntu 24.04. Run
`bash scripts/install-host-deps.sh` on a fresh machine, or `make host-deps` if
Make is installed. This is the only step that may need sudo.

The script uses apt on Ubuntu/Debian, pacman on Arch, dnf on Fedora, and
Homebrew on macOS. Older distros with Python below 3.12 need a newer Python
or OS. On macOS, install Xcode Command Line Tools (`xcode-select --install`)
and [Homebrew](https://brew.sh), then follow Homebrew's instructions to add
its executables to `PATH` before running the script. Installed Homebrew
packages are retained; missing packages are installed.

The pinned [Zephyr SDK 1.0.1](https://github.com/zephyrproject-rtos/sdk-ng/releases/tag/v1.0.1)
provides macOS binaries for Apple Silicon. Intel Macs and Windows require a
supported Linux environment for this workspace. POSIX terminals and file
locks are used on both supported operating systems.

Host packages provide Git, Make, Python/venv, C/C++ build tools, patch, tar, xz,
wget, file, which, CA certificates, pkg-config and GLib development headers.
Homebrew also provides dtc, gperf and libmagic on macOS.
CMake, Ninja and west come from the project venv. The cross-compiler and QEMU
come from the SDK; a system QEMU package is not required.

Allow several GiB of disk space and network access to the Git hosts, PyPI,
Zephyr SDK releases and guest asset URLs. See `guest-assets.md` for guest hashes.

## Managed setup

```sh
git clone https://github.com/processmission/qemu-on-zephyr.git
cd qemu-on-zephyr
bash scripts/install-host-deps.sh
make setup
make doctor
make check
make run
```

Setup creates `.venv`, initializes a local west workspace, updates the four
upstream repositories, installs Python requirements through
`west packages -m qemu_on_zephyr pip --install`, prepares sources, installs or
selects SDK 1.0.1, and verifies the guest assets.

The SDK installation uses the checked-out Zephyr's command:

```sh
west sdk install --version 1.0.1 \
    --install-dir "$PWD/.tools/zephyr-sdk-1.0.1" \
    --gnu-toolchains aarch64-zephyr-elf
```

Only AArch64 GNU is requested. SDK host tools supply outer QEMU on both Linux
and macOS. Linux QEMU is under `hosttools/sysroots/<arch>-pokysdk-linux/usr/bin/`;
macOS QEMU is under `hosttools/usr/bin/`. Zephyr's installer
can reuse a registered SDK even when an install directory is provided; setup
stores the resulting location in `.tools/environment.json` and writes
`.tools/env.sh` for interactive shells. The official SDK installer also
registers the SDK in the user's CMake package registry.

This uses the official [west initialization/update workflow](https://docs.zephyrproject.org/latest/develop/west/built-in.html)
and [SDK manager](https://docs.zephyrproject.org/latest/develop/west/zephyr-cmds.html).
Options follow the pinned source, which uses `--gnu-toolchains` rather than
the older `--toolchains` spelling.

## Native west workflow

After setup, Make is optional for compiling:

```sh
. .tools/env.sh
west list
west update
make prepare
west build -b qemu_cortex_a53 -d build/linux apps/qemu_linux
make run
```

Use `make run` for the profile's outer QEMU memory and console configuration;
the board's generic `west build -t run` target does not necessarily use them.

For explicit initialization, run from the repository root:

```sh
qoz_workspace="$PWD"
(cd / && env -u ZEPHYR_BASE "$qoz_workspace/.venv/bin/west" init -l "$qoz_workspace/west")
.venv/bin/west config --local manifest.path .
.venv/bin/west config --local manifest.file west/west.yml
.venv/bin/west config --local update.narrow true
.venv/bin/west update
git submodule init
```

The first command targets the **`west/` subdirectory** deliberately. Setup
runs initialization from the filesystem root with an absolute manifest path
and a cleared `ZEPHYR_BASE`, so an existing ancestor workspace is preserved.
Configuration then makes the repository root the manifest repository and module.
No manifest import pulls in Zephyr's full dependency graph.
Narrow, depth-one fetches avoid downloading unrelated upstream branches and tags.

`west/west.yml` and Git submodule links describe the same four revisions;
`make test-tools` checks their agreement. West uses initialized Git submodules,
and Git can inspect repositories initially fetched by west.

## Configuration and maintenance

| Setting | Behavior |
| --- | --- |
| `ZEPHYR_SDK_INSTALL_DIR=/path make setup` | Select an SDK explicitly; reject wrong versions or missing compilers |
| `QEMU_SYSTEM_AARCH64=/path make run` | Override SDK QEMU |
| `make run QEMU_ARGS='-M zephyr-virt -accel tcg -cpu cortex-a72'` | Select the inner machine, accelerator and CPU model |
| `make run QEMU_ARGS='-M help'` | List supported inner machines; `-accel help` and `-cpu help` list the other selections |
| `ACCEL=tcg CPU=cortex-a72 make run` | Provide defaults for options omitted from `QEMU_ARGS` |
| `JOBS=16 make build` | Set parallel build jobs |
| `BOOTSTRAP_PYTHON=python3.12 make setup` | Choose the Python used to create the venv |
| `make update` | Synchronize sources to the manifest |
| `make clean` | Remove builds and prepared sources; retain installed tools and downloads |

Rerun setup after changing Python requirements or an interrupted installation.
Normal builds do not reinstall dependencies. Python dependencies beyond the
explicitly pinned build tools follow the pinned Zephyr requirements; this is
not a fully locked transitive Python environment.

For offline builds, complete setup once and retain `.venv/`, `.tools/`,
`upstream/` and `downloads/`. Do not relocate a venv or SDK without reinstalling
it. `make clean` followed by `make build` can reconstruct generated sources
offline from the initialized upstream repositories.

## Troubleshooting

- **Old Python or missing venv:** run the host dependency installer. Python 3.12
  is the minimum; an older distro's `python3` may not qualify. On macOS,
  ensure Homebrew's `bin` directory precedes `/usr/bin` in `PATH`.
- **Missing SDK:** run `make doctor`. Fix or unset a wrong explicit SDK path;
  the wrapper does not silently ignore it.
- **SDK download/API failure:** restore connectivity and rerun setup. SDK
  downloads use GitHub releases and can be affected by API rate limits. The
  wrapper forwards uppercase HTTP(S) proxy settings to the lowercase variables
  used by the SDK downloader; explicit lowercase settings take precedence.
- **Incomplete SDK directory:** inspect it before removing it and retrying;
  avoid deleting a shared SDK used by other projects.
- **Modified upstream checkout:** preserve edits before updating. Wrapper updates
  reject tracked upstream modifications; store port changes in `src/` or `patches/`.
- **Stale CMake state:** `make clean && make build` removes generated state while
  retaining installed dependencies.
- **Guest reports no shell job control:** expected for the minimal `/bin/sh`
  initramfs. Serial commands still work.

Linux guest acceptance writes `build/linux-validation.log`; architecture tests
write `build/twister/`. CI runs the same setup, environment tests, guest
acceptance and component tests on Ubuntu 24.04 and macOS 14 Apple Silicon.

Backend selection and the six supported profiles are described in
[backends.md](backends.md). Patches are maintained as ordered
[atomic series](patches.md), not one rollup per upstream repository.
