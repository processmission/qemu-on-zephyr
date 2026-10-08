/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu.h"
#include "user-internals.h"
#include "user/cpu_loop.h"
#include "loader.h"
#include "qemu/zephyr.h"
#include "qemu/module.h"
#include "qemu/accel.h"
#include "accel/accel-ops.h"
#include "qemu/cutils.h"
#include "qemu/madvise.h"
#include "qemu/main-loop.h"
#include "qemu/rcu.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "exec/page-vary.h"
#include "tcg/startup.h"
#include "exec/replay-core.h"
#include "crypto/random.h"
#include "disas/disas.h"
#include "console.h"
#include "user.h"
#include <zephyr/random/random.h>

#undef IS_ENABLED
#define IS_ENABLED(config_macro) Z_IS_ENABLED1(config_macro)
#undef IS_EMPTY
#define IS_EMPTY(...) Z_IS_EMPTY_(__VA_ARGS__)

__thread CPUState *thread_cpu;
unsigned long guest_stack_size = 1024 * 1024;
char *exec_path;
char real_exec_path[PATH_MAX];
abi_ulong default_sigreturn, default_rt_sigreturn;
abi_ulong vdso_sigreturn_region_start, vdso_sigreturn_region_end;
bool user_exited;
int user_exit_status;
bool user_trace;
bool user_stop_requested;
static bool initialized;
static bool has_run;
static CPUState *user_cpu;
static TaskState task;
static struct image_info image_info;
static struct linux_binprm binary;
static uint8_t code_buffer[CONFIG_QEMU_TCG_CACHE_SIZE_MIB * 1024 * 1024]
    __attribute__((aligned(CONFIG_MMU_PAGE_SIZE)));

void *qemu_zephyr_code_buffer(size_t size, void **rx)
{
    assert(size <= sizeof(code_buffer));
    k_mem_map_phys_bare((uint8_t **)rx, k_mem_phys_addr(code_buffer),
                       size, K_MEM_CACHE_WB | K_MEM_PERM_EXEC);
    assert(*rx != NULL);
    return code_buffer;
}

int qcrypto_random_bytes(void *buffer, size_t length, Error **errp)
{
    int result = sys_csrand_get(buffer, length);

    if (result != 0) {
        error_setg(errp, "Zephyr entropy request failed: %d", result);
    }
    return result;
}

int replay_read_random(void *buffer, size_t length)
{
    g_assert_not_reached();
}

void replay_save_random(int result, void *buffer, size_t length)
{
    g_assert_not_reached();
}

int qemu_madvise(void *address, size_t length, int advice)
{
    errno = ENOSYS;
    return -1;
}

void replay_finish(void)
{
    assert(replay_mode == REPLAY_MODE_NONE);
}

bool qemu_log_enabled(void)
{
    return qemu_loglevel != 0;
}

static void kick_user(void)
{
    CPUState *cpu = qatomic_read(&user_cpu);

    if (cpu != NULL) {
        qatomic_store_release(&cpu->exit_request, true);
        qatomic_set(&cpu->neg.icount_decr.u16.high, -1);
    }
}

static void user_timeslice(struct k_timer *timer)
{
    kick_user();
}

K_TIMER_DEFINE(user_slice, user_timeslice, NULL);

void qemu_zephyr_request_stop(void)
{
    qatomic_set(&user_stop_requested, true);
    kick_user();
}

void qemu_zephyr_console_rx_notify(void)
{
    kick_user();
}

bool qemu_zephyr_started(void)
{
    return qatomic_read(&has_run);
}

int qemu_zephyr_mount_payload(void)
{
    return 0;
}

void cpu_loop_exit_sigsegv(CPUState *cpu, vaddr addr, MMUAccessType access,
                          bool maperr, uintptr_t ra)
{
    fprintf(stderr, "qemu-aarch64: SIGSEGV at 0x%" PRIx64 " (%s)\n", (uint64_t)addr,
            maperr ? "unmapped" : "permission");
    user_exit_status = 128 + TARGET_SIGSEGV;
    user_exited = true;
    cpu->exception_index = EXCP_INTERRUPT;
    cpu_loop_exit_restore(cpu, ra);
}

void cpu_loop_exit_sigbus(CPUState *cpu, vaddr addr, MMUAccessType access, uintptr_t ra)
{
    fprintf(stderr, "qemu-aarch64: SIGBUS at 0x%" PRIx64 "\n", (uint64_t)addr);
    user_exit_status = 128 + TARGET_SIGBUS;
    user_exited = true;
    cpu->exception_index = EXCP_INTERRUPT;
    cpu_loop_exit_restore(cpu, ra);
}

static void initialize_user(void)
{
    AccelState *accel;
    AccelClass *klass;

    qemu_zephyr_os_init();
    module_call_init(MODULE_INIT_QOM);
    qemu_init_cpu_list();
    accel = current_accel();
    klass = ACCEL_GET_CLASS(accel);
    accel_init_interfaces(klass);
    object_property_set_int(OBJECT(accel), "tb-size", CONFIG_QEMU_TCG_CACHE_SIZE_MIB,
                            &error_abort);
    klass->init_machine(accel, NULL);
    set_preferred_target_page_bits(12);
    finalize_target_page_bits();
    user_cpu = cpu_create(CONFIG_QEMU_CPU_MODEL "-arm-cpu");
    thread_cpu = current_cpu = user_cpu;
    user_cpu->opaque = &task;
    qemu_thread_get_self(user_cpu->thread);
    user_cpu->thread_id = qemu_get_thread_id();
    task.ts_tid = user_cpu->thread_id;
    task.info = &image_info;
    task.bprm = &binary;
    qemu_zephyr_user_memory_reset();
    tcg_prologue_init();
    initialized = true;
}

static int run_program(const struct qemu_zephyr_options *options)
{
    char *argv[33] = {0};
    char *envp[10] = {"PATH=/images", NULL};
    CPUARMState *env;
    int fd, result;

    if (!initialized) {
        initialize_user();
    }
    qemu_zephyr_user_memory_reset();
    tb_flush__exclusive_or_serial();
    while (syminfos != NULL) {
        struct syminfo *entry = syminfos;

        syminfos = entry->next;
        g_free((void *)entry->disas_strtab);
        g_free(entry->disas_symtab.elf64);
        g_free(entry);
    }
    cpu_reset(user_cpu);
    memset(&image_info, 0, sizeof(image_info));
    memset(&binary, 0, sizeof(binary));
    for (int i = 0; i < options->argc; i++) {
        argv[i] = (char *)options->strings + options->argv[i];
    }
    for (int i = 0; i < options->envc; i++) {
        envp[i + 1] = (char *)options->strings + options->envp[i];
    }
    exec_path = argv[0];
    pstrcpy(real_exec_path, sizeof(real_exec_path), exec_path);
    fd = open(exec_path, O_RDONLY);
    if (fd < 0) {
        return -errno;
    }
    binary.src.fd = fd;
    result = loader_exec(fd, exec_path, argv, envp, &image_info, &binary);
    if (result != 0) {
        close(fd);
    }
    binary.src.fd = -1;
    if (result != 0) {
        return result;
    }
    qemu_zephyr_user_syscall_reset(image_info.brk);
    env = cpu_env(user_cpu);
    env->pc = image_info.entry & ~3ULL;
    env->xregs[31] = image_info.start_stack;
    user_exited = false;
    user_exit_status = 0;
    user_trace = options->trace;
    qatomic_set(&user_stop_requested, false);
    qatomic_set(&has_run, true);
    qemu_zephyr_uart_open();
    k_timer_start(&user_slice, K_MSEC(1), K_MSEC(1));
    while (!user_exited && !qatomic_read(&user_stop_requested)) {
        int trap;

        bql_unlock();
        cpu_exec_start(user_cpu);
        trap = cpu_exec(user_cpu);
        cpu_exec_end(user_cpu);
        qemu_process_cpu_events(user_cpu);
        bql_lock();
        if (trap == EXCP_SWI) {
            env->xregs[0] = do_syscall(env, env->xregs[8], env->xregs[0],
                                     env->xregs[1], env->xregs[2], env->xregs[3],
                                     env->xregs[4], env->xregs[5], 0, 0);
        } else if (trap == EXCP_ATOMIC) {
            bql_unlock();
            cpu_exec_step_atomic(user_cpu);
            bql_lock();
        } else if (trap != EXCP_INTERRUPT) {
            fprintf(stderr, "qemu-aarch64: unhandled exception %d at 0x%" PRIx64 "\n",
                    trap, env->pc);
            user_exit_status = 128 + TARGET_SIGILL;
            user_exited = true;
        }
        qemu_zephyr_quiesce();
        k_yield();
    }
    k_timer_stop(&user_slice);
    qemu_zephyr_user_syscall_close();
    return user_stop_requested ? 128 + TARGET_SIGINT : user_exit_status;
}

int qemu_zephyr_run(const struct qemu_zephyr_options *options)
{
    jmp_buf exit_env;
    int result;

    qemu_zephyr_exit_env = &exit_env;
    if (setjmp(exit_env) == 0) {
        result = run_program(options);
    } else {
        result = qemu_zephyr_exit_status > 0 ? -qemu_zephyr_exit_status :
                 qemu_zephyr_exit_status < 0 ? qemu_zephyr_exit_status : -ENOEXEC;
        k_timer_stop(&user_slice);
        if (binary.src.fd >= 0) {
            close(binary.src.fd);
            binary.src.fd = -1;
        }
        qemu_zephyr_user_syscall_close();
    }
    qemu_zephyr_exit_env = NULL;
    return result;
}

int qemu_zephyr_parse_options(size_t argc, char **argv,
                             struct qemu_zephyr_options *options,
                             char *error, size_t error_size)
{
    size_t used = 0;
    bool program = false;

    memset(options, 0, sizeof(*options));
    pstrcpy(options->cpu, sizeof(options->cpu), CONFIG_QEMU_CPU_MODEL);
    for (size_t i = 1; i < argc; i++) {
        bool environment = false;
        size_t length;

        if (!program && (!strcmp(argv[i], "-help") || !strcmp(argv[i], "-h"))) {
            printf("qemu-aarch64 [-cpu %s] [-strace] [-E NAME=VALUE] /images/PROGRAM [ARG...]\n",
                   CONFIG_QEMU_CPU_MODEL);
            return 1;
        }
        if (!program && !strcmp(argv[i], "-strace")) {
            options->trace = true;
            continue;
        }
        if (!program && !strcmp(argv[i], "-cpu")) {
            if (++i == argc || strcmp(argv[i], CONFIG_QEMU_CPU_MODEL)) {
                snprintf(error, error_size, "This firmware uses -cpu %s", CONFIG_QEMU_CPU_MODEL);
                return -EINVAL;
            }
            continue;
        }
        if (!program && !strcmp(argv[i], "-E")) {
            if (++i == argc || strchr(argv[i], '=') == NULL ||
                argv[i][0] == '=' || options->envc == 8) {
                snprintf(error, error_size, "Use up to eight -E NAME=VALUE options");
                return -EINVAL;
            }
            environment = true;
        } else if (!program) {
            if (argv[i][0] != '/') {
                snprintf(error, error_size, "Expected an absolute program path; use -help");
                return -EINVAL;
            }
            program = true;
        }
        length = strlen(argv[i]) + 1;
        if (used + length > sizeof(options->strings) || options->argc == 32) {
            snprintf(error, error_size, "Program arguments exceed command capacity");
            return -E2BIG;
        }
        if (environment) {
            options->envp[options->envc++] = used;
        } else {
            options->argv[options->argc++] = used;
        }
        memcpy(options->strings + used, argv[i], length);
        used += length;
    }
    if (options->argc == 0) {
        snprintf(error, error_size, "Specify a Linux AArch64 ELF program; use -help");
        return -EINVAL;
    }
    struct stat st;

    if (stat(options->strings + options->argv[0], &st) != 0) {
        snprintf(error, error_size, "Cannot open program: %s", strerror(errno));
        return -errno;
    }
    return 0;
}
