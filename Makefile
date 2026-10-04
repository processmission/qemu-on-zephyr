# SPDX-License-Identifier: Apache-2.0
.DEFAULT_GOAL := help
PYTHON ?= python3
JOBS ?= 8
export JOBS

.PHONY: help init prepare assets build run check probe native-probe test-glib test-payload test-arch clean
help:
	@echo 'make init          Initialize pinned upstream submodules (no recursive QEMU dependencies)'
	@echo 'make prepare       Generate patched source trees under build/sources'
	@echo 'make assets        Download and verify the Linux guest assets'
	@echo 'make build         Build Zephyr with the QEMU module'
	@echo 'make run           Build and enter the guest Linux serial shell (Ctrl-a x exits)'
	@echo 'make check         Build and verify Linux boot, IRQs, EL0/MMU and host scheduling'
	@echo 'make probe         Run the standalone QOM/PL011 regression'
	@echo 'make native-probe  Run the native accelerator diagnostic'
	@echo 'make test-glib     Compare the GLib subset against host GLib'
	@echo 'make test-payload  Run the read-only filesystem regression'
	@echo 'make test-arch     Run EL1/EL2, FPU and native executor tests (requires Twister dependencies)'
	@echo 'make clean         Remove generated builds; retain downloaded guest assets'

init prepare assets build run probe native-probe clean:
	$(PYTHON) scripts/project.py $@

check: build
	$(PYTHON) apps/qemu_linux/check.py --no-build

test-glib:
	bash tests/glib/run_diff_test.sh

test-payload:
	$(PYTHON) scripts/project.py test-payload

test-arch: prepare
	ZEPHYR_BASE="$(CURDIR)/build/sources/zephyr" $(PYTHON) build/sources/zephyr/scripts/twister \
		-p qemu_cortex_a53 -T build/sources/zephyr/tests/arch/arm64/arm64_el2 \
		-T build/sources/zephyr/tests/arch/arm64/fpu_sharing \
		-T build/sources/zephyr/tests/subsys/virtualization/zhv \
		--outdir build/twister --inline-logs -j $(JOBS)
