/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/target-info-qom.h"
#include "qemu/zephyr.h"
#include "qapi/error.h"
#include "qom/object.h"
#include "hw/core/sysbus.h"
#include "hw/char/pl011.h"
#include "system/memory.h"

int qemu_zephyr_pl011_probe(void)
{
    Object *machine;
    DeviceState *dev;
    SysBusDevice *sbd;
    MemoryRegion *mr;
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
    uint64_t value;
    MemTxResult result;

    qemu_zephyr_os_init();
    module_call_init(MODULE_INIT_QOM);
    module_call_init(MODULE_INIT_TARGET_INFO);
    target_info_qom_set_target();

    /* Supply the normal QOM parent for an initially unattached device. */
    machine = object_new(TYPE_CONTAINER);
    object_property_add_child(object_get_root(), "machine", machine);
    object_unref(machine);
    object_property_add_new_container(machine, "unattached");

    dev = qdev_new(TYPE_PL011);
    sbd = SYS_BUS_DEVICE(dev);
    if (!sysbus_realize(sbd, &error_fatal)) {
        return -EIO;
    }
    device_cold_reset(dev);
    mr = sysbus_mmio_get_region(sbd, 0);
    printf("pl011 type=%s mmio_size=0x%" PRIx64 "\n",
           object_get_typename(OBJECT(dev)), memory_region_size(mr));

    result = memory_region_dispatch_read(mr, 0x30, &value, MO_32, attrs);
    printf("read CR=0x%" PRIx64 " result=%u\n", value, result);
    if (result != MEMTX_OK || value != 0x300) {
        return -EIO;
    }
    result = memory_region_dispatch_write(mr, 0x24, 0x1234, MO_32, attrs);
    if (result != MEMTX_OK) {
        return -EIO;
    }
    result = memory_region_dispatch_read(mr, 0x24, &value, MO_32, attrs);
    printf("read IBRD=0x%" PRIx64 " result=%u\n", value, result);
    if (result != MEMTX_OK || value != 0x1234) {
        return -EIO;
    }
    result = memory_region_dispatch_write(mr, 0x38, 0x10, MO_32, attrs);
    if (result != MEMTX_OK) {
        return -EIO;
    }
    result = memory_region_dispatch_read(mr, 0x40, &value, MO_32, attrs);
    printf("read MIS=0x%" PRIx64 " result=%u\n", value, result);
    if (result != MEMTX_OK || value != 0) {
        return -EIO;
    }

    object_unref(OBJECT(dev));
    qemu_zephyr_quiesce();
    printf("PROBE_OK\n");
    return 0;
}
