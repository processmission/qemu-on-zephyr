/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2026 Zephyr QEMU port contributors
 *
 * Minimal <glib-unix.h> compatibility include.
 *
 * G_OS_UNIX is deliberately not defined by this port: the Zephyr SDK does
 * not provide the full glib-unix host dependency set, and the QOM slice does
 * not use these wrappers.  If QEMU's POSIX port later needs them, they must
 * be implemented by the Zephyr OS adapter rather than stubbed here.
 */

#ifndef __G_UNIX_H__
#define __G_UNIX_H__

#include <glib.h>

G_BEGIN_DECLS

G_END_DECLS

#endif /* __G_UNIX_H__ */
