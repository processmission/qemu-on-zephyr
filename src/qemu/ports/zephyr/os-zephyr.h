/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_OS_ZEPHYR_H
#define QEMU_OS_ZEPHYR_H

#ifndef SSIZE_MAX
#define SSIZE_MAX ((ssize_t)(SIZE_MAX >> 1))
#endif

void qemu_flockfile(FILE *file);
void qemu_funlockfile(FILE *file);
void qemu_zephyr_os_init(void);
void qemu_zephyr_quiesce(void);
bool qemu_zephyr_process_requests(int *status);

#endif
