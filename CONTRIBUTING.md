# Development

Usage and external-program updates are documented in the
[English guidelines](docs/guidelines.md) and [Chinese guidelines](docs/guidelines.zh-CN.md).

Keep changes to the upstream repositories separate from new module code.

* Edit new implementation files under `src/qemu/` or `src/zephyr/`.
* Edit module build configuration under `zephyr/`.
* Store changes to existing upstream files as patches under `patches/<project>/`.
* Never rely on edits made only inside `build/sources/`: preparation replaces it
  whenever an overlay, patch, dependency pin or preparation script changes.
* Keep `upstream/` submodules clean. Preparation rejects tracked modifications
  and unexpected revisions rather than silently ignoring them.

`make build` regenerates the prepared sources automatically when necessary.
Source patches apply in each project's `series` order, then source overlays. Keep
an upstream file in a patch and a new file in an overlay, not both.

To develop an upstream patch, use a temporary checkout of the pinned commit,
apply the existing patches there, make one focused commit per change, and
export each commit with `git format-patch`. Update `patches/<project>/series`
and include a DCO sign-off with commit message lines at most 72 characters.
Copy new files into `src/<project>/`. Do not
change the submodule's recorded commit to a private patched commit.

To upgrade upstream, select an upstream commit in its submodule, update the
matching value in `west/west.yml`, rebase the patches and overlays, then
run the tests before committing the new Git link and pin together. Pins are
also recorded for libfdt/dtc and zlib. Do not run an unbounded recursive QEMU
submodule update.

## Reuse as a module

Complete `make setup` in this repository first. Configure an application
against `build/sources/zephyr` and add the **repository root** to
`ZEPHYR_EXTRA_MODULES`. The sample applications show this arrangement. For example:

```sh
. .tools/env.sh
west build -b qemu_cortex_a53 -d build/custom apps/qemu_linux
```

`CONFIG_QEMU` enables the module. The native system profile additionally requires
`CONFIG_ARM64_EL2`, `CONFIG_ARM64_HYPERVISOR` and `CONFIG_QEMU_ZEPHYR_ACCEL`.
The full sample configuration is in `apps/qemu_linux/prj.conf`.

The module does not make an unpatched upstream Zephyr capable of hosting a
guest at EL2. Use the prepared Zephyr tree until the architecture changes are
available upstream. Current source and guest paths are resolved relative to
this module. The managed workflow uses a repository-local west workspace.

## Validation

Run `make check`, `make probe`, `make test-payload` and `make test-glib` after
changing integration code. `tests/glib/check_cross.sh` adds an AArch64 compile
check when `ZEPHYR_SDK_INSTALL_DIR` is set.

Native executor, EL1/EL2 and FPU regression tests are preserved under
`src/zephyr/tests/` and copied into the prepared Zephyr tree. Using the managed Python environment, run:

```sh
make setup
make test-arch
```

Include relevant test results with changes. Preserve original copyright and
SPDX notices. Use signed-off commits under the Developer Certificate of Origin
and do not include build outputs, guest binaries, credentials or session logs.

Run Linux acceptance for both backends and each supported CPU after changing
CPU or execution code. See [backend profiles](docs/backends.md) and
[patch maintenance](docs/patches.md).
