/* SPDX-License-Identifier: Apache-2.0 */
ENTRY(_start)
PHDRS
{
    code PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}
SECTIONS
{
    . = 0x40000000;
    .text : { *(.text.boot) *(.text*) *(.rodata*) } :code
    . = 0x40100000;
    .data : { *(.data*) } :data
    .bss (NOLOAD) : { *(.bss*) } :data
    /DISCARD/ : { *(.comment) *(.note*) }
}
