/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_ARCH_ARM64_HYPERVISOR_CONTEXT_H_
#define ZEPHYR_ARCH_ARM64_HYPERVISOR_CONTEXT_H_

/* The shared list also generates all C/assembly offsets. */
#define ZHV_SHARED_REGS(_) \
	_(sp_el0) _(sp_el1) _(elr_el1) _(spsr_el1) \
	_(sctlr_el1) _(tcr_el1) _(ttbr0_el1) _(ttbr1_el1) \
	_(mair_el1) _(amair_el1) _(vbar_el1) _(contextidr_el1) \
	_(tpidr_el0) _(tpidrro_el0) _(tpidr_el1) _(par_el1) _(csselr_el1) \
	_(cpacr_el1) _(esr_el1) _(far_el1) _(afsr0_el1) _(afsr1_el1) \
	_(cntkctl_el1)

#define ZHV_HOST_EL2_REGS(_) \
	_(hcr_el2) _(cptr_el2) _(mdcr_el2) _(cnthctl_el2) _(cntvoff_el2) \
	_(vtcr_el2) _(vttbr_el2) _(vbar_el2) _(vpidr_el2) _(vmpidr_el2) \
	_(tpidr_el2) _(elr_el2) _(spsr_el2) _(icc_sre_el2) _(ich_hcr_el2)

#ifndef _ASMLANGUAGE
#include <zephyr/virtualization/zhv.h>

struct zhv_switch_context {
	struct zhv_a64_state guest;
	struct zhv_a64_state host;
#define ZHV_EL2_FIELD(name) uint64_t host_##name;
	ZHV_HOST_EL2_REGS(ZHV_EL2_FIELD)
#undef ZHV_EL2_FIELD
	uint64_t host_sp, host_daif;
	uint64_t guest_hcr, guest_vtcr, guest_vttbr, guest_offset;
	uint64_t esr, far, hpfar, vector, irq_id;
};

void zhv_enter_asm(struct zhv_switch_context *context);
#endif

#endif /* ZEPHYR_ARCH_ARM64_HYPERVISOR_CONTEXT_H_ */
