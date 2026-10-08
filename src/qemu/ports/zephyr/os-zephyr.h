/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_OS_ZEPHYR_H
#define QEMU_OS_ZEPHYR_H

#include <float.h>
#include <setjmp.h>

#ifdef CONFIG_QEMU_USER
#include <sys/mman.h>
#include <netinet/in.h>
ssize_t qemu_zephyr_pread(int fd, void *buffer, size_t size, off_t offset);
ssize_t qemu_zephyr_pwrite(int fd, const void *buffer, size_t size, off_t offset);
#define pread qemu_zephyr_pread
#define pwrite qemu_zephyr_pwrite
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0x4000
#endif
#ifndef MAP_ANON
#define MAP_ANON MAP_ANONYMOUS
#endif
struct CPUState;
void qemu_zephyr_user_probe(struct CPUState *cpu, uint64_t address, int size,
                            int access, uintptr_t retaddr);
#endif

extern __thread jmp_buf *qemu_zephyr_exit_env;
extern __thread int qemu_zephyr_exit_status;

#ifdef CONFIG_QEMU_TCG
/* Protection bits used internally by TCG's region allocator. */
#ifndef CONFIG_QEMU_USER
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#endif
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
