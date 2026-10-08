# Machine, accelerator and CPU options

Pass QEMU-style options through `QEMU_ARGS` to `make build`, `make run`,
`make check` or `make native-probe`:

```sh
make run QEMU_ARGS='-M zephyr-virt -accel zephyr -cpu cortex-a53'
make run QEMU_ARGS='-M zephyr-virt,accel=tcg -cpu cortex-a72'
make check QEMU_ARGS='-machine type=zephyr-virt -accel tcg -cpu cortex-a57'
```

Option spellings follow the [QEMU invocation interface](https://www.qemu.org/docs/master/system/invocation.html).

These options select the QEMU machine inside Zephyr. The outer QEMU board is
fixed to `virt` with TCG; the runner configures its EL2 support and CPU model
for the selected guest backend. Machine, accelerator and CPU choices are
written into the Zephyr build configuration. Each backend/CPU profile has
its own build directory, so profiles can be switched with existing builds present.
`make run QEMU_SHELL=1` opens the Zephyr shell; its `qemu-system-aarch64` command starts the
guest with runtime image paths and CPU options. See [shell usage](shell.md).

`make run` automatically starts the configured guest. `QEMU_MODE=user` selects
the separate `qemu-aarch64` firmware, whose Linux system calls use Zephyr
interfaces. See [Linux process emulation](user-mode.md).

| Option | Supported values |
| --- | --- |
| `-M`, `-machine` | `zephyr-virt`, with optional `type=` and `accel=zephyr` or `accel=tcg` properties |
| `-accel` | `zephyr`, `tcg`; the `accel=NAME` spelling is also accepted |
| `-cpu` | `cortex-a53`, `cortex-a57`, `cortex-a72` |

Use `make run QEMU_ARGS='-help'` for option help, or `-M help`, `-accel help`
and `-cpu help` to list available selections. Help exits before source
preparation, SDK selection or building. Invalid options, unsupported models,
repeated options and conflicting accelerator selections fail before building.
Each option occurs once; accelerator fallback lists are unsupported.

`QEMU_ARGS` is parsed as quoted argument text. Make exports it through the
environment. The guest machine provides one VM, one vCPU and 256 MiB RAM.
Its implemented options are listed above.

`ACCEL` and `CPU` provide defaults for options omitted from `QEMU_ARGS`.
For example, `make run ACCEL=tcg QEMU_ARGS='-cpu cortex-a72'` selects TCG/A72.
An explicit `-accel` or `-cpu` takes precedence over the corresponding Make
variable. With no overrides, the profile is `zephyr-virt`, `zephyr`, Cortex-A53.

| `-accel` | Zephyr host | Guest CPU models | Execution |
| --- | --- | --- | --- |
| `zephyr` | EL2, virtualization enabled | Cortex-A53, A57, A72 matching the host | Native ARM guest entry through `zhv` |
| `tcg` | EL1, outer virtualization disabled | Cortex-A53, A57, A72 | Upstream ARM translator, software MMU and AArch64 JIT |

The native accelerator uses the QEMU name `zephyr`; its kernel executor is `zhv`.

```sh
make run QEMU_ARGS='-accel zephyr -cpu cortex-a53'
make run QEMU_ARGS='-accel zephyr -cpu cortex-a57'
make run QEMU_ARGS='-accel zephyr -cpu cortex-a72'

make run QEMU_ARGS='-accel tcg -cpu cortex-a53'
make run QEMU_ARGS='-accel tcg -cpu cortex-a57'
make run QEMU_ARGS='-accel tcg -cpu cortex-a72'
```

Use `make check` with the same options for serial, timer, EL0/MMU and host
scheduling acceptance. The checker verifies EL2 for native execution and EL1
for TCG, the selected machine/CPU/QOM accelerator types, and guest poweroff isolation.
For TCG it also checks distinct RW/RX code aliases and TCG execution counters.
Return-state statistics are samples, not an instruction-counting facility.

Default native/A53 output remains in `build/linux/`. Other combinations use
`build/linux-<accel>-<cpu>/`. Console transcripts are named
`build/linux-<accel>-<cpu>-validation.log`; the default retains
`build/linux-validation.log`.

## Native CPU compatibility

The outer emulator defaults to the selected guest model for native tests.
Before CPU realization completes, the native adapter checks the physical
MIDR implementer, architecture and part against the selected QEMU model.
It does not pretend that a different physical CPU implements that model.
Revision and variant fields may differ; real-board validation is still needed.

The executor accepts the conventional 32/36/40/42/44/48-bit physical address
ranges and programs VTCR accordingly. It still uses a 32-bit IPA space and
2 MiB guest RAM mappings. Wider LPA/LPA2 formats are not enabled by this change.

`HOST_CPU` can override the outer CPU for diagnostics. For example, native
`CPU=cortex-a72 HOST_CPU=cortex-a53` is expected to fail the compatibility
check before guest execution. Physical boards additionally require the
appropriate Zephyr BSP, memory layout, GIC and timer setup; tests here use
outer QEMU, not a physical board.

## TCG integration

TCG uses the original `tcg-accel` class, CPU execution loop, ARM instruction
decoders, software TLB, translated-block cache, softfloat and device dispatch.
`zephyr/tcg.cmake` builds those upstream sources and runs the original
decodetree generator. Meson and the TCG interpreter are not used.

The Zephyr adapter supplies single-owner `AccelOps`: it binds the existing
POSIX worker, runs `cpu_exec()`, handles halted CPUs with a latched semaphore,
and uses an ISR-safe one-millisecond exit request to service device timers and
console input. Guest poweroff stops that periodic TCG timer. The model and
TCG share an owner; queued RCU callbacks are reclaimed only outside read-side
sections, after returning from CPU execution.

The default 16 MiB JIT buffer has separate writable/non-executable and
read-only/executable mappings. QEMU's original AArch64 cache synchronization
publishes generated instructions through those aliases. The optional TCG
guard page is not provided by this host adapter. The TCG profile reserves
256 MiB guest RAM and uses a 96 MiB libc heap within the outer 512 MiB RAM.

This profile does not offer MTTCG, instruction counting, replay, semihosting,
plugins, M-profile CPUs or an inner GDB server. Unsupported facilities are
disabled or rejected explicitly. Cross-ISA targets such as x86 and RISC-V,
multiple vCPUs and runtime accelerator switching are separate extensions.

## Direct west builds

Make chooses `native.conf` or `tcg.conf`, the machine and CPU Kconfig strings and the
corresponding host overlay. To build the TCG/A72 profile directly:

```sh
. .tools/env.sh
make prepare
west build -b qemu_cortex_a53 -d build/linux-tcg-cortex-a72 apps/qemu_linux -- \
    '-DEXTRA_CONF_FILE=tcg.conf;shell.conf' -DDTC_OVERLAY_FILE=tcg.overlay \
    '-DCONFIG_QEMU_MACHINE_MODEL="zephyr-virt"' \
    '-DCONFIG_QEMU_CPU_MODEL="cortex-a72"'
make run QEMU_ARGS='-M zephyr-virt -accel tcg -cpu cortex-a72'
```

For the standalone native diagnostic, use
`make native-probe QEMU_ARGS='-accel zephyr -cpu cortex-a57'`.
The native diagnostic requires the `zephyr` accelerator. `make check` provides
Linux acceptance for both backends.
