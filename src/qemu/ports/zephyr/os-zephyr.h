/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_OS_ZEPHYR_H
#define QEMU_OS_ZEPHYR_H

#include <float.h>

#ifdef CONFIG_QEMU_TCG
#include <setjmp.h>
/* Protection bits used internally by TCG's region allocator. */
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
/* TCG's three jump sites request no signal-mask save (savesigs == 0). */
typedef jmp_buf sigjmp_buf;
#define sigsetjmp(env, savesigs) (assert((savesigs) == 0), setjmp(env))
#define siglongjmp(env, value) longjmp(env, value)
#endif

#ifndef SSIZE_MAX
#define SSIZE_MAX ((ssize_t)(SIZE_MAX >> 1))
#endif

void qemu_flockfile(FILE *file);
void qemu_funlockfile(FILE *file);
void qemu_zephyr_os_init(void);
void qemu_zephyr_quiesce(void);
bool qemu_zephyr_process_requests(int *status);

#endif
