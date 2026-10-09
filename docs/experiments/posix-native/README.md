# Native Zephyr POSIX boundary experiment

This fixture exercises direct Zephyr/Picolibc file, clock, pthread, signal and
memory-protection calls for the paper's semantic assessment. It runs a real
Zephyr application on an outer QEMU `virt` machine with Cortex-A53, EL2, one CPU
and 512 MiB RAM. The QEMU-on-Zephyr runtime module is disabled.

**Four explicit declarations are required for this baseline.** `main.c` declares
`pread`, `pwrite`, `mprotect` and `pause` using their provider signatures. The
active Zephyr public headers omit these declarations even with
`_POSIX_C_SOURCE=200809L` and `_XOPEN_SOURCE=700`. The runtime results measure the
linked providers with those declarations supplied.

## Reproduce

Run from the project root after [host setup](../../guidelines.md#installation):

```sh
mkdir -p build/posix-native/tmp
export TMPDIR="$PWD/build/posix-native/tmp"
make prepare
make guest-disk QEMU_MODE=user
. .tools/env.sh
python scripts/posix_probe.py
```

The [runner](../../../scripts/posix_probe.py) uses the project's SDK selection,
builds into `build/posix-native`, attaches `build/user-disk.img` read-only,
collects JSON observations and sends
Ctrl-a x after `POSIX_PROBE_DONE`. A 45-second timeout bounds the experiment and
cleanup stops only the QEMU process created by the runner. It never writes the
attached image. `--disk PATH` selects another compatible Ext2 image containing
`/hello`; `--timeout SECONDS` changes the timeout.

To invoke the equivalent build directly after sourcing `.tools/env.sh`:

```sh
python -m west build -b qemu_cortex_a53 \
  -d build/posix-native docs/experiments/posix-native -- \
  -DPython3_EXECUTABLE="$VIRTUAL_ENV/bin/python" \
  -DZEPHYR_BASE="$ZEPHYR_BASE" \
  -DZEPHYR_SDK_INSTALL_DIR="$ZEPHYR_SDK_INSTALL_DIR" \
  -DZEPHYR_MODULES="$PWD" \
  -DBUILD_VERSION=posix-native-probe
python scripts/posix_probe.py
```

The fixture's [prj.conf](prj.conf) selects Picolibc, POSIX threads, files,
signals, memory protection, Ext2 and VirtIO block storage. Its
[app.overlay](app.overlay) binds the `GUESTFILES` disk at VirtIO MMIO bus 4 and
selects SMC for PSCI, matching the EL2 boot. The resolved configuration and
build outputs remain under `build/posix-native`.

Generated files:

| File under `build/posix-native` | Content |
| --- | --- |
| `records.json` | Actual observed return values, errno values and metadata. |
| `metadata.json` | Source/configuration/disk/ELF hashes, portable command arguments and tool versions. |
| `runtime.log` | UART output from this execution. |
| `build.log` | Compiler output. |
| `zephyr/.config` | Resolved Kconfig settings. |

## Recorded observations

[observed.json](observed.json) and [provenance.json](provenance.json) preserve the
9 October 2026 execution of this fixture with Zephyr revision
`ba25413e5b6b2661a71db24888f6b89274f1480d`, SDK 1.0.1/GCC 14.3.0 and outer QEMU
10.0.2. Source files, configuration, image and ELF are identified by SHA-256.
Recreated images can have different inode timestamps and hashes; rebuilt ELFs
can also differ with their build paths and toolchains.

| Boundary | Observed behavior |
| --- | --- |
| `open`, `read`, `lseek`, `close` | Opened `/images/hello`; read ELF magic `7f454c46`; repeated the read after seeking. |
| `stat`, `fstat` | Both returned regular-file type and size 2496, with zero permission bits. Read-only `debugfs` inspection reports inode mode 0755. |
| `pread`, `pwrite` | Both returned -1 with ENOTSUP (134) on the tested regular `O_RDONLY` descriptor. |
| `clock_gettime`, `clock_getres`, `nanosleep` | Calls completed; realtime resolution was 10000000 ns with `CONFIG_SYS_CLOCK_TICKS_PER_SEC=100`. |
| Pthread execution and synchronization | Explicit thread attributes, creation/join, mutex operations and condition notification completed. The joined worker returned `0x51504f53`. |
| Condition timeout | `pthread_cond_timedwait` returned ETIMEDOUT (116) directly; errno remained zero. |
| Signal-set manipulation | `sigemptyset`, `sigaddset` and `sigismember` completed with the expected membership observation. |
| `mprotect`, `sigaction`, `kill`, `pause`, `sigwait` | Returned -1 with ENOSYS (88). `kill` used signal zero inside Zephyr. |
| `pthread_atfork` | Returned ENOSYS (88) directly; errno remained zero. |
| `getpid` | Returned 42. |

The target prints its errno constants. Each observed call starts with errno
cleared for recording; a zero errno after success is not a general POSIX
guarantee. The JSON includes repeated calls and metadata, so its record count
is not an API-coverage denominator. Return values and observations are retained
when behavior changes; the runner does not require ENOSYS or ENOTSUP to remain
the result of future implementations.

## Header and provider scope

Compiler include tracing selected
`include/zephyr/posix/unistd.h` and
`include/zephyr/posix/sys/mman.h` from the prepared Zephyr tree. The declarations
are absent from those files; their existing feature-test conditions do not
expose these four interfaces. A direct compile with `_XOPEN_SOURCE=700` in
addition to the build's `_POSIX_C_SOURCE=200809L` confirmed all four names were
undeclared without the fixture's explicit declarations.

The resolved configuration enables `POSIX_DEVICE_IO`, `POSIX_SIGNALS` and
`POSIX_MEMORY_PROTECTION`; their provider bodies are linked. The byte hashes of
the inspected POSIX source and header files match the pinned upstream revision.
The prepared tree also includes the project's read-only Ext2 synchronization
patch. Picolibc, the selected Zephyr provider, Kconfig, filesystem backend and
architecture are part of the scope of each result.

The experiment provides finite evidence for the executed inputs. It covers
condition notification and timeout, descriptor positioning, file metadata and
selected semantic failures. It does not measure standards-wide conformance,
real-time latency bounds, exhaustive thread semantics, writable filesystems or
physical-board behavior.
