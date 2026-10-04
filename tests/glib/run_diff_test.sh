#!/usr/bin/env bash
# Differential test runner for qemu/ports/zephyr/glib.
#
# The same fixture is compiled once against the host's real GLib and once
# against the Zephyr GLib compatibility layer.  Their stdout must match
# byte-for-byte.  The overflow fixture must abort in both implementations.
#
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
ulimit -c 0

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
GLIB_DIR="${ROOT}/src/qemu/ports/zephyr/glib"
BUILD="${ROOT}/build/glib-host"
FIXTURE="${HERE}/glib_diff_test.c"
CC="${CC:-gcc}"

mkdir -p "${BUILD}"

"${CC}" -std=gnu11 -Wall -Wextra -w -pthread \
    -DGLIB_DIFF_HAVE_PTHREAD -I "${GLIB_DIR}" \
    "${FIXTURE}" "${GLIB_DIR}/glib.c" -o "${BUILD}/diff_ours"
"${CC}" -std=gnu11 -Wall -Wextra -w -pthread \
    -DGLIB_DIFF_HAVE_PTHREAD \
    $(pkg-config --cflags glib-2.0) "${FIXTURE}" \
    $(pkg-config --libs glib-2.0) -o "${BUILD}/diff_real"

"${BUILD}/diff_ours" > "${BUILD}/ours.out"
"${BUILD}/diff_real" > "${BUILD}/real.out"

if diff -u "${BUILD}/real.out" "${BUILD}/ours.out" > "${BUILD}/diff.out"; then
    echo "PASS: host differential output matches ($(wc -l < "${BUILD}/ours.out") lines)"
else
    echo "FAIL: host differential output differs" >&2
    cat "${BUILD}/diff.out" >&2
    exit 1
fi

set +e
"${BUILD}/diff_ours" --overflow > /dev/null 2> "${BUILD}/overflow_ours.err"
OURS_OVERFLOW=$?
"${BUILD}/diff_real" --overflow > /dev/null 2> "${BUILD}/overflow_real.err"
REAL_OVERFLOW=$?
set -e

if [ "${OURS_OVERFLOW}" -eq 0 ] || [ "${REAL_OVERFLOW}" -eq 0 ]; then
    echo "FAIL: overflow case did not abort in both implementations" >&2
    exit 1
fi
echo "PASS: allocation overflow aborts (ours=${OURS_OVERFLOW}, real=${REAL_OVERFLOW})"
