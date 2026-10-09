/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/option.h"
#include "qemu/units.h"
#include "qemu/zephyr.h"
#include "qapi/error.h"
#include "system/zephyr.h"

#ifdef CONFIG_QEMU_TCG
#define BUILT_ACCEL "tcg"
#else
#define BUILT_ACCEL "zephyr"
#endif

static QemuOptsList machine_options = {
    .name = "machine",
    .implied_opt_name = "type",
    .head = QTAILQ_HEAD_INITIALIZER(machine_options.head),
    .desc = {
        { .name = "type", .type = QEMU_OPT_STRING },
        { .name = "accel", .type = QEMU_OPT_STRING },
        { }
    },
};

static QemuOptsList accel_options = {
    .name = "accel",
    .implied_opt_name = "accel",
    .head = QTAILQ_HEAD_INITIALIZER(accel_options.head),
    .desc = {
        { .name = "accel", .type = QEMU_OPT_STRING },
        { }
    },
};

static int copy_option(char *destination, size_t capacity, const char *value,
                       char *error, size_t error_size)
{
    if (value == NULL || *value == '\0' || strlen(value) >= capacity) {
        snprintf(error, error_size, "Empty or oversized option value");
        return -EINVAL;
    }
    pstrcpy(destination, capacity, value);
    return 0;
}

static int check_image(const char *path, char *error, size_t error_size)
{
    struct stat st;

    if (path[0] != '/') {
        snprintf(error, error_size, "Use an absolute Zephyr filesystem path: %s", path);
        return -EINVAL;
    }
    if (stat(path, &st) != 0) {
        snprintf(error, error_size, "Cannot read %s: %s", path, strerror(errno));
        return -errno;
    }
    if (!S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > 128 * MiB) {
        snprintf(error, error_size, "Expected a nonempty image of at most 128 MiB: %s", path);
        return -EINVAL;
    }
    return 0;
}

static void print_help(void)
{
    printf("qemu-system-aarch64 -M zephyr-virt -accel %s -cpu MODEL\n"
           "  -kernel /images/Image [-initrd /images/initramfs.cpio.gz]\n"
           "  [-append \"console=ttyAMA0 rdinit=/bin/sh\"]\n"
           "  -bios /images/firmware.bin  Raw EL1 firmware at 0x40000000\n"
           "  -kernel also accepts AArch64 ELF firmware through QEMU's loader\n"
           "  -m %uM -smp 1 -nographic  Configured guest resources and serial console\n"
           "  -M help | -accel help | -cpu help\n"
           "  -status  Show guest state and scheduling counters\n"
           "  Ctrl-] stops the guest and returns to the Zephyr shell\n"
           "  Use kernel reboot cold before starting another guest\n",
           BUILT_ACCEL, ZEPHYR_GUEST_RAM_MIB);
}

int qemu_zephyr_parse_options(size_t argc, char **argv,
                             struct qemu_zephyr_options *options,
                             char *error, size_t error_size)
{
    QemuOpts *machine = NULL, *accel = NULL;
    Error *qerror = NULL;
    const char *machine_accel = NULL, *explicit_accel = NULL;
    const char *seen[16];
    size_t seen_count = 0;
    bool append_set = false;
    int result = -EINVAL;

    memset(options, 0, sizeof(*options));
    pstrcpy(options->machine, sizeof(options->machine), CONFIG_QEMU_MACHINE_MODEL);
    pstrcpy(options->accelerator, sizeof(options->accelerator), BUILT_ACCEL);
    pstrcpy(options->cpu, sizeof(options->cpu), CONFIG_QEMU_CPU_MODEL);
    pstrcpy(options->append, sizeof(options->append),
            "console=ttyAMA0 earlycon=pl011,0x09000000 rdinit=/bin/sh nokaslr panic=-1");
    if (argc == 1) {
        print_help();
        return 1;
    }
    for (size_t i = 1; i < argc; i++) {
        const char *key = argv[i];
        const char *value;
        char *destination = NULL;
        size_t capacity = 0;

        if (!strcmp(key, "-help") || !strcmp(key, "--help") || !strcmp(key, "-h")) {
            print_help();
            result = 1;
            goto out;
        }
        if (!strcmp(key, "-M")) {
            key = "-machine";
        }
        for (size_t j = 0; j < seen_count; j++) {
            if (!strcmp(key, seen[j])) {
                snprintf(error, error_size, "Repeated option: %s", key);
                goto out;
            }
        }
        if (seen_count == ARRAY_SIZE(seen)) {
            snprintf(error, error_size, "Too many options");
            goto out;
        }
        seen[seen_count++] = key;
        if (!strcmp(key, "-nographic")) {
            continue;
        }
        if (++i == argc || (!argv[i][0] && strcmp(key, "-append"))) {
            snprintf(error, error_size, "Missing value for %s", key);
            goto out;
        }
        value = argv[i];
        if (!strcmp(value, "help")) {
            if (!strcmp(key, "-machine")) {
                printf("Supported machines: zephyr-virt\n");
            } else if (!strcmp(key, "-accel")) {
                printf("Compiled accelerator: %s\n", BUILT_ACCEL);
            } else if (!strcmp(key, "-cpu")) {
                printf("CPU models: %s\n", !strcmp(BUILT_ACCEL, "tcg") ?
                       "cortex-a53 cortex-a57 cortex-a72" : CONFIG_QEMU_CPU_MODEL);
            } else {
                snprintf(error, error_size, "Unsupported help option: %s", key);
                goto out;
            }
            result = 1;
            goto out;
        }
        if (!strcmp(key, "-machine") || !strcmp(key, "-accel")) {
            QemuOpts *parsed = qemu_opts_parse(!strcmp(key, "-machine") ?
                                               &machine_options : &accel_options,
                                               value, true, &qerror);

            if (parsed == NULL) {
                snprintf(error, error_size, "%s", error_get_pretty(qerror));
                error_free(qerror);
                goto out;
            }
            if (!strcmp(key, "-machine")) {
                machine = parsed;
                machine_accel = qemu_opt_get(parsed, "accel");
                value = qemu_opt_get(parsed, "type");
                if (value != NULL && copy_option(options->machine, sizeof(options->machine),
                                                 value, error, error_size)) {
                    goto out;
                }
            } else {
                accel = parsed;
                explicit_accel = qemu_opt_get(parsed, "accel");
                if (explicit_accel == NULL) {
                    snprintf(error, error_size, "Missing accelerator name");
                    goto out;
                }
            }
            continue;
        }
        if (!strcmp(key, "-m")) {
            uint64_t bytes;

            if (qemu_strtosz_MiB(value, NULL, &bytes) ||
                bytes != ZEPHYR_GUEST_RAM_MIB * MiB) {
                snprintf(error, error_size, "This machine requires -m %uM",
                         ZEPHYR_GUEST_RAM_MIB);
                goto out;
            }
            continue;
        }
        if (!strcmp(key, "-smp")) {
            if (strcmp(value, "1")) {
                snprintf(error, error_size, "This machine requires -smp 1");
                goto out;
            }
            continue;
        }
        if (!strcmp(key, "-cpu")) {
            destination = options->cpu;
            capacity = sizeof(options->cpu);
        } else if (!strcmp(key, "-kernel")) {
            destination = options->kernel;
            capacity = sizeof(options->kernel);
        } else if (!strcmp(key, "-initrd")) {
            destination = options->initrd;
            capacity = sizeof(options->initrd);
        } else if (!strcmp(key, "-bios")) {
            destination = options->firmware;
            capacity = sizeof(options->firmware);
        } else if (!strcmp(key, "-append")) {
            append_set = true;
            if (!value[0]) {
                options->append[0] = '\0';
                continue;
            }
            destination = options->append;
            capacity = sizeof(options->append);
        } else {
            snprintf(error, error_size, "Unsupported option: %s", key);
            goto out;
        }
        if (copy_option(destination, capacity, value, error, error_size)) {
            goto out;
        }
    }
    if (machine_accel && explicit_accel && strcmp(machine_accel, explicit_accel)) {
        snprintf(error, error_size, "Conflicting accelerator selections");
        goto out;
    }
    if ((machine_accel && strcmp(machine_accel, BUILT_ACCEL)) ||
        (explicit_accel && strcmp(explicit_accel, BUILT_ACCEL))) {
        snprintf(error, error_size, "This Zephyr image provides the %s accelerator", BUILT_ACCEL);
        goto out;
    }
    if (strcmp(options->machine, "zephyr-virt")) {
        snprintf(error, error_size, "Supported machine: zephyr-virt");
        goto out;
    }
    if ((strcmp(options->cpu, "cortex-a53") && strcmp(options->cpu, "cortex-a57") &&
         strcmp(options->cpu, "cortex-a72") &&
         (strcmp(BUILT_ACCEL, "zephyr") || strcmp(options->cpu, "host"))) ||
        (!strcmp(BUILT_ACCEL, "zephyr") && strcmp(options->cpu, CONFIG_QEMU_CPU_MODEL))) {
        snprintf(error, error_size, "Unsupported CPU for this host: %s", options->cpu);
        goto out;
    }
    if (!!options->kernel[0] == !!options->firmware[0]) {
        snprintf(error, error_size, "Specify exactly one of -kernel or -bios");
        goto out;
    }
    if (options->firmware[0] && (options->initrd[0] || append_set)) {
        snprintf(error, error_size, "-initrd and -append require -kernel");
        goto out;
    }
    if (check_image(options->kernel[0] ? options->kernel : options->firmware,
                     error, error_size) ||
        (options->initrd[0] && check_image(options->initrd, error, error_size))) {
        goto out;
    }
    result = 0;
out:
    qemu_opts_del(machine);
    qemu_opts_del(accel);
    return result;
}
