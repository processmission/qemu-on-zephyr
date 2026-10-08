/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/atomic.h>
#include <qemu/zephyr.h>
#include "console.h"

static const struct device *const console_uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
RING_BUF_DECLARE(console_rx, 4096);
static atomic_t console_opened;

void qemu_zephyr_console_input(const unsigned char *data, size_t length)
{
    unsigned int key = irq_lock();

    ring_buf_put(&console_rx, data, length);
    irq_unlock(key);
    if (atomic_get(&console_opened)) {
        qemu_zephyr_console_rx_notify();
    }
}

#ifndef CONFIG_QEMU_SHELL
static void console_isr(const struct device *uart, void *unused)
{
    uint8_t data[64];
    int length;

    uart_irq_update(uart);
    while (uart_irq_rx_ready(uart)) {
        length = uart_fifo_read(uart, data, sizeof(data));
        if (length <= 0) {
            break;
        }
        ring_buf_put(&console_rx, data, length);
    }
    qemu_zephyr_console_rx_notify();
}
#endif

int qemu_zephyr_uart_open(void)
{
    if (!device_is_ready(console_uart)) {
        return -ENODEV;
    }
#ifndef CONFIG_QEMU_SHELL
    int result = uart_irq_callback_user_data_set(console_uart, console_isr, NULL);
    if (result != 0) {
        return result;
    }
    uart_irq_rx_enable(console_uart);
#endif
    atomic_set(&console_opened, 1);
    return 0;
}

int qemu_zephyr_uart_write(const uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        uart_poll_out(console_uart, data[i]);
    }
    return length;
}

size_t qemu_zephyr_uart_read(uint8_t *data, size_t capacity)
{
    unsigned int key = irq_lock();
    size_t length = ring_buf_get(&console_rx, data, capacity);

    irq_unlock(key);
    return length;
}
