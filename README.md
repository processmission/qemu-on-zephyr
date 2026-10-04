# QEMU on Zephyr

[中文说明](README.zh-CN.md)

Run QEMU's ARM machine and device models inside Zephyr, with a native ARM64
virtualization accelerator executing a Linux guest. Zephyr runs at EL2; Linux
runs at EL1/EL0. The development platform is an outer QEMU ARM `virt` machine
using TCG, so an ARM host or KVM is not required.

This repository is a Zephyr module and a reproducible integration workspace.
Upstream repositories stay unmodified. Local source overlays and patches are
versioned here; patched build trees are generated under `build/sources/`.

## Quick start

The supported development environment is Linux. Install Git, Make, Python 3,
CMake **3.28 or newer**, Ninja, GNU patch, tar, a C compiler, and
`qemu-system-aarch64`. Install **Zephyr SDK 1.0.1**, including its
`aarch64-zephyr-elf` toolchain. The tested outer QEMU version is **10.2.2**.

After cloning this repository:

```sh
cd qemu-on-zephyr
make init
python3 -m venv .venv
. .venv/bin/activate
pip install -r upstream/zephyr/scripts/requirements-base.txt
export ZEPHYR_SDK_INSTALL_DIR=/path/to/zephyr-sdk-1.0.1
make run
```

Use a regular clone followed by `make init`. Recursive submodule initialization
is unnecessary: QEMU's unrelated firmware submodules are not used. The four
direct submodules pin QEMU, Zephyr, libfdt/dtc and zlib to exact commits.

`make run` prepares sources, fetches and SHA256-verifies the guest Image and
initramfs, builds `build/linux/zephyr/zephyr.elf`, and boots it in outer QEMU.
At the Linux shell, try:

```sh
uname -a
mount -t proc proc /proc
cat /proc/interrupts
sleep 1
```

Exit the outer QEMU with **Ctrl-a, then x**. `poweroff -f` shuts down the guest;
the Zephyr host deliberately keeps running.

```sh
make build          # Compile only
make check          # End-to-end automated acceptance; stops QEMU afterward
make probe          # Real QOM/PL011 regression
make native-probe   # Native accelerator diagnostic, without starting Linux
make test-payload   # Read-only payload filesystem regression
make test-glib      # Host GLib differential test (requires pkg-config + GLib headers)
make test-arch      # EL1/EL2, FPU, executor tests (pip install -r tests/requirements.txt)
make clean          # Remove generated sources/builds, retain downloaded assets
```

Use `JOBS=16` to change build parallelism, `PYTHON=/path/to/python` to select
Python, and `QEMU_SYSTEM_AARCH64=/path/to/qemu-system-aarch64` to select outer
QEMU. No global `west init`, `west update`, or Python package installation is
performed by Make. Network access is needed for the first submodule and asset
fetch; subsequent builds can run offline.

## Repository layout

| Path | Purpose |
| --- | --- |
| `zephyr/` | Module metadata, Kconfig and explicit QEMU compilation source list |
| `src/qemu/` | New QEMU accelerator, ARM adapter, OS/GLib/filesystem/console adapters |
| `src/zephyr/` | New native executor, public interface and architecture tests |
| `patches/qemu/` | Changes to existing upstream QEMU files |
| `patches/zephyr/` | Changes to existing upstream Zephyr files for EL2/FPU support |
| `upstream/` | Pristine Git submodules, pinned by Git and `dependencies.json` |
| `apps/qemu_linux/` | Linux application and end-to-end acceptance checker |
| `apps/qemu_probe/` | Standalone device-model regression |
| `tests/` | GLib and payload filesystem tests |
| `scripts/project.py` | Preparation, asset verification, build and run orchestration |
| `build/` | Disposable generated sources, ELF files, generated code and test logs |
| `downloads/` | Verified guest assets, excluded from Git |

## Architecture and current scope

```text
Outer QEMU: virt / Cortex-A53 / GICv3 / 512 MiB / TCG
  Zephyr EL2
    QEMU module: QOM + ARM CPU + MemoryRegion + PL011 + software GICv3
      zephyr accelerator -> zhv executor -> Linux EL1/EL0
    Independent Zephyr threads, timers and UART driver
```

The inner QEMU uses its original ARM Linux loader and device implementations.
It has no TCG execution loop or KVM dependency. Guest RAM is shared between
QEMU's MemoryRegion and the executor's Stage-2 mapping. A single model thread
owns QEMU state; host IRQs queue input and kick that thread.

The tested profile is **one VM, one Cortex-A53 vCPU, 256 MiB guest RAM, PL011,
software GICv3, Linux 6.4.16 and an initramfs shell**. Physical ARM boards, SMP,
multiple VMs, storage/network backends, migration, guest EL2/EL3, and VM
restart/hotplug are not implemented or validated by this profile. This is an
experimental integration, not a production isolation boundary.

`make check` verifies serial input, Linux identity, increasing timer IRQ counts,
native EL0/MMU execution, host scheduling while the guest spins, and host
survival after guest poweroff. Its transcript is `build/linux-validation.log`.

See [architecture](docs/architecture.md), [development](CONTRIBUTING.md),
[guest assets](docs/guest-assets.md), and [licenses](LICENSE.md).
The packaged workspace's validation results are recorded in [validation](docs/validation.md).
