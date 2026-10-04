/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_ZEPHYR_ACCELERATOR_H
#define QEMU_ZEPHYR_ACCELERATOR_H

#include "hw/core/cpu.h"

/* Both backends run on the model owner; guest execution may release BQL. */
int qemu_zephyr_cpu_exec(CPUState *cpu);
int qemu_zephyr_cpu_wait(CPUState *cpu, int64_t deadline_ns);
void qemu_zephyr_cpu_kick(CPUState *cpu);
void qemu_zephyr_cpu_stop(void);
void *qemu_zephyr_code_buffer(size_t size, void **rx);

#endif
