# Workspace validation

The packaged repository was built and tested on 2026-10-04 with Zephyr SDK
1.0.1 and outer QEMU 10.2.2. These are runs of the new module workspace, not
results copied from its predecessor.

| Check | Result |
| --- | --- |
| Clean initial build | Linked the AArch64 Zephyr ELF with QEMU and the native executor |
| `make check` | Linux shell, timer IRQ 355 -> 392, EL0=784, MMU-on=25499, host scheduling and survival after guest poweroff |
| Interactive `make run` | Linux shell; `uname -m` returned `aarch64`; Ctrl-a x exited successfully |
| `make probe` | QOM/PL011 register read/write regression passed |
| `make test-payload` | 196 checks, zero failures |
| `make test-glib` | 177 differential output lines matched; both allocation-overflow cases aborted as expected |
| GLib AArch64 cross-compile | Passed, with no unresolved GLib symbols |
| Twister architecture tests | Five configurations, 26 test cases passed: EL1/EL2 6, FPU 12, executor 8 |
| Fresh clone | Initialized all four submodules from their public upstream URLs, downloaded assets, built, and passed Linux acceptance |

The fresh-clone acceptance recorded timer IRQ 201 -> 228, EL0=1289 and
MMU-on=26895. Its source preparation and build used paths derived from the
clone, with no source or binary dependency on the predecessor workspace.
All four upstream submodules remained clean after building.

The full Linux build retains upstream QEMU compiler warnings, including GNU
pointer arithmetic and unused code in the selected source subset. The GLib
overflow fixture also intentionally triggers an allocation-size warning.
Successful build and runtime checks do not imply a warning-free full QEMU
port or full upstream compliance.

Generated logs are kept under `build/` and are deliberately excluded from Git:
`linux-validation.log`, `interactive-run.log`, `probe.log`, `payload.log`,
`glib-test.log`, `glib-cross.log`, `twister/`, and `clone-check.log`.
`make clean` removes them. The fresh-clone experiment lives under
`build/clone-check/` and is disposable.

No physical hardware test, upstream-wide test suite or production isolation
assessment is included in these results.

## Managed west environment

The west-based setup was additionally exercised on an Ubuntu 24.04 container
with Python 3.12 and no preinstalled Zephyr SDK. The system dependency script,
isolated Python install, public upstream fetches and SDK installation ran inside
that container. SDK 1.0.1's AArch64 GNU compiler and host tools were downloaded
by the official installer, not copied from the development host.

The test also exercised recovery from an incomplete Git fetch. Narrow fetches
are required for depth-one clones of pinned historical commits. SDK downloads
were resumed with proxy variables normalized for the official wget-based
installer; uppercase-only and explicit lowercase proxy handling have a regression
test. Existing upstream work is checked before wrapper-driven updates.

The container's Linux acceptance passed with SDK QEMU 10.0.2: timer IRQ
214 -> 243, EL0=1395, MMU-on=26962, concurrent host scheduling and host survival
after guest poweroff. Ten environment/manifest regression tests passed.
The same container also passed the PL011 probe, all 196 filesystem checks,
177-line GLib differential comparison, and all 26 architecture test cases.

On the development host, repeated `make setup`, `make doctor`, direct
`west build`, managed `make run` with Ctrl-a x, and `west twister` passed.
The latter ran all five configurations and 26 architecture test cases.
Logs for the managed workflow are in `build/west-check.log`,
`build/west-arch.log`, `build/managed-interactive.log`, `build/tools-final.log`
and `build/ubuntu-sdk-validation.log`.

## Native and TCG CPU profiles

The two backends were validated with Zephyr SDK 1.0.1 and its QEMU 10.0.2.
Native tests use an outer CPU matching the guest model and an EL2 host. TCG
tests use an outer Cortex-A53 with virtualization disabled and an EL1 host.
Every row passed Linux shell interaction, timer IRQ growth, EL0/MMU samples,
host progress during a busy guest, and host survival after guest poweroff.

| Backend | Guest CPU | Timer IRQ before/after | EL0 samples | MMU-on samples |
| --- | --- | --- | --- | --- |
| zephyr | Cortex-A53 | 200 -> 223 | 1397 | 26994 |
| zephyr | Cortex-A57 | 288 -> 324 | 1106 | 26770 |
| zephyr | Cortex-A72 | 197 -> 222 | 1513 | 27588 |
| tcg | Cortex-A53 | 1723 -> 1773 | 198 | 12597 |
| tcg | Cortex-A57 | 1434 -> 1478 | 339 | 13175 |
| tcg | Cortex-A72 | 1524 -> 1565 | 327 | 12877 |

TCG additionally reported distinct writable and executable JIT mappings and
nonzero execution return counts. Native Cortex-A72 on an outer Cortex-A53 was
rejected by the MIDR compatibility check before Linux execution.

After integration, all 26 architecture/FPU/executor cases and 196 filesystem
checks passed. The standalone PL011 regression passed, the expanded GLib
differential fixture matched 184 lines, and 16 environment/patch/profile tests
passed. The original 10 QEMU and 6 Zephyr baseline patches were verified to
produce the same upstream file trees as the previous rollups; three new QEMU
patches add the TCG mapping, GICv3-only profile and shared Cortex-A72 model.

Profile consoles are retained as `build/linux*-validation.log`. Other evidence
is in `build/native-model-mismatch.log`, `build/final-arch.log`,
`build/final-glib.log`, `build/final-tools.log`, `build/final-probe.log` and
`build/final-payload.log`. These are local tests; the CI workflow separately
runs the six backend/CPU combinations on Ubuntu and macOS.

## Linux and macOS hosts

Host compatibility was validated on 2026-10-08 with Zephyr SDK 1.0.1 and
SDK-provided QEMU 10.0.2:

| Host | Python | SDK configuration |
| --- | --- | --- |
| macOS 26.5.2, Apple Silicon | 3.14.8 | Reused a registered macOS SDK |
| Ubuntu 24.04, AArch64 container | 3.12.3 | Installed compiler and host tools with `west sdk install` |

Both environments passed the host dependency installer, `make setup`,
`make doctor`, and the 17 environment, manifest, patch and profile tests in
`make test-tools`. SDK and QEMU selection tests execute the installed tools,
including a QEMU override whose path contains spaces. The macOS dependency
installer also passed with the system-provided `/bin/bash`.

Both hosts passed `make check` with Cortex-A53 under the `zephyr` and `tcg`
backends. Acceptance checked the Linux shell, timer IRQ growth, EL0/MMU
execution, concurrent host scheduling and host survival after guest poweroff.
The TCG runs also verified separate writable and executable code mappings.

On each host, `make probe`, `make test-payload`, `make test-glib` and
`make test-arch` passed: the PL011/QOM probe, 196 filesystem checks, matching
184-line GLib output, allocation overflow handling, and 26 architecture/FPU/
executor test cases. `tests/glib/check_cross.sh` passed with the selected SDK.

Local validation logs are retained in `.host-compat/`, excluded through
`.git/info/exclude`. The CI matrix runs Ubuntu 24.04 and macOS 14 Apple Silicon
with separate SDK caches and log artifacts for each operating system.

## QEMU-style Make options

The `QEMU_ARGS` interface was validated on 2026-10-08. All six combinations of
`zephyr`/`tcg` and Cortex-A53/A57/A72 passed `make check` on macOS Apple Silicon
using SDK QEMU 10.0.2. The checker matched the requested inner machine, CPU and
accelerator against the boot output, then verified guest and host behavior.
The outer board remained `virt` throughout.

The 25 tooling tests passed on macOS. The Make argument and configuration
tests also passed on Ubuntu 24.04 with Python 3.12, exercising quoting,
environment defaults, explicit overrides, machine properties, selection help,
and rejection of unsupported or conflicting options before building.

`make run QEMU_ARGS='-M zephyr-virt -accel zephyr -cpu cortex-a57'` entered the
Linux shell. `uname -m` returned `aarch64`, and Ctrl-a followed by x terminated
outer QEMU successfully. `make probe`, `make test-payload` and `make test-glib`
passed with the configured machine support.

Logs for this validation are retained under `.qemu-args/`, excluded through
`.git/info/exclude`. CI selects every backend/CPU combination through
`QEMU_ARGS` on both configured host operating systems.
