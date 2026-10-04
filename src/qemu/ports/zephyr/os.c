/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/thread.h"
#include "qemu/rcu.h"
#include "qemu/main-loop.h"
#include "qemu/event_notifier.h"
#include "qemu/coroutine.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qapi/qapi-commands-control.h"
#include "migration/vmstate.h"
#include "chardev/char.h"
#ifdef CONFIG_QEMU_ZEPHYR_ACCEL
#include "system/cpus.h"
#include "exec/cpu-common.h"
#include "system/zephyr.h"
#include "qemu/mmap-alloc.h"
#include "qemu/memalign.h"
#include "qemu/madvise.h"
#include "migration/blocker.h"
#include "system/replay.h"
#include "system/qtest.h"
#include "crypto/random.h"
#include "exec/replay-core.h"
#endif

/* Kernel API macros below must retain Zephyr's numeric CONFIG semantics. */
#undef IS_ENABLED
#define IS_ENABLED(config_macro) Z_IS_ENABLED1(config_macro)
#undef IS_EMPTY
#define IS_EMPTY(...) Z_IS_EMPTY_(__VA_ARGS__)

/* Only this thread may enter QOM/device/memory code, including RCU readers. */
static k_tid_t qemu_owner;
#ifndef CONFIG_QEMU_ZEPHYR_ACCEL
static QemuMutex qemu_bql;
static bool qemu_bql_held;
#endif
static struct k_sem qemu_event;
static struct rcu_head *rcu_pending;
static struct rcu_head **rcu_pending_tail = &rcu_pending;

unsigned long rcu_gp_ctr = 1;
QemuEvent rcu_gp_event;
QEMU_DEFINE_CO_TLS(struct rcu_reader_data, rcu_reader)

QemuMutexLockFunc qemu_mutex_lock_func = qemu_mutex_lock_impl;
QemuMutexLockFunc bql_mutex_lock_func = qemu_mutex_lock_impl;
QemuCondWaitFunc qemu_cond_wait_func = qemu_cond_wait_impl;
QemuCondTimedWaitFunc qemu_cond_timedwait_func = qemu_cond_timedwait_impl;
unsigned qemu_loglevel = LOG_GUEST_ERROR | LOG_UNIMP;

#ifdef CONFIG_QEMU_ZEPHYR_ACCEL
bool qtest_driver(void)
{
    return false;
}

char *qemu_get_pid_name(pid_t pid)
{
    /* There is no host process namespace on Zephyr. */
    errno = ENOSYS;
    return NULL;
}

void *qemu_anon_ram_alloc(size_t size, uint64_t *alignment, bool shared, bool noreserve)
{
    void *memory;

    if (shared || noreserve) {
        errno = ENOTSUP;
        return NULL;
    }
    *alignment = MAX(*alignment, CONFIG_MMU_PAGE_SIZE);
    memory = qemu_try_memalign(*alignment, size);
    if (memory != NULL) {
        memset(memory, 0, size);
    }
    return memory;
}

void qemu_anon_ram_free(void *memory, size_t size)
{
    qemu_vfree(memory);
}

void qemu_ram_munmap(int fd, void *memory, size_t size)
{
    error_report("Zephyr has no file-backed guest RAM mappings");
    abort();
}

int qemu_madvise(void *address, size_t size, int advice)
{
    errno = ENOSYS;
    return -1;
}

int migrate_add_blocker_modes(Error **reason, unsigned int modes, Error **errp)
{
    error_free(*reason);
    *reason = NULL;
    error_setg(errp, "Zephyr does not provide migration modes");
    return -ENOTSUP;
}

void replay_start(void)
{
    assert(replay_mode == REPLAY_MODE_NONE);
}

void replay_shutdown_request(ShutdownCause cause)
{
    assert(replay_mode == REPLAY_MODE_NONE);
}

int replay_read_random(void *buffer, size_t length)
{
    abort();
}

void replay_save_random(int result, void *buffer, size_t length)
{
    abort();
}

int qcrypto_random_bytes(void *buffer, size_t length, Error **errp)
{
    error_setg(errp, "No guest entropy source is configured");
    return -ENOTSUP;
}
#endif

struct OnceInit {
    volatile void *location;
    struct OnceInit *next;
};

static struct OnceInit *once_pending;

gboolean g_once_init_enter(volatile void *location)
{
    struct OnceInit *entry;

    assert(k_current_get() == qemu_owner);
    if (__atomic_load_n((gsize *)location, __ATOMIC_ACQUIRE) != 0) {
        return false;
    }
    for (entry = once_pending; entry != NULL; entry = entry->next) {
        assert(entry->location != location);
    }
    entry = g_new(struct OnceInit, 1);
    entry->location = location;
    entry->next = once_pending;
    once_pending = entry;
    return true;
}

void g_once_init_leave(volatile void *location, gsize result)
{
    struct OnceInit **entry = &once_pending;
    struct OnceInit *completed;

    assert(k_current_get() == qemu_owner && result != 0);
    while (*entry != NULL && (*entry)->location != location) {
        entry = &(*entry)->next;
    }
    assert(*entry != NULL);
    completed = *entry;
    *entry = completed->next;
    __atomic_store_n((gsize *)location, result, __ATOMIC_RELEASE);
    g_free(completed);
}

static void check_pthread(int result)
{
    if (result != 0) {
        fprintf(stderr, "QEMU pthread operation failed: %d\n", result);
        abort();
    }
}

void qemu_mutex_init(QemuMutex *mutex)
{
    check_pthread(pthread_mutex_init(&mutex->lock, NULL));
    mutex->initialized = true;
}

void qemu_mutex_destroy(QemuMutex *mutex)
{
    assert(mutex->initialized);
    check_pthread(pthread_mutex_destroy(&mutex->lock));
    mutex->initialized = false;
}

void qemu_mutex_lock_impl(QemuMutex *mutex, const char *file, int line)
{
    assert(mutex->initialized);
    check_pthread(pthread_mutex_lock(&mutex->lock));
#ifdef CONFIG_QEMU_ZEPHYR_ACCEL
    if (mutex_is_bql(mutex)) {
        assert(k_current_get() == qemu_owner);
        bql_update_status(true);
    }
#endif
}

void qemu_mutex_unlock_impl(QemuMutex *mutex, const char *file, int line)
{
    assert(mutex->initialized);
#ifdef CONFIG_QEMU_ZEPHYR_ACCEL
    if (mutex_is_bql(mutex)) {
        bql_update_status(false);
    }
#endif
    check_pthread(pthread_mutex_unlock(&mutex->lock));
}

void qemu_cond_init(QemuCond *cond)
{
    check_pthread(pthread_cond_init(&cond->cond, NULL));
    cond->initialized = true;
}

void qemu_cond_destroy(QemuCond *cond)
{
    assert(cond->initialized);
    check_pthread(pthread_cond_destroy(&cond->cond));
    cond->initialized = false;
}

void qemu_cond_signal(QemuCond *cond)
{
    assert(cond->initialized);
    check_pthread(pthread_cond_signal(&cond->cond));
}

void qemu_cond_broadcast(QemuCond *cond)
{
    assert(cond->initialized);
    check_pthread(pthread_cond_broadcast(&cond->cond));
}

void qemu_cond_wait_impl(QemuCond *cond, QemuMutex *mutex,
                         const char *file, int line)
{
    assert(cond->initialized && mutex->initialized);
#ifdef CONFIG_QEMU_ZEPHYR_ACCEL
    bool is_bql = mutex_is_bql(mutex);

    if (is_bql) {
        bql_update_status(false);
    }
#endif
    check_pthread(pthread_cond_wait(&cond->cond, &mutex->lock));
#ifdef CONFIG_QEMU_ZEPHYR_ACCEL
    if (is_bql) {
        bql_update_status(true);
    }
#endif
}

bool qemu_cond_timedwait_impl(QemuCond *cond, QemuMutex *mutex, int ms,
                              const char *file, int line)
{
    struct timespec deadline;
    int result;

    assert(ms >= 0);
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) {
        abort();
    }
    deadline.tv_sec += ms / 1000;
    deadline.tv_nsec += (ms % 1000) * 1000000;
    if (deadline.tv_nsec >= 1000000000) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000;
    }
#ifdef CONFIG_QEMU_ZEPHYR_ACCEL
    bool is_bql = mutex_is_bql(mutex);

    if (is_bql) {
        bql_update_status(false);
    }
#endif
    result = pthread_cond_timedwait(&cond->cond, &mutex->lock, &deadline);
#ifdef CONFIG_QEMU_ZEPHYR_ACCEL
    if (is_bql) {
        bql_update_status(true);
    }
#endif
    if (result != ETIMEDOUT) {
        check_pthread(result);
    }
    return result == 0;
}

void qemu_thread_get_self(QemuThread *thread)
{
    assert(k_current_get() == qemu_owner);
    thread->thread = pthread_self();
}

bool qemu_thread_is_self(QemuThread *thread)
{
    return k_current_get() == qemu_owner &&
           pthread_equal(pthread_self(), thread->thread);
}

int qemu_get_thread_id(void)
{
    assert(k_current_get() == qemu_owner);
    return (int)pthread_self();
}

void rcu_register_thread(void)
{
    /* The fixed owner is the sole entry in this port's reader registry. */
    assert(k_current_get() == qemu_owner);
}

void rcu_unregister_thread(void)
{
    assert(k_current_get() == qemu_owner);
    assert(get_ptr_rcu_reader()->depth == 0);
    qemu_zephyr_quiesce();
}

#ifndef CONFIG_QEMU_ZEPHYR_ACCEL
bool bql_locked(void)
{
    return qemu_bql_held && k_current_get() == qemu_owner;
}

void bql_lock_impl(const char *file, int line)
{
    assert(k_current_get() == qemu_owner);
    assert(!qemu_bql_held);
    qemu_mutex_lock_impl(&qemu_bql, file, line);
    qemu_bql_held = true;
}

void bql_unlock(void)
{
    assert(bql_locked());
    qemu_bql_held = false;
    qemu_mutex_unlock(&qemu_bql);
}
#endif

void qemu_zephyr_os_init(void)
{
    assert(qemu_owner == NULL);
    qemu_owner = k_current_get();
    rcu_register_thread();
#ifdef CONFIG_QEMU_ZEPHYR_ACCEL
    qemu_init_cpu_list();
    qemu_init_cpu_loop();
#else
    qemu_mutex_init(&qemu_bql);
#endif
    qemu_event_init(&rcu_gp_event, false);
    if (k_sem_init(&qemu_event, 0, 1) != 0) {
        abort();
    }
    bql_lock();
}

void qemu_notify_event(void)
{
    k_sem_give(&qemu_event);
#ifdef CONFIG_QEMU_ZEPHYR_ACCEL
    if (first_cpu != NULL) {
        zephyr_cpu_kick(first_cpu);
    }
#endif
}

void call_rcu1(struct rcu_head *head, RCUCBFunc *func)
{
    assert(bql_locked());
    head->func = func;
    head->next = NULL;
    *rcu_pending_tail = head;
    rcu_pending_tail = &head->next;
}

void qemu_zephyr_quiesce(void)
{
    assert(bql_locked());
    assert(get_ptr_rcu_reader()->depth == 0);
    while (rcu_pending != NULL) {
        struct rcu_head *head = rcu_pending;

        rcu_pending = head->next;
        if (rcu_pending == NULL) {
            rcu_pending_tail = &rcu_pending;
        }
        head->func(head);
    }
}

#ifndef CONFIG_QEMU_ZEPHYR_ACCEL
int64_t cpu_get_clock(void)
{
    return k_cyc_to_ns_floor64(k_cycle_get_64());
}

int64_t cpus_get_virtual_clock(void)
{
    return cpu_get_clock();
}
#endif

void qemu_log(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

FILE *qemu_log_trylock(void)
{
    if (qemu_loglevel == 0) {
        return NULL;
    }
    qemu_flockfile(stderr);
    return stderr;
}

void qemu_log_unlock(FILE *file)
{
    qemu_funlockfile(file);
}

ssize_t qemu_write_full(int fd, const void *buffer, size_t count)
{
    size_t written = 0;

    while (written < count) {
        ssize_t result = write(fd, (const char *)buffer + written, count - written);

        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result < 0) {
            return written > 0 ? (ssize_t)written : -1;
        }
        if (result == 0) {
            break;
        }
        written += result;
    }
    return written;
}

int event_notifier_set(EventNotifier *event)
{
    const uint64_t increment = 1;
    ssize_t result;

    if (!event->initialized) {
        return -EINVAL;
    }
    result = qemu_write_full(event->wfd, &increment, sizeof(increment));
    if (result == sizeof(increment) || (result < 0 && errno == EAGAIN)) {
        return 0;
    }
    return result < 0 ? -errno : -EIO;
}

/* This configuration has no coroutine entry point or host FD watches. */
bool qemu_in_coroutine(void)
{
    return false;
}

void qemu_co_sleep_ns_wakeable(QemuCoSleep *sleep, QEMUClockType clock, int64_t ns)
{
    fprintf(stderr, "QEMU coroutine scheduling is unavailable\n");
    abort();
}

void remove_fd_in_watch(Chardev *chr)
{
    assert(chr->gsource == NULL);
}

int vmstate_register_with_alias_id(VMStateIf *object, uint32_t instance_id,
                                   const VMStateDescription *description,
                                   void *base, int alias_id,
                                   int required_version, Error **errp)
{
    error_setg(errp, "Zephyr accelerator does not support migration");
    return -ENOTSUP;
}

void vmstate_unregister(VMStateIf *object, const VMStateDescription *description,
                        void *opaque)
{
    fprintf(stderr, "QEMU migration state was never registered\n");
    abort();
}

bool vmstate_check_only_migratable(const VMStateDescription *description)
{
    /* --only-migratable is not enabled; this is an option-policy check. */
    return true;
}

void qmp_quit(Error **errp)
{
    error_setg(errp, "QMP lifecycle control is unavailable");
}

void blk_commit_all(void)
{
    fprintf(stderr, "QEMU block backends are unavailable\n");
    abort();
}

void qemu_flockfile(FILE *file)
{
    flockfile(file);
}

void qemu_funlockfile(FILE *file)
{
    funlockfile(file);
}

void g_usleep(gulong microseconds)
{
    /* Bound each conversion and recompute after any early thread wakeup. */
    while (microseconds > 0) {
        uint64_t chunk = MIN(microseconds, 1000000UL);
        int64_t deadline = k_uptime_ticks() + k_us_to_ticks_ceil64(chunk) + 1;
        int64_t remaining;

        while ((remaining = deadline - k_uptime_ticks()) > 0) {
            k_sleep(K_TICKS(remaining));
        }
        microseconds -= chunk;
    }
}

gint64 g_get_real_time(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
        abort();
    }
    return (gint64)now.tv_sec * 1000000 + now.tv_nsec / 1000;
}
