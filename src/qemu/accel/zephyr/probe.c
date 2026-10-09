/*
 * Copyright (c) 2026 Chao Liu
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "zephyr-internal.h"
#include "target/arm/cpu.h"
#include "target/arm/internals.h"
#include "exec/cpu-common.h"
#include "hw/char/pl011.h"
#include "qapi/error.h"
#include "qemu/bswap.h"
#include "qemu/main-loop.h"
#include "qemu/rcu.h"
#include "system/address-spaces.h"
#include "system/cpus.h"
#include "system/hw_accel.h"
#include "system/memory.h"

int zephyr_accel_probe(CPUState *cpu, Error **errp)
{
    ARMCPU *arm = ARM_CPU(cpu);
    AccelCPUState *s = cpu->accel;
    AddressSpace *as = cpu_get_address_space(cpu, ARMASIdx_NS);
    MemoryRegion *uart;
    hwaddr translated, length = 4;
    uint64_t ipa, size, first, second, before_mmio;
    void *ram;
    uint8_t counter_data[16];
    size_t code_size = qemu_zephyr_probe_end - qemu_zephyr_probe_start;
    MemTxResult transaction;
    int ret;

    assert(bql_locked());
    ret = zephyr_get_guest_ram(current_accel(), &ram, &ipa, &size, errp);
    if (ret != 0) {
        return ret;
    }
    rcu_read_lock();
    uart = address_space_translate(as, 0x09000000, &translated, &length,
                                   true, MEMTXATTRS_UNSPECIFIED);
    if (uart == NULL || memory_region_owner(uart) == NULL ||
        object_dynamic_cast(memory_region_owner(uart), TYPE_PL011) == NULL) {
        rcu_read_unlock();
        error_setg(errp, "zephyr probe requires QEMU PL011 at 0x09000000");
        return -EINVAL;
    }
    transaction = address_space_write(as, ipa, MEMTXATTRS_UNSPECIFIED,
                                      qemu_zephyr_probe_start, code_size);
    rcu_read_unlock();
    if (transaction != MEMTX_OK) {
        error_setg(errp, "zephyr probe could not populate guest RAM");
        return -EIO;
    }
    cpu_synchronize_state(cpu);
    arm->env.pc = ipa;
    pstate_write(&arm->env, 0x3c5);
    arm->env.sp_el[1] = ipa + size - 16;
    arm->env.xregs[19] = ipa + 0x1000;
    aarch64_restore_sp(&arm->env, 1);
    cpu_synchronize_post_init(cpu);
    before_mmio = s->mmio_exits;
    for (unsigned i = 0; i < 1000; i++) {
        ret = zephyr_cpu_exec(cpu);
        if (ret < 0) {
            error_setg(errp, "zephyr probe execution failed: %d", ret);
            return ret;
        }
        if (ret == EXCP_HLT) {
            break;
        }
    }
    if (ret != EXCP_HLT || s->mmio_exits - before_mmio != 5) {
        error_setg(errp, "zephyr probe did not complete five PL011 writes:"
                   " ret=%d writes=%" PRIu64 " exit=%u pc=0x%" PRIx64,
                   ret, s->mmio_exits - before_mmio, s->exit.reason,
                   s->exit.pc);
        return -EIO;
    }
    rcu_read_lock();
    transaction = address_space_read(as, ipa + 0x1000, MEMTXATTRS_UNSPECIFIED,
                                     counter_data, sizeof(counter_data));
    rcu_read_unlock();
    if (transaction != MEMTX_OK) {
        error_setg(errp, "zephyr probe counter result is inaccessible");
        return -EIO;
    }
    first = ldq_le_p(counter_data);
    second = ldq_le_p(counter_data + 8);
    if (second < first || second - first > zephyr_counter_frequency()) {
        error_setg(errp, "guest counter origins differ: CNTPCT=%" PRIu64
                   " CNTVCT=%" PRIu64, first, second);
        return -EIO;
    }
    printf("zephyr accel probe: ARMCPU, five PL011 MMIO exits, WFI, "
           "CNTPCT/CNTVCT same origin (freq=%" PRIu64 " delta=%" PRIu64 ")\n",
           zephyr_counter_frequency(), second - first);
    return 0;
}
