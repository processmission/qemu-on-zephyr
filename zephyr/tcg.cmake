# SPDX-License-Identifier: GPL-2.0-or-later
# Keep this list aligned with the pinned target/arm/tcg and accel/tcg sources.
zephyr_library_include_directories("${qemu_root}/tcg" "${qemu_root}/tcg/aarch64")
set(tcg_sources
  accel/tcg/cpu-exec.c
  accel/tcg/cpu-exec-common.c
  accel/tcg/tcg-runtime.c
  accel/tcg/tcg-runtime-gvec.c
  accel/tcg/tb-maint.c
  accel/tcg/tcg-all.c
  accel/tcg/tcg-stats.c
  accel/tcg/translate-all.c
  accel/tcg/translator.c
  accel/tcg/cputlb.c
  accel/tcg/watchpoint.c
  tcg/optimize.c
  tcg/region.c
  tcg/tcg.c
  tcg/tcg-common.c
  tcg/tcg-op.c
  tcg/tcg-op-ldst.c
  tcg/tcg-op-gvec.c
  tcg/tcg-op-vec.c
  fpu/softfloat.c
  util/qht.c
  util/qtree.c
  util/cpuinfo-aarch64.c
  util/interval-tree.c
  util/crc32c.c
  cpu-target.c
  system/watchpoint.c
  disas/disas-host.c
  disas/disas-target.c
  disas/objdump.c
  semihosting/stubs-all.c
  crypto/aes.c
  crypto/sm4.c
  crypto/clmul.c
  ports/zephyr/tcg.c
  ports/zephyr/tcg-optional.c
  target/arm/tcg/gengvec64.c
  target/arm/tcg/translate-a64.c
  target/arm/tcg/translate-sve.c
  target/arm/tcg/translate-sme.c
  target/arm/tcg/helper-a64.c
  target/arm/tcg/mte_helper.c
  target/arm/tcg/pauth_helper.c
  target/arm/tcg/sme_helper.c
  target/arm/tcg/sve_helper.c
  target/arm/tcg/vec_helper64.c
  target/arm/tcg/fp8_helper.c
  target/arm/tcg/arith_helper.c
  target/arm/tcg/crypto_helper.c
  target/arm/tcg/idau.c
  target/arm/tcg/cpu-max-v8.c
  target/arm/tcg/debug.c
  target/arm/tcg/gengvec.c
  target/arm/tcg/hflags.c
  target/arm/tcg/m_helper.c
  target/arm/tcg/mve_helper.c
  target/arm/tcg/neon_helper.c
  target/arm/tcg/op_helper.c
  target/arm/tcg/translate.c
  target/arm/tcg/translate-m-nocp.c
  target/arm/tcg/translate-mve.c
  target/arm/tcg/translate-neon.c
  target/arm/tcg/translate-vfp.c
  target/arm/tcg/vec_helper.c
  target/arm/tcg/vfp_helper.c
  target/arm/tcg/cpu64.c
  target/arm/tcg/cpu32.c
  target/arm/tcg/cpregs-at.c
  target/arm/tcg/psci.c
  target/arm/tcg/tlb_helper.c
  target/arm/tcg/tlb-insns.c
  target/arm/gicv5-stubs.c
)
if(CONFIG_QEMU_USER)
  list(REMOVE_ITEM tcg_sources
    accel/tcg/cputlb.c accel/tcg/watchpoint.c system/watchpoint.c
    ports/zephyr/tcg.c
    target/arm/tcg/cpu32.c target/arm/tcg/cpregs-at.c
    target/arm/tcg/psci.c target/arm/tcg/tlb-insns.c target/arm/gicv5-stubs.c)
  list(APPEND tcg_sources accel/tcg/user-exec.c accel/tcg/user-exec-stub.c)
endif()
foreach(source_file IN LISTS tcg_sources)
  zephyr_library_sources("${qemu_root}/${source_file}")
  set_source_files_properties("${qemu_root}/${source_file}"
    PROPERTIES COMPILE_DEFINITIONS "COMPILING_PER_TARGET;CONFIG_TARGET=\"config-target.h\"")
endforeach()

foreach(decoder a64 sve sme sme-fa64 vfp vfp-uncond neon-shared neon-dp neon-ls mve m-nocp a32 a32-uncond t32 t16)
  string(REPLACE "-" "_" decode_func "disas_${decoder}")
  set(decode_args "--decode=${decode_func}")
  if(decoder MATCHES "^(a64|sme-fa64|a32|a32-uncond|t32|t16)$")
    set(decode_args "--static-decode=${decode_func}")
  endif()
  if(decoder STREQUAL "t16")
    list(APPEND decode_args -w 16)
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${qemu_root}/target/arm/tcg/${decoder}.decode" "${qemu_root}/scripts/decodetree.py")
  execute_process(
    COMMAND "${PYTHON_EXECUTABLE}" "${qemu_root}/scripts/decodetree.py"
      ${decode_args} "${qemu_root}/target/arm/tcg/${decoder}.decode"
    OUTPUT_FILE "${qemu_generated}/decode-${decoder}.c.inc"
    COMMAND_ERROR_IS_FATAL ANY
  )
endforeach()
