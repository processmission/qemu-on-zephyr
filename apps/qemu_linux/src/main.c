/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <qemu/zephyr.h>
#include <pthread.h>
#include <zephyr/sys/atomic.h>

#ifdef CONFIG_QEMU_HOST_HEARTBEAT
static atomic_t observer_ticks;
static atomic_t observer_last;
static atomic_t observer_max_gap;

static void observer_record_gap(void)
{
    unsigned int key = irq_lock();
    uint32_t gap = k_uptime_get_32() - (uint32_t)atomic_get(&observer_last);

    if (gap > (uint32_t)atomic_get(&observer_max_gap)) {
        atomic_set(&observer_max_gap, gap);
    }
    irq_unlock(key);
}
#endif

#ifdef CONFIG_QEMU_SHELL
#include <zephyr/fs/fs.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>
#include <string.h>

K_SEM_DEFINE(qemu_request, 0, 1);
static atomic_t qemu_busy;
static atomic_t qemu_ready;
static struct qemu_zephyr_options guest_options;
static const struct shell *guest_shell;
#ifdef CONFIG_QEMU_SYSTEM
static char guest_stats[512];
#endif

static struct fs_mount_t image_mount = {
    .type = FS_EXT2,
    .mnt_point = "/images",
    .storage_dev = "GUESTFILES",
    .flags = FS_MOUNT_FLAG_READ_ONLY | FS_MOUNT_FLAG_NO_FORMAT | FS_MOUNT_FLAG_USE_DISK_ACCESS,
};

static void guest_input(const struct shell *shell, uint8_t *data, size_t length, void *unused)
{
    uint8_t *stop = memchr(data, 0x1d, length);

    qemu_zephyr_console_input(data, stop != NULL ? (size_t)(stop - data) : length);
    if (stop != NULL) {
        qemu_zephyr_request_stop();
    }
}

static int cmd_qemu(const struct shell *shell, size_t argc, char **argv)
{
    struct qemu_zephyr_options options;
    char error[384];
    int result;

    if (!atomic_get(&qemu_ready)) {
        shell_error(shell, "QEMU host is initializing");
        return -EAGAIN;
    }
    if (argc == 2 && strcmp(argv[1], "-status") == 0) {
        bool busy = atomic_get(&qemu_busy);
        const char *state = busy ? "running" :
                            qemu_zephyr_started() ? "exited" : "idle";

#ifdef CONFIG_QEMU_HOST_HEARTBEAT
        shell_print(shell, "QEMU state=%s observer_ticks=%lu observer_max_gap_ms=%lu",
                    state, (unsigned long)atomic_get(&observer_ticks),
                    (unsigned long)atomic_get(&observer_max_gap));
#else
        shell_print(shell, "QEMU state=%s", state);
#endif
#ifdef CONFIG_QEMU_SYSTEM
        if (!busy && guest_stats[0] != '\0') {
            shell_print(shell, "QEMU execution: %s", guest_stats);
        }
#endif
        return 0;
    }
    result = qemu_zephyr_parse_options(argc, argv, &options, error, sizeof(error));
    if (result != 0) {
        if (result < 0) {
            shell_error(shell, "%s", error);
        }
        return result < 0 ? result : 0;
    }
    if (!IS_ENABLED(CONFIG_QEMU_USER) && qemu_zephyr_started()) {
        shell_error(shell, "QEMU already initialized; use kernel reboot cold before another guest");
        return -EALREADY;
    }
    if (!atomic_cas(&qemu_busy, 0, 1)) {
        shell_error(shell, "QEMU guest is already starting or running");
        return -EBUSY;
    }
#ifdef CONFIG_QEMU_HOST_HEARTBEAT
    atomic_clear(&observer_ticks);
    atomic_clear(&observer_max_gap);
    atomic_set(&observer_last, k_uptime_get_32());
#endif
    guest_options = options;
    guest_shell = shell;
    shell_print(shell, "Starting guest; Ctrl-] returns to Zephyr");
    shell_set_bypass(shell, guest_input, NULL);
    k_sem_give(&qemu_request);
    return 0;
}

/* The shell syntax and C identifier are separate for this hyphenated name. */
static const struct shell_static_entry qemu_command = {
#ifdef CONFIG_QEMU_USER
    .syntax = "qemu-aarch64",
    .help = "Run a Linux AArch64 ELF program; use -help for options",
#else
    .syntax = "qemu-system-aarch64",
    .help = "Start an ARM64 guest; use -help for options and filesystem paths",
#endif
    .handler = cmd_qemu,
    .args = { .mandatory = 1, .optional = CONFIG_SHELL_ARGC_MAX - 1 },
};
static const TYPE_SECTION_ITERABLE(union shell_cmd_entry, qemu_system_aarch64,
                                  shell_root_cmds, qemu_system_aarch64) = {
    .entry = &qemu_command,
};
#endif

K_THREAD_STACK_DEFINE(qemu_worker_stack, 131072);

#ifdef CONFIG_QEMU_HOST_HEARTBEAT
static void host_heartbeat(void *first, void *second, void *third)
{
    atomic_set(&observer_last, k_uptime_get_32());
    while (true) {
        k_sleep(K_SECONDS(1));
#ifdef CONFIG_QEMU_SHELL
        if (!atomic_get(&qemu_busy)) {
            continue;
        }
#endif
        observer_record_gap();
        atomic_set(&observer_last, k_uptime_get_32());
        atomic_inc(&observer_ticks);
    }
}

K_THREAD_DEFINE(qemu_host_observer, 1024, host_heartbeat, NULL, NULL, NULL, 1, 0, 0);
#endif

static void *qemu_worker(void *unused)
{
#ifdef CONFIG_QEMU_SHELL
    while (true) {
        int result;

        k_sem_take(&qemu_request, K_FOREVER);
        result = qemu_zephyr_run(&guest_options);
#ifdef CONFIG_QEMU_SYSTEM
        if (result == 0) {
            qemu_zephyr_get_stats(guest_stats, sizeof(guest_stats));
        }
#endif
#ifdef CONFIG_QEMU_HOST_HEARTBEAT
        observer_record_gap();
#endif
        shell_set_bypass(guest_shell, NULL, NULL);
        atomic_clear(&qemu_busy);
        shell_print(guest_shell, "QEMU guest exited: %d", result);
    }
    return NULL;
#else
    return (void *)(intptr_t)qemu_zephyr_linux_main();
#endif
}

int main(void)
{
    uint64_t current_el;
    pthread_t worker;
    pthread_attr_t attr;
    const struct sched_param scheduling = {.sched_priority = 0};
    int status;

    __asm__ volatile("mrs %0, CurrentEL" : "=r"(current_el));
    printk("QEMU Linux host EL%llu\n", current_el >> 2);
    if (current_el != (IS_ENABLED(CONFIG_QEMU_TCG) ? 4U : 8U)) {
        return -1;
    }
    status = pthread_attr_init(&attr);
    if (status != 0) {
        return -status;
    }
    status = pthread_attr_setstack(&attr, qemu_worker_stack,
                                   K_THREAD_STACK_SIZEOF(qemu_worker_stack));
    if (status == 0) {
        status = pthread_attr_setschedpolicy(&attr, SCHED_RR);
    }
    if (status == 0) {
        status = pthread_attr_setschedparam(&attr, &scheduling);
    }
    if (status == 0) {
        status = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    }
    if (status == 0) {
        status = pthread_create(&worker, &attr, qemu_worker, NULL);
    }
    int cleanup_status = pthread_attr_destroy(&attr);

    if (status == 0 && cleanup_status != 0) {
        status = cleanup_status;
    }
    if (status != 0) {
        printk("QEMU worker creation failed: %d\n", status);
        return -status;
    }
#ifdef CONFIG_QEMU_SHELL
    status = fs_mount(&image_mount);
    if (status != 0) {
        printk("Cannot mount /images: %d; provide an Ext2 GUEST_DISK\n", status);
    } else {
        printk("Guest filesystem mounted at /images\n");
    }
    status = qemu_zephyr_mount_payload();
    if (status != 0) {
        return status;
    }
    atomic_set(&qemu_ready, 1);
    printk("QEMU shell ready: %s -help\n", qemu_command.syntax);
#ifdef CONFIG_QEMU_AUTOSTART
    const struct shell *shell = shell_backend_uart_get_ptr();

    while (!shell_ready(shell)) {
        k_msleep(1);
    }
    status = shell_execute_cmd(shell, CONFIG_QEMU_STARTUP_COMMAND);
    if (status != 0) {
        printk("Automatic QEMU command failed: %d\n", status);
    }
#endif
    return 0;
#else
    void *result;

    status = pthread_join(worker, &result);
    return status != 0 ? -status : (int)(intptr_t)result;
#endif
}
