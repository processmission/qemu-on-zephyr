/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Zephyr ARM64 host capabilities, independent of the build host. */
#ifndef QEMU_ZEPHYR_CONFIG_HOST_H
#define QEMU_ZEPHYR_CONFIG_HOST_H

/* POSIX headers pull these in; give QEMU its own macro namespace afterwards. */
#include <zephyr/kernel.h>
#undef likely
#undef unlikely
#undef IS_ENABLED
#undef IS_EMPTY

#define CONFIG_ZEPHYR 1
#define CONFIG_INT128 1
#define CONFIG_INT128_TYPE 1
#define CONFIG_ATOMIC64 1
#define CONFIG_BDRV_RO_WHITELIST
#define CONFIG_BDRV_RW_WHITELIST
#define SIZEOF_VOID_P 8
#define CONFIG_QEMU_DATADIR "/qemu"
#define CONFIG_QEMU_CONFDIR "/qemu"
#define CONFIG_QEMU_LOCALSTATEDIR "/qemu"
#define CONFIG_HOST_DSOSUF ".so"
#define CONFIG_PREFIX "/qemu"
#define CONFIG_BINDIR "/qemu"
#define CONFIG_QEMU_FIRMWAREPATH

#ifdef CONFIG_QEMU_SYSTEM
#define CONFIG_FDT 1
#endif

#ifdef CONFIG_QEMU_TCG
#define CONFIG_TCG 1
/* Select QEMU's own balanced tree implementation for this GLib subset. */
#define HAVE_GLIB_WITH_SLICE_ALLOCATOR 1
#endif

#endif
