/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Read-only QEMU file adapters for Zephyr-mounted filesystems. */
#include "qemu/osdep.h"
#include "qapi/error.h"

#ifdef CONFIG_QEMU_USER
/* Zephyr POSIX pread/pwrite currently support shared-memory descriptors.
 * QEMU owns these regular-file descriptors on its single worker thread. */
static ssize_t positional_io(int fd, void *buffer, size_t size, off_t offset,
                              bool writing)
{
    off_t saved;
    ssize_t result;
    int saved_errno;

    if (offset < 0) {
        errno = EINVAL;
        return -1;
    }
    saved = lseek(fd, 0, SEEK_CUR);
    if (saved < 0 || lseek(fd, offset, SEEK_SET) < 0) {
        return -1;
    }
    result = writing ? write(fd, buffer, size) : read(fd, buffer, size);
    saved_errno = errno;
    if (lseek(fd, saved, SEEK_SET) < 0 && result >= 0) {
        return -1;
    }
    errno = saved_errno;
    return result;
}

ssize_t qemu_zephyr_pread(int fd, void *buffer, size_t size, off_t offset)
{
    return positional_io(fd, buffer, size, offset, false);
}

ssize_t qemu_zephyr_pwrite(int fd, const void *buffer, size_t size, off_t offset)
{
    return positional_io(fd, (void *)buffer, size, offset, true);
}
#endif

struct _GMappedFile {
    gchar *contents;
    gsize length;
    unsigned int references;
};

static void file_error(GError **error, int code, const char *operation)
{
    if (error == NULL) {
        return;
    }
    assert(*error == NULL);
    *error = g_new0(GError, 1);
    (*error)->domain = G_FILE_ERROR;
    (*error)->code = g_file_error_from_errno(code);
    (*error)->message = g_strdup_printf("%s: %s", operation, strerror(code));
}

static gchar *read_file(int fd, gsize *length, GError **error)
{
    struct stat st;
    gchar *buffer;
    gsize used = 0;

    if (fstat(fd, &st) != 0) {
        file_error(error, errno, "stat image");
        return NULL;
    }
    if (st.st_size < 0 || (uint64_t)st.st_size >= SIZE_MAX) {
        file_error(error, EOVERFLOW, "image size");
        return NULL;
    }
    buffer = g_malloc((size_t)st.st_size + 1);
    while (used < st.st_size) {
        ssize_t result = read(fd, buffer + used, st.st_size - used);

        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            file_error(error, result < 0 ? errno : EIO, "read image");
            g_free(buffer);
            return NULL;
        }
        used += result;
    }
    buffer[used] = '\0';
    *length = used;
    return buffer;
}

gboolean g_file_get_contents(const gchar *filename, gchar **contents,
                             gsize *length, GError **error)
{
    int fd = open(filename, O_RDONLY);
    gsize size;
    gchar *buffer;

    *contents = NULL;
    if (fd < 0) {
        file_error(error, errno, "open image");
        return false;
    }
    buffer = read_file(fd, &size, error);
    if (close(fd) != 0 && buffer != NULL) {
        file_error(error, errno, "close image");
        g_free(buffer);
        return false;
    }
    if (buffer == NULL) {
        return false;
    }
    *contents = buffer;
    if (length != NULL) {
        *length = size;
    }
    return true;
}

gboolean g_file_set_contents(const gchar *filename, const gchar *contents,
                             gssize length, GError **error)
{
    file_error(error, EROFS, "write image");
    return false;
}

GMappedFile *g_mapped_file_new_from_fd(gint fd, gboolean writable, GError **error)
{
    off_t saved = lseek(fd, 0, SEEK_CUR);
    GMappedFile *file;

    if (saved < 0 || lseek(fd, 0, SEEK_SET) < 0) {
        file_error(error, errno, "seek image");
        return NULL;
    }
    file = g_new0(GMappedFile, 1);
    file->contents = read_file(fd, &file->length, error);
    if (lseek(fd, saved, SEEK_SET) != saved && file->contents != NULL) {
        file_error(error, errno, "restore image position");
        g_free(file->contents);
        file->contents = NULL;
    }
    if (file->contents == NULL) {
        g_free(file);
        return NULL;
    }
    /* Private snapshot: ELF relocation edits never change the original blob. */
    file->references = 1;
    return file;
}

GMappedFile *g_mapped_file_ref(GMappedFile *file)
{
    __atomic_add_fetch(&file->references, 1, __ATOMIC_RELAXED);
    return file;
}

void g_mapped_file_unref(GMappedFile *file)
{
    if (__atomic_sub_fetch(&file->references, 1, __ATOMIC_ACQ_REL) == 0) {
        g_free(file->contents);
        g_free(file);
    }
}

gsize g_mapped_file_get_length(GMappedFile *file)
{
    return file->length;
}

gchar *g_mapped_file_get_contents(GMappedFile *file)
{
    return file->contents;
}

int qemu_open(const char *name, int flags, Error **errp)
{
    int fd;

    if ((flags & O_ACCMODE) != O_RDONLY || (flags & (O_CREAT | O_TRUNC)) != 0) {
        error_setg(errp, "QEMU image files are opened read-only");
        errno = EROFS;
        return -1;
    }
    fd = open(name, flags);
    if (fd < 0) {
        error_setg_errno(errp, errno, "Could not open '%s'", name);
    }
    return fd;
}

int qemu_open_old(const char *name, int flags, ...)
{
    return qemu_open(name, flags, NULL);
}

int qemu_create(const char *name, int flags, mode_t mode, Error **errp)
{
    error_setg(errp, "QEMU image files are opened read-only");
    errno = EROFS;
    return -1;
}

int qemu_close(int fd)
{
    return close(fd);
}
