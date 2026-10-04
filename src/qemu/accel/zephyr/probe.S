/*
 * Copyright (c) 2026 Chao Liu
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

    .text
    .balign 4
    .global qemu_zephyr_probe_start
    .global qemu_zephyr_probe_end
    .type qemu_zephyr_probe_start, %function
qemu_zephyr_probe_start:
    mov x0, #0x09000000
    mov w1, #0x301
    str w1, [x0, #0x30]
    mov w1, #90
    str w1, [x0]
    mov w1, #72
    str w1, [x0]
    mov w1, #86
    str w1, [x0]
    mov w1, #10
    str w1, [x0]
    mrs x2, cntpct_el0
    mrs x3, cntvct_el0
    stp x2, x3, [x19]
    wfi
    b .
qemu_zephyr_probe_end:
    .size qemu_zephyr_probe_start, . - qemu_zephyr_probe_start
