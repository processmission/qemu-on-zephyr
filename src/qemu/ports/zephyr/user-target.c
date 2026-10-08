/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/target-info-impl.h"
#include "qemu/target-info-init.h"
#include "target/arm/cpu-qom.h"

static const TargetInfo target_info_aarch64_user = {
    .target_name = "aarch64",
    .target_arch = SYS_EMU_TARGET_AARCH64,
    .long_bits = 64,
    .cpu_type = TYPE_ARM_CPU,
    .endianness = ENDIAN_MODE_LITTLE,
    .page_bits_vary = false,
    .page_bits_init = 12,
};

target_info_init(target_info_aarch64_user)
