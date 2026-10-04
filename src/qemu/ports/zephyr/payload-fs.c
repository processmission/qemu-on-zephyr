/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Read-only QEMU payload filesystem for Zephyr.
 *
 * This adapter registers a real Zephyr fs_file_system_t and exposes the
 * caller-owned QEMU payload blobs through the normal Zephyr fs_* and POSIX
 * file APIs.  The blobs are neither copied nor modified.
 */

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/fs/fs_sys.h>
#include <zephyr/kernel.h>

#include "payload-fs.h"

#define PAYLOAD_MOUNT_POINT "/guest"
#define PAYLOAD_MOUNT_POINT_LEN (sizeof(PAYLOAD_MOUNT_POINT) - 1)
#define PAYLOAD_FS_TYPE (FS_TYPE_EXTERNAL_BASE + 0x51)
#define PAYLOAD_MAX_FILES 8
#define PAYLOAD_MAX_DIRS 4
#define PAYLOAD_BLOCK_SIZE 512

/*
 * fs_dirent.name must be able to hold the longest exported name.
 * The expected configuration is CONFIG_FILE_SYSTEM_MAX_FILE_NAME=32.
 */
#ifndef CONFIG_FILE_SYSTEM
#error "CONFIG_FILE_SYSTEM must be enabled for qemu_zephyr_payload_mount()"
#elif MAX_FILE_NAME < 18
#error "CONFIG_FILE_SYSTEM_MAX_FILE_NAME must be at least 18"
#endif

enum payload_file_id {
    PAYLOAD_FILE_IMAGE = 0,
    PAYLOAD_FILE_INITRD,
    PAYLOAD_FILE_COUNT,
};

struct payload_blob {
    const uint8_t *data;
    size_t size;
};

struct payload_handle {
    bool in_use;
    enum payload_file_id file;
    off_t offset;
    fs_mode_t flags;
};

struct payload_dir_handle {
    bool in_use;
    size_t index;
};

static const char *const payload_file_names[PAYLOAD_FILE_COUNT] = {
    [PAYLOAD_FILE_IMAGE] = "Image",
    [PAYLOAD_FILE_INITRD] = "initramfs.cpio.gz",
};

static struct {
    struct payload_blob files[PAYLOAD_FILE_COUNT];
    bool registered;
    bool mounted;
    bool mount_armed;
} payload;

static struct payload_handle payload_handles[PAYLOAD_MAX_FILES];
static struct payload_dir_handle payload_dirs[PAYLOAD_MAX_DIRS];

K_MUTEX_DEFINE(payload_lock);

static struct fs_mount_t payload_mount = {
    .type = PAYLOAD_FS_TYPE,
    .mnt_point = PAYLOAD_MOUNT_POINT,
    .fs_data = &payload,
    .flags = FS_MOUNT_FLAG_READ_ONLY,
};

static off_t payload_off_max(void)
{
    return (off_t)((UINTMAX_MAX >>
                    (sizeof(uintmax_t) * CHAR_BIT -
                     sizeof(off_t) * CHAR_BIT)) >> 1);
}

static size_t payload_read_limit(void)
{
    return (size_t)((UINTMAX_MAX >>
                     (sizeof(uintmax_t) * CHAR_BIT -
                      sizeof(ssize_t) * CHAR_BIT)) >> 1);
}

static int payload_lookup(const char *path, enum payload_file_id *file)
{
    const char *name;
    size_t i;

    if (path == NULL) {
        return -EINVAL;
    }

    if (strncmp(path, PAYLOAD_MOUNT_POINT, PAYLOAD_MOUNT_POINT_LEN) != 0) {
        return -ENOENT;
    }

    name = path + PAYLOAD_MOUNT_POINT_LEN;
    if (*name == '/') {
        name++;
    } else if (*name != '\0') {
        /* /guestfoo is not below the /guest mount point. */
        return -ENOENT;
    }

    if (*name == '\0') {
        return -EISDIR;
    }

    for (i = 0; i < PAYLOAD_FILE_COUNT; i++) {
        if (strcmp(name, payload_file_names[i]) == 0) {
            *file = (enum payload_file_id)i;
            return 0;
        }
    }

    return -ENOENT;
}

static bool payload_is_mount_root(const char *path)
{
    if (path == NULL) {
        return false;
    }

    return strcmp(path, PAYLOAD_MOUNT_POINT) == 0 ||
           strcmp(path, PAYLOAD_MOUNT_POINT "/") == 0;
}

static struct payload_handle *payload_get_handle(void *filep)
{
    size_t i;

    for (i = 0; i < PAYLOAD_MAX_FILES; i++) {
        if (&payload_handles[i] == filep) {
            return &payload_handles[i];
        }
    }

    return NULL;
}

static struct payload_dir_handle *payload_get_dir_handle(void *dirp)
{
    size_t i;

    for (i = 0; i < PAYLOAD_MAX_DIRS; i++) {
        if (&payload_dirs[i] == dirp) {
            return &payload_dirs[i];
        }
    }

    return NULL;
}

static void payload_fill_dirent(enum payload_file_id file,
                                struct fs_dirent *entry)
{
    entry->type = FS_DIR_ENTRY_FILE;
    entry->size = payload.files[file].size;
    strncpy(entry->name, payload_file_names[file], MAX_FILE_NAME);
    entry->name[MAX_FILE_NAME] = '\0';
}

static int payload_open(struct fs_file_t *filp, const char *fs_path,
                        fs_mode_t flags)
{
    enum payload_file_id file;
    struct payload_handle *handle = NULL;
    size_t i;
    int rc;

    if ((flags & ~FS_O_MASK) != 0) {
        return -EINVAL;
    }

    if ((flags & (FS_O_WRITE | FS_O_CREATE | FS_O_APPEND |
                  FS_O_TRUNC)) != 0) {
        return -EROFS;
    }

    if (filp->filep != NULL) {
        return -EBUSY;
    }

    rc = payload_lookup(fs_path, &file);
    if (rc < 0) {
        return rc;
    }

    k_mutex_lock(&payload_lock, K_FOREVER);

    if (!payload.mounted) {
        rc = -ENODEV;
        goto out;
    }

    for (i = 0; i < PAYLOAD_MAX_FILES; i++) {
        if (!payload_handles[i].in_use) {
            handle = &payload_handles[i];
            break;
        }
    }

    if (handle == NULL) {
        rc = -EMFILE;
        goto out;
    }

    handle->in_use = true;
    handle->file = file;
    handle->offset = 0;
    handle->flags = flags;
    filp->filep = handle;
    rc = 0;

out:
    k_mutex_unlock(&payload_lock);
    return rc;
}

static ssize_t payload_read(struct fs_file_t *filp, void *dest, size_t nbytes)
{
    struct payload_handle *handle;
    const struct payload_blob *blob;
    size_t available;
    size_t limit;
    ssize_t rc;

    k_mutex_lock(&payload_lock, K_FOREVER);

    handle = payload_get_handle(filp->filep);
    if (handle == NULL || !handle->in_use) {
        rc = -EBADF;
        goto out;
    }

    if ((handle->flags & FS_O_READ) == 0) {
        rc = -EACCES;
        goto out;
    }

    if (nbytes == 0) {
        rc = 0;
        goto out;
    }

    if (dest == NULL) {
        rc = -EINVAL;
        goto out;
    }

    blob = &payload.files[handle->file];

    if (handle->offset < 0) {
        rc = -EINVAL;
        goto out;
    }

    if ((uintmax_t)handle->offset >= (uintmax_t)blob->size) {
        rc = 0;
        goto out;
    }

    available = blob->size - (size_t)handle->offset;
    if (nbytes > available) {
        nbytes = available;
    }

    limit = payload_read_limit();
    if (nbytes > limit) {
        nbytes = limit;
    }

    memcpy(dest, blob->data + (size_t)handle->offset, nbytes);
    handle->offset += (off_t)nbytes;
    rc = (ssize_t)nbytes;

out:
    k_mutex_unlock(&payload_lock);
    return rc;
}

static ssize_t payload_write(struct fs_file_t *filp, const void *src,
                             size_t nbytes)
{
    (void)filp;
    (void)src;
    (void)nbytes;

    return -EROFS;
}

static int payload_lseek(struct fs_file_t *filp, off_t off, int whence)
{
    struct payload_handle *handle;
    off_t base;
    off_t new_off;
    int rc = 0;

    k_mutex_lock(&payload_lock, K_FOREVER);

    handle = payload_get_handle(filp->filep);
    if (handle == NULL || !handle->in_use) {
        rc = -EBADF;
        goto out;
    }

    switch (whence) {
    case FS_SEEK_SET:
        if (off < 0) {
            rc = -EINVAL;
            goto out;
        }
        handle->offset = off;
        goto out;
    case FS_SEEK_CUR:
        base = handle->offset;
        break;
    case FS_SEEK_END:
        base = (off_t)payload.files[handle->file].size;
        break;
    default:
        rc = -EINVAL;
        goto out;
    }

    if (off >= 0) {
        if (base > payload_off_max() - off) {
            rc = -EOVERFLOW;
            goto out;
        }
        new_off = base + off;
    } else {
        /*
         * base is non-negative and off cannot be smaller than the
         * minimum off_t, so this addition cannot underflow.
         */
        new_off = base + off;
        if (new_off < 0) {
            rc = -EINVAL;
            goto out;
        }
    }

    handle->offset = new_off;

out:
    k_mutex_unlock(&payload_lock);
    return rc;
}

static off_t payload_tell(struct fs_file_t *filp)
{
    struct payload_handle *handle;
    off_t offset;

    k_mutex_lock(&payload_lock, K_FOREVER);

    handle = payload_get_handle(filp->filep);
    if (handle == NULL || !handle->in_use) {
        offset = -EBADF;
    } else {
        offset = handle->offset;
    }

    k_mutex_unlock(&payload_lock);
    return offset;
}

static int payload_truncate(struct fs_file_t *filp, off_t length)
{
    (void)filp;
    (void)length;

    return -EROFS;
}

static int payload_close(struct fs_file_t *filp)
{
    struct payload_handle *handle;
    int rc = -EBADF;

    k_mutex_lock(&payload_lock, K_FOREVER);

    handle = payload_get_handle(filp->filep);
    if (handle != NULL && handle->in_use) {
        handle->in_use = false;
        filp->filep = NULL;
        rc = 0;
    }

    k_mutex_unlock(&payload_lock);
    return rc;
}

static int payload_opendir(struct fs_dir_t *dirp, const char *fs_path)
{
    enum payload_file_id file;
    struct payload_dir_handle *handle = NULL;
    size_t i;
    int rc;

    if (!payload_is_mount_root(fs_path)) {
        rc = payload_lookup(fs_path, &file);
        if (rc == 0) {
            return -ENOTDIR;
        }
        return -ENOENT;
    }

    k_mutex_lock(&payload_lock, K_FOREVER);

    if (!payload.mounted) {
        rc = -ENODEV;
        goto out;
    }

    for (i = 0; i < PAYLOAD_MAX_DIRS; i++) {
        if (!payload_dirs[i].in_use) {
            handle = &payload_dirs[i];
            break;
        }
    }

    if (handle == NULL) {
        rc = -EMFILE;
        goto out;
    }

    handle->in_use = true;
    handle->index = 0;
    dirp->dirp = handle;
    rc = 0;

out:
    k_mutex_unlock(&payload_lock);
    return rc;
}

static int payload_readdir(struct fs_dir_t *dirp, struct fs_dirent *entry)
{
    struct payload_dir_handle *handle;
    int rc = 0;

    if (entry == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&payload_lock, K_FOREVER);

    handle = payload_get_dir_handle(dirp->dirp);
    if (handle == NULL || !handle->in_use) {
        rc = -EBADF;
        goto out;
    }

    if (handle->index >= PAYLOAD_FILE_COUNT) {
        entry->type = FS_DIR_ENTRY_FILE;
        entry->size = 0;
        entry->name[0] = '\0';
        goto out;
    }

    payload_fill_dirent((enum payload_file_id)handle->index, entry);
    handle->index++;

out:
    k_mutex_unlock(&payload_lock);
    return rc;
}

static int payload_closedir(struct fs_dir_t *dirp)
{
    struct payload_dir_handle *handle;
    int rc = -EBADF;

    k_mutex_lock(&payload_lock, K_FOREVER);

    handle = payload_get_dir_handle(dirp->dirp);
    if (handle != NULL && handle->in_use) {
        handle->in_use = false;
        dirp->dirp = NULL;
        rc = 0;
    }

    k_mutex_unlock(&payload_lock);
    return rc;
}

static int payload_unlink(struct fs_mount_t *mountp, const char *name)
{
    (void)mountp;
    (void)name;

    return -EROFS;
}

static int payload_rename(struct fs_mount_t *mountp, const char *from,
                          const char *to)
{
    (void)mountp;
    (void)from;
    (void)to;

    return -EROFS;
}

static int payload_mkdir(struct fs_mount_t *mountp, const char *name)
{
    (void)mountp;
    (void)name;

    return -EROFS;
}

static int payload_stat(struct fs_mount_t *mountp, const char *path,
                        struct fs_dirent *entry)
{
    enum payload_file_id file;
    int rc;

    (void)mountp;

    if (entry == NULL) {
        return -EINVAL;
    }

    if (payload_is_mount_root(path)) {
        entry->type = FS_DIR_ENTRY_DIR;
        entry->size = 0;
        strncpy(entry->name, PAYLOAD_MOUNT_POINT + 1, MAX_FILE_NAME);
        entry->name[MAX_FILE_NAME] = '\0';
        return 0;
    }

    rc = payload_lookup(path, &file);
    if (rc < 0) {
        return rc;
    }

    k_mutex_lock(&payload_lock, K_FOREVER);
    payload_fill_dirent(file, entry);
    k_mutex_unlock(&payload_lock);

    return 0;
}

static int payload_statvfs(struct fs_mount_t *mountp, const char *path,
                           struct fs_statvfs *stat)
{
    enum payload_file_id file;
    uintmax_t total;
    uintmax_t blocks;
    int rc;

    (void)mountp;

    if (stat == NULL) {
        return -EINVAL;
    }

    if (!payload_is_mount_root(path)) {
        rc = payload_lookup(path, &file);
        if (rc < 0) {
            return rc;
        }
    }

    total = (uintmax_t)payload.files[PAYLOAD_FILE_IMAGE].size +
            (uintmax_t)payload.files[PAYLOAD_FILE_INITRD].size;
    blocks = total / PAYLOAD_BLOCK_SIZE;
    if (blocks > (uintmax_t)ULONG_MAX) {
        blocks = (uintmax_t)ULONG_MAX;
    }

    stat->f_bsize = PAYLOAD_BLOCK_SIZE;
    stat->f_frsize = PAYLOAD_BLOCK_SIZE;
    stat->f_blocks = (unsigned long)blocks;
    stat->f_bfree = 0;

    return 0;
}

static int payload_mount_cb(struct fs_mount_t *mountp)
{
    if (mountp != &payload_mount || !payload.mount_armed) {
        return -EINVAL;
    }

    return 0;
}

static int payload_unmount_cb(struct fs_mount_t *mountp)
{
    (void)mountp;

    /*
     * The blobs belong to the caller for the whole mount lifetime, and
     * unmounting would invalidate open handles.  The mount is therefore
     * permanent until the firmware reboots.
     */
    return -EBUSY;
}

static const struct fs_file_system_t payload_fs = {
    .open = payload_open,
    .read = payload_read,
    .write = payload_write,
    .lseek = payload_lseek,
    .tell = payload_tell,
    .truncate = payload_truncate,
    .sync = NULL,
    .close = payload_close,
    .opendir = payload_opendir,
    .readdir = payload_readdir,
    .closedir = payload_closedir,
    .mount = payload_mount_cb,
    .unmount = payload_unmount_cb,
    .unlink = payload_unlink,
    .rename = payload_rename,
    .mkdir = payload_mkdir,
    .stat = payload_stat,
    .statvfs = payload_statvfs,
};

int qemu_zephyr_payload_mount(const void *kernel, size_t kernel_size,
                              const void *initrd, size_t initrd_size)
{
    off_t off_max = payload_off_max();
    int rc = 0;

    if ((kernel == NULL && kernel_size != 0) ||
        (initrd == NULL && initrd_size != 0)) {
        return -EINVAL;
    }

    if ((uintmax_t)kernel_size > (uintmax_t)off_max ||
        (uintmax_t)initrd_size > (uintmax_t)off_max) {
        return -EOVERFLOW;
    }

    k_mutex_lock(&payload_lock, K_FOREVER);

    if (payload.mounted) {
        if (payload.files[PAYLOAD_FILE_IMAGE].data == kernel &&
            payload.files[PAYLOAD_FILE_IMAGE].size == kernel_size &&
            payload.files[PAYLOAD_FILE_INITRD].data == initrd &&
            payload.files[PAYLOAD_FILE_INITRD].size == initrd_size) {
            rc = 0;
        } else {
            rc = -EBUSY;
        }
        goto out;
    }

    if (!payload.registered) {
        rc = fs_register(PAYLOAD_FS_TYPE, &payload_fs);
        if (rc < 0) {
            goto out;
        }
        payload.registered = true;
    }

    payload.files[PAYLOAD_FILE_IMAGE].data = kernel;
    payload.files[PAYLOAD_FILE_IMAGE].size = kernel_size;
    payload.files[PAYLOAD_FILE_INITRD].data = initrd;
    payload.files[PAYLOAD_FILE_INITRD].size = initrd_size;

    payload.mount_armed = true;
    rc = fs_mount(&payload_mount);
    payload.mount_armed = false;

    if (rc < 0) {
        payload.files[PAYLOAD_FILE_IMAGE].data = NULL;
        payload.files[PAYLOAD_FILE_IMAGE].size = 0;
        payload.files[PAYLOAD_FILE_INITRD].data = NULL;
        payload.files[PAYLOAD_FILE_INITRD].size = 0;

        if (payload.registered) {
            (void)fs_unregister(PAYLOAD_FS_TYPE, &payload_fs);
            payload.registered = false;
        }
        goto out;
    }

    payload.mounted = true;

out:
    k_mutex_unlock(&payload_lock);
    return rc;
}
