FROM python:3.12.13-slim-bookworm@sha256:4766d8b510c428e595d74b9cc5bbb2fae8e26316fffb4adc89908d79aacd58a2 AS runtime

ENV DEBIAN_FRONTEND=noninteractive PIP_NO_CACHE_DIR=1 \
    HF_HUB_DISABLE_TELEMETRY=1 TOKENIZERS_PARALLELISM=false \
    OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates git patch libgomp1 tk8.6 xserver-xorg-core \
        xserver-xorg-video-fbdev jwm xterm xdotool x11-xserver-utils \
        fonts-dejavu-core busybox-static procps && \
    rm -rf /var/lib/apt/lists/*
RUN python -m pip install torch==2.14.0 --index-url https://download.pytorch.org/whl/cpu
RUN python -m pip install transformers==5.17.0 safetensors==0.8.0 numpy==2.5.3 accelerate==1.10.1
RUN git init /opt/nanojev && \
    git -C /opt/nanojev fetch --depth 1 https://github.com/LeanFly/NanoJev.git \
        9a5238fe86ca687ba264aabe0d26d77b3b93f939 && \
    git -C /opt/nanojev checkout --detach FETCH_HEAD && \
    rm -rf /opt/nanojev/.git
COPY cpu-memory.patch /opt/cpu-memory.patch
RUN patch --batch -d /opt/nanojev -p1 < /opt/cpu-memory.patch && \
    python -c 'import tkinter, torch; assert not torch.cuda.is_available(); print(torch.__version__, tkinter.TkVersion)'
COPY download_model.py /opt/download_model.py
RUN python /opt/download_model.py
RUN rm -rf /root/.cache/huggingface /opt/model-source && \
    python -m pip freeze > /opt/python-packages.txt && \
    dpkg-query -W > /opt/debian-packages.txt

FROM alpine:3.23.4@sha256:5b10f432ef3da1b8d4c7eb6c487f2f5a8f096bc91145e68878dd4a5019afde11 AS packer
RUN apk add --no-cache cpio squashfs-tools busybox-static linux-virt kmod zstd \
        build-base meson glib-dev zlib-dev zstd-dev git && \
    git init /unzboot && \
    git -C /unzboot fetch --depth 1 https://github.com/eballetbo/unzboot.git \
        3ccaa1acbd1e092787fa488d046bdcb3762e51ee && \
    git -C /unzboot checkout --detach FETCH_HEAD && \
    meson setup /unzboot/build /unzboot && meson compile -C /unzboot/build
COPY --from=runtime / /rootfs/
COPY rootfs/ /rootfs/
COPY init /boot-init
COPY pack.sh /pack.sh
ENTRYPOINT ["/bin/sh", "/pack.sh"]
