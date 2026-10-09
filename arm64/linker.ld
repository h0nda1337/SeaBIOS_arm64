/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* QEMU virt flash starts at 0; SRAM/RAM window for private writable state. */
OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_start)
MEMORY {
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 0x00400000
    RAM   (rwx) : ORIGIN = 0x40200000, LENGTH = 0x00200000
}
SECTIONS {
    .text : {
        KEEP(*(.text.boot))
        *(.text .text.*)
    } > FLASH
    .rodata : { *(.rodata .rodata.*) } > FLASH
    . = ALIGN(16);
    .data : {
        __data_begin = .;
        *(.data .data.*)
        . = ALIGN(16);
        __data_end = .;
    } > RAM AT> FLASH
    __data_load = LOADADDR(.data);
    .bss (NOLOAD) : {
        __bss_begin = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(16);
        __bss_end = .;
    } > RAM
    .stack (NOLOAD) : {
        . = ALIGN(16);
        __stack_begin = .;
        . = . + 16384;
        __stack_top = .;
    } > RAM
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
ASSERT(SIZEOF(.text) + SIZEOF(.rodata) + SIZEOF(.data) < LENGTH(FLASH),
       "ARM64 firmware exceeds QEMU flash allocation")
