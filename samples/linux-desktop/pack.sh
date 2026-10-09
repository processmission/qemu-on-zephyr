#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
mkdir -p /bootfs/bin /bootfs/dev /bootfs/proc /bootfs/sys /bootfs/lower /bootfs/rw /bootfs/newroot
cp /bin/busybox.static /bootfs/bin/busybox
cp /boot-init /bootfs/init
chmod +x /bootfs/init
version=$(basename /lib/modules/*)
for module in loop squashfs overlay simpledrm; do
    modprobe --set-version "$version" --show-depends "$module" |
        while read -r operation module_path; do
            if [ "$operation" = insmod ]; then
                cp --parents "$module_path" /bootfs
            fi
        done
done
find /bootfs/lib/modules -name '*.zst' -exec zstd -d --rm {} +
find /bootfs/lib/modules -name '*.gz' -exec gunzip {} +
cp /lib/modules/"$version"/modules.builtin* /bootfs/lib/modules/"$version"/
cp /lib/modules/"$version"/modules.order /bootfs/lib/modules/"$version"/
depmod -b /bootfs "$version"
mksquashfs /rootfs /bootfs/rootfs.squashfs -noappend -comp xz -b 1M -no-xattrs -no-progress
cd /bootfs
find . -print0 | cpio --null -o -H newc | gzip -9 > /output/initramfs.cpio.gz
cp /rootfs/etc/demo-packages.txt /output/packages.txt
/unzboot/build/unzboot /boot/vmlinuz-virt /output/Image
cp /boot/config-"$version" /output/kernel.config
apk info -v linux-virt >> /output/packages.txt
du -sh /rootfs /bootfs
