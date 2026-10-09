#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
output_owner=$(stat -c '%u:%g' /output)
trap 'chown -R "$output_owner" /output' EXIT
mkdir -p /bootfs/bin /bootfs/dev /bootfs/proc /bootfs/sys /bootfs/lower /bootfs/rw /bootfs/newroot /output/boot
cp /bin/busybox.static /bootfs/bin/busybox
cp /boot-init /bootfs/init
chmod +x /bootfs/init /rootfs/init
version=$(basename /lib/modules/*)
for module in squashfs overlay simpledrm; do
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
mksquashfs /rootfs /rootfs.squashfs -noappend -comp zstd -b 128K -no-xattrs -no-progress
size=$(stat -c %s /rootfs.squashfs)
[ "$size" -le 4294967296 ] || { echo 'NanoJev rootfs exceeds the 4 GiB image region' >&2; exit 1; }
truncate -s 10G /output/memory.img
dd if=/rootfs.squashfs of=/output/memory.img bs=1M seek=5120 conv=notrunc
cd /bootfs
find . -print0 | cpio --null -o -H newc | gzip -9 > /output/boot/initramfs.cpio.gz
/unzboot/build/unzboot /boot/vmlinuz-virt /output/boot/Image
cp /boot/config-"$version" /output/kernel.config
cp /rootfs/opt/nanojev/checkpoint/provenance.json /output/model.json
cp /rootfs/opt/python-packages.txt /output/python-packages.txt
cp /rootfs/opt/debian-packages.txt /output/debian-packages.txt
apk info -v linux-virt > /output/kernel-package.txt
