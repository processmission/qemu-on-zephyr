#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
if [[ $# != 0 ]]; then
    echo 'Unexpected arguments' >&2
    exit 2
fi
exec "${PYTHON:-python3}" "${project_root}/scripts/project.py" probe
