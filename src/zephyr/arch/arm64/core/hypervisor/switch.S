/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>
#include <zephyr/zhv_offsets.h>
#include "context.h"

_ASM_FILE_PROLOGUE

/* x0 is the trusted context; x3 is the selected architectural state. */
.macro save_shared
#define SAVE_SHARED(name) mrs x2, name; str x2, [x3, #__zhv_a64_state_##name##_OFFSET];
	ZHV_SHARED_REGS(SAVE_SHARED)
#undef SAVE_SHARED
	mrs	x2, cntv_cval_el0
	str	x2, [x3, #__zhv_a64_state_cntv_cval_el0_OFFSET]
	mrs	x2, cntv_ctl_el0
	str	x2, [x3, #__zhv_a64_state_cntv_ctl_el0_OFFSET]
.endm

.macro restore_shared
#define RESTORE_SHARED(name) ldr x2, [x3, #__zhv_a64_state_##name##_OFFSET]; msr name, x2;
	ZHV_SHARED_REGS(RESTORE_SHARED)
#undef RESTORE_SHARED
.endm

.macro save_fp
	add	x4, x3, #__zhv_a64_state_q_OFFSET
	.irp reg, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, \
		  16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
	str	q\reg, [x4, #(16 * \reg)]
	.endr
	mrs	x2, fpcr
	str	w2, [x3, #__zhv_a64_state_fpcr_OFFSET]
	mrs	x2, fpsr
	str	w2, [x3, #__zhv_a64_state_fpsr_OFFSET]
.endm

.macro restore_fp
	add	x4, x3, #__zhv_a64_state_q_OFFSET
	.irp reg, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, \
		  16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
	ldr	q\reg, [x4, #(16 * \reg)]
	.endr
	ldr	w2, [x3, #__zhv_a64_state_fpcr_OFFSET]
	msr	fpcr, x2
	ldr	w2, [x3, #__zhv_a64_state_fpsr_OFFSET]
	msr	fpsr, x2
.endm

GTEXT(zhv_enter_asm)
SECTION_FUNC(TEXT, zhv_enter_asm)
	mrs	x1, daif
	str	x1, [x0, #__zhv_switch_context_host_daif_OFFSET]
	msr	daifset, #15
	mov	x1, sp
	str	x1, [x0, #__zhv_switch_context_host_sp_OFFSET]
	add	x3, x0, #__zhv_switch_context_host_OFFSET
	stp	x19, x20, [x3, #(19 * 8)]
	stp	x21, x22, [x3, #(21 * 8)]
	stp	x23, x24, [x3, #(23 * 8)]
	stp	x25, x26, [x3, #(25 * 8)]
	stp	x27, x28, [x3, #(27 * 8)]
	stp	x29, x30, [x3, #(29 * 8)]
	save_shared
#define SAVE_HOST(name) mrs x2, name; str x2, [x0, #__zhv_switch_context_host_##name##_OFFSET];
	ZHV_HOST_EL2_REGS(SAVE_HOST)
#undef SAVE_HOST

	/* Every subsequent exception can recover this pointer without guest TLS. */
	msr	tpidr_el2, x0
	adr	x2, zhv_vectors
	msr	vbar_el2, x2
	isb

	ldr	x2, [x0, #__zhv_switch_context_guest_vtcr_OFFSET]
	msr	vtcr_el2, x2
	ldr	x2, [x0, #__zhv_switch_context_guest_vttbr_OFFSET]
	msr	vttbr_el2, x2
	dsb	ishst
	isb
	ldr	x2, [x0, #__zhv_switch_context_guest_hcr_OFFSET]
	msr	hcr_el2, x2
	isb

	/* Trap GIC CPU interface access without enabling a hardware virtual GIC. */
	mov	x2, #0x5c00
	msr	ich_hcr_el2, x2
	mrs	x2, icc_sre_el2
	bic	x2, x2, #8
	msr	icc_sre_el2, x2
	/* Debug OS/register and PMU access belongs to the external model. */
	mov	x2, #0x660
	msr	mdcr_el2, x2
	/* Keep emulated CNTP and CNTPCT on the device model's clock origin. */
	msr	cnthctl_el2, xzr
	ldr	x2, [x0, #__zhv_switch_context_guest_offset_OFFSET]
	msr	cntvoff_el2, x2
	mrs	x2, midr_el1
	msr	vpidr_el2, x2
	mov	x2, #0x80000000
	msr	vmpidr_el2, x2
	isb

	add	x3, x0, #__zhv_switch_context_guest_OFFSET
	/* No C/helper is called after shared guest TLS is installed. */
	restore_shared
	ldr	x2, [x3, #__zhv_a64_state_cntv_cval_el0_OFFSET]
	msr	cntv_cval_el0, x2
	isb
	ldr	x2, [x3, #__zhv_a64_state_cntv_ctl_el0_OFFSET]
	msr	cntv_ctl_el0, x2
	mrs	x2, cptr_el2
	bic	x2, x2, #0x400
	msr	cptr_el2, x2
	isb
	restore_fp
	ldr	x2, [x3, #__zhv_a64_state_pc_OFFSET]
	msr	elr_el2, x2
	ldr	x2, [x3, #__zhv_a64_state_pstate_OFFSET]
	msr	spsr_el2, x2
	clrex
	ldp	x4, x5, [x3, #(4 * 8)]
	ldp	x6, x7, [x3, #(6 * 8)]
	ldp	x8, x9, [x3, #(8 * 8)]
	ldp	x10, x11, [x3, #(10 * 8)]
	ldp	x12, x13, [x3, #(12 * 8)]
	ldp	x14, x15, [x3, #(14 * 8)]
	ldp	x16, x17, [x3, #(16 * 8)]
	ldp	x18, x19, [x3, #(18 * 8)]
	ldp	x20, x21, [x3, #(20 * 8)]
	ldp	x22, x23, [x3, #(22 * 8)]
	ldp	x24, x25, [x3, #(24 * 8)]
	ldp	x26, x27, [x3, #(26 * 8)]
	ldp	x28, x29, [x3, #(28 * 8)]
	ldr	x30, [x3, #(30 * 8)]
	ldp	x0, x1, [x3]
	ldp	x2, x3, [x3, #(2 * 8)]
	eret

.macro guest_vector number
	.balign 128
	stp	x0, x1, [sp, #-16]!
	mrs	x0, tpidr_el2
	mov	x1, #\number
	str	x1, [x0, #__zhv_switch_context_vector_OFFSET]
	b	zhv_leave
.endm

	.balign 2048
zhv_vectors:
	.irp number, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
	guest_vector \number
	.endr
	.balign 128

zhv_leave:
	/* Preserve every guest GPR before borrowing it. x0/x1 are on host SP_EL2. */
	stp	x2, x3, [x0, #(2 * 8)]
	stp	x4, x5, [x0, #(4 * 8)]
	stp	x6, x7, [x0, #(6 * 8)]
	stp	x8, x9, [x0, #(8 * 8)]
	stp	x10, x11, [x0, #(10 * 8)]
	stp	x12, x13, [x0, #(12 * 8)]
	stp	x14, x15, [x0, #(14 * 8)]
	stp	x16, x17, [x0, #(16 * 8)]
	stp	x18, x19, [x0, #(18 * 8)]
	stp	x20, x21, [x0, #(20 * 8)]
	stp	x22, x23, [x0, #(22 * 8)]
	stp	x24, x25, [x0, #(24 * 8)]
	stp	x26, x27, [x0, #(26 * 8)]
	stp	x28, x29, [x0, #(28 * 8)]
	str	x30, [x0, #(30 * 8)]
	ldp	x1, x2, [sp], #16
	stp	x1, x2, [x0]
	msr	daifset, #15
	add	x3, x0, #__zhv_switch_context_guest_OFFSET
	mrs	x2, elr_el2
	str	x2, [x3, #__zhv_a64_state_pc_OFFSET]
	mrs	x2, spsr_el2
	str	x2, [x3, #__zhv_a64_state_pstate_OFFSET]
	mrs	x2, esr_el2
	str	x2, [x0, #__zhv_switch_context_esr_OFFSET]
	mrs	x2, far_el2
	str	x2, [x0, #__zhv_switch_context_far_OFFSET]
	mrs	x2, hpfar_el2
	str	x2, [x0, #__zhv_switch_context_hpfar_OFFSET]
	/* Observe the physical IRQ before switching off the guest timer output. */
	mrs	x2, icc_hppir1_el1
	str	x2, [x0, #__zhv_switch_context_irq_id_OFFSET]
	save_shared
	msr	cntv_ctl_el0, xzr
	isb
	mrs	x2, cptr_el2
	bic	x2, x2, #0x400
	msr	cptr_el2, x2
	isb
	save_fp
	/* Host FP owner was flushed before entry; restore it lazily through TFP. */
	mrs	x2, cptr_el2
	orr	x2, x2, #0x400
	msr	cptr_el2, x2
	isb
	add	x3, x0, #__zhv_switch_context_host_OFFSET
	restore_shared
#define RESTORE_HOST(name) ldr x2, [x0, #__zhv_switch_context_host_##name##_OFFSET]; msr name, x2;
	ZHV_HOST_EL2_REGS(RESTORE_HOST)
#undef RESTORE_HOST
	/* A NULL original owner does not guarantee saved CPTR had TFP set. */
	mrs	x2, cptr_el2
	orr	x2, x2, #0x400
	msr	cptr_el2, x2
	isb
	ldr	x2, [x3, #__zhv_a64_state_cntv_cval_el0_OFFSET]
	msr	cntv_cval_el0, x2
	ldr	x2, [x3, #__zhv_a64_state_cntv_ctl_el0_OFFSET]
	msr	cntv_ctl_el0, x2
	isb
	clrex
	ldr	x1, [x0, #__zhv_switch_context_host_sp_OFFSET]
	mov	sp, x1
	ldr	x1, [x0, #__zhv_switch_context_host_daif_OFFSET]
	msr	daif, x1
	ldp	x19, x20, [x3, #(19 * 8)]
	ldp	x21, x22, [x3, #(21 * 8)]
	ldp	x23, x24, [x3, #(23 * 8)]
	ldp	x25, x26, [x3, #(25 * 8)]
	ldp	x27, x28, [x3, #(27 * 8)]
	ldp	x29, x30, [x3, #(29 * 8)]
	ret
