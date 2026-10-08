/* SPDX-License-Identifier: Apache-2.0 */
ENTRY(_start)
PHDRS
{
    code PT_LOAD FILEHDR PHDRS FLAGS(5);
}
SECTIONS
{
    . = 0x400000 + SIZEOF_HEADERS;
    .text : { *(.text.start) *(.text*) *(.rodata*) } :code
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
