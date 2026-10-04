/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TESTS_ARCH_ARM64_FPU_SHARING_REGISTERS_H_
#define TESTS_ARCH_ARM64_FPU_SHARING_REGISTERS_H_

#define TEST_FP_REGS_SIZE (32 * 16)
#define TEST_FPCR_OFFSET TEST_FP_REGS_SIZE
#define TEST_FPSR_OFFSET (TEST_FPCR_OFFSET + 4)
#define TEST_THREAD_FP_PATTERN 0x35
#define TEST_ISR_FP_PATTERN 0xa7
#define TEST_THREAD_FPCR (1 << 22)
#define TEST_ISR_FPCR (3 << 22)
#define TEST_THREAD_FPSR (1 << 27)
#define TEST_ISR_FPSR 1

#ifndef _ASMLANGUAGE

#include <zephyr/irq_offload.h>
#include <stdint.h>

struct fp_registers {
	uint8_t regs[TEST_FP_REGS_SIZE];
	uint32_t fpcr;
	uint32_t fpsr;
};

uint32_t arm64_test_fp_timer_window(volatile uint32_t *progress, struct fp_registers *result,
				    uint64_t deadline);
uint32_t arm64_test_fp_offload_window(irq_offload_routine_t routine, struct fp_registers *result);
uint64_t arm64_test_fp_clobber(void);

#endif

#endif /* TESTS_ARCH_ARM64_FPU_SHARING_REGISTERS_H_ */
