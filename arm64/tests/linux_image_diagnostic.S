/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Synthetic standard arm64 Linux Image header to TEST raw kernel handoff.
 * This is NOT a Linux kernel, and must never be advertised as one. */
.section .text, "ax"
.balign 4
.global synthetic_image_start
synthetic_image_start:
    b synthetic_main
    .word 0
    .quad 0x80000 /* text_offset */
    .quad 16384   /* image_size: 32 sectors, two 16-sector requests */
    .quad 0       /* little-endian, any page size */
    .quad 0
    .quad 0
    .quad 0
    .word 0x644d5241  /* ARM\x64 */
    .word 0
synthetic_main:
    /* Confirm the bootloader actually handed us an FDT in x0, rather than
     * simply branching to a synthetic ELF with no protocol setup. */
    ldr w7, [x0]
    movz w8, #0x0dd0
    movk w8, #0xedfe, lsl #16
    cmp w7, w8
    b.ne .Lbadfdt
    /* Check a byte at the very last offset of the 32-sector test Image.
     * This forces the test to verify the second 16-sector VirtIO transfer. */
    adr x5, .Ltail
    ldrb w6, [x5]
    cmp w6, #0x51
    b.ne .Lbad
    ldr x1, =0x09000000
    adr x2, .Lmsg
    b .Lloop
.Lbadfdt:
    ldr x1, =0x09000000
    adr x2, .Lbadfdtmsg
    b .Lloop
.Lbad:
    ldr x1, =0x09000000
    adr x2, .Lbadmsg
.Lloop:
    ldrb w3, [x2], #1
    cbz w3, .Lwait
.Ltx:
    ldr w4, [x1, #0x18]
    tbnz w4, #5, .Ltx
    str w3, [x1]
    b .Lloop
.Lwait:
    wfe
    b .Lwait
.Lmsg:
    .asciz "\r\n[linux] SYNTHETIC IMAGE HANDOFF OK (not Linux; tail verified)\r\n"
.Lbadfdtmsg:
    .asciz "\r\n[linux] SYNTHETIC IMAGE INVALID FDT HANDOFF\r\n"
.Lbadmsg:
    .asciz "\r\n[linux] SYNTHETIC IMAGE TAIL CORRUPT\r\n"
    .org 16383
.Ltail:
    .byte 0x51
