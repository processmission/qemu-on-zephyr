# Implementation map

The application starts a POSIX worker with a statically allocated 128 KiB stack
and calls `qemu_zephyr_linux_main()`. An independent higher-priority Zephyr
thread supplies scheduling evidence through periodic heartbeat messages.

## QEMU model and event loop

`src/qemu/ports/zephyr/bootstrap.c` initializes the normal QEMU subsystems,
selects the configured `zephyr` or `tcg` accelerator, creates the machine and
starts the VM. The owner thread processes runstate requests, console input,
QEMU timers, deferred RCU callbacks and backend execution returns.

`machine.c` defines `zephyr-virt-machine` with a selected Cortex-A53/A57/A72,
256 MiB guest RAM, the original PL011 and software GICv3 models. It generates the guest DTB
and calls upstream `arm_load_kernel()`. The guest Image and initramfs are
embedded in the host ELF, exposed through `/guest` by `payload-fs.c`, and read
by the QEMU file adapter. Native execution borrows the executor RAM; TCG
uses a separate buffer accessed through its software MMU.

`os.c` implements the supported host synchronization and allocation facilities.
The GLib subset supplies the data structures and APIs needed by the selected
QEMU sources. Unsupported host operations fail explicitly. The physical UART
ISR only queues bytes and kicks the owner; device handlers execute on the QEMU
thread.

## Native execution

`src/qemu/accel/zephyr/zephyr-accel.c` registers `AccelClass` and `AccelOpsClass`.
`src/qemu/target/arm/zephyr.c` registers the ARM accelerator interface, transfers
CPU state, dispatches MMIO through `address_space_rw()`, and handles trapped
system registers and PSCI calls. The BQL is released during native execution
and waiting, then reacquired before accessing model state.

`src/zephyr/include/zephyr/virtualization/zhv.h` is the public executor API.
`src/zephyr/arch/arm64/core/hypervisor/executor.c` manages VM/vCPU lifetime,
Stage-2 memory, traps, virtual timer sampling and wait/kick synchronization.
`switch.S` saves and restores host/guest registers, TLS, SIMD/FP state and EL2
controls through dedicated guest exception vectors.

The host uses non-VHE EL2, a host Stage-1 mapping and the CNTHP timer. Guest RAM
has a separate nonidentity Stage-2 mapping. The guest's EL1 Stage-1/MMU state
executes natively. CNTV/CNTVCT run natively; CNTP/CNTPCT are trapped and modeled.
Software GICv3 state drives the next entry's virtual IRQ/FIQ levels. A physical
guest-timer interrupt causes a host exit, not a direct guest device interrupt.

## TCG execution

`zephyr/tcg.cmake` builds upstream TCG and ARM translation sources and generates
instruction decoders with QEMU's decodetree script. The accelerator class is
upstream `tcg-accel`; `ports/zephyr/tcg.c` supplies the single-owner AccelOps,
periodic exit requests, wait/kick handling and split RW/RX code buffer mapping.
The guest runs through QEMU's software TLB and original device dispatch.

The default TCG host runs at EL1 with outer virtualization disabled. A-profile
CPU models are independent of the outer Cortex-A53. The native adapter instead
checks that the selected model matches the physical MIDR and configures
Stage-2 for the physical address range reported by the host. See
[backend profiles](backends.md) for configuration and limits.

## Build boundary

`patches/zephyr/` extends existing ARM64 startup, MMU, exception, FPU and timer
code for the EL2 host. `patches/qemu/` adapts existing loader, CPU, memory,
runstate and host utility paths. New files live in the two `src/` overlays.

`scripts/project.py prepare` exports the exact pinned upstream commits, applies
the explicitly ordered patch series and overlays, and installs the pinned dtc/zlib sources into the
generated QEMU tree. It does not mutate upstream submodules. `zephyr/CMakeLists.txt`
builds the selected QEMU sources directly through Zephyr CMake and regenerates
QAPI/trace files from those sources. It does not reuse a host QEMU build or run
QEMU's full Meson configuration.

Preparation is content-addressed by the pins, patches, overlays and preparation
script. Keep editable source in this repository, not the generated tree.

Environment setup uses the repository-local west manifest, module Python package
metadata and the official SDK installer. Make wraps west build and west twister.
See [setup](setup.md) for the managed and native command paths.
