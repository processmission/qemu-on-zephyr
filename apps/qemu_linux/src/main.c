/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <qemu/zephyr.h>
#include <pthread.h>

K_THREAD_STACK_DEFINE(qemu_worker_stack, 131072);

#ifdef CONFIG_QEMU_HOST_HEARTBEAT
volatile uint32_t qemu_host_heartbeat;

static void host_heartbeat(void *first, void *second, void *third)
{
    while (true) {
        k_sleep(K_SECONDS(1));
        qemu_host_heartbeat++;
        if (qemu_host_heartbeat % 5U == 0U) {
            printk("ZEPHYR_HOST_HEARTBEAT=%u\n", qemu_host_heartbeat);
        }
    }
}

K_THREAD_DEFINE(qemu_host_observer, 1024, host_heartbeat, NULL, NULL, NULL, 1, 0, 0);
#endif

static void *qemu_worker(void *unused)
{
    return (void *)(intptr_t)qemu_zephyr_linux_main();
}

int main(void)
{
    uint64_t current_el;
    pthread_t worker;
    pthread_attr_t attr;
    const struct sched_param scheduling = {.sched_priority = 0};
    void *result;
    int status;

    __asm__ volatile("mrs %0, CurrentEL" : "=r"(current_el));
    printk("QEMU Linux host EL%llu\n", current_el >> 2);
    if (current_el != 8U) {
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
    status = pthread_join(worker, &result);
    return status != 0 ? -status : (int)(intptr_t)result;
}
