/* SPDX-License-Identifier: GPL-2.0-or-later */
/* File adapters for the read-only Zephyr payload mount. */
#include "qemu/osdep.h"
#include "qapi/error.h"

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
        file_error(error, errno, "stat payload");
        return NULL;
    }
    if (st.st_size < 0 || (uint64_t)st.st_size >= SIZE_MAX) {
        file_error(error, EOVERFLOW, "payload size");
        return NULL;
    }
    buffer = g_malloc((size_t)st.st_size + 1);
    while (used < st.st_size) {
        ssize_t result = read(fd, buffer + used, st.st_size - used);

        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            file_error(error, result < 0 ? errno : EIO, "read payload");
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
        file_error(error, errno, "open payload");
        return false;
    }
    buffer = read_file(fd, &size, error);
    if (close(fd) != 0 && buffer != NULL) {
        file_error(error, errno, "close payload");
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
    file_error(error, EROFS, "write payload");
    return false;
}

GMappedFile *g_mapped_file_new_from_fd(gint fd, gboolean writable, GError **error)
{
    off_t saved = lseek(fd, 0, SEEK_CUR);
    GMappedFile *file;

    if (saved < 0 || lseek(fd, 0, SEEK_SET) < 0) {
        file_error(error, errno, "seek payload");
        return NULL;
    }
    file = g_new0(GMappedFile, 1);
    file->contents = read_file(fd, &file->length, error);
    if (lseek(fd, saved, SEEK_SET) != saved && file->contents != NULL) {
        file_error(error, errno, "restore payload position");
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
        error_setg(errp, "QEMU payload mount is read-only");
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
    error_setg(errp, "QEMU payload mount is read-only");
    errno = EROFS;
    return -1;
}

int qemu_close(int fd)
{
    return close(fd);
}
