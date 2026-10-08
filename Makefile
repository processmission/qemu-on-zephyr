# SPDX-License-Identifier: Apache-2.0
.DEFAULT_GOAL := help
BOOTSTRAP_PYTHON ?= python3
PYTHON ?= $(CURDIR)/.venv/bin/python
JOBS ?= 8
ACCEL ?= zephyr
CPU ?= cortex-a53
QEMU_ARGS ?=
QEMU_SHELL ?= 0
QEMU_MODE ?= system
GUEST_FILES ?=
GUEST_DISK ?=
export JOBS ACCEL CPU QEMU_ARGS QEMU_SHELL QEMU_MODE GUEST_FILES GUEST_DISK

.PHONY: help host-deps setup doctor init update prepare assets guest-disk build run check check-firmware check-user probe native-probe test-glib test-payload test-arch test-tools clean check-env
help:
	@echo 'First use: make host-deps (if needed), make setup, make run'
	@echo 'make setup         Prepare .venv, west workspace, SDK and verified guest assets'
	@echo 'make doctor        Check host tools, SDK, Python packages and QEMU'
	@echo 'make update        Synchronize the four pinned upstream repositories with west'
	@echo 'make build / run   Build / start the guest; QEMU_ARGS="-M zephyr-virt -accel zephyr -cpu cortex-a53"'
	@echo 'QEMU_SHELL=1      Enter Zephyr shell and wait for a manual QEMU command'
	@echo 'QEMU_MODE=user    Build qemu-aarch64 Linux process emulation with TCG'
	@echo '                   Its default disk contains /images/hello, built with the SDK'
	@echo 'make check         Verify Linux, IRQs, EL0/MMU and host scheduling'
	@echo 'make guest-disk    Create Ext2 from GUEST_FILES or the selected mode defaults'
	@echo 'make check-firmware Verify ELF and raw firmware loading from the Zephyr filesystem'
	@echo 'make check-user    Verify Linux process execution, syscalls and memory handling'
	@echo 'make probe         QOM/PL011 regression'
	@echo 'make native-probe  Native accelerator diagnostic'
	@echo 'make test-payload  Read-only filesystem regression'
	@echo 'make test-glib     Host GLib differential regression'
	@echo 'make test-arch     EL1/EL2, FPU and executor regressions through west twister'
	@echo 'make test-tools    Workspace setup and configuration tests'
	@echo 'make clean         Delete generated builds, retaining SDK, venv and assets'
	@echo 'QEMU_ARGS="-M help", "-accel help" or "-cpu help" lists supported selections'
	@echo 'ACCEL and CPU provide defaults for options omitted from QEMU_ARGS'

host-deps:
	bash scripts/install-host-deps.sh

setup:
	$(BOOTSTRAP_PYTHON) scripts/setup.py

doctor:
	$(BOOTSTRAP_PYTHON) scripts/setup.py --doctor

check-env:
	@test -x "$(PYTHON)" -a -x "$(CURDIR)/.venv/bin/west" || { echo 'Run make setup first.' >&2; exit 1; }

init prepare assets guest-disk build run check check-firmware check-user probe native-probe test-payload test-arch clean: | check-env
	"$(PYTHON)" scripts/project.py $@

update: init

test-glib:
	bash tests/glib/run_diff_test.sh

test-tools: | check-env
	"$(PYTHON)" -m unittest discover -s tests/tools -v
