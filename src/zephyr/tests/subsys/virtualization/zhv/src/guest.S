/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>

_ASM_FILE_PROLOGUE

GTEXT(zhv_probe_start)
GTEXT(zhv_probe_after_hvc)
GTEXT(zhv_probe_end)
SECTION_FUNC(TEXT, zhv_probe_start)
	mov	x10, #0x1234
	msr	tpidrro_el0, x10
	mov	x10, #0x5678
	msr	tpidr_el0, x10
	mov	x10, #0x9abc
	msr	tpidr_el1, x10
	mov	x0, #0x44
	str	x0, [x19]
	hvc	#0x123
zhv_probe_after_hvc:
	str	x0, [x19, #8]
	mov	x12, #0x09000000
	mov	w13, #0x5a
	str	w13, [x12, #4]
	ldrsb	x14, [x12, #1]
	mrs	x15, icc_pmr_el1
	msr	icc_pmr_el1, x15
	mrs	x16, icc_sre_el1
	mrs	x18, cntpct_el0
	wfi
	mov	x0, #0x55
	hvc	#0x124
	b	.
zhv_probe_end:

GTEXT(zhv_loop_start)
GTEXT(zhv_loop_end)
SECTION_FUNC(TEXT, zhv_loop_start)
	add	x0, x0, #1
	str	x0, [x19]
	b	zhv_loop_start
zhv_loop_end:

.macro fill_guest pattern
	.irp reg, v0.16b, v1.16b, v2.16b, v3.16b, v4.16b, v5.16b, v6.16b, v7.16b, \
		  v8.16b, v9.16b, v10.16b, v11.16b, v12.16b, v13.16b, v14.16b, v15.16b, \
		  v16.16b, v17.16b, v18.16b, v19.16b, v20.16b, v21.16b, v22.16b, v23.16b, \
		  v24.16b, v25.16b, v26.16b, v27.16b, v28.16b, v29.16b, v30.16b, v31.16b
	movi	\reg, #\pattern
	.endr
.endm

GTEXT(zhv_fp_start)
GTEXT(zhv_fp_end)
SECTION_FUNC(TEXT, zhv_fp_start)
	fill_guest 0xa5
	mov	x1, #0x800000
	msr	fpcr, x1
	mov	x1, #1
	msr	fpsr, x1
	hvc	#0x125
	fill_guest 0x3c
	hvc	#0x126
	b	.
zhv_fp_end:

/* Preserve caller ABI state, install host FP state, run, and record it. */
GTEXT(zhv_host_fp_run)
SECTION_FUNC(TEXT, zhv_host_fp_run)
	stp	x19, x30, [sp, #-96]!
	stp	d8, d9, [sp, #16]
	stp	d10, d11, [sp, #32]
	stp	d12, d13, [sp, #48]
	stp	d14, d15, [sp, #64]
	mrs	x4, fpcr
	mrs	x5, fpsr
	stp	x4, x5, [sp, #80]
	mov	x19, x3
	.irp reg, v8.16b, v9.16b, v10.16b, v11.16b, v12.16b, v13.16b, v14.16b, v15.16b
	movi	\reg, #0x66
	.endr
	mov	x4, #0x400000
	msr	fpcr, x4
	msr	fpsr, xzr
	bl	zhv_vcpu_run
	stp	d8, d9, [x19]
	stp	d10, d11, [x19, #16]
	stp	d12, d13, [x19, #32]
	stp	d14, d15, [x19, #48]
	mrs	x4, fpcr
	mrs	x5, fpsr
	stp	x4, x5, [x19, #64]
	ldp	x4, x5, [sp, #80]
	msr	fpcr, x4
	msr	fpsr, x5
	ldp	d8, d9, [sp, #16]
	ldp	d10, d11, [sp, #32]
	ldp	d12, d13, [sp, #48]
	ldp	d14, d15, [sp, #64]
	ldp	x19, x30, [sp], #96
	ret

GTEXT(zhv_timer_start)
GTEXT(zhv_timer_end)
SECTION_FUNC(TEXT, zhv_timer_start)
	mrs	x1, cntvct_el0
	add	x1, x1, x20
	msr	cntv_cval_el0, x1
	mov	x1, #1
	msr	cntv_ctl_el0, x1
	isb
	wfi
	hvc	#0x130
	mrs	x1, cntvct_el0
	add	x1, x1, x20
	msr	cntv_cval_el0, x1
	isb
	wfi
	hvc	#0x131
	msr	cntv_ctl_el0, xzr
	isb
	hvc	#0x132
	b	.
zhv_timer_end:

GTEXT(zhv_timer_busy_start)
GTEXT(zhv_timer_busy_end)
SECTION_FUNC(TEXT, zhv_timer_busy_start)
	mrs	x1, cntvct_el0
	add	x1, x1, x20
	msr	cntv_cval_el0, x1
	/* Give the outer timer callback time to deliver its asynchronous IRQ. */
	add	x3, x1, x20, lsl #2
	mov	x2, #1
	msr	cntv_ctl_el0, x2
	isb
	/* Model Linux's trapped EOI after reprogramming, before the next expiry. */
	hvc	#0x134
1:
	mrs	x2, cntvct_el0
	cmp	x2, x3
	b.lo	1b
	/* Stay in guest after expiry until the test completes a trapped HVC. */
	hvc	#0x135
	b	zhv_timer_busy_start
zhv_timer_busy_end:

GTEXT(zhv_irq_start)
GTEXT(zhv_irq_vectors)
GTEXT(zhv_irq_end)
SECTION_FUNC(TEXT, zhv_irq_start)
	msr	daifclr, #2
	wfi
	b	.
	.balign 2048
zhv_irq_vectors:
	/* Current EL, SPx, IRQ: VBAR_EL1 + 0x280. */
	.space 0x280
	b	zhv_irq_handler
	.space 0x800 - 0x284
zhv_irq_handler:
	mrs	x0, icc_iar1_el1
	msr	icc_eoir1_el1, x0
	hvc	#0x140
	b	.
zhv_irq_end:

GTEXT(zhv_rom_start)
GTEXT(zhv_rom_end)
SECTION_FUNC(TEXT, zhv_rom_start)
	ldr	x0, [x20]
	hvc	#0x150
	str	x0, [x20]
	hvc	#0x151
	b	.
zhv_rom_end:
