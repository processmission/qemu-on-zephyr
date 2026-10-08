/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu.h"
#include "user-mmap.h"
#include "user-internals.h"
#include "user/cpu_loop.h"
#include "exec/mmap-lock.h"
#include "exec/target_page.h"
#include "qemu/error-report.h"

static uint8_t process_memory[CONFIG_QEMU_USER_MEMORY_MIB * 1024 * 1024]
    __attribute__((aligned(CONFIG_MMU_PAGE_SIZE)));
uintptr_t guest_base;
bool have_guest_base = true;
unsigned long reserved_va = sizeof(process_memory) - 1;
unsigned long guest_addr_max = sizeof(process_memory) - 1;
abi_ulong task_unmapped_base = sizeof(process_memory) / 2;
abi_ulong elf_et_dyn_base = sizeof(process_memory) / 8;
abi_ulong mmap_next_start;
static unsigned int mmap_depth;

void mmap_lock(void)
{
    assert(thread_cpu == current_cpu);
    mmap_depth++;
}

void mmap_unlock(void)
{
    assert(mmap_depth > 0);
    mmap_depth--;
}

bool have_mmap_lock(void)
{
    return mmap_depth != 0;
}

void qemu_zephyr_user_memory_reset(void)
{
    guest_base = (uintptr_t)process_memory;
    mmap_depth = 0;
    mmap_next_start = task_unmapped_base;
    mmap_lock();
    page_set_flags(0, guest_addr_max, 0, -1);
    mmap_unlock();
}

static bool valid_mapping(abi_ulong start, abi_ulong length)
{
    return start >= TARGET_PAGE_SIZE &&
           (start & ~TARGET_PAGE_MASK) == 0 && length != 0 &&
           length <= sizeof(process_memory) &&
           start <= sizeof(process_memory) - length;
}

abi_ulong mmap_find_vma(abi_ulong start, abi_ulong size, abi_ulong alignment)
{
    if (start > guest_addr_max) {
        return -1;
    }
    return page_find_range_empty(MAX(start, TARGET_PAGE_SIZE), guest_addr_max,
                                 size, MAX(alignment, TARGET_PAGE_SIZE));
}

abi_long target_mmap(abi_ulong start, abi_ulong len, int prot,
                     int flags, int fd, off_t offset)
{
    abi_ulong address;
    size_t used = 0;
    g_autofree uint8_t *file_data = NULL;
    int supported = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED |
                    MAP_FIXED_NOREPLACE | MAP_NORESERVE;

    if (len == 0 || len > sizeof(process_memory) || offset < 0 ||
        (offset & ~TARGET_PAGE_MASK) != 0 || (flags & ~supported) != 0 ||
        (flags & MAP_PRIVATE) == 0 || (prot & ~7) != 0) {
        errno = EINVAL;
        return -1;
    }
    len = TARGET_PAGE_ALIGN(len);
    if (!(flags & MAP_ANONYMOUS)) {
        if ((uint64_t)offset > INT64_MAX - len) {
            errno = EOVERFLOW;
            return -1;
        }
        file_data = g_try_malloc0(len);
        if (file_data == NULL) {
            errno = ENOMEM;
            return -1;
        }
        while (used < len) {
            ssize_t result = pread(fd, file_data + used, len - used, offset + used);

            if (result < 0) {
                return -1;
            }
            if (result == 0) {
                break;
            }
            used += result;
        }
    }
    mmap_lock();
    if (!(flags & (MAP_FIXED | MAP_FIXED_NOREPLACE))) {
        address = mmap_find_vma(start ? start : mmap_next_start, len,
                                TARGET_PAGE_SIZE);
        if (address == -1) {
            address = mmap_find_vma(TARGET_PAGE_SIZE, len, TARGET_PAGE_SIZE);
        }
        start = address;
    }
    if (!valid_mapping(start, len)) {
        errno = ENOMEM;
        goto fail;
    }
    if (flags & MAP_FIXED_NOREPLACE) {
        for (address = start; address < start + len; address += TARGET_PAGE_SIZE) {
            if (page_get_flags(address) & PAGE_VALID) {
                errno = EEXIST;
                goto fail;
            }
        }
    }
    /* A private file mapping is a snapshot, independent of its descriptor. */
    if (file_data != NULL) {
        memcpy(g2h_untagged(start), file_data, len);
    } else {
        memset(g2h_untagged(start), 0, len);
    }
    page_set_flags(start, start + len - 1, PAGE_VALID | prot, -1);
    if (!(flags & (MAP_FIXED | MAP_FIXED_NOREPLACE))) {
        mmap_next_start = start + len;
    }
    mmap_unlock();
    return start;
fail:
    mmap_unlock();
    return -1;
}

int target_mprotect(abi_ulong start, abi_ulong len, int prot)
{
    if (len > sizeof(process_memory) || !valid_mapping(start, TARGET_PAGE_ALIGN(len)) ||
        (prot & ~7) != 0) {
        errno = EINVAL;
        return -1;
    }
    len = TARGET_PAGE_ALIGN(len);
    mmap_lock();
    if (!page_check_range(start, len, PAGE_VALID)) {
        mmap_unlock();
        errno = ENOMEM;
        return -1;
    }
    page_set_flags(start, start + len - 1, PAGE_VALID | prot, -1);
    mmap_unlock();
    return 0;
}

int target_munmap(abi_ulong start, abi_ulong len)
{
    if (len > sizeof(process_memory) || !valid_mapping(start, TARGET_PAGE_ALIGN(len))) {
        errno = EINVAL;
        return -1;
    }
    mmap_lock();
    page_set_flags(start, start + TARGET_PAGE_ALIGN(len) - 1, 0, -1);
    mmap_unlock();
    return 0;
}

void probe_guest_base(const char *name, const PGBRange *range, const PGBRange *commpage)
{
    assert(commpage == NULL);
    if (range != NULL && (range->lo < TARGET_PAGE_SIZE || range->hi > guest_addr_max)) {
        error_report("%s: ELF mappings exceed the %u MiB process address space",
                     name, CONFIG_QEMU_USER_MEMORY_MIB);
        exit(ENOEXEC);
    }
}

void qemu_zephyr_user_probe(CPUState *cpu, uint64_t address, int size,
                            int access, uintptr_t retaddr)
{
    vaddr start = cpu_untagged_addr_vaddr(cpu, address);
    int needed = access == MMU_DATA_STORE ? PAGE_WRITE_ORG :
                 access == MMU_INST_FETCH ? PAGE_EXEC : PAGE_READ;
    vaddr last;

    if (size == 0) {
        return;
    }
    if (!guest_range_valid_untagged_vaddr(start, size)) {
        cpu_loop_exit_sigsegv(cpu, address, access, true, retaddr);
    }
    last = start + size - 1;
    for (vaddr page = start & TARGET_PAGE_MASK; page <= last; page += TARGET_PAGE_SIZE) {
        int flags = page_get_flags(page);

        if (!(flags & needed)) {
            cpu_loop_exit_sigsegv(cpu, address, access, !(flags & PAGE_VALID), retaddr);
        }
        if (access == MMU_DATA_STORE && !(flags & PAGE_WRITE) &&
            page_unprotect(retaddr ? cpu : NULL, page, retaddr) == 2) {
            cpu_loop_exit_noexc(cpu);
        }
    }
}
