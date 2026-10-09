# QEMU on Zephyr: Usage Guidelines

[中文](guidelines.zh-CN.md) · [Project overview](../README.md) · [Technical paper](paper.md)

Run the host commands below from the repository root. Commands shown for the
`zephyr>` prompt run inside Zephyr; commands for `~ #` run inside a Linux guest.

- [Installation](#installation)
- [Choose a mode](#choose-a-mode)
- [System guests](#system-guests)
- [Linux user programs](#linux-user-programs)
- [External programs and updates](#external-programs-and-updates)
- [Disk images](#disk-images)
- [Exit and restart](#exit-and-restart)
- [Make parameters](#make-parameters)
- [Validation](#validation)
- [Workspace maintenance](#workspace-maintenance)
- [Troubleshooting](#troubleshooting)

## Installation

Use Linux x86_64/AArch64 or macOS Apple Silicon with Python 3.12 or newer.
Ubuntu 24.04 is the reference Linux environment. On macOS, install Xcode
Command Line Tools and Homebrew, and make Homebrew available in `PATH`.

```sh
git clone https://github.com/processmission/qemu-on-zephyr.git
cd qemu-on-zephyr
bash scripts/install-host-deps.sh
make setup
make doctor
```

The dependency script supports apt on Ubuntu/Debian, pacman on Arch, dnf on
Fedora, and Homebrew on macOS. It installs e2fsprogs for image creation.
`make setup` prepares `.venv`, a local west workspace, pinned QEMU/Zephyr/dtc/
zlib sources, Zephyr SDK 1.0.1 and verified Linux guest downloads. The SDK
provides the AArch64 GNU compiler and the outer QEMU executable.

An existing SDK can be selected explicitly:

```sh
ZEPHYR_SDK_INSTALL_DIR=/absolute/path/to/zephyr-sdk-1.0.1 make setup
```

Make selects the managed tools automatically. Detailed installation, proxy,
offline-build and native west instructions are in [setup.md](setup.md).

## Choose a mode

| Setting | Result |
| --- | --- |
| `QEMU_MODE=system` | Default: run a Linux kernel or firmware through `qemu-system-aarch64` |
| `QEMU_MODE=user` | Run a static Linux AArch64 program through `qemu-aarch64` |
| `QEMU_SHELL=0` | Default: execute the configured command automatically after boot and mounting |
| `QEMU_SHELL=1` | Wait at the Zephyr shell for a manual command |

`QEMU_SHELL` controls startup policy. The firmware includes the Zephyr shell
in both settings, and guest termination returns to that shell. System and
user modes are separate firmware builds. Mode, startup policy, accelerator
and CPU selections are reflected in build-directory names.

The outer QEMU always uses the `virt` board and TCG. In system mode, the inner
machine is `zephyr-virt`, with one Cortex-A53/A57/A72 vCPU and 256 MiB RAM.
Its `zephyr` accelerator uses the Zephyr EL2 executor. Its `tcg` accelerator
uses software translation on an EL1 Zephyr host. User mode uses TCG at EL1.
The native backend has been validated on the emulated Arm platform; physical
board operation remains unvalidated.

## System guests

### Automatic Linux startup

```sh
make run
```

This loads `/images/Image` and `/images/initramfs.cpio.gz`. The default inner
configuration is `zephyr-virt`, the `zephyr` accelerator and Cortex-A53.
At the Linux `~ #` prompt, ordinary guest commands are available:

```text
uname -m
poweroff -f
```

Select the inner machine, accelerator and CPU through `QEMU_ARGS`:

```sh
make run QEMU_ARGS='-M zephyr-virt -accel zephyr -cpu cortex-a57'
make run QEMU_ARGS='-M zephyr-virt,accel=tcg -cpu cortex-a72'
```

The accelerator is compiled into the firmware. A shell `-accel` must match
that selection. Native CPU selection must match the configured host model;
a TCG system build can select A53, A57 or A72 at the shell.

### Manual startup

On the development host:

```sh
make run QEMU_SHELL=1 QEMU_ARGS='-accel tcg -cpu cortex-a53'
```

At `zephyr>`:

```text
fs ls /images
qemu-system-aarch64 -help
qemu-system-aarch64 -M zephyr-virt -accel tcg -cpu cortex-a72 -kernel /images/Image -initrd /images/initramfs.cpio.gz -append "console=ttyAMA0 rdinit=/bin/sh nokaslr panic=-1"
```

| Shell option | Supported behavior |
| --- | --- |
| `-M`, `-machine` | `zephyr-virt`, with `type=` and `accel=` properties |
| `-accel`, `-cpu` | Accelerator and CPU subject to the build constraints above |
| `-kernel PATH` | Linux AArch64 Image or AArch64 ELF firmware |
| `-initrd PATH`, `-append STRING` | Optional initramfs and kernel command line |
| `-bios PATH` | Raw EL1 firmware loaded and entered at `0x40000000` |
| `-m 256M`, `-smp 1`, `-nographic` | Fixed memory, one vCPU and the serial console |
| `-help` | Usage; `-M help`, `-accel help` and `-cpu help` list selections |
| `-status` | State, completed execution counters and host scheduling counters |

Use absolute Zephyr paths and quote command-line values containing spaces.
Choose exactly one of `-kernel` and `-bios`; `-initrd` and `-append` require
`-kernel`. System-mode `QEMU_ARGS` at the Make interface accepts machine,
accelerator and CPU selection; image paths are supplied at the Zephyr shell.

### Firmware

Place your firmware in a selected [image directory](#disk-images). With
`firmware.elf` or `firmware.bin` present, use the corresponding command:

```text
qemu-system-aarch64 -kernel /images/firmware.elf
qemu-system-aarch64 -bios /images/firmware.bin
```

Each system launch requires a fresh Zephyr boot. Guest RAM starts at
`0x40000000`; PL011 is at `0x09000000`, and the machine has a software GICv3.
Raw firmware must be linked for its load address. QEMU's loader handles ELF
segments and the entry point. See [shell.md](shell.md) for loader details.

## Linux user programs

### Run the supplied example

```sh
make run QEMU_MODE=user QEMU_SHELL=1
```

The runner uses the installed SDK to compile
[the hello sample](../samples/linux-user/hello/). The resulting
`build/user-programs/hello` is a static ELF using the Linux AArch64 syscall
ABI. The default user disk exposes it at `/images/hello`.

At `zephyr>`:

```text
fs ls /images
qemu-aarch64 /images/hello arg1
qemu-aarch64 -E MESSAGE=hello /images/hello "two words"
qemu-aarch64 -strace /images/hello
```

`hello` prints its arguments and the optional `MESSAGE` value. The command
supports `-help`, the compiled `-cpu` model, `-strace`, and up to eight
`-E NAME=VALUE` settings. Arguments after the program path belong to the
program. The default working directory is `/images`.

### Automatic execution

```sh
make run QEMU_MODE=user QEMU_ARGS='-E MESSAGE=hello /images/hello "two words"'
```

Automatic user startup requires a program path in `QEMU_ARGS`. The program
executes after filesystem mounting and returns to the Zephyr shell on exit.

### Compatibility

The current profile executes one static, single-threaded Linux AArch64
program at a time. It provides a 64 MiB process address space and a 1 MiB
initial stack. Compatibility depends on the syscalls used by the program.
Supported interfaces cover files, console I/O, private memory mappings,
clocks, entropy and selected process metadata. Unsupported syscalls return
Linux `ENOSYS`.

The profile excludes `fork`, `clone`, `execve`, sockets, installed signal
handlers and a dynamic library runtime. Program exit closes its descriptors;
another program can start in the same Zephyr boot. See
[user-mode.md](user-mode.md) for the syscall interface and memory limits.

## External programs and updates

### Supply a program directory

Place your static Linux AArch64 ELF at `guest-programs/myapp` on the
development host. From the repository root:

```sh
make run QEMU_MODE=user \
  GUEST_FILES="$PWD/guest-programs" \
  QEMU_ARGS='/images/myapp arg1'
```

`GUEST_FILES` supplies the disk's contents. A host subdirectory is preserved:
`guest-programs/bin/myapp` becomes `/images/bin/myapp`. To choose a program
manually, start with `QEMU_SHELL=1`, inspect `fs ls /images`, and enter
`qemu-aarch64 /images/myapp arg1` at the Zephyr prompt.

Managed Ext2 disks support up to 112 MiB of file contents within one 128 MiB
block group. Larger source directories are rejected before disk creation.

### Activate an updated binary

1. Exit the outer QEMU with **Ctrl-a, then x**.
2. Rebuild or replace `guest-programs/myapp` on the development host.
3. Run the same Make command with `GUEST_FILES` again.

Make fingerprints source names and file contents. A change regenerates the
selected mode's disk through a temporary image and an atomic rename. The
new outer QEMU process attaches that disk, making the updated program
available at the same Zephyr path.

The attached Ext2 filesystem is a read-only snapshot. Running-session file
upload and live directory sharing are outside the current interface.
`kernel reboot cold` restarts Zephyr inside the existing outer QEMU process;
the process keeps its already-open disk. Activation of a newly generated
disk therefore requires an outer QEMU restart.

### Update an explicitly named disk

An explicit `GUEST_DISK` is reused on `make run`. Regenerate it after changing
the source files, then start a new outer QEMU process:

```sh
make guest-disk QEMU_MODE=user \
  GUEST_FILES="$PWD/guest-programs" GUEST_DISK="$PWD/build/myapp.img"
make run QEMU_MODE=user GUEST_DISK="$PWD/build/myapp.img" \
  QEMU_ARGS='/images/myapp arg1'
```

The same regeneration and restart steps apply to external Linux kernel
images and firmware used by system mode.

## Disk images

| Mode | Default disk | Default contents under `/images` |
| --- | --- | --- |
| `system` | `build/guest-disk.img` | Verified `Image` and `initramfs.cpio.gz` |
| `user` | `build/user-disk.img` | SDK-built `hello` |

`make guest-disk` prepares the selected mode's disk without starting QEMU.
For custom system images:

```sh
make guest-disk GUEST_FILES=/absolute/path/to/images GUEST_DISK="$PWD/build/custom.img"
make run QEMU_SHELL=1 GUEST_DISK="$PWD/build/custom.img"
```

The host directory may contain regular files and subdirectories. Symbolic
links are rejected, and the output disk must be outside the source directory.
An existing disk must contain Ext2 at sector zero, with 4096-byte blocks,
128-byte inodes and the `filetype` feature. The helper selects disk capacity
from the input size, with a minimum of 64 MiB.

Outer QEMU provides the disk through VirtIO Block. Zephyr mounts it read-only
at `/images` with automatic formatting disabled. Writes to that mount return
`EROFS`. Other application-mounted filesystems can also supply input files.
Asset origins and hashes are recorded in [guest-assets.md](guest-assets.md).

## Exit and restart

| Action | Effect |
| --- | --- |
| Ctrl-] during execution | Stop the active guest/program and return to Zephyr |
| `poweroff -f` in the Linux guest | Stop that system guest and return to Zephyr |
| `kernel reboot cold` at `zephyr>` | Restart Zephyr and its QEMU global state |
| Ctrl-a, then x | Exit the outer QEMU process |
| `qemu-system-aarch64 -status` | Query completed execution and scheduling counters |

System mode initializes one VM per Zephyr boot. `QEMU guest exited: 0`
reports successful completion of its execution loop; machine and CPU objects
remain allocated. Before another system launch, execute `kernel reboot cold`,
wait for the prompt, and enter the launch command again. Missing-file and
argument-validation errors allow immediate retry; loader errors after QEMU
initialization require the reboot.

User mode resets its process mappings and CPU state for each launch and
allows successive commands. Ctrl-] returns status 130; other statuses report
the program exit code or 128 plus a terminating signal number. Execution and
host scheduling counters are printed only on an explicit status request.

## Make parameters

| Parameter | Use |
| --- | --- |
| `QEMU_MODE=system\|user` | Select the firmware's execution mode |
| `QEMU_SHELL=0\|1` | Select automatic or manual startup |
| `QEMU_ARGS='…'` | System: `-M`/`-machine`, `-accel`, `-cpu`; user: `-accel tcg`, `-cpu`, `-E`, `-strace`, program and arguments |
| `ACCEL=zephyr\|tcg` | Default system accelerator when omitted from `QEMU_ARGS` |
| `CPU=cortex-a53\|cortex-a57\|cortex-a72` | Default CPU model when `-cpu` is omitted |
| `GUEST_FILES=/absolute/path` | Source directory for disk creation |
| `GUEST_DISK=/absolute/path/disk.img` | Existing disk for launch; output path for `make guest-disk` |
| `JOBS=8` | Build parallelism |
| `ZEPHYR_SDK_INSTALL_DIR=/absolute/path` | Explicit SDK selection |
| `QEMU_SYSTEM_AARCH64=/absolute/path` | Override the outer QEMU executable |

Explicit `QEMU_ARGS` options override the corresponding defaults. User mode
uses TCG. Run `make help`, `make run QEMU_ARGS='-help'`, or
`make run QEMU_MODE=user QEMU_ARGS='-help'` for command help. Backend details
and native CPU compatibility are in [backends.md](backends.md).

## Validation

```sh
make check
make check QEMU_SHELL=1 QEMU_ARGS='-accel tcg -cpu cortex-a53'
make check-firmware
make check-firmware QEMU_ARGS='-accel tcg'
make check-user
make test-tools
```

`make check` exercises Linux console I/O, timer interrupts, guest EL0/MMU
execution, host-thread progress and guest shutdown. `make check-firmware`
loads ELF/raw firmware and exercises loader errors, termination and reboot.
`make check-user` first runs the default `hello` workflow, then verifies Linux
ABI operations, memory faults and consecutive program launches.

Component commands are `make probe`, `make native-probe`, `make test-payload`,
`make test-glib` and `make test-arch`. The checked configurations and evidence
are described in [validation.md](validation.md). Logs and generated programs
are under `build/` and remain outside Git.

## Workspace maintenance

`west/west.yml` pins upstream revisions. `make prepare` exports those sources,
applies the ordered patch series and overlays `src/` into `build/sources/`.
Edit versioned sources and patches. Keep upstream checkouts clean.

`make update` synchronizes upstream sources with the manifest. `make clean`
removes builds while retaining the SDK, environment and downloaded assets.
For offline reconstruction, retain `.venv/`, `.tools/`, `upstream/` and
`downloads/`. SDK and Python changes are handled through `make setup`.

See [CONTRIBUTING.md](../CONTRIBUTING.md) for source changes and
[patches.md](patches.md) for patch maintenance.

## Troubleshooting

| Symptom | Action |
| --- | --- |
| `Cannot open program` | Inspect `fs ls /images`; verify the filename and the selected `GUEST_FILES` or `GUEST_DISK` |
| Default `hello` is missing | Start user mode with the default inputs, then restart outer QEMU to attach `build/user-disk.img` |
| Updated file still runs old code | Recreate an explicitly named disk if used, then exit and restart outer QEMU |
| `QEMU already initialized` | Reboot Zephyr before another system guest |
| Accelerator or CPU mismatch | Rebuild with matching `QEMU_ARGS`; observe the native CPU constraint |
| ELF load failure | Supply a static Linux AArch64 ELF for user mode or a compatible system image/firmware |
| Linux syscall returns `ENOSYS` | Check the supported user-mode interface and the program's syscall requirements |
| Write returns `EROFS` | `/images` is read-only; update its files on the development host |
| Missing compiler, QEMU or `mke2fs` | Run `make doctor` and complete host dependencies/setup |

The integration is experimental. Physical-board behavior, arbitrary Linux
application compatibility and production isolation are outside the current
validation record.
