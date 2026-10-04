/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/arch/arm64/cpu.h>
#include <zephyr/arch/arm64/lib_helpers.h>

extern char _vector_table[];

static K_THREAD_STACK_DEFINE(high_stack, 1024);
static K_THREAD_STACK_DEFINE(low_stack, 1024);
static struct k_thread high_thread;
static struct k_thread low_thread;
static K_SEM_DEFINE(done_sem, 0, 1);

static volatile bool high_started;
static volatile bool high_woke;
static volatile bool low_started;
static volatile bool low_in_loop;
static volatile bool low_timed_out;
static volatile bool low_was_running_when_high_woke;

static void high_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	high_started = true;
	k_sleep(K_MSEC(20));

	/* The low-priority thread must still be in its busy loop here. */
	low_was_running_when_high_woke = low_in_loop;
	high_woke = true;
}

static void low_entry(void *p1, void *p2, void *p3)
{
	uint64_t deadline;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	low_started = true;
	deadline = k_cycle_get_64() + k_ms_to_cyc_ceil64(1000);
	low_in_loop = true;

	while (!high_woke) {
		if (k_cycle_get_64() >= deadline) {
			low_timed_out = true;
			break;
		}
	}

	low_in_loop = false;
	k_sem_give(&done_sem);
}

ZTEST(arm64_el2, test_el2_host_state)
{
	/*
	 * This suite is also run as an EL1 regression variant.  The timer,
	 * thread and preemption tests below are identical at both levels.
	 */
#if defined(CONFIG_ARM64_EL2)
	zassert_equal(GET_EL(read_currentel()), MODE_EL2,
		      "Zephyr is not running at EL2");
	zassert_true((read_sctlr_el2() & SCTLR_M_BIT) != 0U,
		     "EL2 stage-1 MMU is not enabled");
	zassert_not_equal(read_ttbr0_el2(), 0U,
			  "TTBR0_EL2 is not programmed");
	zassert_equal(read_vbar_el2(), (uint64_t)_vector_table,
		      "VBAR_EL2 does not point at the Zephyr vector table");
#else
	zassert_equal(GET_EL(read_currentel()), MODE_EL1,
		      "Zephyr is not running at EL1");
	zassert_true((read_sctlr_el1() & SCTLR_M_BIT) != 0U,
		     "EL1 stage-1 MMU is not enabled");
	zassert_not_equal(read_ttbr0_el1(), 0U,
			  "TTBR0_EL1 is not programmed");
	zassert_equal(read_vbar_el1(), (uint64_t)_vector_table,
		      "VBAR_EL1 does not point at the Zephyr vector table");
#endif
}

ZTEST(arm64_el2, test_time_advance_and_sleep_wakeup)
{
	uint64_t cycles;
	int64_t start;
	int64_t elapsed;

	start = k_uptime_get();
	k_sleep(K_MSEC(50));
	elapsed = k_uptime_get() - start;
	zassert_true(elapsed >= 40, "system time did not advance: %lld ms",
		     elapsed);

	cycles = k_cycle_get_64();
	k_busy_wait(1000);
	zassert_true(k_cycle_get_64() > cycles,
		     "architectural timer counter did not advance");
}

ZTEST(arm64_el2, test_two_threads_sleep_and_preemption)
{
	high_started = false;
	high_woke = false;
	low_started = false;
	low_in_loop = false;
	low_timed_out = false;
	low_was_running_when_high_woke = false;

	k_thread_create(&high_thread, high_stack, K_THREAD_STACK_SIZEOF(high_stack),
			high_entry, NULL, NULL, NULL,
			K_PRIO_PREEMPT(2), 0, K_NO_WAIT);
	k_thread_create(&low_thread, low_stack, K_THREAD_STACK_SIZEOF(low_stack),
			low_entry, NULL, NULL, NULL,
			K_PRIO_PREEMPT(5), 0, K_NO_WAIT);

	zassert_ok(k_sem_take(&done_sem, K_SECONDS(5)),
		   "low-priority worker did not complete");
	zassert_true(high_started, "high-priority worker did not start");
	zassert_true(high_woke, "high-priority worker was not woken by the timer");
	zassert_true(low_started, "low-priority worker did not start");
	zassert_false(low_timed_out, "low-priority worker timed out");
	zassert_true(low_was_running_when_high_woke,
		     "sleeping high-priority thread did not preempt the low-priority thread");
}

ZTEST_SUITE(arm64_el2, NULL, NULL, NULL, NULL, NULL);
