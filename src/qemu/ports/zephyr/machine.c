/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Fixed ARM virt profile: one A53, RAM, PL011 and software GICv3. */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/accel.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qobject/qlist.h"
#include "hw/core/boards.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/char/pl011.h"
#include "hw/intc/arm_gicv3_common.h"
#include "hw/arm/boot.h"
#include "target/arm/cpu.h"
#include "system/address-spaces.h"
#include "system/device_tree.h"
#include "system/zephyr.h"

#define TYPE_ZEPHYR_VIRT MACHINE_TYPE_NAME("zephyr-virt")
#define GUEST_RAM_IPA 0x40000000ULL
#define GIC_DIST_IPA 0x08000000ULL
#define GIC_REDIST_IPA 0x080a0000ULL
#define UART_IPA 0x09000000ULL
#define EXTERNAL_IRQS 256

typedef struct ZephyrVirtState {
    MachineState parent;
    MemoryRegion ram;
    struct arm_boot_info boot_info;
    int fdt_size;
} ZephyrVirtState;

Chardev *qemu_zephyr_console_create(Error **errp);

static void *get_dtb(const struct arm_boot_info *info, int *size)
{
    ZephyrVirtState *s = container_of(info, ZephyrVirtState, boot_info);

    *size = s->fdt_size;
    return g_memdup2(s->parent.fdt, s->fdt_size);
}

static void create_fdt(ZephyrVirtState *s)
{
    void *fdt = create_device_tree(&s->fdt_size);
    const char uart_compat[] = "arm,pl011\0arm,primecell";
    const char clock_names[] = "uartclk\0apb_pclk";
    const char psci_compat[] = "arm,psci-1.0\0arm,psci-0.2";

    s->parent.fdt = fdt;
    qemu_fdt_setprop_string(fdt, "/", "compatible", "linux,dummy-virt");
    qemu_fdt_setprop_string(fdt, "/", "model", "QEMU Zephyr ARM virt profile");
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 2);
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 2);
    qemu_fdt_setprop_cell(fdt, "/", "interrupt-parent", 1);
    qemu_fdt_add_subnode(fdt, "/chosen");
    qemu_fdt_setprop_string(fdt, "/chosen", "stdout-path", "/pl011@9000000");
    qemu_fdt_add_subnode(fdt, "/cpus");
    qemu_fdt_setprop_cell(fdt, "/cpus", "#address-cells", 1);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#size-cells", 0);
    qemu_fdt_add_subnode(fdt, "/cpus/cpu@0");
    qemu_fdt_setprop_string(fdt, "/cpus/cpu@0", "device_type", "cpu");
    qemu_fdt_setprop_string(fdt, "/cpus/cpu@0", "compatible", "arm,cortex-a53");
    qemu_fdt_setprop_cell(fdt, "/cpus/cpu@0", "reg", 0);
    qemu_fdt_add_subnode(fdt, "/psci");
    qemu_fdt_setprop(fdt, "/psci", "compatible", psci_compat, sizeof(psci_compat));
    qemu_fdt_setprop_string(fdt, "/psci", "method", "hvc");
    qemu_fdt_add_subnode(fdt, "/timer");
    qemu_fdt_setprop_string(fdt, "/timer", "compatible", "arm,armv8-timer");
    qemu_fdt_setprop_cells(fdt, "/timer", "interrupts",
                          1, 13, 4, 1, 14, 4, 1, 11, 4, 1, 10, 4);
    qemu_fdt_setprop(fdt, "/timer", "always-on", NULL, 0);
    qemu_fdt_add_subnode(fdt, "/intc@8000000");
    qemu_fdt_setprop_string(fdt, "/intc@8000000", "compatible", "arm,gic-v3");
    qemu_fdt_setprop_cell(fdt, "/intc@8000000", "#interrupt-cells", 3);
    qemu_fdt_setprop_cell(fdt, "/intc@8000000", "phandle", 1);
    qemu_fdt_setprop(fdt, "/intc@8000000", "interrupt-controller", NULL, 0);
    qemu_fdt_setprop_cells(fdt, "/intc@8000000", "reg",
                          0, GIC_DIST_IPA, 0, 0x10000,
                          0, GIC_REDIST_IPA, 0, 0x20000);
    qemu_fdt_setprop_cell(fdt, "/intc@8000000", "#redistributor-regions", 1);
    qemu_fdt_add_subnode(fdt, "/apb-pclk");
    qemu_fdt_setprop_string(fdt, "/apb-pclk", "compatible", "fixed-clock");
    qemu_fdt_setprop_cell(fdt, "/apb-pclk", "#clock-cells", 0);
    qemu_fdt_setprop_cell(fdt, "/apb-pclk", "clock-frequency", 24000000);
    qemu_fdt_setprop_cell(fdt, "/apb-pclk", "phandle", 2);
    qemu_fdt_add_subnode(fdt, "/pl011@9000000");
    qemu_fdt_setprop(fdt, "/pl011@9000000", "compatible", uart_compat, sizeof(uart_compat));
    qemu_fdt_setprop_cells(fdt, "/pl011@9000000", "reg", 0, UART_IPA, 0, 0x1000);
    qemu_fdt_setprop_cells(fdt, "/pl011@9000000", "interrupts", 0, 1, 4);
    qemu_fdt_setprop_cells(fdt, "/pl011@9000000", "clocks", 2, 2);
    qemu_fdt_setprop(fdt, "/pl011@9000000", "clock-names", clock_names, sizeof(clock_names));
}

static void zephyr_virt_init(MachineState *machine)
{
    ZephyrVirtState *s = (ZephyrVirtState *)machine;
    ARMCPU *cpu;
    DeviceState *gic, *uart;
    QList *redist_count;
    void *ram;
    uint64_t ipa, size;

    if (zephyr_get_guest_ram(machine->accelerator, &ram, &ipa, &size, &error_fatal) != 0) {
        abort();
    }
    assert(ipa == GUEST_RAM_IPA && size == machine->ram_size && machine->smp.cpus == 1);
    memory_region_init_ram_ptr(&s->ram, OBJECT(machine), "zephyr.guest-ram", size, ram);
    machine->ram = &s->ram;
    memory_region_add_subregion(get_system_memory(), ipa, &s->ram);

    cpu = ARM_CPU(object_new(ARM_CPU_TYPE_NAME("cortex-a53")));
    object_property_add_child(OBJECT(machine), "cpu0", OBJECT(cpu));
    qdev_realize(DEVICE(cpu), NULL, &error_fatal);

    gic = qdev_new("arm-gicv3");
    qdev_prop_set_uint32(gic, "num-cpu", 1);
    qdev_prop_set_uint32(gic, "num-irq", EXTERNAL_IRQS + 32);
    qdev_prop_set_uint32(gic, "revision", 3);
    redist_count = qlist_new();
    qlist_append_int(redist_count, 1);
    qdev_prop_set_array(gic, "redist-region-count", redist_count);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(gic), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(gic), 0, GIC_DIST_IPA);
    sysbus_mmio_map(SYS_BUS_DEVICE(gic), 1, GIC_REDIST_IPA);
    for (int i = 0; i < 4; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(gic), i, qdev_get_gpio_in(DEVICE(cpu), i));
    }
    qdev_connect_gpio_out(DEVICE(cpu), GTIMER_PHYS, qdev_get_gpio_in(gic, EXTERNAL_IRQS + 30));
    qdev_connect_gpio_out(DEVICE(cpu), GTIMER_VIRT, qdev_get_gpio_in(gic, EXTERNAL_IRQS + 27));

    uart = qdev_new(TYPE_PL011);
    qdev_prop_set_chr(uart, "chardev", qemu_zephyr_console_create(&error_fatal));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(uart), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(uart), 0, UART_IPA);
    sysbus_connect_irq(SYS_BUS_DEVICE(uart), 0, qdev_get_gpio_in(gic, 1));

    printf("QEMU machine=%s cpu=%s accel=%s (%s) RAM=%" PRIu64 "MiB\n",
           object_get_typename(OBJECT(machine)), object_get_typename(OBJECT(cpu)),
           object_get_typename(OBJECT(machine->accelerator)), current_accel_name(), size / MiB);
#ifdef CONFIG_QEMU_NATIVE_PROBE_ONLY
    return;
#endif
    create_fdt(s);
    s->boot_info.ram_size = size;
    s->boot_info.loader_start = ipa;
    s->boot_info.board_id = -1;
    s->boot_info.get_dtb = get_dtb;
    s->boot_info.psci_conduit = QEMU_PSCI_CONDUIT_HVC;
    arm_load_kernel(cpu, machine, &s->boot_info);
}

static void zephyr_virt_class_init(ObjectClass *object_class, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(object_class);

    mc->desc = "Zephyr ARM virt profile (one A53, PL011, GICv3)";
    mc->init = zephyr_virt_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a53");
    mc->default_ram_size = 256 * MiB;
    mc->min_cpus = 1;
    mc->max_cpus = 1;
    mc->default_cpus = 1;
    mc->minimum_page_bits = 12;
    mc->no_floppy = true;
    mc->no_cdrom = true;
    mc->no_parallel = true;
}

static const TypeInfo zephyr_virt_type = {
    .name = TYPE_ZEPHYR_VIRT,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(ZephyrVirtState),
    .class_init = zephyr_virt_class_init,
};

static void zephyr_virt_register_types(void)
{
    type_register_static(&zephyr_virt_type);
}

type_init(zephyr_virt_register_types)
