/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Read-only QEMU payload filesystem for Zephyr.
 *
 * The caller-owned kernel and initrd blobs are exposed as
 * /guest/Image and /guest/initramfs.cpio.gz.  The blobs are not copied
 * and must remain valid for as long as the filesystem stays mounted.
 */

#ifndef QEMU_ZEPHYR_PAYLOAD_FS_H
#define QEMU_ZEPHYR_PAYLOAD_FS_H

#include <stddef.h>

/**
 * Mount the in-memory QEMU payload as a read-only Zephyr filesystem.
 *
 * @param kernel      Pointer to the kernel image blob.
 * @param kernel_size Size of @p kernel in bytes.
 * @param initrd      Pointer to the initrd blob.
 * @param initrd_size Size of @p initrd in bytes.
 *
 * A zero size with a NULL pointer denotes an empty file.
 *
 * The first successful call registers and mounts the filesystem at
 * "/guest".  Repeating the call with the same pointers and sizes is
 * idempotent and returns 0.  A call with different blobs while the
 * filesystem is mounted returns -EBUSY; open handles are never
 * invalidated by a repeated mount.  The mount is permanent for the
 * lifetime of the firmware and write operations are rejected.
 *
 * @return 0 on success, negative errno on failure.
 */
int qemu_zephyr_payload_mount(const void *kernel, size_t kernel_size,
                              const void *initrd, size_t initrd_size);

#endif /* QEMU_ZEPHYR_PAYLOAD_FS_H */
