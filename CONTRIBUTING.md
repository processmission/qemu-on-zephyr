# Development

Keep changes to the upstream repositories separate from new module code.

* Edit new implementation files under `src/qemu/` or `src/zephyr/`.
* Edit module build configuration under `zephyr/`.
* Store changes to existing upstream files as patches under `patches/<project>/`.
* Never rely on edits made only inside `build/sources/`: preparation replaces it
  whenever an overlay, patch, dependency pin or preparation script changes.
* Keep `upstream/` submodules clean. Preparation rejects tracked modifications
  and unexpected revisions rather than silently ignoring them.

`make build` regenerates the prepared sources automatically when necessary.
Source patches apply in filename order, followed by the source overlays. Keep
an upstream file in a patch and a new file in an overlay, not both.

To develop an upstream patch, use a temporary checkout of the pinned commit,
apply the existing patches there, edit the upstream files, and export `git diff
--binary HEAD` into the patch file. Copy new files into `src/<project>/`. Do not
change the submodule's recorded commit to a private patched commit.

To upgrade upstream, select an upstream commit in its submodule, update the
matching value in `dependencies.json`, rebase the patches and overlays, then
run the tests before committing the new Git link and pin together. Pins are
also recorded for libfdt/dtc and zlib. Do not run an unbounded recursive QEMU
submodule update.

## Reuse as a module

Run `make prepare` and `make assets` in this repository. Configure an application
against `build/sources/zephyr` and add the **repository root** to
`ZEPHYR_EXTRA_MODULES`. The sample applications show this arrangement. For example:

```sh
export ZEPHYR_BASE="$PWD/build/sources/zephyr"
cmake -S apps/qemu_linux -B build/custom -G Ninja \
    -DBOARD=qemu_cortex_a53 -DZEPHYR_MODULES="$PWD"
cmake --build build/custom
```

`CONFIG_QEMU` enables the module. The Linux profile additionally requires
`CONFIG_ARM64_EL2`, `CONFIG_ARM64_HYPERVISOR` and `CONFIG_QEMU_ZEPHYR_ACCEL`.
The full sample configuration is in `apps/qemu_linux/prj.conf`.

The module does not make an unpatched upstream Zephyr capable of hosting a
guest at EL2. Use the prepared Zephyr tree until the architecture changes are
available upstream. Current source and guest paths are resolved relative to
this module; a west workspace is optional.

## Validation

Run `make check`, `make probe`, `make test-payload` and `make test-glib` after
changing integration code. `tests/glib/check_cross.sh` adds an AArch64 compile
check when `ZEPHYR_SDK_INSTALL_DIR` is set.

Native executor, EL1/EL2 and FPU regression tests are preserved under
`src/zephyr/tests/` and copied into the prepared Zephyr tree. In the activated
Python virtual environment, run:

```sh
pip install -r tests/requirements.txt
make test-arch
```

Include relevant test results with changes. Preserve original copyright and
SPDX notices. Use signed-off commits under the Developer Certificate of Origin
and do not include build outputs, guest binaries, credentials or session logs.
