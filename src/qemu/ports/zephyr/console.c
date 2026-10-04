/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/main-loop.h"
#include "chardev/char.h"
#include "qapi/error.h"
#include "console.h"

static Chardev *guest_console;

static bool console_open(Chardev *chr, ChardevBackend *backend, Error **errp)
{
    int result = qemu_zephyr_uart_open();

    if (result != 0) {
        error_setg(errp, "Zephyr console open failed: %d", result);
        return false;
    }
    return true;
}

static int console_write(Chardev *chr, const uint8_t *buffer, int length)
{
    return qemu_zephyr_uart_write(buffer, length);
}

static void console_class_init(ObjectClass *object_class, const void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(object_class);

    cc->chr_open = console_open;
    cc->chr_write = console_write;
}

static const TypeInfo console_type = {
    .name = "chardev-zephyr",
    .parent = TYPE_CHARDEV,
    .instance_size = sizeof(Chardev),
    .class_init = console_class_init,
};

static void console_register_types(void)
{
    type_register_static(&console_type);
}

type_init(console_register_types)

Chardev *qemu_zephyr_console_create(Error **errp)
{
    guest_console = qemu_chardev_new("guest-console", "chardev-zephyr", NULL, NULL, errp);
    return guest_console;
}

void qemu_zephyr_console_rx_notify(void)
{
    qemu_notify_event();
}

void qemu_zephyr_console_poll(void)
{
    uint8_t buffer[64];
    int capacity;
    size_t length;

    assert(bql_locked());
    while (guest_console != NULL &&
           (capacity = qemu_chr_be_can_write(guest_console)) > 0) {
        length = qemu_zephyr_uart_read(buffer, MIN(capacity, sizeof(buffer)));
        if (length == 0) {
            break;
        }
        qemu_chr_be_write(guest_console, buffer, length);
    }
}
