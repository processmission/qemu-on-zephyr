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
