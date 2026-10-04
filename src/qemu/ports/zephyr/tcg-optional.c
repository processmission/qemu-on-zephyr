/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Disabled optional facilities for the single-owner A-profile TCG runtime. */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/mprotect.h"
#include "exec/icount.h"
#include "exec/replay-core.h"
#include "hw/intc/armv7m_nvic.h"

ICountMode use_icount = ICOUNT_DISABLED;
bool icount_align_option;

int64_t icount_get(void)
{
    g_assert_not_reached();
}
int64_t icount_get_raw(void)
{
    g_assert_not_reached();
}
int64_t icount_to_ns(int64_t count)
{
    g_assert_not_reached();
}
void icount_update(CPUState *cpu)
{
    g_assert_not_reached();
}

/* Match the upstream replay-disabled behavior; never fabricate replay data. */
bool replay_exception(void)
{
    assert(replay_mode == REPLAY_MODE_NONE);
    return true;
}
bool replay_has_exception(void)
{
    assert(replay_mode == REPLAY_MODE_NONE);
    return false;
}
bool replay_interrupt(void)
{
    assert(replay_mode == REPLAY_MODE_NONE);
    return true;
}
bool replay_has_interrupt(void)
{
    assert(replay_mode == REPLAY_MODE_NONE);
    return false;
}
bool replay_running_debug(void)
{
    assert(replay_mode == REPLAY_MODE_NONE);
    return false;
}
void replay_breakpoint(void)
{
    assert(replay_mode == REPLAY_MODE_NONE);
}
void replay_finish(void)
{
    assert(replay_mode == REPLAY_MODE_NONE);
}

bool qemu_log_in_addr_range(uint64_t addr)
{
    return true;
}
bool qemu_log_separate(void)
{
    return false;
}

size_t qemu_get_host_physmem(void)
{
    return DT_REG_SIZE(DT_CHOSEN(zephyr_sram));
}

int qemu_msync(void *addr, size_t length, int fd)
{
    errno = ENOTSUP;
    return -1;
}

int qemu_mprotect_rwx(void *addr, size_t size)
{
    /* Code uses distinct RW and RX aliases; an RWX mapping is not offered. */
    return -ENOTSUP;
}

int qemu_mprotect_rw(void *addr, size_t size)
{
    return k_mem_update_flags(addr, size, K_MEM_CACHE_WB | K_MEM_PERM_RW);
}

int qemu_mprotect_none(void *addr, size_t size)
{
    /* TCG's optional guard-page optimization may be unavailable on this host. */
    return -ENOTSUP;
}

void do_common_semihosting(CPUState *cpu)
{
    g_assert_not_reached();
}

/* M-profile CPUs are not exposed by the ARM virt machine. */
bool armv7m_nvic_neg_prio_requested(NVICState *s, bool secure)
{
    g_assert_not_reached();
}
bool armv7m_nvic_can_take_pending_exception(NVICState *s)
{
    g_assert_not_reached();
}
bool armv7m_nvic_get_ready_status(NVICState *s, int irq, bool secure)
{
    g_assert_not_reached();
}
void armv7m_nvic_set_pending_derived(NVICState *s, int irq, bool secure)
{
    g_assert_not_reached();
}
void armv7m_nvic_set_pending_lazyfp(NVICState *s, int irq, bool secure)
{
    g_assert_not_reached();
}
