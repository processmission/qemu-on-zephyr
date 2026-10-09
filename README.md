# QEMU on Zephyr

[![Build and boot](https://github.com/processmission/qemu-on-zephyr/actions/workflows/build.yml/badge.svg)](https://github.com/processmission/qemu-on-zephyr/actions/workflows/build.yml)

Run ARM64 Linux kernels, firmware and static Linux programs inside Zephyr.
System mode uses the `zephyr` EL2 accelerator or QEMU TCG. User mode uses TCG
and translates Linux syscalls to Zephyr operations.

This is an experimental AArch64 integration, validated on an outer QEMU
`virt` platform. Physical boards and production isolation remain unvalidated.

[中文](README.zh-CN.md) · [Usage guidelines](docs/guidelines.md) · [中文指南](docs/guidelines.zh-CN.md) · [Technical paper](docs/paper.md)

## Quick start

Use Linux x86_64/AArch64 or macOS Apple Silicon with Python 3.12+.
macOS requires Xcode Command Line Tools and Homebrew.

```sh
git clone https://github.com/processmission/qemu-on-zephyr.git
cd qemu-on-zephyr
bash scripts/install-host-deps.sh
make setup
make run
```

Setup prepares the pinned sources, Python tools, Zephyr SDK 1.0.1 and guest
assets. `make run` automatically boots Linux. Use `QEMU_SHELL=1` to wait at
the Zephyr shell and enter a launch command manually.

## Linux user mode

On the development host:

```sh
make run QEMU_MODE=user QEMU_SHELL=1
```

The default user disk includes an SDK-built static Linux ELF. At `zephyr>`:

```text
qemu-aarch64 /images/hello arg1
```

For automatic execution:

```sh
make run QEMU_MODE=user QEMU_ARGS='/images/hello arg1'
```

## External programs

Use `GUEST_FILES` to place your compiled programs under `/images`.
Files are provided through a read-only disk snapshot; updated files take
effect after disk regeneration and an outer QEMU restart. The
[external-program workflow](docs/guidelines.md#external-programs-and-updates)
covers both managed and explicitly named disks.

## Run and exit

- **Ctrl-]** stops the active guest/program and returns to Zephyr.
- **Ctrl-a, then x** exits outer QEMU.
- A subsequent system VM requires `kernel reboot cold`; user programs can
  run successively in the same Zephyr boot.

Use `make check` for system acceptance and `make check-user` for user-mode
acceptance. The [guidelines](docs/guidelines.md) contain supported options,
firmware examples, compatibility limits and troubleshooting.

## Development

[CONTRIBUTING.md](CONTRIBUTING.md) describes source and patch maintenance.
The [technical paper](docs/paper.md) examines QEMU porting and Zephyr POSIX
compatibility; [validation.md](docs/validation.md) records execution evidence.
Components retain their original licenses; see [LICENSE.md](LICENSE.md).
