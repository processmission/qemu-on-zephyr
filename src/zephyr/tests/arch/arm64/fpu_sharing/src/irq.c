/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/arch/arm64/lib_helpers.h>
#include <stddef.h>
#include "registers.h"

BUILD_ASSERT(offsetof(struct fp_registers, fpcr) == TEST_FPCR_OFFSET);
BUILD_ASSERT(offsetof(struct fp_registers, fpsr) == TEST_FPSR_OFFSET);

static struct fp_registers result;
static volatile uint32_t timer_progress;
static uint64_t irq_daif;
static uint32_t irq_depth;
static bool in_isr;
static int32_t disable_result;

static void fp_irq(const void *arg)
{
	ARG_UNUSED(arg);
	in_isr = k_is_in_isr();
	irq_depth = arch_exception_depth();
	/* The nested FP trap must mask IRQs before granting this ISR access. */
	enable_irq();
	irq_daif = arm64_test_fp_clobber();
}

static void disable_fp_irq(const void *arg)
{
	ARG_UNUSED(arg);
	in_isr = k_is_in_isr();
	irq_depth = arch_exception_depth();
	disable_result = k_float_disable(k_current_get());
	irq_daif = read_daif();
}

static void nested_irq(const void *arg)
{
	ARG_UNUSED(arg);
	irq_offload(fp_irq, NULL);
}

static void timer_expiry(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	if (timer_progress == 1U) {
		fp_irq(NULL);
		timer_progress = 2U;
	}
}

static K_TIMER_DEFINE(fp_timer, timer_expiry, NULL);

static void check_registers(uint32_t expected_depth)
{
	zassert_true(in_isr, "FP callback did not execute in an ISR");
	zassert_equal(irq_depth, expected_depth, "wrong interrupt nesting depth");
	zassert_true((irq_daif & DAIF_IRQ_BIT) != 0U, "FP use in an ISR did not mask IRQs");
	zassert_equal(result.fpcr, TEST_THREAD_FPCR, "thread FPCR was not restored");
	zassert_equal(result.fpsr, TEST_THREAD_FPSR, "thread FPSR was not restored");
	for (size_t i = 0U; i < sizeof(result.regs); i++) {
		zassert_equal(result.regs[i], TEST_THREAD_FP_PATTERN,
			      "SIMD register byte %zu was not restored", i);
	}
}

ZTEST(fpu_sharing_generic, test_fp_in_timer_irq)
{
	uint32_t status;
	uint64_t deadline = read_cntpct_el0() + k_ms_to_cyc_ceil64(1000);

	in_isr = false;
	timer_progress = 0U;
	k_timer_start(&fp_timer, K_MSEC(1), K_MSEC(1));
	status = arm64_test_fp_timer_window(&timer_progress, &result, deadline);
	k_timer_stop(&fp_timer);
	zassert_ok(status, "physical timer IRQ did not interrupt the SIMD window");
	zassert_equal(timer_progress, 2U, "timer did not execute the FP callback");
	check_registers(1U);
}

ZTEST(fpu_sharing_generic, test_fp_in_nested_irq)
{
	in_isr = false;
	zassert_ok(arm64_test_fp_offload_window(nested_irq, &result));
	check_registers(2U);
}

ZTEST(fpu_sharing_generic, test_fp_disable_restore)
{
	in_isr = false;
	zassert_ok(arm64_test_fp_offload_window(disable_fp_irq, &result));
	zassert_ok(disable_result, "could not flush the interrupted thread's FP context");
	check_registers(1U);
}

ZTEST(fpu_sharing_generic, test_current_el)
{
	uint32_t expected_el = IS_ENABLED(CONFIG_ARM64_EL2) ? MODE_EL2 : MODE_EL1;

	zassert_equal(GET_EL(read_currentel()), expected_el, "wrong host exception level");
}
