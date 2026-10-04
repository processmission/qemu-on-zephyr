/*
 * Copyright (c) 2026 Chao Liu
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef QEMU_ACCEL_ZEPHYR_INTERNAL_H
#define QEMU_ACCEL_ZEPHYR_INTERNAL_H

#include "accel/accel-ops.h"
#include "hw/core/cpu.h"
#include "system/zephyr.h"
#include <zephyr/virtualization/zhv.h>

#define TYPE_ZEPHYR_ACCEL ACCEL_CLASS_NAME("zephyr")
OBJECT_DECLARE_SIMPLE_TYPE(ZephyrAccelState, ZEPHYR_ACCEL)

struct ZephyrAccelState {
    AccelState parent;
    struct zhv_vm *vm;
    struct zhv_ram ram;
    struct zhv_clock clock;
    CPUState *cpu;
};

struct AccelCPUState {
    struct zhv_vcpu *vcpu;
    struct zhv_a64_state native;
    struct zhv_run_input input;
    struct zhv_exit exit;
    uint64_t mmio_exits;
    uint64_t sysreg_exits;
    uint64_t hvc_exits;
    uint64_t wfi_exits;
    uint64_t timer_edges;
    uint64_t el0_exits;
    uint64_t el1_exits;
    uint64_t mmu_exits;
    uint64_t kick_seq;
    int error;
};

int zephyr_arm_get_registers(CPUState *cpu);
int zephyr_arm_put_registers(CPUState *cpu);
int zephyr_arm_handle_exit(CPUState *cpu, const struct zhv_exit *exit);
void zephyr_arm_sync_timer(CPUState *cpu, const struct zhv_timer_sample *timer);
void zephyr_arm_irq_levels(CPUState *cpu, struct zhv_run_input *input);
extern const uint8_t qemu_zephyr_probe_start[], qemu_zephyr_probe_end[];

#endif
