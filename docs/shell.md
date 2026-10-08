# Starting guests from the Zephyr shell

`make run QEMU_SHELL=1` boots the Zephyr shell on the outer QEMU `virt` board. The shell
registers `qemu-system-aarch64` and the standard Zephyr `fs` commands. An Ext2
disk is attached through VirtIO Block and mounted read-only at `/images`.

```sh
make run QEMU_SHELL=1
```

Enter these commands at the `zephyr>` prompt:

```text
fs ls /images
qemu-system-aarch64 -M zephyr-virt -accel zephyr -cpu cortex-a53 -kernel /images/Image -initrd /images/initramfs.cpio.gz -append "console=ttyAMA0 rdinit=/bin/sh nokaslr panic=-1"
```

`QEMU_SHELL=0` is the default. It automatically invokes the configured QEMU
command after mounting the filesystem. `QEMU_SHELL=1` waits for an explicit
command and uses a separate build directory. Both modes return to the shell
after guest termination.

The guest receives the serial terminal while running. Linux `poweroff -f`
returns to the Zephyr shell. Ctrl-] requests guest termination and also returns
to the shell. Ctrl-a followed by x exits the outer QEMU process.

Each Zephyr boot supports one initialized QEMU guest. Argument validation and
missing-file errors allow another command immediately. After guest execution
or a loader failure, use `kernel reboot cold` before starting another guest.
The shell and filesystem remain available after the guest exits. Process-style
QEMU loader failures return an error status to the shell worker.

## Startup options

| Option | Meaning |
| --- | --- |
| `-M`, `-machine` | `zephyr-virt`; accepts `type=` and `accel=` properties |
| `-accel` | The accelerator compiled into the current Zephyr image |
| `-cpu` | Native: the configured host CPU; TCG: Cortex-A53, A57 or A72 |
| `-kernel PATH` | Linux AArch64 Image or an AArch64 ELF firmware image |
| `-initrd PATH` | Optional initramfs used with `-kernel` |
| `-append STRING` | Kernel command line; quote values containing spaces |
| `-bios PATH` | Raw firmware loaded and entered at guest address `0x40000000` |
| `-m 256M` | The machine's fixed 256 MiB guest RAM |
| `-smp 1` | The machine's single vCPU |
| `-nographic` | Use the existing serial console |
| `-help` | Show command help; `-M help`, `-accel help`, `-cpu help` list selections |
| `-status` | Query guest state, completed execution counters and host scheduling counters |

Use absolute Zephyr filesystem paths. `-kernel` and `-bios` are mutually
exclusive. `-initrd` and `-append` apply to `-kernel`. Unsupported options,
conflicting accelerators and missing image files are reported before QEMU
initialization. Firmware runs at guest EL1 on the `zephyr-virt` memory map:
RAM starts at `0x40000000`, PL011 is at `0x09000000`, and the interrupt
controller is a software GICv3. ELF segments and the entry point are handled
by QEMU's ELF loader. Raw firmware must be linked for its load address.

Execution and host scheduling counters are printed only by `-status`.
After the guest exits or Ctrl-] returns to Zephyr, the command reports
the completed execution's accelerator counters.

Build the TCG host from the development machine with:

```sh
make run QEMU_SHELL=1 QEMU_ARGS='-accel tcg -cpu cortex-a53'
```

Then select a guest CPU at the Zephyr prompt:

```text
qemu-system-aarch64 -M zephyr-virt -accel tcg -cpu cortex-a72 -kernel /images/Image -initrd /images/initramfs.cpio.gz
```

Accelerators remain build selections because native EL2 and TCG use distinct
Zephyr configurations. `QEMU_ARGS` supplies the image's machine, accelerator
and CPU defaults; the shell command selects image paths and runtime options.

## Preparing image files

The default disk is `build/guest-disk.img`. `make run` and `make check` create
it from the verified downloads, exposing `Image` and `initramfs.cpio.gz`.
The host uses e2fsprogs `mke2fs`; `make host-deps` installs that dependency on
Linux and macOS. Source changes regenerate the disk through a temporary file
and atomic rename.

To populate a disk from your own directory:

```sh
make guest-disk GUEST_FILES=/absolute/path/to/images
make run QEMU_SHELL=1 GUEST_DISK="$PWD/build/guest-disk.img"
```

The directory may contain subdirectories with regular image files. Names and
file contents are preserved. Select them by their paths under `/images`:

```text
qemu-system-aarch64 -kernel /images/firmware.elf
```

Use an existing Ext2 disk with `make run GUEST_DISK=/absolute/path/to/disk.img`.
The filesystem begins at sector zero and uses 4096-byte blocks, 128-byte
inodes and the `filetype` feature. An equivalent host creation command is:

```sh
mke2fs -t ext2 -b 4096 -I 128 -O none,filetype -d /path/to/images disk.img 16384
```

The example creates a 64 MiB filesystem. `make guest-disk` selects capacity
from the input size. The Zephyr mount disables automatic formatting, and the
outer QEMU drive is read-only. The loader also accepts paths from other
filesystems mounted by the Zephyr application. With
`CONFIG_QEMU_EMBEDDED_PAYLOAD=y`, the built-in Linux files are additionally
available under `/guest`.

## Validation

`make check QEMU_SHELL=1` enters the Zephyr shell, lists `/images`, checks a missing-file
error, launches Linux with a quoted command line, and validates the guest.
It then checks shell and filesystem access after guest poweroff.
`make check` validates automatic Linux startup and the same guest behavior.

`make check-firmware` builds AArch64 ELF and raw firmware using the selected
SDK. The firmware verifies EL1, initialized data in a separate segment, and
BSS, then writes to PL011 and powers off through PSCI. The tests also cover
Zephyr reboot followed by another launch, Ctrl-] termination, and shell
recovery after an incompatible ELF architecture. TCG firmware tests select
Cortex-A72 at runtime from the Cortex-A53 build profile.
