# Linux AArch64 processes on Zephyr

The [usage guidelines](guidelines.md#external-programs-and-updates) describe
how to supply external programs and activate updated files. A
[Chinese version](guidelines.zh-CN.md#外部程序与更新) is also available.

Build the user emulation firmware and wait at the Zephyr shell:

```sh
make run QEMU_MODE=user QEMU_SHELL=1
```

The runner builds [the hello sample](../samples/linux-user/hello/) with the
installed SDK. Its static Linux AArch64 executable is saved as
`build/user-programs/hello` and placed on the default `build/user-disk.img`.
The disk mounts at `/images`; the program prints arguments and `MESSAGE`
when that environment variable is supplied:

```text
fs ls /images
qemu-aarch64 -help
qemu-aarch64 /images/hello arg1
qemu-aarch64 -E MESSAGE=hello /images/hello "an argument"
qemu-aarch64 -strace /images/hello
```

`make guest-disk QEMU_MODE=user` prepares the example disk without launching
QEMU. To attach a newly prepared disk, exit an existing outer QEMU session
with Ctrl-a followed by x and run Make again.

For custom static Linux AArch64 executables, use
`make run QEMU_MODE=user QEMU_SHELL=1 GUEST_FILES=/absolute/path/to/programs`.
That directory supplies the disk's files; a file named `my-program` is
loaded with `qemu-aarch64 /images/my-program`. An explicit `GUEST_DISK`
attaches the supplied disk unchanged. Inspect filenames with `fs ls /images`.

The command executes the ELF through QEMU's `CONFIG_USER_ONLY` ARM translator
and AArch64 TCG backend. Linux `svc` instructions dispatch to the Zephyr
system call adapter. QEMU's Linux ELF loader constructs the initial process
arguments, environment, auxiliary vector and ELF mappings.

`QEMU_MODE=system` builds `qemu-system-aarch64` with the selected `zephyr` or
`tcg` accelerator. `QEMU_MODE=user` builds `qemu-aarch64` with TCG. These
configurations use separate build directories. The outer QEMU always uses
the `virt` board. User emulation runs on a Zephyr EL1 host.

For automatic execution, provide the process path and its arguments:

```sh
make run QEMU_MODE=user QEMU_ARGS='-cpu cortex-a53 -E MESSAGE=hello /images/hello example'
```

`QEMU_SHELL=0` is the default. Manual mode accepts `-cpu` matching the CPU
compiled into the firmware, `-strace`, and up to eight `-E NAME=VALUE`
settings. Program arguments preserve shell quoting. All file paths refer to
the Zephyr filesystem. The default working directory is `/images`.

## Process interfaces

The process owns a 64 MiB address space and a 1 MiB initial stack.
`CONFIG_QEMU_USER_MEMORY_MIB` configures the address-space capacity.
ELF mappings must fit that space. The runtime supports one process at a time.
Exit closes its descriptors and restores the Zephyr prompt; another command
can launch another process. Ctrl-] terminates the active process and returns
status 130. The printed status otherwise reports the Linux exit status or
128 plus a terminating signal number.

| Linux interface | Zephyr implementation |
| --- | --- |
| `read`, `write`, `readv`, `writev` | Zephyr file descriptors or the shell UART |
| `openat`, `close`, `lseek`, `fstat`, `newfstatat` | POSIX filesystem operations with Linux flag and structure conversion |
| `getcwd`, `chdir` | Per-process working directory validated through the mounted filesystem |
| `brk`, `mmap`, `munmap`, `mprotect` | Bounded process memory, private file snapshots and QEMU page permissions |
| `clock_gettime`, `clock_getres`, `nanosleep`, `sched_yield` | Zephyr clocks and scheduler |
| `getrandom` and ELF `AT_RANDOM` | Zephyr VirtIO entropy driver, backed by the outer host's `/dev/urandom` |
| Process and identity queries | Per-launch process ID and a virtual root user/group |
| `set_tid_address` | Clear the supplied process address at exit |
| `prlimit64` | Query the implemented memory, stack and descriptor limits |
| `uname` | Zephyr host identity with the emulated `aarch64` machine name |
| `exit`, `exit_group` | Release process resources and return to the shell |

The `/images` disk is mounted read-only. File writes to that mount return
`EROFS`. Programs can access additional filesystems mounted by the application.
Unsupported calls return Linux `ENOSYS`; unsupported flags return an error.
This profile supports statically linked, single-threaded command-line
programs using the interfaces above. It does not provide `fork`, `clone`,
`execve`, sockets, installed signal handlers or a dynamic library runtime.

The TCG backend routes user loads and stores through its memory helpers.
The adapter checks guest ranges and page permissions, handles translated-code
invalidation, and reports invalid guest access as process termination.
Instruction decoding and ELF parsing use the pinned upstream QEMU sources.
Zephyr's POSIX file metadata exposes file type and size without executable
permission bits; the explicit shell command authorizes loading a regular ELF
file. Architecture and mapping validation remain in QEMU's loader.

## Validation

```sh
make check-user
```

Acceptance first executes the documented default `make run` and checks
`/images/hello`, its arguments, environment and syscall tracing. The SDK also
builds an AArch64 Linux ABI regression program. Acceptance loads it from
Ext2 and exercises arguments, environment, data/BSS, file reads, Linux errno,
heap growth, memory mappings, self-modifying code, clocks and entropy. It
also checks invalid addresses, write protection, unmapping, Ctrl-] and
successive program launches.
