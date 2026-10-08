/* SPDX-License-Identifier: Apache-2.0 */
.section .text.boot, "ax"
.global _start
_start:
    ldr x1, =0x09000000
    mov w2, #0x301
    str w2, [x1, #0x30]
    mrs x3, CurrentEL
    cmp x3, #4
    b.ne failed
    ldr x3, =firmware_data
    ldr x4, [x3]
    ldr x5, =0x123456789abcdef0
    cmp x4, x5
    b.ne failed
    ldr x3, =firmware_bss
    ldr x4, [x3]
    cbnz x4, failed
    adr x2, success
    b print
failed:
    adr x2, failure
print:
    ldrb w3, [x2], #1
    cbz w3, poweroff
wait_uart:
    ldr w4, [x1, #0x18]
    tbnz w4, #5, wait_uart
    str w3, [x1]
    b print
poweroff:
#ifndef FIRMWARE_WAIT
    movz x0, #8
    movk x0, #0x8400, lsl #16
    hvc #0
#endif
1:
    wfi
    b 1b
success:
    .asciz "QEMU_FIRMWARE_OK EL1 DATA BSS\n"
failure:
    .asciz "QEMU_FIRMWARE_FAILED\n"

.section .data, "aw"
.balign 8
firmware_data:
    .quad 0x123456789abcdef0
.section .bss, "aw", %nobits
.balign 8
firmware_bss:
    .skip 8
