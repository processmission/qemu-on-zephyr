# Linux desktop and README recordings

The desktop profile runs Alpine Linux AArch64 with Xorg, the fbdev driver,
JWM, xterm and xclock. The [Alpine minimal filesystem](https://alpinelinux.org/downloads/)
and its [JWM package](https://pkgs.alpinelinux.org/package/v3.23/community/aarch64/jwm)
provide the userspace. The guest kernel comes from Alpine's
[linux-virt package](https://pkgs.alpinelinux.org/package/v3.23/main/aarch64/linux-virt).

## Build and run

Complete `make setup` and start a Docker daemon that can execute `linux/arm64`
containers. The image builder runs on Linux and macOS; an x86_64 Docker host
needs ARM64 container emulation.

```sh
make desktop
```

This creates `build/desktop-files/`, builds a separate Zephyr desktop profile,
creates `build/desktop-disk.img`, and opens the outer QEMU display. The serial
terminal supplies the Linux shell. Desktop changes are held in memory and
discarded when the VM exits.

To reuse the generated image:

```sh
make run QEMU_DESKTOP=1
```

Use `QEMU_DISPLAY=none` for execution without a display window. Set
`QEMU_SYSTEM_AARCH64` to select an external QEMU binary if the installed binary
lacks `ramfb` or a local display backend.

The display presents guest pixels. Desktop interaction is available through
the Linux serial shell with `DISPLAY=:0` and `xdotool`; physical keyboard and
mouse events from the outer display are not forwarded to Xorg. For example:

```sh
xdotool search --name 'Linux AArch64 inside Zephyr' windowactivate --sync
xdotool type 'uname -a'
xdotool key Return
```

`poweroff -f` shuts down Linux and restores the Zephyr prompt. Ctrl-] stops the
guest. Use `kernel reboot cold` before starting another system guest.

## Image and memory layout

The Docker base image and the `unzboot` source revision are pinned in
`samples/linux-desktop/Dockerfile`. APK resolves the desktop dependencies from
the Alpine v3.23 repositories. Installed package versions, the Linux kernel
configuration and the Docker image ID are saved alongside the generated images.
[unzboot](https://github.com/eballetbo/unzboot) extracts the ARM64 Image from
Alpine's EFI kernel using its format validation and decompressor.

The initramfs contains a static BusyBox, the required kernel modules, and an
XZ-compressed SquashFS root filesystem. Linux mounts that filesystem with a
writable tmpfs overlay. `rootfstype=ramfs` allows the boot archive to unpack
within the 256 MiB guest memory reservation. The bootstrap then releases the
boot archive and starts the desktop from the compressed filesystem.

`CONFIG_QEMU_FRAMEBUFFER` reserves the final 4 MiB of guest RAM at
`0x4fc00000`. The Linux memory node excludes this region, and the device tree
describes it as a `simple-framebuffer`. Linux simpledrm exposes `/dev/fb0`.
The QEMU owner copies ARGB8888 pixels to the existing Zephyr display driver
every 100 ms. The sample's outer `ramfb` uses a separate 4 MiB region at
`0x5fc00000`, with an 800 × 600 display. The outer machine retains 512 MiB RAM;
the desktop profile provides a 160 MiB host libc heap for loading the images.

The Zephyr Ext2 implementation accepts one block group. Managed disks use
4096-byte blocks and remain at most 128 MiB. The image builder rejects file
contents above 112 MiB, leaving space for filesystem metadata.

## Record the demonstrations

Install the recording dependencies in the project environment:

```sh
.venv/bin/python -m pip install -r requirements-demo.txt
make record-demos
```

The recorder boots both Zephyr profiles, executes the demonstration commands,
checks the guest results, and writes:

| File | Captured content |
| --- | --- |
| `docs/images/system-desktop.gif` | QEMU display frames while Linux runs Xorg and JWM |
| `docs/images/linux-user.gif` | Actual Zephyr UART output during TCG program execution |

Startup console output plays at 12× speed; `--boot-speed` changes that multiplier.
The desktop and program demonstrations play at normal speed with five captured
frames per second. Once JWM and xterm are visible, the desktop recording shows
terminal commands, the desktop menu, and window dragging. QMP supplies desktop
screenshots; pyte interprets the terminal's ANSI
output and Pillow encodes the GIFs. No generated guest output is inserted.

Serial logs, asciicast streams, final PNG frames, invocation details and GIF
checksums are saved under `build/demo-recordings/`. The desktop check verifies
Xorg, JWM, xterm and `/dev/fb0`, then powers off Linux and checks the Zephyr
status command. The user check verifies program arguments, environment
variables and successive executions.

Individual recordings can reuse existing firmware:

```sh
.venv/bin/python scripts/record_demos.py --mode desktop --no-build
.venv/bin/python scripts/record_demos.py --mode user --no-build
```

The recorder uses Menlo on macOS or DejaVu Sans Mono on Linux. `--font` accepts
another monospace TrueType font, and `--output` selects another output directory.
