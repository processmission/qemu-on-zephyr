/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_ZEPHYR_FRAMEBUFFER_H
#define QEMU_ZEPHYR_FRAMEBUFFER_H

#define QEMU_ZEPHYR_FRAMEBUFFER_SIZE (4 * 1024 * 1024)

void qemu_zephyr_framebuffer_init(void *fdt, void *pixels, uint64_t address);

#endif
