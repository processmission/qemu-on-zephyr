# SPDX-License-Identifier: Apache-2.0
.DEFAULT_GOAL := help
BOOTSTRAP_PYTHON ?= python3
PYTHON ?= $(CURDIR)/.venv/bin/python
JOBS ?= 8
ACCEL ?= zephyr
CPU ?= cortex-a53
HOST_ACCEL ?= tcg
QEMU_ARGS ?=
QEMU_SHELL ?= 0
QEMU_MODE ?= system
QEMU_DESKTOP ?= 0
QEMU_NANOJEV ?= 0
NANOJEV_MAZE_SIZE ?= 8
GUEST_FILES ?=
GUEST_DISK ?=
export JOBS ACCEL CPU QEMU_ARGS QEMU_SHELL QEMU_MODE QEMU_DESKTOP GUEST_FILES GUEST_DISK
export QEMU_NANOJEV
export HOST_ACCEL

.PHONY: help host-deps setup doctor init update prepare assets guest-disk build run check check-firmware check-user probe native-probe test-glib test-payload test-arch test-tools test-counter clean check-env
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
	@echo 'make test-counter  Validate native counter agreement at 24 MHz'
	@echo 'make desktop       Build and run Alpine Linux with Xorg and JWM (requires Docker)'
	@echo 'make check-desktop Verify the Alpine desktop headlessly and save validation artifacts'
	@echo 'make nanojev       Build and run the separate NanoJev CPU desktop image'
	@echo 'make check-nanojev Verify local CPU inference inside the ZHV Linux desktop'
	@echo 'make record-demos  Record system desktop and user-mode GIFs for the README'
	@echo 'make clean         Delete generated builds, retaining SDK, venv and assets'
	@echo 'QEMU_ARGS="-M help", "-accel help" or "-cpu help" lists supported selections'
	@echo 'ACCEL and CPU provide defaults for options omitted from QEMU_ARGS'
	@echo 'HOST_ACCEL=hvf CPU=host uses outer hardware virtualization on supported Apple Silicon'

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

test-counter: | check-env
	HOST_ACCEL=tcg HOST_CPU=cortex-a53 "$(PYTHON)" scripts/check_counter.py

.PHONY: desktop-assets desktop check-desktop record-demos
desktop-assets: | check-env
	"$(PYTHON)" scripts/desktop.py

desktop: desktop-assets
	$(MAKE) run QEMU_DESKTOP=1

check-desktop: desktop-assets
	"$(PYTHON)" scripts/record_demos.py --mode desktop --output "$(CURDIR)/build/desktop-validation"

record-demos: desktop-assets
	"$(PYTHON)" scripts/record_demos.py

.PHONY: nanojev-assets nanojev check-nanojev
nanojev-assets: | check-env
	"$(PYTHON)" scripts/nanojev.py

nanojev: nanojev-assets
	$(MAKE) run QEMU_NANOJEV=1 ACCEL=zephyr

check-nanojev: nanojev-assets
	"$(PYTHON)" scripts/check_nanojev.py --maze-size "$(NANOJEV_MAZE_SIZE)" --cpu "$(CPU)"
