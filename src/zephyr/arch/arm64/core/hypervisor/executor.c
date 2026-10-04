/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/kernel/internal/mm.h>
#include <zephyr/arch/arm64/lib_helpers.h>
#include <zephyr/drivers/interrupt_controller/gic.h>
#include <zephyr/drivers/timer/arm_arch_timer.h>
#include <zephyr/irq.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <kernel_arch_interface.h>
#include <errno.h>
#include <string.h>
#include "context.h"

#define BLOCK_SIZE BIT64(21)
#define RAM_CAPACITY (CONFIG_ARM64_HYPERVISOR_RAM_SIZE_MIB * 1024ULL * 1024ULL)
#define S2_RAM_ATTRIBUTES (BIT64(10) | (3ULL << 8) | (3ULL << 6) | (15ULL << 2) | 1ULL)
#define GUEST_HCR (BIT64(31) | BIT64(22) | BIT64(21) | BIT64(20) | BIT64(19) | BIT64(18) | \
		   BIT64(14) | BIT64(13) | BIT64(5) | BIT64(4) | BIT64(3) | BIT64(0))

struct zhv_vm {
	atomic_t allocated;
	struct zhv_ram ram;
	struct zhv_clock clock;
};

struct zhv_vcpu {
	struct zhv_switch_context context;
	struct zhv_exit pending;
	atomic_t allocated;
	atomic_t kick_seq;
	uint64_t consumed_kick;
	uint64_t exit_seq;
	bool initialized;
	bool awaiting_completion;
	bool failed;
	bool waiting;
	bool clean_ram;
};

static struct zhv_vm single_vm;
static struct zhv_vcpu single_vcpu;
static uint8_t guest_ram[RAM_CAPACITY] __aligned(BLOCK_SIZE) __noinit;
static uint64_t stage2_l1[512] __aligned(4096);
static uint64_t stage2_l2[4][512] __aligned(4096);
static K_MUTEX_DEFINE(operation_lock);
static K_SEM_DEFINE(wake_event, 0, 1);

BUILD_ASSERT((RAM_CAPACITY % BLOCK_SIZE) == 0U);
BUILD_ASSERT(offsetof(struct zhv_switch_context, guest) == 0U);
BUILD_ASSERT(offsetof(struct zhv_a64_state, x) == 0U);
BUILD_ASSERT((offsetof(struct zhv_a64_state, q) % 16U) == 0U);

static void scheduling_deadline(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	/* A physical IRQ also bounds vCPU runs in an otherwise idle tickless host. */
}

static K_TIMER_DEFINE(preemption_timer, scheduling_deadline, NULL);

static void virtual_timer_irq(const void *arg)
{
	ARG_UNUSED(arg);
	/* Retire a delayed notification outside the direct guest timer window. */
	arm_gic_irq_disable(ARM_TIMER_VIRTUAL_IRQ);
	arm_gic_irq_clear_pending(ARM_TIMER_VIRTUAL_IRQ);
}

static int take_lock(void)
{
	if (k_is_in_isr() || (read_daif() & DAIF_IRQ_BIT) != 0U) {
		return -EINVAL;
	}
	if (k_mutex_lock(&operation_lock, K_NO_WAIT) != 0) {
		return -EBUSY;
	}
	return 0;
}

static bool vm_valid(const struct zhv_vm *vm)
{
	return vm == &single_vm && atomic_get(&single_vm.allocated) != 0;
}

static bool vcpu_valid(const struct zhv_vcpu *vcpu)
{
	return vcpu == &single_vcpu && vm_valid(&single_vm) &&
	       atomic_get(&single_vcpu.allocated) != 0;
}

static uint64_t kick_generation(void)
{
	return (uint32_t)atomic_get(&single_vcpu.kick_seq);
}

static void invalidate_stage2(void)
{
	uint64_t previous = read_sysreg(vttbr_el2);

	write_sysreg(single_vcpu.context.guest_vttbr, vttbr_el2);
	__asm__ volatile("dsb ishst; isb; tlbi vmalls12e1is; dsb ish; isb" ::: "memory");
	write_sysreg(previous, vttbr_el2);
	barrier_isync_fence_full();
}

static void prepare_ram(void)
{
	uintptr_t end = (uintptr_t)single_vm.ram.host_va + single_vm.ram.size;
	uint64_t ctr = read_sysreg(ctr_el0);
	size_t line_size = 4U << ((ctr >> 16) & 15U);

	for (uintptr_t addr = (uintptr_t)single_vm.ram.host_va; addr < end;
	     addr += line_size) {
		__asm__ volatile("dc cvac, %0" :: "r"(addr) : "memory");
	}
	__asm__ volatile("dsb ish; ic iallu; dsb ish; isb" ::: "memory");
	single_vcpu.clean_ram = true;
}

static struct zhv_timer_sample timer_sample(void)
{
	const struct zhv_a64_state *guest = &single_vcpu.context.guest;
	struct zhv_timer_sample sample = {
		.counter = read_cntpct_el0() - single_vm.clock.counter_offset,
		.cval = guest->cntv_cval_el0,
		.ctl = guest->cntv_ctl_el0 & 3U,
	};

	if ((sample.ctl & 1U) != 0U && sample.counter >= sample.cval) {
		sample.ctl |= 4U;
	}
	sample.level = (sample.ctl & 7U) == 5U;
	return sample;
}

int zhv_vm_create(const struct zhv_vm_config *config, struct zhv_vm **vm, struct zhv_ram *ram)
{
	uint64_t pa;
	uint64_t parange;
	int ret = take_lock();

	if (ret != 0) {
		return ret;
	}
	if (config == NULL || vm == NULL || ram == NULL || config->ram_size == 0U ||
	    (config->ram_ipa % BLOCK_SIZE) != 0U ||
	    (config->ram_size % BLOCK_SIZE) != 0U || config->ram_ipa >= BIT64(32) ||
	    config->ram_size > BIT64(32) - config->ram_ipa) {
		ret = -EINVAL;
		goto out;
	}
	if (atomic_get(&single_vm.allocated) != 0) {
		ret = -EBUSY;
		goto out;
	}
	if (config->ram_size > RAM_CAPACITY) {
		ret = -ENOMEM;
		goto out;
	}
	parange = read_id_aa64mmfr0_el1() & 15U;
	if (GET_EL(read_currentel()) != MODE_EL2 || parange > 2U ||
	    (read_cntv_ctl_el0() & 1U) != 0U) {
		ret = -ENOTSUP;
		goto out;
	}
	pa = k_mem_phys_addr(guest_ram);
	if ((pa % BLOCK_SIZE) != 0U ||
	    pa + config->ram_size > (1ULL << (parange == 2U ? 40U :
					    parange == 1U ? 36U : 32U))) {
		ret = -ENOTSUP;
		goto out;
	}
	memset(&single_vcpu, 0, sizeof(single_vcpu));
	memset(stage2_l1, 0, sizeof(stage2_l1));
	memset(stage2_l2, 0, sizeof(stage2_l2));
	memset(guest_ram, 0, config->ram_size);
	for (size_t offset = 0U; offset < config->ram_size; offset += BLOCK_SIZE) {
		uint64_t ipa = config->ram_ipa + offset;
		size_t l1 = ipa >> 30;
		size_t l2 = (ipa >> 21) & 511U;

		stage2_l1[l1] = k_mem_phys_addr(stage2_l2[l1]) | 3U;
		stage2_l2[l1][l2] = (pa + offset) | S2_RAM_ATTRIBUTES;
	}
	single_vm.ram = (struct zhv_ram){guest_ram, config->ram_ipa, config->ram_size};
	single_vm.clock.frequency_hz = read_cntfrq_el0();
	single_vm.clock.counter_offset = read_cntpct_el0();
	single_vcpu.context.guest_vtcr = BIT64(31) | (parange << 16) | (3U << 12) |
					 (1U << 10) | (1U << 8) | (1U << 6) | 32U;
	single_vcpu.context.guest_vttbr = BIT64(48) | k_mem_phys_addr(stage2_l1);
	single_vcpu.context.guest_offset = single_vm.clock.counter_offset;
	invalidate_stage2();
	IRQ_CONNECT(ARM_TIMER_VIRTUAL_IRQ, ARM_TIMER_VIRTUAL_PRIO,
		    virtual_timer_irq, NULL, ARM_TIMER_VIRTUAL_FLAGS);
	arm_gic_irq_disable(ARM_TIMER_VIRTUAL_IRQ);
	arm_gic_irq_clear_pending(ARM_TIMER_VIRTUAL_IRQ);
	k_sem_reset(&wake_event);
	atomic_set(&single_vm.allocated, 1);
	k_timer_start(&preemption_timer, K_MSEC(1), K_MSEC(1));
	*vm = &single_vm;
	*ram = single_vm.ram;
out:
	k_mutex_unlock(&operation_lock);
	return ret;
}

int zhv_vm_get_clock(struct zhv_vm *vm, struct zhv_clock *clock)
{
	int ret = take_lock();

	if (ret != 0) {
		return ret;
	}
	if (!vm_valid(vm) || clock == NULL) {
		ret = -EINVAL;
	} else {
		*clock = vm->clock;
	}
	k_mutex_unlock(&operation_lock);
	return ret;
}

int zhv_vcpu_create(struct zhv_vm *vm, struct zhv_vcpu **vcpu)
{
	int ret = take_lock();

	if (ret != 0) {
		return ret;
	}
	if (!vm_valid(vm) || vcpu == NULL) {
		ret = -EINVAL;
	} else if (atomic_get(&single_vcpu.allocated) != 0) {
		ret = -EBUSY;
	} else {
		atomic_set(&single_vcpu.allocated, 1);
		*vcpu = &single_vcpu;
	}
	k_mutex_unlock(&operation_lock);
	return ret;
}

int zhv_vcpu_get_state(struct zhv_vcpu *vcpu, struct zhv_a64_state *state)
{
	int ret = take_lock();

	if (ret != 0) {
		return ret;
	}
	if (!vcpu_valid(vcpu) || state == NULL) {
		ret = -EINVAL;
	} else {
		*state = vcpu->context.guest;
	}
	k_mutex_unlock(&operation_lock);
	return ret;
}

int zhv_vcpu_set_state(struct zhv_vcpu *vcpu, const struct zhv_a64_state *state)
{
	int ret = take_lock();

	if (ret != 0) {
		return ret;
	}
	if (!vcpu_valid(vcpu) || state == NULL || (state->pc & 3U) != 0U ||
	    ((state->pstate & 31U) != 0U && (state->pstate & 31U) != 4U &&
	     (state->pstate & 31U) != 5U) ||
	    (state->sctlr_el1 & (BIT64(24) | BIT64(25))) != 0U) {
		ret = -EINVAL;
	} else if (vcpu->awaiting_completion || vcpu->waiting) {
		ret = -EBUSY;
	} else {
		vcpu->context.guest = *state;
		vcpu->context.guest.cntv_ctl_el0 &= 3U;
		vcpu->initialized = true;
		vcpu->clean_ram = false;
		vcpu->failed = false;
		invalidate_stage2();
	}
	k_mutex_unlock(&operation_lock);
	return ret;
}

static uint64_t register_read(const struct zhv_a64_state *state, uint8_t rt)
{
	return rt < 31U ? state->x[rt] : 0U;
}

static void decode_exit(struct zhv_exit *exit)
{
	const struct zhv_switch_context *context = &single_vcpu.context;
	uint64_t syndrome = context->esr;
	uint32_t ec = (syndrome >> 26) & 63U;

	exit->reason = ZHV_EXIT_FAIL;
	exit->u.fail.category = context->vector;
	if (context->vector == 9U) {
		exit->reason = ZHV_EXIT_HOST_IRQ;
		exit->u.host_irq.intid = context->irq_id;
		return;
	}
	if (context->vector != 8U) {
		return;
	}
	switch (ec) {
	case 0x01:
		exit->reason = (syndrome & 1U) != 0U ? ZHV_EXIT_WFE : ZHV_EXIT_WFI;
		break;
	case 0x16:
	case 0x17:
		exit->reason = ec == 0x16U ? ZHV_EXIT_HVC : ZHV_EXIT_SMC;
		memcpy(exit->u.call.x, context->guest.x, sizeof(exit->u.call.x));
		exit->u.call.immediate = syndrome & 0xffffU;
		break;
	case 0x18:
		exit->reason = ZHV_EXIT_SYSREG;
		exit->u.sysreg.encoding = (((syndrome >> 20) & 3U) << 14) |
			(((syndrome >> 14) & 7U) << 11) | (((syndrome >> 10) & 15U) << 7) |
			(((syndrome >> 1) & 15U) << 3) | ((syndrome >> 17) & 7U);
		exit->u.sysreg.rt = (syndrome >> 5) & 31U;
		exit->u.sysreg.read = (syndrome & 1U) != 0U;
		exit->u.sysreg.value = register_read(&context->guest, exit->u.sysreg.rt);
		break;
	case 0x24: {
		uint32_t dfsc = syndrome & 63U;
		uint64_t ipa = ((context->hpfar & GENMASK64(39, 4)) << 8) |
			       (context->far & 4095U);

		if ((syndrome & BIT64(24)) == 0U ||
		    (syndrome & (BIT64(7) | BIT64(8) | BIT64(10))) != 0U ||
		    dfsc < 4U || dfsc > 7U || ipa >= BIT64(32) ||
		    (ipa >= single_vm.ram.guest_ipa &&
		     ipa - single_vm.ram.guest_ipa < single_vm.ram.size)) {
			return;
		}
		exit->reason = ZHV_EXIT_MMIO;
		exit->u.mmio.ipa = ipa;
		exit->u.mmio.rt = (syndrome >> 16) & 31U;
		exit->u.mmio.size = 1U << ((syndrome >> 22) & 3U);
		exit->u.mmio.write = (syndrome & BIT64(6)) != 0U;
		exit->u.mmio.sign_extend = (syndrome & BIT64(21)) != 0U;
		exit->u.mmio.reg_64 = (syndrome & BIT64(15)) != 0U;
		exit->u.mmio.value = register_read(&context->guest, exit->u.mmio.rt);
		break;
	}
	default:
		return;
	}
	exit->needs_completion = true;
}

int zhv_vcpu_run(struct zhv_vcpu *vcpu, const struct zhv_run_input *input,
		struct zhv_exit *exit)
{
	struct zhv_timer_sample sample;
	unsigned int key;
	int ret = take_lock();

	if (ret != 0) {
		return ret;
	}
	if (!vcpu_valid(vcpu) || input == NULL || exit == NULL || !vcpu->initialized) {
		ret = -EINVAL;
		goto out;
	}
	if (vcpu->awaiting_completion || vcpu->waiting) {
		ret = -EBUSY;
		goto out;
	}
	if (vcpu->failed) {
		ret = -EIO;
		goto out;
	}
	memset(exit, 0, sizeof(*exit));
	sample = timer_sample();
	if (sample.level != input->vtimer_level_seen) {
		exit->reason = ZHV_EXIT_TIMER;
		goto returned;
	}
	if (kick_generation() != vcpu->consumed_kick) {
		vcpu->consumed_kick = kick_generation();
		exit->reason = ZHV_EXIT_KICK;
		goto returned;
	}
	if (!vcpu->clean_ram) {
		prepare_ram();
	}
	key = irq_lock();
	/* Recheck after host work, before changing the GIC or loading guest state. */
	sample = timer_sample();
	if (sample.level != input->vtimer_level_seen) {
		irq_unlock(key);
		exit->reason = ZHV_EXIT_TIMER;
		goto returned;
	}
	arm_gic_irq_disable(ARM_TIMER_VIRTUAL_IRQ);
	arm_gic_irq_clear_pending(ARM_TIMER_VIRTUAL_IRQ);
	if (!sample.level) {
		arm_gic_irq_enable(ARM_TIMER_VIRTUAL_IRQ);
	}
	vcpu->context.guest_hcr = GUEST_HCR | (input->irq ? BIT64(7) : 0U) |
				  (input->fiq ? BIT64(6) : 0U);
	arch_flush_local_fpu();
	zhv_enter_asm(&vcpu->context);
	/* Assembly restored host pointers, vectors and FP traps before reaching C. */
	arm_gic_irq_disable(ARM_TIMER_VIRTUAL_IRQ);
	arm_gic_irq_clear_pending(ARM_TIMER_VIRTUAL_IRQ);
	irq_unlock(key);
	decode_exit(exit);
	vcpu->failed = exit->reason == ZHV_EXIT_FAIL;
returned:
	exit->seq = ++vcpu->exit_seq;
	exit->kick_seq = kick_generation();
	exit->pc = vcpu->context.guest.pc;
	exit->pstate = vcpu->context.guest.pstate;
	exit->esr = vcpu->context.esr;
	exit->far = vcpu->context.far;
	exit->hpfar = vcpu->context.hpfar;
	exit->vtimer = timer_sample();
	vcpu->awaiting_completion = exit->needs_completion;
	if (exit->needs_completion) {
		vcpu->pending = *exit;
	}
out:
	k_mutex_unlock(&operation_lock);
	return ret;
}

int zhv_vcpu_complete(struct zhv_vcpu *vcpu, const struct zhv_completion *completion)
{
	struct zhv_a64_state *state;
	struct zhv_exit *exit;
	uint8_t count = 0U;
	uint8_t rt = 31U;
	uint64_t value;
	int ret = take_lock();

	if (ret != 0) {
		return ret;
	}
	if (!vcpu_valid(vcpu) || completion == NULL) {
		ret = -EINVAL;
		goto out;
	}
	exit = &vcpu->pending;
	state = &vcpu->context.guest;
	if (!vcpu->awaiting_completion || completion->exit_seq != exit->seq) {
		ret = -ESTALE;
		goto out;
	}
	if (exit->reason == ZHV_EXIT_HVC || exit->reason == ZHV_EXIT_SMC) {
		count = 4U;
	} else if (exit->reason == ZHV_EXIT_MMIO && !exit->u.mmio.write) {
		count = 1U;
		rt = exit->u.mmio.rt;
	} else if (exit->reason == ZHV_EXIT_SYSREG && exit->u.sysreg.read) {
		count = 1U;
		rt = exit->u.sysreg.rt;
	}
	if (completion->nr_values != count) {
		ret = -EINVAL;
		goto out;
	}
	if (count == 4U) {
		memcpy(state->x, completion->value, sizeof(completion->value));
	} else if (count == 1U && rt != 31U) {
		value = completion->value[0];
		if (exit->reason == ZHV_EXIT_MMIO) {
			uint8_t bits = exit->u.mmio.size * 8U;

			if (bits < 64U) {
				value &= BIT64(bits) - 1U;
				if (exit->u.mmio.sign_extend) {
					value = (value ^ BIT64(bits - 1U)) - BIT64(bits - 1U);
				}
			}
			if (!exit->u.mmio.reg_64) {
				value = (uint32_t)value;
			}
		}
		state->x[rt] = value;
	}
	if (exit->reason != ZHV_EXIT_HVC) {
		state->pc += 4U;
	}
	vcpu->awaiting_completion = false;
out:
	k_mutex_unlock(&operation_lock);
	return ret;
}

int zhv_vcpu_wait(struct zhv_vcpu *vcpu, uint64_t observed_kick_seq,
		  uint64_t host_deadline_cycles)
{
	k_timeout_t timeout = K_FOREVER;
	uint64_t deadline = host_deadline_cycles;
	uint64_t now;
	struct zhv_timer_sample sample;
	int ret = take_lock();

	if (ret != 0) {
		return ret;
	}
	if (!vcpu_valid(vcpu) || !vcpu->initialized) {
		ret = -EINVAL;
		goto out;
	}
	if (vcpu->awaiting_completion || vcpu->waiting) {
		ret = -EBUSY;
		goto out;
	}
	sample = timer_sample();
	if (sample.level || kick_generation() != observed_kick_seq) {
		goto out;
	}
	if ((sample.ctl & 3U) == 1U && sample.cval <= UINT64_MAX - single_vm.clock.counter_offset) {
		deadline = MIN(deadline, sample.cval + single_vm.clock.counter_offset);
	}
	now = read_cntpct_el0();
	if (deadline <= now) {
		goto out;
	}
	if (deadline != UINT64_MAX) {
		/* A bounded spurious wake also avoids overflow converting distant deadlines. */
		uint64_t delta = MIN(deadline - now, k_ms_to_cyc_ceil64(1000));

		timeout = K_TICKS(k_cyc_to_ticks_ceil64(delta));
	}
	vcpu->waiting = true;
	k_mutex_unlock(&operation_lock);
	/* The semaphore retains a kick between the generation check and this call. */
	ret = k_sem_take(&wake_event, timeout);
	if (ret == -EAGAIN || ret == -EBUSY) {
		ret = 0;
	}
	int lock_ret = k_mutex_lock(&operation_lock, K_FOREVER);

	__ASSERT_NO_MSG(lock_ret == 0);
	if (lock_ret != 0) {
		return lock_ret;
	}
	vcpu->waiting = false;
out:
	k_mutex_unlock(&operation_lock);
	return ret;
}

void zhv_vcpu_kick(struct zhv_vcpu *vcpu)
{
	if (vcpu_valid(vcpu)) {
		atomic_inc(&vcpu->kick_seq);
		k_sem_give(&wake_event);
	}
}

int zhv_vm_destroy(struct zhv_vm *vm)
{
	int ret = take_lock();

	if (ret != 0) {
		return ret;
	}
	if (!vm_valid(vm)) {
		ret = -EINVAL;
	} else if (single_vcpu.waiting) {
		ret = -EBUSY;
	} else {
		k_timer_stop(&preemption_timer);
		arm_gic_irq_disable(ARM_TIMER_VIRTUAL_IRQ);
		arm_gic_irq_clear_pending(ARM_TIMER_VIRTUAL_IRQ);
		invalidate_stage2();
		atomic_clear(&single_vcpu.allocated);
		atomic_clear(&single_vm.allocated);
	}
	k_mutex_unlock(&operation_lock);
	return ret;
}
