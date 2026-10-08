/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu.h"
#include "user-internals.h"
#include "user-mmap.h"
#include "user/tswap-target.h"
#include "exec/mmap-lock.h"
#include "target_fcntl.h"
#include "target_mman.h"
#include "console.h"
#include "user.h"
#include <zephyr/random/random.h>
#include <sys/utsname.h>

#undef IS_ENABLED
#define IS_ENABLED(config_macro) Z_IS_ENABLED1(config_macro)
#undef IS_EMPTY
#define IS_EMPTY(...) Z_IS_EMPTY_(__VA_ARGS__)

#define USER_FILES 32
#define USER_CONSOLE (-2)
#define TARGET_AT_FDCWD (-100)
#define TARGET_AT_SYMLINK_NOFOLLOW 0x100
#define TARGET_AT_EMPTY_PATH 0x1000
static int files[USER_FILES];
static bool files_initialized;
static abi_ulong initial_brk, current_brk, clear_tid;
static unsigned int next_pid = 1;
static unsigned int process_pid;
static char current_directory[256] = "/images";

static int target_errno(int error)
{
    switch (error) {
#define ERRNO(name) case name: return TARGET_##name
    ERRNO(EPERM); ERRNO(ENOENT); ERRNO(EINTR); ERRNO(EIO); ERRNO(EBADF);
    ERRNO(EAGAIN); ERRNO(ENOMEM); ERRNO(EACCES); ERRNO(EFAULT); ERRNO(EBUSY);
    ERRNO(EEXIST); ERRNO(ENODEV); ERRNO(ENOTDIR); ERRNO(EISDIR); ERRNO(EINVAL);
    ERRNO(ENFILE); ERRNO(EMFILE); ERRNO(ENOTTY); ERRNO(EFBIG); ERRNO(ENOSPC);
    ERRNO(ESPIPE); ERRNO(EROFS); ERRNO(EPIPE); ERRNO(ERANGE); ERRNO(ENAMETOOLONG);
    ERRNO(ENOSYS); ERRNO(ENOTEMPTY); ERRNO(ELOOP); ERRNO(EOVERFLOW);
    ERRNO(ETIMEDOUT);
#undef ERRNO
    case ENOTSUP: return TARGET_EOPNOTSUPP;
    default: return TARGET_EIO;
    }
}

abi_long get_errno(abi_long result)
{
    return result == -1 ? -target_errno(errno) : result;
}

void qemu_zephyr_user_syscall_close(void)
{
    if (clear_tid != 0) {
        put_user_u32(0, clear_tid);
        clear_tid = 0;
    }
    if (files_initialized) {
        for (int i = 0; i < USER_FILES; i++) {
            if (files[i] >= 0) {
                close(files[i]);
            }
            files[i] = -1;
        }
    }
}

void qemu_zephyr_user_syscall_reset(abi_ulong brk)
{
    qemu_zephyr_user_syscall_close();
    for (int i = 0; i < USER_FILES; i++) {
        files[i] = i < 3 ? USER_CONSOLE : -1;
    }
    files_initialized = true;
    process_pid = next_pid++;
    if (next_pid > INT_MAX) {
        next_pid = 1;
    }
    get_task_state(thread_cpu)->ts_tid = process_pid;
    initial_brk = current_brk = brk;
    strcpy(current_directory, "/images");
}

static int host_fd(abi_long fd)
{
    return fd >= 0 && fd < USER_FILES ? files[fd] : -1;
}

static abi_long user_path(abi_ulong address, char *path, size_t capacity)
{
    char *value = lock_user_string(address);
    int length;

    if (value == NULL) {
        return -TARGET_EFAULT;
    }
    if (value[0] == '\0') {
        return -TARGET_ENOENT;
    }
    length = value[0] == '/' ? snprintf(path, capacity, "%s", value) :
             snprintf(path, capacity, "%s/%s", current_directory, value);
    unlock_user(value, address, 0);
    return length < 0 || length >= capacity ? -TARGET_ENAMETOOLONG : 0;
}

static abi_long user_open(abi_long dirfd, abi_ulong name, abi_long flags, abi_long mode)
{
    char path[512];
    int host_flags = 0, slot, fd;
    abi_long result = user_path(name, path, sizeof(path));
    int supported = TARGET_O_ACCMODE | TARGET_O_CREAT | TARGET_O_EXCL | TARGET_O_TRUNC |
                    TARGET_O_APPEND | TARGET_O_NONBLOCK | TARGET_O_CLOEXEC |
                    TARGET_O_DIRECTORY | TARGET_O_NOFOLLOW | TARGET_O_LARGEFILE;

    if (result < 0) {
        return result;
    }
    /* Relative *at operations require an emulated directory descriptor. */
    char *original = lock_user_string(name);
    bool relative = original[0] != '/';
    unlock_user(original, name, 0);
    if (relative && dirfd != TARGET_AT_FDCWD) {
        return -TARGET_EBADF;
    }
    if (flags & ~supported) {
        return -TARGET_EINVAL;
    }
    switch (flags & TARGET_O_ACCMODE) {
    case TARGET_O_RDONLY: host_flags = O_RDONLY; break;
    case TARGET_O_WRONLY: host_flags = O_WRONLY; break;
    case TARGET_O_RDWR: host_flags = O_RDWR; break;
    default: return -TARGET_EINVAL;
    }
    if (flags & TARGET_O_CREAT) { host_flags |= O_CREAT; }
    if (flags & TARGET_O_EXCL) { host_flags |= O_EXCL; }
    if (flags & TARGET_O_TRUNC) { host_flags |= O_TRUNC; }
    if (flags & TARGET_O_APPEND) { host_flags |= O_APPEND; }
    if (flags & TARGET_O_NONBLOCK) { host_flags |= O_NONBLOCK; }
    if (flags & (TARGET_O_DIRECTORY | TARGET_O_NOFOLLOW)) {
        return -TARGET_EOPNOTSUPP;
    }
    for (slot = 0; slot < USER_FILES && files[slot] != -1; slot++) {
    }
    if (slot == USER_FILES) {
        return -TARGET_EMFILE;
    }
    fd = open(path, host_flags, mode & 0777);
    if (fd < 0) {
        return get_errno(-1);
    }
    files[slot] = fd;
    return slot;
}

static abi_long user_io(abi_long fd, abi_ulong address, abi_ulong length, bool writing)
{
    int host = host_fd(fd);
    void *buffer;
    abi_long result;

    if (host == -1) {
        return -TARGET_EBADF;
    }
    if (length > SSIZE_MAX) {
        return -TARGET_EINVAL;
    }
    buffer = lock_user(writing ? VERIFY_READ : VERIFY_WRITE, address, length, writing);
    if (buffer == NULL && length != 0) {
        return -TARGET_EFAULT;
    }
    if (host == USER_CONSOLE) {
        if (writing) {
            result = qemu_zephyr_uart_write(buffer, length);
        } else {
            result = 0;
            while (length != 0 && (result = qemu_zephyr_uart_read(buffer, length)) == 0) {
                if (qatomic_read(&user_stop_requested)) {
                    result = -TARGET_EINTR;
                    break;
                }
                k_msleep(1);
            }
        }
    } else {
        result = get_errno(writing ? write(host, buffer, length) : read(host, buffer, length));
    }
    unlock_user(buffer, address, !writing && result > 0 ? result : 0);
    return result;
}

static abi_long user_iov(abi_long fd, abi_ulong address, abi_long count, bool writing)
{
    struct target_iovec *vector;
    abi_long total = 0;

    if (count < 0 || count > 1024) {
        return -TARGET_EINVAL;
    }
    vector = lock_user(VERIFY_READ, address, count * sizeof(*vector), true);
    if (vector == NULL) {
        return -TARGET_EFAULT;
    }
    for (int i = 0; i < count; i++) {
        abi_ulong len = tswapal(vector[i].iov_len);
        abi_long result = user_io(fd, tswapal(vector[i].iov_base), len, writing);

        if (result < 0) {
            total = total ? total : result;
            break;
        }
        total += result;
        if (result != len) {
            break;
        }
    }
    unlock_user(vector, address, 0);
    return total;
}

static abi_long user_stat(int fd, const char *path, abi_ulong address)
{
    struct stat st = {0};
    struct target_stat *target;
    int result;

    if (path != NULL) {
        result = stat(path, &st);
    } else if (host_fd(fd) == USER_CONSOLE) {
        st.st_mode = S_IFCHR | 0600;
        st.st_nlink = 1;
        result = 0;
    } else if (host_fd(fd) == -1) {
        return -TARGET_EBADF;
    } else {
        result = fstat(host_fd(fd), &st);
    }
    if (result != 0) {
        return get_errno(result);
    }
    target = lock_user(VERIFY_WRITE, address, sizeof(*target), false);
    if (target == NULL) {
        return -TARGET_EFAULT;
    }
    memset(target, 0, sizeof(*target));
    target->st_dev = tswap64(st.st_dev);
    target->st_ino = tswap64(st.st_ino);
    target->st_mode = tswap32(st.st_mode);
    target->st_nlink = tswap32(st.st_nlink);
    target->st_uid = tswap32(st.st_uid);
    target->st_gid = tswap32(st.st_gid);
    target->st_rdev = tswap64(st.st_rdev);
    target->st_size = tswap64(st.st_size);
    target->st_blksize = tswap32(st.st_blksize);
    target->st_blocks = tswap64(st.st_blocks);
    target->target_st_atime = tswap64(st.st_atime);
    target->target_st_mtime = tswap64(st.st_mtime);
    target->target_st_ctime = tswap64(st.st_ctime);
    unlock_user(target, address, sizeof(*target));
    return 0;
}

static abi_long user_brk(abi_ulong requested)
{
    abi_ulong old_end = TARGET_PAGE_ALIGN(current_brk);
    abi_ulong new_end = TARGET_PAGE_ALIGN(requested);

    if (requested < initial_brk || requested > guest_addr_max) {
        return current_brk;
    }
    if (new_end > old_end && target_mmap(old_end, new_end - old_end,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
        -1, 0) == -1) {
        return current_brk;
    }
    if (new_end < old_end) {
        target_munmap(new_end, old_end - new_end);
    }
    current_brk = requested;
    return current_brk;
}

static abi_long dispatch(CPUARMState *env, int number, abi_long a, abi_long b,
                         abi_long c, abi_long d, abi_long e, abi_long f)
{
    switch (number) {
    case TARGET_NR_exit:
    case TARGET_NR_exit_group:
        user_exit_status = a & 255;
        user_exited = true;
        return 0;
    case TARGET_NR_read: return user_io(a, b, c, false);
    case TARGET_NR_write: return user_io(a, b, c, true);
    case TARGET_NR_readv: return user_iov(a, b, c, false);
    case TARGET_NR_writev: return user_iov(a, b, c, true);
    case TARGET_NR_openat: return user_open(a, b, c, d);
    case TARGET_NR_close: {
        int fd = host_fd(a);
        if (fd == -1) { return -TARGET_EBADF; }
        files[a] = -1;
        return fd == USER_CONSOLE ? 0 : get_errno(close(fd));
    }
    case TARGET_NR_lseek:
        if (host_fd(a) == -1) { return -TARGET_EBADF; }
        if (host_fd(a) == USER_CONSOLE) { return -TARGET_ESPIPE; }
        return get_errno(lseek(host_fd(a), b, c));
    case TARGET_NR_fstat: return user_stat(a, NULL, b);
    case TARGET_NR_newfstatat: {
        char path[512];
        char *name = lock_user_string(b);
        if (name == NULL) { return -TARGET_EFAULT; }
        bool empty = name[0] == '\0';
        bool relative = name[0] != '/';
        unlock_user(name, b, 0);
        if (d & ~(TARGET_AT_EMPTY_PATH | TARGET_AT_SYMLINK_NOFOLLOW)) {
            return -TARGET_EINVAL;
        }
        if (empty && (d & TARGET_AT_EMPTY_PATH)) {
            return user_stat(a, NULL, c);
        }
        if (d & TARGET_AT_SYMLINK_NOFOLLOW) { return -TARGET_EOPNOTSUPP; }
        if (relative && a != TARGET_AT_FDCWD) { return -TARGET_EBADF; }
        abi_long result = user_path(b, path, sizeof(path));
        return result < 0 ? result : user_stat(-1, path, c);
    }
    case TARGET_NR_ioctl:
        return host_fd(a) == -1 ? -TARGET_EBADF : -TARGET_ENOTTY;
    case TARGET_NR_getcwd: {
        size_t length = strlen(current_directory) + 1;
        if (b < length) { return -TARGET_ERANGE; }
        return copy_to_user(a, current_directory, length) < 0 ? -TARGET_EFAULT : length;
    }
    case TARGET_NR_chdir: {
        char path[sizeof(current_directory)];
        struct stat st;
        abi_long result = user_path(a, path, sizeof(path));
        if (result < 0) { return result; }
        if (stat(path, &st) != 0) { return get_errno(-1); }
        if (!S_ISDIR(st.st_mode)) { return -TARGET_ENOTDIR; }
        strcpy(current_directory, path);
        return 0;
    }
    case TARGET_NR_brk: return user_brk(a);
    case TARGET_NR_mmap: {
        int flags = 0;
        if (d & ~(TARGET_MAP_PRIVATE | TARGET_MAP_FIXED | TARGET_MAP_ANONYMOUS |
                  TARGET_MAP_NORESERVE | TARGET_MAP_FIXED_NOREPLACE)) {
            return -TARGET_EINVAL;
        }
        if (d & TARGET_MAP_PRIVATE) { flags |= MAP_PRIVATE; }
        if (d & TARGET_MAP_FIXED) { flags |= MAP_FIXED; }
        if (d & TARGET_MAP_ANONYMOUS) { flags |= MAP_ANONYMOUS; }
        if (d & TARGET_MAP_NORESERVE) { flags |= MAP_NORESERVE; }
        if (d & TARGET_MAP_FIXED_NOREPLACE) { flags |= MAP_FIXED_NOREPLACE; }
        return get_errno(target_mmap(a, b, c, flags, host_fd(e), f));
    }
    case TARGET_NR_mprotect: return get_errno(target_mprotect(a, b, c));
    case TARGET_NR_munmap: return get_errno(target_munmap(a, b));
    case TARGET_NR_clock_gettime:
    case TARGET_NR_clock_getres: {
        struct timespec now;
        struct target_timespec value;
        clockid_t clock;
        if (a == 0) { clock = CLOCK_REALTIME; }
        else if (a == 1) { clock = CLOCK_MONOTONIC; }
        else { return -TARGET_EINVAL; }
        int result = number == TARGET_NR_clock_gettime ? clock_gettime(clock, &now) :
                                                       clock_getres(clock, &now);
        if (result < 0) { return get_errno(result); }
        if (b == 0 && number == TARGET_NR_clock_getres) { return 0; }
        value.tv_sec = tswapal(now.tv_sec);
        value.tv_nsec = tswapal(now.tv_nsec);
        return copy_to_user(b, &value, sizeof(value));
    }
    case TARGET_NR_nanosleep: {
        struct target_timespec value;
        struct timespec requested;
        if (copy_from_user(&value, a, sizeof(value)) != 0) { return -TARGET_EFAULT; }
        requested.tv_sec = tswapal(value.tv_sec);
        requested.tv_nsec = tswapal(value.tv_nsec);
        if (requested.tv_sec < 0 || requested.tv_sec > INT64_MAX / 1000 ||
            requested.tv_nsec < 0 || requested.tv_nsec >= 1000000000) {
            return -TARGET_EINVAL;
        }
        int64_t milliseconds = requested.tv_sec * 1000 + (requested.tv_nsec + 999999) / 1000000;
        while (milliseconds > 0 && !qatomic_read(&user_stop_requested)) {
            int duration = MIN(milliseconds, 10);
            k_msleep(duration);
            milliseconds -= duration;
        }
        if (milliseconds > 0) {
            value.tv_sec = tswapal(milliseconds / 1000);
            value.tv_nsec = tswapal(milliseconds % 1000 * 1000000);
            if (b && copy_to_user(b, &value, sizeof(value)) < 0) { return -TARGET_EFAULT; }
            return -TARGET_EINTR;
        }
        return 0;
    }
    case TARGET_NR_getrandom: {
        void *buffer = lock_user(VERIFY_WRITE, a, b, false);
        int result;
        if (c & ~3) { return -TARGET_EINVAL; }
        if (buffer == NULL) { return -TARGET_EFAULT; }
        result = sys_csrand_get(buffer, b);
        unlock_user(buffer, a, result == 0 ? b : 0);
        return result == 0 ? b : -target_errno(-result);
    }
    case TARGET_NR_getpid:
    case TARGET_NR_gettid: return process_pid;
    /* A Zephyr-hosted process has one virtual root identity. */
    case TARGET_NR_getuid:
    case TARGET_NR_geteuid:
    case TARGET_NR_getgid:
    case TARGET_NR_getegid: return 0;
    case TARGET_NR_set_tid_address:
        if (a != 0 && !access_ok(thread_cpu, VERIFY_WRITE, a, sizeof(uint32_t))) {
            return -TARGET_EFAULT;
        }
        clear_tid = a;
        return process_pid;
    case TARGET_NR_sched_yield:
        k_yield();
        return 0;
    case TARGET_NR_uname: {
        struct utsname host;
        char fields[6][65] = {0};
        if (uname(&host) != 0) { return get_errno(-1); }
        snprintf(fields[0], 65, "%.64s", host.sysname);
        snprintf(fields[1], 65, "%.64s", host.nodename);
        snprintf(fields[2], 65, "%.64s", host.release);
        snprintf(fields[3], 65, "%.64s", host.version);
        strcpy(fields[4], "aarch64");
        return copy_to_user(a, fields, sizeof(fields));
    }
    case TARGET_NR_prlimit64: {
        uint64_t limits[2];
        if (a != 0 && a != process_pid) { return -TARGET_ESRCH; }
        if (c != 0) { return -TARGET_EPERM; }
        switch (b) {
        case 3: limits[0] = 1024 * 1024; break;
        case 7: limits[0] = USER_FILES; break;
        case 2:
        case 9: limits[0] = guest_addr_max + 1; break;
        default: return -TARGET_EINVAL;
        }
        limits[0] = tswap64(limits[0]);
        limits[1] = limits[0];
        return d ? copy_to_user(d, limits, sizeof(limits)) : 0;
    }
    default: return -TARGET_ENOSYS;
    }
}

abi_long do_syscall(CPUARMState *env, int number, abi_long a, abi_long b,
                    abi_long c, abi_long d, abi_long e, abi_long f,
                    abi_long unused7, abi_long unused8)
{
    abi_long result = dispatch(env, number, a, b, c, d, e, f);

    if (user_trace) {
        fprintf(stderr, "syscall %d(0x%" PRIx64 ",0x%" PRIx64 ",0x%" PRIx64
                        ",0x%" PRIx64 ",0x%" PRIx64 ",0x%" PRIx64 ") = %" PRId64 "\n",
                number, a, b, c, d, e, f, result);
    }
    return result;
}
