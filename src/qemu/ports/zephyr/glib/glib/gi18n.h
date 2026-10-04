/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2026 Zephyr QEMU port contributors
 *
 * Minimal <glib/gi18n.h> compatibility include.
 */

#ifndef __G_I18N_H__
#define __G_I18N_H__

#include <glib.h>

#ifndef _
#define _(String) (String)
#endif
#ifndef N_
#define N_(String) (String)
#endif
#ifndef C_
#define C_(Context, String) (String)
#endif

#endif /* __G_I18N_H__ */
