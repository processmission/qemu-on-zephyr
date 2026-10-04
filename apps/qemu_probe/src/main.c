/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <qemu/zephyr.h>

int main(void)
{
    uint64_t current_el;
    int result;

    __asm__ volatile("mrs %0, CurrentEL" : "=r"(current_el));
    printk("QEMU Zephyr host EL%llu\n", current_el >> 2);
    if (current_el != 8U) {
        return -1;
    }
    result = qemu_zephyr_pl011_probe();
    printk("QEMU probe result=%d\n", result);
    return result;
}
