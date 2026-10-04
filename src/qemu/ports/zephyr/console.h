/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_ZEPHYR_CONSOLE_H
#define QEMU_ZEPHYR_CONSOLE_H

#include <stddef.h>
#include <stdint.h>

int qemu_zephyr_uart_open(void);
int qemu_zephyr_uart_write(const uint8_t *data, size_t length);
size_t qemu_zephyr_uart_read(uint8_t *data, size_t capacity);
void qemu_zephyr_console_rx_notify(void);

#endif
