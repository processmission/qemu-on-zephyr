#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Explicit system-package installation; make setup never invokes sudo.
set -euo pipefail
source /etc/os-release
elevate=()
if [[ $EUID != 0 ]]; then
    elevate=(sudo)
fi
case "$ID" in
    ubuntu|debian)
        "${elevate[@]}" apt-get update
        "${elevate[@]}" apt-get install -y --no-install-recommends \
            git make python3 python3-venv python3-dev build-essential \
            wget xz-utils file patch tar ca-certificates pkg-config libglib2.0-dev
        ;;
    arch)
        "${elevate[@]}" pacman -S --needed --noconfirm \
            git make python python-pip base-devel wget xz file patch tar \
            ca-certificates pkgconf glib2
        ;;
    fedora)
        "${elevate[@]}" dnf install -y \
            git make python3 python3-pip python3-devel gcc gcc-c++ \
            wget xz file patch tar which ca-certificates pkgconf-pkg-config glib2-devel
        ;;
    *)
        echo "Unsupported package manager for $ID. See docs/setup.md for required host tools." >&2
        exit 1
        ;;
esac
python3 -c 'import sys; assert sys.version_info >= (3, 12), "Pinned Zephyr requires Python >= 3.12; use Ubuntu 24.04+ or another supported Python"'
