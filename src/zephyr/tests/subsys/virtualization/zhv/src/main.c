/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/kernel/internal/mm.h>
#include <zephyr/arch/arm64/lib_helpers.h>
#include <zephyr/virtualization/zhv.h>
#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>

extern char zhv_probe_start[], zhv_probe_after_hvc[], zhv_probe_end[];
extern char zhv_loop_start[], zhv_loop_end[], zhv_fp_start[], zhv_fp_end[];
extern char zhv_timer_start[], zhv_timer_end[];
extern char zhv_timer_busy_start[], zhv_timer_busy_end[];
extern char zhv_irq_start[], zhv_irq_vectors[], zhv_irq_end[];
extern char zhv_rom_start[], zhv_rom_end[];
extern int zhv_host_fp_run(struct zhv_vcpu *vcpu, const struct zhv_run_input *input,
			   struct zhv_exit *exit, uint64_t result[10]);

static struct zhv_vm *vm;
static struct zhv_vcpu *vcpu;
static struct zhv_ram ram;
static struct zhv_a64_state state;
static struct zhv_exit stopped;
static struct zhv_run_input input;
static uint8_t image_data[2U * 1024U * 1024U] __aligned(2U * 1024U * 1024U);
static __thread uint64_t tls_sentinel = 0x778899aabbccddeeULL;

static void before(void *fixture)
{
	const struct zhv_vm_config config = {0x40000000, 16U * 1024U * 1024U};

	ARG_UNUSED(fixture);
	zassert_ok(zhv_vm_create(&config, &vm, &ram));
	zassert_ok(zhv_vcpu_create(vm, &vcpu));
	memset(&state, 0, sizeof(state));
	memset(&input, 0, sizeof(input));
	state.pc = ram.guest_ipa;
	state.pstate = 0x3c5;
	state.sctlr_el1 = SCTLR_EL1_RES1;
	state.cpacr_el1 = 3U << 20;
	state.sp_el1 = ram.guest_ipa + ram.size - 16U;
	state.x[19] = ram.guest_ipa + 0x10000U;
}

static void after(void *fixture)
{
	ARG_UNUSED(fixture);
	zassert_ok(zhv_vm_destroy(vm));
}

static void load_probe(const char *start, const char *end)
{
	memcpy(ram.host_va, start, end - start);
	zassert_ok(zhv_vcpu_set_state(vcpu, &state));
}

static void run_to(enum zhv_exit_reason expected)
{
	uint64_t deadline = read_cntpct_el0() + k_ms_to_cyc_ceil64(1000);

	do {
		zassert_ok(zhv_vcpu_run(vcpu, &input, &stopped));
		input.vtimer_level_seen = stopped.vtimer.level;
		if (stopped.reason == ZHV_EXIT_HOST_IRQ || stopped.reason == ZHV_EXIT_KICK ||
		    stopped.reason == ZHV_EXIT_TIMER) {
			continue;
		}
		zassert_equal(stopped.reason, expected,
			      "exit=%u pc=%llx esr=%llx far=%llx vector=%u",
			      stopped.reason, stopped.pc, stopped.esr, stopped.far,
			      stopped.u.fail.category);
		return;
	} while (read_cntpct_el0() < deadline);
	zassert_unreachable("guest exit timed out");
}

static void complete(uint64_t value)
{
	struct zhv_completion response = {.exit_seq = stopped.seq};

	if (stopped.reason == ZHV_EXIT_HVC || stopped.reason == ZHV_EXIT_SMC) {
		response.nr_values = 4U;
		memcpy(response.value, stopped.u.call.x, sizeof(response.value));
		response.value[0] = value;
	} else if ((stopped.reason == ZHV_EXIT_MMIO && !stopped.u.mmio.write) ||
		   (stopped.reason == ZHV_EXIT_SYSREG && stopped.u.sysreg.read)) {
		response.nr_values = 1U;
		response.value[0] = value;
	}
	zassert_ok(zhv_vcpu_complete(vcpu, &response));
	zassert_equal(zhv_vcpu_complete(vcpu, &response), -ESTALE);
}

ZTEST(zhv, test_native_exits_and_host_context)
{
	uint64_t host_cpu = read_tpidrro_el0();
	uint64_t host_tls = read_sysreg(tpidr_el0);
	volatile uint64_t *marker = (uint64_t *)((uint8_t *)ram.host_va + 0x10000U);

	zassert_not_equal(k_mem_phys_addr(ram.host_va), ram.guest_ipa,
			  "probe requires nonidentity Stage-2");
	printk("zhv RAM IPA=%llx PA=%lx size=%zu\n", ram.guest_ipa,
	       k_mem_phys_addr(ram.host_va), ram.size);
	load_probe(zhv_probe_start, zhv_probe_end);
	run_to(ZHV_EXIT_HVC);
	zassert_equal(stopped.u.call.immediate, 0x123U);
	zassert_equal(stopped.pc, ram.guest_ipa + (zhv_probe_after_hvc - zhv_probe_start));
	zassert_equal(marker[0], 0x44U);
	zassert_equal(read_tpidrro_el0(), host_cpu);
	zassert_equal(read_sysreg(tpidr_el0), host_tls);
	zassert_equal(tls_sentinel, 0x778899aabbccddeeULL);
	zassert_equal(zhv_vcpu_run(vcpu, &input, &stopped), -EBUSY);
	complete(0x888);
	run_to(ZHV_EXIT_MMIO);
	zassert_equal(marker[1], 0x888U, "HVC return skipped an instruction");
	zassert_true(stopped.u.mmio.write);
	zassert_equal(stopped.u.mmio.ipa, 0x09000004U);
	zassert_equal(stopped.u.mmio.size, 4U);
	zassert_equal(stopped.u.mmio.value, 0x5aU);
	complete(0U);
	run_to(ZHV_EXIT_MMIO);
	zassert_false(stopped.u.mmio.write);
	zassert_equal(stopped.u.mmio.ipa, 0x09000001U);
	zassert_true(stopped.u.mmio.sign_extend);
	complete(0x80);
	run_to(ZHV_EXIT_SYSREG);
	zassert_true(stopped.u.sysreg.read);
	zassert_equal(stopped.u.sysreg.encoding, 0xc230U);
	complete(0x90);
	run_to(ZHV_EXIT_SYSREG);
	zassert_false(stopped.u.sysreg.read);
	zassert_equal(stopped.u.sysreg.value, 0x90U);
	complete(0U);
	run_to(ZHV_EXIT_SYSREG);
	zassert_true(stopped.u.sysreg.read);
	zassert_equal(stopped.u.sysreg.encoding, 0xdf01U, "expected CNTPCT_EL0 trap");
	complete(0x123456U);
	run_to(ZHV_EXIT_WFI);
	complete(0U);
	run_to(ZHV_EXIT_HVC);
	zassert_equal(stopped.u.call.immediate, 0x124U);
	zassert_ok(zhv_vcpu_get_state(vcpu, &state));
	zassert_equal(state.x[14], 0xffffffffffffff80ULL);
	zassert_equal(state.x[16], 7U, "outer QEMU SRE model differs");
	zassert_equal(state.x[18], 0x123456U);
	zassert_equal(state.tpidrro_el0, 0x1234U);
	zassert_equal(state.tpidr_el0, 0x5678U);
	zassert_equal(state.tpidr_el1, 0x9abcU);
}

static K_THREAD_STACK_DEFINE(high_stack, 2048);
static K_THREAD_STACK_DEFINE(low_stack, 4096);
static struct k_thread high_thread, low_thread;
static K_SEM_DEFINE(work_done, 0, 1);
static volatile bool in_run, stop_loop, observed_run;
static uint64_t first_count, second_count;
static int concurrent_result, worker_result;

static void high_entry(void *a, void *b, void *c)
{
	volatile uint64_t *marker = (uint64_t *)((uint8_t *)ram.host_va + 0x10000U);
	struct zhv_clock clock;
	uint64_t deadline = read_cntpct_el0() + k_ms_to_cyc_ceil64(500);

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	/* Observe guest execution after the executor's initial cache maintenance. */
	do {
		k_sleep(K_MSEC(1));
	} while (*marker == 0U && read_cntpct_el0() < deadline);
	observed_run = in_run;
	concurrent_result = zhv_vm_get_clock(vm, &clock);
	first_count = *marker;
	k_sleep(K_MSEC(20));
	second_count = *marker;
	stop_loop = true;
}

static void low_entry(void *a, void *b, void *c)
{
	struct zhv_exit exit;
	uint64_t deadline = read_cntpct_el0() + k_ms_to_cyc_ceil64(1000);

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	worker_result = 0;
	while (!stop_loop && read_cntpct_el0() < deadline) {
		in_run = true;
		worker_result = zhv_vcpu_run(vcpu, &input, &exit);
		in_run = false;
		if (worker_result != 0 || exit.reason != ZHV_EXIT_HOST_IRQ) {
			worker_result = -EIO;
			break;
		}
	}
	k_sem_give(&work_done);
}

ZTEST(zhv, test_thread_handoff_and_guest_preemption)
{
	load_probe(zhv_loop_start, zhv_loop_end);
	in_run = false;
	stop_loop = false;
	observed_run = false;
	k_thread_create(&high_thread, high_stack, K_THREAD_STACK_SIZEOF(high_stack),
			high_entry, NULL, NULL, NULL, K_PRIO_PREEMPT(1), 0, K_NO_WAIT);
	k_thread_create(&low_thread, low_stack, K_THREAD_STACK_SIZEOF(low_stack),
			low_entry, NULL, NULL, NULL, K_PRIO_PREEMPT(3), 0, K_NO_WAIT);
	zassert_ok(k_sem_take(&work_done, K_SECONDS(3)));
	zassert_ok(k_thread_join(&high_thread, K_SECONDS(1)));
	zassert_ok(k_thread_join(&low_thread, K_SECONDS(1)));
	zassert_ok(worker_result);
	zassert_true(stop_loop && observed_run, "host did not preempt guest execution");
	zassert_equal(concurrent_result, -EBUSY);
	zassert_true(first_count > 0U && second_count > first_count,
		     "guest failed to resume between host wakeups: %llu -> %llu",
		     first_count, second_count);
}

ZTEST(zhv, test_guest_host_fp_separation)
{
	uint64_t host_result[10];

	load_probe(zhv_fp_start, zhv_fp_end);
	for (uint32_t pass = 0U; pass < 2U; pass++) {
		uint64_t deadline = read_cntpct_el0() + k_ms_to_cyc_ceil64(1000);

		do {
			zassert_ok(zhv_host_fp_run(vcpu, &input, &stopped, host_result));
			for (size_t i = 0U; i < 8U; i++) {
				zassert_equal(host_result[i], 0x6666666666666666ULL);
			}
			zassert_equal(host_result[8], 0x400000U);
			zassert_equal(host_result[9], 0U);
			zassert_true(read_cntpct_el0() < deadline, "FP guest timed out");
		} while (stopped.reason == ZHV_EXIT_HOST_IRQ);
		zassert_equal(stopped.reason, ZHV_EXIT_HVC, "FP exit=%u esr=%llx",
			      stopped.reason, stopped.esr);
		zassert_equal(stopped.u.call.immediate, 0x125U + pass);
		zassert_ok(zhv_vcpu_get_state(vcpu, &state));
		for (size_t i = 0U; i < 32U; i++) {
			uint64_t expected = pass == 0U ? 0xa5a5a5a5a5a5a5a5ULL :
							0x3c3c3c3c3c3c3c3cULL;

			zassert_equal(state.q[i][0], expected);
			zassert_equal(state.q[i][1], expected);
		}
		zassert_equal(state.fpcr, 0x800000U);
		zassert_equal(state.fpsr, 1U);
		complete(0U);
	}
}

ZTEST(zhv, test_virtual_timer_wait_and_rearm)
{
	state.x[20] = k_ms_to_cyc_ceil64(10);
	load_probe(zhv_timer_start, zhv_timer_end);
	for (uint32_t pass = 0U; pass < 2U; pass++) {
		uint64_t start;

		run_to(ZHV_EXIT_WFI);
		zassert_false(stopped.vtimer.level);
		complete(0U);
		start = read_cntpct_el0();
		zassert_ok(zhv_vcpu_wait(vcpu, stopped.kick_seq, UINT64_MAX));
		zassert_true(read_cntpct_el0() > start);
		zassert_ok(zhv_vcpu_run(vcpu, &input, &stopped));
		zassert_equal(stopped.reason, ZHV_EXIT_TIMER);
		zassert_true(stopped.vtimer.level);
		input.vtimer_level_seen = true;
		run_to(ZHV_EXIT_HVC);
		zassert_equal(stopped.u.call.immediate, 0x130U + pass);
		complete(0U);
	}
	run_to(ZHV_EXIT_HVC);
	zassert_equal(stopped.u.call.immediate, 0x132U);
	zassert_false(stopped.vtimer.level);
}

ZTEST(zhv, test_gic_traps_and_virtual_irq)
{
	state.vbar_el1 = ram.guest_ipa + (zhv_irq_vectors - zhv_irq_start);
	load_probe(zhv_irq_start, zhv_irq_end);
	input.irq = true;
	run_to(ZHV_EXIT_SYSREG);
	zassert_true(stopped.u.sysreg.read);
	zassert_equal(stopped.u.sysreg.encoding, 0xc660U, "expected ICC_IAR1_EL1");
	input.irq = false;
	complete(27U);
	run_to(ZHV_EXIT_SYSREG);
	zassert_false(stopped.u.sysreg.read);
	zassert_equal(stopped.u.sysreg.encoding, 0xc661U, "expected ICC_EOIR1_EL1");
	zassert_equal(stopped.u.sysreg.value, 27U);
	complete(0U);
	run_to(ZHV_EXIT_HVC);
	zassert_equal(stopped.u.call.immediate, 0x140U);
}

ZTEST(zhv, test_physical_timer_notification_rearm)
{
	uint32_t notifications = 0U;

	state.x[20] = k_ms_to_cyc_ceil64(2);
	load_probe(zhv_timer_busy_start, zhv_timer_busy_end);
	for (uint32_t pass = 0U; pass < 8U; pass++) {
		uint64_t deadline = read_cntpct_el0() + k_ms_to_cyc_ceil64(1000);

		run_to(ZHV_EXIT_HVC);
		zassert_equal(stopped.u.call.immediate, 0x134U);
		zassert_false(stopped.vtimer.level);
		complete(0U);
		do {
			zassert_ok(zhv_vcpu_run(vcpu, &input, &stopped));
			input.vtimer_level_seen = stopped.vtimer.level;
			if (stopped.reason == ZHV_EXIT_HOST_IRQ && stopped.u.host_irq.intid == 27U) {
				notifications++;
				zassert_true(stopped.vtimer.level);
			}
			zassert_true(read_cntpct_el0() < deadline, "timer IRQ storm");
		} while (stopped.reason == ZHV_EXIT_HOST_IRQ || stopped.reason == ZHV_EXIT_TIMER);
		zassert_equal(stopped.reason, ZHV_EXIT_HVC);
		zassert_equal(stopped.u.call.immediate, 0x135U);
		complete(0U);
	}
	zassert_true(notifications >= 2U, "physical PPI27 did not rearm: %u", notifications);
	printk("zhv physical CNTV notifications: %u over 8 rearm cycles\n", notifications);
}

ZTEST(zhv, test_unmapped_instruction_fault)
{
	state.pc = 0x80000000U;
	zassert_ok(zhv_vcpu_set_state(vcpu, &state));
	run_to(ZHV_EXIT_FAIL);
	zassert_equal((stopped.esr >> 26) & 63U, 0x20U);
	zassert_equal(stopped.pc, 0x80000000U);
	zassert_false(stopped.needs_completion);
	zassert_equal(zhv_vcpu_run(vcpu, &input, &stopped), -EIO);
}

ZTEST(zhv, test_validation_and_latched_wake)
{
	struct zhv_clock clock;
	struct zhv_completion bad = {.exit_seq = 99U};

	zassert_ok(zhv_vm_get_clock(vm, &clock));
	zassert_equal(clock.frequency_hz, read_cntfrq_el0());
	zassert_equal(zhv_vcpu_complete(vcpu, &bad), -ESTALE);
	state.pstate = 9U;
	zassert_equal(zhv_vcpu_set_state(vcpu, &state), -EINVAL, "EL2 guest accepted");
	state.pstate = 0x3c5;
	/* Exercise cval+offset overflow without accidentally arming an immediate deadline. */
	state.cntv_cval_el0 = UINT64_MAX;
	state.cntv_ctl_el0 = 1U;
	load_probe(zhv_probe_start, zhv_probe_end);
	zhv_vcpu_kick(vcpu);
	zassert_ok(zhv_vcpu_wait(vcpu, 0U, UINT64_MAX));
	zassert_ok(zhv_vcpu_run(vcpu, &input, &stopped));
	zassert_equal(stopped.reason, ZHV_EXIT_KICK);
	zassert_false(stopped.vtimer.level);
	run_to(ZHV_EXIT_HVC);
	bad.exit_seq = stopped.seq;
	bad.nr_values = 1U;
	zassert_equal(zhv_vcpu_complete(vcpu, &bad), -EINVAL);
	bad.nr_values = 4U;
	zassert_ok(zhv_vcpu_complete(vcpu, &bad));
}

ZTEST(zhv, test_rom_mapping_and_write_protection)
{
	struct zhv_ram image = {
		.host_va = image_data,
		.guest_ipa = BIT64(CONFIG_ARM64_HYPERVISOR_IPA_BITS) - sizeof(image_data),
		.size = sizeof(image_data),
	};
	struct zhv_vm_config config = {
		.ram_ipa = ram.guest_ipa,
		.ram_size = ram.size,
		.rom = &image,
	};
	uint64_t sentinel = 0x426f6f74496d6167ULL;

	zassert_ok(zhv_vm_destroy(vm));
	image.guest_ipa = config.ram_ipa;
	zassert_equal(zhv_vm_create(&config, &vm, &ram), -EINVAL);
	image.guest_ipa = BIT64(CONFIG_ARM64_HYPERVISOR_IPA_BITS) - sizeof(image_data);
	memcpy(image_data, &sentinel, sizeof(sentinel));
	__asm__ volatile("dc cvac, %0; dsb ish" :: "r"(image_data) : "memory");
	zassert_ok(zhv_vm_create(&config, &vm, &ram));
	zassert_ok(zhv_vcpu_create(vm, &vcpu));
	state.x[20] = image.guest_ipa;
	load_probe(zhv_rom_start, zhv_rom_end);
	run_to(ZHV_EXIT_HVC);
	zassert_equal(stopped.u.call.x[0], sentinel);
	complete(sentinel);
	run_to(ZHV_EXIT_FAIL);
	zassert_equal((stopped.esr >> 26) & 63U, 0x24U);
	zassert_equal(stopped.esr & 63U, 14U);
	zassert_mem_equal(image_data, &sentinel, sizeof(sentinel));
	zassert_equal(zhv_vcpu_run(vcpu, &input, &stopped), -EIO);

	zassert_ok(zhv_vm_destroy(vm));
	zassert_ok(zhv_vm_create(&config, &vm, &ram));
	zassert_ok(zhv_vcpu_create(vm, &vcpu));
	state.pc = image.guest_ipa;
	zassert_ok(zhv_vcpu_set_state(vcpu, &state));
	run_to(ZHV_EXIT_FAIL);
	zassert_equal((stopped.esr >> 26) & 63U, 0x20U);
	zassert_equal(stopped.esr & 63U, 14U);
}

ZTEST_SUITE(zhv, NULL, NULL, before, after, NULL);
