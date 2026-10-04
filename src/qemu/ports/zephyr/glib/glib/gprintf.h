/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2026 Zephyr QEMU port contributors
 *
 * Minimal <glib/gprintf.h> compatibility include.
 *
 * The QOM/qdev/MemoryRegion/PL011 slice does not use g_printf().  The
 * declarations are intentionally left out until an implementation exists,
 * so callers get a compile-time diagnostic instead of a silent stub.
 */

#ifndef __G_PRINTF_H__
#define __G_PRINTF_H__

#include <glib.h>

#endif /* __G_PRINTF_H__ */
