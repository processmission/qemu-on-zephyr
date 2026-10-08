# SPDX-License-Identifier: GPL-2.0-or-later
zephyr_ld_options(-Wl,--wrap=exit)
zephyr_library_include_directories(
  "${qemu_root}/target/arm"
  "${qemu_root}/linux-user/aarch64"
  "${qemu_root}/linux-user"
  "${qemu_root}/linux-user/include"
  "${qemu_root}/common-user"
  "${qemu_root}/subprojects/zlib"
)
execute_process(
  COMMAND sh "${qemu_root}/linux-user/aarch64/syscallhdr.sh"
    "${qemu_root}/linux-user/aarch64/syscall_64.tbl"
    "${qemu_generated}/syscall_nr.h" "common,64,renameat,rlimit,memfd_secret"
  COMMAND_ERROR_IS_FATAL ANY
)
set(qemu_user_sources
  accel/accel-common.c accel/accel-user.c
  hw/core/cpu-common.c hw/core/cpu-user.c hw/core/qdev-user.c
  hw/core/irq.c hw/core/clock.c hw/core/qdev-clock.c
  hw/core/vmstate-if.c
  stubs/hotplug-stubs.c stubs/cpu-synchronize-state.c stubs/cpu-destroy-address-spaces.c
  cpu-common.c
  disas/disas-common.c
  util/lockcnt.c util/cacheflush.c util/guest-random.c util/path.c util/getauxval.c
  target/arm/cpu.c target/arm/cpu64.c target/arm/cpu-max.c
  target/arm/mmuidx.c target/arm/helper.c target/arm/vfp_fpscr.c
  target/arm/cpregs-gcs.c target/arm/cpregs-pmu.c
  target/arm/cpregs-omap-stub.c target/arm/el2-stubs.c
  target/arm/debug_helper.c
  linux-user/elfload.c linux-user/linuxload.c linux-user/uaccess.c
  linux-user/aarch64/elfload.c
  ports/zephyr/user.c ports/zephyr/user-memory.c ports/zephyr/user-syscall.c
  ports/zephyr/console-uart.c ports/zephyr/file.c ports/zephyr/payload-fs.c
)
foreach(source_file IN LISTS qemu_user_sources)
  zephyr_library_sources("${qemu_root}/${source_file}")
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/tcg.cmake")
foreach(zlib_source adler32 crc32)
  zephyr_library_sources("${qemu_root}/subprojects/zlib/${zlib_source}.c")
  set_source_files_properties("${qemu_root}/subprojects/zlib/${zlib_source}.c"
    PROPERTIES COMPILE_DEFINITIONS Z_SOLO)
endforeach()
