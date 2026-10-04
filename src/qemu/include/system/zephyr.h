/*
 * Copyright (c) 2026 Chao Liu
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef QEMU_SYSTEM_ZEPHYR_H
#define QEMU_SYSTEM_ZEPHYR_H

#include "qemu/typedefs.h"

bool zephyr_enabled(void);

/* The Machine owns the MemoryRegion; the native VM owns this RAM allocation. */
int zephyr_get_guest_ram(AccelState *accel, void **ptr, uint64_t *ipa,
                        uint64_t *size, Error **errp);
int64_t zephyr_clock_get_ns(void);
uint64_t zephyr_counter_frequency(void);

/*
 * Call on the single QEMU owner thread with BQL held and outside RCU readers.
 * exec handles one native exit and returns EXCP_INTERRUPT or EXCP_HLT; negative
 * errno means a diagnosed terminal failure. It never executes translated code.
 * wait also returns with BQL held; -1 means no extra caller deadline.
 */
int zephyr_cpu_exec(CPUState *cpu);
int zephyr_cpu_wait(CPUState *cpu, int64_t max_wait_ns);
int zephyr_cpu_last_error(CPUState *cpu);

/* ISR/event producers may wake the owner; they must not enter device models. */
void zephyr_cpu_kick(CPUState *cpu);

/* Intermediate native-execution/PL011 check on an already-realized Machine. */
int zephyr_accel_probe(CPUState *cpu, Error **errp);

#endif
