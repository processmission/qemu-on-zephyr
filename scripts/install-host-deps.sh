#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Explicit system-package installation; make setup never invokes sudo.
set -euo pipefail
case "$(uname -s)" in
    Darwin)
        if [[ $(uname -m) != arm64 ]]; then
            echo "Zephyr SDK 1.0.1 requires Apple Silicon on macOS." >&2
            exit 1
        fi
        if ! xcode-select -p >/dev/null 2>&1; then
            echo "Install Xcode Command Line Tools with xcode-select --install, then rerun this script." >&2
            exit 1
        fi
        if ! command -v brew >/dev/null 2>&1; then
            echo "Install Homebrew from https://brew.sh and add it to PATH, then rerun this script." >&2
            exit 1
        fi
        HOMEBREW_NO_INSTALL_UPGRADE=1 brew install \
            git make python wget xz pkgconf glib dtc gperf libmagic e2fsprogs
        ;;
    Linux)
        if [[ ! -r /etc/os-release ]]; then
            echo "Cannot identify this Linux distribution. See docs/setup.md for required host tools." >&2
            exit 1
        fi
        # shellcheck source=/dev/null
        source /etc/os-release
        elevate=()
        if [[ $EUID != 0 ]]; then
            elevate=(sudo)
        fi
        case "${ID:-unknown}" in
            ubuntu|debian)
                "${elevate[@]}" apt-get update
                "${elevate[@]}" apt-get install -y --no-install-recommends \
                    git make python3 python3-venv python3-dev build-essential \
                    wget xz-utils file patch tar ca-certificates pkg-config libglib2.0-dev e2fsprogs
                ;;
            arch)
                "${elevate[@]}" pacman -S --needed --noconfirm \
                    git make python python-pip base-devel wget xz file patch tar \
                    ca-certificates pkgconf glib2 e2fsprogs
                ;;
            fedora)
                "${elevate[@]}" dnf install -y \
                    git make python3 python3-pip python3-devel gcc gcc-c++ \
                    wget xz file patch tar which ca-certificates pkgconf-pkg-config glib2-devel e2fsprogs
                ;;
            *)
                echo "Unsupported package manager for ${ID:-unknown}. See docs/setup.md for required host tools." >&2
                exit 1
                ;;
        esac
        ;;
    *)
        echo "Supported hosts: Linux x86_64/AArch64 and macOS Apple Silicon. See docs/setup.md." >&2
        exit 1
        ;;
esac
"${BOOTSTRAP_PYTHON:-python3}" -c 'import sys; assert sys.version_info >= (3, 12), "Pinned Zephyr requires Python >= 3.12; put a supported python3 on PATH or set BOOTSTRAP_PYTHON"'
