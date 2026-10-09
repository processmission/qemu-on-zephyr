/*
 * Copyright (c) 2026 Chao Liu
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "zephyr-internal.h"
#include "accel/accel-cpu-ops.h"
#include "exec/cpu-common.h"
#include "hw/core/boards.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/guest-random.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/rcu.h"
#include "qemu/timer.h"
#include "system/cpus.h"
#include "system/runstate.h"

static bool zephyr_allowed;
static ZephyrAccelState *zephyr_accel;

bool zephyr_enabled(void)
{
    return zephyr_allowed;
}

static uint64_t host_counter(void)
{
    uint64_t value;

    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(value));
    return value;
}

uint64_t zephyr_counter_frequency(void)
{
    uint64_t frequency;

    if (zephyr_accel != NULL) {
        return zephyr_accel->clock.frequency_hz;
    }
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    return frequency;
}

int64_t zephyr_clock_get_ns(void)
{
    uint64_t ticks, seconds, remainder, frequency;

    if (zephyr_accel == NULL) {
        return 0;
    }
    frequency = zephyr_accel->clock.frequency_hz;
    ticks = host_counter() - zephyr_accel->clock.counter_offset;
    seconds = ticks / frequency;
    remainder = ticks % frequency;
    if (seconds >= INT64_MAX / NANOSECONDS_PER_SECOND) {
        return INT64_MAX;
    }
    /* CNTFRQ is at most UINT32_MAX, so only the bounded remainder is scaled. */
    return seconds * NANOSECONDS_PER_SECOND +
           remainder * NANOSECONDS_PER_SECOND / frequency;
}

int zephyr_get_guest_ram(AccelState *accel, void **ptr, uint64_t *ipa,
                        uint64_t *size, Error **errp)
{
    ZephyrAccelState *s;

    if (accel == NULL || ptr == NULL || ipa == NULL || size == NULL ||
        !object_dynamic_cast(OBJECT(accel), TYPE_ZEPHYR_ACCEL)) {
        error_setg(errp, "zephyr: invalid RAM request");
        return -EINVAL;
    }
    s = ZEPHYR_ACCEL(accel);
    if (s->vm == NULL) {
        error_setg(errp, "zephyr: native VM is not initialized");
        return -EINVAL;
    }
    *ptr = s->ram.host_va;
    *ipa = s->ram.guest_ipa;
    *size = s->ram.size;
    return 0;
}

static int zephyr_machine_init(AccelState *accel, MachineState *machine)
{
    ZephyrAccelState *s = ZEPHYR_ACCEL(accel);
    struct zhv_vm_config config = {
        .ram_ipa = 0x40000000,
        .ram_size = machine->ram_size,
        .rom = zephyr_guest_rom(),
    };
    int ret;

    if (machine->smp.cpus != 1 || machine->smp.max_cpus != 1) {
        error_report("zephyr: one vCPU is supported");
        return -ENOTSUP;
    }
    ret = zhv_vm_create(&config, &s->vm, &s->ram);
    if (ret != 0) {
        error_report("zephyr: native VM creation failed: %d", ret);
        return ret;
    }
    ret = zhv_vm_get_clock(s->vm, &s->clock);
    if (ret != 0 || s->clock.frequency_hz == 0 ||
        s->clock.frequency_hz > UINT32_MAX) {
        error_report("zephyr: unsupported native counter frequency");
        ret = zhv_vm_destroy(s->vm);
        if (ret != 0) {
            error_report("zephyr: failed to release native VM: %d", ret);
        }
        s->vm = NULL;
        return -ENOTSUP;
    }
    zephyr_accel = s;
    return 0;
}

static void zephyr_machine_finalize(Object *object)
{
    ZephyrAccelState *s = ZEPHYR_ACCEL(object);
    int ret;

    if (s->vm != NULL) {
        ret = zhv_vm_destroy(s->vm);
        if (ret != 0) {
            error_report("zephyr: native VM destruction failed: %d", ret);
            abort();
        }
    }
    if (zephyr_accel == s) {
        zephyr_accel = NULL;
    }
}

static void zephyr_create_vcpu_thread(CPUState *cpu)
{
    int ret;

    assert(bql_locked());
    assert(zephyr_accel != NULL && zephyr_accel->cpu == NULL);
    assert(cpu->thread != NULL && cpu->halt_cond != NULL);
    cpu->accel = g_new0(AccelCPUState, 1);
    ret = zhv_vcpu_create(zephyr_accel->vm, &cpu->accel->vcpu);
    if (ret != 0) {
        error_report("zephyr: native vCPU creation failed: %d", ret);
        abort();
    }
    /* Bind the POSIX worker that also drives the single device model. */
    qemu_thread_get_self(cpu->thread);
    cpu->thread_id = qemu_get_thread_id();
    current_cpu = cpu;
    zephyr_accel->cpu = cpu;
    cpu->vcpu_dirty = true;
    qemu_guest_random_seed_thread_part2(cpu->random_seed);
    cpu_thread_signal_created(cpu);
}

static void zephyr_synchronize_state(CPUState *cpu)
{
    int ret;

    assert(bql_locked() && qemu_cpu_is_self(cpu));
    if (cpu->accel != NULL && !cpu->vcpu_dirty) {
        ret = zephyr_arm_get_registers(cpu);
        if (ret != 0) {
            cpu->accel->error = ret;
            error_report("zephyr: read vCPU state failed: %d", ret);
            abort();
        }
        cpu->vcpu_dirty = true;
    }
}

static void zephyr_synchronize_dirty(CPUState *cpu)
{
    assert(bql_locked());
    cpu->vcpu_dirty = true;
}

void zephyr_cpu_kick(CPUState *cpu)
{
    if (cpu != NULL && cpu->accel != NULL) {
        zhv_vcpu_kick(cpu->accel->vcpu);
    }
}

static bool zephyr_cpu_idle(CPUState *cpu)
{
    return cpu->halted && !cpu_has_work(cpu);
}

int zephyr_cpu_last_error(CPUState *cpu)
{
    return cpu->accel != NULL ? cpu->accel->error : -EINVAL;
}

int zephyr_cpu_exec(CPUState *cpu)
{
    AccelCPUState *s = cpu->accel;
    int ret;

    assert(bql_locked() && qemu_cpu_is_self(cpu));
    assert(get_ptr_rcu_reader()->depth == 0);
    if (s == NULL || s->error != 0) {
        return s != NULL ? s->error : -EINVAL;
    }
    if (!cpu_can_run(cpu)) {
        return EXCP_HLT;
    }
    if (cpu->halted && !cpu_has_work(cpu)) {
        return EXCP_HLT;
    }
    cpu->halted = false;
    if (cpu->vcpu_dirty) {
        ret = zephyr_arm_put_registers(cpu);
        if (ret != 0) {
            goto failed;
        }
        cpu->vcpu_dirty = false;
    }
    zephyr_arm_irq_levels(cpu, &s->input);
    bql_unlock();
    cpu_exec_start(cpu);
    ret = zhv_vcpu_run(s->vcpu, &s->input, &s->exit);
    cpu_exec_end(cpu);
    bql_lock();
    if (ret != 0) {
        goto failed;
    }
    s->kick_seq = s->exit.kick_seq;
    ret = zephyr_arm_get_registers(cpu);
    if (ret != 0) {
        goto failed;
    }
    if (s->exit.reason != ZHV_EXIT_KICK && s->exit.reason != ZHV_EXIT_TIMER) {
        if ((s->native.pstate & 15) == 0) {
            s->el0_exits++;
        } else if (((s->native.pstate >> 2) & 3) == 1) {
            s->el1_exits++;
        }
        if (s->native.sctlr_el1 & 1) {
            s->mmu_exits++;
        }
    }
    /* Deassert the old device level before any guest ICC_EOIR/DIR write. */
    zephyr_arm_sync_timer(cpu, &s->exit.vtimer);
    s->input.vtimer_level_seen = s->exit.vtimer.level;
    ret = zephyr_arm_handle_exit(cpu, &s->exit);
    if (ret < 0) {
        goto failed;
    }
    return ret;

failed:
    s->error = ret;
    cpu->stopped = true;
    error_report("zephyr: vCPU stopped: error=%d pc=0x%" PRIx64
                 " esr=0x%" PRIx64 " far=0x%" PRIx64,
                 ret, s->exit.pc, s->exit.esr, s->exit.far);
    return ret;
}

int zephyr_cpu_wait(CPUState *cpu, int64_t max_wait_ns)
{
    AccelCPUState *s = cpu->accel;
    int64_t device_ns;
    uint64_t deadline = UINT64_MAX;
    uint64_t ticks, now, frequency;
    int ret;

    assert(bql_locked() && qemu_cpu_is_self(cpu));
    assert(get_ptr_rcu_reader()->depth == 0);
    if (s == NULL || s->error != 0) {
        return s != NULL ? s->error : -EINVAL;
    }
    if (!cpu_can_run(cpu)) {
        return 0;
    }
    if (!cpu->halted || cpu_has_work(cpu)) {
        cpu->halted = false;
        return 0;
    }
    device_ns = qemu_clock_deadline_ns_all(QEMU_CLOCK_VIRTUAL,
                                         QEMU_TIMER_ATTR_ALL);
    if (max_wait_ns >= 0 && (device_ns < 0 || max_wait_ns < device_ns)) {
        device_ns = max_wait_ns;
    }
    if (device_ns >= 0) {
        frequency = zephyr_counter_frequency();
        /* A one-second spurious wake bounds conversion and polling. */
        device_ns = MIN(device_ns, NANOSECONDS_PER_SECOND);
        ticks = ((uint64_t)device_ns * frequency +
                 NANOSECONDS_PER_SECOND - 1) / NANOSECONDS_PER_SECOND;
        now = host_counter();
        if (ticks <= UINT64_MAX - now) {
            deadline = now + ticks;
        }
    }
    bql_unlock();
    ret = zhv_vcpu_wait(s->vcpu, s->kick_seq, deadline);
    bql_lock();
    /* Reenter run to observe CNTV expiry before the GIC has seen its level. */
    cpu->halted = false;
    return ret;
}

static void zephyr_vcpu_stats(CPUState *cpu, GString *buf)
{
    AccelCPUState *s = cpu->accel;

    if (s != NULL) {
        g_string_append_printf(buf, "MMIO=%" PRIu64 " SYSREG=%" PRIu64
                               " HVC=%" PRIu64 " WFI=%" PRIu64
                               " timer-edges=%" PRIu64 " EL0=%" PRIu64
                               " EL1=%" PRIu64 " MMU-on=%" PRIu64 "\n",
                               s->mmio_exits, s->sysreg_exits, s->hvc_exits,
                               s->wfi_exits, s->timer_edges, s->el0_exits,
                               s->el1_exits, s->mmu_exits);
    }
}

static void zephyr_accel_class_init(ObjectClass *klass, const void *data)
{
    AccelClass *ac = ACCEL_CLASS(klass);

    ac->name = "Zephyr";
    ac->allowed = &zephyr_allowed;
    ac->init_machine = zephyr_machine_init;
}

static void zephyr_accel_ops_class_init(ObjectClass *klass, const void *data)
{
    AccelOpsClass *ops = ACCEL_OPS_CLASS(klass);

    ops->create_vcpu_thread = zephyr_create_vcpu_thread;
    ops->kick_vcpu_thread = zephyr_cpu_kick;
    ops->cpu_thread_is_idle = zephyr_cpu_idle;
    ops->synchronize_state = zephyr_synchronize_state;
    ops->synchronize_post_reset = zephyr_synchronize_dirty;
    ops->synchronize_post_init = zephyr_synchronize_dirty;
    ops->handle_interrupt = generic_handle_interrupt;
    ops->get_virtual_clock = zephyr_clock_get_ns;
    ops->get_elapsed_ticks = zephyr_clock_get_ns;
    ops->get_vcpu_stats = zephyr_vcpu_stats;
}

static const TypeInfo zephyr_types[] = {
    {
        .name = TYPE_ZEPHYR_ACCEL,
        .parent = TYPE_ACCEL,
        .instance_size = sizeof(ZephyrAccelState),
        .instance_finalize = zephyr_machine_finalize,
        .class_init = zephyr_accel_class_init,
    }, {
        .name = ACCEL_OPS_NAME("zephyr"),
        .parent = TYPE_ACCEL_OPS,
        .class_init = zephyr_accel_ops_class_init,
        .abstract = true,
    },
};

DEFINE_TYPES(zephyr_types)
