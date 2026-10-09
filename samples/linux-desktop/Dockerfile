FROM public.ecr.aws/docker/library/alpine:3.23.4@sha256:5b10f432ef3da1b8d4c7eb6c487f2f5a8f096bc91145e68878dd4a5019afde11

RUN apk add --no-cache cpio squashfs-tools busybox-static linux-virt kmod zstd && \
    mkdir /rootfs && \
    apk --root /rootfs --initdb --no-cache add \
        --keys-dir /etc/apk/keys --repositories-file /etc/apk/repositories \
        alpine-baselayout alpine-release busybox musl-utils xorg-server xf86-video-fbdev \
        jwm xterm xclock xdotool xsetroot font-dejavu && \
    apk --root /rootfs info -v > /rootfs/etc/demo-packages.txt
RUN apk add --no-cache build-base meson glib-dev zlib-dev zstd-dev git && \
    git init /unzboot && \
    git -C /unzboot fetch --depth 1 https://github.com/eballetbo/unzboot.git \
        3ccaa1acbd1e092787fa488d046bdcb3762e51ee && \
    git -C /unzboot checkout --detach FETCH_HEAD && \
    meson setup /unzboot/build /unzboot && meson compile -C /unzboot/build
COPY rootfs/ /rootfs/
RUN chmod +x /rootfs/init /rootfs/usr/local/bin/*
COPY pack.sh /pack.sh
COPY init /boot-init
ENTRYPOINT ["/bin/sh", "/pack.sh"]
