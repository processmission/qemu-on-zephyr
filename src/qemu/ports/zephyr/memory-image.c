/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "system/zephyr.h"
#include <zephyr/devicetree.h>
#include <zephyr/virtualization/zhv.h>

const struct zhv_ram *zephyr_guest_rom(void)
{
#ifdef CONFIG_QEMU_MEMORY_IMAGE
    static const struct zhv_ram image = {
        .host_va = (void *)DT_REG_ADDR(DT_CHOSEN(qemu_memory_image)),
        .guest_ipa = 0x100000000ULL,
        .size = DT_REG_SIZE(DT_CHOSEN(qemu_memory_image)),
    };

    return &image;
#else
    return NULL;
#endif
}
