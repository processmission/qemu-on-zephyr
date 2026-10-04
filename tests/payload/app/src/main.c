/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Independent EL1 test for qemu/ports/zephyr/payload-fs.c.
 *
 * The test uses two small constant blobs and exercises both the native
 * Zephyr fs_* API and the POSIX open/read/lseek/fstat/close path.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/fs/fs_sys.h>
#include <zephyr/kernel.h>
#include <zephyr/posix/dirent.h>
#include <zephyr/posix/fcntl.h>
#include <zephyr/posix/sys/stat.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/sys/util.h>

#include "payload-fs.h"

#define KERNEL_SIZE 19
#define INITRD_SIZE 9
#define PAYLOAD_HANDLE_TEST_COUNT 8

static const uint8_t kernel_blob[KERNEL_SIZE] = {
    'I', 'm', 'a', 'g', 'e', '-', 'p', 'a', 'y', 'l',
    'o', 'a', 'd', 0x00, 0xff, 0x10, 0x20, 0x7f, 0x80,
};

static const uint8_t initrd_blob[INITRD_SIZE] = {
    'i', 'n', 'i', 't', 'r', 'd', 0xde, 0xad, 0xbe,
};

static unsigned int checks;
static unsigned int failures;

static int dummy_mount_cb(struct fs_mount_t *mountp)
{
    (void)mountp;
    return 0;
}

static int dummy_unmount_cb(struct fs_mount_t *mountp)
{
    (void)mountp;
    return 0;
}

static const struct fs_file_system_t dummy_fs = {
    .mount = dummy_mount_cb,
    .unmount = dummy_unmount_cb,
};

static struct fs_mount_t dummy_mount = {
    .type = FS_TYPE_EXTERNAL_BASE + 0x52,
    .mnt_point = "/guest",
    .fs_data = &dummy_mount,
};

static void check(bool ok, const char *what)
{
    checks++;
    if (!ok) {
        failures++;
        printk("FAIL: %s\n", what);
    }
}

static void check_long(long actual, long expected, const char *what)
{
    checks++;
    if (actual != expected) {
        failures++;
        printk("FAIL: %s got=%ld want=%ld\n", what, actual, expected);
    }
}

static void check_fs_rc(int rc, int expected, const char *what)
{
    checks++;
    if (rc != expected) {
        failures++;
        printk("FAIL: %s rc=%d want=%d\n", what, rc, expected);
    }
}

static void check_posix_errno(int rc, int expected, const char *what)
{
    int saved = errno;

    checks++;
    if (rc != -1 || saved != expected) {
        failures++;
        printk("FAIL: %s rc=%d errno=%d want errno=%d\n",
               what, rc, saved, expected);
    }
}

static void check_buffer(const uint8_t *actual, const uint8_t *expected,
                         size_t size, const char *what)
{
    checks++;
    if (memcmp(actual, expected, size) != 0) {
        failures++;
        printk("FAIL: %s buffer mismatch\n", what);
    }
}

static void test_mount_rollback(void)
{
    const int probe_type_a = FS_TYPE_EXTERNAL_BASE + 0x53;
    const int probe_type_b = FS_TYPE_EXTERNAL_BASE + 0x54;
    struct fs_dirent entry;
    int rc;

    rc = fs_register(dummy_mount.type, &dummy_fs);
    check_fs_rc(rc, 0, "register dummy fs");

    rc = fs_mount(&dummy_mount);
    check_fs_rc(rc, 0, "mount dummy fs at /guest");

    rc = qemu_zephyr_payload_mount(kernel_blob, KERNEL_SIZE,
                                   initrd_blob, INITRD_SIZE);
    check_fs_rc(rc, -EBUSY, "payload mount failure is reported");

    rc = fs_unmount(&dummy_mount);
    check_fs_rc(rc, 0, "unmount dummy fs");

    rc = fs_unregister(dummy_mount.type, &dummy_fs);
    check_fs_rc(rc, 0, "unregister dummy fs");

    /*
     * With CONFIG_FILE_SYSTEM_MAX_TYPES=2, two new registrations can
     * only succeed if the failed payload mount released its slot.
     */
    rc = fs_register(probe_type_a, &dummy_fs);
    check_fs_rc(rc, 0, "first probe registration after rollback");
    rc = fs_register(probe_type_b, &dummy_fs);
    check_fs_rc(rc, 0, "second probe registration after rollback");
    rc = fs_unregister(probe_type_a, &dummy_fs);
    check_fs_rc(rc, 0, "unregister first probe");
    rc = fs_unregister(probe_type_b, &dummy_fs);
    check_fs_rc(rc, 0, "unregister second probe");

    rc = qemu_zephyr_payload_mount(kernel_blob, KERNEL_SIZE,
                                   initrd_blob, INITRD_SIZE);
    check_fs_rc(rc, 0, "payload mount succeeds after rollback");

    rc = fs_stat("/guest/Image", &entry);
    check_fs_rc(rc, 0, "payload filesystem usable after rollback");
}

static void test_mount_rules(void)
{
    static const uint8_t other_kernel[KERNEL_SIZE] = { 0 };
    struct fs_file_t held;
    uint8_t buffer[4];
    int rc;

    rc = qemu_zephyr_payload_mount(NULL, 1, initrd_blob, INITRD_SIZE);
    check_fs_rc(rc, -EINVAL, "mount rejects NULL kernel with nonzero size");

    rc = qemu_zephyr_payload_mount(kernel_blob, KERNEL_SIZE, NULL, 1);
    check_fs_rc(rc, -EINVAL, "mount rejects NULL initrd with nonzero size");

    if (sizeof(size_t) >= 8) {
        rc = qemu_zephyr_payload_mount(kernel_blob, SIZE_MAX,
                                       initrd_blob, INITRD_SIZE);
        check_fs_rc(rc, -EOVERFLOW, "mount rejects oversized blob");
    }

    rc = qemu_zephyr_payload_mount(kernel_blob, KERNEL_SIZE,
                                   initrd_blob, INITRD_SIZE);
    check_fs_rc(rc, 0, "first mount succeeds");

    rc = qemu_zephyr_payload_mount(kernel_blob, KERNEL_SIZE,
                                   initrd_blob, INITRD_SIZE);
    check_fs_rc(rc, 0, "same mount is idempotent");

    rc = qemu_zephyr_payload_mount(other_kernel, KERNEL_SIZE,
                                   initrd_blob, INITRD_SIZE);
    check_fs_rc(rc, -EBUSY, "different blob is rejected with EBUSY");

    rc = qemu_zephyr_payload_mount(kernel_blob, KERNEL_SIZE,
                                   initrd_blob, INITRD_SIZE);
    check_fs_rc(rc, 0, "original mount still valid after EBUSY");

    fs_file_t_init(&held);
    rc = fs_open(&held, "/guest/Image", FS_O_READ);
    check_fs_rc(rc, 0, "open handle before remount attempt");

    rc = qemu_zephyr_payload_mount(kernel_blob, KERNEL_SIZE,
                                   initrd_blob, INITRD_SIZE);
    check_fs_rc(rc, 0, "idempotent mount with open handle");

    rc = fs_read(&held, buffer, 2);
    check_fs_rc(rc, 2, "read through open handle after idempotent mount");
    check_buffer(buffer, kernel_blob, 2,
                 "open handle data after idempotent mount");

    rc = qemu_zephyr_payload_mount(other_kernel, KERNEL_SIZE,
                                   initrd_blob, INITRD_SIZE);
    check_fs_rc(rc, -EBUSY, "different blob with open handle");

    rc = fs_read(&held, buffer, 2);
    check_fs_rc(rc, 2, "read through open handle after EBUSY");
    check_buffer(buffer, kernel_blob + 2, 2,
                 "open handle data after EBUSY");

    rc = fs_close(&held);
    check_fs_rc(rc, 0, "close handle after remount attempts");
}

static void test_stat_api(void)
{
    struct fs_dirent entry;
    struct stat st;
    struct fs_statvfs statvfs;
    int rc;

    rc = fs_stat("/guest", &entry);
    check_fs_rc(rc, 0, "fs_stat /guest");
    check(entry.type == FS_DIR_ENTRY_DIR, "fs_stat /guest type dir");
    check(entry.size == 0, "fs_stat /guest size zero");
    check(strcmp(entry.name, "guest") == 0, "fs_stat /guest name");

    rc = fs_stat("/guest/", &entry);
    check_fs_rc(rc, 0, "fs_stat /guest/");
    check(entry.type == FS_DIR_ENTRY_DIR, "fs_stat /guest/ type dir");

    rc = fs_stat("/guest/Image", &entry);
    check_fs_rc(rc, 0, "fs_stat /guest/Image");
    check(entry.type == FS_DIR_ENTRY_FILE, "fs_stat Image type file");
    check(entry.size == KERNEL_SIZE, "fs_stat Image size");
    check(strcmp(entry.name, "Image") == 0, "fs_stat Image name");

    rc = fs_stat("/guest/initramfs.cpio.gz", &entry);
    check_fs_rc(rc, 0, "fs_stat /guest/initramfs.cpio.gz");
    check(entry.type == FS_DIR_ENTRY_FILE, "fs_stat initrd type file");
    check(entry.size == INITRD_SIZE, "fs_stat initrd size");
    check(strcmp(entry.name, "initramfs.cpio.gz") == 0,
          "fs_stat initrd name");

    rc = fs_stat("/guest/unknown", &entry);
    check_fs_rc(rc, -ENOENT, "fs_stat unknown path");

    rc = fs_statvfs("/guest/Image", &statvfs);
    check_fs_rc(rc, 0, "fs_statvfs file");
    check(statvfs.f_bsize == 512, "fs_statvfs f_bsize");
    check(statvfs.f_bfree == 0, "fs_statvfs read-only f_bfree");

    rc = fs_statvfs("/guest", &statvfs);
    check_fs_rc(rc, 0, "fs_statvfs mount root");

    rc = fs_statvfs("/guest/unknown", &statvfs);
    check_fs_rc(rc, -ENOENT, "fs_statvfs unknown path");

    rc = stat("/guest", &st);
    check_fs_rc(rc, 0, "posix stat /guest");
    check(S_ISDIR(st.st_mode), "posix stat /guest mode dir");

    rc = stat("/guest/Image", &st);
    check_fs_rc(rc, 0, "posix stat /guest/Image");
    check(S_ISREG(st.st_mode), "posix stat Image mode file");
    check_long((long)st.st_size, KERNEL_SIZE, "posix stat Image size");
    check(st.st_blksize == 512, "posix stat Image blksize");

    rc = stat("/guest/initramfs.cpio.gz", &st);
    check_fs_rc(rc, 0, "posix stat initrd");
    check_long((long)st.st_size, INITRD_SIZE, "posix stat initrd size");

    rc = stat("/guest/unknown", &st);
    check_posix_errno(rc, ENOENT, "posix stat unknown path");
}

static void test_fs_file_api(void)
{
    struct fs_file_t first;
    struct fs_file_t second;
    struct fs_file_t third;
    struct fs_file_t pool[9];
    uint8_t buffer[64];
    int rc;
    size_t i;

    fs_file_t_init(&first);
    fs_file_t_init(&second);
    fs_file_t_init(&third);

    rc = fs_open(&first, "/guest/Image", FS_O_READ);
    check_fs_rc(rc, 0, "fs_open Image");
    rc = fs_open(&second, "/guest/initramfs.cpio.gz", FS_O_READ);
    check_fs_rc(rc, 0, "fs_open initrd");

    rc = fs_read(&first, buffer, 3);
    check_fs_rc(rc, 3, "fs_read Image first");
    check_buffer(buffer, kernel_blob, 3, "fs_read Image first");

    rc = fs_read(&second, buffer, 2);
    check_fs_rc(rc, 2, "fs_read initrd first");
    check_buffer(buffer, initrd_blob, 2, "fs_read initrd first");

    rc = fs_read(&first, buffer, 2);
    check_fs_rc(rc, 2, "fs_read Image second");
    check_buffer(buffer, kernel_blob + 3, 2, "fs_read Image second");

    check_long((long)fs_tell(&first), 5, "fs_tell Image");
    check_long((long)fs_tell(&second), 2, "fs_tell initrd");

    rc = fs_seek(&first, 0, FS_SEEK_SET);
    check_fs_rc(rc, 0, "fs_seek SET");
    check_long((long)fs_tell(&first), 0, "fs_tell after SET");

    rc = fs_seek(&first, 0, FS_SEEK_END);
    check_fs_rc(rc, 0, "fs_seek END");
    check_long((long)fs_tell(&first), KERNEL_SIZE, "fs_tell at EOF");

    rc = fs_read(&first, buffer, sizeof(buffer));
    check_fs_rc(rc, 0, "fs_read at EOF");

    rc = fs_seek(&first, -1, FS_SEEK_END);
    check_fs_rc(rc, 0, "fs_seek END-1");
    rc = fs_read(&first, buffer, 1);
    check_fs_rc(rc, 1, "fs_read last byte");
    check_buffer(buffer, kernel_blob + KERNEL_SIZE - 1, 1,
                 "fs_read last byte data");

    rc = fs_seek(&first, 100, FS_SEEK_END);
    check_fs_rc(rc, 0, "fs_seek past EOF");
    check_long((long)fs_tell(&first), KERNEL_SIZE + 100,
               "fs_tell past EOF");
    rc = fs_read(&first, buffer, 1);
    check_fs_rc(rc, 0, "fs_read past EOF");

    rc = fs_seek(&first, -1, FS_SEEK_SET);
    check_fs_rc(rc, -EINVAL, "fs_seek negative SET");
    rc = fs_seek(&first, -1000, FS_SEEK_END);
    check_fs_rc(rc, -EINVAL, "fs_seek negative END");
    rc = fs_seek(&first, 0, 99);
    check_fs_rc(rc, -EINVAL, "fs_seek bad whence");

    if (sizeof(off_t) >= 8) {
        rc = fs_seek(&first, (off_t)INT64_MAX, FS_SEEK_SET);
        check_fs_rc(rc, 0, "fs_seek to off_t max");
        rc = fs_seek(&first, 1, FS_SEEK_CUR);
        check_fs_rc(rc, -EOVERFLOW, "fs_seek overflow");
    }

    rc = fs_seek(&first, 0, FS_SEEK_SET);
    check_fs_rc(rc, 0, "fs_seek rewind");
    rc = fs_read(&first, NULL, 0);
    check_fs_rc(rc, 0, "fs_read zero bytes");
    rc = fs_read(&first, buffer, sizeof(buffer));
    check_fs_rc(rc, KERNEL_SIZE, "fs_read whole Image");
    check_buffer(buffer, kernel_blob, KERNEL_SIZE, "fs_read whole Image");

    fs_file_t_init(&third);
    rc = fs_open(&third, "/guest/Image", FS_O_READ);
    check_fs_rc(rc, 0, "fs_open second Image handle");
    rc = fs_seek(&first, 0, FS_SEEK_SET);
    check_fs_rc(rc, 0, "rewind first Image handle");
    rc = fs_read(&first, buffer, 2);
    check_fs_rc(rc, 2, "read first handle");
    rc = fs_read(&third, buffer, 3);
    check_fs_rc(rc, 3, "read second handle");
    check_buffer(buffer, kernel_blob, 3, "second handle data");
    rc = fs_read(&first, buffer, 2);
    check_fs_rc(rc, 2, "read first handle again");
    check_buffer(buffer, kernel_blob + 2, 2, "first handle data");
    check_long((long)fs_tell(&first), 4, "first handle offset independent");
    check_long((long)fs_tell(&third), 3, "second handle offset independent");
    rc = fs_close(&third);
    check_fs_rc(rc, 0, "fs_close second Image handle");

    rc = fs_write(&first, kernel_blob, 1);
    check_fs_rc(rc, -EROFS, "fs_write is rejected");
    rc = fs_truncate(&first, 0);
    check_fs_rc(rc, -EROFS, "fs_truncate is rejected");
    rc = fs_sync(&first);
    check_fs_rc(rc, -ENOTSUP, "fs_sync is not silently successful");

    rc = fs_close(&first);
    check_fs_rc(rc, 0, "fs_close Image");
    rc = fs_close(&second);
    check_fs_rc(rc, 0, "fs_close initrd");
    rc = fs_close(&first);
    check_fs_rc(rc, 0, "fs_close again is harmless");
    rc = fs_read(&first, buffer, 1);
    check_fs_rc(rc, -EBADF, "fs_read after close");

    fs_file_t_init(&third);
    rc = fs_open(&third, "/guest/Image", 0);
    check_fs_rc(rc, 0, "fs_open with flags zero");
    rc = fs_read(&third, buffer, 1);
    check_fs_rc(rc, -EACCES, "fs_read without read access");
    rc = fs_close(&third);
    check_fs_rc(rc, 0, "fs_close flags-zero handle");

    rc = fs_open(&third, "/guest/Image", FS_O_WRITE);
    check_fs_rc(rc, -EROFS, "fs_open write");
    rc = fs_open(&third, "/guest/Image", FS_O_CREATE);
    check_fs_rc(rc, -EROFS, "fs_open create");
    rc = fs_open(&third, "/guest/Image", FS_O_TRUNC);
    check_fs_rc(rc, -EACCES, "fs_open truncate without write");
    rc = fs_open(&third, "/guest/Image", FS_O_APPEND);
    check_fs_rc(rc, -EROFS, "fs_open append");
    rc = fs_open(&third, "/guest/Image", FS_O_RDWR);
    check_fs_rc(rc, -EROFS, "fs_open read-write");
    rc = fs_open(&third, "/guest/Image", FS_O_READ | 0x80);
    check_fs_rc(rc, -EINVAL, "fs_open unknown flag");
    rc = fs_open(&third, "/guest/unknown", FS_O_READ);
    check_fs_rc(rc, -ENOENT, "fs_open unknown path");
    rc = fs_open(&third, "/guest", FS_O_READ);
    check_fs_rc(rc, -EISDIR, "fs_open mount root");

    rc = fs_unlink("/guest/Image");
    check_fs_rc(rc, -EROFS, "fs_unlink is rejected");
    rc = fs_mkdir("/guest/newdir");
    check_fs_rc(rc, -EROFS, "fs_mkdir is rejected");
    rc = fs_rename("/guest/Image", "/guest/renamed");
    check_fs_rc(rc, -EROFS, "fs_rename is rejected");

    for (i = 0; i < ARRAY_SIZE(pool); i++) {
        fs_file_t_init(&pool[i]);
    }
    for (i = 0; i < PAYLOAD_HANDLE_TEST_COUNT; i++) {
        rc = fs_open(&pool[i], "/guest/Image", FS_O_READ);
        check_fs_rc(rc, 0, "fs_open pool handle");
    }
    rc = fs_open(&pool[PAYLOAD_HANDLE_TEST_COUNT], "/guest/Image",
                 FS_O_READ);
    check_fs_rc(rc, -EMFILE, "fs_open handle pool exhausted");
    for (i = 0; i < PAYLOAD_HANDLE_TEST_COUNT; i++) {
        rc = fs_close(&pool[i]);
        check_fs_rc(rc, 0, "fs_close pool handle");
    }
}

static void test_posix_file_api(void)
{
    struct stat st;
    uint8_t buffer[64];
    int image_fd;
    int initrd_fd;
    int rc;
    off_t offset;

    image_fd = open("/guest/Image", O_RDONLY);
    check(image_fd >= 0, "posix open Image");
    initrd_fd = open("/guest/initramfs.cpio.gz", O_RDONLY);
    check(initrd_fd >= 0, "posix open initrd");

    rc = (int)read(image_fd, buffer, 4);
    check_long(rc, 4, "posix read Image first");
    check_buffer(buffer, kernel_blob, 4, "posix read Image first data");

    rc = (int)read(initrd_fd, buffer, 2);
    check_long(rc, 2, "posix read initrd first");
    check_buffer(buffer, initrd_blob, 2, "posix read initrd first data");

    rc = (int)read(image_fd, buffer, 3);
    check_long(rc, 3, "posix read Image second");
    check_buffer(buffer, kernel_blob + 4, 3,
                 "posix read Image second data");

    offset = lseek(image_fd, 0, SEEK_CUR);
    check_long((long)offset, 7, "posix lseek CUR Image");
    offset = lseek(initrd_fd, 0, SEEK_CUR);
    check_long((long)offset, 2, "posix lseek CUR initrd");

    rc = fstat(image_fd, &st);
    check_fs_rc(rc, 0, "posix fstat Image");
    check(S_ISREG(st.st_mode), "posix fstat Image mode file");
    check_long((long)st.st_size, KERNEL_SIZE, "posix fstat Image size");

    rc = fstat(initrd_fd, &st);
    check_fs_rc(rc, 0, "posix fstat initrd");
    check_long((long)st.st_size, INITRD_SIZE, "posix fstat initrd size");

    offset = lseek(image_fd, 2, SEEK_SET);
    check_long((long)offset, 2, "posix lseek SET");
    rc = (int)read(image_fd, buffer, 2);
    check_long(rc, 2, "posix read after SET");
    check_buffer(buffer, kernel_blob + 2, 2, "posix read after SET data");

    offset = lseek(image_fd, 0, SEEK_END);
    check_long((long)offset, KERNEL_SIZE, "posix lseek END");
    offset = lseek(image_fd, -1, SEEK_END);
    check_long((long)offset, KERNEL_SIZE - 1, "posix lseek END-1");
    rc = (int)read(image_fd, buffer, 1);
    check_long(rc, 1, "posix read last byte");
    check_buffer(buffer, kernel_blob + KERNEL_SIZE - 1, 1,
                 "posix read last byte data");

    offset = lseek(image_fd, -2, SEEK_CUR);
    check_long((long)offset, KERNEL_SIZE - 2, "posix lseek CUR-2");
    rc = (int)read(image_fd, buffer, 1);
    check_long(rc, 1, "posix read after CUR-2");
    check_buffer(buffer, kernel_blob + KERNEL_SIZE - 2, 1,
                 "posix read after CUR-2 data");

    offset = lseek(image_fd, 100, SEEK_END);
    check_long((long)offset, KERNEL_SIZE + 100, "posix lseek past EOF");
    rc = (int)read(image_fd, buffer, 1);
    check_long(rc, 0, "posix read past EOF");

    rc = fstat(image_fd, &st);
    check_fs_rc(rc, 0, "posix fstat after seek past EOF");
    check_long((long)st.st_size, KERNEL_SIZE, "posix fstat size stable");
    offset = lseek(image_fd, 0, SEEK_CUR);
    check_long((long)offset, KERNEL_SIZE + 100,
               "posix fstat preserves offset");

    rc = (int)read(image_fd, NULL, 0);
    check_long(rc, 0, "posix read zero bytes");

    offset = lseek(image_fd, 0, SEEK_SET);
    check_long((long)offset, 0, "posix lseek rewind");
    rc = (int)read(image_fd, buffer, sizeof(buffer));
    check_long(rc, KERNEL_SIZE, "posix read whole Image");
    check_buffer(buffer, kernel_blob, KERNEL_SIZE,
                 "posix read whole Image data");

    offset = lseek(image_fd, -1, SEEK_SET);
    check_posix_errno((int)offset, EINVAL, "posix lseek negative SET");
    offset = lseek(image_fd, -1000, SEEK_END);
    check_posix_errno((int)offset, EINVAL, "posix lseek negative END");

    rc = close(image_fd);
    check_fs_rc(rc, 0, "posix close Image");
    rc = close(initrd_fd);
    check_fs_rc(rc, 0, "posix close initrd");
    rc = (int)read(image_fd, buffer, 1);
    check_posix_errno(rc, EBADF, "posix read closed fd");

    rc = open("/guest/Image", O_WRONLY);
    check_posix_errno(rc, EROFS, "posix open O_WRONLY");
    rc = open("/guest/Image", O_RDWR);
    check_posix_errno(rc, EROFS, "posix open O_RDWR");
    rc = open("/guest/Image", O_RDONLY | O_CREAT, 0644);
    check_posix_errno(rc, EROFS, "posix open O_CREAT");
    rc = open("/guest/Image", O_RDONLY | O_TRUNC);
    check_posix_errno(rc, EACCES, "posix open O_TRUNC without write");
    rc = open("/guest/Image", O_RDONLY | O_APPEND);
    check_posix_errno(rc, EROFS, "posix open O_APPEND");
    rc = open("/guest/unknown", O_RDONLY);
    check_posix_errno(rc, ENOENT, "posix open unknown path");
    rc = open("/guest", O_RDONLY);
    check_posix_errno(rc, EISDIR, "posix open mount root");
}

static void test_directory_api(void)
{
    struct fs_dir_t fs_dir;
    struct fs_dirent entry;
    DIR *posix_dir;
    struct dirent *dirent;
    bool saw_image = false;
    bool saw_initrd = false;
    int rc;

    fs_dir_t_init(&fs_dir);
    rc = fs_opendir(&fs_dir, "/guest");
    check_fs_rc(rc, 0, "fs_opendir /guest");

    rc = fs_readdir(&fs_dir, &entry);
    check_fs_rc(rc, 0, "fs_readdir first");
    check(entry.type == FS_DIR_ENTRY_FILE, "fs_readdir first type");
    check(strcmp(entry.name, "Image") == 0, "fs_readdir first name");
    check(entry.size == KERNEL_SIZE, "fs_readdir first size");

    rc = fs_readdir(&fs_dir, &entry);
    check_fs_rc(rc, 0, "fs_readdir second");
    check(strcmp(entry.name, "initramfs.cpio.gz") == 0,
          "fs_readdir second name");
    check(entry.size == INITRD_SIZE, "fs_readdir second size");

    rc = fs_readdir(&fs_dir, &entry);
    check_fs_rc(rc, 0, "fs_readdir end");
    check(entry.name[0] == '\0', "fs_readdir end marker");

    rc = fs_closedir(&fs_dir);
    check_fs_rc(rc, 0, "fs_closedir");

    fs_dir_t_init(&fs_dir);
    rc = fs_opendir(&fs_dir, "/guest/Image");
    check_fs_rc(rc, -ENOTDIR, "fs_opendir on file");
    rc = fs_opendir(&fs_dir, "/guest/unknown");
    check_fs_rc(rc, -ENOENT, "fs_opendir unknown path");

    posix_dir = opendir("/guest");
    check(posix_dir != NULL, "posix opendir /guest");
    if (posix_dir != NULL) {
        while ((dirent = readdir(posix_dir)) != NULL) {
            if (strcmp(dirent->d_name, "Image") == 0) {
                saw_image = true;
            } else if (strcmp(dirent->d_name,
                              "initramfs.cpio.gz") == 0) {
                saw_initrd = true;
            }
        }
        check(saw_image, "posix readdir saw Image");
        check(saw_initrd, "posix readdir saw initrd");
        rc = closedir(posix_dir);
        check_fs_rc(rc, 0, "posix closedir");
    }
}

int main(void)
{
    uint64_t current_el;

    __asm__ volatile("mrs %0, CurrentEL" : "=r"(current_el));
    printk("PAYLOAD_FS_TEST_START\n");
    printk("PAYLOAD_FS_TEST_EL: EL%u\n", (unsigned int)(current_el >> 2));

    test_mount_rollback();
    test_mount_rules();
    test_stat_api();
    test_fs_file_api();
    test_posix_file_api();
    test_directory_api();

    printk("PAYLOAD_FS_TEST_CHECKS: %u\n", checks);
    printk("PAYLOAD_FS_TEST_FAILURES: %u\n", failures);
    if (failures == 0) {
        printk("PAYLOAD_FS_TEST_RESULT: PASS\n");
    } else {
        printk("PAYLOAD_FS_TEST_RESULT: FAIL\n");
    }

    return failures == 0 ? 0 : 1;
}
