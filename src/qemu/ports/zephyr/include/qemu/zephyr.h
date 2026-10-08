/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_ZEPHYR_H
#define QEMU_ZEPHYR_H

#include <stdbool.h>
#include <stddef.h>

struct qemu_zephyr_options {
#ifdef CONFIG_QEMU_USER
    int argc;
    int envc;
    unsigned short argv[32];
    unsigned short envp[8];
    char strings[2048];
    bool trace;
    char cpu[32];
#else
    char machine[32];
    char accelerator[16];
    char cpu[32];
    char kernel[256];
    char initrd[256];
    char firmware[256];
    char append[1024];
#endif
};

int qemu_zephyr_pl011_probe(void);
int qemu_zephyr_linux_main(void);
int qemu_zephyr_parse_options(size_t argc, char **argv,
                             struct qemu_zephyr_options *options,
                             char *error, size_t error_size);
int qemu_zephyr_run(const struct qemu_zephyr_options *options);
bool qemu_zephyr_started(void);
void qemu_zephyr_request_stop(void);
void qemu_zephyr_console_input(const unsigned char *data, size_t length);
int qemu_zephyr_mount_payload(void);
#ifdef CONFIG_QEMU_SYSTEM
/* Copy counters on the QEMU owner after guest execution has stopped. */
void qemu_zephyr_get_stats(char *buffer, size_t capacity);
#endif

#endif
