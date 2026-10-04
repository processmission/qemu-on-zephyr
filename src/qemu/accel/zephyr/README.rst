..
   Copyright (c) 2026 Chao Liu
   SPDX-License-Identifier: GPL-2.0-or-later

Zephyr native accelerator
=========================

The ``zephyr`` accelerator uses Zephyr's non-VHE AArch64 ``zhv`` executor.
It registers ``zephyr-accel``, ``zephyr-accel-ops`` and
``zephyr-accel-arm-cpu``. Guest instructions execute at EL1/EL0; devices remain
QEMU QOM/qdev/MemoryRegion objects. No TCG execution or KVM device is used.

The initial machine has one Cortex-A53 vCPU, no guest EL2/EL3 or PMU, and
AArch64-only EL0/EL1. SVE, PAC and later ISA features are absent from that CPU
model. Debug CPRegs retain QEMU's software state; hardware breakpoint execution
and an inner GDB server are not implemented by this accelerator. Migration,
multiple VMs/vCPUs and nested virtualization are outside this initial profile.

Machine integration
-------------------

Initialize QEMU on one real POSIX worker, then create a MachineState with
``smp.cpus = smp.max_cpus = 1`` and its RAM size. Select the accelerator through
``accel_find("zephyr")`` and ``accel_init_machine()``. Before CPU realization,
initialize its AccelOps and AccelCPU interfaces using the normal machine setup.
The ARM instance hook invokes ``accel_cpu_instance_init()`` after CPU properties
are installed; the accelerator chooses the native clock frequency and HVC PSCI.

``zephyr_get_guest_ram()`` returns borrowed RAM owned by the native VM. Back an
actual RAM MemoryRegion with this pointer, and preserve its IPA and size. The
QEMU ARM loader writes Image, initrd, boot stub and DTB through its usual RAM
path. Native state installation invalidates the executor's cache-publication
flag, so first entry and reset publish loaded RAM before guest execution.

``create_vcpu_thread`` binds the existing POSIX worker. CPUState owns its real
QemuThread/condition/work-queue objects; the accelerator initializes the native
vCPU, thread identity, current_cpu, random seed and creation notification.
Other Zephyr threads/IRQs enqueue input and call ``zephyr_cpu_kick``. They do not
call QEMU device handlers or enter BQL/RCU model operations.

After the normal ROM reset and ``vm_start()``, the worker runs a loop with BQL
held and no RCU reader spanning a blocking operation:

* Process QEMU runstate requests and queued CPU work.
* Drain console input and run QEMU timers.
* Retire deferred RCU callbacks at the owner's quiescent point.
* Call ``zephyr_cpu_exec()`` to handle one native exit.
* On ``EXCP_HLT``, process newly arrived events and call ``zephyr_cpu_wait()``.

The executor releases BQL around native execution/wait and reacquires it before
touching model state. MMIO uses QEMU address-space transactions under an RCU
read section. A negative return terminates the current run with PC/ESR/FAR
diagnostics; unsupported CPRegs are not silently replaced with zero.

Interrupt and clock state
-------------------------

Each native return synchronizes ARMCPU state, then updates the virtual timer's
QEMU GPIO level before any ICC_IAR/EOIR/DIR emulation. ICC accesses use the
software GICv3's registered CPReg access/read/write handlers. The next entry
copies QEMU HARD/FIQ levels to native VI/VF. The inner model's HCR and CNTVOFF
remain zero; they are distinct from the physical host's execution controls.

CNTP/CNTPCT are trapped and use QEMU's existing generic timer handlers. CNTV and
CNTVCT execute natively. ``zephyr_clock_get_ns`` derives QEMU virtual time from
the native frequency and counter offset, with bounded intermediate products.
The two guest counter views therefore share their origin when modeled CNTVOFF
is zero. WFI wait combines the saved native CNTV deadline, QEMU virtual timers
and an optional caller deadline. A kick is latched across the check/block race.

Validation
==========

Run ``make check`` from the module repository root for Linux boot, serial
input, timer interrupt growth, EL0/MMU execution, host scheduling during a
busy guest and host survival after guest poweroff. The console transcript is
written to ``build/linux-validation.log``.

``make native-probe`` runs ``zephyr_accel_probe()`` with the same native CPU
and device profile. It checks the real PL011 mapping, guest stores, native
MMIO exits, WFI and the trapped CNTPCT/direct CNTVCT clock relationship.
It modifies the beginning of guest RAM and a scratch pair at RAM+0x1000;
the Linux path subsequently restores the boot stub with a normal ROM reset.

Executor microguest tests are under
``src/zephyr/tests/subsys/virtualization/zhv``. They exercise nonidentity
Stage-2 RAM, timer rearming, FP/TLS separation and host preemption. The module
repository's CONTRIBUTING.md documents how to run them with Twister.

All these tests use an outer QEMU ARM virt platform. They do not establish
physical-board cache coherency or hardware errata behavior.
