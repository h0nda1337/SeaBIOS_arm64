/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Synthetic import-free PE32+ (native subsystem), not Microsoft software. */
.text
.balign 4
.globl pe_native_start
pe_native_start:
    ldr w7, [x0]
    movz w8, #0x0dd0
    movk w8, #0xedfe, lsl #16
    cmp w7, w8
    b.ne pe_bad_fdt
    adr x2, pe_message
    b pe_print
pe_bad_fdt:
    adr x2, pe_bad_message
pe_print:
    ldr x1, =0x09000000
pe_next:
    ldrb w3, [x2], #1
    cbz w3, pe_halt
pe_wait:
    ldr w4, [x1, #0x18]
    tbnz w4, #5, pe_wait
    str w3, [x1]
    b pe_next
pe_halt:
    wfe
    b pe_halt
pe_message:
    .asciz "\r\n[legacy] NATIVE ARM64 PE32+ STAGE2 OK (not Windows)\r\n"
pe_bad_message:
    .asciz "\r\n[legacy] NATIVE PE32+ INVALID DEVICE TREE\r\n"
