/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/cutils.h"
#include "qemu/main-loop.h"
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
#include "accelerator.h"

#ifdef CONFIG_QEMU_EMBEDDED_PAYLOAD
extern const uint8_t qemu_guest_image_start[], qemu_guest_image_end[];
extern const uint8_t qemu_guest_initrd_start[], qemu_guest_initrd_end[];
#endif
void qemu_zephyr_console_poll(void);

static bool started;
static bool stop_requested;

bool qemu_zephyr_started(void)
{
    return qatomic_read(&started);
}

void qemu_zephyr_request_stop(void)
{
    qatomic_set(&stop_requested, true);
    if (qemu_zephyr_started()) {
        qemu_notify_event();
    }
}

int qemu_zephyr_mount_payload(void)
{
#ifdef CONFIG_QEMU_EMBEDDED_PAYLOAD
    return qemu_zephyr_payload_mount(qemu_guest_image_start,
                                     qemu_guest_image_end - qemu_guest_image_start,
                                     qemu_guest_initrd_start,
                                     qemu_guest_initrd_end - qemu_guest_initrd_start);
#else
    return 0;
#endif
}

static int run_guest(const struct qemu_zephyr_options *options)
{
    AccelClass *accel_class;
    AccelState *accel;
    MachineState *machine;
    int result;
    int exit_status = 0;
#ifdef CONFIG_QEMU_HOST_HEARTBEAT
    int64_t next_stats = 5 * NANOSECONDS_PER_SECOND;
#endif

    if (qemu_zephyr_started()) {
        return -EALREADY;
    }
    if (strcmp(options->machine, "zephyr-virt") != 0) {
        error_report("unsupported machine model: %s (use zephyr-virt)",
                     options->machine);
        return -EINVAL;
    }
    if (strcmp(options->cpu, "cortex-a53") != 0 &&
        strcmp(options->cpu, "cortex-a57") != 0 &&
        strcmp(options->cpu, "cortex-a72") != 0) {
        error_report("unsupported CPU model: %s (use cortex-a53/a57/a72)",
                     options->cpu);
        return -EINVAL;
    }

    module_call_init(MODULE_INIT_TARGET_INFO);
    target_info_qom_set_target();
    qemu_init_subsystems();
    qemu_init_clocks(qemu_timer_notify_cb);
    cpu_timers_init();
    qatomic_set(&started, true);
    machine = MACHINE(object_new(MACHINE_TYPE_NAME("zephyr-virt")));
    current_machine = machine;
    object_property_add_child(object_get_root(), "machine", OBJECT(machine));
    object_property_add_new_container(OBJECT(machine), "unattached");
    object_property_add_new_container(OBJECT(machine), "peripheral");
    object_property_add_new_container(OBJECT(machine), "peripheral-anon");
    object_property_add_child(machine_get_container("unattached"), "sysbus",
                               OBJECT(sysbus_get_default()));
    machine->cpu_type = g_strdup_printf("%s-arm-cpu", options->cpu);
#ifndef CONFIG_QEMU_NATIVE_PROBE_ONLY
    machine->kernel_filename = options->kernel[0] ? g_strdup(options->kernel) : NULL;
    machine->initrd_filename = options->initrd[0] ? g_strdup(options->initrd) : NULL;
    machine->firmware = options->firmware[0] ? g_strdup(options->firmware) : NULL;
    g_free(machine->kernel_cmdline);
    machine->kernel_cmdline = g_strdup(options->append);
#endif
    if (!set_preferred_target_page_bits(MACHINE_GET_CLASS(machine)->minimum_page_bits)) {
        return -EINVAL;
    }
    machine_memory_init();
    phase_advance(PHASE_MACHINE_CREATED);
    accel_class = accel_find(options->accelerator);
    assert(accel_class != NULL);
    accel = ACCEL(object_new_with_class(OBJECT_CLASS(accel_class)));
#ifdef CONFIG_QEMU_TCG
    object_property_set_str(OBJECT(accel), "thread", "single", &error_fatal);
    object_property_set_int(OBJECT(accel), "tb-size", CONFIG_QEMU_TCG_CACHE_SIZE_MIB,
                            &error_fatal);
#endif
    result = accel_init_machine(accel, machine);
    if (result != 0) {
        return result;
    }
    phase_advance(PHASE_ACCEL_CREATED);
    phase_advance(PHASE_LATE_BACKENDS_CREATED);
    machine_run_board_init(machine, NULL, &error_fatal);
    qdev_machine_creation_done();
    vm_start();
#ifdef CONFIG_QEMU_ZEPHYR_ACCEL
    if (zephyr_accel_probe(first_cpu, &error_fatal) != 0) {
        return -EIO;
    }
#endif
#ifdef CONFIG_QEMU_NATIVE_PROBE_ONLY
    printf("QEMU_NATIVE_PROBE_OK\n");
    return 0;
#endif
    qemu_system_reset(SHUTDOWN_CAUSE_GUEST_RESET);
    printf("Starting ARM64 guest using QEMU loaders and %s accelerator\n",
           current_accel_name());
    while (runstate_is_running()) {
        if (qatomic_read(&stop_requested)) {
            qemu_zephyr_cpu_stop();
            return 0;
        }
        if (qemu_zephyr_process_requests(&exit_status)) {
            qemu_zephyr_cpu_stop();
            return exit_status;
        }
        qemu_process_cpu_events_common(first_cpu);
        qemu_zephyr_console_poll();
        qemu_clock_run_all_timers();
        qemu_zephyr_quiesce();
#ifdef CONFIG_QEMU_HOST_HEARTBEAT
        if (qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) >= next_stats) {
            GString *stats = g_string_new(NULL);

            cpus_get_accel()->get_vcpu_stats(first_cpu, stats);
            printf("QEMU_ACCEL_STATS %s\n", stats->str);
            g_string_free(stats, true);
            next_stats += 5 * NANOSECONDS_PER_SECOND;
        }
#endif
        result = qemu_zephyr_cpu_exec(first_cpu);
        if (result < 0) {
            qemu_zephyr_cpu_stop();
            return result;
        }
        if (result == EXCP_HLT) {
            qemu_zephyr_console_poll();
            qemu_clock_run_all_timers();
            result = qemu_zephyr_cpu_wait(first_cpu,
                      timerlistgroup_deadline_ns(&main_loop_tlg));
            if (result < 0) {
                qemu_zephyr_cpu_stop();
                return result;
            }
        }
    }
    qemu_zephyr_cpu_stop();
    return 0;
}

int qemu_zephyr_run(const struct qemu_zephyr_options *options)
{
    jmp_buf exit_target;
    int result;

    /* Process-style loader exits return to the owning shell worker. */
    qemu_zephyr_exit_env = &exit_target;
    if (setjmp(exit_target) == 0) {
        result = run_guest(options);
    } else {
        if (qemu_zephyr_started()) {
            qemu_zephyr_cpu_stop();
        }
        result = -qemu_zephyr_exit_status;
    }
    qemu_zephyr_exit_env = NULL;
    return result;
}

int qemu_zephyr_linux_main(void)
{
    struct qemu_zephyr_options options = {
        .machine = CONFIG_QEMU_MACHINE_MODEL,
        .cpu = CONFIG_QEMU_CPU_MODEL,
#ifdef CONFIG_QEMU_TCG
        .accelerator = "tcg",
#else
        .accelerator = "zephyr",
#endif
        .kernel = "/guest/Image",
        .initrd = "/guest/initramfs.cpio.gz",
        .append = "console=ttyAMA0 earlycon=pl011,0x09000000 rdinit=/bin/sh nokaslr panic=-1",
    };
    int result = qemu_zephyr_mount_payload();

    return result != 0 ? result : qemu_zephyr_run(&options);
}
