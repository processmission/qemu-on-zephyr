/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2026 Zephyr QEMU port contributors
 *
 * Minimal <glib/gstdio.h> compatibility include for the Zephyr GLib layer.
 *
 * QEMU's include/glib-compat.h includes this header unconditionally.  The
 * QOM/qdev/MemoryRegion/PL011 slice does not use the g_* stdio wrappers, so
 * this file only provides the include structure.  Any real use of an
 * unimplemented gstdio entry point will fail at compile or link time.
 */

#ifndef __G_STDIO_H__
#define __G_STDIO_H__

#include <glib.h>

G_BEGIN_DECLS

G_END_DECLS

#endif /* __G_STDIO_H__ */
