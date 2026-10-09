/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_VIRTUALIZATION_ZHV_H_
#define ZEPHYR_INCLUDE_VIRTUALIZATION_ZHV_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @file Native AArch64 vCPU execution for an external device model.
 * CNTV/CNTVCT execute natively; CNTP/CNTPCT exit to the device model so its
 * physical timer and counter share the VM-relative virtual clock origin.
 */

struct zhv_vm;
struct zhv_vcpu;
struct zhv_ram;

struct zhv_vm_config {
	uint64_t ram_ipa;
	size_t ram_size;
	const struct zhv_ram *rom;
};

struct zhv_ram {
	void *host_va;
	uint64_t guest_ipa;
	size_t size;
};

struct zhv_clock {
	uint64_t frequency_hz;
	uint64_t counter_offset;
};

struct zhv_a64_state {
	uint64_t x[31];
	uint64_t pc, pstate, sp_el0, sp_el1, elr_el1, spsr_el1;
	uint64_t sctlr_el1, tcr_el1, ttbr0_el1, ttbr1_el1;
	uint64_t mair_el1, amair_el1, vbar_el1, contextidr_el1;
	uint64_t tpidr_el0, tpidrro_el0, tpidr_el1, par_el1, csselr_el1;
	uint64_t cpacr_el1, esr_el1, far_el1, afsr0_el1, afsr1_el1;
	uint64_t cntkctl_el1, cntv_cval_el0, cntv_ctl_el0;

	_Alignas(16) uint64_t q[32][2];
	uint32_t fpcr, fpsr;
};

struct zhv_timer_sample {
	uint64_t counter;
	uint64_t cval;
	uint32_t ctl;
	bool level;
};

enum zhv_exit_reason {
	ZHV_EXIT_MMIO,
	ZHV_EXIT_SYSREG,
	ZHV_EXIT_HVC,
	ZHV_EXIT_SMC,
	ZHV_EXIT_WFI,
	ZHV_EXIT_WFE,
	ZHV_EXIT_HOST_IRQ,
	ZHV_EXIT_TIMER,
	ZHV_EXIT_KICK,
	ZHV_EXIT_FAIL,
};

struct zhv_exit {
	uint64_t seq, kick_seq;
	enum zhv_exit_reason reason;
	bool needs_completion;
	uint64_t pc, pstate, esr, far, hpfar;
	struct zhv_timer_sample vtimer;
	union {
		struct {
			uint64_t ipa, value;
			uint8_t size, rt;
			bool write, sign_extend, reg_64;
		} mmio;
		struct {
			uint16_t encoding;
			uint8_t rt;
			bool read;
			uint64_t value;
		} sysreg;
		struct {
			uint64_t x[8];
			uint16_t immediate;
		} call;
		struct {
			uint32_t category;
		} fail;
		struct {
			uint32_t intid; /* Physical HPPIR snapshot; no acknowledgment. */
		} host_irq;
	} u;
};

struct zhv_run_input {
	bool irq, fiq;
	bool vtimer_level_seen;
};

struct zhv_completion {
	uint64_t exit_seq;
	uint64_t value[4];
	uint8_t nr_values;
};

/**
 * @brief Reserve the single VM and its exclusive RAM backing.
 * @param config RAM IPA and size, both aligned to 2 MiB.
 * Optional ROM has the same alignment, disjoint IPA, and a host mapping
 * valid until VM destruction. The guest can read it but cannot write or execute it.
 * @param vm Resulting VM handle.
 * @param ram Borrowed RAM mapping, valid until destruction.
 * @retval 0 VM created.
 * @retval -EINVAL Invalid arguments or caller context.
 * @retval -ENOTSUP Unsupported hardware.
 * @retval -ENOMEM RAM reservation too small.
 * @retval -EBUSY VM already allocated or another operation in progress.
 */
int zhv_vm_create(const struct zhv_vm_config *config, struct zhv_vm **vm,
		  struct zhv_ram *ram);

/**
 * @brief Read the VM's counter frequency and fixed CNTVOFF.
 * @param vm VM handle.
 * @param clock Clock description; host cycles come from k_cycle_get_64().
 * @retval 0 Clock returned.
 * @retval -EINVAL Invalid arguments or caller context.
 * @retval -EBUSY Another operation in progress.
 */
int zhv_vm_get_clock(struct zhv_vm *vm, struct zhv_clock *clock);

/**
 * @brief Create the single vCPU in a stopped, uninitialized state.
 * @param vm VM handle.
 * @param vcpu Resulting vCPU handle.
 * @retval 0 vCPU created.
 * @retval -EINVAL Invalid arguments or caller context.
 * @retval -EBUSY vCPU already created or another operation in progress.
 */
int zhv_vcpu_create(struct zhv_vm *vm, struct zhv_vcpu **vcpu);

/**
 * @brief Read stopped guest state, including an outstanding exit.
 * @param vcpu vCPU handle.
 * @param state Resulting architectural state.
 * @retval 0 State returned.
 * @retval -EINVAL Invalid arguments or caller context.
 * @retval -EBUSY Another operation in progress.
 */
int zhv_vcpu_get_state(struct zhv_vcpu *vcpu, struct zhv_a64_state *state);

/**
 * @brief Replace stopped state without an outstanding instruction exit.
 * @param vcpu vCPU handle.
 * @param state AArch64 EL0/EL1 state; CNTV_CTL.ISTATUS is ignored.
 * @retval 0 State installed.
 * @retval -EINVAL Invalid state, arguments, or caller context.
 * @retval -EBUSY An exit awaits completion or another operation is in progress.
 */
int zhv_vcpu_set_state(struct zhv_vcpu *vcpu, const struct zhv_a64_state *state);

/**
 * @brief Execute a guest until an exit, with host IRQs and scheduling available.
 * @param vcpu vCPU handle. Stopped state may be handed to a different thread.
 * @param input Current device-model IRQ/FIQ and observed virtual timer levels.
 * @param exit Result; synchronize its timer level before emulating the exit.
 * @retval 0 Guest exited; this includes ZHV_EXIT_FAIL.
 * @retval -EINVAL Invalid arguments, caller context, or uninitialized state.
 * @retval -EBUSY Concurrent operation or uncompleted instruction exit.
 * @retval -EIO vCPU has entered a terminal failure state.
 */
int zhv_vcpu_run(struct zhv_vcpu *vcpu, const struct zhv_run_input *input,
		struct zhv_exit *exit);

/**
 * @brief Complete an instruction exit exactly once and advance PC as required.
 * @param vcpu vCPU handle.
 * @param completion Sequence and read/call results. HVC does not advance PC.
 * @retval 0 Instruction completed.
 * @retval -EINVAL Invalid result count, arguments, or caller context.
 * @retval -ESTALE No pending exit or mismatched sequence.
 * @retval -EBUSY Another operation in progress.
 */
int zhv_vcpu_complete(struct zhv_vcpu *vcpu, const struct zhv_completion *completion);

/**
 * @brief Wait for a kick or the earlier of the guest and device-model deadlines.
 * @param vcpu vCPU handle with no outstanding completion.
 * @param observed_kick_seq Generation obtained from the last exit.
 * @param host_deadline_cycles Absolute host cycles, or UINT64_MAX.
 * @retval 0 A deadline, kick, or spurious wake permits the caller to check work.
 * @retval -EINVAL Invalid handle or caller context.
 * @retval -EBUSY Another operation or outstanding exit.
 */
int zhv_vcpu_wait(struct zhv_vcpu *vcpu, uint64_t observed_kick_seq,
		  uint64_t host_deadline_cycles);

/** @brief Wake the vCPU owner from thread or ISR context. @param vcpu vCPU handle. */
void zhv_vcpu_kick(struct zhv_vcpu *vcpu);

/**
 * @brief Release a stopped VM and invalidate borrowed RAM and vCPU handles.
 * @param vm VM handle.
 * @retval 0 VM destroyed.
 * @retval -EINVAL Invalid handle or caller context.
 * @retval -EBUSY Another operation or wait in progress.
 */
int zhv_vm_destroy(struct zhv_vm *vm);

#endif /* ZEPHYR_INCLUDE_VIRTUALIZATION_ZHV_H_ */
