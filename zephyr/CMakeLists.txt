# SPDX-License-Identifier: GPL-2.0-or-later
if(NOT CONFIG_QEMU)
  return()
endif()

get_filename_component(qoz_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(qemu_root "${qoz_root}/build/sources/qemu")
if(NOT EXISTS "${qoz_root}/build/sources/.prepared")
  message(FATAL_ERROR "Run make prepare in ${qoz_root} before configuring this module")
endif()
set(qemu_port "${qemu_root}/ports/zephyr")
set(qemu_generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(STRINGS "${qemu_root}/VERSION" qemu_version LIMIT_COUNT 1)
file(MAKE_DIRECTORY "${qemu_generated}/qapi" "${qemu_generated}/trace")
file(GLOB qemu_schema "${qemu_root}/qapi/*.json")
file(GLOB qemu_generators "${qemu_root}/scripts/qapi/*.py")
file(GLOB_RECURSE qemu_trace_generators "${qemu_root}/scripts/tracetool/*.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${qemu_schema}
  ${qemu_generators}
  ${qemu_trace_generators}
  "${qemu_root}/scripts/qapi-gen.py"
  "${qemu_root}/scripts/tracetool.py"
  "${qemu_root}/scripts/make-config-poison.sh"
  "${qemu_port}/config-target.h"
  "${qemu_root}/VERSION"
)
execute_process(
  COMMAND sh "${qemu_root}/scripts/make-config-poison.sh" "${qemu_port}/config-target.h"
  OUTPUT_FILE "${qemu_generated}/config-poison.h"
  COMMAND_ERROR_IS_FATAL ANY
)

# Generate from this source checkout; no host build products enter the target.
execute_process(
  COMMAND "${PYTHON_EXECUTABLE}" "${qemu_root}/scripts/qapi-gen.py"
          -o "${qemu_generated}/qapi" -b "${qemu_root}/qapi/qapi-schema.json"
  COMMAND_ERROR_IS_FATAL ANY
)
foreach(trace_dir qom qapi hw/core hw/char system util chardev target/arm hw/intc hw/arm hw/mem accel/tcg target/arm/tcg linux-user)
  if(NOT EXISTS "${qemu_root}/${trace_dir}/trace-events")
    continue()
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${qemu_root}/${trace_dir}/trace-events")
  string(REPLACE "/" "_" trace_group "${trace_dir}")
  execute_process(
    COMMAND "${PYTHON_EXECUTABLE}" "${qemu_root}/scripts/tracetool.py"
            --backend=nop "--group=${trace_group}" --format=h
            "${qemu_root}/${trace_dir}/trace-events"
            "${qemu_generated}/trace/trace-${trace_group}.h"
    COMMAND_ERROR_IS_FATAL ANY
  )
endforeach()
execute_process(
  COMMAND "${PYTHON_EXECUTABLE}" "${qemu_root}/scripts/tracetool.py"
          --backend=nop --group=root --format=h "${qemu_root}/trace-events"
          "${qemu_generated}/trace/trace-root.h"
  COMMAND_ERROR_IS_FATAL ANY
)

zephyr_include_directories("${qemu_port}/include")
zephyr_library_named(qemu_core)
zephyr_library_include_directories(
  "${qemu_port}"
  "${qemu_port}/glib"
  "${qemu_generated}"
  "${qemu_generated}/qapi"
  "${qemu_generated}/trace"
  "${qemu_root}"
  "${qemu_root}/include"
  "${qemu_root}/host/include/aarch64"
  "${qemu_root}/host/include/generic"
)
zephyr_library_compile_definitions(COMPILING_SYSTEM_VS_USER _GNU_SOURCE)
if(CONFIG_QEMU_USER)
  zephyr_library_compile_definitions(CONFIG_USER_ONLY CONFIG_LINUX_USER
    COMPILING_PER_TARGET "CONFIG_TARGET=\"config-target.h\"")
else()
  zephyr_library_compile_definitions(CONFIG_SOFTMMU)
endif()
zephyr_library_compile_definitions(
  "QEMU_VERSION=\"${qemu_version}\""
  "QEMU_FULL_VERSION=\"${qemu_version}-zephyr\""
)
zephyr_library_compile_options(-std=gnu11 -fno-strict-aliasing -fno-common)

set(qemu_sources
  qom/object.c
  qom/container.c
  qom/object_interfaces.c
  qom/compat-properties.c
  qom/qom-qobject.c
  hw/core/qdev.c
  hw/core/qdev-properties.c
  hw/core/qdev-properties-system.c
  hw/core/qdev-clock.c
  hw/core/bus.c
  hw/core/sysbus.c
  hw/core/clock.c
  hw/core/irq.c
  hw/core/gpio.c
  hw/core/resettable.c
  hw/core/resetcontainer.c
  hw/core/vmstate-if.c
  hw/core/qdev-fw.c
  hw/char/pl011.c
  system/memory.c
  system/physmem.c
  system/ram-block-attributes.c
  system/ram-discard-manager.c
  chardev/char-fe.c
  chardev/char.c
  chardev/char-mux.c
  target-info.c
  target-info-qom.c
  configs/targets/aarch64-softmmu.c
  page-vary-common.c
  util/module.c
  util/memalign.c
  util/event.c
  util/error.c
  util/error-report.c
  util/cutils.c
  util/qemu-option.c
  util/notify.c
  util/bitmap.c
  util/bitops.c
  util/bufferiszero.c
  util/qemu-print.c
  util/qemu-timer.c
  util/qemu-timer-common.c
  util/range.c
  util/qdist.c
  util/keyval.c
  util/unicode.c
  qapi/qapi-visit-core.c
  qapi/qapi-dealloc-visitor.c
  qapi/qapi-util.c
  qapi/string-input-visitor.c
  qapi/string-output-visitor.c
  qapi/qobject-input-visitor.c
  qapi/qobject-output-visitor.c
  qapi/qapi-forward-visitor.c
  qapi/qmp-event.c
  qapi/qmp-registry.c
  qapi/qmp-dispatch.c
  qobject/qobject.c
  qobject/qnull.c
  qobject/qbool.c
  qobject/qnum.c
  qobject/qstring.c
  qobject/qlist.c
  qobject/qdict.c
  qobject/json-lexer.c
  qobject/json-parser.c
  qobject/json-streamer.c
  qobject/json-writer.c
  qobject/qjson.c
  qobject/block-qdict.c
  hw/xen/xen_stubs.c
  accel/stubs/kvm-stub.c
  accel/stubs/xen-stub.c
  stubs/monitor-cur.c
  stubs/monitor-hmp.c
  stubs/hotplug-stubs.c
  stubs/qtest.c
  stubs/replay-mode.c
  stubs/replay-tools.c
  stubs/qapi-event-emit.c
  ports/zephyr/glib/glib.c
  ports/zephyr/os.c
)
if(CONFIG_QEMU_USER)
  list(REMOVE_ITEM qemu_sources configs/targets/aarch64-softmmu.c target-info-qom.c)
  list(APPEND qemu_sources ports/zephyr/user-target.c)
  list(FILTER qemu_sources EXCLUDE REGEX "^(system/|chardev/|hw/char/|hw/xen/|accel/stubs/|stubs/hotplug|hw/core/(qdev-properties-system|qdev-clock|sysbus|clock|irq|gpio|resetcontainer|qdev-fw|vmstate-if)\\.c)")
endif()
foreach(source_file IN LISTS qemu_sources)
  zephyr_library_sources("${qemu_root}/${source_file}")
endforeach()
zephyr_library_sources_ifdef(CONFIG_QEMU_PL011_PROBE "${qemu_port}/probe.c")
set_source_files_properties("${qemu_root}/configs/targets/aarch64-softmmu.c"
  PROPERTIES COMPILE_DEFINITIONS "COMPILING_PER_TARGET;CONFIG_TARGET=\"config-target.h\"")

file(GLOB qemu_qapi_sources
  "${qemu_generated}/qapi/qapi-types-*.c"
  "${qemu_generated}/qapi/qapi-visit-*.c"
  "${qemu_generated}/qapi/qapi-builtin-*.c"
)
zephyr_library_sources(${qemu_qapi_sources})
zephyr_library_sources("${qemu_generated}/qapi/qapi-events-qdev.c")

if(CONFIG_QEMU_USER)
  include("${CMAKE_CURRENT_LIST_DIR}/user.cmake")
endif()

if(CONFIG_QEMU_SYSTEM)
  zephyr_ld_options(-Wl,--wrap=exit)
  zephyr_library_include_directories("${qemu_root}/target/arm")
  set(qemu_native_common_sources
    accel/accel-common.c
    accel/accel-system.c
    accel/accel-irq.c
    hw/core/cpu-common.c
    hw/core/cpu-system.c
    hw/core/machine.c
    hw/core/machine-smp.c
    hw/core/numa.c
    cpu-common.c
    system/cpus.c
    system/cpu-timers.c
    hw/core/reset.c
    system/runstate.c
    system/runstate-action.c
    system/ioport.c
    system/bootdevice.c
    hw/mem/memory-device.c
    disas/disas-common.c
    util/lockcnt.c
    util/cacheflush.c
    util/datadir.c
    util/guest-random.c
    util/id.c
    ports/zephyr/bootstrap.c
    ports/zephyr/options.c
    ports/zephyr/accelerator.c
    ports/zephyr/console.c
    ports/zephyr/console-uart.c
    ports/zephyr/file.c
    ports/zephyr/payload-fs.c
  )
  set(qemu_native_arm_sources
    target/arm/cpu.c
    target/arm/cpu64.c
    target/arm/cpu-max.c
    target/arm/cpu-irq.c
    target/arm/mmuidx.c
    target/arm/helper.c
    target/arm/vfp_fpscr.c
    target/arm/cpregs-gcs.c
    target/arm/cpregs-pmu.c
    target/arm/cpregs-omap.c
    target/arm/cortex-regs.c
    target/arm/debug_helper.c
    target/arm/arm-powerctl.c
    target/arm/ptw.c
    target/arm/kvm-stub.c
    hw/intc/arm_gicv3.c
    hw/intc/arm_gicv3_common.c
    hw/intc/arm_gicv3_cpuif.c
    hw/intc/arm_gicv3_cpuif_common.c
    hw/intc/arm_gicv3_dist.c
    hw/intc/arm_gicv3_redist.c
    hw/arm/boot.c
    hw/core/loader.c
    system/device_tree.c
    ports/zephyr/machine.c
  )
  foreach(source_file IN LISTS qemu_native_common_sources qemu_native_arm_sources)
    zephyr_library_sources("${qemu_root}/${source_file}")
  endforeach()
  if(CONFIG_QEMU_ZEPHYR_ACCEL)
    zephyr_library_sources(
      "${qemu_root}/accel/zephyr/zephyr-accel.c"
      "${qemu_root}/accel/zephyr/probe.c"
      "${qemu_root}/accel/zephyr/probe.S"
      "${qemu_root}/target/arm/zephyr.c"
      "${qemu_root}/target/arm/tcg-stubs.c"
      "${qemu_root}/target/arm/gicv5-stubs.c"
    )
    set_source_files_properties(
      "${qemu_root}/target/arm/zephyr.c" "${qemu_root}/accel/zephyr/probe.c"
      PROPERTIES COMPILE_DEFINITIONS "COMPILING_PER_TARGET;CONFIG_TARGET=\"config-target.h\"")
  endif()
  if(CONFIG_QEMU_TCG)
    include("${CMAKE_CURRENT_LIST_DIR}/tcg.cmake")
  endif()
  zephyr_library_sources("${qemu_generated}/qapi/qapi-events-run-state.c")
  if(CONFIG_QEMU_EMBEDDED_PAYLOAD)
    set(qemu_kernel "${qoz_root}/downloads/tuxrun-arm64-Image")
    set(qemu_initrd "${qoz_root}/downloads/generic-arm64-rootfs.cpio.gz")
    configure_file("${qemu_port}/payload.S.in" "${qemu_generated}/payload.S" @ONLY)
    set_source_files_properties("${qemu_generated}/payload.S"
      PROPERTIES OBJECT_DEPENDS "${qemu_kernel};${qemu_initrd}")
    zephyr_library_sources("${qemu_generated}/payload.S")
  endif()
  foreach(source_file IN LISTS qemu_native_arm_sources)
    set_source_files_properties("${qemu_root}/${source_file}"
      PROPERTIES COMPILE_DEFINITIONS "COMPILING_PER_TARGET;CONFIG_TARGET=\"config-target.h\"")
  endforeach()
  zephyr_library_include_directories(
    "${qemu_root}/subprojects/dtc/libfdt"
    "${qemu_root}/subprojects/zlib"
  )
  foreach(fdt_source fdt fdt_ro fdt_rw fdt_sw fdt_wip fdt_strerror fdt_empty_tree fdt_addresses)
    zephyr_library_sources("${qemu_root}/subprojects/dtc/libfdt/${fdt_source}.c")
  endforeach()
  foreach(zlib_source adler32 crc32 inflate inftrees inffast zutil)
    zephyr_library_sources("${qemu_root}/subprojects/zlib/${zlib_source}.c")
    set_source_files_properties("${qemu_root}/subprojects/zlib/${zlib_source}.c"
      PROPERTIES COMPILE_DEFINITIONS Z_SOLO)
  endforeach()
endif()
