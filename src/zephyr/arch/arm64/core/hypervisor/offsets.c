/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gen_offset.h>
#include "context.h"

GEN_ABS_SYM_BEGIN(zhv_offset_symbols)

#define ZHV_SHARED_OFFSET(name) \
	GEN_ABSOLUTE_SYM(__zhv_a64_state_##name##_OFFSET, offsetof(struct zhv_a64_state, name));
ZHV_SHARED_REGS(ZHV_SHARED_OFFSET)
ZHV_SHARED_OFFSET(x)
ZHV_SHARED_OFFSET(pc)
ZHV_SHARED_OFFSET(pstate)
ZHV_SHARED_OFFSET(q)
ZHV_SHARED_OFFSET(fpcr)
ZHV_SHARED_OFFSET(fpsr)
ZHV_SHARED_OFFSET(cntv_cval_el0)
ZHV_SHARED_OFFSET(cntv_ctl_el0)

#define ZHV_CONTEXT_OFFSET(name) \
	GEN_ABSOLUTE_SYM(__zhv_switch_context_##name##_OFFSET, \
			 offsetof(struct zhv_switch_context, name));
#define ZHV_HOST_OFFSET(name) ZHV_CONTEXT_OFFSET(host_##name)
ZHV_HOST_EL2_REGS(ZHV_HOST_OFFSET)
ZHV_CONTEXT_OFFSET(guest)
ZHV_CONTEXT_OFFSET(host)
ZHV_CONTEXT_OFFSET(host_sp)
ZHV_CONTEXT_OFFSET(host_daif)
ZHV_CONTEXT_OFFSET(guest_hcr)
ZHV_CONTEXT_OFFSET(guest_vtcr)
ZHV_CONTEXT_OFFSET(guest_vttbr)
ZHV_CONTEXT_OFFSET(guest_offset)
ZHV_CONTEXT_OFFSET(guest_irq_lines)
ZHV_CONTEXT_OFFSET(host_lrs)
ZHV_CONTEXT_OFFSET(host_aprs)
ZHV_CONTEXT_OFFSET(esr)
ZHV_CONTEXT_OFFSET(far)
ZHV_CONTEXT_OFFSET(hpfar)
ZHV_CONTEXT_OFFSET(vector)
ZHV_CONTEXT_OFFSET(irq_id)

GEN_ABS_SYM_END
