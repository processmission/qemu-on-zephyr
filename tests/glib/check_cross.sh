#!/usr/bin/env bash
# Cross-compile check for the Zephyr GLib compatibility layer.
#
# Compile objects with the SDK toolchain. The object's undefined symbols must
# come from libc/libgcc; host GLib libraries are excluded.
#
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
GLIB_DIR="${ROOT}/src/qemu/ports/zephyr/glib"
BUILD="${ROOT}/build/glib-arm"
SDK="${ZEPHYR_SDK_INSTALL_DIR:?Set ZEPHYR_SDK_INSTALL_DIR to your Zephyr SDK}"
CC="${SDK}/gnu/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-gcc"
NM="${SDK}/gnu/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-nm"

mkdir -p "${BUILD}"

"${CC}" -std=gnu11 -mcpu=cortex-a53 -mabi=lp64 -Wall -Wextra -Werror \
    -I "${GLIB_DIR}" -c "${GLIB_DIR}/glib.c" -o "${BUILD}/glib.o"
"${CC}" -std=gnu11 -mcpu=cortex-a53 -mabi=lp64 -Wall -Wextra \
    -I "${GLIB_DIR}" -c "${HERE}/glib_diff_test.c" \
    -o "${BUILD}/glib_diff_test.o"

"${NM}" -u "${BUILD}/glib.o" > "${BUILD}/undefined.txt"
if grep -E '^.* U (g_|G_)' "${BUILD}/undefined.txt"; then
    echo "FAIL: ARM object has unresolved g_* symbols" >&2
    exit 1
fi
if grep -E '(^|/)lib(glib|gobject|gio|gmodule)' "${BUILD}/undefined.txt"; then
    echo "FAIL: ARM object references a host GLib library" >&2
    exit 1
fi

echo "PASS: aarch64 glib.o and fixture compile; no unresolved g_* symbols"
echo "undefined symbols:"
sed 's/^/  /' "${BUILD}/undefined.txt"
