/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Relocatable PE32+ ARM64 native diagnostic. Linked to the wrong image base
 * deliberately. It can only print success if IMAGE_REL_BASED_DIR64 is applied. */
.text
.balign 4
.globl pe_native_reloc_start
pe_native_reloc_start:
    ldr w7, [x0]
    movz w8, #0x0dd0
    movk w8, #0xedfe, lsl #16
    cmp w7, w8
    b.ne reloc_bad_fdt
    adrp x5, reloc_message_ptr
    add x5, x5, :lo12:reloc_message_ptr
    ldr x2, [x5]
    b reloc_print
reloc_bad_fdt:
    adr x2, reloc_bad_message
reloc_print:
    ldr x1, =0x09000000
reloc_next:
    ldrb w3, [x2], #1
    cbz w3, reloc_halt
reloc_wait:
    ldr w4, [x1, #0x18]
    tbnz w4, #5, reloc_wait
    str w3, [x1]
    b reloc_next
reloc_halt:
    wfe
    b reloc_halt
reloc_message:
    .asciz "\r\n[legacy] RELOCATED ARM64 PE32+ STAGE2 OK (no EFI, not Windows)\r\n"
reloc_bad_message:
    .asciz "\r\n[legacy] RELOCATED PE32+ INVALID DEVICE TREE\r\n"
.section .rdata, "dr"
.balign 8
reloc_message_ptr:
    .quad reloc_message
