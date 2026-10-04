# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

zephyr_constants_library(
  NAME zhv_offsets
  SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/offsets.c"
  INCLUDES "${ZEPHYR_BASE}/arch/common/include"
)

zephyr_library()
zephyr_library_sources(
  executor.c
  switch.S
)
zephyr_library_compile_options($<$<COMPILE_LANGUAGE:C>:-mgeneral-regs-only>)
zephyr_library_include_directories("${ZEPHYR_BASE}/kernel/include")
