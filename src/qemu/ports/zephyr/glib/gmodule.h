/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2026 Zephyr QEMU port contributors
 *
 * Minimal <gmodule.h> compatibility include for the Zephyr GLib layer.
 *
 * QEMU's include/qemu/transactions.h includes <gmodule.h> even when the
 * module loader is disabled.  The declarations below keep that header
 * parseable.  No module-loader implementation is provided by this layer, so
 * any reachable use fails at link time rather than silently doing nothing.
 */

#ifndef __G_MODULE_H__
#define __G_MODULE_H__

#include <glib.h>

G_BEGIN_DECLS

typedef struct _GModule GModule;

typedef enum {
    G_MODULE_BIND_LAZY  = 1 << 0,
    G_MODULE_BIND_LOCAL = 1 << 1,
    G_MODULE_BIND_MASK  = 0x03
} GModuleFlags;

gboolean g_module_supported(void);
GModule *g_module_open(const gchar *file_name, GModuleFlags flags);
gboolean g_module_close(GModule *module);
void     g_module_make_resident(GModule *module);
const gchar *g_module_error(void);
gboolean g_module_symbol(GModule *module, const gchar *symbol_name,
                         gpointer *symbol);

G_END_DECLS

#endif /* __G_MODULE_H__ */
