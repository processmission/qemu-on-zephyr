/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_ZEPHYR_CONFIG_TARGET_H
#define QEMU_ZEPHYR_CONFIG_TARGET_H

#ifdef CONFIG_QEMU_USER
#define CONFIG_USER_ONLY 1
#define CONFIG_LINUX_USER 1
#else
#define CONFIG_SOFTMMU 1
#define CONFIG_SYSTEM_ONLY 1
#define TARGET_NEED_FDT 1
#endif
#define TARGET_AARCH64 1
#define TARGET_ARM 1
#define TARGET_ARCH AARCH64
#define TARGET_BIG_ENDIAN 0
#define TARGET_LONG_BITS 64
#define TARGET_NAME "aarch64"

#ifdef CONFIG_QEMU_TCG
#define CONFIG_TCG 1
#endif

#endif
