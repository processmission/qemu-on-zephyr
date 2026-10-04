/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/target-info-qom.h"
#include "qemu/target-info.h"
#include "qemu/zephyr.h"
#include "qom/object.h"
#include "hw/core/cpu.h"
#include "hw/core/boards.h"
#include "hw/core/sysbus.h"
#include "qemu/accel.h"
#include "accel/accel-cpu-ops.h"
#include "qemu/timer.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "exec/cpu-common.h"
#include "exec/page-vary.h"
#include "system/cpus.h"
#include "system/cpu-timers.h"
#include "system/memory.h"
#include "system/physmem.h"
#include "system/address-spaces.h"
#include "system/memory-internal.h"
#include "system/system.h"
#include "system/runstate.h"
#include "system/zephyr.h"
#include "payload-fs.h"

extern const uint8_t qemu_guest_image_start[], qemu_guest_image_end[];
extern const uint8_t qemu_guest_initrd_start[], qemu_guest_initrd_end[];
void qemu_zephyr_console_poll(void);

int qemu_zephyr_linux_main(void)
{
    AccelClass *accel_class;
    AccelState *accel;
    MachineState *machine;
    int result;
    int exit_status = 0;
#ifdef CONFIG_QEMU_HOST_HEARTBEAT
    int64_t next_stats = 5 * NANOSECONDS_PER_SECOND;
    unsigned int stats_reports = 0;
#endif

    module_call_init(MODULE_INIT_TARGET_INFO);
    target_info_qom_set_target();
    qemu_init_subsystems();
    qemu_init_clocks(qemu_timer_notify_cb);
    cpu_timers_init();
#ifndef CONFIG_QEMU_NATIVE_PROBE_ONLY
    result = qemu_zephyr_payload_mount(qemu_guest_image_start,
                                       qemu_guest_image_end - qemu_guest_image_start,
                                       qemu_guest_initrd_start,
                                       qemu_guest_initrd_end - qemu_guest_initrd_start);
    if (result != 0) {
        error_report("payload mount failed: %d", result);
        return result;
    }
#endif
    machine = MACHINE(object_new(MACHINE_TYPE_NAME("zephyr-virt")));
    current_machine = machine;
    object_property_add_child(object_get_root(), "machine", OBJECT(machine));
    object_property_add_new_container(OBJECT(machine), "unattached");
    object_property_add_new_container(OBJECT(machine), "peripheral");
    object_property_add_new_container(OBJECT(machine), "peripheral-anon");
    object_property_add_child(machine_get_container("unattached"), "sysbus",
                               OBJECT(sysbus_get_default()));
    machine->cpu_type = g_strdup(machine_default_cpu_type(machine));
#ifndef CONFIG_QEMU_NATIVE_PROBE_ONLY
    machine->kernel_filename = g_strdup("/guest/Image");
    machine->initrd_filename = g_strdup("/guest/initramfs.cpio.gz");
    g_free(machine->kernel_cmdline);
    machine->kernel_cmdline = g_strdup("console=ttyAMA0 earlycon=pl011,0x09000000 "
                                      "rdinit=/bin/sh nokaslr panic=-1");
#endif
    if (!set_preferred_target_page_bits(MACHINE_GET_CLASS(machine)->minimum_page_bits)) {
        return -EINVAL;
    }
    machine_memory_init();
    phase_advance(PHASE_MACHINE_CREATED);
    accel_class = accel_find("zephyr");
    assert(accel_class != NULL);
    accel = ACCEL(object_new_with_class(OBJECT_CLASS(accel_class)));
    result = accel_init_machine(accel, machine);
    if (result != 0) {
        return result;
    }
    phase_advance(PHASE_ACCEL_CREATED);
    phase_advance(PHASE_LATE_BACKENDS_CREATED);
    machine_run_board_init(machine, NULL, &error_fatal);
    qdev_machine_creation_done();
    vm_start();
    if (zephyr_accel_probe(first_cpu, &error_fatal) != 0) {
        return -EIO;
    }
#ifdef CONFIG_QEMU_NATIVE_PROBE_ONLY
    printf("QEMU_NATIVE_PROBE_OK\n");
    return 0;
#endif
    qemu_system_reset(SHUTDOWN_CAUSE_GUEST_RESET);
    printf("Starting ARM64 Linux using QEMU arm_load_kernel and zephyr accelerator\n");
    while (runstate_is_running()) {
        if (qemu_zephyr_process_requests(&exit_status)) {
            return exit_status;
        }
        qemu_process_cpu_events_common(first_cpu);
        qemu_zephyr_console_poll();
        qemu_clock_run_all_timers();
        qemu_zephyr_quiesce();
#ifdef CONFIG_QEMU_HOST_HEARTBEAT
        if (stats_reports < 3 && zephyr_clock_get_ns() >= next_stats) {
            GString *stats = g_string_new(NULL);

            cpus_get_accel()->get_vcpu_stats(first_cpu, stats);
            printf("QEMU_ACCEL_STATS %s\n", stats->str);
            g_string_free(stats, true);
            stats_reports++;
            next_stats += 5 * NANOSECONDS_PER_SECOND;
        }
#endif
        result = zephyr_cpu_exec(first_cpu);
        if (result < 0) {
            return result;
        }
        if (result == EXCP_HLT) {
            qemu_zephyr_console_poll();
            qemu_clock_run_all_timers();
            result = zephyr_cpu_wait(first_cpu,
                      timerlistgroup_deadline_ns(&main_loop_tlg));
            if (result < 0) {
                return result;
            }
        }
    }
    return 0;
}
