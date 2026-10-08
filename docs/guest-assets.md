# Guest test assets

`make assets` downloads two pinned ARM64 assets and verifies SHA256 before
using them. Existing files with a wrong hash cause an error. Downloads are
stored under `downloads/`, which is ignored by Git and retained by `make clean`.
For offline use, place already verified files there using the names below.

| File | SHA256 |
| --- | --- |
| `tuxrun-arm64-Image` | `b74743c5e89e1cea0f73368d24ae0ae85c5204ff84be3b5e9610417417d2f235` |
| `generic-arm64-rootfs.cpio.gz` | `7c0b16d1853772f6f4c3ca63e789b3b9ff4936efac9c8a01fb0c98c05c7a7648` |

The Linux 6.4.16 Image comes from
<https://storage.tuxboot.com/buildroot/20241119/arm64/Image>, matching the asset
in the pinned QEMU `tests/functional/aarch64/test_tuxrun.py`.

The generic BusyBox initramfs comes from
<https://raw.githubusercontent.com/groeck/linux-build-test/86b2be1384d41c8c388e63078a847f1e1c4cb1de/rootfs/arm64/rootfs.cpio.gz>,
matching the asset in QEMU `tests/functional/aarch64/test_raspi4.py`. Only the
generic initramfs is reused; this profile does not boot a Raspberry Pi kernel.

The checked combination boots Cortex-A53 with `rdinit=/bin/sh`. System-mode `make run`
places the verified assets in an Ext2 disk and mounts it at `/images` in
Zephyr. The shell command reads the kernel and initramfs from that filesystem.
`GUEST_FILES` and `GUEST_DISK` select custom contents or an existing disk; see
[shell usage](shell.md).

For user mode, `make run QEMU_MODE=user QEMU_SHELL=1` compiles
`samples/linux-user/hello/` with the installed SDK and creates
`build/user-disk.img`, exposing the static Linux AArch64 executable as
`/images/hello`. `make guest-disk QEMU_MODE=user` prepares the same disk.
The binary and disk are generated under `build/`; the sample source is
versioned. Custom `GUEST_FILES` or `GUEST_DISK` supply the user's program files.
