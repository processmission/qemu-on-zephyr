/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/accel.h"
#include "qemu/main-loop.h"
#include "qemu/guest-random.h"
#include "qemu/timer.h"
#include "qemu/rcu.h"
#include "accel/accel-cpu-ops.h"
#include "accel/tcg/cpu-loop.h"
#include "exec/cputlb.h"
#include "exec/translation-block.h"
#include "exec/tb-flush.h"
#include "system/cpus.h"
#include "system/tcg.h"
#include "cpu.h"
#include "internals.h"
#include "tcg/startup.h"
#include "accelerator.h"

/* Use only Zephyr's numeric CONFIG macro rules in kernel calls below. */
#undef IS_ENABLED
#define IS_ENABLED(config_macro) Z_IS_ENABLED1(config_macro)
#undef IS_EMPTY
#define IS_EMPTY(...) Z_IS_EMPTY_(__VA_ARGS__)

static struct k_sem wake;
static CPUState *vcpu;
static uint64_t runs;
static uint64_t el0_returns;
static uint64_t mmu_returns;
static int64_t clock_origin;
static uint8_t code_buffer[CONFIG_QEMU_TCG_CACHE_SIZE_MIB * 1024 * 1024]
    __attribute__((aligned(CONFIG_MMU_PAGE_SIZE)));

void *qemu_zephyr_code_buffer(size_t size, void **rx)
{
    assert(size <= sizeof(code_buffer));
    k_mem_map_phys_bare((uint8_t **)rx, k_mem_phys_addr(code_buffer),
                       size, K_MEM_CACHE_WB | K_MEM_PERM_EXEC);
    assert(*rx != NULL);
    printf("QEMU_TCG_JIT RW=%p RX=%p size=%zu\n", code_buffer, *rx, size);
    return code_buffer;
}

void qemu_zephyr_cpu_kick(CPUState *cpu)
{
    /* IRQ-safe: do not enter QEMU's pthread-based wakeup path here. */
    qatomic_store_release(&cpu->exit_request, true);
    qatomic_set(&cpu->neg.icount_decr.u16.high, -1);
    if (qatomic_read(&vcpu) != NULL) {
        k_sem_give(&wake);
    }
}

static void timeslice(struct k_timer *timer)
{
    if (vcpu != NULL) {
        qemu_zephyr_cpu_kick(vcpu);
    }
}

K_TIMER_DEFINE(tcg_slice, timeslice, NULL);

static int64_t virtual_clock(void)
{
    return k_cyc_to_ns_floor64(k_cycle_get_64()) - clock_origin;
}

static void create_vcpu(CPUState *cpu)
{
    assert(vcpu == NULL);
    k_sem_init(&wake, 0, 1);
    qatomic_set(&vcpu, cpu);
    qemu_thread_get_self(cpu->thread);
    cpu->thread_id = qemu_get_thread_id();
    current_cpu = cpu;
    tcg_register_thread();
    tcg_cflags_set(cpu, cpu->cluster_index << CF_CLUSTER_SHIFT);
    qemu_guest_random_seed_thread_part2(cpu->random_seed);
    clock_origin = k_cyc_to_ns_floor64(k_cycle_get_64());
    cpu_thread_signal_created(cpu);
    k_timer_start(&tcg_slice, K_MSEC(1), K_MSEC(1));
}

int qemu_zephyr_cpu_exec(CPUState *cpu)
{
    int result;

    assert(bql_locked() && qemu_cpu_is_self(cpu));
    bql_unlock();
    cpu_exec_start(cpu);
    result = cpu_exec(cpu);
    cpu_exec_end(cpu);
    /* cpu_exec reports exit_request but leaves its acknowledgement to RR. */
    qatomic_set(&cpu->exit_request, false);
    bql_lock();
    if (result == EXCP_HALTED) {
        return EXCP_HLT;
    }
    runs++;
    el0_returns += arm_current_el(&ARM_CPU(cpu)->env) == 0;
    mmu_returns += (ARM_CPU(cpu)->env.cp15.sctlr_el[1] & 1) != 0;
    return result;
}

void qemu_zephyr_cpu_stop(void)
{
    k_timer_stop(&tcg_slice);
}

int qemu_zephyr_cpu_wait(CPUState *cpu, int64_t deadline_ns)
{
    bql_unlock();
    /* The latched kick and one-millisecond slice bound virtual timer latency. */
    k_sem_take(&wake, K_MSEC(1));
    bql_lock();
    return 0;
}

static void reset_cpu(CPUState *cpu)
{
    tcg_flush_jmp_cache(cpu);
    tlb_flush(cpu);
}

static void interrupt_cpu(CPUState *cpu, int mask)
{
    cpu_set_interrupt(cpu, mask);
    qemu_zephyr_cpu_kick(cpu);
}

static void stats(CPUState *cpu, GString *buf)
{
    g_string_append_printf(buf, "TCG-runs=%" PRIu64 " EL0=%" PRIu64
                           " MMU-on=%" PRIu64, runs, el0_returns, mmu_returns);
}

static void class_init(ObjectClass *object_class, const void *data)
{
    AccelOpsClass *ops = ACCEL_OPS_CLASS(object_class);

    ops->create_vcpu_thread = create_vcpu;
    ops->kick_vcpu_thread = qemu_zephyr_cpu_kick;
    ops->handle_interrupt = interrupt_cpu;
    ops->cpu_reset_hold = reset_cpu;
    ops->get_virtual_clock = virtual_clock;
    ops->get_elapsed_ticks = virtual_clock;
    ops->get_vcpu_stats = stats;
}

static const TypeInfo tcg_ops = {
    .name = ACCEL_OPS_NAME("tcg"),
    .parent = TYPE_ACCEL_OPS,
    .class_init = class_init,
    .abstract = true,
};

static void register_types(void)
{
    type_register_static(&tcg_ops);
}

type_init(register_types)
