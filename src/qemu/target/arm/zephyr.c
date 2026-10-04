/*
 * Copyright (c) 2026 Chao Liu
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "cpregs.h"
#include "kvm-consts.h"
#include "multiprocessing.h"
#include "accel/accel-cpu-target.h"
#include "accel/zephyr/zephyr-internal.h"
#include "exec/cpu-common.h"
#include "hw/core/irq.h"
#include "qapi/error.h"
#include "qemu/bswap.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/rcu.h"
#include "system/address-spaces.h"
#include "system/cpus.h"
#include "system/runstate.h"

/* Guest EL2/EL3 are absent. These are the non-secure EL1 register banks. */
#define ZEPHYR_ENV_REGS(_) \
    _(sp_el0, sp_el[0]) \
    _(sp_el1, sp_el[1]) \
    _(elr_el1, elr_el[1]) \
    _(spsr_el1, banked_spsr[BANK_SVC]) \
    _(sctlr_el1, cp15.sctlr_el[1]) \
    _(tcr_el1, cp15.tcr_el[1]) \
    _(ttbr0_el1, cp15.ttbr0_el[1]) \
    _(ttbr1_el1, cp15.ttbr1_el[1]) \
    _(mair_el1, cp15.mair_el[1]) \
    _(vbar_el1, cp15.vbar_el[1]) \
    _(contextidr_el1, cp15.contextidr_el[1]) \
    _(tpidr_el0, cp15.tpidr_el[0]) \
    _(tpidrro_el0, cp15.tpidrro_el[0]) \
    _(tpidr_el1, cp15.tpidr_el[1]) \
    _(par_el1, cp15.par_el[1]) \
    _(csselr_el1, cp15.csselr_el[1]) \
    _(cpacr_el1, cp15.cpacr_el1) \
    _(esr_el1, cp15.esr_el[1]) \
    _(far_el1, cp15.far_el[1]) \
    _(cntkctl_el1, cp15.c14_cntkctl) \
    _(cntv_cval_el0, cp15.c14_timer[GTIMER_VIRT].cval) \
    _(cntv_ctl_el0, cp15.c14_timer[GTIMER_VIRT].ctl)

int zephyr_arm_get_registers(CPUState *cpu)
{
    AccelCPUState *s = cpu->accel;
    CPUARMState *env = &ARM_CPU(cpu)->env;
    struct zhv_a64_state *native = &s->native;
    int ret = zhv_vcpu_get_state(s->vcpu, native);

    if (ret != 0) {
        return ret;
    }
    memcpy(env->xregs, native->x, sizeof(native->x));
    env->pc = native->pc;
    pstate_write(env, native->pstate);
#define GET_ENV(native_reg, model_reg) env->model_reg = native->native_reg;
    ZEPHYR_ENV_REGS(GET_ENV)
#undef GET_ENV
    aarch64_restore_sp(env, arm_current_el(env));
    for (unsigned i = 0; i < 32; i++) {
        memcpy(aa64_vfp_qreg(env, i), native->q[i], sizeof(native->q[i]));
    }
    vfp_set_fpcr(env, native->fpcr);
    vfp_set_fpsr(env, native->fpsr);
    /* AMAIR/AFSR are RAZ/WI in this QEMU model; retain their native shadow. */
    return 0;
}

int zephyr_arm_put_registers(CPUState *cpu)
{
    AccelCPUState *s = cpu->accel;
    CPUARMState *env = &ARM_CPU(cpu)->env;
    struct zhv_a64_state *native = &s->native;

    if (!env->aarch64 || arm_current_el(env) > 1) {
        error_report("zephyr: only AArch64 EL0/EL1 execution is supported");
        return -ENOTSUP;
    }
    aarch64_save_sp(env, arm_current_el(env));
    memcpy(native->x, env->xregs, sizeof(native->x));
    native->pc = env->pc;
    native->pstate = pstate_read(env);
#define PUT_ENV(native_reg, model_reg) native->native_reg = env->model_reg;
    ZEPHYR_ENV_REGS(PUT_ENV)
#undef PUT_ENV
    for (unsigned i = 0; i < 32; i++) {
        memcpy(native->q[i], aa64_vfp_qreg(env, i), sizeof(native->q[i]));
    }
    native->fpcr = vfp_get_fpcr(env);
    native->fpsr = vfp_get_fpsr(env);
    return zhv_vcpu_set_state(s->vcpu, native);
}

void zephyr_arm_irq_levels(CPUState *cpu, struct zhv_run_input *input)
{
    input->irq = cpu_test_interrupt(cpu, CPU_INTERRUPT_HARD);
    input->fiq = cpu_test_interrupt(cpu, CPU_INTERRUPT_FIQ);
}

void zephyr_arm_sync_timer(CPUState *cpu, const struct zhv_timer_sample *timer)
{
    ARMCPU *arm = ARM_CPU(cpu);
    AccelCPUState *s = cpu->accel;

    if (s->input.vtimer_level_seen != timer->level) {
        s->timer_edges++;
    }
    arm->env.cp15.c14_timer[GTIMER_VIRT].ctl = timer->ctl;
    arm->env.cp15.c14_timer[GTIMER_VIRT].cval = timer->cval;
    qemu_set_irq(arm->gt_timer_outputs[GTIMER_VIRT], timer->level);
}

static bool allowed_cpreg(const ARMCPRegInfo *ri, bool read)
{
    bool identification = ri->opc0 == 3 && ri->opc1 == 0 &&
                          ri->crn == 0 && ri->crm < 8;
    bool cache_setway = ri->opc0 == 1 && ri->opc1 == 0 && ri->crn == 7 &&
                        ri->opc2 == 2 &&
                        (ri->crm == 6 || ri->crm == 10 || ri->crm == 14);

    if (identification && read) {
        return true;
    }
    if (cache_setway && !read) {
        return true;
    }
    return g_str_has_prefix(ri->name, "ICC_") ||
           g_str_has_prefix(ri->name, "CNTP_") ||
           strcmp(ri->name, "CNTPCT_EL0") == 0 ||
           strcmp(ri->name, "ACTLR_EL1") == 0 ||
           strcmp(ri->name, "L2CTLR_EL1") == 0 ||
           strcmp(ri->name, "L2ECTLR_EL1") == 0 ||
           strcmp(ri->name, "OSLAR_EL1") == 0 ||
           strcmp(ri->name, "OSLSR_EL1") == 0 ||
           strcmp(ri->name, "OSDLR_EL1") == 0 ||
           strcmp(ri->name, "MDCCINT_EL1") == 0 ||
           strcmp(ri->name, "MDSCR_EL1") == 0 ||
           g_str_has_prefix(ri->name, "DBGBVR") ||
           g_str_has_prefix(ri->name, "DBGBCR") ||
           g_str_has_prefix(ri->name, "DBGWVR") ||
           g_str_has_prefix(ri->name, "DBGWCR");
}

static int emulate_cpreg(CPUState *cpu, const struct zhv_exit *exit,
                        struct zhv_completion *completion)
{
    ARMCPU *arm = ARM_CPU(cpu);
    CPUARMState *env = &arm->env;
    uint16_t reg = exit->u.sysreg.encoding;
    bool read = exit->u.sysreg.read;
    const ARMCPRegInfo *ri = get_arm_cp_reginfo(arm->cp_regs,
        ENCODE_AA64_CP_REG((reg >> 14) & 3, (reg >> 11) & 7,
                          (reg >> 7) & 15, (reg >> 3) & 15, reg & 7));

    if (ri == NULL || !allowed_cpreg(ri, read) ||
        !cp_access_ok(arm_current_el(env), ri, read) ||
        (ri->accessfn != NULL &&
         ri->accessfn(env, ri, read) != CP_ACCESS_OK) ||
        (ri->type & ARM_CP_RAISES_EXC) != 0) {
        error_report("zephyr: unsupported %s sysreg %s (0x%04x)"
                     " at pc=0x%" PRIx64 " value=0x%" PRIx64,
                     read ? "read" : "write", ri != NULL ? ri->name : "unknown",
                     reg, exit->pc, exit->u.sysreg.value);
        return -ENOTSUP;
    }
    /* Preserve QEMU's documented cache NOP and constant-register semantics. */
    if ((ri->type & ARM_CP_SPECIAL_MASK) == ARM_CP_NOP) {
        if (read) {
            return -ENOTSUP;
        }
        return 0;
    }
    if (read) {
        completion->nr_values = 1;
        if (ri->type & ARM_CP_CONST) {
            completion->value[0] = ri->resetvalue;
        } else if (ri->readfn != NULL) {
            completion->value[0] = ri->readfn(env, ri);
        } else if (ri->fieldoffset != 0) {
            completion->value[0] = raw_read(env, ri);
        } else {
            return -ENOTSUP;
        }
    } else if ((ri->type & ARM_CP_CONST) == 0) {
        if (ri->writefn != NULL) {
            ri->writefn(env, ri, exit->u.sysreg.value);
        } else if (ri->fieldoffset != 0) {
            raw_write(env, ri, exit->u.sysreg.value);
        } else {
            return -ENOTSUP;
        }
    }
    return 0;
}

static int emulate_mmio(CPUState *cpu, const struct zhv_exit *exit,
                        struct zhv_completion *completion)
{
    AddressSpace *as = cpu_get_address_space(cpu, ARMASIdx_NS);
    uint8_t data[8] = { 0 };
    MemTxResult result;

    if (exit->u.mmio.write) {
        stq_le_p(data, exit->u.mmio.value);
    }
    rcu_read_lock();
    result = address_space_rw(as, exit->u.mmio.ipa, MEMTXATTRS_UNSPECIFIED,
                              data, exit->u.mmio.size, exit->u.mmio.write);
    rcu_read_unlock();
    if (result != MEMTX_OK) {
        error_report("zephyr: MMIO %s failed at 0x%" PRIx64 " size=%u",
                     exit->u.mmio.write ? "write" : "read", exit->u.mmio.ipa,
                     exit->u.mmio.size);
        return -EIO;
    }
    if (!exit->u.mmio.write) {
        completion->nr_values = 1;
        completion->value[0] = ldq_le_p(data);
    }
    return 0;
}

static bool psci_function_supported(uint32_t function)
{
    switch (function) {
    case QEMU_PSCI_0_2_FN_PSCI_VERSION:
    case QEMU_PSCI_0_2_FN_CPU_SUSPEND:
    case QEMU_PSCI_0_2_FN64_CPU_SUSPEND:
    case QEMU_PSCI_0_2_FN_CPU_OFF:
    case QEMU_PSCI_0_2_FN_CPU_ON:
    case QEMU_PSCI_0_2_FN64_CPU_ON:
    case QEMU_PSCI_0_2_FN_AFFINITY_INFO:
    case QEMU_PSCI_0_2_FN64_AFFINITY_INFO:
    case QEMU_PSCI_0_2_FN_MIGRATE_INFO_TYPE:
    case QEMU_PSCI_0_2_FN_SYSTEM_OFF:
    case QEMU_PSCI_0_2_FN_SYSTEM_RESET:
    case QEMU_PSCI_1_0_FN_PSCI_FEATURES:
    case 0x80000000: /* SMCCC_VERSION */
        return true;
    default:
        return false;
    }
}

static int emulate_firmware_call(CPUState *cpu, const struct zhv_exit *exit,
                                 struct zhv_completion *completion)
{
    ARMCPU *arm = ARM_CPU(cpu);
    uint32_t function = exit->u.call.x[0];
    uint64_t arg1 = exit->u.call.x[1];
    uint64_t arg2 = exit->u.call.x[2];
    int64_t result = QEMU_PSCI_RET_NOT_SUPPORTED;
    int ret = EXCP_INTERRUPT;

    completion->nr_values = 4;
    memcpy(completion->value, exit->u.call.x, sizeof(completion->value));
    switch (function) {
    case QEMU_PSCI_0_2_FN_PSCI_VERSION:
        result = QEMU_PSCI_VERSION_1_1;
        break;
    case 0x80000000: /* SMCCC_VERSION */
        result = 0x10001;
        break;
    case 0x80000001: /* SMCCC_ARCH_FEATURES: no optional services. */
        break;
    case QEMU_PSCI_1_0_FN_PSCI_FEATURES:
        result = psci_function_supported(arg1) ? QEMU_PSCI_RET_SUCCESS :
                                                QEMU_PSCI_RET_NOT_SUPPORTED;
        break;
    case QEMU_PSCI_0_2_FN_MIGRATE_INFO_TYPE:
        result = QEMU_PSCI_0_2_RET_TOS_MIGRATION_NOT_REQUIRED;
        break;
    case QEMU_PSCI_0_2_FN_AFFINITY_INFO:
    case QEMU_PSCI_0_2_FN64_AFFINITY_INFO:
        result = arg2 <= 3 && arg1 == arm_cpu_mp_affinity(arm) ?
                 arm->power_state : QEMU_PSCI_RET_INVALID_PARAMS;
        break;
    case QEMU_PSCI_0_2_FN_CPU_ON:
    case QEMU_PSCI_0_2_FN64_CPU_ON:
        result = arg1 == arm_cpu_mp_affinity(arm) ? QEMU_PSCI_RET_ALREADY_ON :
                                                  QEMU_PSCI_RET_INVALID_PARAMS;
        break;
    case QEMU_PSCI_0_2_FN_CPU_SUSPEND:
    case QEMU_PSCI_0_2_FN64_CPU_SUSPEND:
        /* Standby only: no advertised powerdown state or resume trampoline. */
        if (arg1 == 0) {
            result = QEMU_PSCI_RET_SUCCESS;
            cpu->halted = !cpu_has_work(cpu);
            ret = cpu->halted ? EXCP_HLT : EXCP_INTERRUPT;
        }
        break;
    case QEMU_PSCI_0_2_FN_SYSTEM_RESET:
    case QEMU_PSCI_0_2_FN_SYSTEM_OFF:
    case QEMU_PSCI_0_2_FN_CPU_OFF:
        if (function == QEMU_PSCI_0_2_FN_SYSTEM_RESET) {
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        } else if (function == QEMU_PSCI_0_2_FN_SYSTEM_OFF) {
            qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
        }
        arm->power_state = PSCI_OFF;
        cpu->halted = true;
        cpu->stopped = true;
        result = QEMU_PSCI_RET_SUCCESS;
        ret = EXCP_HLT;
        break;
    default:
        /* SMCCC requires unknown functions to return NOT_SUPPORTED. */
        break;
    }
    completion->value[0] = result;
    return ret;
}

int zephyr_arm_handle_exit(CPUState *cpu, const struct zhv_exit *exit)
{
    AccelCPUState *s = cpu->accel;
    struct zhv_completion completion = { .exit_seq = exit->seq };
    int result = EXCP_INTERRUPT;
    int ret = 0;

    switch (exit->reason) {
    case ZHV_EXIT_MMIO:
        s->mmio_exits++;
        ret = emulate_mmio(cpu, exit, &completion);
        break;
    case ZHV_EXIT_SYSREG:
        s->sysreg_exits++;
        ret = emulate_cpreg(cpu, exit, &completion);
        break;
    case ZHV_EXIT_HVC:
    case ZHV_EXIT_SMC:
        s->hvc_exits++;
        result = emulate_firmware_call(cpu, exit, &completion);
        break;
    case ZHV_EXIT_WFI:
        s->wfi_exits++;
        cpu->halted = !cpu_has_work(cpu);
        result = cpu->halted ? EXCP_HLT : EXCP_INTERRUPT;
        break;
    case ZHV_EXIT_WFE:
        break;
    case ZHV_EXIT_HOST_IRQ:
    case ZHV_EXIT_TIMER:
    case ZHV_EXIT_KICK:
        return EXCP_INTERRUPT;
    case ZHV_EXIT_FAIL:
    default:
        error_report("zephyr: native guest failure, vector=%u pc=0x%" PRIx64
                     " esr=0x%" PRIx64 " hpfar=0x%" PRIx64,
                     exit->u.fail.category, exit->pc, exit->esr, exit->hpfar);
        return -EIO;
    }
    if (ret != 0) {
        return ret;
    }
    ret = zhv_vcpu_complete(s->vcpu, &completion);
    if (ret != 0) {
        return ret;
    }
    ret = zephyr_arm_get_registers(cpu);
    if (ret != 0) {
        return ret;
    }
    cpu->vcpu_dirty = false;
    return result;
}

static void zephyr_arm_cpu_instance_init(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);

    cpu->has_el2 = false;
    cpu->has_el3 = false;
    cpu->has_pmu = false;
    cpu->gt_cntfrq_hz = zephyr_counter_frequency();
    cpu->psci_conduit = QEMU_PSCI_CONDUIT_HVC;
    cpu->psci_version = QEMU_PSCI_VERSION_1_1;
    /* The executor supports AArch64 EL0/EL1, including Linux userspace. */
    FIELD_DP64_IDREG(&cpu->isar, ID_AA64PFR0, EL0, 1);
    FIELD_DP64_IDREG(&cpu->isar, ID_AA64PFR0, EL1, 1);
}

static bool zephyr_arm_cpu_realize(CPUState *cs, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(cs);
    const char *type = object_get_typename(OBJECT(cpu));
    uint64_t host_midr;
    bool supported = strcmp(type, ARM_CPU_TYPE_NAME("cortex-a53")) == 0 ||
                     strcmp(type, ARM_CPU_TYPE_NAME("cortex-a57")) == 0 ||
                     strcmp(type, ARM_CPU_TYPE_NAME("cortex-a72")) == 0;

    __asm__ volatile("mrs %0, midr_el1" : "=r"(host_midr));
    /* Match implementer, architecture and part; revisions may differ. */
    if (!supported || (host_midr & 0xff0ffff0) != (cpu->midr & 0xff0ffff0) ||
        cpu->has_el2 || cpu->has_el3 || cpu->has_pmu || cpu->cfgend ||
        cpu->gt_cntfrq_hz != zephyr_counter_frequency() ||
        cpu->psci_conduit != QEMU_PSCI_CONDUIT_HVC) {
        error_setg(errp, "zephyr requires a Cortex-A53/A57/A72 matching the host"
                   " MIDR (0x%" PRIx64 "), EL1 without EL2/EL3/PMU, little-endian,"
                   " native CNTFRQ and PSCI over HVC", host_midr);
        return false;
    }
    return true;
}

static void zephyr_arm_accel_cpu_class_init(ObjectClass *oc, const void *data)
{
    AccelCPUClass *acc = ACCEL_CPU_CLASS(oc);

    acc->cpu_instance_init = zephyr_arm_cpu_instance_init;
    acc->cpu_target_realize = zephyr_arm_cpu_realize;
}

static const TypeInfo zephyr_arm_accel_cpu = {
    .name = ACCEL_CPU_NAME("zephyr"),
    .parent = TYPE_ACCEL_CPU,
    .class_init = zephyr_arm_accel_cpu_class_init,
    .abstract = true,
};

static void zephyr_arm_register_types(void)
{
    type_register_static(&zephyr_arm_accel_cpu);
}

type_init(zephyr_arm_register_types)
