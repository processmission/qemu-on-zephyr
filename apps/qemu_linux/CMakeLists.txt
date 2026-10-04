# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
if(NOT DEFINED EXTRA_CONF_FILE)
  set(EXTRA_CONF_FILE native.conf)
endif()
list(APPEND ZEPHYR_EXTRA_MODULES "${CMAKE_CURRENT_SOURCE_DIR}/../..")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(qemu_linux)
target_sources(app PRIVATE src/main.c)
