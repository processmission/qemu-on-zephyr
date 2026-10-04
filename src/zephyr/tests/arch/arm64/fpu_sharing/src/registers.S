/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>
#include <zephyr/arch/arm64/syscall.h>
#include "registers.h"

_ASM_FILE_PROLOGUE

.macro fill_fp_registers pattern
	.irp reg, v0.16b, v1.16b, v2.16b, v3.16b, v4.16b, v5.16b, v6.16b, v7.16b, \
		  v8.16b, v9.16b, v10.16b, v11.16b, v12.16b, v13.16b, v14.16b, v15.16b, \
		  v16.16b, v17.16b, v18.16b, v19.16b, v20.16b, v21.16b, v22.16b, v23.16b, \
		  v24.16b, v25.16b, v26.16b, v27.16b, v28.16b, v29.16b, v30.16b, v31.16b
	movi	\reg, #\pattern
	.endr
.endm

/* Preserve the AAPCS64 callee-saved portion of the SIMD register file. */
.macro save_callee_fp
	stp	d8, d9, [sp, #-80]!
	stp	d10, d11, [sp, #16]
	stp	d12, d13, [sp, #32]
	stp	d14, d15, [sp, #48]
.endm

.macro restore_callee_fp
	ldp	d10, d11, [sp, #16]
	ldp	d12, d13, [sp, #32]
	ldp	d14, d15, [sp, #48]
	ldp	d8, d9, [sp], #80
.endm

GTEXT(arm64_test_fp_timer_window)
SECTION_FUNC(TEXT, arm64_test_fp_timer_window)
	mov	x3, #0
	b	fp_window

GTEXT(arm64_test_fp_offload_window)
SECTION_FUNC(TEXT, arm64_test_fp_offload_window)
	mov	x3, #1

fp_window:
	save_callee_fp
	mrs	x4, fpcr
	mrs	x5, fpsr
	stp	x4, x5, [sp, #64]
	fill_fp_registers TEST_THREAD_FP_PATTERN
	mov	x4, #TEST_THREAD_FPCR
	msr	fpcr, x4
	mov	x4, #TEST_THREAD_FPSR
	msr	fpsr, x4

	cbnz	x3, offload_window

	/* No C call may clobber registers while a physical timer IRQ is pending. */
	mov	w3, #1
	str	w3, [x0]
wait_for_irq:
	ldr	w3, [x0]
	cmp	w3, #2
	b.eq	window_done
	mrs	x3, cntpct_el0
	cmp	x3, x2
	b.lo	wait_for_irq
	mov	w0, #1
	b	store_registers

offload_window:
	/* SVC preserves all interrupted registers, unlike an ordinary C call. */
	mov	x9, x1
	mov	x1, #0
	svc	#_SVC_CALL_IRQ_OFFLOAD
	mov	x1, x9

window_done:
	mov	w0, #0
store_registers:
	.irp reg, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, \
		  16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
	str	q\reg, [x1, #(16 * \reg)]
	.endr
	mrs	x4, fpcr
	mrs	x5, fpsr
	str	w4, [x1, #TEST_FPCR_OFFSET]
	str	w5, [x1, #TEST_FPSR_OFFSET]
	ldp	x4, x5, [sp, #64]
	msr	fpcr, x4
	msr	fpsr, x5
	restore_callee_fp
	ret

/* Change the FP environment in an ISR and return its resulting IRQ mask. */
GTEXT(arm64_test_fp_clobber)
SECTION_FUNC(TEXT, arm64_test_fp_clobber)
	save_callee_fp
	fill_fp_registers TEST_ISR_FP_PATTERN
	mov	x0, #TEST_ISR_FPCR
	msr	fpcr, x0
	mov	x0, #TEST_ISR_FPSR
	msr	fpsr, x0
	mrs	x0, daif
	restore_callee_fp
	ret
