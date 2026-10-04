# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(arm64_fpu_sharing)

set(generic_dir "${ZEPHYR_BASE}/tests/kernel/fpu_sharing/generic")
target_compile_definitions(app PRIVATE PI_NUM_ITERATIONS=70000)
target_sources(app PRIVATE
    "${generic_dir}/src/main.c"
    "${generic_dir}/src/load_store.c"
    "${generic_dir}/src/pi.c"
    src/irq.c
    src/registers.S
)
