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
