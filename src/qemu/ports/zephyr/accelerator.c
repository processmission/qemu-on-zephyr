/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "accelerator.h"
#include "system/zephyr.h"

#ifdef CONFIG_QEMU_ZEPHYR_ACCEL
int qemu_zephyr_cpu_exec(CPUState *cpu)
{
    return zephyr_cpu_exec(cpu);
}

int qemu_zephyr_cpu_wait(CPUState *cpu, int64_t deadline_ns)
{
    return zephyr_cpu_wait(cpu, deadline_ns);
}

void qemu_zephyr_cpu_kick(CPUState *cpu)
{
    zephyr_cpu_kick(cpu);
}

void qemu_zephyr_cpu_stop(void)
{
    /* Native execution has already returned to the model owner. */
}
#endif
