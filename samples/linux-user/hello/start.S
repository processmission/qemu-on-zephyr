/* SPDX-License-Identifier: Apache-2.0 */
.section .text.start
.global _start
_start:
    ldr x0, [sp]
    add x1, sp, #8
    add x2, x1, x0, lsl #3
    add x2, x2, #8
    bl hello_main
    mov x8, #94
    svc #0
    brk #0
