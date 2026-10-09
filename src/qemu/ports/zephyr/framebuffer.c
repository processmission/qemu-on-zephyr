/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "system/device_tree.h"
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include "framebuffer.h"

static const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static struct display_buffer_descriptor descriptor;
static const void *framebuffer;
static QEMUTimer *refresh_timer;

static void refresh(void *opaque)
{
    int result = display_write(display, 0, 0, &descriptor, framebuffer);

    if (result != 0) {
        error_report("Zephyr framebuffer refresh failed: %d", result);
        return;
    }
    timer_mod(refresh_timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + 100);
}

void qemu_zephyr_framebuffer_init(void *fdt, void *pixels, uint64_t address)
{
    struct display_capabilities caps;
    char node[80];

    if (!device_is_ready(display)) {
        error_report("Zephyr display is unavailable; enable the outer ramfb device");
        exit(1);
    }
    display_get_capabilities(display, &caps);
    descriptor = (struct display_buffer_descriptor) {
        .width = caps.x_resolution,
        .height = caps.y_resolution,
        .pitch = caps.x_resolution,
        .buf_size = (size_t)caps.x_resolution * caps.y_resolution * 4,
    };
    if (caps.current_pixel_format != PIXEL_FORMAT_ARGB_8888 ||
        descriptor.buf_size > QEMU_ZEPHYR_FRAMEBUFFER_SIZE) {
        error_report("Display requires ARGB8888 and at most 4 MiB of pixels");
        exit(1);
    }
    snprintf(node, sizeof(node), "/chosen/framebuffer@%" PRIx64, address);
    qemu_fdt_setprop_cell(fdt, "/chosen", "#address-cells", 2);
    qemu_fdt_setprop_cell(fdt, "/chosen", "#size-cells", 2);
    qemu_fdt_setprop(fdt, "/chosen", "ranges", NULL, 0);
    qemu_fdt_add_subnode(fdt, node);
    qemu_fdt_setprop_string(fdt, node, "compatible", "simple-framebuffer");
    qemu_fdt_setprop_string(fdt, node, "status", "okay");
    qemu_fdt_setprop_cells(fdt, node, "reg", (uint32_t)(address >> 32),
                          (uint32_t)address, 0, QEMU_ZEPHYR_FRAMEBUFFER_SIZE);
    qemu_fdt_setprop_cell(fdt, node, "width", descriptor.width);
    qemu_fdt_setprop_cell(fdt, node, "height", descriptor.height);
    qemu_fdt_setprop_cell(fdt, node, "stride", descriptor.pitch * 4);
    qemu_fdt_setprop_string(fdt, node, "format", "a8r8g8b8");
    framebuffer = pixels;
    refresh_timer = timer_new_ms(QEMU_CLOCK_REALTIME, refresh, NULL);
    timer_mod(refresh_timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + 100);
    printf("QEMU framebuffer: %ux%u ARGB8888 at 0x%" PRIx64 "\n",
           descriptor.width, descriptor.height, address);
}
