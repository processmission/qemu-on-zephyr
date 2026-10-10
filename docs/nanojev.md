# NanoJev CPU desktop

The additional NanoJev image runs Debian 12 ARM64 userspace, Xorg, JWM and
PyTorch CPU inside the `zephyr` system-mode accelerator. ZHV executes the Linux
guest at EL1 and provides Stage-2 memory isolation. The guest has one AArch64
vCPU, 3 GiB RAM and a 1024 × 768 framebuffer.

## Build and start

Complete the normal workspace setup and start Docker with Linux ARM64 support.
Allow at least 16 GiB of host RAM and sufficient disk space for the Docker layers,
model downloads and generated images.

```sh
make nanojev
```

For the hardware-accelerated recording on Apple Silicon with nested EL2 support,
use a recent Homebrew QEMU and the native host CPU profile:

```sh
make nanojev CPU=host HOST_ACCEL=hvf QEMU_SYSTEM_AARCH64="$(command -v qemu-system-aarch64)"
```

The desktop loads the model once. Its window displays an 8 × 8 generated maze,
four local safety probabilities, actual movement, collisions and inference
duration. NanoJev supplies the local judgments. The upstream `EdgeExplorer`
maintains movement memory and explores edges using those probabilities.

Use the Linux serial shell to activate and control the window:

```sh
xdotool search --onlyvisible --name 'NanoJev CPU' windowactivate --sync
xdotool key Right
xdotool key space
xdotool key space
xdotool key r
```

`Right` performs one step, `space` toggles continuous execution, and `r` resets
the maze. The size selector and `F5` / `F8` choose a 5 × 5 or 8 × 8 maze.
A running prediction finishes before pause or reset takes effect.
The serial shell remains available during inference. `busybox poweroff -f`
shuts down Linux and returns to Zephyr.

Build assets separately with `make nanojev-assets`. Subsequent launches can use
`make run QEMU_NANOJEV=1`; add `QEMU_SHELL=1` for manual startup. At `zephyr>`:

```text
qemu-system-aarch64 -M zephyr-virt -accel zephyr -cpu cortex-a53 -kernel /images/Image -initrd /images/initramfs.cpio.gz -append "console=ttyAMA0 rdinit=/init rootfstype=ramfs panic=-1"
```

## Image and memory layout

The small boot disk supplies the kernel and bootstrap initramfs through Zephyr
Ext2. A separate SquashFS filesystem contains Debian, the model runtime and the
checkpoint. Linux mounts that filesystem through its existing `pmem` driver,
then adds a temporary writable OverlayFS layer.

| Region | Address and size |
| --- | --- |
| Zephyr main RAM in outer QEMU | 1 GiB physical base, 4 GiB size |
| Linux guest RAM | IPA `0x40000000`, 3 GiB size |
| Linux framebuffer | Final 4 MiB of guest RAM |
| Image backing in outer RAM | Physical `0x180000000`, 4 GiB size |
| Linux read-only image | IPA `0x100000000`, 4 GiB size |

`build/nanojev-files/memory.img` is a sparse 10 GiB file. It contains the
SquashFS image at file offset 5 GiB. Outer QEMU maps the file privately as its
RAM backing, so guest writes do not update the image. ZHV maps the filesystem
region read-only and prevents instruction execution from it. The guest's
physical address width is 36 bits for this profile.

The Linux kernel and bootstrap modules use the same Alpine `linux-virt` package
as the smaller desktop profile. Debian provides the guest userspace. Package
versions, source revisions and file hashes are saved in `build/nanojev-files/`.

## Model provenance

The maze uses the checkpoint referenced by NanoJev's original maze demonstration:

| Component | Pin |
| --- | --- |
| CPU-capable source | `LeanFly/NanoJev` at `9a5238fe86ca687ba264aabe0d26d77b3b93f939` |
| Model repository | `C-Tianyu/NanoJev` at `047b927b30882a1138fc504821b82ac145a4b81a` |
| Variant | `variants/local_atomic_seed17` |
| Weights SHA256 | `2b06e4423861f47da7ca53ad9c23a26aebdbfe79de3a238b36c2954ae9c6abf0` |
| Runtime | PyTorch `2.14.0+cpu`, Transformers `5.17.0`, QNNPACK dynamic INT8 |

The CPU patch uses Accelerate's empty parameter initialization and PyTorch's
`load_state_dict(assign=True)` to load the actual checkpoint without a second
initialized parameter allocation. The model architecture, tokenizer and scoring
code come from NanoJev. Inference reads local weights with Hub access disabled.
PyTorch dynamically quantizes the backbone's linear layers to INT8. Embeddings,
normalization and decision heads retain FP32 parameters. Each result records the
quantization mode and number of quantized linear modules.

Sources: [NanoJev](https://github.com/TianyuCodings/NanoJev),
[CPU implementation](https://github.com/LeanFly/NanoJev),
[checkpoint](https://huggingface.co/C-Tianyu/NanoJev/tree/047b927b30882a1138fc504821b82ac145a4b81a/variants/local_atomic_seed17),
[PyTorch loading API](https://docs.pytorch.org/tutorials/recipes/recipes/module_load_state_dict_tips.html).

## Validation and recording

```sh
.venv/bin/python -m pip install -r requirements-demo.txt
make check-nanojev
```

Install FFmpeg and FFprobe before recording (`brew install ffmpeg` on macOS,
or `sudo apt-get install ffmpeg` on Debian/Ubuntu).

The separate `NanoJev CPU desktop` workflow builds the ARM64 image using
`qemu-user-static` on an x86_64 Ubuntu runner, then runs the ZHV guest with the
SDK QEMU and a complete 5 × 5 maze. The default local recording
uses an 8 × 8 maze. The check verifies Zephyr EL2 startup, the Linux pmem device,
real CPU forward passes, finite probabilities, window interaction, goal arrival
and guest shutdown. It saves the full requests and responses, serial output,
image provenance and captured frames in `build/nanojev-validation/`.

Recording first produces `nanojev-original.mp4` at normal speed. Video processing
accelerates boot and model initialization by 12×, keeps the complete maze
execution at normal speed, and produces `nanojev.mp4` and `nanojev.gif`.
`recording.json` records the phase boundary and verifies the edited durations.
The window displays each inference's actual duration. The recorded command
identifies the outer QEMU accelerator; the inner guest uses `zephyr`.

To record a complete maze from an already built image:

```sh
.venv/bin/python scripts/check_nanojev.py --no-build --maze-size 8
```

For the HVF recording:

```sh
HOST_ACCEL=hvf QEMU_SYSTEM_AARCH64="$(command -v qemu-system-aarch64)" \
    .venv/bin/python scripts/check_nanojev.py --cpu host --maze-size 8
```

The README recording reaches the goal after 19 attempted moves and 3 collisions.
Its original video is 228.6 seconds. The edited MP4 and GIF are 205.4 seconds,
including the 25.2-second boot and initialization segment played at 12× speed.
