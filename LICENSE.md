# Licenses and source provenance

This repository contains components with different licenses. Existing file
headers are retained; a single root license does not replace them.

| Component | License information |
| --- | --- |
| New QEMU port code in `src/qemu/`, QEMU module build metadata | GPL-2.0-or-later, as identified in each file |
| New Zephyr architecture code and tests in `src/zephyr/` | Apache-2.0, as identified in each file |
| New orchestration scripts, applications and documentation | Apache-2.0, unless a file states otherwise |
| Upstream QEMU, Zephyr, dtc/libfdt and zlib | Their upstream licenses and per-file notices |
| Patches | Preserve the licensing of the upstream files they modify |
| NanoJev source fetched for the CPU desktop image | MIT; its license remains in `/opt/nanojev/LICENSE` inside the image |

The GPL version 2 and Apache version 2 license texts are in `LICENSES/`.
Upstream license texts are retained in their submodules. QEMU's upstream
`LICENSE` describes its mixed licensing; inspect the actual linked components
when preparing a redistributable combined image. This source repository does
not assign a new blanket license to the statically linked ELF.

Linux and initramfs binaries are fetched separately for testing, verified by
hash, and excluded from version control. Their source locations and hashes are
documented in `docs/guest-assets.md`; no guest binary release is included here.
The optional NanoJev image fetches its runtime and checkpoint separately.
Source revisions and the model checksum are documented in `docs/nanojev.md`.
