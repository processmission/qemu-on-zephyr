/* SPDX-License-Identifier: Apache-2.0 */
#include <stddef.h>
#include <string.h>

static int write_text(const char *text)
{
    size_t remaining = strlen(text);

    while (remaining != 0) {
        register long result __asm__("x0") = 1;
        register const char *buffer __asm__("x1") = text;
        register size_t length __asm__("x2") = remaining;
        register long number __asm__("x8") = 64;

        __asm__ volatile("svc #0" : "+r"(result)
                         : "r"(buffer), "r"(length), "r"(number) : "memory", "cc");
        if (result <= 0) {
            return 1;
        }
        text += result;
        remaining -= result;
    }
    return 0;
}

int hello_main(int argc, char **argv, char **envp)
{
    if (write_text("Hello from Linux AArch64 on Zephyr!\n") != 0) {
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        if (write_text("argument: ") || write_text(argv[i]) || write_text("\n")) {
            return 1;
        }
    }
    for (char **entry = envp; *entry != NULL; entry++) {
        if (strncmp(*entry, "MESSAGE=", 8) == 0) {
            return write_text("MESSAGE: ") || write_text(*entry + 8) || write_text("\n");
        }
    }
    return 0;
}
