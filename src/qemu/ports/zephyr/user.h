/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_ZEPHYR_USER_H
#define QEMU_ZEPHYR_USER_H

extern bool user_exited;
extern int user_exit_status;
extern bool user_trace;
extern bool user_stop_requested;
void qemu_zephyr_user_memory_reset(void);
void qemu_zephyr_user_syscall_reset(abi_ulong brk);
void qemu_zephyr_user_syscall_close(void);

#endif
