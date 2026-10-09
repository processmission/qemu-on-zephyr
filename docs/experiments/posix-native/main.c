/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>

/* These provider signatures have no declarations in the pinned public headers. */
ssize_t pread(int fd, void *buf, size_t count, off_t offset);
ssize_t pwrite(int fd, void *buf, size_t count, off_t offset);
int mprotect(void *addr, size_t len, int prot);
int pause(void);

static struct fs_mount_t image_mount = {
    .type = FS_EXT2,
    .mnt_point = "/images",
    .storage_dev = "GUESTFILES",
    .flags = FS_MOUNT_FLAG_READ_ONLY | FS_MOUNT_FLAG_NO_FORMAT |
             FS_MOUNT_FLAG_USE_DISK_ACCESS,
};

K_THREAD_STACK_DEFINE(probe_worker_stack, 16384);
static pthread_mutex_t probe_mutex;
static pthread_cond_t probe_condition;
static int worker_ready;
static unsigned char protection_page[4096] __aligned(4096);

static void record(const char *name, long long result, int error)
{
    printf("POSIX_PROBE {\"name\":\"%s\",\"rc\":%lld,\"errno\":%d}\n",
           name, result, error);
}

#define OBSERVE(name, expression) \
    do { \
        errno = 0; \
        long long probe_result = (expression); \
        int probe_error = errno; \
        record(name, probe_result, probe_error); \
    } while (0)

static struct timespec deadline(long milliseconds)
{
    struct timespec result;

    clock_gettime(CLOCK_REALTIME, &result);
    result.tv_nsec += milliseconds * 1000000;
    result.tv_sec += result.tv_nsec / 1000000000;
    result.tv_nsec %= 1000000000;
    return result;
}

static void *worker(void *argument)
{
    OBSERVE("worker_mutex_lock", pthread_mutex_lock(&probe_mutex));
    worker_ready = 1;
    OBSERVE("worker_cond_signal", pthread_cond_signal(&probe_condition));
    OBSERVE("worker_mutex_unlock", pthread_mutex_unlock(&probe_mutex));
    return argument;
}

static void probe_files(void)
{
    struct stat path_stat = {0};
    struct stat descriptor_stat = {0};
    unsigned char bytes[4] = {0};
    int descriptor;

    errno = 0;
    int mounted = fs_mount(&image_mount);
    record("fs_mount", mounted, errno);
    if (mounted != 0) {
        return;
    }
    errno = 0;
    descriptor = open("/images/hello", O_RDONLY);
    record("open_readonly", descriptor, errno);
    if (descriptor < 0) {
        return;
    }
    OBSERVE("read_header", read(descriptor, bytes, sizeof(bytes)));
    printf("POSIX_PROBE {\"name\":\"read_bytes\",\"hex\":\"%02x%02x%02x%02x\"}\n",
           bytes[0], bytes[1], bytes[2], bytes[3]);
    OBSERVE("lseek_start", lseek(descriptor, 0, SEEK_SET));
    OBSERVE("read_header_after_seek", read(descriptor, bytes, sizeof(bytes)));
    OBSERVE("stat", stat("/images/hello", &path_stat));
    OBSERVE("fstat", fstat(descriptor, &descriptor_stat));
    printf("POSIX_PROBE {\"name\":\"file_metadata\",\"stat_mode\":%u,"
           "\"stat_size\":%lld,\"fstat_mode\":%u,\"fstat_size\":%lld,"
           "\"stat_is_regular\":%d,\"fstat_is_regular\":%d,"
           "\"stat_permissions\":%u,\"fstat_permissions\":%u}\n",
           (unsigned int)path_stat.st_mode, (long long)path_stat.st_size,
           (unsigned int)descriptor_stat.st_mode, (long long)descriptor_stat.st_size,
           S_ISREG(path_stat.st_mode), S_ISREG(descriptor_stat.st_mode),
           (unsigned int)(path_stat.st_mode & 0777),
           (unsigned int)(descriptor_stat.st_mode & 0777));
    OBSERVE("pread_regular_readonly", pread(descriptor, bytes, sizeof(bytes), 0));
    OBSERVE("pwrite_regular_readonly", pwrite(descriptor, bytes, 1, 0));
    OBSERVE("lseek_current_after_positional_io", lseek(descriptor, 0, SEEK_CUR));
    OBSERVE("close", close(descriptor));
    OBSERVE("fs_unmount", fs_unmount(&image_mount));
}

static void probe_clocks(void)
{
    struct timespec value = {0};
    const struct timespec delay = {.tv_sec = 0, .tv_nsec = 1000000};

    OBSERVE("clock_gettime_realtime", clock_gettime(CLOCK_REALTIME, &value));
    printf("POSIX_PROBE {\"name\":\"realtime_value\",\"sec\":%lld,\"nsec\":%ld}\n",
           (long long)value.tv_sec, value.tv_nsec);
    OBSERVE("clock_gettime_monotonic", clock_gettime(CLOCK_MONOTONIC, &value));
    printf("POSIX_PROBE {\"name\":\"monotonic_value\",\"sec\":%lld,\"nsec\":%ld}\n",
           (long long)value.tv_sec, value.tv_nsec);
    OBSERVE("clock_getres_realtime", clock_getres(CLOCK_REALTIME, &value));
    printf("POSIX_PROBE {\"name\":\"realtime_resolution\",\"sec\":%lld,\"nsec\":%ld}\n",
           (long long)value.tv_sec, value.tv_nsec);
    OBSERVE("clock_getres_monotonic", clock_getres(CLOCK_MONOTONIC, &value));
    OBSERVE("nanosleep", nanosleep(&delay, NULL));
}

static void probe_threads(void)
{
    pthread_t thread;
    pthread_attr_t attr;
    const struct sched_param scheduling = {.sched_priority = 0};
    void *thread_result = NULL;
    int created;

    OBSERVE("pthread_mutex_init", pthread_mutex_init(&probe_mutex, NULL));
    OBSERVE("pthread_cond_init", pthread_cond_init(&probe_condition, NULL));
    OBSERVE("pthread_mutex_lock", pthread_mutex_lock(&probe_mutex));
    OBSERVE("pthread_attr_init", pthread_attr_init(&attr));
    OBSERVE("pthread_attr_setstack",
            pthread_attr_setstack(&attr, probe_worker_stack,
                                  K_THREAD_STACK_SIZEOF(probe_worker_stack)));
    OBSERVE("pthread_attr_setschedpolicy", pthread_attr_setschedpolicy(&attr, SCHED_RR));
    OBSERVE("pthread_attr_setschedparam", pthread_attr_setschedparam(&attr, &scheduling));
    OBSERVE("pthread_attr_setinheritsched",
            pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED));
    errno = 0;
    created = pthread_create(&thread, &attr, worker, (void *)(uintptr_t)0x51504f53);
    record("pthread_create", created, errno);
    OBSERVE("pthread_attr_destroy", pthread_attr_destroy(&attr));
    if (created == 0) {
        struct timespec until = deadline(1000);
        OBSERVE("pthread_cond_timedwait_notification",
                pthread_cond_timedwait(&probe_condition, &probe_mutex, &until));
        printf("POSIX_PROBE {\"name\":\"worker_ready\",\"value\":%d}\n", worker_ready);
    }
    OBSERVE("pthread_mutex_unlock", pthread_mutex_unlock(&probe_mutex));
    if (created == 0) {
        OBSERVE("pthread_join", pthread_join(thread, &thread_result));
        printf("POSIX_PROBE {\"name\":\"thread_result\",\"value\":%llu}\n",
               (unsigned long long)(uintptr_t)thread_result);
    }
    OBSERVE("pthread_mutex_trylock", pthread_mutex_trylock(&probe_mutex));
    struct timespec until = deadline(10);
    OBSERVE("pthread_cond_timedwait_timeout",
            pthread_cond_timedwait(&probe_condition, &probe_mutex, &until));
    OBSERVE("pthread_cond_broadcast", pthread_cond_broadcast(&probe_condition));
    OBSERVE("pthread_mutex_unlock_after_timeout", pthread_mutex_unlock(&probe_mutex));
    OBSERVE("pthread_cond_destroy", pthread_cond_destroy(&probe_condition));
    OBSERVE("pthread_mutex_destroy", pthread_mutex_destroy(&probe_mutex));
    OBSERVE("pthread_atfork", pthread_atfork(NULL, NULL, NULL));
}

static void probe_signal_and_memory_boundaries(void)
{
    struct sigaction previous = {0};
    sigset_t signals;
    int received = -1;

    OBSERVE("mprotect", mprotect(protection_page, sizeof(protection_page),
                                  PROT_READ | PROT_WRITE));
    OBSERVE("sigemptyset", sigemptyset(&signals));
    OBSERVE("sigaddset", sigaddset(&signals, SIGUSR1));
    OBSERVE("sigismember", sigismember(&signals, SIGUSR1));
    OBSERVE("sigaction_query", sigaction(SIGUSR1, NULL, &previous));
    OBSERVE("getpid", getpid());
    OBSERVE("kill_signal_zero", kill(getpid(), 0));
    OBSERVE("pause", pause());
    OBSERVE("sigwait", sigwait(&signals, &received));
    OBSERVE("sched_yield", sched_yield());
}

int main(void)
{
    unsigned long current_el;

    __asm__ volatile("mrs %0, CurrentEL" : "=r"(current_el));
    printf("POSIX_PROBE {\"name\":\"environment\",\"el\":%lu,\"libc\":\"Picolibc\"}\n",
           current_el >> 2);
    printf("POSIX_PROBE {\"name\":\"target_errno_constants\",\"ENOSYS\":%d,"
           "\"ENOTSUP\":%d,\"ETIMEDOUT\":%d}\n", ENOSYS, ENOTSUP, ETIMEDOUT);
    probe_files();
    probe_clocks();
    probe_threads();
    probe_signal_and_memory_boundaries();
    printf("POSIX_PROBE_DONE\n");
    return 0;
}
