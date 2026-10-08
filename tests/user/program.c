/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include <stddef.h>

static long call(long number, long a, long b, long c, long d, long e, long f)
{
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x4 __asm__("x4") = e;
    register long x5 __asm__("x5") = f;
    register long x8 __asm__("x8") = number;

    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3),
                     "r"(x4), "r"(x5), "r"(x8) : "memory", "cc");
    return x0;
}

static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void output(const char *text)
{
    size_t length = 0;
    while (text[length]) { length++; }
    call(64, 1, (long)text, length, 0, 0, 0);
}

static volatile uint64_t initialized = 0x1122334455667788ULL;
static volatile uint64_t zeroed[16];

#define REQUIRE(value) do { if (!(value)) { output("USER_FAIL " #value "\n"); return 1; } } while (0)

int user_main(long argc, char **argv, char **envp)
{
    long page;

    REQUIRE(argc >= 2);
    if (equal(argv[1], "wait")) {
        output("USER_WAIT\n");
        for (;;) { __asm__ volatile("nop"); }
    }
    if (equal(argv[1], "fault")) {
        return *(volatile int *)(uintptr_t)0xfffffffffffffff0ULL;
    }
    if (equal(argv[1], "readonly") || equal(argv[1], "readonly-zero")) {
        page = call(222, 0, 4096, 3, 0x22, -1, 0);
        REQUIRE(page > 0);
        REQUIRE(call(226, page, 4096, 1, 0, 0, 0) == 0);
        if (equal(argv[1], "readonly-zero")) {
            __asm__ volatile("dc zva, %0" : : "r"(page) : "memory");
        }
        *(volatile long *)page = 1;
        return 1;
    }
    if (equal(argv[1], "unmapped")) {
        page = call(222, 0, 4096, 3, 0x22, -1, 0);
        REQUIRE(page > 0);
        REQUIRE(call(215, page, 4096, 0, 0, 0, 0) == 0);
        return *(volatile int *)page;
    }
    REQUIRE(argc == 3 && equal(argv[2], "two words"));
    REQUIRE(initialized == 0x1122334455667788ULL);
    REQUIRE(zeroed[0] == 0 && zeroed[15] == 0);
    int found = 0;
    for (char **entry = envp; *entry; entry++) {
        found |= equal(*entry, "QOZ_USER=yes");
    }
    REQUIRE(found);
    char buffer[128] = {0};
    long fd = call(56, -100, (long)"/images/message.txt", 0, 0, 0, 0);
    REQUIRE(fd >= 3);
    REQUIRE(call(63, fd, (long)buffer, 127, 0, 0, 0) == 18);
    REQUIRE(equal(buffer, "Zephyr filesystem\n"));
    REQUIRE(call(62, fd, 0, 0, 0, 0, 0) == 0);
    page = call(222, 0, 4096, 1, 2, fd, 0);
    REQUIRE(page > 0 && equal((char *)page, "Zephyr filesystem\n"));
    REQUIRE(call(215, page, 4096, 0, 0, 0, 0) == 0);
    REQUIRE(call(57, fd, 0, 0, 0, 0, 0) == 0);
    REQUIRE(call(63, fd, (long)buffer, 1, 0, 0, 0) == -9);
    REQUIRE(call(56, -100, (long)"/images/missing", 0, 0, 0, 0) == -2);
    REQUIRE(call(64, 1, -1, 1, 0, 0, 0) == -14);
    REQUIRE(call(9999, 0, 0, 0, 0, 0, 0) == -38);
    long brk = call(214, 0, 0, 0, 0, 0, 0);
    REQUIRE(brk > 0);
    REQUIRE(call(214, brk + 8192, 0, 0, 0, 0, 0) == brk + 8192);
    *(volatile long *)(brk + 4096) = 12345;
    REQUIRE(*(volatile long *)(brk + 4096) == 12345);
    page = call(222, 0, 4096, 7, 0x22, -1, 0);
    REQUIRE(page > 0);
    *(volatile long *)page = 12345;
    __asm__ volatile("dc zva, %0" : : "r"(page) : "memory");
    REQUIRE(*(volatile long *)page == 0);
    volatile uint32_t *code = (volatile uint32_t *)page;
    code[0] = 0xd28002a0;
    code[1] = 0xd65f03c0;
    REQUIRE(((long (*)(void))page)() == 21);
    code[0] = 0xd2800540;
    REQUIRE(((long (*)(void))page)() == 42);
    REQUIRE(call(215, page, 4096, 0, 0, 0, 0) == 0);
    long before[2], after[2], delay[2] = {0, 20000000};
    REQUIRE(call(113, 1, (long)before, 0, 0, 0, 0) == 0);
    REQUIRE(call(101, (long)delay, 0, 0, 0, 0, 0) == 0);
    REQUIRE(call(113, 1, (long)after, 0, 0, 0, 0) == 0);
    REQUIRE((after[0] - before[0]) * 1000000000 + after[1] - before[1] >= 20000000);
    uint64_t entropy[2];
    REQUIRE(call(278, (long)entropy, sizeof(entropy), 0, 0, 0, 0) == sizeof(entropy));
    REQUIRE(entropy[0] != entropy[1]);
    REQUIRE(call(172, 0, 0, 0, 0, 0, 0) == call(178, 0, 0, 0, 0, 0, 0));
    REQUIRE(call(172, 0, 0, 0, 0, 0, 0) > 0);
    output("USER_OK argv env data bss files errno brk mmap smc clock entropy\n");
    return 7;
}
