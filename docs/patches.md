# Patch series

Each upstream repository has a `patches/<project>/series` file. Preparation
applies entries in that order and rejects duplicate, missing or unlisted
patches. Upstream Git submodules remain clean.

The original rollups were split by responsibility. Applying the first ten
QEMU patches and the first six Zephyr patches produces exactly the same modified
upstream file trees as the original rollups. TCG and shared CPU support are
additional patches after that baseline.

## QEMU

| Patch | Responsibility |
| --- | --- |
| 0001 | Zephyr host headers, bit helpers and event descriptors |
| 0002 | Aligned host allocation |
| 0003 | Executable/data paths and file readability |
| 0004 | Diagnostics without wall-clock GLib objects |
| 0005 | Unsupported deterministic guest random mode |
| 0006 | CPU, machine and accelerator lifecycle integration |
| 0007 | Device models without migration registration |
| 0008 | ARM loader without firmware configuration services |
| 0009 | Preallocated guest RAM |
| 0010 | Host runstate integration |
| 0011 | TCG code allocation through separate RW/RX aliases |
| 0012 | GICv3-only CPU initialization for the selected models |
| 0013 | Share the unchanged Cortex-A72 model across accelerators |
| 0014 | Linux user ELF loading and checked TCG user memory on Zephyr |
| 0015 | Native host CPU registration for the Zephyr accelerator |
| 0016 | Accurate fractional-nanosecond counter rates and timer expiry |

## Zephyr

| Patch | Responsibility |
| --- | --- |
| 0001 | Non-VHE EL2 configuration and startup |
| 0002 | EL2 stage-one translation and TLB handling |
| 0003 | EL2 exceptions, diagnostics and thread state |
| 0004 | Hypervisor physical timer for the host |
| 0005 | EL2 lazy FPU ownership |
| 0006 | Native executor build hook and QEMU board configuration |
| 0007 | Read-only Ext2 inode close and synchronization |
| 0008 | ARM64 interrupt metadata placement for virtual address ranges above 4 GiB |
| 0009 | Runtime architected counter frequency for virtual hardware |

These are module integration patches: new implementation files remain in
`src/qemu/` and `src/zephyr/` and are overlaid by `make prepare`. The series
alone is not a standalone upstream source submission. A complete profile is
built after applying its series and overlays.

## Maintaining a change

Use a temporary checkout at the pinned upstream revision, apply the preceding
patches, then commit one functional change and export it with
`git format-patch`. Add the filename to `series` at the required position.
Do not append unrelated changes to the existing rollups or change pristine
submodule revisions to private patched commits.

Use QEMU's `subsystem: summary` format for QEMU patches and Zephyr's
`area: subarea: summary` format for Zephyr patches. Wrap commit messages to
at most 72 characters and include the author's DCO sign-off. Preserve
upstream copyright and licensing when moving code.

Run `make prepare`, `make test-tools`, and the affected backend/CPU acceptance
tests after editing a series. Patch ordering is explicit rather than inferred
from directory enumeration.
